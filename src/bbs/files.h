#pragma once
// File areas, found on the card: every folder in /BBS/FILES is an area and
// its sub-folders can be browsed. This file has the areas, a folder's
// entries, download counts, descriptions and the listing; ZIPs are in
// bbs/zip.h and the viewers in bbs/viewers.h.
// Part of the single TinyBBS translation unit: included in order from main.cpp.

static constexpr const char *FILES_ROOT = "/BBS/FILES";
static constexpr uint16_t FILES_PER_PAGE = 16;

struct FileArea {
  String folder, name, flag;  // folder under /BBS/FILES, e.g. 014_BACKROOM_IMAGES
  uint8_t level = 0;
  bool downloads = true;
  bool uploads = false;       // browser callers may upload here (AREA.INI uploads=1)
  uint16_t files = 0;         // files in the area and its sub-folders
};
static std::vector<FileArea> fileAreas;

static void fileMenu(Session &s);
static void doorMenu(Session &s);
static String shortName(const String &path) { int slash = path.lastIndexOf('/'); return slash >= 0 ? path.substring(slash + 1) : path; }
// Names a caller never sees in a listing: hidden entries and description files.
static bool hiddenEntry(const String &name) {
  String u = up(name);
  return !name.length() || name[0] == '_' || name[0] == '.' || u == "FILES.BBS" || u == "DESCRIPT.ION" || u == "AREA.INI";
}
// "014_BACKROOM_IMAGES" -> "Backroom Images".
static String areaTitle(const String &folder) {
  int start = 0;
  while (start < (int)folder.length() && isdigit((unsigned char)folder[start])) start++;
  while (start < (int)folder.length() && (folder[start] == '_' || folder[start] == ' ' || folder[start] == '-')) start++;
  String out; bool first = true;
  for (int i = start; i < (int)folder.length(); ++i) {
    char c = folder[i];
    if (c == '_') { out += ' '; first = true; continue; }
    out += first ? (char)toupper((unsigned char)c) : (char)tolower((unsigned char)c);
    first = c == ' ';
  }
  return out.length() ? out : folder;
}
static uint16_t countFiles(const String &path, uint8_t depth = 0) {
  File dir = SD_MMC.open(path);
  if (!dir || !dir.isDirectory()) return 0;
  uint16_t n = 0;
  File e;
  while ((e = dir.openNextFile())) {
    String name = e.name();
    bool sub = e.isDirectory();
    e.close();
    if (hiddenEntry(name)) continue;
    if (sub) { if (depth < 8) n += countFiles(path + "/" + name, depth + 1); }
    else if (n < 60000) n++;
  }
  dir.close();
  return n;
}
// FILEAREAS.INI held the area settings before areas came from folders. Its
// values are written once into an AREA.INI in each folder, then the old file
// is renamed, so a Back Room stays locked after the upgrade.
static void migrateAreaIni() {
  Ini old;
  if (!SD_MMC.exists("/BBS/CONFIG/FILEAREAS.INI") || !iniLoad("/BBS/CONFIG/FILEAREAS.INI", old)) return;
  for (const auto &sec : iniSections(old)) {
    String folder = shortName(iniGet(old, sec, "path"));
    String dir = String(FILES_ROOT) + "/" + folder;
    if (!folder.length() || !SD_MMC.exists(dir) || SD_MMC.exists(dir + "/AREA.INI")) continue;
    File f = SD_MMC.open(dir + "/AREA.INI", FILE_WRITE);
    if (!f) continue;
    f.print("; File area settings. Edit, then RELOAD as SysOp.\n[AREA]\n");
    f.print("; Shown in the file menu. Empty = made from the folder name.\nname=" + iniGet(old, sec, "name") + "\n");
    f.print("; Lowest security level that sees this area.\nmin_security=" + iniGet(old, sec, "minimum_security", "0") + "\n");
    f.print("; A user flag the caller must also have, e.g. BACKROOM. Empty = none.\nrequired_flag=" + iniGet(old, sec, "required_flag") + "\n");
    f.print("; 0 = files can be viewed but not downloaded.\ndownloads=" + iniGet(old, sec, "download_allowed", "1") + "\n");
    f.print("; 1 = browser callers may upload here, for the SysOp to approve.\nuploads=0\n");
    f.close();
  }
  SD_MMC.rename("/BBS/CONFIG/FILEAREAS.INI", "/BBS/CONFIG/FILEAREAS.OLD");
  event("FILEAREAS", "settings moved into AREA.INI files");
}
// Called from loadSettings(): at boot and on RELOAD.
static String loadFileAreas() {
  fileAreas.clear();
  if (!sdReady) return "no file areas";
  migrateAreaIni();
  std::vector<String> folders;
  File root = SD_MMC.open(FILES_ROOT);
  File e;
  while (root && (e = root.openNextFile())) {
    String name = e.name();
    if (e.isDirectory() && !hiddenEntry(name)) folders.push_back(name);
    e.close();
  }
  if (root) root.close();
  std::sort(folders.begin(), folders.end(), [](const String &a, const String &b) { return up(a) < up(b); });
  for (const auto &folder : folders) {
    FileArea a;
    a.folder = folder;
    a.name = areaTitle(folder);
    Ini ini;
    if (iniLoad((String(FILES_ROOT) + "/" + folder + "/AREA.INI").c_str(), ini)) {
      String name = iniGet(ini, "AREA", "name");
      if (name.length()) a.name = name;
      a.level = (uint8_t)constrain(iniGet(ini, "AREA", "min_security", "0").toInt(), 0L, 255L);
      a.flag = up(iniGet(ini, "AREA", "required_flag"));
      a.downloads = iniGet(ini, "AREA", "downloads", "1").toInt() != 0;
      a.uploads = iniGet(ini, "AREA", "uploads", "0").toInt() != 0;
    }
    a.files = countFiles(String(FILES_ROOT) + "/" + folder);
    fileAreas.push_back(a);
  }
  // Areas behind a flag (the back room) always come last in the file menu.
  std::stable_sort(fileAreas.begin(), fileAreas.end(), [](const FileArea &x, const FileArea &y) { return !x.flag.length() && y.flag.length(); });
  return String(fileAreas.size()) + " file areas";
}
static bool canUseArea(const Session &s, uint8_t area) {
  if (area >= fileAreas.size()) return false;
  const FileArea &a = fileAreas[area];
  return s.user.level >= a.level && (!a.flag.length() || hasFlag(s.user, a.flag));
}
// Areas listed in the file menu: ones this caller may enter that have files.
// An empty area that takes uploads is listed for browser callers, who can fill it.
static bool listedArea(const Session &s, uint8_t area) {
  return canUseArea(s, area) && (fileAreas[area].files || (s.web && fileAreas[area].uploads && board.uploadMax));
}
static String areaDir(const Session &s) {
  String dir = String(FILES_ROOT) + "/" + fileAreas[s.fileArea].folder;
  return s.filePath.length() ? dir + "/" + s.filePath : dir;
}

