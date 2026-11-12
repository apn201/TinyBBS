#pragma once
// ZIP archives, read in place: the central directory for listings and
// contents, FILE_ID.DIZ and NFO files inflated with the ROM inflater. An
// archive is never unpacked, changed or renamed.
// Part of the single TinyBBS translation unit: included in order from main.cpp.

#include "miniz.h"  // the inflater in the ESP32-S3 ROM

// Replaces DIZ.DAT, whose descriptions had DOS characters turned into "?".
static constexpr const char *ZIP_CACHE = "/BBS/DATA/INDEX/ZIPINFO.DAT";

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
// The end-of-central-directory record: where the index of entries is, how
// many there are, and the archive comment. Read from the last bytes.
struct ZipDirectory { uint16_t entries = 0; uint32_t at = 0; String comment; };
static bool zipDirectory(File &f, ZipDirectory &dir, size_t commentMax = 0) {
  uint32_t size = f.size();
  uint32_t tailLen = min<uint32_t>(size, 22 + 4096);  // the record, plus a comment of up to 4 KB
  std::unique_ptr<uint8_t[]> tail(new (std::nothrow) uint8_t[tailLen]);
  if (!tail || !f.seek(size - tailLen) || f.read(tail.get(), tailLen) != tailLen) return false;
  for (int i = (int)tailLen - 22; i >= 0; --i) {
    if (le32(tail.get() + i) != 0x06054b50) continue;
    dir.entries = le16(tail.get() + i + 10);
    dir.at = le32(tail.get() + i + 16);
    size_t len = min<size_t>(min<size_t>(le16(tail.get() + i + 20), tailLen - i - 22), commentMax);
    dir.comment = "";
    dir.comment.reserve(len);
    for (size_t k = 0; k < len; ++k) dir.comment += (char)tail[i + 22 + k];
    return true;
  }
  return false;
}
// One entry of the central directory.
struct ZipEntry {
  String name;  // as stored, at most 80 characters
  bool longName = false;
  uint16_t method = 0;
  uint32_t csize = 0, usize = 0, local = 0;
  uint32_t stamp = 0;  // DOS date << 16 | time
  bool folder() const { return name.endsWith("/"); }
};
// Walks the first `limit` entries; fn returns false to stop.
template <typename F> static void eachZipEntry(File &f, const ZipDirectory &dir, uint16_t limit, F fn) {
  uint32_t at = dir.at;
  for (uint16_t n = 0; n < dir.entries && n < limit; ++n) {
    uint8_t h[46];
    if (!f.seek(at) || f.read(h, 46) != 46 || le32(h) != 0x02014b50) return;
    uint16_t nameLen = le16(h + 28);
    char name[81] = {0};
    f.read((uint8_t *)name, min<uint16_t>(nameLen, 80));
    at += 46 + nameLen + le16(h + 30) + le16(h + 32);
    ZipEntry e;
    for (const char *p = name; *p; ++p) e.name += (uint8_t)*p < 32 ? '?' : *p;
    e.longName = nameLen > 80;
    e.method = le16(h + 10); e.csize = le32(h + 20); e.usize = le32(h + 24); e.local = le32(h + 42);
    e.stamp = (uint32_t)le16(h + 14) << 16 | le16(h + 12);
    if (!fn(e)) return;
  }
}
// FILE_ID.DIZ and the newest file date from a ZIP's central directory. The
// date is the DOS stamp of its newest entry: when the archive was made, not
// when it was copied to the card.
static String zipInfo(const String &path, uint32_t &newest) {
  newest = 0;
  File f = SD_MMC.open(path, FILE_READ);
  if (!f) return "";
  String found;
  ZipDirectory dir;
  if (zipDirectory(f, dir)) {
    ZipEntry diz;
    bool have = false;
    eachZipEntry(f, dir, 400, [&](const ZipEntry &e) {
      if (!e.folder() && e.stamp > newest) newest = e.stamp;
      if (!have && up(shortName(e.name)) == "FILE_ID.DIZ" && e.csize <= 6000 && e.usize <= 8000) { diz = e; have = true; }
      return true;
    });
    std::unique_ptr<uint8_t[]> out;
    size_t outLen = 0;
    if (have && zipInflate(f, diz.method, diz.csize, diz.usize, diz.local, out, outLen)) {
      String text;
      text.reserve(outLen);
      for (size_t i = 0; i < outLen; ++i) text += (char)out[i];
      found = oneLine(text, 120);
      if (!found.length()) found = " ";
    }
  }
  f.close();
  return found;
}
// A DOS date and time stamp as seconds since 1970, 0 when it makes no sense.
static time_t dosTime(uint32_t stamp) {
  unsigned d = stamp >> 16, t = stamp & 0xffff;
  int y = 1980 + (d >> 9);
  unsigned m = (d >> 5) & 15, day = d & 31;
  if (!stamp || m < 1 || m > 12 || day < 1 || y > 2107) return 0;
  // Days from 1970-01-01 to y-m-day (proleptic Gregorian).
  int yy = y - (m <= 2);
  int era = yy / 400;
  unsigned yoe = yy - era * 400, doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  long days = era * 146097L + (yoe * 365 + yoe / 4 - yoe / 100 + doy) - 719468;
  return (time_t)(days * 86400 + (t >> 11) * 3600 + ((t >> 5) & 63) * 60);
}
// ZIP details for a listing page: the FILE_ID.DIZ where no description came
// from the folder, and the archive's own date. Read once per ZIP and kept in
// /BBS/DATA/INDEX/ZIPINFO.DAT as "path|size|dos stamp|description".
static void zipDetails(const String &dir, std::vector<std::pair<String, String>> &want, const std::vector<uint32_t> &sizes,
                       std::vector<time_t> &when) {
  when.assign(want.size(), 0);
  std::vector<size_t> zips;
  for (size_t i = 0; i < want.size(); ++i) if (zipName(want[i].first)) zips.push_back(i);
  if (zips.empty()) return;
  std::vector<bool> known(want.size(), false);
  std::vector<String> diz(want.size());
  File c = SD_MMC.open(ZIP_CACHE, FILE_READ);
  while (c && c.available()) {
    String line = c.readStringUntil('\n');
    String path = dec(field(line, 0));
    uint32_t size = strtoul(field(line, 1).c_str(), nullptr, 10);
    for (size_t i : zips)
      if (size == sizes[i] && path == dir + "/" + want[i].first) {
        known[i] = true;
        when[i] = dosTime(strtoul(field(line, 2).c_str(), nullptr, 10));
        diz[i] = dec(field(line, 3));
      }
  }
  if (c) c.close();
  for (size_t i : zips) {
    if (!known[i]) {
      uint32_t stamp = 0;
      String d = zipInfo(dir + "/" + want[i].first, stamp);
      if (!d.length()) d = " ";  // remember that there is none
      File w = SD_MMC.open(ZIP_CACHE, FILE_APPEND);
      if (w) { w.print(enc(dir + "/" + want[i].first) + "|" + sizes[i] + "|" + stamp + "|" + enc(d) + "\n"); w.close(); }
      when[i] = dosTime(stamp);
      diz[i] = d;
    }
    if (!want[i].second.length()) want[i].second = diz[i];
  }
}
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
  ZipDirectory dir;
  if (!zipDirectory(f, dir, 4096)) { f.close(); return false; }
  std::vector<String> commentRows;
  dosTextRows((const uint8_t *)dir.comment.c_str(), dir.comment.length(), commentRows, 40);
  std::vector<ZipEntry> texts;
  rows.push_back("\x1b[0;36m    Length  Packed  Ratio  Date      Time   Name\x1b[0m");
  rows.push_back("\x1b[0;36m  --------  ------  -----  --------  -----  ----\x1b[0m");
  uint64_t total = 0, packed = 0;
  uint16_t files = 0;
  const uint16_t LIMIT = 1500;
  eachZipEntry(f, dir, LIMIT, [&](const ZipEntry &e) {
    unsigned date = e.stamp >> 16, time = e.stamp & 0xffff;
    unsigned ratio = e.usize ? (unsigned)(100 - min<uint64_t>(100, (uint64_t)e.csize * 100 / e.usize)) : 0;
    char row[160];
    snprintf(row, sizeof(row), "  %8lu  %6s  %4u%%  %02u-%02u-%02u  %02u:%02u  %s%s%s\x1b[0m",
             (unsigned long)e.usize, sizeShort(e.csize).c_str(), ratio, (date >> 5) & 15, date & 31, ((date >> 9) + 80) % 100,
             time >> 11, (time >> 5) & 63, e.folder() ? "\x1b[1;36m" : "\x1b[1;37m", e.name.c_str(), e.longName ? "..." : "");
    rows.push_back(row);
    if (e.folder()) return true;
    files++; total += e.usize; packed += e.csize;
    String u = up(shortName(e.name));
    if (e.usize && e.usize <= 32768 && e.csize <= 32768 && texts.size() < 4 && (u.endsWith(".DIZ") || u.endsWith(".NFO"))) texts.push_back(e);
    return true;
  });
  char sum[100];
  snprintf(sum, sizeof(sum), "  %8llu  %6s  %4u%%                   %u file%s",
           (unsigned long long)total, sizeShort((uint32_t)min<uint64_t>(packed, UINT32_MAX)).c_str(),
           total ? (unsigned)(100 - min<uint64_t>(100, packed * 100 / total)) : 0, files, files == 1 ? "" : "s");
  rows.push_back("\x1b[0;36m  --------  ------  -----                   ----\x1b[0m");
  rows.push_back(sum);
  if (dir.entries > LIMIT) rows.push_back("  ... and " + String(dir.entries - LIMIT) + " more");
  // FILE_ID.DIZ first, then the NFOs.
  std::stable_sort(texts.begin(), texts.end(), [](const ZipEntry &a, const ZipEntry &b) {
    return (up(shortName(a.name)) == "FILE_ID.DIZ") > (up(shortName(b.name)) == "FILE_ID.DIZ");
  });
  for (const ZipEntry &t : texts) {
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
