#pragma once
// SysOp console and door settings. The user editor is in bbs/user_editor.h.
// Part of the single TinyBBS translation unit: included in order from main.cpp.

static bool sysopCommand(Session &s, String &line, const String &cmd) {
  if (s.state == SYSOP_MENU) {
    if (cmd == "Q" || !cmd.length()) { mainMenu(s); return true; }
    if (cmd == "U") { sysopUsers(s); return true; }
    if (cmd == "D") { sysopDoors(s); return true; }
    if (cmd == "W") { nodeMonitor(s, SYSOP_MENU); return true; }
    if (cmd == "I") { uploadQueue(s); return true; }
    if (tokenAt(cmd, 0) == "L") { sysopLogs(s, afterTokens(line, 1)); return true; }
    if (cmd == "R" || cmd == "RELOAD") {
      say(s, "Settings reloaded: " + loadSettings() + ".");
      say(s, "Network settings take effect after a restart.");
    }
    else if (cmd == "F" && s.web) {
      write(s, "\x1b]9;LINK;/sysop?t=" + issueAdminToken(s.user) + ";OPEN FILE MANAGER\x07");
      say(s, "\x1b[1;36mFile manager armed. Tap OPEN FILE MANAGER below the terminal.\x1b[0m");
      say(s, "The link stays valid for 30 minutes of use.");
    }
    else if (cmd == "S") {
      say(s, "Calls: " + String(callers) + "  Users next #: " + String(nextUserId) + "  Messages: " + String(nextMessageId - 1));
      say(s, "Nodes: " + String(board.maxNodes) + "  SD: " + (sdReady ? "online" : "offline"));
    } else say(s, s.web ? "Use U, D, W, I, L, S, R, F, or Q." : "Use U, D, W, I, L, S, R, or Q.");
    prompt(s, "SysOp command? "); return true;
  }
  if (s.state == SYSOP_DOORS) {
    if (cmd == "Q" || !cmd.length()) { sysopMenu(s); return true; }
    String verb = tokenAt(cmd, 0), args = afterTokens(line, 1);
    int split = args.lastIndexOf(' ');
    uint32_t add = 0;
    if (verb == "F") {
      if (split < 0 || !readNumber(args.substring(split + 1), add) || add < 1 || add > 255) {
        prompt(s, "Use F <handle> <1-255>: ");
        return true;
      }
      args = args.substring(0, split);
    } else if (verb != "V") { prompt(s, "Use F, V, or Q: "); return true; }
    User target;
    if (!findUser(up(args), target)) { prompt(s, "No such caller. Door settings command? "); return true; }
    Session *live = nullptr;
    for (auto &other : sessions)
      if (other.used && other.user.id == target.id && playingQuest(other)) { live = &other; break; }
    Quest quest;
    if (live) quest = live->quest;
    else if (!loadQuest(target.id, quest)) { prompt(s, "Door storage unavailable. Command? "); return true; }
    if (add) {
      if (quest.fights + add > 255) { prompt(s, "Maximum is 255 remaining fights. Command? "); return true; }
      quest.fights += add;
      if (!saveQuest(quest)) { prompt(s, "Could not save forest fights. Command? "); return true; }
      if (live) live->quest.fights = quest.fights;
      event("SYSOP_FIGHTS", s.user.handle + " -> " + target.handle + " +" + add);
    }
    say(s, target.handle + " has " + quest.fights + " forest fights remaining.");
    prompt(s, "Door settings command? "); return true;
  }
  return false;
}