// ---------- one folder ------------------------------------------------------------
struct FileEntry { String name; uint32_t size = 0; bool dir = false; time_t when = 0; };
// The folder's entries in listing order: sub-folders first, then files, by name.
static std::vector<FileEntry> folderEntries(const Session &s) {
  std::vector<FileEntry> list;
  if (!sdReady || !canUseArea(s, s.fileArea)) return list;
  File dir = SD_MMC.open(areaDir(s));
  File e;
  while (dir && dir.isDirectory() && (e = dir.openNextFile())) {
    FileEntry f; f.name = e.name(); f.dir = e.isDirectory(); f.size = f.dir ? 0 : e.size(); f.when = e.getLastWrite();
    e.close();
    if (!hiddenEntry(f.name)) list.push_back(f);
  }
  if (dir) dir.close();
  std::sort(list.begin(), list.end(), [](const FileEntry &a, const FileEntry &b) {
    return a.dir != b.dir ? a.dir : up(a.name) < up(b.name);
  });
  return list;
}
// ---------- download counts --------------------------------------------------------
// One line per file that has been downloaded: "path|count", the path under
// /BBS/FILES. Kept in memory once read; rewritten on each download.
static constexpr const char *DL_COUNTS = "/BBS/DATA/INDEX/DOWNLOADS.DAT";
static std::vector<std::pair<String, uint16_t>> dlCounts;
static bool dlLoaded = false;
static void loadDownloadCounts() {
  if (dlLoaded || !sdReady) return;
  dlLoaded = true;
  File f = SD_MMC.open(DL_COUNTS, FILE_READ);
  while (f && f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length()) dlCounts.push_back({dec(field(line, 0)), (uint16_t)field(line, 1).toInt()});
  }
  if (f) f.close();
}
static String countKey(const String &path) { return path.startsWith(FILES_ROOT) ? path.substring(strlen(FILES_ROOT) + 1) : path; }
static uint16_t downloadCount(const String &path) {
  loadDownloadCounts();
  String key = countKey(path);
  for (const auto &c : dlCounts) if (c.first == key) return c.second;
  return 0;
}
static void countDownload(const String &path) {
  loadDownloadCounts();
  String key = countKey(path);
  bool found = false;
  for (auto &c : dlCounts) if (c.first == key) { if (c.second < 999) c.second++; found = true; }
  if (!found) dlCounts.push_back({key, 1});
  File f = SD_MMC.open(DL_COUNTS, FILE_WRITE);
  if (!f) return;
  for (const auto &c : dlCounts) f.print(enc(c.first) + "|" + c.second + "\n");
  f.close();
}
// A file's date from the card, as on a 90s listing: MM-DD-YY.
static String fileDate(time_t t) {
  if (t < 315532800) return "--------";  // before 1980: no date
  int y; unsigned m, d;
  civilFromDays((int32_t)(t / 86400), y, m, d);
  char b[12];
  snprintf(b, sizeof(b), "%02u-%02u-%02d", m, d, y % 100);
  return b;
}
static bool zipName(const String &name) { return up(name).endsWith(".ZIP"); }
// What the ratio and credits still let this caller download, in words.
static String creditText(const User &u) {
  if (!u.ratioEnforced) return "no ratio";
  uint64_t may = (uint64_t)u.uploadBytes * u.ratio + u.credits;
  uint64_t left = may > u.downloadBytes ? may - u.downloadBytes : 0;
  return sizeText((uint32_t)min<uint64_t>(left, UINT32_MAX)) + " left (ratio 1:" + String(u.ratio) + ")";
}

