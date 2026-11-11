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
static bool listedArea(const Session &s, uint8_t area) { return canUseArea(s, area) && fileAreas[area].files; }
static String areaDir(const Session &s) {
  String dir = String(FILES_ROOT) + "/" + fileAreas[s.fileArea].folder;
  return s.filePath.length() ? dir + "/" + s.filePath : dir;
}

// ---------- one folder ------------------------------------------------------------
struct FileEntry { String name; uint32_t size = 0; bool dir = false; };
// The folder's entries in listing order: sub-folders first, then files, by name.
static std::vector<FileEntry> folderEntries(const Session &s) {
  std::vector<FileEntry> list;
  if (!sdReady || !canUseArea(s, s.fileArea)) return list;
  File dir = SD_MMC.open(areaDir(s));
  File e;
  while (dir && dir.isDirectory() && (e = dir.openNextFile())) {
    FileEntry f; f.name = e.name(); f.dir = e.isDirectory(); f.size = f.dir ? 0 : e.size();
    e.close();
    if (!hiddenEntry(f.name)) list.push_back(f);
  }
  if (dir) dir.close();
  std::sort(list.begin(), list.end(), [](const FileEntry &a, const FileEntry &b) {
    return a.dir != b.dir ? a.dir : up(a.name) < up(b.name);
  });
  return list;
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
      if (n2 != "FILE_ID.DIZ" || csize > 6000 || usize > 8000 || (method != 0 && method != 8)) continue;
      uint8_t lh[30];
      if (!f.seek(local) || f.read(lh, 30) != 30 || le32(lh) != 0x04034b50) break;
      if (!f.seek(local + 30 + le16(lh + 26) + le16(lh + 28))) break;
      std::unique_ptr<uint8_t[]> in(new (std::nothrow) uint8_t[csize + 1]);
      std::unique_ptr<uint8_t[]> out(new (std::nothrow) uint8_t[usize + 1]);
      if (!in || !out || f.read(in.get(), csize) != csize) break;
      size_t outLen = usize;
      if (method == 0) memcpy(out.get(), in.get(), min(csize, usize));
      else {
        std::unique_ptr<tinfl_decompressor> d(new (std::nothrow) tinfl_decompressor);
        if (!d) break;
        tinfl_init(d.get());
        size_t inLen = csize;
        tinfl_status st = tinfl_decompress(d.get(), in.get(), &inLen, out.get(), out.get(), &outLen, TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
        if (st != TINFL_STATUS_DONE) break;
      }
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
  for (size_t k = 0; k < want.size(); ++k) {
    const FileEntry &f = list[s.fileOffset + k];
    char row[140];
    String desc = want[k].second; desc.trim();
    if (f.dir) snprintf(row, sizeof(row), "\x1b[1;33m[%3u]\x1b[0m \x1b[1;36m%-24.24s  <DIR>\x1b[0m", (unsigned)(s.fileOffset + k + 1), f.name.c_str());
    else snprintf(row, sizeof(row), "\x1b[1;33m[%3u]\x1b[0m %-24.24s %6s  \x1b[0;37m%.40s\x1b[0m", (unsigned)(s.fileOffset + k + 1), f.name.c_str(), sizeShort(f.size).c_str(), desc.c_str());
    say(s, row);
  }
  s.moreFiles = s.fileOffset + FILES_PER_PAGE < list.size();
  if (list.empty()) say(s, "This cabinet is empty tonight.");
  else say(s, "\x1b[0;36m" + String(s.fileOffset + 1) + "-" + String(s.fileOffset + want.size()) + " of " + list.size() + "\x1b[0m");
  bool dl = s.web && a.downloads;
  String keys = "[#] Open  [V #] View  ";
  if (dl) keys += "[D #] Download  ";
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
static bool openFileViewer(Session &s, uint16_t number, bool quiet = false) {
  String path, name;
  size_t bytes;
  if (!areaFile(s, number, path, name, bytes)) { if (!quiet) prompt(s, "No such file. File command? "); return false; }
  String upper = up(name);
  const bool ansi = upper.endsWith(".ANS") || upper.endsWith(".ANSI");
  if (!viewableName(name) || !bytes || bytes > UINT32_MAX - 8300) {
    if (!quiet) prompt(s, s.web && fileAreas[s.fileArea].downloads ? "That file can only be downloaded. File command? " : "Only ANSI and text files can be viewed. File command? ");
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
  StreamJob job;
  job.path = path; job.offset = view.offset; job.end = view.end;
  job.ansi = s.ansi && ansi; job.utf8 = view.utf8; job.width = 80; job.crop = !ansi && artLike;
  job.paced = job.ansi;
  write(s, "\x1b[0m\r\n");
  if (!ansi) say(s, clean(name, 65) + "\r\n");
  s.jobs.push_back(job);
  write(s, "\x1b[0m\r\n" + clean(name, 40) + "  [N] Next  [P] Prev  [R] Replay  [Q] File list: ");
  s.state = FILE_VIEW;
  activity(s, "VIEWING FILE");
  return true;
}
// N and P in the viewer: the next or previous file in the folder that can be viewed.
static void stepViewer(Session &s, bool forward) {
  auto list = folderEntries(s);
  int at = s.viewer.number;  // 1-based
  for (int i = forward ? at + 1 : at - 1; i >= 1 && i <= (int)list.size(); i += forward ? 1 : -1) {
    if (list[i - 1].dir || !viewableName(list[i - 1].name)) continue;
    if (openFileViewer(s, i, true)) return;
  }
  write(s, forward ? "\r\nLast viewable file in this folder. [P] Prev  [R] Replay  [Q] File list: "
                   : "\r\nFirst viewable file in this folder. [N] Next  [R] Replay  [Q] File list: ");
}
