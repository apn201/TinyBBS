#pragma once
// Streamed output. Art and viewed files are read from the SD card a chunk at
// a time, so any size works. ANSI files opened with V in a file area are
// released at modem speed (the session's cps: baud= in SYSTEM.INI, or the
// browser's SPEED button), so a tall picture scrolls past while it is drawn.
// Everything else is shown at once.
// Part of the single TinyBBS translation unit: included in order from main.cpp.

// True while a paced file is playing: keys then skip or pause it.
static bool streaming(const Session &s) {
  for (const auto &j : s.jobs) if (j.paced) return true;
  return false;
}
// Key mode for the browser: 0 = line input, 1 = a file is streaming (keys skip
// or pause it), 2 = the text pager (every key goes to the board at once).
static uint8_t keyMode(const Session &s) { return streaming(s) ? 1 : s.state == FILE_PAGER ? 2 : 0; }
static bool outputDone(const Session &s) { return s.jobs.empty() && !s.pending.length() && !s.output.length(); }

// Append up to `want` bytes of the job's file to `out`. Returns true when the
// file is finished (end, DOS EOF mark 0x1A, or unreadable).
static bool streamChunk(StreamJob &j, String &out, size_t want) {
  File f = SD_MMC.open(j.path, FILE_READ);
  if (!f || f.isDirectory() || !f.seek(j.offset)) return true;
  size_t start = out.length();
  bool done = false;
  // Art chunks only end between escape sequences and whole UTF-8 characters,
  // so a skip or a frame boundary never leaves half a sequence behind.
  while (out.length() - start < want || j.inEsc || j.utfLeft) {
    if (f.position() >= j.end) { done = true; break; }
    int b = f.read();
    if (b < 0 || b == 0x1a) { done = true; break; }
    if (j.art) {
      // Board art is trusted: bytes pass through, bare LF becomes CRLF.
      if (b == '\n' && j.column != 1) out += '\r';
      out += char(b);
      j.column = b == '\r' ? 1 : 0;
      j.lineStart = b == '\n';
      if (j.utfLeft) j.utfLeft = (b & 0xc0) == 0x80 ? j.utfLeft - 1 : 0;
      else if (b >= 0xc0) j.utfLeft = utf8Tail(b);
      if (b == 27) j.inEsc = 1;
      else if (j.inEsc == 1) j.inEsc = b == '[' ? 2 : 0;
      else if (j.inEsc == 2 && b >= 0x40 && b <= 0x7e) j.inEsc = 0;
      continue;
    }
    // Archive files: only the ANSI drawing subset passes, DOS glyphs become UTF-8.
    if (b == 27) { readArchiveEscape(f, j.end, out, j.ansi); continue; }
    if (b == '\r' || b == '\n') {
      // A bare CR returns to column 1 so art can draw over the same line.
      if (b == '\r' && !(f.position() < j.end && f.peek() == '\n')) { out += '\r'; j.column = 0; continue; }
      if (b == '\r') f.read();
      out += "\r\n";
      j.column = 0;
      continue;
    }
    if (b == '\t') {
      do { out += ' '; ++j.column; } while (j.column < j.width && j.column % 8);
      continue;
    }
    if (j.ansi && b < 32 && b != 0 && b != 7 && b != 8) { appendDosGlyph(out, b); continue; }
    if (b < 32 || b == 127) continue;
    // ANSI art moves the cursor around, so the terminal does the wrapping at
    // column 80, as it did on DOS. Plain text is wrapped here.
    if (!j.ansi && j.column >= j.width) {
      if (j.crop) {  // skip the rest of the line, including UTF-8 tails
        if (j.utf8 && b >= 0xc0) { uint8_t tail = utf8Tail(b); while (tail-- && f.position() < j.end && (f.peek() & 0xc0) == 0x80) f.read(); }
        continue;
      }
      out += "\r\n"; j.column = 0;
    }
    ++j.column;
    if (b < 128) out += char(b);
    else if (!j.utf8) appendDosGlyph(out, b);
    else {
      char glyph[4] = {char(b)};
      uint8_t tail = utf8Tail(b), got = 0;
      while (got < tail && f.position() < j.end && (f.peek() & 0xc0) == 0x80) glyph[++got] = char(f.read());
      if (tail && got == tail && !(uint8_t(glyph[0]) == 0xc2 && uint8_t(glyph[1]) < 0xa0)) out.concat(glyph, got + 1);
      else out += '?';
    }
  }
  j.offset = f.position();
  f.close();
  if (done && j.art && !j.lineStart) out += "\r\n";
  return done;
}

// Never cut inside a UTF-8 character: a WebSocket text frame must be valid UTF-8.
static size_t utf8Cut(const String &s, size_t want) {
  if (want >= s.length()) return s.length();
  size_t cut = want;
  while (cut > 0 && ((uint8_t)s[cut] & 0xc0) == 0x80) cut--;
  if (!cut) cut = min((size_t)s.length(), (size_t)(1 + utf8Tail((uint8_t)s[0])));
  return cut;
}

// Move bytes from pending (and queued files) to the transport buffer. Paced
// files go at the session's speed; everything else goes at once. Called for
// every session on every loop pass.
static void pace(Session &s, uint32_t now) {
  uint32_t elapsed = now - s.paceAt;
  s.paceAt = now;
  if (s.paused) return;
  size_t budget;
  bool paced = !s.jobs.empty() && s.jobs.front().paced && s.cps;
  if (!paced) { budget = 4096; s.paceCredit = 0; }
  else {
    s.paceCredit = min<uint32_t>(s.paceCredit + min<uint32_t>(elapsed, 1000) * s.cps, 128000);
    budget = s.paceCredit / 1000;
    if (!budget) return;
  }
  size_t moved = 0;
  while (moved < budget) {
    if (!s.pending.length()) {
      if (s.jobs.empty()) break;
      StreamJob &j = s.jobs.front();
      size_t want = min<size_t>(max<size_t>(budget - moved, 256), 2048);
      if (streamChunk(j, s.pending, want)) {
        if (!j.art) s.pending += "\x1b[0m";
        s.pending += j.after;
        s.jobs.erase(s.jobs.begin());
      }
      continue;
    }
    size_t take = utf8Cut(s.pending, budget - moved);
    s.output += s.pending.substring(0, take);
    s.pending.remove(0, take);
    moved += take;
  }
  if (paced) s.paceCredit -= min<uint32_t>(s.paceCredit, moved * 1000);
}

// A key while a file streams: drop the rest of the file(s), keep what was
// written after them (menus, prompts), and show what is already queued now.
static void skipStream(Session &s) {
  String tail;
  for (auto &j : s.jobs) tail += "\x1b[0m\r\n" + j.after;
  s.jobs.clear();
  s.paused = false;
  s.output += s.pending;
  s.pending = tail;
}

// Queue a board art file. Text written after this call appears after the art.
static void art(Session &s, const char *path, const String &fallback = "") {
  if (sdReady && s.ansi) {
    File f = SD_MMC.open(path, FILE_READ);
    if (f && !f.isDirectory() && f.size()) {
      StreamJob j;
      j.path = path;
      j.end = f.size();
      j.art = true;
      f.close();
      s.jobs.push_back(j);
      return;
    }
  }
  say(s, fallback);
}
