#pragma once
// Caller state machine, node pool and output helpers.
// Part of the single TinyBBS translation unit: included in order from main.cpp.

enum State : uint8_t { DEAD, LOGIN_CHOICE, LOGIN_HANDLE, LOGIN_PASSWORD, NEW_HANDLE, NEW_PASSWORD, NEW_REAL, NEW_LOCATION, BULLETIN, MAIN, USER_LIST, MSG_MENU, MSG_READ, POST_TO, POST_SUBJECT, POST_BODY, JOIN_CONF, CHAT, SYSOP_MENU, SYSOP_USERS, SYSOP_DOORS, SYSOP_ADD_PASSWORD, SYSOP_DELETE_CONFIRM, ANSI_GALLERY, FILE_MENU, FILE_LIST, DOOR_MENU, QUEST_MENU, QUEST_TAVERN, QUEST_FIGHT, QUEST_RESULT, QUEST_BANK, QUEST_SHOP, DICE_BET, QUEST_DUEL_OFFER, QUEST_DUEL_WAIT, QUEST_DUEL, QUEST_DUEL_RESULT, MUD_MENU, BULLETIN_LIST, MSG_VIEW, POST_PRIVATE, SYSOP_USER_EDIT, SYSOP_USER_FIELD, SYSOP_ADD_HANDLE, SYSOP_NODES, READER, MSG_KILL, FILE_IMAGE, FILE_MUSIC, FILE_UPLOAD_DESC, FILE_UPLOAD_WAIT, SYSOP_UPLOADS, GOODBYE };
struct Quest {
  uint32_t id = 0, gold = 35, bank = 0, exp = 0;
  uint16_t hp = 30, maxHp = 30, monsterHp = 0;
  uint8_t level = 1, fights = 12, monsterAtk = 0, weapon = 0, armor = 0;
  uint32_t kills = 0, pvpWins = 0, pvpLosses = 0, fame = 0;
  uint32_t day = 0;  // board day the forest fights were last refilled
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
// A file queued for paced output: presentation art or a viewed file. Text
// written while a job is queued waits in `after`, so it appears after the file.
struct StreamJob {
  String path;
  uint32_t offset = 0, end = 0;
  bool art = false;        // presentation art: bytes pass through, newlines normalised
  bool ansi = true, utf8 = false;
  uint8_t column = 0, width = 80;
  bool lineStart = true;
  bool paced = false;      // released at modem speed (ANSI files opened with V)
  uint8_t inEsc = 0, utfLeft = 0;  // art: don't end a chunk inside these
  bool crop = false;       // plain text art wider than the screen: cut, don't wrap
  String after;
};
// The reader (core/reader.h): one pager for text files, ANSI pictures and long
// messages. A text file is indexed by the file offset of each screen line, an
// ANSI picture is drawn into an 80-column cell buffer, a message is ready rows.
struct Session;
enum ReaderKind : uint8_t { READER_TEXT, READER_ANSI, READER_ROWS };
struct Reader {
  ReaderKind kind = READER_ROWS;
  String path, title, hint;
  uint32_t start = 0, end = 0;
  bool utf8 = false, crop = false;
  std::vector<uint32_t> lines;      // READER_TEXT
  std::shared_ptr<uint32_t> cells;  // READER_ANSI: 80 per row, codepoint | attribute << 16
  uint32_t cellRows = 0;
  std::vector<String> rows;         // READER_ROWS
  uint32_t top = 0, playRow = 0;
  bool playing = false;
  String keyEsc;                    // a cursor key's escape sequence, arriving byte by byte
  bool (*onKey)(Session &, const String &) = nullptr;  // the owner's keys, tried first
  void (*onQuit)(Session &) = nullptr;
};
struct Session {
  bool used = false, web = false, ansi = true, hideInput = false, paged = false, ignoreNextLf = false;
  uint8_t node = 0, conference = 0, fileArea = 0; uint16_t userPage = 0; State state = DEAD; NetClient socket;
  String token, input, output, pendingHandle, pendingPass, pendingReal, pendingTo, pendingSubject, activity;
  String editHandle;
  uint32_t editId = 0;
  uint8_t editField = 0;  // user editor: field being changed
  State nodesBack = MAIN;  // where the SysOp node monitor returns to
  uint8_t pendingLevel = 0;
  uint8_t duelNode = 0, duelTurnNode = 0;
  uint16_t fileOffset = 0;
  bool moreFiles = false;
  bool galleryCabinet = false;
  String filePath;  // sub-folder inside the current file area, "" = its top
  String uploadTicket, uploadDesc, uploadPath;  // a browser upload being set up
  uint8_t uploadArea = 0;
  struct { uint16_t number = 0; } viewer;  // the file being viewed, 1-based in its folder
  Reader reader;  // core/reader.h: the pager for files, pictures and long messages
  User user; Quest quest; MudCharacter mud; Wallet wallet;
  uint16_t mudEnemyHp = 0;
  uint8_t mudEnemyKind = 0, mudEnemyPick = 0;
  bool mudEnemyDefeated = false;
  uint32_t connectedAt = 0, lastAt = 0, lastRead = 0;
  std::vector<std::pair<uint8_t, uint32_t>> readPtrs;  // last-read message per area
  uint32_t lastPoll = 0;
  uint32_t wsClient = 0;  // live WebSocket for a browser node, 0 = polling
  int8_t wsSecret = -1;   // password masking state last sent over the socket
  uint8_t pageNode = 0;   // SysOp only: node of a caller waiting for an answer
  uint32_t pageUser = 0;  // and that caller's user id
  uint32_t msgId = 0, replyTo = 0;  // message being read; message being answered
  uint32_t msgEdit = 0;  // message being edited, 0 = writing a new one
  String draft; uint8_t draftLines = 0; bool pendingPrivate = false;  // line editor
  // Paced output: write() -> pending (or the last job's `after`) -> output at
  // the line speed -> transport. Jobs stream files between them.
  String pending;
  std::vector<StreamJob> jobs;
  bool paused = false;
  uint32_t paceAt = 0, paceCredit = 0, sentAt = 0;
  uint32_t cps = lineCps;  // modem speed for paced files, characters per second (0 = no limit)
  int8_t wsStream = -1;  // streaming flag last sent over the socket
  bool authenticated = false;
  uint8_t telnetState = 0;
};
static Session sessions[MAX_NODES];
static Session *sessionByNode(uint8_t node) {
  if (!node || node > MAX_NODES) return nullptr;
  Session &candidate = sessions[node - 1];
  return candidate.used ? &candidate : nullptr;
}

static void write(Session &s, const String &text) {
  String &dst = s.jobs.empty() ? s.pending : s.jobs.back().after;
  dst += text;
  if (dst.length() > 14000) dst.remove(0, dst.length() - 14000);
}
static void say(Session &s, const String &text = "") { write(s, text + "\r\n"); }
static void prompt(Session &s, const String &text) { write(s, text); }
// What the caller is doing, for WHO and the node monitor. A change is also
// written to the event log, so the SysOp can follow a caller's visit.
static void activity(Session &s, const String &what) {
  if (s.activity == what) return;
  s.activity = what;
  if (s.authenticated && s.user.handle.length()) event("DOING", s.user.handle + " node " + s.node + ": " + what);
}
// Last-read message per area, in /BBS/DATA/STATE/PTR_<user>.DAT as
// "<fallback>|<conference>|c:id,c:id,...". The first two fields are the old
// format: a single high-water mark that still applies to areas not listed.
static uint32_t lastReadIn(const Session &s, uint8_t conference) {
  for (const auto &p : s.readPtrs) if (p.first == conference) return p.second;
  return s.lastRead;
}
static void savePointer(Session &s) {
  if (!sdReady || !s.user.id) return;
  File f = SD_MMC.open((String("/BBS/DATA/STATE/PTR_") + s.user.id + ".DAT").c_str(), FILE_WRITE);
  if (!f) return;
  String areas;
  for (const auto &p : s.readPtrs) { if (areas.length()) areas += ','; areas += String(p.first) + ":" + p.second; }
  f.print(String(s.lastRead) + "|" + s.conference + "|" + areas + "\n");
  f.close();
}
static void markRead(Session &s, uint8_t conference, uint32_t id) {
  for (auto &p : s.readPtrs) if (p.first == conference) { if (msgOrder(id) > msgOrder(p.second)) { p.second = id; savePointer(s); } return; }
  if (msgOrder(id) > msgOrder(s.lastRead)) { s.readPtrs.push_back({conference, id}); savePointer(s); }
}
static void loadPointer(Session &s) {
  if (!sdReady || !s.user.id) return;
  File f = SD_MMC.open((String("/BBS/DATA/STATE/PTR_") + s.user.id + ".DAT").c_str(), FILE_READ);
  if (!f) return;
  String x = f.readStringUntil('\n'); f.close();
  s.lastRead = strtoul(field(x, 0).c_str(), nullptr, 10);
  s.readPtrs.clear();
  String areas = field(x, 2);
  int start = 0;
  while (start < (int)areas.length()) {
    int comma = areas.indexOf(',', start); if (comma < 0) comma = areas.length();
    String item = areas.substring(start, comma);
    int colon = item.indexOf(':');
    if (colon > 0) s.readPtrs.push_back({(uint8_t)item.substring(0, colon).toInt(), (uint32_t)strtoul(item.substring(colon + 1).c_str(), nullptr, 10)});
    start = comma + 1;
  }
}
