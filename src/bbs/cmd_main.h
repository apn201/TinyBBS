#pragma once
// Main board, bulletins, user list, conferences and node chat.
// Part of the single TinyBBS translation unit: included in order from main.cpp.

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
    else if (cmd == "P") { bool sent = false; for (auto &n : sessions) if (n.used && isSysop(n)) { say(n, "\x1b[5;31m*** SYSOP PAGE from " + s.user.handle + " (node " + s.node + ") ***\x1b[0m"); n.paged = true; sent = true; } say(s, sent ? "Page sent." : "The SysOp is not online."); s.state = BULLETIN; prompt(s, "Press ENTER: "); }
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
