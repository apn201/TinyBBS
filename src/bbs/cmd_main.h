#pragma once
// Main board, bulletins, user list, conferences and node chat.
// Part of the single TinyBBS translation unit: included in order from main.cpp.

// SysOp page: the SysOp answers with /A from any prompt, which pulls both
// callers into node chat, or refuses with /R.
static void enterChat(Session &s, const String &banner) {
  if (s.duelNode) duelCancel(s, s.user.handle + " was called to chat");
  if (playingMud(s)) mudBroadcastRoom(s, s.user.handle + " is called away by the SysOp.");
  s.hideInput = false;
  s.state = CHAT; activity(s, "CHAT");
  say(s, banner);
  broadcastChat(s, "joined the chat.");
  prompt(s, "> ");
}
static void answerPage(Session &op, bool accept) {
  uint8_t node = op.pageNode; uint32_t uid = op.pageUser;
  for (auto &n : sessions) if (n.pageNode == node) { n.pageNode = 0; n.pageUser = 0; n.paged = false; }
  Session *caller = sessionByNode(node);
  if (!caller || !caller->used || caller->user.id != uid) { say(op, "\r\nThe caller already hung up."); return; }
  if (!accept) {
    say(*caller, "\r\n\x1b[1;31mThe SysOp is busy right now. Try again later.\x1b[0m");
    say(op, "\r\nPage refused.");
    return;
  }
  event("SYSOP_CHAT", op.user.handle + " answered " + caller->user.handle);
  enterChat(*caller, "\r\n\x1b[1;33m*** The SysOp breaks in: you are in chat with " + op.user.handle + ". /Q returns to the board. ***\x1b[0m");
  enterChat(op, "\r\n\x1b[1;33m*** Chat with " + caller->user.handle + " (node " + caller->node + "). /Q returns to the board. ***\x1b[0m");
}

static bool mainCommand(Session &s, String &line, const String &cmd) {
  if (s.state == BULLETIN) { mainMenu(s); return true; }
  if (s.state == MAIN) {
    if (cmd == "B") { art(s, "/BBS/BULLETINS/001_RULES.ANS"); prompt(s, "Press ENTER: "); s.state = BULLETIN; }
    else if (cmd == "W") { who(s); prompt(s, "Press ENTER: "); s.state = BULLETIN; }
    else if (cmd == "U") { s.userPage = 0; userList(s); }
    else if (cmd == "M") { s.state = MSG_MENU; activity(s, "MESSAGES"); art(s, "/BBS/ANSI/MENUS/MESSAGES.ANS"); prompt(s, "Message command? "); }
    else if (cmd == "N") { activity(s, "NEW MESSAGE SCAN"); showMessages(s, true); prompt(s, "Read message #, or ENTER: "); s.state = MSG_READ; }
    else if (cmd == "C") { s.state = CHAT; activity(s, "CHAT"); say(s, "\r\n\x1b[1;33mEntered Multi-Node Chat. /Q returns to the board.\x1b[0m"); broadcastChat(s, "joined the chat."); prompt(s, "> "); }
    else if (cmd == "J") { s.state = JOIN_CONF; say(s, "0 Main Board\r\n1 General Discussion"); if (canJoinConference(s.user, 2)) say(s, "2 The Back Room"); if (canJoinConference(s.user, 3)) say(s, "3 SysOp Console"); prompt(s, "Join conference #: "); }
    else if (cmd == "P") {
      bool sent = false;
      for (auto &n : sessions) {
        if (!n.used || &n == &s || !isSysop(n)) continue;
        say(n, "\r\n\x1b[5;31m*** SYSOP PAGE from " + s.user.handle + " (node " + s.node + ") ***\x1b[0m");
        say(n, "\x1b[1;33mType /A to answer and chat, or /R to refuse.\x1b[0m");
        n.paged = true; n.pageNode = s.node; n.pageUser = s.user.id; sent = true;
      }
      say(s, sent ? "Paging the SysOp... stay on the line." : "The SysOp is not online.");
      s.state = BULLETIN; prompt(s, "Press ENTER: ");
    }
    else if (cmd == "S") { say(s, "System: " + config("/BBS/CONFIG/SYSTEM.INI", "name", "TINYBBS") + " // " + nowText()); say(s, "Nodes: " + String(MAX_NODES) + "  Messages: " + String(nextMessageId - 1)); s.state = BULLETIN; prompt(s, "Press ENTER: "); }
    else if (cmd == "A") { ansiGallery(s); }
    else if (cmd == "F") { s.galleryCabinet = false; fileMenu(s); }
    else if (cmd == "D") { doorMenu(s); }
    else if (cmd == "?" || cmd == "H") { say(s, "Type a command and press ENTER. Q backs out of most menus."); if (isSysop(s)) say(s, "USER opens the user editor. E <handle> selects an account."); s.state = BULLETIN; prompt(s, "Press ENTER: "); }
    else if (cmd == "Z" && isSysop(s)) { sysopMenu(s); }
    else if (cmd == "USER" && isSysop(s)) { sysopUsers(s); }
    else if (cmd == "Z" || cmd == "USER") { say(s, "SysOp access requires level 255 and the SYSOP flag."); prompt(s, "Main Board Command? "); }
    else if (cmd == "G") { s.state = GOODBYE; activity(s, "LOGOFF"); uint32_t mins = (millis() - s.connectedAt) / 60000; art(s, "/BBS/ANSI/GOODBYE/GOODBYE.ANS"); say(s, "Time online: " + String(mins) + " minutes"); }
    else if (!cmd.length()) mainMenu(s);
    else { say(s, "Unknown command. Type ? for help."); prompt(s, "Main Board Command? "); }
    return true;
  }
  if (s.state == USER_LIST) {
    if (cmd == "Q") { mainMenu(s); return true; }
    if (cmd == "Z" && isSysop(s)) { sysopUsers(s); return true; }
    uint16_t pages = userListPages(false);
    if (cmd == "P" && s.userPage) s.userPage--;
    else if (!cmd.length() && s.userPage + 1 < pages) s.userPage++;
    userList(s); return true;
  }
  if (s.state == JOIN_CONF) { uint8_t c = line.toInt(); if (!canJoinConference(s.user, c)) say(s, "That conference is not available to you."); else { s.conference = c; say(s, "Joined " + confName(c) + "."); } mainMenu(s); return true; }
  if (s.state == CHAT) { if (cmd == "/Q") { broadcastChat(s, "left the chat."); mainMenu(s); } else if (line.length()) { broadcastChat(s, line); prompt(s, "> "); } return true; }
  return false;
}
