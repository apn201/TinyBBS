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
static uint16_t showUserList(Session &s, bool sysopView) {
  const uint8_t PAGE = 10; uint16_t visible = 0, shown = 0, start = s.userPage * PAGE;
  say(s, sysopView ? "\x1b[1;35m#     Handle           Access Lock Calls Location\x1b[0m" : "\x1b[1;36m#     Handle           Calls  Location\x1b[0m");
  File f = sdReady ? SD_MMC.open("/BBS/DATA/USERS/USERS.DAT", FILE_READ) : File();
  while (f && f.available()) {
    User candidate; if (!parseUser(f.readStringUntil('\n'), candidate) || (!sysopView && candidate.locked)) continue;
    if (visible++ < start) continue;
    if (shown++ >= PAGE) continue;
    char row[116];
    if (sysopView) snprintf(row, sizeof(row), "%-5lu %-16s %-6u %-4s %-5lu %s", (unsigned long)candidate.id, candidate.handle.c_str(), candidate.level, candidate.locked ? "YES" : "NO", (unsigned long)candidate.calls, candidate.location.c_str());
    else snprintf(row, sizeof(row), "%-5lu %-16s %-6lu %s", (unsigned long)candidate.id, candidate.handle.c_str(), (unsigned long)candidate.calls, candidate.location.c_str());
    say(s, row);
  }
  if (f) f.close();
  if (!shown) say(s, start ? "No more callers." : "No callers are listed.");
  uint16_t pages = visible ? (visible + PAGE - 1) / PAGE : 1;
  say(s, "\x1b[1;33mPage " + String(s.userPage + 1) + "/" + String(pages) + "  " + String(visible) + " caller" + (visible == 1 ? "" : "s") + "\x1b[0m");
  return pages;
}
static uint16_t userListPages(bool sysopView) {
  const uint8_t PAGE = 10; uint16_t visible = 0;
  File f = sdReady ? SD_MMC.open("/BBS/DATA/USERS/USERS.DAT", FILE_READ) : File();
  while (f && f.available()) { User candidate; if (parseUser(f.readStringUntil('\n'), candidate) && (sysopView || !candidate.locked)) visible++; }
  if (f) f.close(); return visible ? (visible + PAGE - 1) / PAGE : 1;
}
static void userList(Session &s) {
  s.state = USER_LIST; activity(s, "USER LIST"); uint16_t pages = showUserList(s, false);
  if (isSysop(s)) say(s, "[Z] User editor: levels, flags, limits and passwords");
  prompt(s, pages > 1 ? "ENTER next, P previous, Q board: " : "Q returns to board: ");
}
static void sysopMenu(Session &s) {
  s.state = SYSOP_MENU; activity(s, "SYSOP CONSOLE");
  art(s, "/BBS/ANSI/MENUS/SYSOP.ANS", "KOUVOSTO // SYSOP");
  say(s, "[U] User editor                    [D] Door settings / forest fights");
  say(s, "[W] Node monitor   [S] Board statistics   [R] Reload settings   [Q] Main Board");
  size_t waiting = uploadsWaiting();
  say(s, "[I] Incoming uploads" + (waiting ? " \x1b[1;35m(" + String(waiting) + " waiting)\x1b[0m" : String(" (none waiting)")));
  if (s.web) say(s, "[F] File manager (opens a separate admin page)");
  prompt(s, "SysOp command? ");
}
static void sysopDoors(Session &s) {
  s.state = SYSOP_DOORS;
  activity(s, "SYSOP DOOR SETTINGS");
  say(s, "\x1b[1;35mLEGEND OF KOUVOLA // DOOR SETTINGS\x1b[0m");
  say(s, "[F handle count] Add forest fights (1-255)   [V handle] View fights");
  prompt(s, "[Q] Return. Door settings command? ");
}
