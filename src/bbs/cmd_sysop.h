#pragma once
// SysOp console and user editor.
// Part of the single TinyBBS translation unit: included in order from main.cpp.

static bool sysopCommand(Session &s, String &line, const String &cmd) {
  if (s.state == SYSOP_MENU) {
    if (cmd == "Q" || !cmd.length()) { mainMenu(s); return true; }
    if (cmd == "U") { sysopUsers(s); return true; }
    if (cmd == "D") { sysopDoors(s); return true; }
    if (cmd == "W") who(s);
    else if (cmd == "F" && s.web) {
      write(s, "\x1b]9;LINK;/sysop?t=" + issueAdminToken(s.user) + ";OPEN FILE MANAGER\x07");
      say(s, "\x1b[1;36mFile manager armed. Tap OPEN FILE MANAGER below the terminal.\x1b[0m");
      say(s, "The link stays valid for 30 minutes of use.");
    }
    else if (cmd == "S") {
      say(s, "Calls: " + String(callers) + "  Users next #: " + String(nextUserId) + "  Messages: " + String(nextMessageId - 1));
      say(s, "Nodes: " + String(MAX_NODES) + "  SD: " + (sdReady ? "online" : "offline"));
    } else say(s, s.web ? "Use U, D, W, S, F, or Q." : "Use U, D, W, S, or Q.");
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
  if (s.state == SYSOP_DELETE_CONFIRM) {
    if (cmd == "Q" || !cmd.length()) { say(s, "Delete cancelled."); sysopUsers(s); return true; }
    if (cmd != "DELETE " + s.pendingHandle) { say(s, "Not deleted. Type DELETE " + s.pendingHandle + " exactly, or Q."); prompt(s, "Confirm delete: "); return true; }
    User target;
    if (!findUser(s.pendingHandle, target) || !removeUser(target)) say(s, "Delete failed. The user record was left intact.");
    else { event("SYSOP_DELETE", s.user.handle + " removed " + target.handle); say(s, "User " + target.handle + " deleted."); for (auto &n : sessions) if (n.used && n.user.id == target.id) { say(n, "\r\nYour account was removed by the SysOp."); closeSession(n); } }
    sysopUsers(s); return true;
  }
  if (s.state == SYSOP_ADD_PASSWORD) {
    if (line.length() && line.length() < 6) { prompt(s, "\r\nPassword (6+ chars, ENTER cancels): "); return true; }
    s.hideInput = false;
    say(s);
    User created, existing;
    created.handle = s.pendingHandle;
    created.normalized = up(created.handle);
    if (!line.length()) say(s, "Add cancelled.");
    else if (findUser(created.normalized, existing)) say(s, "That handle already exists.");
    else {
      created.salt = salt();
      created.hash = sha256hex(created.salt + ":" + line);
      created.level = s.pendingLevel;
      if (created.level == 255) created.flags = "SYSOP";
      if (appendUser(created)) {
        event("SYSOP_ADD", s.user.handle + " added " + created.handle);
        say(s, "User #" + String(created.id) + " added.");
      } else say(s, "Could not add user.");
    }
    s.pendingHandle = "";
    sysopUsers(s); return true;
  }
  if (s.state == SYSOP_USERS) {
    if (s.editId && (cmd == "Q" || !cmd.length())) {
      s.editId = 0;
      sysopUsers(s);
      return true;
    }
    if (cmd == "Q" || !cmd.length()) { sysopMenu(s); return true; }
    String verb = tokenAt(cmd, 0), handle = up(clean(tokenAt(line, 1), 16, true));
    if (verb == "E") {
      User target;
      if (!findUser(up(afterTokens(line, 1)), target)) say(s, "No such user.");
      else {
        s.editId = target.id;
        s.editHandle = target.normalized;
        showUserRecord(s, target);
        say(s, "[B bytes] Quota (0 unlimited)  [R n|OFF] Download bytes per upload byte");
        say(s, "[C bytes] Credits   [A level] Access   [F flags|-] Permissions");
        say(s, "[K] Lock   [U] Unlock   [V] View   [Q] Return");
      }
      prompt(s, "User editor command? "); return true;
    }
    String value = tokenAt(line, 2);
    if (s.editId) {
      handle = s.editHandle;
      value = tokenAt(line, 1);
      if (verb == "B") verb = "QUOTA";
      else if (verb == "R") verb = "RATIO";
      else if (verb == "C") verb = "CREDIT";
      else if (verb == "A") verb = "LEVEL";
      else if (verb == "F") verb = "FLAGS";
      else if (verb == "K") verb = "LOCK";
      else if (verb == "U") verb = "UNLOCK";
      else if (verb != "V") {
        say(s, "Use B, R, C, A, F, K, U, V, or Q.");
        prompt(s, "User editor command? "); return true;
      }
    }
    if (verb == "L") { s.userPage = numberAt(line, 1); showUserList(s, true); prompt(s, "User editor command? "); return true; }
    if (verb == "V") { User target; if (!handle.length() || !findUser(handle, target) || (s.editId && target.id != s.editId)) say(s, "No such user."); else showUserRecord(s, target); prompt(s, "User editor command? "); return true; }
    if (verb == "ADD") {
      String args = afterTokens(line, 1);
      int split = args.lastIndexOf(' ');
      String newHandle = split > 0 ? clean(args.substring(0, split), 16, true) : String();
      uint32_t level;
      User exists;
      if (newHandle.length() < 3 || !readNumber(args.substring(split + 1), level) || level > 255)
        say(s, "Usage: ADD <handle> <level 0-255>");
      else if (findUser(up(newHandle), exists)) say(s, "That handle already exists.");
      else {
        s.pendingHandle = newHandle;
        s.pendingLevel = level;
        s.state = SYSOP_ADD_PASSWORD;
        s.hideInput = true;
        prompt(s, "Password (6+ chars, ENTER cancels): "); return true;
      }
      prompt(s, "User editor command? "); return true;
    }
    if (verb == "DEL") {
      User target; if (!handle.length() || !findUser(handle, target)) { say(s, "No such user."); prompt(s, "User editor command? "); return true; }
      if (target.id == s.user.id || target.normalized == "SYSOP") { say(s, "The primary SysOp account may not be deleted here."); prompt(s, "User editor command? "); return true; }
      s.pendingHandle = target.normalized; s.state = SYSOP_DELETE_CONFIRM; say(s, "\x1b[1;31mPermanently delete " + target.handle + "?\x1b[0m"); prompt(s, "Type DELETE " + target.normalized + " to confirm: "); return true;
    }
    User target;
    if (!handle.length() || !findUser(handle, target) || (s.editId && target.id != s.editId)) { say(s, "Usage error or no such user. Use E, V, L, ADD, LEVEL, LOCK, UNLOCK, FLAGS, QUOTA, CREDIT, RATIO, DEL, Q."); prompt(s, "User editor command? "); return true; }
    bool changed = false;
    if (verb == "LEVEL") {
      uint32_t level;
      if (!readNumber(value, level) || level > 255) say(s, "Level must be 0 through 255.");
      else if (target.id == s.user.id && level < 255) say(s, "Refusing to lower the active SysOp account.");
      else { target.level = level; changed = true; }
    } else if (verb == "LOCK" || verb == "UNLOCK") {
      if (target.id == s.user.id && verb == "LOCK") say(s, "Refusing to lock the active SysOp account.");
      else { target.locked = verb == "LOCK"; changed = true; }
    } else if (verb == "FLAGS") {
      String flags = cleanFlags(afterTokens(line, s.editId ? 1 : 2));
      if (target.id == s.user.id && ("," + flags + ",").indexOf(",SYSOP,") < 0) say(s, "The active SysOp must retain the SYSOP flag.");
      else { target.flags = flags; changed = true; }
    } else if (verb == "QUOTA" || verb == "CREDIT") {
      uint32_t bytes;
      if (!readNumber(value, bytes)) say(s, "Enter a byte count from 0 to 4294967295.");
      else { if (verb == "QUOTA") target.downloadQuota = bytes; else target.credits = bytes; changed = true; }
    } else if (verb == "RATIO") {
      String policy = up(value);
      uint32_t multiplier;
      if (policy == "OFF" || policy == "ON") {
        target.ratioEnforced = policy == "ON"; changed = true;
      } else if (readNumber(policy, multiplier) && multiplier >= 1 && multiplier <= 999) {
        target.ratio = multiplier; target.ratioEnforced = true; changed = true;
      } else say(s, "Enter OFF or download bytes per upload byte (1-999).");
    } else say(s, "Unknown user-editor command.");
    if (changed) {
      if (touchUser(target)) {
        refreshLiveUser(target);
        event("SYSOP_USER_EDIT", s.user.handle + " " + verb + " " + target.handle);
        say(s, "Updated " + target.handle + ".");
        if (s.editId) showUserRecord(s, target);
      }
      else say(s, "Update failed; user record was left intact.");
    }
    prompt(s, "User editor command? "); return true;
  }
  return false;
}
