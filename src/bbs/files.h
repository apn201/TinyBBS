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
  job.ansi = s.ansi && ansi; job.utf8 = view.utf8; job.width = job.ansi ? 80 : 78;
  write(s, "\x1b[0m\r\n");
  if (!ansi) say(s, clean(name, 65) + "\r\n");
  s.jobs.push_back(job);
  write(s, "\x1b[0m\r\n[R] Replay  [Q] File list: ");
  s.state = FILE_VIEW;
  activity(s, "VIEWING FILE");
}
