#pragma once
// User records in /BBS/DATA/USERS/USERS.DAT.
// Part of the single TinyBBS translation unit: included in order from main.cpp.

struct User {
  uint32_t id = 0, calls = 0, minutes = 0, messages = 0;
  // These are deliberately per-user policy fields, rather than a second,
  // browser-only account model.  They are also safe for old 0.1 records:
  // absent trailing fields simply mean unlocked/unlimited/no ratio policy.
  uint32_t uploadBytes = 0, downloadBytes = 0, credits = 0, downloadQuota = 0;
  uint16_t uploads = 0, downloads = 0, ratio = 1;
  uint8_t level = 10;
  bool locked = false, ratioEnforced = false;
  String handle, normalized, realName, location, salt, hash, flags;
};
static bool parseUser(const String &line, User &u) {
  if (!line.length()) return false;
  u = User();
  u.id = field(line, 0).toInt(); u.handle = dec(field(line, 1)); u.normalized = field(line, 2);
  u.realName = dec(field(line, 3)); u.location = dec(field(line, 4)); u.salt = field(line, 5); u.hash = field(line, 6);
  u.level = field(line, 7).toInt(); u.calls = field(line, 8).toInt(); u.minutes = field(line, 9).toInt(); u.messages = field(line, 10).toInt(); u.flags = field(line, 11);
  u.locked = field(line, 12).toInt() != 0; u.ratioEnforced = field(line, 13).toInt() != 0;
  u.uploadBytes = strtoul(field(line, 14).c_str(), nullptr, 10);
  u.downloadBytes = strtoul(field(line, 15).c_str(), nullptr, 10);
  u.credits = strtoul(field(line, 16).c_str(), nullptr, 10);
  u.downloadQuota = strtoul(field(line, 17).c_str(), nullptr, 10);
  u.uploads = field(line, 18).toInt(); u.downloads = field(line, 19).toInt();
  u.ratio = constrain(field(line, 20).toInt(), 1L, 999L);
  return u.id > 0;
}
static String userLine(const User &u) {
  return String(u.id) + "|" + enc(u.handle) + "|" + u.normalized + "|" + enc(u.realName) + "|" + enc(u.location) + "|" + u.salt + "|" + u.hash + "|" + u.level + "|" + u.calls + "|" + u.minutes + "|" + u.messages + "|" + u.flags + "|" + (u.locked ? "1" : "0") + "|" + (u.ratioEnforced ? "1" : "0") + "|" + u.uploadBytes + "|" + u.downloadBytes + "|" + u.credits + "|" + u.downloadQuota + "|" + u.uploads + "|" + u.downloads + "|" + u.ratio;
}
static void userCounters() {
  if (!sdReady) return; File f = SD_MMC.open("/BBS/DATA/USERS/USERS.DAT", FILE_READ); if (!f) return;
  uint32_t high = 0; while (f.available()) { User u; if (parseUser(f.readStringUntil('\n'), u)) high = max(high, u.id); } f.close(); nextUserId = high + 1;
}
static bool findUser(const String &normalized, User &found) {
  if (!sdReady) return false; File f = SD_MMC.open("/BBS/DATA/USERS/USERS.DAT", FILE_READ); if (!f) return false;
  bool ok = false; while (f.available()) { User u; if (parseUser(f.readStringUntil('\n'), u) && u.normalized == normalized) { found = u; ok = true; break; } } f.close(); return ok;
}
static bool findUserById(uint32_t id, User &found) {
  if (!sdReady || !id) return false;
  File f = SD_MMC.open("/BBS/DATA/USERS/USERS.DAT", FILE_READ); if (!f) return false;
  bool ok = false;
  while (f.available()) {
    User u;
    if (parseUser(f.readStringUntil('\n'), u) && u.id == id) { found = u; ok = true; break; }
  }
  f.close();
  return ok;
}
static bool anySysop() { User u; return findUser("SYSOP", u); }
static bool appendUser(User &u) {
  if (!sdReady) { trace("USER_APPEND_FAIL", "SD unavailable: " + storageFault); return false; }
  u.id = nextUserId;
  File f = SD_MMC.open("/BBS/DATA/USERS/USERS.DAT", FILE_APPEND);
  if (!f) { trace("USER_APPEND_FAIL", "cannot open USERS.DAT"); return false; }
  bool ok = f.println(userLine(u)); f.close();
  if (ok) { nextUserId++; trace("USER_APPEND_OK", u.handle + " #" + u.id); }
  else trace("USER_APPEND_FAIL", "write error for " + u.handle);
  return ok;
}
static bool touchUser(User &changed) { // journal-style rewrite: .NEW first, then replace
  if (!sdReady) { trace("USER_TOUCH_FAIL", "SD unavailable"); return false; }
  File in = SD_MMC.open("/BBS/DATA/USERS/USERS.DAT", FILE_READ);
  if (!in) { trace("USER_TOUCH_FAIL", "cannot read USERS.DAT"); return false; }
  SD_MMC.remove("/BBS/DATA/USERS/USERS.NEW");
  File out = SD_MMC.open("/BBS/DATA/USERS/USERS.NEW", FILE_WRITE);
  if (!out) { in.close(); trace("USER_TOUCH_FAIL", "cannot create USERS.NEW"); return false; }
  bool wrote = true;
  while (in.available()) {
    String line = in.readStringUntil('\n'); User u;
    wrote = (parseUser(line, u) && u.id == changed.id) ? out.println(userLine(changed)) : out.println(line);
    if (!wrote) break;
  }
  in.close(); out.close();
  if (!wrote) { SD_MMC.remove("/BBS/DATA/USERS/USERS.NEW"); trace("USER_TOUCH_FAIL", "rewrite write error"); return false; }
  SD_MMC.remove("/BBS/DATA/USERS/USERS.BAK");
  if (!SD_MMC.rename("/BBS/DATA/USERS/USERS.DAT", "/BBS/DATA/USERS/USERS.BAK")) { SD_MMC.remove("/BBS/DATA/USERS/USERS.NEW"); trace("USER_TOUCH_FAIL", "cannot make backup"); return false; }
  if (!SD_MMC.rename("/BBS/DATA/USERS/USERS.NEW", "/BBS/DATA/USERS/USERS.DAT")) {
    SD_MMC.rename("/BBS/DATA/USERS/USERS.BAK", "/BBS/DATA/USERS/USERS.DAT");
    trace("USER_TOUCH_FAIL", "cannot activate rewritten users"); return false;
  }
  SD_MMC.remove("/BBS/DATA/USERS/USERS.BAK"); trace("USER_TOUCH_OK", changed.handle); return true;
}
static bool removeUser(const User &removed) {
  if (!sdReady || !removed.id) return false;
  File in = SD_MMC.open("/BBS/DATA/USERS/USERS.DAT", FILE_READ);
  if (!in) { trace("USER_DELETE_FAIL", "cannot read USERS.DAT"); return false; }
  SD_MMC.remove("/BBS/DATA/USERS/USERS.NEW");
  File out = SD_MMC.open("/BBS/DATA/USERS/USERS.NEW", FILE_WRITE);
  if (!out) { in.close(); trace("USER_DELETE_FAIL", "cannot create USERS.NEW"); return false; }
  bool found = false, wrote = true;
  while (in.available()) {
    String line = in.readStringUntil('\n'); User candidate;
    if (parseUser(line, candidate) && candidate.id == removed.id) { found = true; continue; }
    if (!out.println(line)) { wrote = false; break; }
  }
  in.close(); out.close();
  if (!found || !wrote) { SD_MMC.remove("/BBS/DATA/USERS/USERS.NEW"); trace("USER_DELETE_FAIL", found ? "rewrite write error" : "user vanished"); return false; }
  SD_MMC.remove("/BBS/DATA/USERS/USERS.BAK");
  if (!SD_MMC.rename("/BBS/DATA/USERS/USERS.DAT", "/BBS/DATA/USERS/USERS.BAK")) { SD_MMC.remove("/BBS/DATA/USERS/USERS.NEW"); trace("USER_DELETE_FAIL", "cannot make backup"); return false; }
  if (!SD_MMC.rename("/BBS/DATA/USERS/USERS.NEW", "/BBS/DATA/USERS/USERS.DAT")) { SD_MMC.rename("/BBS/DATA/USERS/USERS.BAK", "/BBS/DATA/USERS/USERS.DAT"); trace("USER_DELETE_FAIL", "cannot activate rewrite"); return false; }
  SD_MMC.remove("/BBS/DATA/USERS/USERS.BAK"); trace("USER_DELETE_OK", removed.handle); return true;
}
