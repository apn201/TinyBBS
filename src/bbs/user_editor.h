#pragma once
// SysOp user editor. The list shows every account with the numbers that
// matter. Opening one shows a record screen where each setting has a number:
// pick it, type the new value, ENTER keeps the old one.
// Part of the single TinyBBS translation unit: included in order from main.cpp.

static constexpr uint8_t USERS_PER_PAGE = 15;

static String sizeText(uint32_t bytes) {
  if (bytes < 1024) return String(bytes) + " B";
  if (bytes < 1048576) return String((bytes + 512) / 1024) + " KB";
  char b[16];
  if (bytes >= 1073741824UL) snprintf(b, sizeof(b), "%.1f GB", bytes / 1073741824.0);
  else snprintf(b, sizeof(b), "%.1f MB", bytes / 1048576.0); return String(b);
}
// "500", "64K", "1.5M", "2MB" -> bytes.
static bool readSize(String v, uint32_t &out) {
  v = up(v);
  if (v.endsWith("B") && v.length() > 1 && !isdigit((unsigned char)v[v.length() - 2])) v.remove(v.length() - 1);
  double mult = 1;
  if (v.endsWith("K")) mult = 1024; else if (v.endsWith("M")) mult = 1048576; else if (v.endsWith("G")) mult = 1073741824.0;
  if (mult > 1) v.remove(v.length() - 1);
  v.trim();
  if (!v.length()) return false;
  char *end; double n = strtod(v.c_str(), &end);
  if (*end || n < 0 || n * mult > 4294967295.0) return false;
  out = (uint32_t)(n * mult + 0.5);
  return true;
}
static std::vector<User> allUsers() {
  std::vector<User> list;
  File f = sdReady ? SD_MMC.open("/BBS/DATA/USERS/USERS.DAT", FILE_READ) : File();
  while (f && f.available()) { User u; if (parseUser(f.readStringUntil('\n'), u)) list.push_back(u); }
  if (f) f.close();
  std::sort(list.begin(), list.end(), [](const User &a, const User &b) { return a.id < b.id; });
  return list;
}
static bool userById(uint32_t id, User &out) {
  for (auto &u : allUsers()) if (u.id == id) { out = u; return true; }
  return false;
}
static String ratioText(const User &u) { return u.ratioEnforced ? "1:" + String(u.ratio) : String("off"); }
static String downloadText(const User &u) {
  return sizeText(u.downloadBytes) + " / " + (u.downloadQuota ? sizeText(u.downloadQuota) : String("no limit"));
}
static void eachFlag(const String &list, std::function<void(const String &)> fn) {
  int start = 0;
  while (start < (int)list.length()) {
    int comma = list.indexOf(',', start); if (comma < 0) comma = list.length();
    String f = up(list.substring(start, comma));
    if (f.length()) fn(f);
    start = comma + 1;
  }
}
// Every flag the board checks, with what it opens, plus flags users already
// carry that nothing checks (shown as "not used").
struct FlagUse { String flag, opens; };
static std::vector<FlagUse> knownFlags() {
  std::vector<FlagUse> flags{{"SYSOP", "SysOp access"}};
  auto add = [&](const String &flag, const String &what) {
    String f = up(flag);
    if (!f.length()) return;
    for (auto &k : flags) if (k.flag == f) { if (what.length() && k.opens.indexOf(what) < 0) k.opens += (k.opens.length() ? ", " : "") + what; return; }
    flags.push_back({f, what});
  };
  for (auto &c : board.conferences) add(c.flag, "messages: " + c.name);
  for (auto &b : board.bulletins) add(b.flag, "bulletin: " + b.title);
  for (uint8_t i = 0; i < FILE_AREA_COUNT; ++i) add(FILE_AREAS[i].flag, String("files: ") + FILE_AREAS[i].name);
  for (auto &u : allUsers()) eachFlag(u.flags, [&](const String &f) { add(f, ""); });
  for (auto &k : flags) if (!k.opens.length()) k.opens = "not used by anything";
  return flags;
}

static bool sessionOnline(uint32_t id) { for (auto &o : sessions) if (o.used && o.user.id == id) return true; return false; }

