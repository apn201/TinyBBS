#pragma once
// WHO, user list and SysOp menus.
// Part of the single TinyBBS translation unit: included in order from main.cpp.

static void who(Session &s) {
  say(s, "\x1b[1;36mNode  User             Activity\x1b[0m");
  for (auto &n : sessions) if (n.used && n.state != DEAD) { String h = n.user.handle.length() ? n.user.handle : "(logging in)"; char row[90]; snprintf(row, sizeof(row), "%-5u %-16s %s", n.node, h.c_str(), n.activity.c_str()); say(s, row); }
}
static bool isSysop(const Session &s) { return s.authenticated && !s.user.locked && s.user.level == 255 && hasFlag(s.user, "SYSOP"); }
static void refreshLiveUser(const User &changed) {
  for (auto &n : sessions) if (n.used && n.user.id == changed.id) n.user = changed;
}
static bool downloadAllowed(const User &u, size_t bytes, String &reason) {
  if (u.locked) { reason = "Account locked"; return false; }
  uint64_t proposed = (uint64_t)u.downloadBytes + bytes;
  if (u.downloadQuota && proposed > u.downloadQuota) { reason = "Download quota exceeded"; return false; }
  if (proposed > UINT32_MAX) { reason = "Download counter limit reached"; return false; }
  if (u.ratioEnforced && proposed > (uint64_t)u.uploadBytes * u.ratio + u.credits) { reason = "Ratio or credits do not permit this download"; return false; }
  return true;
}
static void sysopUsers(Session &s);  // bbs/user_editor.h
static size_t uploadsWaiting();      // bbs/uploads.h
// W for the SysOp: who is on, and a node number disconnects that caller.
static void nodeMonitor(Session &s, State back) {
  who(s);
  s.nodesBack = back; s.state = SYSOP_NODES; activity(s, "NODE MONITOR");
  prompt(s, "Node # to disconnect, M <node|ALL> <text> to message (ENTER = back): ");
}
// The caller-facing user list: locked accounts are not shown.
static constexpr uint8_t USER_LIST_PAGE = 10;
static uint16_t showUserList(Session &s) {
  uint16_t visible = 0, shown = 0, start = s.userPage * USER_LIST_PAGE;
  say(s, "\x1b[1;36m#     Handle           Calls  Location\x1b[0m");
  eachUser([&](const User &u) {
    if (u.locked || visible++ < start || shown >= USER_LIST_PAGE) return true;
    shown++;
    char row[116];
    snprintf(row, sizeof(row), "%-5lu %-16s %-6lu %s", (unsigned long)u.id, u.handle.c_str(), (unsigned long)u.calls, u.location.c_str());
    say(s, row);
    return true;
  });
  if (!shown) say(s, start ? "No more callers." : "No callers are listed.");
  uint16_t pages = visible ? (visible + USER_LIST_PAGE - 1) / USER_LIST_PAGE : 1;
  say(s, "\x1b[1;33mPage " + String(s.userPage + 1) + "/" + String(pages) + "  " + String(visible) + " caller" + (visible == 1 ? "" : "s") + "\x1b[0m");
  return pages;
}
static uint16_t userListPages() {
  uint16_t visible = 0;
  eachUser([&](const User &u) { if (!u.locked) visible++; return true; });
  return visible ? (visible + USER_LIST_PAGE - 1) / USER_LIST_PAGE : 1;
}
static void userList(Session &s) {
  s.state = USER_LIST; activity(s, "USER LIST"); uint16_t pages = showUserList(s);
  if (isSysop(s)) say(s, "[Z] User editor: levels, flags, limits and passwords");
  prompt(s, pages > 1 ? "ENTER next, P previous, Q board: " : "Q returns to board: ");
}
static void sysopMenu(Session &s) {
  s.state = SYSOP_MENU; activity(s, "SYSOP CONSOLE");
  art(s, "/BBS/ANSI/MENUS/SYSOP.ANS", shortName() + " // SYSOP");
  say(s, "[U] User editor                    [D] Door settings / forest fights");
  say(s, "[W] Node monitor   [S] Board statistics   [R] Reload settings   [Q] Main Board");
  size_t waiting = uploadsWaiting();
  say(s, "[I] Incoming uploads" + (waiting ? " \x1b[1;35m(" + String(waiting) + " waiting)\x1b[0m" : String(" (none waiting)")));
  say(s, "[L] Logs: logins and what callers do   L <text> shows only matching lines");
  if (s.web) say(s, "[F] File manager (opens a separate admin page)");
  prompt(s, "SysOp command? ");
}
// The event log in the reader, newest at the bottom. With a filter, only the
// lines that contain it (any case): a handle, a node, LOGIN, AUTH_FAIL...
static String logColour(const String &tag) {
  if (tag == "LOGIN" || tag == "NEW_USER") return "\x1b[1;32m";
  if (tag == "LOGOFF" || tag == "CONNECT") return "\x1b[0;32m";
  if (tag.startsWith("AUTH") || tag.indexOf("FAIL") >= 0 || tag.startsWith("SYSOP")) return "\x1b[1;31m";
  if (tag == "DOING") return "\x1b[0;37m";
  return "\x1b[1;33m";
}
static void sysopMenuBack(Session &s) { sysopMenu(s); }
static void sysopLogs(Session &s, String filter) {
  filter.trim(); filter.toUpperCase();
  std::vector<String> rows;
  File f = sdReady ? SD_MMC.open(EVENT_LOG, FILE_READ) : File();
  while (f && f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (!line.length()) continue;
    String when = field(line, 0), tag = field(line, 1), detail = dec(field(line, 2));
    if (filter.length() && up(when + " " + tag + " " + detail).indexOf(filter) < 0) continue;
    char head[48];
    snprintf(head, sizeof(head), "%-16.16s %-11.11s ", when.c_str(), tag.c_str());
    rows.push_back("\x1b[0;36m" + String(head).substring(0, 17) + logColour(tag) + String(head).substring(17) + "\x1b[0m" + detail.substring(0, 50));
    if (rows.size() > 1000) rows.erase(rows.begin());
  }
  if (f) f.close();
  if (rows.empty()) { say(s, filter.length() ? "No log lines match." : "The log is empty."); prompt(s, "SysOp command? "); return; }
  activity(s, "SYSOP LOGS");
  openRows(s, rows, filter.length() ? "Log: " + filter.substring(0, 16) : String("Event log"),
           "SPACE page  B back  \x18\x19 line  G/< top  Q console", sysopMenuBack, nullptr, true);
}
static void sysopDoors(Session &s) {
  s.state = SYSOP_DOORS;
  activity(s, "SYSOP DOOR SETTINGS");
  say(s, "\x1b[1;35mLEGEND OF KOUVOLA // DOOR SETTINGS\x1b[0m");
  say(s, "[F handle count] Add forest fights (1-255)   [V handle] View fights");
  prompt(s, "[Q] Return. Door settings command? ");
}
