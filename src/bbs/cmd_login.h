#pragma once
// Login and new-user prompts.
// Part of the single TinyBBS translation unit: included in order from main.cpp.

static bool loginCommand(Session &s, String &line, const String &cmd) {
  if (s.state == LOGIN_CHOICE) { if (cmd == "N" || cmd == "NEW") { s.state = NEW_HANDLE; prompt(s, "Handle (3-16 chars, ENTER cancels): "); } else if (cmd == "L" || cmd == "LOGIN") { s.state = LOGIN_HANDLE; prompt(s, "Handle: "); } else if (cmd == "G") { closeSession(s); } else prompt(s, "N, L or G? "); return true; }
  if (s.state == LOGIN_HANDLE) { s.pendingHandle = clean(line, 16, true); User u; if (!s.pendingHandle.length() || !findUser(up(s.pendingHandle), u)) { say(s, "Unknown caller."); s.state = LOGIN_CHOICE; prompt(s, "(N)ew user, (L)ogin, or (G)oodbye? "); } else if (u.locked) { say(s, "This account is locked. Page the SysOp."); event("AUTH_LOCKED", u.handle); s.state = LOGIN_CHOICE; prompt(s, "(N)ew user, (L)ogin, or (G)oodbye? "); } else { s.user = u; s.state = LOGIN_PASSWORD; s.hideInput = true; prompt(s, "Password: "); } return true; }
  if (s.state == LOGIN_PASSWORD) { s.hideInput = false; if (sha256hex(s.user.salt + ":" + line) != s.user.hash) { say(s, "\r\nBad password."); event("AUTH_FAIL", s.pendingHandle); s.state = LOGIN_CHOICE; prompt(s, "(N)ew user, (L)ogin, or (G)oodbye? "); } else finishLogin(s); return true; }
  // ENTER on an empty line backs out of signing up, at the handle and the password.
  if ((s.state == NEW_HANDLE || s.state == NEW_PASSWORD) && !line.length()) { s.hideInput = false; say(s, "\r\nNo account made."); loginPrompt(s); return true; }
  if (s.state == NEW_HANDLE) { s.pendingHandle = clean(line, 16, true); User exists; if (s.pendingHandle.length() < 3 || findUser(up(s.pendingHandle), exists)) { say(s, "Handle unavailable."); prompt(s, "Handle (3-16 chars, ENTER cancels): "); } else { s.state = NEW_PASSWORD; s.hideInput = true; prompt(s, "Password (6+ chars, ENTER cancels): "); } return true; }
  if (s.state == NEW_PASSWORD) { if (line.length() < 6) { say(s, "\r\nToo short."); prompt(s, "Password (6+ chars, ENTER cancels): "); } else { s.hideInput = false; s.pendingPass = line; s.state = NEW_REAL; say(s); prompt(s, "Real name: "); } return true; }
  if (s.state == NEW_REAL) { s.pendingReal = clean(line, 40); s.state = NEW_LOCATION; prompt(s, "Location: "); return true; }
  if (s.state == NEW_LOCATION) {
    trace("NEW_USER_LOCATION", s.pendingHandle + " / " + clean(line, 40));
    s.user = User(); s.user.handle = s.pendingHandle; s.user.normalized = up(s.pendingHandle); s.user.realName = s.pendingReal; s.user.location = clean(line, 40); s.user.salt = salt(); s.user.hash = sha256hex(s.user.salt + ":" + s.pendingPass); s.user.level = (s.user.normalized == "SYSOP" && !anySysop()) ? 255 : board.newUserLevel; newUserDefaults(s.user); if (s.user.level == 255) s.user.flags = "SYSOP";
    if (!appendUser(s.user)) { say(s, "\r\nDatabase write failed: " + (storageFault.length() ? storageFault : "see Serial console") + "."); trace("NEW_USER_FAIL", s.pendingHandle); s.state = LOGIN_CHOICE; prompt(s, "(N)ew user, (L)ogin, or (G)oodbye? "); return true; }
    say(s, "\r\nAccount created. Welcome aboard."); event("NEW_USER", s.user.handle); trace("NEW_USER_OK", s.user.handle); finishLogin(s); return true;
  }
  return false;
}
