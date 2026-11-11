#pragma once
// File areas, listings and the paged file viewer.
// Part of the single TinyBBS translation unit: included in order from main.cpp.

struct FileArea { const char *name, *path, *flag; uint8_t level; };
static const FileArea FILE_AREAS[] = {
  {"Scene and Demo Files", "/BBS/FILES/005_DEMOS", "", 20},
  {"ANSI and ASCII Art", "/BBS/FILES/006_ANSI_ASCII", "", 20},
  {"Textfiles, Docs and Weirdness", "/BBS/FILES/010_TEXTFILES", "", 20},
  {"Public Image Gallery", "/BBS/FILES/013_IMAGES", "", 20},
  {"Programming", "/BBS/FILES/008_PROGRAMMING", "", 20},
  {"BBS Software", "/BBS/FILES/001_BBS_SOFTWARE", "", 20},
  {"The Back Room", "/BBS/FILES/013_THE_BACK_ROOM", "BACKROOM", 50},
  {"SysOp Vault", "/BBS/FILES/014_SYSOP_VAULT", "SYSOP", 255}
};
static const uint8_t FILE_AREA_COUNT = sizeof(FILE_AREAS) / sizeof(FILE_AREAS[0]);
static void fileMenu(Session &s);
static void doorMenu(Session &s);
static bool canUseArea(const Session &s, uint8_t area) { return area < FILE_AREA_COUNT && s.user.level >= FILE_AREAS[area].level && (!FILE_AREAS[area].flag[0] || hasFlag(s.user, FILE_AREAS[area].flag)); }
static String shortName(const String &path) { int slash = path.lastIndexOf('/'); return slash >= 0 ? path.substring(slash + 1) : path; }
static bool areaFile(Session &s, uint16_t wanted, String &path, String &name, size_t &bytes) {
  if (!sdReady || !canUseArea(s, s.fileArea) || !wanted) return false;
  File dir = SD_MMC.open(FILE_AREAS[s.fileArea].path); if (!dir || !dir.isDirectory()) return false;
  uint16_t n = 0; File entry;
  while ((entry = dir.openNextFile())) { bool take = !entry.isDirectory(); if (take) n++; if (take && n == wanted) { path = entry.path(); name = shortName(path); bytes = entry.size(); entry.close(); dir.close(); return true; } entry.close(); }
  dir.close(); return false;
}
static void listArea(Session &s) {
  if (!canUseArea(s, s.fileArea)) { say(s, "Access denied."); fileMenu(s); return; }
  const FileArea &a = FILE_AREAS[s.fileArea]; activity(s, String("FILES: ") + a.name);
  say(s, "\x1b[1;36m\r\n+---------------- " + String(a.name) + " ----------------+\x1b[0m");
  File dir = SD_MMC.open(a.path);
  uint16_t n = 0;
  constexpr uint16_t perPage = 16;
  s.moreFiles = false;
  if (dir && dir.isDirectory()) {
    File entry;
    while ((entry = dir.openNextFile())) {
      if (entry.isDirectory() || ++n <= s.fileOffset) continue;
      if (n - s.fileOffset > perPage) { s.moreFiles = true; break; }
      String name = shortName(entry.path());
      char row[100];
      snprintf(row, sizeof(row), "\x1b[1;33m[%02u]\x1b[0m %-42.42s %6lu K",
               n, name.c_str(), (unsigned long)((entry.size() + 1023) / 1024));
      say(s, row);
    }
  }
  if (!n) say(s, "This cabinet is empty tonight.");
  say(s, s.web ? "[V #] View   [D #] Download   [N/P] Page   [L] Re-list   [Q] Back"
               : "[V #] View   [N/P] Page   [L] Re-list   [Q] Back");
  prompt(s, "File command? ");
  s.state = FILE_LIST;
}
static void renderFilePage(Session &s) {
  auto &view = s.viewer;
  if (!canUseArea(s, s.fileArea)) { fileMenu(s); return; }
  File file = SD_MMC.open(view.path, FILE_READ);
  if (!file || file.size() < view.end || !file.seek(view.offset)) {
    say(s, "File no longer available.");
    listArea(s);
    return;
  }
  const bool ansi = s.ansi && view.ansi;
  const uint8_t width = ansi ? 80 : 78;
  const uint8_t pageRows = ansi ? 23 : 20;
  const uint32_t limit = min(view.end, view.offset + 8192);
  String page;
  if (!page.reserve(10240)) { say(s, "Not enough memory to open the viewer."); listArea(s); return; }
  if (s.ansi) page = "\x1b[2J\x1b[H\x1b[0m";
  if (!ansi) page += clean(view.name, 65) + "\r\n\r\n";
  uint8_t lines = 0, column = 0;
  bool eof = false;
  while (file.position() < limit && lines < pageRows && page.length() < 10100) {
    int byte = file.read();
    if (byte < 0 || byte == 0x1a) { eof = true; break; }
    if (byte == 27) { readArchiveEscape(file, min(view.end, limit + 32), page, ansi); continue; }
    if (byte == '\r' || byte == '\n') {
      if (byte == '\r' && file.position() < view.end && file.peek() == '\n') file.read();
      page += "\r\n";
      column = 0;
      ++lines;
      continue;
    }
    if (byte == '\t') {
      do { page += ' '; ++column; } while (column < width && column % 8);
      continue;
    }
    if (byte < 32 || byte == 127) continue;
    if (column >= width) {
      if (++lines >= pageRows) { file.seek(file.position() - 1); break; }
      page += "\r\n";
      column = 0;
    }
    ++column;
    if (byte < 128) page += char(byte);
    else if (!view.utf8) appendDosGlyph(page, byte);
    else {
      char glyph[4] = {char(byte)};
      uint8_t tail = utf8Tail(byte), got = 0;
      while (got < tail && file.position() < view.end && (file.peek() & 0xc0) == 0x80)
        glyph[++got] = char(file.read());
      if (tail && got == tail && !(uint8_t(glyph[0]) == 0xc2 && uint8_t(glyph[1]) < 0xa0))
        page.concat(glyph, got + 1);
      else page += '?';
    }
  }
  view.next = eof || file.position() >= view.end ? 0 : file.position();
  if (s.ansi) page += ansi ? "\x1b[0m\x1b[24;1H\x1b[K" : "\x1b[0m\r\n";
  else page += "\r\n";
  if (view.next) page += "[ENTER/N] Next  ";
  if (view.depth) page += "[P] Previous  ";
  page += "[R] Start  [Q] File list: ";
  if (s.web && s.ansi) s.output = "";
  write(s, page);
  s.state = FILE_VIEW;
  activity(s, "VIEWING FILE");
}
static void openFileViewer(Session &s, uint16_t number) {
  String path, name;
  size_t bytes;
  if (!areaFile(s, number, path, name, bytes)) { prompt(s, "No such file. File command? "); return; }
  String upper = up(name);
  const bool ansi = upper.endsWith(".ANS") || upper.endsWith(".ANSI");
  const bool plain = upper.endsWith(".ASC") || upper.endsWith(".NFO") || upper.endsWith(".DIZ") ||
                     upper.endsWith(".TXT") || upper.endsWith(".ION") || upper.endsWith(".SDI") ||
                     (s.fileArea == 2 && upper.indexOf('.') < 0);
  if ((!ansi && !plain) || !bytes || bytes > UINT32_MAX - 8300) {
    prompt(s, "Only ANSI and text files can be viewed. File command? ");
    return;
  }
  File file = SD_MMC.open(path, FILE_READ);
  if (!file) { prompt(s, "File unavailable. File command? "); return; }
  uint8_t sample[768];
  size_t got = file.read(sample, min(bytes, sizeof(sample)));
  if (memchr(sample, 0, got)) { prompt(s, "Binary file. File command? "); return; }
  auto &view = s.viewer;
  view.number = number;
  view.path = path;
  view.name = name;
  view.ansi = ansi;
  view.utf8 = sampleUtf8(sample, got);
  view.depth = 0;
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
  renderFilePage(s);
}
