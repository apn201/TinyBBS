/*
  TinyBBS 0.1 — transport-independent offline BBS core.
  Hardware layout is retained verbatim from the known-good TinyIRC sketch.
*/
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <FS.h>
#include <SD_MMC.h>
#include <Arduino_GFX_Library.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <mbedtls/sha256.h>
#include <memory>
#include "scene_logo.h"
#include "web_terminal.h"
#include "file_viewer.h"

static const int LCD_SCLK = 10, LCD_MOSI = 11, LCD_CS = 12, LCD_DC = 13, LCD_RST = 14;
static const int SD_CLK = 17, SD_CMD = 18, SD_D0 = 16, UI_BOOT_PIN = 0;
// Eight active BBS nodes is the current, deliberately conservative target.
// Browser and Telnet callers share this one pool.
static const uint8_t MAX_NODES = 8;
static constexpr uint32_t LOGIN_TIMEOUT_MS = 120000;
static constexpr uint32_t WEB_CARRIER_TIMEOUT_MS = 180000;
static const char *ROOT = "/BBS";
static const char *MODEM_AUDIO = "/BBS/ASSETS/DIALUP.MP3";

Arduino_DataBus *lcdBus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCLK, LCD_MOSI, GFX_NOT_DEFINED);
Arduino_GFX *gfx = new Arduino_ST7735(lcdBus, LCD_RST, 0, true, 80, 160, 26, 1, 26, 1);
WebServer http(80);
DNSServer dns;
WiFiServer telnet(23);
static bool sdReady = false;
static uint32_t bootAt = 0, callers = 0, nextUserId = 1, nextMessageId = 1;
static String storageFault;

