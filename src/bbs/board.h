#pragma once
// Message reading, menus, login and node allocation.
// Part of the single TinyBBS translation unit: included in order from main.cpp.

static void showMessages(Session &s, bool onlyNew) {
  if (!sdReady) { say(s, "Message base unavailable."); return; }
  File f = SD_MMC.open("/BBS/DATA/MSGS/MESSAGES.DAT", FILE_READ); uint8_t shown = 0;
  say(s, "\x1b[1;36m#     From             To               Subject\x1b[0m");
  while (f && f.available() && shown < 20) { Message m; if (!parseMessage(f.readStringUntil('\n'), m) || m.conference != s.conference || (onlyNew && m.id <= s.lastRead)) continue; char row[120]; snprintf(row, sizeof(row), "%-5lu %-16s %-16s %s", (unsigned long)m.id, m.from.c_str(), m.to.length() ? m.to.c_str() : "All", m.subject.c_str()); say(s, row); shown++; }
  if (f) f.close(); if (!shown) say(s, onlyNew ? "No new messages." : "No messages in this area.");
}
static bool readMessage(Session &s, uint32_t id) {
  if (!sdReady) return false; File f = SD_MMC.open("/BBS/DATA/MSGS/MESSAGES.DAT", FILE_READ); Message m; bool found = false;
  while (f && f.available()) { Message candidate; if (parseMessage(f.readStringUntil('\n'), candidate) && candidate.id == id && candidate.conference == s.conference) { m = candidate; found = true; break; } } if (f) f.close();
  if (!found) return false; if (m.to.length() && up(m.to) != up(s.user.handle) && s.user.level < 255) { say(s, "Private message. Access denied."); return true; }
  say(s, ""); say(s, "\x1b[1;33mMsg #: " + String(m.id) + "\x1b[0m"); say(s, "From : " + m.from); say(s, "To   : " + (m.to.length() ? m.to : "ALL")); say(s, "Subj : " + m.subject); say(s, "Date : " + m.when); say(s, ""); say(s, m.body); say(s, "");
  s.lastRead = max(s.lastRead, m.id); savePointer(s); return true;
}
static void mainMenu(Session &s) {
  s.state = MAIN; activity(s, "MAIN MENU");
  const char *fallback = "[M] Messages  [N] New scan   [C] Chat   [W] Who\r\n"
                         "[F] Files     [A] Art        [D] Doors  [U] User list\r\n"
                         "[B] Bulletins [J] Conference [P] Page   [S] System\r\n"
                         "[?] Help      [G] Goodbye";
  if (s.ansi) art(s, board.mainMenu.c_str(), fallback);
  else say(s, fallback);
  if (isSysop(s)) say(s, "\x1b[1;35m[Z]\x1b[0m SysOp console  \x1b[1;35m[USER]\x1b[0m User editor");
  prompt(s, "\x1b[1;33mMain Board Command? \x1b[0m");
}
static void ansiGallery(Session &s) {
  s.state = ANSI_GALLERY;
  activity(s, "ANSI GALLERY");
  art(s, "/BBS/ANSI/MENUS/GALLERY.ANS", "[A] Login splash  [F] File cabinet  [Q] Main Board");
  if (canUseArea(s, 1)) say(s, "[G] Browse ANSI / ASCII files");
  prompt(s, "Gallery command? ");
}
static void fileMenu(Session &s) { s.state = FILE_MENU; activity(s, "FILE AREAS"); art(s, "/BBS/ANSI/MENUS/FILES.ANS"); if (canUseArea(s, 6)) say(s, "\x1b[1;35m[7]\x1b[0m The Back Room"); if (canUseArea(s, 7)) say(s, "\x1b[1;31m[8]\x1b[0m SysOp Vault"); prompt(s, "File area? "); }
static void doorMenu(Session &s) { s.state = DOOR_MENU; activity(s, "DOORS & GAMES"); art(s, "/BBS/ANSI/MENUS/DOORS_V2.ANS"); prompt(s, "Door command? "); }
static void finishLogin(Session &s) {
  s.authenticated = true;
  // Older cards could already contain a SYSOP-named account created before
  // the level/flag bootstrap rule was added.  SYSOP is the reserved primary
  // operator identity, so normalize that record on its next successful login
  // instead of leaving the caller trapped behind the console gate forever.
  bool repairedSysop = false;
  if (s.user.normalized == "SYSOP") {
    String flags = up(s.user.flags);
    bool flagged = ("," + flags + ",").indexOf(",SYSOP,") >= 0;
    if (s.user.level != 255 || !flagged || s.user.locked) {
      s.user.level = 255;
      s.user.locked = false;
      if (!flagged) {
        if (flags.length()) flags += ",";
        flags += "SYSOP";
        s.user.flags = flags;
      }
      repairedSysop = true;
      trace("SYSOP_REPAIR", "normalized primary SYSOP account");
    }
  }
  s.user.calls++;
  if (!touchUser(s.user)) say(s, "\r\nWarning: user statistics were not saved. See Serial console.");
  if (repairedSysop) say(s, "\r\nPrimary SYSOP account normalized: level 255, SYSOP flag.");
  callers++; loadPointer(s); event("LOGIN", s.user.handle + " node " + s.node); trace("LOGIN_OK", s.user.handle + " node " + s.node); activity(s, "BULLETINS");
  say(s, "\r\nCaller #" + String(callers) + "  Security: " + String(s.user.level) + "  Conference: " + confName(s.conference));
  // Mandatory bulletins from BULLETINS.INI are shown at every login.
  bool shown = false;
  for (const auto &b : board.bulletins)
    if (b.mandatory && canSee(s.user, b.level, b.flag)) { art(s, b.path.c_str(), b.title); shown = true; }
  if (!shown) say(s, "Welcome to " + board.name + ".");
  s.state = BULLETIN; prompt(s, "Press ENTER for Main Board: ");
}
static void loginPrompt(Session &s) {
  s.state = LOGIN_CHOICE;
  activity(s, "AWAITING LOGIN");
  if (s.ansi) art(s, board.welcome.c_str(), board.name);
  else say(s, board.name);
  prompt(s, "(N)ew user, (L)ogin, or (G)oodbye? ");
}
static void newSession(Session &s) {
  s.connectedAt = s.lastAt = s.lastPoll = millis();
  loginPrompt(s);
}
static void closeSession(Session &s) {
  if (!s.used) return;
  if (playingMud(s)) mudBroadcastRoom(s, s.user.handle + " loses carrier.");
  if (s.duelNode) {
    Session *other = sessionByNode(s.duelNode);
    if (other && other != &s) {
      other->duelNode = other->duelTurnNode = 0;
      if (other->state == QUEST_DUEL_OFFER || other->state == QUEST_DUEL_WAIT || other->state == QUEST_DUEL) {
        say(*other, "\r\nYour arena opponent lost carrier.");
        questMenu(*other);
      }
    }
  }
  trace("SESSION_CLOSE", String("node ") + s.node + " " + (s.user.handle.length() ? s.user.handle : "anonymous"));
  event("LOGOFF", s.user.handle.length() ? s.user.handle : "anonymous");
  if (s.socket) s.socket.stop();
  if (s.wsClient) wsHangups.push_back(s.wsClient);
  s = Session();
}
static Session *allocate(bool web, WiFiClient incoming = WiFiClient()) {
  for (uint8_t i = 0; i < board.maxNodes; ++i) if (!sessions[i].used) { Session &s = sessions[i]; s = Session(); s.used = true; s.web = web; s.socket = incoming; s.node = i + 1; s.token = salt(); newSession(s); event("CONNECT", String("node ") + s.node + (web ? " web" : " telnet")); trace("SESSION_OPEN", String("node ") + s.node + (web ? " web" : " telnet")); return &s; } trace("SESSION_REJECT", "all nodes busy"); return nullptr;
}
static Session *webSession(uint8_t node, const String &token) { if (!node || node > MAX_NODES) return nullptr; Session &s = sessions[node - 1]; return s.used && s.web && s.token == token ? &s : nullptr; }
static void broadcastChat(Session &from, const String &line) { for (auto &s : sessions) if (s.used && s.state == CHAT) say(s, "\x1b[0;36m[" + from.user.handle + "]\x1b[0m " + line); }
