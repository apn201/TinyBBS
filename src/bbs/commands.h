#pragma once
// Line input and the top-level command dispatch.
// Part of the single TinyBBS translation unit: included in order from main.cpp.

static void handleLine(Session &s, String line) {
  s.lastAt = millis(); line.trim(); String cmd = up(line);
  if (s.user.locked) { closeSession(s); return; }
  if (s.pageNode && isSysop(s) && (cmd == "/A" || cmd == "/R")) { answerPage(s, cmd == "/A"); return; }
  bool sysopState = s.state == SYSOP_MENU || s.state == SYSOP_USERS || s.state == SYSOP_DOORS || s.state == SYSOP_ADD_PASSWORD ||
                    s.state == SYSOP_DELETE_CONFIRM || s.state == SYSOP_USER_EDIT || s.state == SYSOP_USER_FIELD || s.state == SYSOP_ADD_HANDLE || s.state == SYSOP_NODES;
  if (sysopState && !isSysop(s)) {
    s.hideInput = false;
    s.editId = 0;
    mainMenu(s);
    return;
  }
  if (s.authenticated && !canJoinConference(s.user, s.conference)) {
    s.conference = 0;
    mainMenu(s);
    return;
  }
  loginCommand(s, line, cmd) || mainCommand(s, line, cmd) || sysopCommand(s, line, cmd) || userEditorCommand(s, line, cmd) ||
    uploadCommand(s, line, cmd) || fileCommand(s, line, cmd) || messageCommand(s, line, cmd) || doorCommand(s, line, cmd);
}
static void input(Session &s, const String &bytes) {
  // While a picture or file is streaming, keys belong to the stream: space
  // pauses and resumes, anything else skips to the end.
  if (s.state == READER && !streaming(s)) { readerInput(s, bytes); return; }
  if (s.state == FILE_IMAGE && !streaming(s)) { imageKey(s, bytes); return; }
  if (s.state == FILE_MUSIC && !streaming(s)) { musicKey(s, bytes); return; }
  if (streaming(s)) {
    bool space = false, other = false;
    for (size_t i = 0; i < bytes.length(); ++i) {
      char c = bytes[i];
      // Telnet sends Enter as CR NUL or CR LF. The CR started the picture;
      // its NUL or LF can arrive a moment later and must not skip it.
      if (c == 0) continue;
      if (c == '\n' && s.ignoreNextLf) { s.ignoreNextLf = false; continue; }
      if (c == ' ') space = true;
      else other = true;
    }
    if (!space && !other) return;
    if (space && !other) s.paused = !s.paused;
    else { if (bytes.indexOf('\r') >= 0) s.ignoreNextLf = true; skipStream(s); }
    return;
  }
  for (uint16_t i = 0; i < bytes.length() && s.used && s.state != GOODBYE; ++i) { char c = bytes[i];
    // CRLF is one Enter key.  Without this guard the LF becomes a phantom
    // handle/command in Telnet clients such as PuTTY.
    if (c == '\r') { if (!s.hideInput) say(s); String line = s.input; s.input = ""; s.ignoreNextLf = true; handleLine(s, line); }
    else if (c == '\n') { if (s.ignoreNextLf) { s.ignoreNextLf = false; continue; } if (!s.hideInput) say(s); String line = s.input; s.input = ""; handleLine(s, line); }
    else if (c == 8 || c == 127) { if (s.input.length()) { s.input.remove(s.input.length() - 1); if (!s.hideInput) write(s, "\b \b"); } }
    else if ((uint8_t)c >= 32 && s.input.length() < 900) { s.input += c; if (!s.hideInput) write(s, String(c)); }
  }
}