// ---------- small, intentionally boring helpers --------------------------------
static void trace(const char *tag, const String &detail = "") {
  Serial.printf("[TINYBBS %08lu] %s", (unsigned long)millis(), tag);
  if (detail.length()) Serial.printf(" :: %s", detail.c_str());
  Serial.println();
}
static String up(String s) { s.trim(); s.toUpperCase(); return s; }
static String nowText() {
  uint32_t n = millis() / 1000; char b[18];
  snprintf(b, sizeof(b), "UP %02lu:%02lu:%02lu", n / 3600, (n / 60) % 60, n % 60); return String(b);
}
static String clean(const String &in, uint16_t max, bool handle = false) {
  String out; out.reserve(max);
  for (uint16_t i = 0; i < in.length() && out.length() < max; ++i) {
    char c = in[i];
    if ((uint8_t)c < 32 || c == '|' || c == '%' || c == '\\' || c == '/') continue;
    if (handle && !(isalnum((unsigned char)c) || c == '_' || c == '-' || c == ' ')) continue;
    out += c;
  }
  out.trim(); return out;
}
static String enc(const String &in) { // stable journal delimiter escaping
  const char *hex = "0123456789ABCDEF"; String out;
  for (uint16_t i = 0; i < in.length(); ++i) { uint8_t c = in[i];
    if (c == '%' || c == '|' || c == '\r' || c == '\n') { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    else out += (char)c;
  } return out;
}
static String dec(const String &in) {
  String out; for (uint16_t i = 0; i < in.length(); ++i) {
    if (in[i] == '%' && i + 2 < in.length()) { char h[3] = {in[i + 1], in[i + 2], 0}; out += (char)strtoul(h, nullptr, 16); i += 2; }
    else out += in[i];
  } return out;
}
static String field(const String &line, uint8_t wanted) {
  uint8_t n = 0; int start = 0;
  for (int i = 0; i <= line.length(); ++i) if (i == line.length() || line[i] == '|') {
    if (n++ == wanted) return line.substring(start, i); start = i + 1;
  } return "";
}
static String sha256hex(const String &s) {
  uint8_t digest[32]; mbedtls_sha256_context c; mbedtls_sha256_init(&c);
  mbedtls_sha256_starts(&c, 0); mbedtls_sha256_update(&c, (const unsigned char*)s.c_str(), s.length());
  mbedtls_sha256_finish(&c, digest); mbedtls_sha256_free(&c);
  char out[65]; for (uint8_t i = 0; i < 32; ++i) sprintf(out + i * 2, "%02x", digest[i]); out[64] = 0; return String(out);
}
static String salt() { char b[17]; snprintf(b, sizeof(b), "%08lx%08lx", (unsigned long)esp_random(), (unsigned long)esp_random()); return String(b); }
static bool mkdirs() {
  // A freshly formatted card has no BBS tree at all.  TinyIRC only needed a
  // root-level log, but TinyBBS owns a directory hierarchy, so provision the
  // root first rather than treating a missing /BBS as a dead card.
  const char *paths[] = {"/BBS", "/BBS/DATA", "/BBS/DATA/USERS", "/BBS/DATA/MSGS", "/BBS/DATA/STATE", "/BBS/DATA/DOORS", "/BBS/LOGS", "/BBS/TEMP", "/BBS/TEMP/UPLOADS"};
  bool ok = true;
  for (auto p : paths) {
    if (!SD_MMC.exists(p) && !SD_MMC.mkdir(p)) { trace("SD_MKDIR_FAIL", p); ok = false; }
  }
  return ok;
}
static bool ensureFile(const char *path) {
  if (SD_MMC.exists(path)) return true;
  File f = SD_MMC.open(path, FILE_WRITE);
  if (!f) { storageFault = String("cannot create ") + path; trace("SD_FILE_FAIL", storageFault); return false; }
  f.close(); return true;
}
static bool prepareStorage() {
  if (!mkdirs()) return false;
  if (!ensureFile("/BBS/DATA/USERS/USERS.DAT") || !ensureFile("/BBS/DATA/MSGS/MESSAGES.DAT")) return false;
  // Keep the probe FAT 8.3-safe. A leading-dot long name made some SD stacks
  // report a harmless cleanup issue as a fatal storage failure.
  const char *probe = "/BBS/DATA/STATE/TBPROBE.TMP";
  File f = SD_MMC.open(probe, FILE_WRITE);
  if (!f) { storageFault = "cannot open write probe"; trace("SD_PROBE_FAIL", "open"); return false; }
  bool wrote = f.print("OK") == 2; f.close();
  bool removed = SD_MMC.remove(probe);
  if (!wrote) { storageFault = "write probe failed"; trace("SD_PROBE_FAIL", "write"); return false; }
  if (!removed) trace("SD_PROBE_WARN", "probe cleanup failed");
  trace("SD_READY", String("total=") + (unsigned long)(SD_MMC.totalBytes() / 1024 / 1024) + "MiB free=" + (unsigned long)(SD_MMC.totalBytes() - SD_MMC.usedBytes()) / 1024 / 1024 + "MiB");
  return true;
}
static String config(const char *file, const char *key, const String &fallback) {
  if (!sdReady) return fallback; File f = SD_MMC.open(file, FILE_READ); if (!f) return fallback;
  String needle = String(key) + "=";
  while (f.available()) { String line = f.readStringUntil('\n'); line.trim(); if (line.startsWith(needle)) { f.close(); return line.substring(needle.length()); } }
  f.close(); return fallback;
}
static void event(const String &tag, const String &detail) {
  if (!sdReady) return; File f = SD_MMC.open("/BBS/LOGS/EVENTS.LOG", FILE_APPEND);
  if (f) { f.println(nowText() + "|" + tag + "|" + enc(detail)); f.close(); }
}

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

struct Message { uint32_t id = 0; uint8_t conference = 0; String from, to, subject, body, when; };
static bool parseMessage(const String &line, Message &m) {
  m.id = field(line, 0).toInt(); m.conference = field(line, 1).toInt(); m.from = dec(field(line, 2)); m.to = dec(field(line, 3)); m.subject = dec(field(line, 4)); m.when = dec(field(line, 5)); m.body = dec(field(line, 6)); return m.id > 0;
}
static String messageLine(const Message &m) { return String(m.id) + "|" + m.conference + "|" + enc(m.from) + "|" + enc(m.to) + "|" + enc(m.subject) + "|" + enc(m.when) + "|" + enc(m.body); }
static void messageCounters() { if (!sdReady) return; File f = SD_MMC.open("/BBS/DATA/MSGS/MESSAGES.DAT", FILE_READ); if (!f) return; uint32_t high = 0; while (f.available()) { Message m; if (parseMessage(f.readStringUntil('\n'), m)) high = max(high, m.id); } f.close(); nextMessageId = high + 1; }
static bool appendMessage(Message &m) { if (!sdReady) return false; m.id = nextMessageId++; File f = SD_MMC.open("/BBS/DATA/MSGS/MESSAGES.DAT", FILE_APPEND); if (!f) return false; bool ok = f.println(messageLine(m)); f.close(); return ok; }

enum State : uint8_t { DEAD, LOGIN_CHOICE, LOGIN_HANDLE, LOGIN_PASSWORD, NEW_HANDLE, NEW_PASSWORD, NEW_REAL, NEW_LOCATION, BULLETIN, MAIN, USER_LIST, MSG_MENU, MSG_READ, POST_TO, POST_SUBJECT, POST_BODY, JOIN_CONF, CHAT, SYSOP_MENU, SYSOP_USERS, SYSOP_DOORS, SYSOP_ADD_PASSWORD, SYSOP_DELETE_CONFIRM, ANSI_GALLERY, FILE_MENU, FILE_LIST, FILE_VIEW, DOOR_MENU, QUEST_MENU, QUEST_TAVERN, QUEST_FIGHT, QUEST_RESULT, QUEST_BANK, QUEST_SHOP, DICE_BET, QUEST_DUEL_OFFER, QUEST_DUEL_WAIT, QUEST_DUEL, QUEST_DUEL_RESULT, MUD_MENU, GOODBYE };
struct Quest {
  uint32_t id = 0, gold = 35, bank = 0, exp = 0;
  uint16_t hp = 30, maxHp = 30, monsterHp = 0;
  uint8_t level = 1, fights = 12, monsterAtk = 0, weapon = 0, armor = 0;
  uint32_t kills = 0, pvpWins = 0, pvpLosses = 0, fame = 0;
  String monster;
};
struct MudCharacter {
  uint32_t id = 0, experience = 0, coins = 12;
  uint16_t hp = 24, maxHp = 24;
  uint8_t room = 0, level = 1, tutorial = 0;
};
struct Session {
  bool used = false, web = false, ansi = true, hideInput = false, paged = false, ignoreNextLf = false;
  uint8_t node = 0, conference = 0, fileArea = 0; uint16_t userPage = 0; State state = DEAD; WiFiClient socket;
  String token, input, output, pendingHandle, pendingPass, pendingReal, pendingTo, pendingSubject, activity;
  String editHandle;
  uint32_t editId = 0;
  uint8_t pendingLevel = 0;
  uint8_t duelNode = 0, duelTurnNode = 0;
  uint16_t fileOffset = 0;
  bool moreFiles = false;
  bool galleryCabinet = false;
  struct {
    uint16_t number = 0;
    uint32_t end = 0, offset = 0, next = 0, history[24] = {};
    uint8_t depth = 0;
    bool ansi = false, utf8 = false;
    String path, name;
  } viewer;
  User user; Quest quest; MudCharacter mud; uint16_t mudEnemyHp = 0; bool mudEnemyDefeated = false;
  uint32_t connectedAt = 0, lastAt = 0, lastRead = 0;
  uint32_t lastPoll = 0;
  bool authenticated = false;
  uint8_t telnetState = 0;
};
static Session sessions[MAX_NODES];
static Session *sessionByNode(uint8_t node) {
  if (!node || node > MAX_NODES) return nullptr;
  Session &candidate = sessions[node - 1];
  return candidate.used ? &candidate : nullptr;
}

static void write(Session &s, const String &text) { s.output += text; if (s.output.length() > 14000) s.output.remove(0, s.output.length() - 14000); }
static void say(Session &s, const String &text = "") { write(s, text + "\r\n"); }
static void prompt(Session &s, const String &text) { write(s, text); }
static void activity(Session &s, const String &what) { s.activity = what; }
static void art(Session &s, const char *path, const String &fallback = "") {
  // Read a screen as a unit. A clearing screen supersedes undelivered text;
  // the amount of output waiting for the browser must never crop a new screen.
  if (sdReady && s.ansi) {
    File f = SD_MMC.open(path, FILE_READ);
    if (f && !f.isDirectory() && f.size() <= 12000) {
      String screen;
      size_t bytes = f.size();
      if (bytes && screen.reserve(bytes + 1)) {
        std::unique_ptr<char[]> buffer(new (std::nothrow) char[bytes]);
        if (!buffer || f.read(reinterpret_cast<uint8_t *>(buffer.get()), bytes) != bytes) {
          say(s, fallback);
          return;
        }
        screen.concat(buffer.get(), bytes);
        screen.replace("\r\n", "\n");
        screen.replace("\n", "\r\n");
        if (!screen.endsWith("\r\n")) screen += "\r\n";
        if (s.web && screen.startsWith("\x1b[2J")) s.output = "";
        if (s.output.length() + screen.length() <= 14000) {
          write(s, screen);
          return;
        }
      }
    }
  }
  say(s, fallback);
}
static void savePointer(Session &s) { if (!sdReady || !s.user.id) return; File f = SD_MMC.open((String("/BBS/DATA/STATE/PTR_") + s.user.id + ".DAT").c_str(), FILE_WRITE); if (f) { f.printf("%lu|%u\n", (unsigned long)s.lastRead, s.conference); f.close(); } }
static void loadPointer(Session &s) { if (!sdReady || !s.user.id) return; File f = SD_MMC.open((String("/BBS/DATA/STATE/PTR_") + s.user.id + ".DAT").c_str(), FILE_READ); if (f) { String x = f.readStringUntil('\n'); s.lastRead = field(x, 0).toInt(); f.close(); } }
static String confName(uint8_t n) { return n == 0 ? "Main Board" : n == 1 ? "General Discussion" : n == 2 ? "The Back Room" : n == 3 ? "SysOp Console" : "Unknown"; }
static bool hasFlag(const User &u, const String &flag) { return ("," + up(u.flags) + ",").indexOf("," + up(flag) + ",") >= 0; }
static bool canJoinConference(const User &u, uint8_t conference) {
  if (conference < 2) return true;
  if (conference == 2) return u.level >= 50 && hasFlag(u, "BACKROOM");
  if (conference == 3) return u.level >= 255 && hasFlag(u, "SYSOP");
  return false;
}
struct FileArea { const char *name, *path, *flag; uint8_t level; };
static const FileArea FILE_AREAS[] = {
  {"Scene and Demo Files", "/BBS/FILES/005_DEMOS", "", 20},
  {"ANSI and ASCII Art", "/BBS/FILES/006_ANSI_ASCII", "", 20},
  {"Textfiles, Docs and Weirdness", "/BBS/FILES/010_TEXTFILES", "", 20},
  {"Public Image Gallery", "/BBS/FILES/013_IMAGES", "", 20},
  {"Programming", "/BBS/FILES/008_PROGRAMMING", "", 20},
  {"BBS Software", "/BBS/FILES/001_BBS_SOFTWARE", "", 20},
  {"The Back Room", "/BBS/FILES/013_THE_BACK_ROOM", "BACKROOM", 50},
  {"SysOp Vault", "/BBS/FILES/014_SYSOP_VAULT", "SYSOP", 255}
};
static const uint8_t FILE_AREA_COUNT = sizeof(FILE_AREAS) / sizeof(FILE_AREAS[0]);
static void fileMenu(Session &s);
static void doorMenu(Session &s);
static String tokenAt(String text, uint8_t wanted);
static String afterTokens(String text, uint8_t count);
static bool canUseArea(const Session &s, uint8_t area) { return area < FILE_AREA_COUNT && s.user.level >= FILE_AREAS[area].level && (!FILE_AREAS[area].flag[0] || hasFlag(s.user, FILE_AREAS[area].flag)); }
static String shortName(const String &path) { int slash = path.lastIndexOf('/'); return slash >= 0 ? path.substring(slash + 1) : path; }
static bool areaFile(Session &s, uint16_t wanted, String &path, String &name, size_t &bytes) {
  if (!sdReady || !canUseArea(s, s.fileArea) || !wanted) return false;
  File dir = SD_MMC.open(FILE_AREAS[s.fileArea].path); if (!dir || !dir.isDirectory()) return false;
  uint16_t n = 0; File entry;
  while ((entry = dir.openNextFile())) { bool take = !entry.isDirectory(); if (take) n++; if (take && n == wanted) { path = entry.path(); name = shortName(path); bytes = entry.size(); entry.close(); dir.close(); return true; } entry.close(); }
  dir.close(); return false;
}
static void listArea(Session &s) {
  if (!canUseArea(s, s.fileArea)) { say(s, "Access denied."); fileMenu(s); return; }
  const FileArea &a = FILE_AREAS[s.fileArea]; activity(s, String("FILES: ") + a.name);
  say(s, "\x1b[1;36m\r\n+---------------- " + String(a.name) + " ----------------+\x1b[0m");
  File dir = SD_MMC.open(a.path);
  uint16_t n = 0;
  constexpr uint16_t perPage = 16;
  s.moreFiles = false;
  if (dir && dir.isDirectory()) {
    File entry;
    while ((entry = dir.openNextFile())) {
      if (entry.isDirectory() || ++n <= s.fileOffset) continue;
      if (n - s.fileOffset > perPage) { s.moreFiles = true; break; }
      String name = shortName(entry.path());
      char row[100];
      snprintf(row, sizeof(row), "\x1b[1;33m[%02u]\x1b[0m %-42.42s %6lu K",
               n, name.c_str(), (unsigned long)((entry.size() + 1023) / 1024));
      say(s, row);
    }
  }
  if (!n) say(s, "This cabinet is empty tonight.");
  say(s, s.web ? "[V #] View   [D #] Download   [N/P] Page   [L] Re-list   [Q] Back"
               : "[V #] View   [N/P] Page   [L] Re-list   [Q] Back");
  prompt(s, "File command? ");
  s.state = FILE_LIST;
}
static void renderFilePage(Session &s) {
  auto &view = s.viewer;
  if (!canUseArea(s, s.fileArea)) { fileMenu(s); return; }
  File file = SD_MMC.open(view.path, FILE_READ);
  if (!file || file.size() < view.end || !file.seek(view.offset)) {
    say(s, "File no longer available.");
    listArea(s);
    return;
  }
  const bool ansi = s.ansi && view.ansi;
  const uint8_t width = ansi ? 80 : 78;
  const uint8_t pageRows = ansi ? 23 : 20;
  const uint32_t limit = min(view.end, view.offset + 8192);
  String page;
  if (!page.reserve(10240)) { say(s, "Not enough memory to open the viewer."); listArea(s); return; }
  if (s.ansi) page = "\x1b[2J\x1b[H\x1b[0m";
  if (!ansi) page += clean(view.name, 65) + "\r\n\r\n";
  uint8_t lines = 0, column = 0;
  bool eof = false;
  while (file.position() < limit && lines < pageRows && page.length() < 10100) {
    int byte = file.read();
    if (byte < 0 || byte == 0x1a) { eof = true; break; }
    if (byte == 27) { readArchiveEscape(file, min(view.end, limit + 32), page, ansi); continue; }
    if (byte == '\r' || byte == '\n') {
      if (byte == '\r' && file.position() < view.end && file.peek() == '\n') file.read();
      page += "\r\n";
      column = 0;
      ++lines;
      continue;
    }
    if (byte == '\t') {
      do { page += ' '; ++column; } while (column < width && column % 8);
      continue;
    }
    if (byte < 32 || byte == 127) continue;
    if (column >= width) {
      if (++lines >= pageRows) { file.seek(file.position() - 1); break; }
      page += "\r\n";
      column = 0;
    }
    ++column;
    if (byte < 128) page += char(byte);
    else if (!view.utf8) appendDosGlyph(page, byte);
    else {
      char glyph[4] = {char(byte)};
      uint8_t tail = utf8Tail(byte), got = 0;
      while (got < tail && file.position() < view.end && (file.peek() & 0xc0) == 0x80)
        glyph[++got] = char(file.read());
      if (tail && got == tail && !(uint8_t(glyph[0]) == 0xc2 && uint8_t(glyph[1]) < 0xa0))
        page.concat(glyph, got + 1);
      else page += '?';
    }
  }
  view.next = eof || file.position() >= view.end ? 0 : file.position();
  if (s.ansi) page += ansi ? "\x1b[0m\x1b[24;1H\x1b[K" : "\x1b[0m\r\n";
  else page += "\r\n";
  if (view.next) page += "[ENTER/N] Next  ";
  if (view.depth) page += "[P] Previous  ";
  page += "[R] Start  [Q] File list: ";
  if (s.web && s.ansi) s.output = "";
  write(s, page);
  s.state = FILE_VIEW;
  activity(s, "VIEWING FILE");
}
static void openFileViewer(Session &s, uint16_t number) {
  String path, name;
  size_t bytes;
  if (!areaFile(s, number, path, name, bytes)) { prompt(s, "No such file. File command? "); return; }
  String upper = up(name);
  const bool ansi = upper.endsWith(".ANS") || upper.endsWith(".ANSI");
  const bool plain = upper.endsWith(".ASC") || upper.endsWith(".NFO") || upper.endsWith(".DIZ") ||
                     upper.endsWith(".TXT") || upper.endsWith(".ION") || upper.endsWith(".SDI") ||
                     (s.fileArea == 2 && upper.indexOf('.') < 0);
  if ((!ansi && !plain) || !bytes || bytes > UINT32_MAX - 8300) {
    prompt(s, "Only ANSI and text files can be viewed. File command? ");
    return;
  }
  File file = SD_MMC.open(path, FILE_READ);
  if (!file) { prompt(s, "File unavailable. File command? "); return; }
  uint8_t sample[768];
  size_t got = file.read(sample, min(bytes, sizeof(sample)));
  if (memchr(sample, 0, got)) { prompt(s, "Binary file. File command? "); return; }
  auto &view = s.viewer;
  view.number = number;
  view.path = path;
  view.name = name;
  view.ansi = ansi;
  view.utf8 = sampleUtf8(sample, got);
  view.depth = 0;
  view.offset = 0;
  if (got >= 3 && sample[0] == 0xef && sample[1] == 0xbb && sample[2] == 0xbf)
    view.offset = 3;
  view.end = bytes;
  if (bytes >= 128 && file.seek(bytes - 128) && file.read(sample, 128) == 128 &&
      memcmp(sample, "SAUCE00", 7) == 0) {
    view.end -= 128;
    size_t comment = 5 + size_t(sample[104]) * 64;
    if (sample[104] && comment <= view.end && file.seek(view.end - comment) &&
        file.read(sample, 5) == 5 && memcmp(sample, "COMNT", 5) == 0) view.end -= comment;
  }
  file.close();
  renderFilePage(s);
}

// VolaMUD is a native C++ port of the GoMud default tutorial world. It keeps
// the source world's room topology, tutorial progression and command model,
// while using TinyBBS sessions and SD records instead of Go, goroutines or a
// web stack. The GoMud MIT notice is in THIRD_PARTY_NOTICES.md.
struct MudRoom {
  const char *name, *description;
  int8_t north, south, east, west;
};
static const MudRoom MUD_ROOMS[] = {
  {"Learning to Look", "You are at the Newbie School. Look around before heading east.", -1, -1, 1, -1},
  {"Learning about You", "Inspect your character. Status, conditions and useful bits are only a command away. A training door leads south.", -1, 2, -1, 0},
  {"Learning to Fight", "This is a room for basic fight training. A clockwork training opponent waits here. The exit north returns to the school and west leads to graduation.", 1, -1, -1, 3},
  {"Training Complete", "You completed basic training. The full VolaMUD world will grow out from this room.", -1, -1, -1, -1}
};
static constexpr uint8_t MUD_ROOM_COUNT = sizeof(MUD_ROOMS) / sizeof(MUD_ROOMS[0]);
static constexpr uint8_t MUD_TUTORIAL_LOOKED = 1, MUD_TUTORIAL_STATUS = 2;
static constexpr const char *MUD_DATA = "/BBS/DATA/DOORS/VOLAMUD.DAT";
static constexpr const char *MUD_NEW = "/BBS/DATA/DOORS/VOLAMUD.NEW";
static constexpr const char *MUD_BACKUP = "/BBS/DATA/DOORS/VOLAMUD.BAK";

static bool parseMud(const String &line, MudCharacter &character) {
  character = MudCharacter();
  character.id = strtoul(field(line, 0).c_str(), nullptr, 10);
  character.room = constrain(field(line, 1).toInt(), 0L, long(MUD_ROOM_COUNT - 1));
  character.level = constrain(field(line, 2).toInt(), 1L, 99L);
  character.hp = constrain(field(line, 3).toInt(), 1L, 65535L);
  character.maxHp = constrain(field(line, 4).toInt(), long(character.hp), 65535L);
  character.experience = strtoul(field(line, 5).c_str(), nullptr, 10);
  character.coins = strtoul(field(line, 6).c_str(), nullptr, 10);
  character.tutorial = constrain(field(line, 7).toInt(), 0L, 255L);
  return character.id > 0;
}
static String mudLine(const MudCharacter &character) {
  return String(character.id) + "|" + character.room + "|" + character.level + "|" +
         character.hp + "|" + character.maxHp + "|" + character.experience + "|" + character.coins + "|" + character.tutorial;
}
static bool recoverMud() {
  return SD_MMC.exists(MUD_DATA) || !SD_MMC.exists(MUD_BACKUP) || SD_MMC.rename(MUD_BACKUP, MUD_DATA);
}
static bool loadMud(uint32_t id, MudCharacter &character) {
  character = MudCharacter();
  character.id = id;
  if (!sdReady || !recoverMud()) return false;
  File file = SD_MMC.open(MUD_DATA, FILE_READ);
  if (!file) return !SD_MMC.exists(MUD_DATA);
  MudCharacter candidate;
  while (file.available()) {
    if (parseMud(file.readStringUntil('\n'), candidate) && candidate.id == id) {
      character = candidate;
      break;
    }
  }
  file.close();
  return true;
}
static bool saveMud(const MudCharacter &character) {
  if (!sdReady || !character.id || !recoverMud()) return false;
  const bool exists = SD_MMC.exists(MUD_DATA);
  File in = SD_MMC.open(MUD_DATA, FILE_READ);
  if (exists && !in) return false;
  if (SD_MMC.exists(MUD_NEW) && !SD_MMC.remove(MUD_NEW)) { in.close(); return false; }
  File out = SD_MMC.open(MUD_NEW, FILE_WRITE);
  if (!out) { in.close(); return false; }
  bool found = false, ok = true;
  while (in && in.available() && ok) {
    String line = in.readStringUntil('\n');
    if (line.endsWith("\r")) line.remove(line.length() - 1);
    MudCharacter current;
    if (parseMud(line, current) && current.id == character.id) {
      if (!found) ok = out.println(mudLine(character));
      found = true;
    } else ok = out.println(line);
  }
  if (!found && ok) ok = out.println(mudLine(character));
  in.close(); out.close();
  if (!ok) return false;
  if (SD_MMC.exists(MUD_BACKUP) && !SD_MMC.remove(MUD_BACKUP)) return false;
  if (exists && !SD_MMC.rename(MUD_DATA, MUD_BACKUP)) return false;
  if (!SD_MMC.rename(MUD_NEW, MUD_DATA)) {
    if (exists) recoverMud();
    return false;
  }
  if (exists) SD_MMC.remove(MUD_BACKUP);
  return true;
}
static bool playingMud(const Session &s) { return s.state == MUD_MENU; }
static bool loadMud(Session &s) {
  for (auto &other : sessions) {
    if (&other != &s && other.used && other.user.id == s.user.id && playingMud(other)) {
      say(s, "That VolaMUD character is already on another node.");
      return false;
    }
  }
  if (loadMud(s.user.id, s.mud)) return true;
  say(s, "VolaMUD storage is unavailable.");
  return false;
}
static String mudExits(const MudRoom &room) {
  String exits;
  if (room.north >= 0) exits += "N ";
  if (room.south >= 0) exits += "S ";
  if (room.east >= 0) exits += "E ";
  if (room.west >= 0) exits += "W ";
  return exits.length() ? exits : "none";
}
static void mudLook(Session &s) {
  const MudRoom &room = MUD_ROOMS[s.mud.room];
  say(s, "\x1b[1;36m\r\n[ " + String(room.name) + " ]\x1b[0m");
  say(s, room.description);
  say(s, "Exits: " + mudExits(room));
  uint8_t present = 0;
  String names;
  for (auto &other : sessions) if (&other != &s && other.used && other.authenticated && playingMud(other) && other.mud.room == s.mud.room) {
    if (names.length()) names += ", ";
    names += other.user.handle;
    ++present;
  }
  if (present) say(s, "Here: " + names);
  if (s.mud.room == 0 && !(s.mud.tutorial & MUD_TUTORIAL_LOOKED)) {
    s.mud.tutorial |= MUD_TUTORIAL_LOOKED;
    saveMud(s.mud);
  }
}
static void mudPrompt(Session &s) {
  activity(s, "VOLAMUD: " + String(MUD_ROOMS[s.mud.room].name));
  prompt(s, "[" + String(s.mud.hp) + "/" + s.mud.maxHp + " HP " + String(s.mud.coins) + "c] > ");
}
static void mudMenu(Session &s, bool entering = false) {
  if (entering) art(s, "/BBS/ANSI/DOORS/VOLAMUD.ANS", "VOLAMUD");
  s.state = MUD_MENU;
  mudLook(s);
  say(s, "Commands: LOOK, N/S/E/W, GO <direction>, SAY <text>, WHO, STATS, HELP, QUIT");
  mudPrompt(s);
}
static void mudBroadcastRoom(Session &s, const String &text) {
  for (auto &other : sessions) if (other.used && other.authenticated && playingMud(other) && other.mud.room == s.mud.room) {
    say(other, text);
    if (&other != &s) mudPrompt(other);
  }
}
static void mudMove(Session &s, const String &direction) {
  const MudRoom &room = MUD_ROOMS[s.mud.room];
  int8_t next = direction == "N" || direction == "NORTH" ? room.north :
                direction == "S" || direction == "SOUTH" ? room.south :
                direction == "E" || direction == "EAST" ? room.east :
                direction == "W" || direction == "WEST" ? room.west : -2;
  if (next == -2) { say(s, "Use N, S, E or W."); mudPrompt(s); return; }
  if (next < 0) { say(s, "There is no way that direction."); mudPrompt(s); return; }
  if (s.mud.room == 1 && next == 2 && !(s.mud.tutorial & MUD_TUTORIAL_STATUS)) {
    say(s, "The training door stays shut. Check your character with STATS first.");
    mudPrompt(s);
    return;
  }
  mudBroadcastRoom(s, s.user.handle + " leaves.");
  MudCharacter before = s.mud;
  s.mud.room = uint8_t(next);
  if (!saveMud(s.mud)) { s.mud = before; say(s, "The world could not save your move."); mudPrompt(s); return; }
  mudBroadcastRoom(s, s.user.handle + " arrives.");
  mudLook(s);
  mudPrompt(s);
}
static void mudFight(Session &s) {
  if (s.mud.room != 2) { say(s, "There is nothing here to fight."); mudPrompt(s); return; }
  if (s.mudEnemyDefeated) { say(s, "The training opponent is already in pieces. Move on when ready."); mudPrompt(s); return; }
  if (!s.mudEnemyHp) {
    s.mudEnemyHp = 18;
    say(s, "The clockwork training opponent turns toward you.");
  }
  const uint8_t hit = 3 + esp_random() % 7;
  s.mudEnemyHp = hit >= s.mudEnemyHp ? 0 : s.mudEnemyHp - hit;
  say(s, "You hit the training opponent for " + String(hit) + ".");
  if (!s.mudEnemyHp) {
    MudCharacter before = s.mud;
    s.mudEnemyDefeated = true;
    s.mud.experience += 10;
    s.mud.coins += 2;
    if (s.mud.experience >= uint32_t(s.mud.level) * 50u && s.mud.level < 99) {
      s.mud.level++;
      s.mud.maxHp = min<uint16_t>(200, s.mud.maxHp + 4);
      s.mud.hp = s.mud.maxHp;
      say(s, "You reach level " + String(s.mud.level) + ".");
    }
    if (!saveMud(s.mud)) { s.mud = before; s.mudEnemyDefeated = false; s.mudEnemyHp = 18; say(s, "The training result could not be saved."); }
    else say(s, "The opponent stops moving. XP +10, coins +2.");
    mudPrompt(s);
    return;
  }
  const uint8_t retaliation = 1 + esp_random() % 4;
  if (retaliation >= s.mud.hp) {
    s.mud.hp = s.mud.maxHp;
    s.mud.room = 0;
    s.mudEnemyHp = 0;
    say(s, "The training opponent wins. You wake at the Newbie School.");
  } else {
    s.mud.hp -= retaliation;
    say(s, "The opponent hits you for " + String(retaliation) + ".");
  }
  if (!saveMud(s.mud)) say(s, "Your condition could not be saved.");
  mudPrompt(s);
}
static void mudWho(Session &s) {
  say(s, "\x1b[1;36mNode  Traveller         Location\x1b[0m");
  uint8_t count = 0;
  for (auto &other : sessions) if (other.used && other.authenticated && playingMud(other)) {
    char row[100];
    snprintf(row, sizeof(row), "%-5u %-16.16s %s", other.node, other.user.handle.c_str(), MUD_ROOMS[other.mud.room].name);
    say(s, row);
    ++count;
  }
  if (!count) say(s, "No one is in VolaMUD.");
  mudPrompt(s);
}
static void enterMud(Session &s) {
  if (!loadMud(s)) { prompt(s, "Door command? "); return; }
  mudMenu(s, true);
  mudBroadcastRoom(s, s.user.handle + " arrives from the BBS.");
}
static void handleMudInput(Session &s, const String &line) {
  String cmd = tokenAt(up(line), 0);
  String args = clean(afterTokens(line, 1), 140);
  if (cmd == "Q" || cmd == "QUIT" || cmd == "EXIT") {
    mudBroadcastRoom(s, s.user.handle + " returns to the BBS.");
    doorMenu(s);
    return;
  }
  if (cmd == "L" || cmd == "LOOK") { mudLook(s); mudPrompt(s); return; }
  if (cmd == "N" || cmd == "S" || cmd == "E" || cmd == "W" || cmd == "NORTH" || cmd == "SOUTH" || cmd == "EAST" || cmd == "WEST") { mudMove(s, cmd); return; }
  if (cmd == "GO") { mudMove(s, tokenAt(up(args), 0)); return; }
  if (cmd == "SAY" || cmd == "'") {
    if (!args.length()) say(s, "Say what?");
    else mudBroadcastRoom(s, s.user.handle + " says, '" + args + "'");
    mudPrompt(s);
    return;
  }
  if (cmd == "WHO") { mudWho(s); return; }
  if (cmd == "FIGHT" || cmd == "ATTACK" || cmd == "KILL") { mudFight(s); return; }
  if (cmd == "STATS" || cmd == "STATUS") {
    if (s.mud.room == 1 && !(s.mud.tutorial & MUD_TUTORIAL_STATUS)) {
      s.mud.tutorial |= MUD_TUTORIAL_STATUS;
      if (!saveMud(s.mud)) say(s, "Tutorial progress could not be saved.");
    }
    say(s, "Level " + String(s.mud.level) + "  HP " + s.mud.hp + "/" + s.mud.maxHp + "  Coins " + s.mud.coins + "  XP " + s.mud.experience);
    mudPrompt(s);
    return;
  }
  if (cmd == "HELP" || !cmd.length()) {
    say(s, "LOOK, N/S/E/W, GO <direction>, SAY <text>, FIGHT, WHO, STATS and QUIT are available now.");
    mudPrompt(s);
    return;
  }
  say(s, "Unknown VolaMUD command. Type HELP.");
  mudPrompt(s);
}

// Kouvola's native fantasy door: original data and prose, but the durable
// town/forest rhythm callers expect from a 1990s BBS game.
static constexpr uint8_t MAX_GEAR = 3;
static bool parseQuest(const String &line, Quest &q) {
  q = Quest();
  q.id = strtoul(field(line, 0).c_str(), nullptr, 10);
  q.level = constrain(field(line, 1).toInt(), 1L, 12L);
  q.maxHp = constrain(field(line, 3).toInt(), 1L, 65535L);
  q.hp = constrain(field(line, 2).toInt(), 1L, long(q.maxHp));
  q.gold = strtoul(field(line, 4).c_str(), nullptr, 10);
  q.bank = strtoul(field(line, 5).c_str(), nullptr, 10);
  q.exp = strtoul(field(line, 6).c_str(), nullptr, 10);
  q.fights = constrain(field(line, 7).toInt(), 0L, 255L);
  q.weapon = constrain(field(line, 8).toInt(), 0L, long(MAX_GEAR));
  q.armor = constrain(field(line, 9).toInt(), 0L, long(MAX_GEAR));
  q.kills = strtoul(field(line, 10).c_str(), nullptr, 10);
  q.pvpWins = strtoul(field(line, 11).c_str(), nullptr, 10);
  q.pvpLosses = strtoul(field(line, 12).c_str(), nullptr, 10);
  q.fame = strtoul(field(line, 13).c_str(), nullptr, 10);
  return q.id > 0;
}
static String questLine(const Quest &q) {
  return String(q.id) + "|" + q.level + "|" + q.hp + "|" + q.maxHp + "|" +
         q.gold + "|" + q.bank + "|" + q.exp + "|" + q.fights + "|" + q.weapon + "|" + q.armor +
         "|" + q.kills + "|" + q.pvpWins + "|" + q.pvpLosses + "|" + q.fame;
}
static constexpr const char *QUEST_DATA = "/BBS/DATA/DOORS/KOUVQUEST.DAT";
static constexpr const char *QUEST_NEW = "/BBS/DATA/DOORS/KOUVQUEST.NEW";
static constexpr const char *QUEST_BACKUP = "/BBS/DATA/DOORS/KOUVQUEST.BAK";
static bool recoverQuest() {
  return SD_MMC.exists(QUEST_DATA) || !SD_MMC.exists(QUEST_BACKUP) ||
         SD_MMC.rename(QUEST_BACKUP, QUEST_DATA);
}
static bool loadQuest(uint32_t id, Quest &quest) {
  quest = Quest();
  quest.id = id;
  if (!sdReady || !recoverQuest()) return false;
  File file = SD_MMC.open(QUEST_DATA, FILE_READ);
  if (!file) return !SD_MMC.exists(QUEST_DATA);
  Quest candidate;
  while (file.available()) {
    if (parseQuest(file.readStringUntil('\n'), candidate) && candidate.id == id) {
      quest = candidate;
      break;
    }
  }
  return true;
}
static bool saveQuest(const Quest &quest) {
  if (!sdReady || !quest.id || !recoverQuest()) return false;
  const bool exists = SD_MMC.exists(QUEST_DATA);
  File in = SD_MMC.open(QUEST_DATA, FILE_READ);
  if (exists && !in) return false;
  if (SD_MMC.exists(QUEST_NEW) && !SD_MMC.remove(QUEST_NEW)) return false;
  File out = SD_MMC.open(QUEST_NEW, FILE_WRITE);
  if (!out) return false;
  bool found = false, ok = true;
  auto put = [&](String line) {
    if (line.endsWith("\r")) line.remove(line.length() - 1);
    return out.println(line) == line.length() + 2;
  };
  while (in && in.available() && ok) {
    String line = in.readStringUntil('\n');
    Quest current;
    if (parseQuest(line, current) && current.id == quest.id) {
      if (!found) ok = put(questLine(quest));
      found = true;
    } else ok = put(line);
  }
  if (!found && ok) ok = put(questLine(quest));
  in.close();
  out.close();
  if (!ok) return false;
  if (SD_MMC.exists(QUEST_BACKUP) && !SD_MMC.remove(QUEST_BACKUP)) return false;
  if (exists && !SD_MMC.rename(QUEST_DATA, QUEST_BACKUP)) return false;
  if (!SD_MMC.rename(QUEST_NEW, QUEST_DATA)) {
    if (exists) recoverQuest();
    return false;
  }
  if (exists) SD_MMC.remove(QUEST_BACKUP);
  return true;
}
static bool saveQuest(Session &s) {
  if (saveQuest(s.quest)) return true;
  say(s, "Door state could not be saved.");
  return false;
}
static bool playingQuest(const Session &s) {
  return s.state == QUEST_MENU || s.state == QUEST_TAVERN || s.state == QUEST_FIGHT || s.state == QUEST_RESULT ||
         s.state == QUEST_BANK || s.state == QUEST_SHOP || s.state == DICE_BET ||
         s.state == QUEST_DUEL_OFFER || s.state == QUEST_DUEL_WAIT ||
         s.state == QUEST_DUEL || s.state == QUEST_DUEL_RESULT;
}
struct LeaderRow {
  String handle;
  uint8_t level = 1;
  uint32_t score = 0, kills = 0, wins = 0, losses = 0;
};
static uint32_t questScore(const Quest &q) {
  uint64_t score = uint64_t(q.level) * 100000ULL + q.exp + uint64_t(q.kills) * 50ULL +
                   uint64_t(q.pvpWins) * 250ULL + uint64_t(q.fame) * 10ULL;
  return score > UINT32_MAX ? UINT32_MAX : uint32_t(score);
}
static void questLeaderboard(Session &s) {
  LeaderRow rows[10]; uint8_t count = 0;
  File f = sdReady ? SD_MMC.open(QUEST_DATA, FILE_READ) : File();
  while (f && f.available()) {
    Quest q; User u;
    if (!parseQuest(f.readStringUntil('\n'), q) || !findUserById(q.id, u)) continue;
    LeaderRow row; row.handle = u.handle; row.level = q.level; row.score = questScore(q);
    row.kills = q.kills; row.wins = q.pvpWins; row.losses = q.pvpLosses;
    uint8_t pos = 0; while (pos < count && rows[pos].score >= row.score) ++pos;
    if (pos >= 10) continue;
    if (count < 10) ++count;
    for (uint8_t i = count - 1; i > pos; --i) rows[i] = rows[i - 1];
    rows[pos] = row;
  }
  if (f) f.close();
  say(s, "\x1b[1;35m\r\n+================ KOUVOLA HALL OF FAME =================+\x1b[0m");
  say(s, "Rank  Adventurer       Lv  Score       Slays  PvP W/L");
  if (!count) say(s, "The stone is blank. Be the first name carved into it.");
  for (uint8_t i = 0; i < count; ++i) {
    char row[128];
    snprintf(row, sizeof(row), "%2u    %-16.16s %2u  %-10lu %5lu  %lu/%lu",
             i + 1, rows[i].handle.c_str(), rows[i].level,
             (unsigned long)rows[i].score, (unsigned long)rows[i].kills,
             (unsigned long)rows[i].wins, (unsigned long)rows[i].losses);
    say(s, row);
  }
  say(s, "Score: level, experience, forest slays, fame and arena victories.");
}
static void questChallenge(Session &s, const String &line);
static bool loadQuest(Session &s) {
  for (auto &other : sessions) {
    if (&other != &s && other.used && other.user.id == s.user.id && playingQuest(other)) {
      say(s, "That character is already playing on another node.");
      return false;
    }
  }
  if (loadQuest(s.user.id, s.quest)) return true;
  say(s, "Door storage is unavailable.");
  return false;
}
static void questMenu(Session &s, bool entering = false) {
  if (entering) art(s, "/BBS/ANSI/DOORS/KOUVOLA_LEGEND.ANS", "LEGEND OF KOUVOLA");
  s.state = QUEST_MENU;
  activity(s, "LEGEND OF KOUVOLA");
  say(s, String("\x1b[1;36m") + s.user.handle + "\x1b[0m  LV " + s.quest.level +
         "  HP " + s.quest.hp + "/" + s.quest.maxHp + "  GOLD " + s.quest.gold +
         "  BANK " + s.quest.bank);
  say(s, "Forest fights: " + String(s.quest.fights) + "  EXP: " + s.quest.exp +
          "  Slays: " + s.quest.kills + "  Fame: " + s.quest.fame);
  say(s, "[F]orest [H]ealer [B]ank [T]avern [M]aster [S]hop [L]egend board [C]hallenge [Q]uit");
  prompt(s, "Town command? ");
}
static void questTavern(Session &s, bool entering = false);
static void questMeal(Session &s) {
  const uint16_t cost = 5 + s.quest.level * 2;
  if (s.quest.gold < cost) say(s, "The tavern keeper wants " + String(cost) + " gold for a hot meal.");
  else {
    Quest updated = s.quest; updated.gold -= cost; updated.hp = updated.maxHp;
    if (saveQuest(updated)) { s.quest = updated; say(s, "The stew is hot, the room is loud, and your HP is restored."); }
    else say(s, "The tavern ledger could not be saved.");
  }
  questTavern(s);
}
static void questTavern(Session &s, bool entering) {
  if (entering) art(s, "/BBS/ANSI/DOORS/TAVERN.ANS", "THE COPPER WIRE TAVERN");
  s.state = QUEST_TAVERN; activity(s, "COPPER WIRE TAVERN");
  say(s, "\x1b[1;33mTHE COPPER WIRE TAVERN\x1b[0m  A smoky room beneath the town board.");
  say(s, "A brass bell rings whenever an adventurer enters the forest.");
  say(s, "[R]umor  [W]ho is in town  [L]egend board  [C]hallenge  [M]eal  [Q]Town");
  prompt(s, "Tavern command? ");
}
static void tavernRumor(Session &s) {
  static const char *rumors[] = {
    "The ash wolf hates bright steel, or so the old callers swear.",
    "A fighter with a full purse is worth watching in the arena.",
    "The barmaid says fame outlives gold when the board is still online.",
    "Someone has been asking whether the SysOp keeps extra forest turns.",
    "The back table is covered in maps of a forest that is not on any chart."
  };
  const char *rumor = rumors[esp_random() % (sizeof(rumors) / sizeof(rumors[0]))];
  say(s, String("\x1b[1;35mRumor from the back table:\x1b[0m ") + rumor);
  prompt(s, "Tavern command? ");
}
static bool duelLocation(const Session &s);
static void tavernWho(Session &s) {
  say(s, "\x1b[1;36mNode  Adventurer       Level  Activity\x1b[0m");
  uint8_t count = 0;
  for (auto &other : sessions) if (other.used && other.authenticated && duelLocation(other)) {
    char row[100]; snprintf(row, sizeof(row), "%-5u %-16.16s %-6u %s", other.node,
      other.user.handle.c_str(), other.quest.level, other.activity.c_str()); say(s, row); count++;
  }
  if (!count) say(s, "The tavern is empty but for the radio hum.");
  prompt(s, "Tavern command? ");
}
static bool duelLocation(const Session &s) {
  return s.state == QUEST_MENU || s.state == QUEST_TAVERN || s.state == QUEST_RESULT || s.state == QUEST_BANK || s.state == QUEST_SHOP;
}
static void duelCancel(Session &s, const String &reason) {
  Session *other = sessionByNode(s.duelNode);
  if (other && other != &s) {
    other->duelNode = other->duelTurnNode = 0;
    say(*other, "\r\n\x1b[1;31mThe arena closes: " + reason + "\x1b[0m");
    questMenu(*other);
  }
  s.duelNode = s.duelTurnNode = 0;
  say(s, "\r\n\x1b[1;31mThe arena closes: " + reason + "\x1b[0m");
  questMenu(s);
}
static void finishDuel(Session &winner, Session &loser) {
  const Quest winnerBefore = winner.quest, loserBefore = loser.quest;
  const uint32_t loot = std::min<uint32_t>(loser.quest.gold / 4, UINT32_MAX - winner.quest.gold);
  winner.quest.gold += loot;
  loser.quest.gold -= loot;
  winner.quest.exp = std::min<uint32_t>(UINT32_MAX - winner.quest.exp, winner.quest.exp + 25u + loser.quest.level * 10u);
  if (winner.quest.pvpWins != UINT32_MAX) winner.quest.pvpWins++;
  if (winner.quest.fame != UINT32_MAX) winner.quest.fame += 5;
  if (loser.quest.pvpLosses != UINT32_MAX) loser.quest.pvpLosses++;
  loser.quest.hp = loser.quest.maxHp / 2; if (!loser.quest.hp) loser.quest.hp = 1;
  if (!saveQuest(winner.quest) || !saveQuest(loser.quest)) {
    winner.quest = winnerBefore; loser.quest = loserBefore;
    saveQuest(winner.quest); saveQuest(loser.quest);
    duelCancel(winner, "the result could not be saved");
    return;
  }
  say(winner, "\r\n\x1b[1;32mYou win the arena duel!\x1b[0m  " + loser.user.handle +
      " drops " + loot + " gold.  Fame +5.");
  say(loser, String("\r\n\x1b[1;31mYou lose the arena duel.\x1b[0m  You wake with half HP and lose ") + loot + " gold.");
  winner.duelNode = winner.duelTurnNode = loser.duelNode = loser.duelTurnNode = 0;
  winner.state = loser.state = QUEST_DUEL_RESULT;
  winner.activity = "ARENA VICTORY"; loser.activity = "ARENA DEFEAT";
  prompt(winner, "[ENTER] Return to town: ");
  prompt(loser, "[ENTER] Return to town: ");
}
static void duelAttack(Session &s) {
  Session *other = sessionByNode(s.duelNode);
  if (!other || other->state != QUEST_DUEL) { duelCancel(s, "your opponent left"); return; }
  if (s.node != s.duelTurnNode) { prompt(s, "The other fighter has the initiative. Wait... "); return; }
  const uint16_t strength = 8 + s.quest.level * 3 + s.quest.weapon * 3;
  const uint16_t defence = 2 + other->quest.level * 2 + other->quest.armor * 3;
  const uint16_t half = strength / 2 ? strength / 2 : 1;
  const int swing = int(strength / 2) + int(esp_random() % half) - int(defence);
  if (swing <= 0) {
    say(s, "Your blade glances off " + other->user.handle + "'s guard.");
  } else {
    const uint16_t damage = min<uint16_t>(other->quest.hp, swing);
    other->quest.hp -= damage;
    say(s, "\x1b[1;33mYou strike " + other->user.handle + " for " + damage + "!\x1b[0m");
    say(*other, "\x1b[1;31m" + s.user.handle + " strikes you for " + damage + "!\x1b[0m");
    if (!other->quest.hp) { finishDuel(s, *other); return; }
  }
  s.duelTurnNode = other->node;
  other->duelTurnNode = s.duelTurnNode;
  prompt(s, "[A]ttack  [R]etreat: ");
  prompt(*other, "Your move. [A]ttack  [R]etreat: ");
}
static void questChallenge(Session &s, const String &line) {
  String target = up(clean(line.substring(1), 16, true));
  if (!target.length()) { prompt(s, "Use C <online adventurer>: "); return; }
  Session *other = nullptr;
  for (auto &candidate : sessions)
    if (&candidate != &s && candidate.used && candidate.authenticated &&
        candidate.user.normalized == target && duelLocation(candidate)) { other = &candidate; break; }
  if (!other) { say(s, "That adventurer is not waiting in town."); prompt(s, "Use C <online adventurer>: "); return; }
  s.duelNode = other->node; s.duelTurnNode = s.node; s.state = QUEST_DUEL_WAIT;
  other->duelNode = s.node; other->duelTurnNode = s.node; other->state = QUEST_DUEL_OFFER;
  activity(s, "WAITING FOR ARENA ACCEPT"); activity(*other, "ARENA CHALLENGE");
  say(s, "Challenge sent to " + other->user.handle + ". Awaiting their answer.");
  prompt(s, "[Q] Cancel challenge: ");
  say(*other, "\r\n\x1b[1;35m" + s.user.handle + " challenges you to an arena duel!\x1b[0m");
  prompt(*other, "Accept? [Y]es / [N]o: ");
}
static void enterQuest(Session &s) {
  if (loadQuest(s)) questMenu(s, true);
  else prompt(s, "Door command? ");
}
static void enterForest(Session &s) {
  if (!s.quest.fights) {
    say(s, "No forest fights remaining. Ask the SysOp for more turns.");
    questMenu(s);
    return;
  }
  s.quest.fights--;
  if (!saveQuest(s)) {
    s.quest.fights++;
    questMenu(s);
    return;
  }
  static const char *beasts[] = {"ash wolf", "barrow wraith", "forest troll", "marsh drake", "cave goblin"};
  s.quest.monster = beasts[esp_random() % 5];
  s.quest.monsterHp = 12 + s.quest.level * 8 + esp_random() % 10;
  s.quest.monsterAtk = 3 + s.quest.level * 2 + esp_random() % 5;
  s.state = QUEST_FIGHT;
  activity(s, "FOREST: " + s.quest.monster);
  say(s, "A " + s.quest.monster + " blocks the path!  HP " + s.quest.monsterHp);
  prompt(s, "[A]ttack  [R]un? ");
}
static const char *const weapons[] = {"Fists", "Ash Club", "Iron Sword", "Blacksteel Blade"};
static const char *const armorNames[] = {"Clothes", "Leather", "Chainmail", "Blacksteel Plate"};
static constexpr uint16_t gearPrice[] = {0, 45, 150, 500};
static constexpr uint8_t gearPower[] = {0, 2, 5, 9};

static void fightResult(Session &s, const Quest &before, const String &report) {
  if (!saveQuest(s.quest)) {
    s.quest = before;
    say(s, "Could not save the outcome. Your last blow was rolled back.");
    prompt(s, "[A]ttack  [R]un? ");
    return;
  }
  say(s, report);
  s.state = QUEST_RESULT;
  activity(s, "FOREST RESULT");
  prompt(s, "[ENTER] Town  [F] Search the forest again: ");
}
static void questAttack(Session &s) {
  const Quest before = s.quest;
  uint16_t hit = 4 + s.quest.level * 3 + esp_random() % 8 + gearPower[s.quest.weapon];
  const bool critical = esp_random() % 10 == 0;
  if (critical) hit *= 2;
  const String strike = (critical ? String("\x1b[1;33mCritical strike!\x1b[0m ") : String()) +
                        "You hit for " + hit + ". ";
  if (hit >= s.quest.monsterHp) {
    s.quest.monsterHp = 0;
    const uint32_t gold = std::min<uint32_t>(8u + s.quest.level * 7u + esp_random() % 14, UINT32_MAX - s.quest.gold);
    const uint32_t xp = std::min<uint32_t>(10u + s.quest.level * 12u, UINT32_MAX - s.quest.exp);
    s.quest.gold += gold;
    s.quest.exp += xp;
    if (s.quest.kills != UINT32_MAX) s.quest.kills++;
    if (UINT32_MAX - s.quest.fame >= 1u + s.quest.level / 4u) s.quest.fame += 1u + s.quest.level / 4u;
    fightResult(s, before, strike + "\x1b[1;32mYou slew the " + s.quest.monster +
                "!\x1b[0m\r\nYou found " + gold + " gold and earned " + xp + " experience." +
                "\r\nHP: " + s.quest.hp + "/" + s.quest.maxHp + "  Forest fights: " + s.quest.fights +
                "  Fame: " + s.quest.fame);
    return;
  }
  s.quest.monsterHp -= hit;
  const int attack = 1 + esp_random() % s.quest.monsterAtk;
  const uint16_t hurt = max(1, attack - int(gearPower[s.quest.armor]));
  if (hurt >= s.quest.hp) {
    const uint32_t loss = s.quest.gold - s.quest.gold / 2;
    s.quest.gold -= loss;
    s.quest.hp = max(1, int(s.quest.maxHp) / 2);
    fightResult(s, before, strike + "\x1b[1;31mThe " + s.quest.monster + " defeated you.\x1b[0m" +
                "\r\nYou wake at the healer. You lost " + loss + " gold.");
    return;
  }
  s.quest.hp -= hurt;
  say(s, strike + s.quest.monster + " HP: " + s.quest.monsterHp);
  say(s, "It hits you for " + String(hurt) + ". Your HP: " + s.quest.hp);
  prompt(s, "[A]ttack  [R]un? ");
}
static void questTrain(Session &s) {
  const uint32_t needed = s.quest.level * s.quest.level * 120u;
  if (s.quest.level >= 12) say(s, "The master has nothing left to teach you.");
  else if (s.quest.exp < needed) say(s, "The master asks you to return with " + String(needed) + " XP.");
  else {
    Quest trained = s.quest;
    trained.level++;
    trained.maxHp = min(65535, int(trained.maxHp) + 12);
    trained.hp = trained.maxHp;
    if (saveQuest(trained)) {
      s.quest = trained;
      say(s, "\x1b[1;33mThe master advances you to level " + String(trained.level) + "!\x1b[0m");
    } else say(s, "Training could not be saved.");
  }
  questMenu(s);
}
static void questShop(Session &s) {
  s.state = QUEST_SHOP;
  activity(s, "TOWN ARMOURY");
  say(s, "\x1b[1;33mTOWN ARMOURY\x1b[0m  Gold: " + String(s.quest.gold));
  for (uint8_t tier = 1; tier <= MAX_GEAR; ++tier)
    say(s, String("W ") + tier + " " + weapons[tier] + "   A " + tier + " " +
           armorNames[tier] + "   " + gearPrice[tier] + " gold each");
  prompt(s, "[W #] Weapon  [A #] Armour  [Q] Town: ");
}

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
static void showMessages(Session &s, bool onlyNew) {
  if (!sdReady) { say(s, "Message base unavailable."); return; }
  File f = SD_MMC.open("/BBS/DATA/MSGS/MESSAGES.DAT", FILE_READ); uint8_t shown = 0;
  say(s, "\x1b[1;36m#     From             To               Subject\x1b[0m");
  while (f && f.available() && shown < 20) { Message m; if (!parseMessage(f.readStringUntil('\n'), m) || m.conference != s.conference || (onlyNew && m.id <= s.lastRead)) continue; char row[120]; snprintf(row, sizeof(row), "%-5lu %-16s %-16s %s", (unsigned long)m.id, m.from.c_str(), m.to.length() ? m.to.c_str() : "All", m.subject.c_str()); say(s, row); shown++; }
  if (f) f.close(); if (!shown) say(s, onlyNew ? "No new messages." : "No messages in this area.");
}
static bool readMessage(Session &s, uint32_t id) {
  if (!sdReady) return false; File f = SD_MMC.open("/BBS/DATA/MSGS/MESSAGES.DAT", FILE_READ); Message m; bool found = false;
  while (f && f.available()) { Message candidate; if (parseMessage(f.readStringUntil('\n'), candidate) && candidate.id == id && candidate.conference == s.conference) { m = candidate; found = true; break; } } if (f) f.close();
  if (!found) return false; if (m.to.length() && up(m.to) != up(s.user.handle) && s.user.level < 255) { say(s, "Private message. Access denied."); return true; }
  say(s, ""); say(s, "\x1b[1;33mMsg #: " + String(m.id) + "\x1b[0m"); say(s, "From : " + m.from); say(s, "To   : " + (m.to.length() ? m.to : "ALL")); say(s, "Subj : " + m.subject); say(s, "Date : " + m.when); say(s, ""); say(s, m.body); say(s, "");
  s.lastRead = max(s.lastRead, m.id); savePointer(s); return true;
}
static void mainMenu(Session &s) {
  s.state = MAIN; activity(s, "MAIN MENU");
  const char *fallback = "[M] Messages  [N] New scan   [C] Chat   [W] Who\r\n"
                         "[F] Files     [A] Art        [D] Doors  [U] User list\r\n"
                         "[B] Bulletins [J] Conference [P] Page   [S] System\r\n"
                         "[?] Help      [G] Goodbye";
  if (s.ansi) art(s, "/BBS/ANSI/MENUS/MAIN.ANS", fallback);
  else say(s, fallback);
  if (isSysop(s)) say(s, "\x1b[1;35m[Z]\x1b[0m SysOp console  \x1b[1;35m[USER]\x1b[0m User editor");
  prompt(s, "\x1b[1;33mMain Board Command? \x1b[0m");
}
static void ansiGallery(Session &s) {
  s.state = ANSI_GALLERY;
  activity(s, "ANSI GALLERY");
  art(s, "/BBS/ANSI/MENUS/GALLERY.ANS", "[A] Login splash  [F] File cabinet  [Q] Main Board");
  if (canUseArea(s, 1)) say(s, "[G] Browse ANSI / ASCII files");
  prompt(s, "Gallery command? ");
}
static void fileMenu(Session &s) { s.state = FILE_MENU; activity(s, "FILE AREAS"); art(s, "/BBS/ANSI/MENUS/FILES.ANS"); if (canUseArea(s, 6)) say(s, "\x1b[1;35m[7]\x1b[0m The Back Room"); if (canUseArea(s, 7)) say(s, "\x1b[1;31m[8]\x1b[0m SysOp Vault"); prompt(s, "File area? "); }
static void doorMenu(Session &s) { s.state = DOOR_MENU; activity(s, "DOORS & GAMES"); art(s, "/BBS/ANSI/MENUS/DOORS_V2.ANS"); prompt(s, "Door command? "); }
static void finishLogin(Session &s) {
  s.authenticated = true;
  // Older cards could already contain a SYSOP-named account created before
  // the level/flag bootstrap rule was added.  SYSOP is the reserved primary
  // operator identity, so normalize that record on its next successful login
  // instead of leaving the caller trapped behind the console gate forever.
  bool repairedSysop = false;
  if (s.user.normalized == "SYSOP") {
    String flags = up(s.user.flags);
    bool flagged = ("," + flags + ",").indexOf(",SYSOP,") >= 0;
    if (s.user.level != 255 || !flagged || s.user.locked) {
      s.user.level = 255;
      s.user.locked = false;
      if (!flagged) {
        if (flags.length()) flags += ",";
        flags += "SYSOP";
        s.user.flags = flags;
      }
      repairedSysop = true;
      trace("SYSOP_REPAIR", "normalized primary SYSOP account");
    }
  }
  s.user.calls++;
  if (!touchUser(s.user)) say(s, "\r\nWarning: user statistics were not saved. See Serial console.");
  if (repairedSysop) say(s, "\r\nPrimary SYSOP account normalized: level 255, SYSOP flag.");
  callers++; loadPointer(s); event("LOGIN", s.user.handle + " node " + s.node); trace("LOGIN_OK", s.user.handle + " node " + s.node); activity(s, "BULLETINS");
  say(s, "\r\nCaller #" + String(callers) + "  Security: " + String(s.user.level) + "  Conference: " + confName(s.conference));
  art(s, "/BBS/BULLETINS/001_RULES.ANS", "Welcome to the board."); s.state = BULLETIN; prompt(s, "Press ENTER for Main Board: ");
}
static void loginPrompt(Session &s) {
  s.state = LOGIN_CHOICE;
  activity(s, "AWAITING LOGIN");
  if (s.ansi) art(s, "/BBS/ANSI/LOGIN/WELCOME.ANS", "KOUVOSTO BBS");
  else say(s, "KOUVOSTO BBS");
  prompt(s, "(N)ew user, (L)ogin, or (G)oodbye? ");
}
static void newSession(Session &s) {
  s.connectedAt = s.lastAt = s.lastPoll = millis();
  loginPrompt(s);
}
static void closeSession(Session &s) {
  if (!s.used) return;
  if (playingMud(s)) mudBroadcastRoom(s, s.user.handle + " loses carrier.");
  if (s.duelNode) {
    Session *other = sessionByNode(s.duelNode);
    if (other && other != &s) {
      other->duelNode = other->duelTurnNode = 0;
      if (other->state == QUEST_DUEL_OFFER || other->state == QUEST_DUEL_WAIT || other->state == QUEST_DUEL) {
        say(*other, "\r\nYour arena opponent lost carrier.");
        questMenu(*other);
      }
    }
  }
  trace("SESSION_CLOSE", String("node ") + s.node + " " + (s.user.handle.length() ? s.user.handle : "anonymous"));
  event("LOGOFF", s.user.handle.length() ? s.user.handle : "anonymous");
  if (s.socket) s.socket.stop();
  s = Session();
}
static Session *allocate(bool web, WiFiClient incoming = WiFiClient()) {
  for (uint8_t i = 0; i < MAX_NODES; ++i) if (!sessions[i].used) { Session &s = sessions[i]; s = Session(); s.used = true; s.web = web; s.socket = incoming; s.node = i + 1; s.token = salt(); newSession(s); event("CONNECT", String("node ") + s.node + (web ? " web" : " telnet")); trace("SESSION_OPEN", String("node ") + s.node + (web ? " web" : " telnet")); return &s; } trace("SESSION_REJECT", "all nodes busy"); return nullptr;
}
static Session *webSession(uint8_t node, const String &token) { if (!node || node > MAX_NODES) return nullptr; Session &s = sessions[node - 1]; return s.used && s.web && s.token == token ? &s : nullptr; }
static void broadcastChat(Session &from, const String &line) { for (auto &s : sessions) if (s.used && s.state == CHAT) say(s, "\x1b[0;36m[" + from.user.handle + "]\x1b[0m " + line); }

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

// ---------- network adapters ----------------------------------------------------
static void root() {
  http.sendHeader("Cache-Control", "no-store");
  http.send_P(200, "text/html; charset=utf-8", kWebTerminal);
}
static void apiConnect() {
  Session *s = allocate(true);
  http.sendHeader("Cache-Control", "no-store");
  if (!s) { http.send(503, "text/plain", "All BBS nodes are busy"); return; }
  http.send(200, "application/json", String("{\"node\":") + s->node + ",\"token\":\"" + s->token + "\"}");
}
static void apiHangup() {
  Session *s = webSession(http.arg("n").toInt(), http.arg("q"));
  if (s) closeSession(*s);
  http.send(204, "text/plain", "");
}
static void sendTerminalOutput(Session &s) {
  s.lastPoll = millis();
  http.sendHeader("Cache-Control", "no-store");
  http.sendHeader("X-BBS-Password", s.hideInput ? "1" : "0");
  if (s.output.length()) http.send(200, "text/plain; charset=utf-8", s.output);
  else http.send(204, "text/plain", "");
  s.output = "";
  if (s.state == GOODBYE) closeSession(s);
}
static void apiOut() {
  Session *s = webSession(http.arg("n").toInt(), http.arg("q"));
  if (!s) { http.send(404, "text/plain", "Lost carrier"); return; }
  sendTerminalOutput(*s);
}
static void apiIn() {
  Session *s = webSession(http.arg("n").toInt(), http.arg("q"));
  if (!s || s->state == GOODBYE) { http.send(404, "text/plain", "Lost carrier"); return; }
  input(*s, http.arg("k"));
  if (s->used) sendTerminalOutput(*s);
  else http.send(404, "text/plain", "Lost carrier");
}
static void downloadFile() {
  Session *s = webSession(http.arg("n").toInt(), http.arg("q")); String path, name; size_t bytes = 0;
  if (!s || !s->authenticated || s->state != FILE_LIST || !areaFile(*s, http.arg("i").toInt(), path, name, bytes)) { http.send(404, "text/plain", "File unavailable"); return; }
  String reason; if (!downloadAllowed(s->user, bytes, reason)) { http.send(403, "text/plain", reason); return; }
  File f = SD_MMC.open(path, FILE_READ); if (!f) { http.send(404, "text/plain", "File unavailable"); return; }
  http.sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\""); http.sendHeader("Cache-Control", "no-store"); size_t sent = http.streamFile(f, "application/octet-stream"); f.close();
  if (sent == bytes) { s->user.downloadBytes += bytes; if (s->user.downloads != UINT16_MAX) s->user.downloads++; if (!touchUser(s->user)) trace("DOWNLOAD_ACCOUNT_WARN", s->user.handle); refreshLiveUser(s->user); event("DOWNLOAD", s->user.handle + " " + name); trace("DOWNLOAD", name); }
  else trace("DOWNLOAD_SHORT", name);
}
static void modemAudio() {
  if (!sdReady) { http.send(404, "text/plain", "Modem audio unavailable"); return; }
  File f = SD_MMC.open(MODEM_AUDIO, FILE_READ);
  if (!f || f.isDirectory()) { http.send(404, "text/plain", "Modem audio unavailable"); return; }
  http.sendHeader("Cache-Control", "public, max-age=86400");
  http.streamFile(f, "audio/mpeg");
  f.close();
}
static void status() { uint8_t active = 0; for (auto &s : sessions) if (s.used) active++; http.send(200, "application/json", String("{\"nodes\":") + active + ",\"max_nodes\":" + MAX_NODES + ",\"sd\":" + (sdReady ? "true" : "false") + "}"); }
static void notFound() { http.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true); http.send(302, "text/plain", "TinyBBS captive portal"); }
static void telnetInput(Session &s, uint8_t byte) {
  // Drop negotiation and subnegotiation before handing bytes to the BBS.
  switch (s.telnetState) {
    case 0:
      if (byte == 255) s.telnetState = 255;
      else input(s, String((char)byte));
      break;
    case 255:
      s.telnetState = (byte >= 250 && byte <= 254) ? byte : 0;
      break;
    case 250:
      if (byte == 255) s.telnetState = 240;
      break;
    case 240:
      s.telnetState = byte == 240 ? 0 : 250;
      break;
    default:
      if (s.telnetState == 251 || (s.telnetState == 253 && byte != 1 && byte != 3)) {
        const uint8_t response[] = {255, uint8_t(s.telnetState == 251 ? 254 : 252), byte};
        s.socket.write(response, sizeof(response));
      }
      s.telnetState = 0;
      break;
  }
}
static void tickTelnet() {
  WiFiClient incoming = telnet.accept();
  if (incoming) {
    if (allocate(false, incoming)) {
      const uint8_t options[] = {255, 251, 1, 255, 251, 3}; // WILL ECHO, WILL SGA
      incoming.write(options, sizeof(options));
    } else { incoming.print("All BBS nodes are busy.\r\n"); incoming.stop(); }
  }
  for (auto &s : sessions) if (s.used && !s.web) {
    if (!s.socket.connected()) { closeSession(s); continue; }
    for (size_t budget = 256; budget && s.used && s.socket.available() && s.state != GOODBYE; --budget)
      telnetInput(s, s.socket.read());
    if (s.output.length()) { s.socket.print(s.output); s.output = ""; }
    if (s.state == GOODBYE) closeSession(s);
  }
}

