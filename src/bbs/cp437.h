#pragma once

// Archive ANSI uses DOS code page 437. Both current transports use UTF-8.
static constexpr uint16_t kCp437[] = {
  0x00c7,0x00fc,0x00e9,0x00e2,0x00e4,0x00e0,0x00e5,0x00e7,
  0x00ea,0x00eb,0x00e8,0x00ef,0x00ee,0x00ec,0x00c4,0x00c5,
  0x00c9,0x00e6,0x00c6,0x00f4,0x00f6,0x00f2,0x00fb,0x00f9,
  0x00ff,0x00d6,0x00dc,0x00a2,0x00a3,0x00a5,0x20a7,0x0192,
  0x00e1,0x00ed,0x00f3,0x00fa,0x00f1,0x00d1,0x00aa,0x00ba,
  0x00bf,0x2310,0x00ac,0x00bd,0x00bc,0x00a1,0x00ab,0x00bb,
  0x2591,0x2592,0x2593,0x2502,0x2524,0x2561,0x2562,0x2556,
  0x2555,0x2563,0x2551,0x2557,0x255d,0x255c,0x255b,0x2510,
  0x2514,0x2534,0x252c,0x251c,0x2500,0x253c,0x255e,0x255f,
  0x255a,0x2554,0x2569,0x2566,0x2560,0x2550,0x256c,0x2567,
  0x2568,0x2564,0x2565,0x2559,0x2558,0x2552,0x2553,0x256b,
  0x256a,0x2518,0x250c,0x2588,0x2584,0x258c,0x2590,0x2580,
  0x03b1,0x00df,0x0393,0x03c0,0x03a3,0x03c3,0x00b5,0x03c4,
  0x03a6,0x0398,0x03a9,0x03b4,0x221e,0x03c6,0x03b5,0x2229,
  0x2261,0x00b1,0x2265,0x2264,0x2320,0x2321,0x00f7,0x2248,
  0x00b0,0x2219,0x00b7,0x221a,0x207f,0x00b2,0x25a0,0x00a0
};

static void appendDosGlyph(String &out, uint8_t byte) {
  const uint16_t cp = kCp437[byte - 0x80];
  if (cp < 0x800) out += char(0xc0 | (cp >> 6));
  else {
    out += char(0xe0 | (cp >> 12));
    out += char(0x80 | ((cp >> 6) & 63));
  }
  out += char(0x80 | (cp & 63));
}

static uint8_t utf8Tail(uint8_t byte) {
  if (byte >= 0xc2 && byte <= 0xdf) return 1;
  if (byte >= 0xe0 && byte <= 0xef) return 2;
  if (byte >= 0xf0 && byte <= 0xf4) return 3;
  return 0;
}

static bool sampleUtf8(const uint8_t *bytes, size_t len) {
  bool nonAscii = false;
  for (size_t i = 0; i < len;) {
    uint8_t first = bytes[i++];
    if (first < 0x80) continue;
    uint8_t tail = utf8Tail(first);
    if (!tail) return false;
    if (i + tail > len) return nonAscii;
    if ((first == 0xe0 && bytes[i] < 0xa0) || (first == 0xed && bytes[i] >= 0xa0) ||
        (first == 0xf0 && bytes[i] < 0x90) || (first == 0xf4 && bytes[i] >= 0x90)) return false;
    while (tail--) if ((bytes[i++] & 0xc0) != 0x80) return false;
    nonAscii = true;
  }
  return nonAscii;
}

// Only the bounded ANSI drawing subset crosses the terminal boundary.
// In particular, file contents cannot emit OSC download links or clipboard commands.
static void readArchiveEscape(File &file, uint32_t end, String &out, bool ansi) {
  if (file.position() >= end) return;
  int kind = file.peek();
  if (kind == ']' || kind == 'P' || kind == '_' || kind == '^' || kind == 'X') {
    file.read();
    int previous = 0;
    while (file.position() < end) {
      int next = file.read();
      if (next < 0 || next == 7 || (previous == 27 && next == '\\')) break;
      previous = next;
    }
    return;
  }
  if (kind != '[') return;
  file.read();
  char params[25];
  size_t size = 0;
  bool valid = true;
  while (file.position() < end) {
    int next = file.read();
    if (next < 0 || next == 27) return;
    if (next >= '@' && next <= '~') {
      if (!ansi || !valid || !strchr("mABCDHfGJKsu", next)) return;
      if (next != 'm') {
        // Avoid expensive movement in old terminal emulators.
        unsigned value = 0;
        for (size_t i = 0; i < size; ++i) {
          value = params[i] == ';' ? 0 : value * 10 + (params[i] - '0');
          if (value > 80) return;
        }
      }
      out += "\x1b[";
      out.concat(params, size);
      out += char(next);
      return;
    }
    if (size < sizeof(params) && ((next >= '0' && next <= '9') || next == ';'))
      params[size++] = char(next);
    else valid = false;
  }
}
