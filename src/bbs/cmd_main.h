#pragma once
// Main board, bulletins, user list, conferences and node chat.
// Part of the single TinyBBS translation unit: included in order from main.cpp.

// SysOp page: the SysOp answers with /A from any prompt, which pulls both
// callers into node chat, or refuses with /R.
static void enterChat(Session &s, const String &banner) {
  if (s.duelNode) duelCancel(s, s.user.handle + " was called to chat");
  if (playingMud(s)) mudBroadcastRoom(s, s.user.handle + " is called away by the SysOp.");
  startChat(s, banner);
}
static void answerPage(Session &op, bool accept) {
  uint8_t node = op.pageNode; uint32_t uid = op.pageUser;
  for (auto &n : sessions) if (n.pageNode == node) { n.pageNode = 0; n.pageUser = 0; n.paged = false; }
  Session *caller = sessionByNode(node);
  if (!caller || !caller->used || caller->user.id != uid) { timedLine(op, "The caller already hung up."); return; }
  if (!accept) {
    timedLine(*caller, "\x1b[1;31mThe SysOp is busy right now. Try again later.\x1b[0m");
    timedLine(op, "Page refused.");
    return;
  }
  event("SYSOP_CHAT", op.user.handle + " answered " + caller->user.handle);
  enterChat(*caller, "\r\n\x1b[1;33m*** The SysOp breaks in: you are in chat with " + op.user.handle + ". Q returns to the board. ***\x1b[0m");
  enterChat(op, "\r\n\x1b[1;33m*** Chat with " + caller->user.handle + " (node " + caller->node + "). Q returns to the board. ***\x1b[0m");
}