// ---------- physical SysOp panel ------------------------------------------------
static void lcdText(int x, int y, const String &s, uint16_t c) { gfx->setTextSize(1); gfx->setTextColor(c); gfx->setCursor(x, y); gfx->print(s); }
static void drawLcdLogo() {
  gfx->fillScreen(0);
  for (size_t y = 0; y < sizeof(kSceneLogo) / sizeof(kSceneLogo[0]); ++y) {
    for (size_t x = 0; x < sizeof(kSceneLogo[0]) - 1; ++x) {
      char pixel = kSceneLogo[y][x];
      uint8_t color = pixel <= '9' ? pixel - '0' : pixel - 'a' + 10;
      if (!color) continue;
      if (y < 25) gfx->fillRect(2 + x * 2, y * 2, 2, 2, kScenePalette[color]);
      else gfx->drawFastHLine(2 + x * 2, 50 + y - 25, 2, kScenePalette[color]);
    }
  }
}
static void drawLcd() {
  static uint32_t last = 0;
  if (millis() - last < 1000) return;
  last = millis();
  uint8_t active = 0;
  String call = "WAITING FOR CALL";
  for (auto &s : sessions) if (s.used) {
    active++;
    if (s.paged) call = "*** SYSOP PAGE N" + String(s.node) + " ***";
  }
  String statusLine = "NODE " + String(active) + "/" + MAX_NODES + "  SD " + (sdReady ? "OK" : "ERR");
  String ipLine = "IP " + WiFi.softAPIP().toString();
  static String previous;
  String panel = statusLine + "|" + ipLine + "|" + call;
  if (panel == previous) return;
  previous = panel;
  gfx->fillRect(0, 53, 160, 27, 0);
  gfx->drawFastHLine(0, 54, 160, 0xF81F);
  lcdText(4, 56, statusLine, sdReady ? 0x07FF : 0xF8AA);
  lcdText(4, 64, ipLine, 0xFFE0);
  lcdText(4, 72, call, call.startsWith("***") ? 0xF8AA : 0xF81F);
}