// ---------- the list -------------------------------------------------------------
static void sysopUsers(Session &s) {
  s.state = SYSOP_USERS; s.editId = 0; s.hideInput = false; activity(s, "SYSOP USER EDITOR");
  auto users = allUsers();
  uint16_t pages = users.empty() ? 1 : (users.size() + USERS_PER_PAGE - 1) / USERS_PER_PAGE;
  if (s.userPage >= pages) s.userPage = 0;
  say(s, "\r\n\x1b[1;35mUSER EDITOR\x1b[0m  " + String(users.size()) + " accounts" + (pages > 1 ? "   page " + String(s.userPage + 1) + "/" + pages : String()));
  char row[140];
  snprintf(row, sizeof(row), "%4s  %-16s %3s %-12s %5s %4s %-19s %s", "#", "Handle", "Lvl", "Flags", "Calls", "Msgs", "Downloads used/max", "Ratio");
  say(s, String("\x1b[1;36m") + row + "\x1b[0m");
  for (size_t i = s.userPage * USERS_PER_PAGE; i < users.size() && i < (size_t)(s.userPage + 1) * USERS_PER_PAGE; ++i) {
    const User &u = users[i];
    snprintf(row, sizeof(row), "%4lu  %-16.16s %3u %-12.12s %5lu %4lu %-19.19s %s", (unsigned long)u.id, u.handle.c_str(), u.level,
             u.flags.length() ? u.flags.c_str() : "-", (unsigned long)u.calls, (unsigned long)u.messages, downloadText(u).c_str(), ratioText(u).c_str());
    say(s, u.locked ? String("\x1b[0;31m") + row + "  LOCKED\x1b[0m" : String(row));
  }
  if (users.empty()) say(s, "No accounts yet.");
  say(s, String("Type a \x1b[1;33m#\x1b[0m or a \x1b[1;33mhandle\x1b[0m to open it.  \x1b[1;33m[A]\x1b[0m Add user  ") +
         (pages > 1 ? "\x1b[1;33m[ENTER]\x1b[0m Next page  \x1b[1;33m[P]\x1b[0m Previous  " : "") + "\x1b[1;33m[Q]\x1b[0m SysOp menu");
  prompt(s, "User editor: ");
}

