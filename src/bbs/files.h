#pragma once
// File areas, found on the card: every folder in /BBS/FILES is an area, its
// sub-folders can be browsed, and descriptions come from FILES.BBS,
// DESCRIPT.ION or the FILE_ID.DIZ inside a ZIP.
// Part of the single TinyBBS translation unit: included in order from main.cpp.

#include "miniz.h"  // the inflater in the ESP32-S3 ROM

static constexpr const char *FILES_ROOT = "/BBS/FILES";
static constexpr const char *DIZ_CACHE = "/BBS/DATA/INDEX/DIZ.DAT";
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
static String oneLine(String text, uint16_t max) {
  String out; bool space = false;
  for (size_t i = 0; i < text.length() && out.length() < max; ++i) {
    uint8_t c = text[i];
    if (c == 4) break;  // 4DOS DESCRIPT.ION: extended data follows
    if (c < 32 || c == ' ') { space = out.length() > 0; continue; }
    if (space) { out += ' '; space = false; }
    out += (char)c;
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
static uint16_t le16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return p[0] | (p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
// One stored or deflated entry of a ZIP, from its local header offset.
static bool zipInflate(File &f, uint16_t method, uint32_t csize, uint32_t usize, uint32_t local,
                       std::unique_ptr<uint8_t[]> &out, size_t &outLen) {
  if (method != 0 && method != 8) return false;
  uint8_t lh[30];
  if (!f.seek(local) || f.read(lh, 30) != 30 || le32(lh) != 0x04034b50) return false;
  if (!f.seek(local + 30 + le16(lh + 26) + le16(lh + 28))) return false;
  std::unique_ptr<uint8_t[]> in(new (std::nothrow) uint8_t[csize + 1]);
  out.reset(new (std::nothrow) uint8_t[usize + 1]);
  if (!in || !out || f.read(in.get(), csize) != csize) return false;
  outLen = usize;
  if (method == 0) { memcpy(out.get(), in.get(), min(csize, usize)); outLen = min(csize, usize); return true; }
  std::unique_ptr<tinfl_decompressor> d(new (std::nothrow) tinfl_decompressor);
  if (!d) return false;
  tinfl_init(d.get());
  size_t inLen = csize;
  return tinfl_decompress(d.get(), in.get(), &inLen, out.get(), out.get(), &outLen, TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF) == TINFL_STATUS_DONE;
}
// FILE_ID.DIZ from a ZIP's central directory, stored or deflated.
static String zipDiz(const String &path) {
  File f = SD_MMC.open(path, FILE_READ);
  if (!f) return "";
  String found;
  uint32_t size = f.size();
  uint8_t tail[512];
  uint32_t tailStart = size > sizeof(tail) ? size - sizeof(tail) : 0;
  size_t got = (f.seek(tailStart) ? f.read(tail, size - tailStart) : 0);
  int eocd = -1;
  for (int i = (int)got - 22; i >= 0; --i) if (le32(tail + i) == 0x06054b50) { eocd = i; break; }
  if (eocd >= 0) {
    uint16_t entries = le16(tail + eocd + 10);
    uint32_t cdOffset = le32(tail + eocd + 16);
    uint32_t at = cdOffset;
    for (uint16_t n = 0; n < entries && n < 400 && found.length() == 0; ++n) {
      uint8_t h[46];
      if (!f.seek(at) || f.read(h, 46) != 46 || le32(h) != 0x02014b50) break;
      uint16_t method = le16(h + 10), nameLen = le16(h + 28), extra = le16(h + 30), comment = le16(h + 32);
      uint32_t csize = le32(h + 20), usize = le32(h + 24), local = le32(h + 42);
      char name[64] = {0};
      f.read((uint8_t *)name, min<uint16_t>(nameLen, 63));
      at += 46 + nameLen + extra + comment;
      String n2 = up(shortName(String(name)));
      if (n2 != "FILE_ID.DIZ" || csize > 6000 || usize > 8000) continue;
      std::unique_ptr<uint8_t[]> out;
      size_t outLen = 0;
      if (!zipInflate(f, method, csize, usize, local, out, outLen)) break;
      String text;
      for (size_t i = 0; i < outLen; ++i) text += (char)(out[i] >= 128 ? '?' : out[i]);
      found = oneLine(text, 120);
      if (!found.length()) found = " ";
    }
  }
  f.close();
  return found;
}
// Descriptions for ZIPs come from their FILE_ID.DIZ, read once and kept in
// /BBS/DATA/INDEX/DIZ.DAT as "path|size|description".
static void zipDescriptions(const String &dir, std::vector<std::pair<String, String>> &want, const std::vector<uint32_t> &sizes) {
  std::vector<size_t> missing;
  for (size_t i = 0; i < want.size(); ++i) if (!want[i].second.length() && up(want[i].first).endsWith(".ZIP")) missing.push_back(i);
  if (missing.empty()) return;
  File c = SD_MMC.open(DIZ_CACHE, FILE_READ);
  while (c && c.available()) {
    String line = c.readStringUntil('\n');
    String path = dec(field(line, 0));
    uint32_t size = strtoul(field(line, 1).c_str(), nullptr, 10);
    for (size_t i : missing) if (path == dir + "/" + want[i].first && size == sizes[i]) want[i].second = dec(field(line, 2));
  }
  if (c) c.close();
  for (size_t i : missing) {
    if (want[i].second.length()) continue;
    String d = zipDiz(dir + "/" + want[i].first);
    if (!d.length()) d = " ";  // remember that there is none
    File w = SD_MMC.open(DIZ_CACHE, FILE_APPEND);
    if (w) { w.print(enc(dir + "/" + want[i].first) + "|" + sizes[i] + "|" + enc(d) + "\n"); w.close(); }
    want[i].second = d;
  }
}

// ---------- listing -------------------------------------------------------------------
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
  zipDescriptions(dir, want, sizes);
  if (!want.empty()) say(s, "\x1b[0;36m  #   Name               Size Date      DL Description\x1b[0m");
  for (size_t k = 0; k < want.size(); ++k) {
    const FileEntry &f = list[s.fileOffset + k];
    char row[180];
    String desc = want[k].second; desc.trim();
    if (f.dir) snprintf(row, sizeof(row), "\x1b[1;33m[%3u]\x1b[0m \x1b[1;36m%-16.16s  <DIR>\x1b[0m \x1b[0;32m%s\x1b[0m", (unsigned)(s.fileOffset + k + 1), f.name.c_str(), fileDate(f.when).c_str());
    else {
      uint16_t dls = downloadCount(dir + "/" + f.name);
      snprintf(row, sizeof(row), "\x1b[1;33m[%3u]\x1b[0m \x1b[1;37m%-16.16s\x1b[0m \x1b[0;36m%6s\x1b[0m \x1b[0;32m%s\x1b[0m %s%3u\x1b[0m \x1b[0;37m%.36s\x1b[0m",
               (unsigned)(s.fileOffset + k + 1), f.name.c_str(), sizeShort(f.size).c_str(), fileDate(f.when).c_str(),
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

// ---------- the viewer ------------------------------------------------------------------
static bool viewableName(const String &name) {
  String u = up(name);
  int dot = u.lastIndexOf('.');
  if (dot < 0) return true;  // FAQ, SITELIST: old text files had no extension
  String ext = u.substring(dot + 1);
  static const char *exts[] = {"ANS", "ANSI", "ASC", "NFO", "DIZ", "TXT", "ION", "SDI", "DOC", "ME", "1ST", "BAS", "LST", "C", "H", "PAS", "ASM", "BBS", "LOG", "FAQ", "LAW", "MAD", "MS", "OM"};
  for (const char *e : exts) if (ext == e) return true;
  return false;
}
static bool fileReaderKey(Session &s, const String &k);
static void fileReaderQuit(Session &s);
// DOS text (an NFO, a DIZ, a ZIP comment) as screen rows: CP437 glyphs, ANSI
// codes dropped, lines cut at 80 columns so art keeps its shape.
static void dosTextRows(const uint8_t *b, size_t len, std::vector<String> &rows, size_t maxRows = 400) {
  bool utf8 = sampleUtf8(b, min<size_t>(len, 768));
  String line;
  uint16_t cols = 0;
  size_t first = rows.size();
  for (size_t i = 0; i <= len && rows.size() - first < maxRows; ++i) {
    uint8_t c = i < len ? b[i] : '\n';
    if (c == 0x1a) { c = '\n'; len = i; }  // DOS end of file
    if (c == '\r') continue;
    if (c == '\n') { rows.push_back(line); line = ""; cols = 0; continue; }
    if (c == 0x1b) { while (i + 1 < len && !isalpha(b[i + 1])) ++i; ++i; continue; }
    if (c == '\t') { do { if (cols < 80) line += ' '; cols++; } while (cols % 8); continue; }
    if (utf8 && c >= 0x80) {
      if ((c & 0xc0) != 0x80) cols++;
      if (cols <= 80) line += (char)c;
      continue;
    }
    if (cols >= 80) { cols++; continue; }
    if (c < 0x20 || c >= 0x80) appendDosGlyph(line, c); else line += (char)c;
    cols++;
  }
  while (rows.size() > first && !rows.back().length()) rows.pop_back();
  while (first < rows.size() && !rows[first].length()) rows.erase(rows.begin() + first);
}
// A ZIP's contents from its central directory, without unpacking anything.
// FILE_ID.DIZ and NFO files inside are inflated and shown below the list.
static bool zipContents(const String &path, std::vector<String> &rows) {
  File f = SD_MMC.open(path, FILE_READ);
  if (!f) return false;
  uint32_t size = f.size();
  // The end record sits in the last 22 bytes plus up to 64K of comment.
  uint32_t tailLen = min<uint32_t>(size, 22 + 4096);
  std::unique_ptr<uint8_t[]> tail(new (std::nothrow) uint8_t[tailLen]);
  if (!tail || !f.seek(size - tailLen) || f.read(tail.get(), tailLen) != tailLen) { f.close(); return false; }
  int eocd = -1;
  for (int i = (int)tailLen - 22; i >= 0; --i) if (le32(tail.get() + i) == 0x06054b50) { eocd = i; break; }
  if (eocd < 0) { f.close(); return false; }
  uint16_t entries = le16(tail.get() + eocd + 10);
  uint32_t at = le32(tail.get() + eocd + 16);
  uint16_t commentLen = le16(tail.get() + eocd + 20);
  std::vector<String> commentRows;
  dosTextRows(tail.get() + eocd + 22, min<size_t>(commentLen, tailLen - eocd - 22), commentRows, 40);
  tail.reset();
  struct Inside { String name; uint16_t method; uint32_t csize, usize, local; };
  std::vector<Inside> texts;
  rows.push_back("\x1b[0;36m    Length  Packed  Ratio  Date      Time   Name\x1b[0m");
  rows.push_back("\x1b[0;36m  --------  ------  -----  --------  -----  ----\x1b[0m");
  uint64_t total = 0, packed = 0;
  uint16_t files = 0;
  for (uint16_t n = 0; n < entries && n < 1500; ++n) {
    uint8_t h[46];
    if (!f.seek(at) || f.read(h, 46) != 46 || le32(h) != 0x02014b50) break;
    uint16_t time = le16(h + 12), date = le16(h + 14), nameLen = le16(h + 28), extra = le16(h + 30), note = le16(h + 32);
    uint32_t csize = le32(h + 20), usize = le32(h + 24), local = le32(h + 42);
    char name[81] = {0};
    f.read((uint8_t *)name, min<uint16_t>(nameLen, 80));
    at += 46 + nameLen + extra + note;
    String shown;
    for (const char *p = name; *p; ++p) shown += (uint8_t)*p < 32 ? '?' : *p;
    if (nameLen > 80) shown += "...";
    bool folder = shown.endsWith("/");
    unsigned ratio = usize ? (unsigned)(100 - min<uint64_t>(100, (uint64_t)csize * 100 / usize)) : 0;
    char row[160];
    snprintf(row, sizeof(row), "  %8lu  %6s  %4u%%  %02u-%02u-%02u  %02u:%02u  %s%s\x1b[0m",
             (unsigned long)usize, sizeShort(csize).c_str(), ratio, (date >> 5) & 15, date & 31, ((date >> 9) + 80) % 100,
             time >> 11, (time >> 5) & 63, folder ? "\x1b[1;36m" : "\x1b[1;37m", shown.c_str());
    rows.push_back(row);
    if (!folder) { files++; total += usize; packed += csize; }
    String u = up(shortName(shown));
    if (!folder && usize && usize <= 32768 && csize <= 32768 && texts.size() < 4 && (u.endsWith(".DIZ") || u.endsWith(".NFO")))
      texts.push_back({shown, le16(h + 10), csize, usize, local});
  }
  char sum[100];
  snprintf(sum, sizeof(sum), "  %8llu  %6s  %4u%%                   %u file%s",
           (unsigned long long)total, sizeShort((uint32_t)min<uint64_t>(packed, UINT32_MAX)).c_str(),
           total ? (unsigned)(100 - min<uint64_t>(100, packed * 100 / total)) : 0, files, files == 1 ? "" : "s");
  rows.push_back("\x1b[0;36m  --------  ------  -----                   ----\x1b[0m");
  rows.push_back(sum);
  if (entries > 1500) rows.push_back("  ... and " + String(entries - 1500) + " more");
  // FILE_ID.DIZ first, then the NFOs.
  std::stable_sort(texts.begin(), texts.end(), [](const Inside &a, const Inside &b) {
    return (up(shortName(a.name)) == "FILE_ID.DIZ") > (up(shortName(b.name)) == "FILE_ID.DIZ");
  });
  for (const Inside &t : texts) {
    std::unique_ptr<uint8_t[]> out;
    size_t outLen = 0;
    rows.push_back("");
    rows.push_back("\x1b[1;36m---- " + t.name + " " + String("------------------------------------------------------------------------").substring(0, max(4, 72 - (int)t.name.length())) + "\x1b[0m");
    if (zipInflate(f, t.method, t.csize, t.usize, t.local, out, outLen)) dosTextRows(out.get(), outLen, rows);
    else rows.push_back("  (packed in a way this board cannot open)");
  }
  f.close();
  if (!commentRows.empty()) {
    rows.push_back("");
    rows.push_back("\x1b[1;36m---- Archive comment ---------------------------------------------------\x1b[0m");
    for (auto &r : commentRows) rows.push_back(r);
  }
  return true;
}
static bool openZipList(Session &s, uint16_t number, const String &path, const String &name, size_t bytes, bool quiet) {
  std::vector<String> rows;
  rows.push_back("\x1b[1;36m" + clean(name, 40) + "\x1b[0m  " + sizeShort(bytes));
  rows.push_back("");
  if (!zipContents(path, rows)) { if (!quiet) prompt(s, "Not a ZIP this board can read. File command? "); return false; }
  s.viewer.number = number; s.viewer.path = path; s.viewer.name = name; s.viewer.ansi = false;
  Reader &r = s.reader;
  r = Reader();
  r.kind = READER_ROWS; r.rows.swap(rows);
  r.title = clean(name, 24);
  r.hint = "SPACE page  B back  \x18\x19 line  N/P file  Q quit";
  r.onKey = fileReaderKey; r.onQuit = fileReaderQuit;
  activity(s, "LISTING " + clean(name, 30));
  openReader(s, false);
  return true;
}
// ---------- the image viewer (browser only) ---------------------------------------------
// The board tells the browser which picture to show; the browser draws it over
// the terminal and sends N, P or any other key back.
static bool imageName(const String &name) {
  String u = up(name);
  return u.endsWith(".GIF") || u.endsWith(".JPG") || u.endsWith(".JPEG") || u.endsWith(".PNG") || u.endsWith(".BMP");
}
static bool openImage(Session &s, uint16_t number, const String &name, size_t bytes) {
  if (!s.web) return false;
  s.viewer.number = number; s.viewer.name = name; s.viewer.ansi = false;
  s.state = FILE_IMAGE;
  activity(s, "VIEWING " + clean(name, 30));
  write(s, "\x1b]9;IMG;/view?n=" + String(s.node) + "&q=" + s.token + "&i=" + number + ";" + clean(name, 40) + ";" + sizeShort(bytes) + "\x07");
  return true;
}
static void closeImage(Session &s) {
  write(s, "\x1b]9;IMG;close\x07");
  listArea(s);
}
// N and P in the image viewer: the next or previous picture in the folder.
static void imageKey(Session &s, const String &bytes) {
  for (size_t i = 0; i < bytes.length(); ++i) {
    char c = toupper((unsigned char)bytes[i]);
    if (c == 0 || c == '\n') continue;
    if (c != 'N' && c != 'P') { closeImage(s); return; }
    auto list = folderEntries(s);
    int step = c == 'N' ? 1 : -1;
    for (int k = s.viewer.number + step; k >= 1 && k <= (int)list.size(); k += step) {
      if (list[k - 1].dir || !imageName(list[k - 1].name)) continue;
      openImage(s, k, list[k - 1].name, list[k - 1].size);
      break;
    }
  }
}
// ---------- the music player (browser only) ---------------------------------------------
// Tracker modules play in the browser with libopenmpt from /BBS/ASSETS/PLAYER.
// Like the image viewer: the board names the tune, keys come back to it.
static bool musicName(const String &name) {
  String u = up(name);
  return u.endsWith(".MOD") || u.endsWith(".S3M") || u.endsWith(".XM") || u.endsWith(".IT");
}
static bool playerReady() { return sdReady && SD_MMC.exists("/BBS/ASSETS/PLAYER/player.js") && SD_MMC.exists("/BBS/ASSETS/PLAYER/libopenmpt.wasm"); }
static bool openMusic(Session &s, uint16_t number, const String &name, size_t bytes) {
  if (!s.web || !playerReady()) return false;
  s.viewer.number = number; s.viewer.name = name; s.viewer.ansi = false;
  s.state = FILE_MUSIC;
  activity(s, "LISTENING " + clean(name, 30));
  write(s, "\x1b]9;MOD;/music?n=" + String(s.node) + "&q=" + s.token + "&i=" + number + ";" + clean(name, 40) + ";" + sizeShort(bytes) + "\x07");
  return true;
}
static void musicKey(Session &s, const String &bytes) {
  for (size_t i = 0; i < bytes.length(); ++i) {
    char c = toupper((unsigned char)bytes[i]);
    if (c == 0 || c == '\n' || c == ' ') continue;  // SPACE pauses in the browser
    if (c != 'N' && c != 'P') { write(s, "\x1b]9;MOD;close\x07"); listArea(s); return; }
    auto list = folderEntries(s);
    int step = c == 'N' ? 1 : -1;
    for (int k = s.viewer.number + step; k >= 1 && k <= (int)list.size(); k += step) {
      if (list[k - 1].dir || !musicName(list[k - 1].name)) continue;
      openMusic(s, k, list[k - 1].name, list[k - 1].size);
      break;
    }
  }
}
static bool openFileViewer(Session &s, uint16_t number, bool quiet = false) {
  String path, name;
  size_t bytes;
  if (!areaFile(s, number, path, name, bytes)) { if (!quiet) prompt(s, "No such file. File command? "); return false; }
  if (zipName(name)) return openZipList(s, number, path, name, bytes, quiet);
  if (musicName(name)) {
    if (openMusic(s, number, name, bytes)) return true;
    if (!quiet) prompt(s, s.web ? "The music player is not on the card. File command? " : "Music plays in the browser terminal. File command? ");
    return false;
  }
  if (imageName(name)) {
    if (openImage(s, number, name, bytes)) return true;
    if (!quiet) prompt(s, "Pictures open in the browser terminal. File command? ");
    return false;
  }
  String upper = up(name);
  const bool ansi = upper.endsWith(".ANS") || upper.endsWith(".ANSI");
  if (!viewableName(name) || !bytes || bytes > UINT32_MAX - 8300) {
    if (!quiet) prompt(s, s.web && fileAreas[s.fileArea].downloads ? "That file can only be downloaded: D " + String(number) + ". File command? "
                     : s.web ? String("That file cannot be viewed, and this area has no downloads. File command? ")
                             : String("That file can only be downloaded, from the browser terminal. File command? "));
    return false;
  }
  File file = SD_MMC.open(path, FILE_READ);
  if (!file) { if (!quiet) prompt(s, "File unavailable. File command? "); return false; }
  uint8_t sample[768];
  size_t got = file.read(sample, min(bytes, sizeof(sample)));
  if (memchr(sample, 0, got)) { file.close(); if (!quiet) prompt(s, "Binary file. File command? "); return false; }
  // Text that is mostly not letters is ASCII art: lines wider than the screen
  // are cut instead of wrapped, so the picture keeps its shape.
  size_t letters = 0, marks = 0;
  for (size_t i = 0; i < got; ++i) {
    if (isalpha(sample[i])) letters++;
    else if (sample[i] > ' ' && (sample[i] < 0x80 || sample[i] >= 0xc0)) marks++;
  }
  const bool artLike = upper.endsWith(".ASC") || marks > letters;
  auto &view = s.viewer;
  view.number = number;
  view.path = path;
  view.name = name;
  view.ansi = ansi;
  view.utf8 = sampleUtf8(sample, got);
  view.offset = 0;
  if (got >= 3 && sample[0] == 0xef && sample[1] == 0xbb && sample[2] == 0xbf)
    view.offset = 3;
  view.end = bytes;
  if (bytes >= 128 && file.seek(bytes - 128) && file.read(sample, 128) == 128 &&
      memcmp(sample, "SAUCE00", 7) == 0) {
    view.end -= 128;
    size_t comment = 5 + size_t(sample[104]) * 64;
    if (sample[104] && comment <= view.end && file.seek(view.end - comment) &&
        file.read(sample, 5) == 5 && memcmp(sample, "COMNT", 5) == 0) view.end -= comment;
  }
  file.close();
  // Text and pictures open in the reader. Pictures play at once (any key
  // stops them in the reader); text starts on its first page.
  Reader &r = s.reader;
  r = Reader();
  r.path = path; r.start = view.offset; r.end = view.end; r.utf8 = view.utf8;
  r.title = clean(name, 24);
  r.hint = "SPACE page  B back  \x18\x19 line  S play  N/P file  Q quit";
  r.onKey = fileReaderKey; r.onQuit = fileReaderQuit;
  if (ansi && s.ansi) { r.kind = READER_ANSI; renderAnsi(r); }
  else { r.kind = READER_TEXT; r.crop = artLike; indexText(r); }
  activity(s, (ansi ? "VIEWING " : "READING ") + clean(name, 30));
  openReader(s, ansi);
  return true;
}
// N and P in the viewer: the next or previous file in the folder that can be viewed.
static bool stepViewer(Session &s, bool forward) {
  auto list = folderEntries(s);
  int at = s.viewer.number;  // 1-based
  for (int i = forward ? at + 1 : at - 1; i >= 1 && i <= (int)list.size(); i += forward ? 1 : -1) {
    if (list[i - 1].dir || !(viewableName(list[i - 1].name) || zipName(list[i - 1].name))) continue;
    if (openFileViewer(s, i, true)) return true;
  }
  return false;
}

// ---------- the reader's file keys -----------------------------------------------------
// N and P open the next or previous viewable file; Q goes back to the list.
static bool fileReaderKey(Session &s, const String &k) {
  if (k != "n" && k != "N" && k != "p" && k != "P") return false;
  bool forward = k == "n" || k == "N";
  // openFileViewer() only touches the reader once a file opens.
  if (!stepViewer(s, forward)) showPage(s, forward ? "Last file in this folder" : "First file in this folder");
  return true;
}
static void fileReaderQuit(Session &s) { listArea(s); }