void setup() {
  pinMode(UI_BOOT_PIN, INPUT_PULLUP); Serial.begin(115200); delay(100); bootAt = millis(); trace("BOOT", String("reset_reason=") + (int)esp_reset_reason()); gfx->begin(); gfx->setRotation(1); drawLcdLogo(); lcdText(4, 64, "TINYBBS BOOTING", 0xFE66);
  SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0); sdReady = SD_MMC.begin("/sdcard", true);
  if (sdReady && prepareStorage()) { userCounters(); messageCounters(); }
  else { if (!storageFault.length()) storageFault = "storage setup failed"; sdReady = false; trace("SD_OFFLINE", storageFault); }
  String ssid = config("/BBS/CONFIG/NETWORK.INI", "ssid", "THE_BOARD"), password = config("/BBS/CONFIG/NETWORK.INI", "password", ""), mode = config("/BBS/CONFIG/NETWORK.INI", "mode", "ap");
  if (mode != "ap") trace("NETWORK", "ap_sta reserved; starting isolated AP only");
  constexpr int apLimit = ESP_WIFI_MAX_CONN_NUM < 17 - CONFIG_ESP_WIFI_ESPNOW_MAX_ENCRYPT_NUM
                        ? ESP_WIFI_MAX_CONN_NUM : 17 - CONFIG_ESP_WIFI_ESPNOW_MAX_ENCRYPT_NUM;
  int apClients = constrain(config("/BBS/CONFIG/NETWORK.INI", "max_wifi_clients", "10").toInt(), 1L, (long)apLimit);
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(ssid.c_str(), password.length() ? password.c_str() : nullptr,
                   config("/BBS/CONFIG/NETWORK.INI", "channel", "1").toInt(), false, apClients))
    trace("AP_FAIL", "could not start access point");
  dns.start(53, "*", WiFi.softAPIP());
  http.on("/", HTTP_GET, root); http.on("/api/out", HTTP_GET, apiOut); http.on("/api/in", HTTP_POST, apiIn); http.on("/download", HTTP_GET, downloadFile);
  http.on("/api/connect", HTTP_POST, apiConnect);
  http.on("/api/hangup", HTTP_POST, apiHangup);
  // Compatibility with an early browser page that used /out and /in.
  http.on("/out", HTTP_GET, apiOut); http.on("/in", HTTP_POST, apiIn);
  http.on("/status", HTTP_GET, status); http.on("/modem.mp3", HTTP_GET, modemAudio); http.onNotFound(notFound); http.begin(); telnet.begin(); event("BOOT", "TinyBBS started"); trace("NETWORK_READY", WiFi.softAPIP().toString());
}
void loop() {
  dns.processNextRequest(); http.handleClient(); tickTelnet();
  uint32_t now = millis();
  for (auto &s : sessions) {
    if (!s.used) continue;
    uint32_t idleLimit = s.authenticated ? 30UL * 60UL * 1000UL : LOGIN_TIMEOUT_MS;
    if (now - s.lastAt >= idleLimit || (s.web && now - s.lastPoll >= WEB_CARRIER_TIMEOUT_MS))
      closeSession(s);
  }
  drawLcd();
}
