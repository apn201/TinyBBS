#pragma once
// Message reading, menus, login and node allocation.
// Part of the single TinyBBS translation unit: included in order from main.cpp.

static bool sysopOnline(const Session &s) {
  for (auto &n : sessions) if (n.used && &n != &s && isSysop(n)) return true;
  return false;
}
// Only what works for this caller: no file cabinets without an open area, no
// bulletins without one to read, no page when no SysOp is on.
static void mainMenu(Session &s) {
  s.state = MAIN; activity(s, "MAIN MENU");
  bool files = false, bulletins = false;
  uint8_t areas = 0;
  for (size_t i = 0; i < fileAreas.size(); ++i) files |= listedArea(s, i);
  for (const auto &b : board.bulletins) bulletins |= canSee(s.user, b.level, b.flag);
  for (const auto &c : board.conferences) if (canJoinConference(s.user, c.id)) areas++;
  std::vector<MenuItem> items;
  items.push_back({"M", "Message bases"});
  items.push_back({"N", "New scan"});
  if (files) items.push_back({"F", "File cabinets"});
  items.push_back({"A", "ANSI gallery"});
  items.push_back({"D", "Doors & games"});
  items.push_back({"C", "Node chat"});
  if (bulletins) items.push_back({"B", "Bulletins"});
  if (areas > 1) items.push_back({"J", "Conferences"});
  items.push_back({"W", "Who is online"});
  if (sysopOnline(s)) items.push_back({"P", "Page SysOp"});
  items.push_back({"U", "User list"});
  items.push_back({"S", "System info"});
  items.push_back({"?", "Help"});
  items.push_back({"G", "Goodbye"});
  if (isSysop(s)) items.push_back({"Z", "SysOp console", "\x1b[1;35m"});
  showMenu(s, "MAIN", items, board.name + " // MAIN BOARD");
  prompt(s, "\x1b[1;33mMain Board Command? \x1b[0m");
}
// The gallery's G browses the first file area whose folder name has ANSI in it.
static int galleryArea(const Session &s) {
  for (size_t i = 0; i < fileAreas.size(); ++i) if (listedArea(s, i) && up(fileAreas[i].folder).indexOf("ANSI") >= 0) return i;
  return -1;
}
static void ansiGallery(Session &s) {
  s.state = ANSI_GALLERY;
  activity(s, "ANSI GALLERY");
  std::vector<MenuItem> items;
  items.push_back({"A", "Kouvosto login splash"});
  items.push_back({"F", "File cabinet screen"});
  if (galleryArea(s) >= 0) items.push_back({"G", "Browse ANSI / ASCII files"});
  items.push_back({"Q", "Main Board"});
  showMenu(s, "GALLERY", items, "ANSI GALLERY");
  prompt(s, "Gallery command? ");
}
// FILES.ANS is the header art; the areas below it come from the card.
static void fileMenu(Session &s) {
  s.state = FILE_MENU; activity(s, "FILE AREAS");
  art(s, "/BBS/ANSI/MENUS/FILES.ANS", "\x1b[1;36mFILE CABINETS\x1b[0m");
  uint32_t n = 0;
  for (size_t i = 0; i < fileAreas.size(); ++i) {
    if (!listedArea(s, i)) continue;
    const FileArea &a = fileAreas[i];
    char row[100];
    snprintf(row, sizeof(row), "  \x1b[1;33m[%2lu]\x1b[0m %s%-32.32s\x1b[0m \x1b[0;36m%5u files\x1b[0m", (unsigned long)++n,
             a.flag.length() ? "\x1b[1;35m" : "\x1b[1;37m", a.name.c_str(), a.files);
    say(s, row);
  }
  if (!n) say(s, "  No file areas are open to you tonight.");
  say(s, "  \x1b[1;33m[Q]\x1b[0m Main Board");
  prompt(s, "File area? ");
}
// The door table (doors/doors.h) is the list: a new door shows up by itself.
static void doorMenu(Session &s) {
  s.state = DOOR_MENU; activity(s, "DOORS & GAMES");
  std::vector<MenuItem> items;
  for (const Door &d : DOORS) items.push_back({String(d.keys[0]), d.name});
  items.push_back({"Q", "Main Board"});
  showMenu(s, "DOORS", items, "DOORS & GAMES");
  prompt(s, "Door command? ");
}
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
    if (b.mandatory && canSee(s.user, b.level, b.flag)) { showBulletin(s, b); shown = true; }
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
  for (uint8_t i = 0; i < board.maxNodes; ++i) if (!sessions[i].used) { Session &s = sessions[i]; s = Session(); s.used = true; s.web = web; s.socket = incoming; if (!web) s.cps = 0; /* Telnet: full speed */ s.node = i + 1; s.token = salt(); newSession(s); event("CONNECT", String("node ") + s.node + (web ? " web" : " telnet")); trace("SESSION_OPEN", String("node ") + s.node + (web ? " web" : " telnet")); return &s; } trace("SESSION_REJECT", "all nodes busy"); return nullptr;
}
static Session *webSession(uint8_t node, const String &token) { if (!node || node > MAX_NODES) return nullptr; Session &s = sessions[node - 1]; return s.used && s.web && s.token == token ? &s : nullptr; }
static void broadcastChat(Session &from, const String &line) { for (auto &s : sessions) if (s.used && s.state == CHAT) say(s, "\x1b[0;36m[" + from.user.handle + "]\x1b[0m " + line); }
