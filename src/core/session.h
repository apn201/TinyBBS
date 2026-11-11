#pragma once
// Caller state machine, node pool and output helpers.
// Part of the single TinyBBS translation unit: included in order from main.cpp.

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
  uint16_t items = 0, worn = 0;
  int16_t worldX = 0, worldY = 0;
  uint8_t level = 1, boots = 0;
  uint8_t taskType = 0;
  int16_t taskX = 0, taskY = 0;
};
struct Wallet { uint32_t id = 0, gold = 35, bank = 0; };
static bool loadSharedWallet(uint32_t id, Wallet &wallet);
static bool saveSharedWallet(const Wallet &wallet);
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
  User user; Quest quest; MudCharacter mud; Wallet wallet;
  uint16_t mudEnemyHp = 0;
  uint8_t mudEnemyKind = 0, mudEnemyPick = 0;
  bool mudEnemyDefeated = false;
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