// ---------- the record -----------------------------------------------------------
static void fieldLine(Session &s, uint8_t n, const String &name, const String &value) {
  String dots = " ";
  for (int i = name.length(); i < 18; ++i) dots += '.';
  say(s, String(n < 10 ? " " : "") + "\x1b[1;33m[" + String(n) + "]\x1b[0m " + name + dots + " \x1b[1;37m" + value + "\x1b[0m");
}
static void showRecord(Session &s, const User &u) {
  s.state = SYSOP_USER_EDIT; s.editId = u.id; s.editHandle = u.normalized; s.hideInput = false;
  activity(s, "SYSOP USER EDITOR");
  Quest q; loadQuest(u.id, q);
  say(s, "");
  say(s, "\x1b[1;35m== USER #" + String(u.id) + "  " + u.handle + " ==\x1b[0m  " +
         (u.locked ? "\x1b[1;31mLOCKED\x1b[0m" : "\x1b[1;32mACTIVE\x1b[0m") + (sessionOnline(u.id) ? "  \x1b[1;36mONLINE\x1b[0m" : ""));
  say(s, "     Calls " + String(u.calls) + "   Messages " + u.messages + "   Uploads " + u.uploads + " (" + sizeText(u.uploadBytes) +
         ")   Downloads " + u.downloads + " (" + sizeText(u.downloadBytes) + ")");
  say(s, "");
  fieldLine(s, 1, "Access level", String(u.level));
  fieldLine(s, 2, "Flags", u.flags.length() ? u.flags : String("none"));
  fieldLine(s, 3, "Account", u.locked ? "locked, cannot log in" : "active");
  fieldLine(s, 4, "Download limit", u.downloadQuota ? sizeText(u.downloadBytes) + " used of " + sizeText(u.downloadQuota) : sizeText(u.downloadBytes) + " used, no limit");
  fieldLine(s, 5, "Credits", sizeText(u.credits));
  fieldLine(s, 6, "Ratio", u.ratioEnforced ? "1:" + String(u.ratio) + ", may download " + u.ratio + "x what they upload" : String("off"));
  fieldLine(s, 7, "Password", "set a new one");
  fieldLine(s, 8, "Forest fights", String(q.fights) + " left today (Legend of Kouvola)");
  fieldLine(s, 9, "Real name", u.realName.length() ? u.realName : String("-"));
  fieldLine(s, 10, "Location", u.location.length() ? u.location : String("-"));
  say(s, "");
  say(s, " \x1b[1;33m[N]\x1b[0m Next user  \x1b[1;33m[P]\x1b[0m Previous  \x1b[1;33m[D]\x1b[0m Delete  \x1b[1;33m[Q]\x1b[0m User list");
  prompt(s, "Field # or command: ");
}
static void saveRecord(Session &s, User &u, const String &what) {
  if (touchUser(u)) {
    refreshLiveUser(u);
    event("SYSOP_USER_EDIT", s.user.handle + " " + what + " " + u.handle);
    say(s, "\x1b[1;32mSaved.\x1b[0m");
  } else say(s, "\x1b[1;31mNot saved: the user file could not be written.\x1b[0m");
}
static void showFlags(Session &s, const User &u) {
  say(s, "\r\n\x1b[1;36mFLAGS FOR " + u.handle + "\x1b[0m");
  for (auto &k : knownFlags()) {
    char row[100];
    snprintf(row, sizeof(row), "  %s %-12s %s", hasFlag(u, k.flag) ? "\x1b[1;32m[x]\x1b[0m" : "[ ]", k.flag.c_str(), k.opens.c_str());
    say(s, row);
  }
  prompt(s, "Flag to switch on or off, or a new flag name (ENTER done): ");
}
static void openField(Session &s, uint8_t field) {
  User u;
  if (!userById(s.editId, u)) { say(s, "That account is gone."); sysopUsers(s); return; }
  if (field == 3) {
    if (u.id == s.user.id) say(s, "You cannot lock your own account.");
    else { u.locked = !u.locked; saveRecord(s, u, u.locked ? "LOCK" : "UNLOCK"); }
    showRecord(s, u); return;
  }
  Quest q;
  s.editField = field; s.state = SYSOP_USER_FIELD;
  switch (field) {
    case 1: prompt(s, "Access level 0-255 (now " + String(u.level) + ", ENTER keeps): "); break;
    case 2: showFlags(s, u); break;
    case 4: prompt(s, "Download limit, e.g. 2M or 500K, 0 = no limit (now " + (u.downloadQuota ? sizeText(u.downloadQuota) : String("no limit")) + "): "); break;
    case 5: prompt(s, "Credits, e.g. 100K (now " + sizeText(u.credits) + "): "); break;
    case 6: prompt(s, "Bytes they may download per byte uploaded, 1-999, or OFF (now " + ratioText(u) + "): "); break;
    case 7: s.hideInput = true; prompt(s, "New password, 6+ characters (ENTER cancels): "); break;
    case 9: prompt(s, "Real name (now " + (u.realName.length() ? u.realName : String("empty")) + ", - clears): "); break;
    case 10: prompt(s, "Location (now " + (u.location.length() ? u.location : String("empty")) + ", - clears): "); break;
    case 8: loadQuest(u.id, q); prompt(s, "Forest fights left today, 0-255 (now " + String(q.fights) + "): "); break;
    default: showRecord(s, u);
  }
}
// The value typed for the field picked in openField().
static void fieldValue(Session &s, const String &line) {
  User u;
  if (!userById(s.editId, u)) { s.hideInput = false; say(s, "That account is gone."); sysopUsers(s); return; }
  String v = line; v.trim();
  uint32_t n = 0;
  if (s.editField == 2) {
    if (!v.length()) { showRecord(s, u); return; }
    String flag = cleanFlags(v);
    if (!flag.length() || flag.indexOf(',') >= 0) { prompt(s, "One flag at a time, letters and digits (ENTER done): "); return; }
    if (hasFlag(u, flag)) {
      if (flag == "SYSOP" && u.id == s.user.id) { say(s, "You cannot remove your own SYSOP flag."); showFlags(s, u); return; }
      String kept;
      eachFlag(u.flags, [&](const String &f) { if (f != flag) kept += (kept.length() ? "," : "") + f; });
      u.flags = kept;
    } else u.flags = cleanFlags(u.flags.length() ? u.flags + "," + flag : flag);
    saveRecord(s, u, "FLAGS");
    showFlags(s, u); return;
  }
  if (s.editField == 7) {
    s.hideInput = false; say(s);
    if (!v.length()) say(s, "Password not changed.");
    else if (v.length() < 6) { s.hideInput = true; prompt(s, "At least 6 characters (ENTER cancels): "); return; }
    else { u.salt = salt(); u.hash = sha256hex(u.salt + ":" + v); saveRecord(s, u, "PASSWORD"); }
    showRecord(s, u); return;
  }
  if (!v.length()) { showRecord(s, u); return; }
  bool ok = false;
  String what;
  switch (s.editField) {
    case 1:
      if (!readNumber(v, n) || n > 255) say(s, "Access level is a number from 0 to 255.");
      else if (u.id == s.user.id && n < 255) say(s, "You cannot lower your own access.");
      else { u.level = n; ok = true; what = "LEVEL"; }
      break;
    case 4: if (readSize(v, n)) { u.downloadQuota = n; ok = true; what = "QUOTA"; } else say(s, "Use a size like 500K or 2M, or 0."); break;
    case 5: if (readSize(v, n)) { u.credits = n; ok = true; what = "CREDITS"; } else say(s, "Use a size like 100K or 1M."); break;
    case 6:
      if (up(v) == "OFF") { u.ratioEnforced = false; ok = true; }
      else if (readNumber(v, n) && n >= 1 && n <= 999) { u.ratio = n; u.ratioEnforced = true; ok = true; }
      else say(s, "Use a number from 1 to 999, or OFF.");
      what = "RATIO";
      break;
    case 9: case 10: {
      String text = v == "-" ? String() : clean(v, 40);
      if (s.editField == 9) { u.realName = text; what = "NAME"; } else { u.location = text; what = "LOCATION"; }
      ok = true;
      break;
    }
    case 8: {
      if (!readNumber(v, n) || n > 255) { say(s, "Forest fights is a number from 0 to 255."); break; }
      Session *live = nullptr;
      for (auto &o : sessions) if (o.used && o.user.id == u.id && playingQuest(o)) { live = &o; break; }
      Quest q;
      if (live) q = live->quest; else loadQuest(u.id, q);
      q.id = u.id; q.fights = n;
      if (saveQuest(q)) { if (live) live->quest.fights = n; event("SYSOP_FIGHTS", s.user.handle + " -> " + u.handle + " " + n); say(s, "\x1b[1;32mSaved.\x1b[0m"); }
      else say(s, "Door storage could not be written.");
      break;
    }
  }
  if (ok) saveRecord(s, u, what);
  showRecord(s, u);
}

