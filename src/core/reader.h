#pragma once
// The reader: one pager for text files, ANSI pictures and long messages. A
// screen of 23 rows plus a status bar, SPACE and B page, the arrows scroll by
// a row, S plays from the current row at modem speed and any key stops it
// there. Keys act at once: Telnet sends them one by one, the browser switches
// to key mode while the reader is open.
// Part of the single TinyBBS translation unit: included in order from main.cpp.

static constexpr uint8_t PAGE_LINES = 23;     // plus the status bar: fits a 24-row terminal
static constexpr uint32_t ANSI_MAX_ROWS = 2000;

static void appendCodepoint(String &out, uint16_t cp) {
  if (cp < 0x80) out += char(cp);
  else if (cp < 0x800) { out += char(0xc0 | (cp >> 6)); out += char(0x80 | (cp & 63)); }
  else { out += char(0xe0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 63)); out += char(0x80 | (cp & 63)); }
}
static uint16_t dosCodepoint(uint8_t b) {
  if (b < 0x20) return kCp437Low[b];
  if (b >= 0x80) return kCp437[b - 0x80];
  return b;
}

// ---------- text files ----------------------------------------------------------------
// One screen row from f's position: up to 80 columns, or to the end of the
// source line. Wide lines wrap (the next row starts where this one stopped)
// or, for ASCII art, are cut. With out == nullptr it only advances.
// Returns false at the end of the file.
static bool textRow(File &f, uint32_t end, bool utf8, bool crop, String *out) {
  if (f.position() >= end) return false;
  uint8_t col = 0;
  while (f.position() < end) {
    int b = f.peek();
    if (b < 0 || b == 0x1a) { f.seek(end); break; }
    if (b == '\r' || b == '\n') {
      f.read();
      if (b == '\r' && f.position() < end && f.peek() == '\n') f.read();
      return true;
    }
    if (col >= 80) { if (!crop) return true; f.read(); continue; }
    f.read();
    if (b == 27) { String sink; readArchiveEscape(f, end, sink, false); continue; }
    if (b == '\t') { do { if (out) *out += ' '; } while (++col < 80 && col % 8); continue; }
    if (b == 0 || b == 7 || b == 8 || b == 127) continue;
    col++;
    if (utf8 && b >= 0xc0) {
      char glyph[4] = {char(b)};
      uint8_t tail = utf8Tail(b), got = 0;
      while (got < tail && f.position() < end && (f.peek() & 0xc0) == 0x80) glyph[++got] = char(f.read());
      if (out) { if (tail && got == tail) out->concat(glyph, got + 1); else *out += '?'; }
      continue;
    }
    if (!out) continue;
    if (b < 32 || (b >= 128 && !utf8)) appendDosGlyph(*out, b);  // DOS glyphs, as TYPE showed them
    else if (b < 128) *out += char(b);
    else *out += '?';
  }
  return true;
}
static void indexText(Reader &r) {
  File f = SD_MMC.open(r.path, FILE_READ);
  if (f && f.seek(r.start)) {
    while (r.lines.size() < 20000) {
      uint32_t at = f.position();
      if (!textRow(f, r.end, r.utf8, r.crop, nullptr)) break;
      r.lines.push_back(at);
    }
  }
  if (f) f.close();
}