static bool areaFile(Session &s, uint16_t wanted, String &path, String &name, size_t &bytes) {
  auto list = folderEntries(s);
  if (!wanted || wanted > list.size() || list[wanted - 1].dir) return false;
  name = list[wanted - 1].name; path = areaDir(s) + "/" + name; bytes = list[wanted - 1].size;
  return true;
}

// ---------- descriptions ------------------------------------------------------------
// One line of description text, at most `max` screen columns. DOS text (bytes
// 128-255 that are not UTF-8) shows its CP437 line-drawing characters.
static String oneLine(String text, uint16_t max) {
  const bool utf8 = sampleUtf8((const uint8_t *)text.c_str(), text.length());
  String out; bool space = false;
  uint16_t cols = 0;
  for (size_t i = 0; i < text.length(); ++i) {
    uint8_t c = text[i];
    if (c == 4) break;  // 4DOS DESCRIPT.ION: extended data follows
    if (utf8 && (c & 0xc0) == 0x80) { if (cols <= max) out += (char)c; continue; }  // rest of a UTF-8 character
    if (c < 32 || c == ' ') { space = cols > 0; continue; }
    if (cols + (space ? 1 : 0) >= max) { cols = max + 1; continue; }
    if (space) { out += ' '; cols++; space = false; }
    if (c >= 128 && !utf8) appendDosGlyph(out, c); else out += (char)c;
    cols++;
  }
  return out;
}
// FILES.BBS ("NAME.EXT  description") and DESCRIPT.ION ("name description" or
// "\"long name\" description") of the current folder, for the names wanted.
static void folderDescriptions(const String &dir, std::vector<std::pair<String, String>> &want) {
  for (const char *file : {"/FILES.BBS", "/DESCRIPT.ION"}) {
    File f = SD_MMC.open(dir + file, FILE_READ);
    while (f && f.available()) {
      String line = f.readStringUntil('\n');
      if (!line.length() || line[0] == ' ' || line[0] == '\t' || line[0] == '|') continue;  // continuation lines
      String name, rest;
      if (line[0] == '"') { int q = line.indexOf('"', 1); if (q < 0) continue; name = line.substring(1, q); rest = line.substring(q + 1); }
      else { int sp = line.indexOf(' '); int tab = line.indexOf('\t'); if (tab >= 0 && (sp < 0 || tab < sp)) sp = tab; if (sp < 0) continue; name = line.substring(0, sp); rest = line.substring(sp + 1); }
      for (auto &w : want) if (!w.second.length() && up(w.first) == up(name)) w.second = oneLine(rest, 120);
    }
    if (f) f.close();
  }
}
// At most `cols` screen columns of UTF-8 text.
static String fitCols(const String &s, uint16_t cols) {
  uint16_t n = 0;
  for (size_t i = 0; i < s.length(); ++i) {
    if (((uint8_t)s[i] & 0xc0) == 0x80) continue;
    if (n++ == cols) return s.substring(0, i);
  }
  return s;
}

// ---------- listing -------------------------------------------------------------------
static void zipDetails(const String &dir, std::vector<std::pair<String, String>> &want,
                       const std::vector<uint32_t> &sizes, std::vector<time_t> &when);  // bbs/zip.h
