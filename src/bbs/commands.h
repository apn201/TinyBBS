#pragma once
// Line input and command dispatch.
// Part of the single TinyBBS translation unit: included in order from main.cpp.

static void handleLine(Session &s, String line) {
  s.lastAt = millis(); line.trim(); String cmd = up(line);
  if (s.user.locked) { closeSession(s); return; }
  if ((s.state == SYSOP_MENU || s.state == SYSOP_USERS || s.state == SYSOP_DOORS || s.state == SYSOP_ADD_PASSWORD || s.state == SYSOP_DELETE_CONFIRM) && !isSysop(s)) {
    s.hideInput = false;
    s.editId = 0;
    mainMenu(s);
    return;
  }
  if (s.authenticated && !canJoinConference(s.user, s.conference)) {
    s.conference = 0;
    s.lastRead = 0;
    mainMenu(s);
    return;
  }
  if (s.state == LOGIN_CHOICE && (cmd == "ASCII" || cmd == "ANSI")) {
    s.ansi = cmd == "ANSI";
    loginPrompt(s);
    return;
  }
  if (s.state == LOGIN_CHOICE) { if (cmd == "N" || cmd == "NEW") { s.state = NEW_HANDLE; prompt(s, "Handle (3-16 chars): "); } else if (cmd == "L" || cmd == "LOGIN") { s.state = LOGIN_HANDLE; prompt(s, "Handle: "); } else if (cmd == "G") { closeSession(s); } else prompt(s, "N, L or G? "); return; }
  if (s.state == LOGIN_HANDLE) { s.pendingHandle = clean(line, 16, true); User u; if (!s.pendingHandle.length() || !findUser(up(s.pendingHandle), u)) { say(s, "Unknown caller."); s.state = LOGIN_CHOICE; prompt(s, "(N)ew user, (L)ogin, or (G)oodbye? "); } else if (u.locked) { say(s, "This account is locked. Page the SysOp."); event("AUTH_LOCKED", u.handle); s.state = LOGIN_CHOICE; prompt(s, "(N)ew user, (L)ogin, or (G)oodbye? "); } else { s.user = u; s.state = LOGIN_PASSWORD; s.hideInput = true; prompt(s, "Password: "); } return; }
  if (s.state == LOGIN_PASSWORD) { s.hideInput = false; if (sha256hex(s.user.salt + ":" + line) != s.user.hash) { say(s, "\r\nBad password."); event("AUTH_FAIL", s.pendingHandle); s.state = LOGIN_CHOICE; prompt(s, "(N)ew user, (L)ogin, or (G)oodbye? "); } else finishLogin(s); return; }
  if (s.state == NEW_HANDLE) { s.pendingHandle = clean(line, 16, true); User exists; if (s.pendingHandle.length() < 3 || findUser(up(s.pendingHandle), exists)) { say(s, "Handle unavailable."); prompt(s, "Handle (3-16 chars): "); } else { s.state = NEW_PASSWORD; s.hideInput = true; prompt(s, "Password (6+ chars): "); } return; }
  if (s.state == NEW_PASSWORD) { s.hideInput = false; if (line.length() < 6) { say(s, "\r\nToo short."); s.state = NEW_HANDLE; prompt(s, "Handle: "); } else { s.pendingPass = line; s.state = NEW_REAL; prompt(s, "Real name: "); } return; }
  if (s.state == NEW_REAL) { s.pendingReal = clean(line, 40); s.state = NEW_LOCATION; prompt(s, "Location: "); return; }
  if (s.state == NEW_LOCATION) {
    trace("NEW_USER_LOCATION", s.pendingHandle + " / " + clean(line, 40));
    s.user = User(); s.user.handle = s.pendingHandle; s.user.normalized = up(s.pendingHandle); s.user.realName = s.pendingReal; s.user.location = clean(line, 40); s.user.salt = salt(); s.user.hash = sha256hex(s.user.salt + ":" + s.pendingPass); s.user.level = (s.user.normalized == "SYSOP" && !anySysop()) ? 255 : config("/BBS/CONFIG/SYSTEM.INI", "new_user_level", "20").toInt(); if (s.user.level == 255) s.user.flags = "SYSOP";
    if (!appendUser(s.user)) { say(s, "\r\nDatabase write failed: " + (storageFault.length() ? storageFault : "see Serial console") + "."); trace("NEW_USER_FAIL", s.pendingHandle); s.state = LOGIN_CHOICE; prompt(s, "(N)ew user, (L)ogin, or (G)oodbye? "); return; }
    say(s, "\r\nAccount created. Welcome aboard."); event("NEW_USER", s.user.handle); trace("NEW_USER_OK", s.user.handle); finishLogin(s); return;
  }
  if (s.state == BULLETIN) { mainMenu(s); return; }
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
    return;
  }
  if (s.state == USER_LIST) {
    if (cmd == "Q") { mainMenu(s); return; }
    if (cmd == "Z" && isSysop(s)) { sysopUsers(s); return; }
    uint16_t pages = userListPages(false);
    if (cmd == "P" && s.userPage) s.userPage--;
    else if (!cmd.length() && s.userPage + 1 < pages) s.userPage++;
    userList(s); return;
  }
  if (s.state == SYSOP_MENU) {
    if (cmd == "Q" || !cmd.length()) { mainMenu(s); return; }
    if (cmd == "U") { sysopUsers(s); return; }
    if (cmd == "D") { sysopDoors(s); return; }
    if (cmd == "W") who(s);
    else if (cmd == "S") {
      say(s, "Calls: " + String(callers) + "  Users next #: " + String(nextUserId) + "  Messages: " + String(nextMessageId - 1));
      say(s, "Nodes: " + String(MAX_NODES) + "  SD: " + (sdReady ? "online" : "offline"));
    } else say(s, "Use U, D, W, S, or Q.");
    prompt(s, "SysOp command? "); return;
  }
  if (s.state == SYSOP_DOORS) {
    if (cmd == "Q" || !cmd.length()) { sysopMenu(s); return; }
    String verb = tokenAt(cmd, 0), args = afterTokens(line, 1);
    int split = args.lastIndexOf(' ');
    uint32_t add = 0;
    if (verb == "F") {
      if (split < 0 || !readNumber(args.substring(split + 1), add) || add < 1 || add > 255) {
        prompt(s, "Use F <handle> <1-255>: ");
        return;
      }
      args = args.substring(0, split);
    } else if (verb != "V") { prompt(s, "Use F, V, or Q: "); return; }
    User target;
    if (!findUser(up(args), target)) { prompt(s, "No such caller. Door settings command? "); return; }
    Session *live = nullptr;
    for (auto &other : sessions)
      if (other.used && other.user.id == target.id && playingQuest(other)) { live = &other; break; }
    Quest quest;
    if (live) quest = live->quest;
    else if (!loadQuest(target.id, quest)) { prompt(s, "Door storage unavailable. Command? "); return; }
    if (add) {
      if (quest.fights + add > 255) { prompt(s, "Maximum is 255 remaining fights. Command? "); return; }
      quest.fights += add;
      if (!saveQuest(quest)) { prompt(s, "Could not save forest fights. Command? "); return; }
      if (live) live->quest.fights = quest.fights;
      event("SYSOP_FIGHTS", s.user.handle + " -> " + target.handle + " +" + add);
    }
    say(s, target.handle + " has " + quest.fights + " forest fights remaining.");
    prompt(s, "Door settings command? "); return;
  }
  if (s.state == SYSOP_DELETE_CONFIRM) {
    if (cmd == "Q" || !cmd.length()) { say(s, "Delete cancelled."); sysopUsers(s); return; }
    if (cmd != "DELETE " + s.pendingHandle) { say(s, "Not deleted. Type DELETE " + s.pendingHandle + " exactly, or Q."); prompt(s, "Confirm delete: "); return; }
    User target;
    if (!findUser(s.pendingHandle, target) || !removeUser(target)) say(s, "Delete failed. The user record was left intact.");
    else { event("SYSOP_DELETE", s.user.handle + " removed " + target.handle); say(s, "User " + target.handle + " deleted."); for (auto &n : sessions) if (n.used && n.user.id == target.id) { say(n, "\r\nYour account was removed by the SysOp."); closeSession(n); } }
    sysopUsers(s); return;
  }
  if (s.state == SYSOP_ADD_PASSWORD) {
    if (line.length() && line.length() < 6) { prompt(s, "\r\nPassword (6+ chars, ENTER cancels): "); return; }
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
    sysopUsers(s); return;
  }
  if (s.state == SYSOP_USERS) {
    if (s.editId && (cmd == "Q" || !cmd.length())) {
      s.editId = 0;
      sysopUsers(s);
      return;
    }
    if (cmd == "Q" || !cmd.length()) { sysopMenu(s); return; }
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
      prompt(s, "User editor command? "); return;
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
        prompt(s, "User editor command? "); return;
      }
    }
    if (verb == "L") { s.userPage = numberAt(line, 1); showUserList(s, true); prompt(s, "User editor command? "); return; }
    if (verb == "V") { User target; if (!handle.length() || !findUser(handle, target) || (s.editId && target.id != s.editId)) say(s, "No such user."); else showUserRecord(s, target); prompt(s, "User editor command? "); return; }
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
        prompt(s, "Password (6+ chars, ENTER cancels): "); return;
      }
      prompt(s, "User editor command? "); return;
    }
    if (verb == "DEL") {
      User target; if (!handle.length() || !findUser(handle, target)) { say(s, "No such user."); prompt(s, "User editor command? "); return; }
      if (target.id == s.user.id || target.normalized == "SYSOP") { say(s, "The primary SysOp account may not be deleted here."); prompt(s, "User editor command? "); return; }
      s.pendingHandle = target.normalized; s.state = SYSOP_DELETE_CONFIRM; say(s, "\x1b[1;31mPermanently delete " + target.handle + "?\x1b[0m"); prompt(s, "Type DELETE " + target.normalized + " to confirm: "); return;
    }
    User target;
    if (!handle.length() || !findUser(handle, target) || (s.editId && target.id != s.editId)) { say(s, "Usage error or no such user. Use E, V, L, ADD, LEVEL, LOCK, UNLOCK, FLAGS, QUOTA, CREDIT, RATIO, DEL, Q."); prompt(s, "User editor command? "); return; }
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
    prompt(s, "User editor command? "); return;
  }
  if (s.state == ANSI_GALLERY) {
    if (cmd == "Q" || !cmd.length()) { mainMenu(s); return; }
    if (cmd == "G" && canUseArea(s, 1)) {
      s.galleryCabinet = true;
      s.fileArea = 1;
      s.fileOffset = 0;
      listArea(s);
      return;
    }
    if (cmd == "A") art(s, "/BBS/ANSI/LOGIN/WELCOME.ANS");
    else if (cmd == "F") art(s, "/BBS/ANSI/MENUS/FILES.ANS");
    else say(s, "Try A, F, G, or Q.");
    prompt(s, "[A] Login splash  [F] File cabinet  [G] Browse art  [Q] Main board: "); return;
  }
  if (s.state == FILE_MENU) {
    if (cmd == "Q" || !cmd.length()) { mainMenu(s); return; }
    if (cmd.length() == 1 && cmd[0] >= '1' && cmd[0] <= '8') { uint8_t area = cmd[0] - '1'; if (canUseArea(s, area)) { s.fileArea = area; s.fileOffset = 0; listArea(s); } else { say(s, "That cabinet is locked."); prompt(s, "File area? "); } }
    else { say(s, "Choose a cabinet number, or Q."); prompt(s, "File area? "); } return;
  }
  if (s.state == FILE_LIST) {
    if (cmd == "Q" || !cmd.length()) {
      if (s.galleryCabinet) { s.galleryCabinet = false; ansiGallery(s); }
      else fileMenu(s);
      return;
    }
    if (tokenAt(cmd, 0) == "V") {
      uint32_t number;
      if (!readNumber(afterTokens(line, 1), number) || !number || number > UINT16_MAX)
        prompt(s, "Use V <file number>: ");
      else openFileViewer(s, number);
      return;
    }
    if (cmd == "L") { listArea(s); return; }
    if (cmd == "N" || cmd == "P") {
      if (cmd == "N" && s.moreFiles && s.fileOffset <= UINT16_MAX - 32) s.fileOffset += 16;
      else if (cmd == "P" && s.fileOffset) s.fileOffset -= 16;
      else { prompt(s, "End of list. File command? "); return; }
      listArea(s);
      return;
    }
    if (cmd.startsWith("D")) { uint16_t wanted = line.substring(1).toInt(); String path, name, reason; size_t bytes = 0; if (!areaFile(s, wanted, path, name, bytes)) say(s, "No such file in this cabinet."); else if (!downloadAllowed(s.user, bytes, reason)) say(s, reason + ". Ask the SysOp."); else if (s.web) { String url = String("/download?n=") + s.node + "&q=" + s.token + "&i=" + wanted; write(s, "\x1b]9;DL;" + url + ";" + name + "\x07\r\n"); say(s, "\x1b[1;36mDownload link armed for " + name + ".\x1b[0m"); } else say(s, "Use the web terminal for downloads from this ESP32 node."); prompt(s, "File command? "); return; }
    say(s, s.web ? "Use V <number>, D <number>, N, P, L, or Q."
                 : "Use V <number>, N, P, L, or Q."); prompt(s, "File command? "); return;
  }
  if (s.state == FILE_VIEW) {
    auto &view = s.viewer;
    if (cmd == "Q") { listArea(s); return; }
    if (cmd == "R") { openFileViewer(s, view.number); return; }
    if (cmd == "P" && view.depth) {
      view.offset = view.history[--view.depth];
      renderFilePage(s);
      return;
    }
    if ((cmd == "N" || !cmd.length()) && view.next) {
      constexpr uint8_t cap = sizeof(view.history) / sizeof(view.history[0]);
      if (view.depth == cap) {
        memmove(view.history, view.history + 1, (cap - 1) * sizeof(view.history[0]));
        view.depth--;
      }
      view.history[view.depth++] = view.offset;
      view.offset = view.next;
      renderFilePage(s);
      return;
    }
    renderFilePage(s);
    return;
  }
  if (s.state == DOOR_MENU) {
    if (cmd == "Q" || !cmd.length()) { mainMenu(s); return; }
    if (cmd == "L" || cmd == "K") { enterQuest(s); return; }
    if (cmd == "V") { enterMud(s); return; }
    if (cmd == "D") { if (!loadQuest(s)) { prompt(s, "Door command? "); return; } s.state=DICE_BET; activity(s,"DICE CABINET"); say(s,"\x1b[1;33mThe neon dice cabinet takes 1-50 gold. Roll 4-6 to double your stake.\x1b[0m"); prompt(s,"Bet (Q quits): "); return; }
    say(s, "Choose L, V, D, or Q."); prompt(s, "Door command? "); return;
  }
  if (s.state == MUD_MENU) { handleMudInput(s, line); return; }
  if (s.state == QUEST_MENU) {
    if (cmd == "Q") { doorMenu(s); return; }
    if (cmd == "F") { enterForest(s); return; }
    if (cmd == "M") { questTrain(s); return; }
    if (cmd == "S") { questShop(s); return; }
    if (cmd == "T") { questTavern(s, true); return; }
    if (cmd == "L") { questLeaderboard(s); questMenu(s); return; }
    if (cmd == "C" || cmd.startsWith("C ")) { questChallenge(s, line); return; }
    if (cmd == "H") {
      uint16_t missing = s.quest.maxHp - s.quest.hp;
      uint16_t cost = missing * 2;
      if (!missing) say(s, "You are already in fighting shape.");
      else if (s.quest.gold < cost) say(s, "You need " + String(cost) + " gold.");
      else {
        Quest updated = s.quest;
        updated.gold -= cost;
        updated.hp = updated.maxHp;
        if (saveQuest(updated)) {
          s.quest = updated;
           say(s, "The healer restores your HP.");
        } else say(s, "Door state could not be saved.");
      }
      questMenu(s);
      return;
    }
    if (cmd == "B") { s.state=QUEST_BANK; activity(s,"KOUVOLA BANK"); say(s,"Bank holds "+String(s.quest.bank)+" gold. Use D <amount>, W <amount>, or Q."); prompt(s,"Bank command? "); return; }
    say(s,"Use F, H, B, T, M, S, L, C <name>, or Q."); prompt(s,"Town command? "); return;
  }
  if (s.state == QUEST_TAVERN) {
    if (cmd == "Q" || !cmd.length()) { questMenu(s); return; }
    if (cmd == "R") { tavernRumor(s); return; }
    if (cmd == "W") { tavernWho(s); return; }
    if (cmd == "L") { questLeaderboard(s); questTavern(s); return; }
    if (cmd == "M") { questMeal(s); return; }
    if (cmd == "C" || cmd.startsWith("C ")) { questChallenge(s, line); return; }
    say(s, "Use R, W, L, C <name>, M, or Q."); prompt(s, "Tavern command? "); return;
  }
  if (s.state == QUEST_DUEL_WAIT) {
    if (cmd == "Q") duelCancel(s, "you withdrew the challenge");
    else prompt(s, "Waiting for the other adventurer. [Q] Cancel: ");
    return;
  }
  if (s.state == QUEST_DUEL_OFFER) {
    if (cmd == "Y" || cmd == "YES") {
      Session *other = sessionByNode(s.duelNode);
      if (!other || other->state != QUEST_DUEL_WAIT) { duelCancel(s, "the challenge expired"); return; }
      s.state = other->state = QUEST_DUEL;
      s.duelTurnNode = other->duelTurnNode = other->node;
      activity(s, "ARENA DUEL"); activity(*other, "ARENA DUEL");
      say(s, "\r\nThe arena gate opens. " + other->user.handle + " has the initiative.");
      say(*other, "\r\nThe arena gate opens. You have the initiative.");
      prompt(s, "Wait for the opening. [A]ttack  [R]etreat: ");
      prompt(*other, "[A]ttack  [R]etreat: ");
    } else if (cmd == "N" || cmd == "NO" || cmd == "Q") duelCancel(s, "the challenge was declined");
    else prompt(s, "Accept? [Y]es / [N]o: ");
    return;
  }
  if (s.state == QUEST_DUEL) {
    if (cmd == "R") { duelCancel(s, s.user.handle + " retreated"); return; }
    if (cmd == "A") duelAttack(s);
    else prompt(s, s.node == s.duelTurnNode ? "[A]ttack  [R]etreat: " : "Wait for your initiative... ");
    return;
  }
  if (s.state == QUEST_DUEL_RESULT) {
    if (!cmd.length() || cmd == "Q") questMenu(s);
    else prompt(s, "[ENTER] Return to town: ");
    return;
  }
  if (s.state == QUEST_FIGHT) {
    if (cmd == "A") { questAttack(s); return; }
    if (cmd == "R" && saveQuest(s)) {
      say(s, "You escape with " + String(s.quest.hp) + " HP. The forest keeps the turn.");
      s.state = QUEST_RESULT;
      prompt(s, "[ENTER] Town  [F] Search the forest again: ");
      return;
    }
    prompt(s, "[A]ttack  [R]un? "); return;
  }
  if (s.state == QUEST_RESULT) {
    if (cmd == "F" && s.quest.fights) enterForest(s);
    else if (!cmd.length() || cmd == "Q") questMenu(s);
    else prompt(s, s.quest.fights ? "[ENTER] Town  [F] Search again: " : "No fights left. [ENTER] Town: ");
    return;
  }
  if (s.state == QUEST_SHOP) {
    if (cmd == "Q" || !cmd.length()) { questMenu(s); return; }
    String verb = tokenAt(cmd, 0);
    uint32_t tier;
    if ((verb != "W" && verb != "A") || !readNumber(afterTokens(line, 1), tier) || tier < 1 || tier > MAX_GEAR) {
      prompt(s, "Use W 1-3, A 1-3, or Q: "); return;
    }
    Quest purchase = s.quest;
    uint8_t &equipped = verb == "W" ? purchase.weapon : purchase.armor;
    if (tier <= equipped) say(s, "You already have equal or better gear.");
    else if (purchase.gold < gearPrice[tier]) say(s, "You cannot afford it.");
    else {
      equipped = tier;
      purchase.gold -= gearPrice[tier];
      if (saveQuest(purchase)) {
        s.quest = purchase;
        say(s, String("Equipped ") + (verb == "W" ? weapons[tier] : armorNames[tier]) +
               ". Gold: " + s.quest.gold);
      } else say(s, "Purchase could not be saved.");
    }
    prompt(s, "[W #] Weapon  [A #] Armour  [Q] Town: "); return;
  }
  if (s.state == QUEST_BANK) {
    if (cmd == "Q" || !cmd.length()) { questMenu(s); return; }
    String verb = tokenAt(cmd, 0);
    uint32_t amount;
    if ((verb != "D" && verb != "W") || !readNumber(afterTokens(line, 1), amount) || !amount) {
      prompt(s, "Use D <amount>, W <amount>, or Q: "); return;
    }
    Quest updated = s.quest;
    uint32_t &from = verb == "D" ? updated.gold : updated.bank;
    uint32_t &to = verb == "D" ? updated.bank : updated.gold;
    if (from < amount || UINT32_MAX - to < amount) say(s, "That transfer is unavailable.");
    else {
      from -= amount;
      to += amount;
      if (saveQuest(updated)) {
        s.quest = updated;
        say(s, "Purse: " + String(updated.gold) + "  Bank: " + updated.bank);
      } else say(s, "Transfer could not be saved.");
    }
    prompt(s, "Bank command? "); return;
  }
  if (s.state == DICE_BET) { if (cmd == "Q" || !cmd.length()) { doorMenu(s); return; } uint32_t bet=line.toInt(); if (!bet || bet>50 || bet>s.quest.gold) { say(s,"Bet 1-50 gold, within your purse."); prompt(s,"Bet (Q quits): "); return; } s.quest.gold-=bet; uint8_t roll=1+(esp_random()%6); say(s,"The cube lands on \x1b[1;33m"+String(roll)+"\x1b[0m."); if (roll>=4) { s.quest.gold+=bet*2; say(s,"\x1b[1;32mWinner. The machine pays double.\x1b[0m"); } else say(s,"\x1b[1;31mThe cabinet keeps the stake.\x1b[0m"); saveQuest(s); prompt(s,"Bet again (Q quits): "); return; }
  if (s.state == MSG_MENU) { if (cmd == "R") { showMessages(s, false); prompt(s, "Read message #: "); s.state = MSG_READ; } else if (cmd == "N") { showMessages(s, true); prompt(s, "Read message #: "); s.state = MSG_READ; } else if (cmd == "P") { s.state = POST_TO; activity(s, "POSTING MESSAGE"); prompt(s, "To (ENTER = ALL): "); } else mainMenu(s); return; }
  if (s.state == MSG_READ) { if (line.length()) { if (!readMessage(s, line.toInt())) say(s, "No such message."); } s.state = MSG_MENU; prompt(s, "(R)ead headers  (N)ew scan  (P)ost  (Q)uit: "); return; }
  if (s.state == POST_TO) { s.pendingTo = clean(line, 16, true); s.state = POST_SUBJECT; prompt(s, "Subject: "); return; }
  if (s.state == POST_SUBJECT) { s.pendingSubject = clean(line, 55); s.state = POST_BODY; prompt(s, "Text (one line in 0.1): "); return; }
  if (s.state == POST_BODY) { Message m; m.conference = s.conference; m.from = s.user.handle; m.to = s.pendingTo; m.subject = s.pendingSubject.length() ? s.pendingSubject : "No subject"; m.body = clean(line, 900); m.when = nowText(); if (m.body.length() && appendMessage(m)) { s.user.messages++; touchUser(s.user); event("MESSAGE", s.user.handle + " #" + m.id); say(s, "Message #" + String(m.id) + " posted."); } else say(s, "Message not posted."); mainMenu(s); return; }
  if (s.state == JOIN_CONF) { uint8_t c = line.toInt(); if (!canJoinConference(s.user, c)) say(s, "That conference is not available to you."); else { s.conference = c; say(s, "Joined " + confName(c) + "."); } mainMenu(s); return; }
  if (s.state == CHAT) { if (cmd == "/Q") { broadcastChat(s, "left the chat."); mainMenu(s); } else if (line.length()) { broadcastChat(s, line); prompt(s, "> "); } return; }
}
static void input(Session &s, const String &bytes) {
  for (uint16_t i = 0; i < bytes.length() && s.used && s.state != GOODBYE; ++i) { char c = bytes[i];
    // CRLF is one Enter key.  Without this guard the LF becomes a phantom
    // handle/command in Telnet clients such as PuTTY.
    if (c == '\r') { if (!s.hideInput) say(s); String line = s.input; s.input = ""; s.ignoreNextLf = true; handleLine(s, line); }
    else if (c == '\n') { if (s.ignoreNextLf) { s.ignoreNextLf = false; continue; } if (!s.hideInput) say(s); String line = s.input; s.input = ""; handleLine(s, line); }
    else if (c == 8 || c == 127) { if (s.input.length()) { s.input.remove(s.input.length() - 1); if (!s.hideInput) write(s, "\b \b"); } }
    else if ((uint8_t)c >= 32 && s.input.length() < 900) { s.input += c; if (!s.hideInput) write(s, String(c)); }
  }
}