// ---------- ANSI pictures ---------------------------------------------------------------
// The picture is drawn into a cell buffer the way an 80-column DOS screen
// would draw it: cursor moves, colours, wrap at column 80. Each cell is a
// codepoint plus ink (low 4 bits) and paper (high 4 bits) in bits 16-23.
static constexpr uint32_t BLANK_CELL = ' ' | (7u << 16);
static void renderAnsi(Reader &r) {
  uint32_t maxRows = ANSI_MAX_ROWS;
  uint32_t *cells = (uint32_t *)bigAlloc(maxRows * 80 * 4);
  if (!cells) { maxRows = 200; cells = (uint32_t *)malloc(maxRows * 80 * 4); }
  if (!cells) return;
  r.cells = std::shared_ptr<uint32_t>(cells, free);
  for (uint32_t i = 0; i < maxRows * 80; ++i) cells[i] = BLANK_CELL;
  File f = SD_MMC.open(r.path, FILE_READ);
  if (!f || !f.seek(r.start)) { if (f) f.close(); return; }
  int x = 0, y = 0, sx = 0, sy = 0;
  bool wrap = false, bold = false, rev = false;
  uint8_t fg = 7, bg = 0;
  uint32_t used = 0;
  auto attr = [&]() -> uint32_t {
    uint8_t ink = fg | (bold ? 8 : 0), paper = bg;
    if (rev) { uint8_t t = ink; ink = paper; paper = t & 7; }
    return (uint32_t)(ink | (paper << 4)) << 16;
  };
  auto put = [&](uint16_t cp) {
    if (wrap) { x = 0; y++; wrap = false; }
    if (y >= (int)maxRows) return;
    cells[y * 80 + x] = cp | attr();
    if ((uint32_t)y + 1 > used) used = y + 1;
    if (x == 79) wrap = true; else x++;
  };
  while (f.position() < r.end && y < (int)maxRows) {
    int b = f.read();
    if (b < 0 || b == 0x1a) break;
    if (b == 27) {
      int kind = f.peek();
      if (kind == '[') {
        f.read();
        char p[32]; size_t n = 0; int fin = 0;
        while (f.position() < r.end) {
          int c = f.read();
          if (c < 0) break;
          if (c >= 0x40 && c <= 0x7e) { fin = c; break; }
          if (n < sizeof(p) - 1) p[n++] = (char)c;
        }
        p[n] = 0;
        int args[8] = {0}, count = 0;
        for (char *t = p; count < 8;) {
          if (*t == '?') { t++; continue; }
          args[count++] = atoi(t);
          char *semi = strchr(t, ';');
          if (!semi) break;
          t = semi + 1;
        }
        int a = args[0] ? args[0] : 1;
        switch (fin) {
          case 'm':
            for (int i = 0; i < max(count, 1); ++i) {
              int v = args[i];
              if (v == 0) { fg = 7; bg = 0; bold = rev = false; }
              else if (v == 1) bold = true;
              else if (v == 22) bold = false;
              else if (v == 7) rev = true;
              else if (v == 27) rev = false;
              else if (v >= 30 && v <= 37) fg = v - 30;
              else if (v == 39) fg = 7;
              else if (v >= 40 && v <= 47) bg = v - 40;
              else if (v == 49) bg = 0;
              else if (v >= 90 && v <= 97) fg = v - 82;
              else if (v >= 100 && v <= 107) bg = v - 92;
            }
            break;
          case 'A': y = max(0, y - a); wrap = false; break;
          case 'B': y = min((int)maxRows - 1, y + a); wrap = false; break;
          case 'C': x = min(79, x + a); wrap = false; break;
          case 'D': x = max(0, x - a); wrap = false; break;
          case 'G': x = constrain(a - 1, 0, 79); wrap = false; break;
          case 'H': case 'f':
            y = constrain((args[0] ? args[0] : 1) - 1, 0, (int)maxRows - 1);
            x = constrain((count > 1 && args[1] ? args[1] : 1) - 1, 0, 79); wrap = false; break;
          case 's': sx = x; sy = y; break;
          case 'u': x = sx; y = sy; wrap = false; break;
          case 'J':
            if (args[0] == 2) { for (uint32_t i = 0; i < used * 80; ++i) cells[i] = BLANK_CELL; used = 0; x = y = 0; wrap = false; }
            break;
          case 'K': {
            int from = args[0] == 0 ? x : 0, to = args[0] == 1 ? x + 1 : 80;
            for (int i = from; i < to; ++i) cells[y * 80 + i] = ' ' | attr();
            break;
          }
        }
        continue;
      }
      if (kind == ']') {  // operating system commands: skip to BEL or ESC backslash
        int prev = 0;
        while (f.position() < r.end) { int c = f.read(); if (c < 0 || c == 7 || (prev == 27 && c == '\\')) break; prev = c; }
      }
      continue;
    }
    if (b == '\r') { x = 0; wrap = false; continue; }
    if (b == '\n') { y++; wrap = false; continue; }
    if (b == '\t') { x = min(79, (x / 8 + 1) * 8); continue; }
    if (b == 8) { if (x) x--; wrap = false; continue; }
    if (b == 0 || b == 7 || b == 127) continue;
    if (r.utf8 && b >= 0xc0) {
      uint8_t tail = utf8Tail(b);
      uint32_t cp = b & (tail == 1 ? 0x1f : tail == 2 ? 0x0f : 0x07);
      for (uint8_t i = 0; i < tail && f.position() < r.end && (f.peek() & 0xc0) == 0x80; ++i) cp = (cp << 6) | (f.read() & 0x3f);
      put(cp > 0xffff ? '?' : (uint16_t)cp);
      continue;
    }
    put(r.utf8 && b >= 0x80 ? '?' : dosCodepoint(b));
  }
  f.close();
  r.cellRows = used;
}
static String ansiRow(const Reader &r, uint32_t row) {
  const uint32_t *c = r.cells.get() + row * 80;
  int last = 79;
  while (last >= 0 && (c[last] & 0xffff) == ' ' && ((c[last] >> 20) & 15) == 0) last--;
  String out;
  int cur = -1;
  for (int i = 0; i <= last; ++i) {
    int a = (c[i] >> 16) & 0xff;
    if (a != cur) {
      int ink = a & 15, paper = a >> 4;
      out += String("\x1b[0;") + (ink & 8 ? "1;" : "") + "3" + (ink & 7) + ";" + (paper & 8 ? "10" : "4") + (paper & 7) + "m";
      cur = a;
    }
    appendCodepoint(out, c[i] & 0xffff);
  }
  out += "\x1b[0m";
  return out;
}