static String sizeShort(uint32_t bytes) {
  char b[12];
  if (bytes < 1024) snprintf(b, sizeof(b), "%luB", (unsigned long)bytes);
  else if (bytes < 10UL * 1048576) snprintf(b, sizeof(b), "%luK", (unsigned long)((bytes + 1023) / 1024));
  else snprintf(b, sizeof(b), "%luM", (unsigned long)((bytes + 524288) / 1048576));
  return b;
}
static void listArea(Session &s) {
  if (!canUseArea(s, s.fileArea)) { say(s, "Access denied."); fileMenu(s); return; }
  const FileArea &a = fileAreas[s.fileArea];
  activity(s, "FILES: " + a.name);
  auto list = folderEntries(s);
  if (s.fileOffset >= list.size()) s.fileOffset = 0;
  String where = a.name;
  if (s.filePath.length()) { String p = s.filePath; p.replace("/", " / "); where += " / " + p; }
  say(s, "\r\n\x1b[1;36m+---- " + where + " ----+\x1b[0m");
  std::vector<std::pair<String, String>> want;
  std::vector<uint32_t> sizes;
  for (size_t i = s.fileOffset; i < list.size() && i < (size_t)s.fileOffset + FILES_PER_PAGE; ++i) {
    want.push_back({list[i].name, ""}); sizes.push_back(list[i].size);
  }
  String dir = areaDir(s);
  folderDescriptions(dir, want);
  std::vector<time_t> zipWhen;
  zipDetails(dir, want, sizes, zipWhen);
  if (!want.empty()) say(s, "\x1b[0;36m  #   Name               Size Date      DL Description\x1b[0m");
  for (size_t k = 0; k < want.size(); ++k) {
    const FileEntry &f = list[s.fileOffset + k];
    char row[280];  /* a DOS character is up to 3 bytes */
    String desc = want[k].second; desc.trim();
    if (f.dir) snprintf(row, sizeof(row), "\x1b[1;33m[%3u]\x1b[0m \x1b[1;36m%-16.16s  <DIR>\x1b[0m \x1b[0;32m%s\x1b[0m", (unsigned)(s.fileOffset + k + 1), f.name.c_str(), fileDate(f.when).c_str());
    else {
      uint16_t dls = downloadCount(dir + "/" + f.name);
      desc = fitCols(desc, 36);  // by columns: DOS characters take several bytes
      snprintf(row, sizeof(row), "\x1b[1;33m[%3u]\x1b[0m \x1b[1;37m%-16.16s\x1b[0m \x1b[0;36m%6s\x1b[0m \x1b[0;32m%s\x1b[0m %s%3u\x1b[0m \x1b[0;37m%s\x1b[0m",
               (unsigned)(s.fileOffset + k + 1), f.name.c_str(), sizeShort(f.size).c_str(), fileDate(zipWhen[k] ? zipWhen[k] : f.when).c_str(),
               dls ? "\x1b[1;35m" : "\x1b[0;35m", (unsigned)dls, desc.c_str());
    }
    say(s, row);
  }
  s.moreFiles = s.fileOffset + FILES_PER_PAGE < list.size();
  if (list.empty()) say(s, "This cabinet is empty tonight.");
  else say(s, "\x1b[0;36m" + String(s.fileOffset + 1) + "-" + String(s.fileOffset + want.size()) + " of " + list.size() + "\x1b[0m");
  bool dl = s.web && a.downloads;
  String keys = "[#] Open  [V #] View or list ZIP  ";
  if (dl) { keys += "[D #] Download  "; say(s, "\x1b[0;36mDownload credit: " + creditText(s.user) + "\x1b[0m"); }
  if (s.web && a.uploads && board.uploadMax) keys += "[U] Upload  ";
  if (list.size() > FILES_PER_PAGE) keys += "[N/P] Page  ";
  keys += s.filePath.length() ? "[Q] Up a folder" : "[Q] Back";
  say(s, keys);
  prompt(s, "File command? ");
  s.state = FILE_LIST;
}
static void enterFolder(Session &s, const String &name) {
  s.filePath = s.filePath.length() ? s.filePath + "/" + name : name;
  s.fileOffset = 0;
  listArea(s);
}
static void leaveFolder(Session &s) {
  int slash = s.filePath.lastIndexOf('/');
  String left = slash >= 0 ? s.filePath.substring(slash + 1) : s.filePath;
  s.filePath = slash >= 0 ? s.filePath.substring(0, slash) : String();
  // Come back to the page that shows the folder just left.
  auto list = folderEntries(s);
  s.fileOffset = 0;
  for (size_t i = 0; i < list.size(); ++i) if (list[i].dir && list[i].name == left) { s.fileOffset = i / FILES_PER_PAGE * FILES_PER_PAGE; break; }
  listArea(s);
}
