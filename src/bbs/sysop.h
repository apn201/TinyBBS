#pragma once
// WHO, user list, SysOp menus and command-line parsing helpers.
// Part of the single TinyBBS translation unit: included in order from main.cpp.

static void who(Session &s) {
  say(s, "\x1b[1;36mNode  User             Activity\x1b[0m");
  for (auto &n : sessions) if (n.used && n.state != DEAD) { String h = n.user.handle.length() ? n.user.handle : "(logging in)"; char row[90]; snprintf(row, sizeof(row), "%-5u %-16s %s", n.node, h.c_str(), n.activity.c_str()); say(s, row); }
}
static bool isSysop(const Session &s) { return s.authenticated && !s.user.locked && s.user.level == 255 && hasFlag(s.user, "SYSOP"); }
static String tokenAt(String text, uint8_t wanted) {
  text.trim();
  while (wanted--) { int cut = text.indexOf(' '); if (cut < 0) return ""; text = text.substring(cut + 1); text.trim(); }
  int cut = text.indexOf(' '); return cut < 0 ? text : text.substring(0, cut);
}
static String afterTokens(String text, uint8_t count) {
  text.trim();
  while (count--) { int cut = text.indexOf(' '); if (cut < 0) return ""; text = text.substring(cut + 1); text.trim(); }
  return text;
}
static String cleanFlags(const String &in) {
  String out; bool comma = true;
  for (uint16_t i = 0; i < in.length() && out.length() < 63; ++i) {
    char c = in[i];
    if (isalnum((unsigned char)c) || c == '_') { out += (char)toupper((unsigned char)c); comma = false; }
    else if (c == ',' && !comma) { out += ','; comma = true; }
  }
  if (out.endsWith(",")) out.remove(out.length() - 1); return out;
}
static uint32_t numberAt(const String &line, uint8_t position) { return strtoul(tokenAt(line, position).c_str(), nullptr, 10); }
static bool readNumber(const String &value, uint32_t &out) {
  if (!value.length()) return false;
  out = 0;
  for (size_t i = 0; i < value.length(); ++i) {
    char digit = value[i];
    if (digit < '0' || digit > '9' || out > (UINT32_MAX - (digit - '0')) / 10) return false;
    out = out * 10 + (digit - '0');
  }
  return true;
}
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
  if (isSysop(s)) say(s, "[Z] Manage users, quotas and permissions");
  prompt(s, pages > 1 ? "ENTER next, P previous, Q board: " : "Q returns to board: ");
}
static void showUserRecord(Session &s, const User &u) {
  String quota = "unlimited"; if (u.downloadQuota) quota = String(u.downloadQuota) + " bytes";
  say(s, "\x1b[1;35m+------------------------- USER RECORD -------------------------+\x1b[0m");
  say(s, "#" + String(u.id) + "  " + u.handle + "  Access " + String(u.level) + (u.locked ? "  \x1b[1;31mLOCKED\x1b[0m" : "  \x1b[1;32mACTIVE\x1b[0m"));
  say(s, "Real: " + u.realName + "  From: " + u.location);
  say(s, "Calls: " + String(u.calls) + "  Messages: " + String(u.messages) + "  Flags: " + (u.flags.length() ? u.flags : "(none)"));
  say(s, "Uploads: " + String(u.uploads) + " / " + String(u.uploadBytes) + " bytes   Downloads: " + String(u.downloads) + " / " + String(u.downloadBytes) + " bytes");
  String ratio = u.ratioEnforced ? "1:" : "OFF";
  if (u.ratioEnforced) ratio += u.ratio;
  say(s, "Upload:download " + ratio + "  Credits: " + String(u.credits) + " bytes");
  say(s, "Lifetime download quota: " + quota);
}
static void sysopUsers(Session &s) {
  s.state = SYSOP_USERS; activity(s, "SYSOP USER EDITOR");
  say(s, "\x1b[1;35m\r\n+===================== KOUVOSTO USER EDITOR =====================+\x1b[0m");
  say(s, "[E handle] Edit quota, ratio, credits, level, flags and lock status.");
  say(s, "\x1b[1;36m[L]\x1b[0m List  \x1b[1;36m[V handle]\x1b[0m View  \x1b[1;36m[ADD handle level]\x1b[0m Add");
  say(s, "\x1b[1;33m[LEVEL handle 0-255]\x1b[0m  \x1b[1;33m[LOCK|UNLOCK handle]\x1b[0m  \x1b[1;33m[FLAGS handle list]\x1b[0m");
  say(s, "\x1b[1;32m[QUOTA handle bytes]  [CREDIT handle bytes]  [RATIO handle n|OFF]\x1b[0m");
  say(s, "\x1b[1;31m[DEL handle]\x1b[0m  \x1b[1;37m[Q] SysOp menu\x1b[0m"); prompt(s, "User editor command? ");
}
static void sysopMenu(Session &s) {
  s.state = SYSOP_MENU; activity(s, "SYSOP CONSOLE");
  art(s, "/BBS/ANSI/MENUS/SYSOP.ANS", "KOUVOSTO // SYSOP");
  say(s, "[U] Accounts / quotas / access     [D] Door settings / forest fights");
  say(s, "[W] Node monitor   [S] Board statistics   [Q] Main Board");
  prompt(s, "SysOp command? ");
}
static void sysopDoors(Session &s) {
  s.state = SYSOP_DOORS;
  activity(s, "SYSOP DOOR SETTINGS");
  say(s, "\x1b[1;35mLEGEND OF KOUVOLA // DOOR SETTINGS\x1b[0m");
  say(s, "[F handle count] Add forest fights (1-255)   [V handle] View fights");
  prompt(s, "[Q] Return. Door settings command? ");
}