static bool userEditorCommand(Session &s, String &line, const String &cmd) {
  if (s.state == SYSOP_NODES) {
    uint32_t n;
    auto back = [&]() { if (s.nodesBack == SYSOP_MENU) sysopMenu(s); else mainMenu(s); };
    if (!cmd.length() || cmd == "Q") { back(); return true; }
    // M <node|ALL> <text>: a one-way message that pops up on the caller's screen.
    if (tokenAt(cmd, 0) == "M") {
      String to = tokenAt(cmd, 1), text = textLine(afterTokens(line, 2), 200);
      uint32_t node = 0;
      bool all = to == "ALL";
      if (!text.length() || (!all && (!readNumber(to, node) || !sessionByNode(node)))) {
        prompt(s, "Use M <node> <text> or M ALL <text>: "); return true;
      }
      uint8_t sent = 0;
      for (auto &o : sessions) {
        if (!o.used || &o == &s || o.state == DEAD || o.state == GOODBYE || (!all && o.node != node)) continue;
        say(o, "\r\n\x1b[1;33m*** Message from the SysOp: " + text + " ***\x1b[0m");
        sent++;
      }
      event("SYSOP_BROADCAST", (all ? String("ALL") : "node " + String(node)) + ": " + text);
      say(s, "Message sent to " + String(sent) + " caller" + (sent == 1 ? "" : "s") + ".");
      prompt(s, "Node # to disconnect, M <node|ALL> <text> to message (ENTER = back): "); return true;
    }
    Session *t = readNumber(cmd, n) && n <= 255 ? sessionByNode(n) : nullptr;
    if (!t) { prompt(s, "No caller on that node. Node # (ENTER = back): "); return true; }
    if (t == &s) { prompt(s, "That is you. Node # (ENTER = back): "); return true; }
    String who = t->user.handle.length() ? t->user.handle : String("a caller logging in");
    event("SYSOP_KICK", s.user.handle + " disconnected " + who + " on node " + t->node);
    t->hideInput = false;
    say(*t, "\r\n\x1b[1;31mThe SysOp has disconnected you. NO CARRIER\x1b[0m");
    t->state = GOODBYE;  // closes once the line above has gone out
    say(s, "\x1b[1;32mNode " + String(n) + " (" + who + ") disconnected.\x1b[0m");
    back(); return true;
  }
  if (s.state == SYSOP_USERS) {
    User u;
    uint32_t n;
    if (cmd == "Q") { sysopMenu(s); return true; }
    if (!cmd.length()) { s.userPage++; sysopUsers(s); return true; }
    if (cmd == "P") { if (s.userPage) s.userPage--; sysopUsers(s); return true; }
    if (cmd == "A") { s.state = SYSOP_ADD_HANDLE; prompt(s, "Handle for the new account (ENTER cancels): "); return true; }
    if ((readNumber(cmd, n) && userById(n, u)) || findUser(up(clean(line, 16, true)), u)) { showRecord(s, u); return true; }
    say(s, "No account with that number or handle.");
    prompt(s, "User editor: "); return true;
  }
  if (s.state == SYSOP_USER_EDIT) {
    uint32_t n;
    User u;
    if (cmd == "Q" || !cmd.length()) { sysopUsers(s); return true; }
    if (cmd == "N" || cmd == "P") {
      auto users = allUsers();
      size_t at = 0;
      for (size_t i = 0; i < users.size(); ++i) if (users[i].id == s.editId) at = i;
      if (users.empty()) { sysopUsers(s); return true; }
      at = cmd == "N" ? (at + 1) % users.size() : (at + users.size() - 1) % users.size();
      showRecord(s, users[at]); return true;
    }
    if (cmd == "D") {
      if (!userById(s.editId, u)) { sysopUsers(s); return true; }
      if (u.id == s.user.id) { say(s, "You cannot delete your own account."); prompt(s, "Field # or command: "); return true; }
      s.state = SYSOP_DELETE_CONFIRM;
      say(s, "\x1b[1;31mDelete " + u.handle + " for good? Their messages stay.\x1b[0m");
      prompt(s, "Type YES to delete: "); return true;
    }
    if (readNumber(cmd, n) && n >= 1 && n <= 10) { openField(s, n); return true; }
    say(s, "Pick a field 1-10, or N, P, D, Q.");
    prompt(s, "Field # or command: "); return true;
  }
  if (s.state == SYSOP_USER_FIELD) { fieldValue(s, line); return true; }
  if (s.state == SYSOP_DELETE_CONFIRM) {
    User u;
    if (cmd != "YES" || !userById(s.editId, u)) { say(s, "Not deleted."); if (userById(s.editId, u)) showRecord(s, u); else sysopUsers(s); return true; }
    if (!removeUser(u)) { say(s, "Delete failed. The account was left as it was."); showRecord(s, u); return true; }
    event("SYSOP_DELETE", s.user.handle + " removed " + u.handle);
    say(s, "\x1b[1;32m" + u.handle + " deleted.\x1b[0m");
    for (auto &o : sessions) if (o.used && o.user.id == u.id) { say(o, "\r\nYour account was removed by the SysOp."); closeSession(o); }
    sysopUsers(s); return true;
  }
  if (s.state == SYSOP_ADD_HANDLE) {
    String handle = clean(line, 16, true);
    User exists;
    if (!line.length()) { sysopUsers(s); return true; }
    if (handle.length() < 3) { prompt(s, "At least 3 letters or digits (ENTER cancels): "); return true; }
    if (findUser(up(handle), exists)) { prompt(s, "That handle is taken. Another one (ENTER cancels): "); return true; }
    s.pendingHandle = handle;
    s.state = SYSOP_ADD_PASSWORD; s.hideInput = true;
    prompt(s, "Password, 6+ characters (ENTER cancels): "); return true;
  }
  if (s.state == SYSOP_ADD_PASSWORD) {
    if (line.length() && line.length() < 6) { prompt(s, "\r\nAt least 6 characters (ENTER cancels): "); return true; }
    s.hideInput = false; say(s);
    if (!line.length()) { say(s, "Add cancelled."); sysopUsers(s); return true; }
    User u;
    u.handle = s.pendingHandle; u.normalized = up(u.handle);
    u.salt = salt(); u.hash = sha256hex(u.salt + ":" + line);
    u.level = board.newUserLevel;
    s.pendingHandle = "";
    if (!appendUser(u)) { say(s, "Could not add the account."); sysopUsers(s); return true; }
    event("SYSOP_ADD", s.user.handle + " added " + u.handle);
    say(s, "\x1b[1;32mAccount #" + String(u.id) + " added.\x1b[0m Set its level and flags below.");
    showRecord(s, u); return true;
  }
  return false;
}