// ---------- paging ----------------------------------------------------------------------
static uint32_t readerCount(const Reader &r) {
  return r.kind == READER_TEXT ? r.lines.size() : r.kind == READER_ANSI ? r.cellRows : r.rows.size();
}
static uint32_t readerMaxTop(const Reader &r) {
  uint32_t n = readerCount(r);
  return n > PAGE_LINES ? n - PAGE_LINES : 0;
}
static String readerRow(const Reader &r, uint32_t row) {
  if (row >= readerCount(r)) return "";
  if (r.kind == READER_ROWS) return r.rows[row];
  if (r.kind == READER_ANSI) return r.cells ? ansiRow(r, row) : String();
  String out;
  File f = SD_MMC.open(r.path, FILE_READ);
  if (f && f.seek(r.lines[row])) textRow(f, r.end, r.utf8, r.crop, &out);
  if (f) f.close();
  return out;
}
static void showPage(Session &s, const String &note = "") {
  Reader &r = s.reader;
  uint32_t total = readerCount(r);
  if (r.top > readerMaxTop(r)) r.top = readerMaxTop(r);
  String out = "\x1b[0m\x1b[2J\x1b[H";
  if (r.kind == READER_TEXT) {  // one open file for the whole page
    File f = SD_MMC.open(r.path, FILE_READ);
    for (uint32_t i = 0; i < PAGE_LINES; ++i) {
      uint32_t n = r.top + i;
      if (f && n < total && f.seek(r.lines[n])) textRow(f, r.end, r.utf8, r.crop, &out);
      out += "\r\n";
    }
    if (f) f.close();
  } else {
    for (uint32_t i = 0; i < PAGE_LINES; ++i) out += readerRow(r, r.top + i) + "\r\n";
  }
  uint32_t last = min(r.top + PAGE_LINES, total);
  String where = total ? String(r.top + 1) + "-" + last + "/" + total + (last >= total ? " END" : "  " + String(last * 100 / total) + "%") : String("empty");
  String status = " " + r.title + "  " + where + "  " + (note.length() ? note : r.hint);
  String bar;
  size_t width = 0;
  for (size_t i = 0; i < status.length() && width < 79; ++i) {
    uint8_t c = status[i];
    if (c < 32) appendDosGlyph(bar, c); else bar += (char)c;
    if ((c & 0xc0) != 0x80) width++;
  }
  while (width++ < 79) bar += ' ';
  write(s, out + "\x1b[0;30;46m" + bar + "\x1b[0m");
}
// Open the reader on what the caller filled into s.reader. ANSI pictures
// play at once when the caller has a modem speed; everything else pages.
// atEnd opens on the last page: logs and chat, where the newest is last.
static void openReader(Session &s, bool play, bool atEnd = false) {
  Reader &r = s.reader;
  r.top = atEnd ? readerMaxTop(r) : 0; r.keyEsc = ""; r.playing = false;
  if (!r.hint.length()) r.hint = "SPACE page  B back  \x18\x19 line  S play  Q quit";
  s.state = READER;
  if (play && s.cps && readerCount(r)) {
    write(s, "\x1b[0m\x1b[2J\x1b[H");
    r.playRow = 0; r.playing = true;
  } else showPage(s);
}
// Ready-made screen rows in the reader: ZIP contents, long messages, chat
// history, the event log. onKey gets keys first; onQuit runs on Q.
static void openRows(Session &s, std::vector<String> &rows, const String &title, const String &hint,
                     void (*onQuit)(Session &), bool (*onKey)(Session &, const String &) = nullptr, bool atEnd = false) {
  Reader &r = s.reader;
  r = Reader();
  r.kind = READER_ROWS; r.rows.swap(rows);
  r.title = title; r.hint = hint;
  r.onKey = onKey; r.onQuit = onQuit;
  openReader(s, false, atEnd);
}
static void readerStopped(Session &s) {
  Reader &r = s.reader;
  bool ended = r.playRow >= readerCount(r);
  r.playing = false;
  r.top = r.playRow > PAGE_LINES ? r.playRow - PAGE_LINES : 0;
  showPage(s, ended ? "" : "Stopped. S plays on from here");
}
static void closeReader(Session &s) {
  Reader &r = s.reader;
  void (*quit)(Session &) = r.onQuit;
  r = Reader();
  write(s, "\x1b[0m\x1b[2J\x1b[H");
  if (quit) quit(s);
}
static bool readerAtEnd(const Session &s) { return s.reader.top >= readerMaxTop(s.reader); }
// One key in the reader: a character, or a cursor key's escape sequence.
static void readerKey(Session &s, const String &k) {
  Reader &r = s.reader;
  if (r.onKey && r.onKey(s, k)) return;
  int32_t top = r.top, maxTop = readerMaxTop(r);
  if (k == " " || k == "f" || k == "F" || k == "\r" || k == "\x1b[6~") {
    if (top >= maxTop) { showPage(s, "END. Q quits"); return; }
    top += PAGE_LINES;
  }
  else if (k == "b" || k == "B" || k == "-" || k == "\x1b[5~") top -= PAGE_LINES;
  else if (k == "j" || k == "J" || k == "\x1b[B") top += 1;
  else if (k == "k" || k == "K" || k == "\x1b[A") top -= 1;
  else if (k == "g" || k == "<" || k == "\x1b[H" || k == "\x1b[1~") top = 0;
  else if (k == "G" || k == ">" || k == "\x1b[F" || k == "\x1b[4~") top = maxTop;
  else if (k == "s" || k == "S") {
    if (top >= maxTop && readerCount(r) > PAGE_LINES) top = 0;  // at the end: play from the top
    write(s, "\x1b[0m\x1b[2J\x1b[H");
    r.playRow = top; r.playing = true;
    return;
  }
  else if (k == "q" || k == "Q") { closeReader(s); return; }
  else { showPage(s); return; }
  r.top = constrain(top, (int32_t)0, maxTop);
  showPage(s);
}
// Raw bytes while the reader is open. Cursor keys arrive as ESC [ A and so on,
// possibly split across calls.
static void readerInput(Session &s, const String &bytes) {
  for (size_t i = 0; i < bytes.length() && s.state == READER && !s.reader.playing; ++i) {
    char c = bytes[i];
    String &esc = s.reader.keyEsc;
    if (c == 0) continue;
    if (c == '\n') { if (s.ignoreNextLf) { s.ignoreNextLf = false; continue; } c = '\r'; }
    s.ignoreNextLf = c == '\r';
    if (esc.length() || c == 27) {
      esc += c;
      size_t n = esc.length();
      if (n == 2 && c != '[' && c != 'O') esc = "";
      else if (n >= 3 && (isalpha((unsigned char)c) || c == '~')) {
        String k = esc; esc = "";
        if (k[1] == 'O') k.setCharAt(1, '[');
        readerKey(s, k);
      } else if (n > 8) esc = "";
      continue;
    }
    readerKey(s, String(c));
  }
}
