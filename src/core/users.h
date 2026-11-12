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
static bool parseUser(const String &raw, User &u) {
  // A stray CR (a record once ended in CRLF) would land inside a field and
  // send the cursor back to column 0 when the field is shown.
  String line = raw; line.replace("\r", "");
  if (!line.length()) return false;
  u = User();
  u.id = field(line, 0).toInt(); u.handle = dec(field(line, 1)); u.normalized = field(line, 2);
  u.realName = dec(field(line, 3)); u.location = dec(field(line, 4)); u.salt = field(line, 5); u.hash = field(line, 6);
  u.level = field(line, 7).toInt(); u.calls = field(line, 8).toInt(); u.minutes = field(line, 9).toInt(); u.messages = field(line, 10).toInt(); u.flags = cleanFlags(field(line, 11));
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
// Every account, oldest first. fn returns false to stop.
template <typename F> static void eachUser(F fn) {
  File f = sdReady ? SD_MMC.open(USERS_FILE, FILE_READ) : File();
  while (f && f.available()) {
    User u;
    if (parseUser(f.readStringUntil('\n'), u) && !fn(u)) break;
  }
  if (f) f.close();
}
static void userCounters() {
  uint32_t high = 0;
  eachUser([&](const User &u) { high = max(high, u.id); return true; });
  nextUserId = high + 1;
}
static bool findUser(const String &normalized, User &found) {
  bool ok = false;
  eachUser([&](const User &u) { if (u.normalized != normalized) return true; found = u; ok = true; return false; });
  return ok;
}
static bool findUserById(uint32_t id, User &found) {
  bool ok = false;
  if (id) eachUser([&](const User &u) { if (u.id != id) return true; found = u; ok = true; return false; });
  return ok;
}
static bool anySysop() { User u; return findUser("SYSOP", u); }
static bool appendUser(User &u) {
  if (!sdReady) { trace("USER_APPEND_FAIL", "SD unavailable: " + storageFault); return false; }
  u.id = nextUserId;
  File f = SD_MMC.open(USERS_FILE, FILE_APPEND);
  if (!f) { trace("USER_APPEND_FAIL", "cannot open USERS.DAT"); return false; }
  bool ok = f.println(userLine(u)); f.close();
  if (ok) { nextUserId++; trace("USER_APPEND_OK", u.handle + " #" + u.id); }
  else trace("USER_APPEND_FAIL", "write error for " + u.handle);
  return ok;
}
static bool touchUser(User &changed) {
  bool ok = putRecord(USERS_FILE, changed.id, userLine(changed), false);
  trace(ok ? "USER_TOUCH_OK" : "USER_TOUCH_FAIL", changed.handle);
  return ok;
}
static bool removeUser(const User &removed) {
  bool ok = putRecord(USERS_FILE, removed.id, "", false);
  trace(ok ? "USER_DELETE_OK" : "USER_DELETE_FAIL", removed.handle);
  return ok;
}