// Bulletins from BULLETINS.INI that this caller may read, numbered from 1.
static const Bulletin *visibleBulletin(const Session &s, uint32_t wanted) {
  uint32_t n = 0;
  for (const auto &b : board.bulletins) if (canSee(s.user, b.level, b.flag) && ++n == wanted) return &b;
  return nullptr;
}
static uint32_t bulletinCount(const Session &s) {
  uint32_t n = 0;
  for (const auto &b : board.bulletins) if (canSee(s.user, b.level, b.flag)) n++;
  return n;
}
static void bulletinMenu(Session &s);
static void bulletinBack(Session &s) { if (bulletinCount(s) > 1) bulletinMenu(s); else mainMenu(s); }
// C4: a bulletin is read in the pager, like every text. An art file is drawn into the pager's
// screen; a bulletin with no file (or a caller without ANSI and no .ASC twin) gets its text.
static void openBulletin(Session &s, const Bulletin &b) {
  Reader &r = s.reader;
  r = Reader();
  r.title = clean(b.title, 24); r.onQuit = bulletinBack; r.utf8 = true;
  String path = b.path;
  bool ansiFile = path.length() && s.ansi;
  if (path.length() && !s.ansi) {
    path.replace("/BBS/", "/BBS/ASCII/");
    if (up(path).endsWith(".ANS")) path = path.substring(0, path.length() - 4) + ".ASC";
  }
  File f;
  if (path.length() && sdReady) f = SD_MMC.open(path, FILE_READ);
  if (f && !f.isDirectory() && f.size()) {
    r.path = path; r.start = 0; r.end = f.size();
    f.close();
    if (ansiFile) { r.kind = READER_ANSI; renderAnsi(r); }
    else { r.kind = READER_TEXT; r.crop = true; indexText(r); }
  } else {
    if (f) f.close();
    r.kind = READER_ROWS;
    r.rows.push_back("");
    r.rows.push_back("\x1b[1;36m" + b.title + "\x1b[0m");
    String text = b.text; text.replace("|", "\n");
    bodyRows(r.rows, text);
  }
  activity(s, "BULLETINS");
  openReader(s, false);
}
static void bulletinMenu(Session &s) {
  activity(s, "BULLETINS");
  uint32_t count = bulletinCount(s);
  if (!count) { say(s, "No bulletins tonight."); s.state = BULLETIN; prompt(s, "Press any key: "); return; }
  if (count == 1) { openBulletin(s, *visibleBulletin(s, 1)); return; }
  say(s, "\r\n\x1b[1;36mBULLETINS\x1b[0m");
  for (uint32_t i = 1; i <= count; ++i) say(s, "\x1b[1;33m[" + String(i) + "]\x1b[0m " + visibleBulletin(s, i)->title);
  s.state = BULLETIN_LIST;
  prompt(s, "Bulletin #, then ENTER (Q goes back at once): ");
}
// The SysOp page: every SysOp online is told, on a line of their own.
static void pageSysop(Session &s) {
  bool sent = false;
  for (auto &n : sessions) {
    if (!n.used || &n == &s || !isSysop(n)) continue;
    timedLine(n, "\x1b[5;31m*** SYSOP PAGE from " + s.user.handle + " (node " + s.node + ") ***\x1b[0m\r\n\x1b[1;33mType /A to answer and chat, or /R to refuse.\x1b[0m");
    n.paged = true; n.pageNode = s.node; n.pageUser = s.user.id; sent = true;
  }
  say(s, sent ? "Paging the SysOp... stay on the line." : "The SysOp is not online.");
  s.state = BULLETIN; prompt(s, "Press any key: ");
}
static void systemInfo(Session &s) {
  std::vector<String> rows;
  rows.push_back("");
  rows.push_back("System: " + board.name + (board.location.length() ? " // " + board.location : String()) + " // " + clockText(boardNow(), true));
  rows.push_back("SysOp: " + board.sysop + "  Nodes: " + String(board.maxNodes) + "  Messages: " + String(nextMessageId - 1));
  rows.push_back("");
  rows.push_back("\x1b[1;36mYour stats\x1b[0m  Calls " + String(s.user.calls) + "  Messages " + s.user.messages +
                 "  Uploaded " + sizeText(s.user.uploadBytes) + "  Downloaded " + sizeText(s.user.downloadBytes) + " (" + s.user.downloads + " files)");
  rows.push_back("Download credit: " + creditText(s.user) + (board.creditsPerMessage ? "  Each message posted earns " + sizeText(board.creditsPerMessage) + "." : String()));
  rows.push_back("");
  openRows(s, rows, "System info", "", mainMenu);
}
static bool mainCommand(Session &s, String &line, const String &cmd) {
  if (s.state == BULLETIN) { mainMenu(s); return true; }   // C6: any key
  if (s.state == MAIN) {
    // C10: a SysOp word still works typed with a slash in front (a letter alone acts at once).
    String word = cmd.startsWith("/") ? cmd.substring(1) : cmd;
    if (cmd == "B") { bulletinMenu(s); }
    else if (cmd == "W" && isSysop(s)) { nodeMonitor(s, MAIN); }
    else if (cmd == "W") { std::vector<String> rows; rows.push_back(""); whoRows(s, rows); rows.push_back(""); openRows(s, rows, "Who is online", "", mainMenu); }
    else if (cmd == "U") { s.userPage = 0; userList(s); }
    else if (cmd == "M") { messageArea(s); }
    else if (cmd == "N") { readNew(s); }
    else if (cmd == "C") { startChat(s, "\x1b[1;33mMulti-Node Chat. Type and press ENTER to talk. Q leaves.\x1b[0m"); }
    else if (cmd == "J" && severalAreas(s.user)) { areaList(s); }
    else if (cmd == "O" && sysopOnline(s)) { pageSysop(s); }
    else if (cmd == "S") { systemInfo(s); }
    else if (cmd == "A") { ansiGallery(s); }
    else if (cmd == "F") { s.galleryCabinet = false; fileMenu(s); }
    else if (cmd == "D") { doorMenu(s); }
    else if (cmd == "?") { keysHelp(s); }
    else if (cmd == "X") { mainMenu(s); }
    else if (cmd == "Q") { say(s, "This is the main menu. G logs off."); prompt(s, "\x1b[1;33mMain Board Command? \x1b[0m"); }
    else if (cmd == "Z" && isSysop(s)) { sysopMenu(s); }
    else if (word == "RELOAD" && isSysop(s)) { say(s, "Settings reloaded: " + loadSettings() + "."); say(s, "Network settings take effect after a restart."); prompt(s, "\x1b[1;33mMain Board Command? \x1b[0m"); }
    else if (word == "USER" && isSysop(s)) { sysopUsers(s); }
    else if (cmd == "Z" || word == "USER") { say(s, "SysOp access requires level 255 and the SYSOP flag."); prompt(s, "\x1b[1;33mMain Board Command? \x1b[0m"); }
    else if (cmd == "G") { s.state = GOODBYE; activity(s, "LOGOFF"); uint32_t mins = (millis() - s.connectedAt) / 60000; art(s, board.goodbye.c_str()); say(s, "Time online: " + String(mins) + " minutes"); }
    else if (!cmd.length()) mainMenu(s);
    else { say(s, "Not a key here. ? shows the keys."); prompt(s, "\x1b[1;33mMain Board Command? \x1b[0m"); }
    return true;
  }
  if (s.state == USER_LIST) {
    if (cmd == "Q" || cmd == "X") { mainMenu(s); return true; }
    if (cmd == "?") { keysHelp(s); return true; }
    if (cmd == "Z" && isSysop(s)) { sysopUsers(s); return true; }
    uint16_t pages = userListPages();
    if (cmd == "P" && s.userPage) s.userPage--;
    else if ((cmd == "N" || !cmd.length()) && s.userPage + 1 < pages) s.userPage++;
    userList(s); return true;
  }
  if (s.state == BULLETIN_LIST) {
    uint32_t n;
    if (cmd == "Q" || cmd == "X") { mainMenu(s); return true; }
    if (cmd == "?") { keysHelp(s); return true; }
    if (!line.length()) { bulletinMenu(s); return true; }
    const Bulletin *b = readNumber(line, n) ? visibleBulletin(s, n) : nullptr;
    if (!b) { say(s, "No such bulletin."); prompt(s, "Bulletin #, then ENTER (Q goes back at once): "); }
    else openBulletin(s, *b);
    return true;
  }
  if (s.state == CHAT) { chatCommand(s, line, cmd); return true; }
  return false;
}