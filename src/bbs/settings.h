#pragma once
// Board settings from the SD card: SYSTEM.INI, CONFERENCES.INI, BULLETINS.INI,
// MENUS.INI.
// Read at boot and again with the SysOp RELOAD command.
// Part of the single TinyBBS translation unit: included in order from main.cpp.

struct Conference { uint8_t id = 0; String name, description, flag; uint8_t level = 0; };
// A bulletin is an art file (path=) or a quick line of text (text=, | breaks lines).
struct Bulletin { String title, path, text, flag; uint8_t level = 0; bool mandatory = false; };
// A menu from MENUS.INI: header art, and where in it the generated options go.
// rows = 0 puts the options below the art instead.
struct MenuLayout {
  String name, art;
  uint8_t row = 0, col = 3, columns = 1, width = 26, rows = 0;
  uint8_t height = 0;  // lines in the art: the prompt goes below them
};
struct BoardSettings {
  String name = "KOUVOSTO BBS", location, sysop = "SYSOP";
  uint8_t maxNodes = MAX_NODES, newUserLevel = 20;
  uint32_t baud = 28800;
  // New callers: download credit and ratio. Credits are bytes.
  uint32_t newUserCredits = 1048576, creditsPerMessage = 51200, creditsPerUploadKb = 0;
  uint16_t newUserRatio = 3;  // 0 = no ratio
  uint32_t uploadMax = 0;     // largest upload in bytes, 0 = no uploads
  String welcome = "/BBS/ANSI/LOGIN/WELCOME.ANS";
  String mainMenu = "/BBS/ANSI/MENUS/MAIN.ANS";
  String goodbye = "/BBS/ANSI/GOODBYE/GOODBYE.ANS";
  std::vector<Conference> conferences;
  std::vector<Bulletin> bulletins;
  std::vector<MenuLayout> menus;
};
static BoardSettings board;
// A new account's starting credit and ratio, from SYSTEM.INI.
static void newUserDefaults(User &u) {
  u.credits = board.newUserCredits;
  u.ratioEnforced = board.newUserRatio > 0;
  u.ratio = board.newUserRatio ? board.newUserRatio : 1;
}
static const MenuLayout *findMenu(const String &name) {
  for (const auto &m : board.menus) if (m.name == name) return &m;
  return nullptr;
}
static uint8_t artHeight(const String &path) {
  File f = sdReady ? SD_MMC.open(path, FILE_READ) : File();
  if (!f) return 0;
  uint16_t lines = 0;
  uint8_t buf[256];
  while (f.available()) { size_t n = f.read(buf, sizeof(buf)); for (size_t i = 0; i < n; ++i) if (buf[i] == '\n') lines++; }
  f.close();
  return min<uint16_t>(lines, 24);
}

// Paths in the INI files are relative to /BBS. Anything climbing out is ignored.
static String boardPath(String p, const String &fallback) {
  p.trim();
  p.replace('\\', '/');
  if (!p.length() || p.indexOf("..") >= 0) return fallback;
  while (p.startsWith("/")) p.remove(0, 1);
  if (p.startsWith("BBS/") || p.startsWith("bbs/")) p.remove(0, 4);
  return "/BBS/" + p;
}
static uint8_t iniLevel(const Ini &ini, const String &section) {
  String v = iniGet(ini, section, "min_security", iniGet(ini, section, "minimum_security", "0"));
  return (uint8_t)constrain(v.toInt(), 0L, 255L);
}

// Returns a one-line summary for the SysOp.
static String loadFileAreas();  // bbs/files.h
static String loadSettings() {
  BoardSettings b;
  Ini ini;
  String epoch;
  bool sync = false;
  if (iniLoad("/BBS/CONFIG/SYSTEM.INI", ini)) {
    b.name = iniGet(ini, "SYSTEM", "name", b.name);
    b.location = iniGet(ini, "SYSTEM", "location", b.location);
    b.sysop = iniGet(ini, "SYSTEM", "sysop", b.sysop);
    b.maxNodes = (uint8_t)constrain(iniGet(ini, "SYSTEM", "max_nodes", String(MAX_NODES)).toInt(), 1L, (long)MAX_NODES);
    b.newUserLevel = (uint8_t)constrain(iniGet(ini, "SYSTEM", "new_user_level", "20").toInt(), 0L, 254L);
    b.baud = (uint32_t)max(0L, iniGet(ini, "SYSTEM", "baud", "28800").toInt());
    uint32_t size;
    if (readSize(iniGet(ini, "SYSTEM", "new_user_credits", "1M"), size)) b.newUserCredits = size;
    if (readSize(iniGet(ini, "SYSTEM", "credits_per_message", "50K"), size)) b.creditsPerMessage = size;
    if (readSize(iniGet(ini, "SYSTEM", "credits_per_upload_kb", "0"), size)) b.creditsPerUploadKb = size;
    if (readSize(iniGet(ini, "SYSTEM", "upload_max", "0"), size)) b.uploadMax = size;
    b.newUserRatio = (uint16_t)constrain(iniGet(ini, "SYSTEM", "new_user_ratio", "3").toInt(), 0L, 999L);
    b.welcome = boardPath(iniGet(ini, "SYSTEM", "welcome"), b.welcome);
    b.mainMenu = boardPath(iniGet(ini, "SYSTEM", "main_menu"), b.mainMenu);
    b.goodbye = boardPath(iniGet(ini, "SYSTEM", "goodbye"), b.goodbye);
    epoch = iniGet(ini, "SYSTEM", "epoch");
    sync = up(iniGet(ini, "SYSTEM", "clock_sync")) == "BROWSER";
  }
  if (iniLoad("/BBS/CONFIG/CONFERENCES.INI", ini)) {
    for (const auto &sec : iniSections(ini)) {
      long id = sec.toInt();
      if (id < 0 || id > 255 || (id == 0 && sec != "0")) continue;
      Conference c;
      c.id = (uint8_t)id;
      c.name = iniGet(ini, sec, "name", "Conference " + sec);
      c.description = iniGet(ini, sec, "description");
      c.flag = iniGet(ini, sec, "required_flag");
      c.level = iniLevel(ini, sec);
      b.conferences.push_back(c);
    }
  }
  // Conference 0 is where every caller starts. It always exists and is open.
  bool hasMain = false;
  for (auto &c : b.conferences) if (c.id == 0) { hasMain = true; c.level = 0; c.flag = ""; }
  if (!hasMain) { Conference c; c.name = "Main Board"; b.conferences.insert(b.conferences.begin(), c); }
  if (iniLoad("/BBS/CONFIG/BULLETINS.INI", ini)) {
    for (const auto &sec : iniSections(ini)) {
      Bulletin bl;
      bl.title = iniGet(ini, sec, "title", "Bulletin " + sec);
      bl.path = boardPath(iniGet(ini, sec, "path"), "");
      bl.text = iniGet(ini, sec, "text");
      if (!bl.path.length() && !bl.text.length()) continue;
      bl.flag = iniGet(ini, sec, "required_flag");
      bl.level = iniLevel(ini, sec);
      bl.mandatory = iniGet(ini, sec, "mandatory", "0").toInt() != 0;
      b.bulletins.push_back(bl);
    }
  }
  if (iniLoad("/BBS/CONFIG/MENUS.INI", ini)) {
    for (const auto &sec : iniSections(ini)) {
      MenuLayout m;
      m.name = up(sec);
      m.art = boardPath(iniGet(ini, sec, "art"), m.name == "MAIN" ? b.mainMenu : String());
      m.row = constrain(iniGet(ini, sec, "row", "0").toInt(), 0L, 24L);
      m.col = constrain(iniGet(ini, sec, "col", "3").toInt(), 1L, 80L);
      m.columns = constrain(iniGet(ini, sec, "columns", "1").toInt(), 1L, 4L);
      m.width = constrain(iniGet(ini, sec, "width", "26").toInt(), 8L, 80L);
      m.rows = constrain(iniGet(ini, sec, "rows", "0").toInt(), 0L, 24L);
      if (!m.row) m.rows = 0;
      m.height = artHeight(m.art);
      b.menus.push_back(m);
    }
  }
  board = b;
  lineCps = board.baud / 10;
  clockSyncBrowser = sync;
  String clock = startClock(epoch);
  String areas = loadFileAreas();
  String history = loadHistory();
  return areas + ", " + history + ", clock " + clock + (sync ? " (browser sync)" : "") + ", " + String(board.conferences.size()) + " conferences, " + board.bulletins.size() + " bulletins, " +
         board.maxNodes + " nodes, " + (board.baud ? String(board.baud) + " baud" : String("no speed limit"));
}

static void showBulletin(Session &s, const Bulletin &b) {
  if (b.path.length()) { art(s, b.path.c_str(), b.title); return; }
  say(s, "\r\n\x1b[1;36m" + b.title + "\x1b[0m");
  String text = b.text;
  text.replace("|", "\r\n");
  say(s, "\x1b[1;37m" + text + "\x1b[0m");
}
static bool hasFlag(const User &u, const String &flag) { return ("," + up(u.flags) + ",").indexOf("," + up(flag) + ",") >= 0; }
static bool canSee(const User &u, uint8_t level, const String &flag) {
  return u.level >= level && (!flag.length() || hasFlag(u, flag));
}
static const Conference *findConference(uint8_t id) {
  for (const auto &c : board.conferences) if (c.id == id) return &c;
  return nullptr;
}
static String confName(uint8_t id) { const Conference *c = findConference(id); return c ? c->name : String("Unknown"); }
static bool canJoinConference(const User &u, uint8_t id) {
  const Conference *c = findConference(id);
  return c && canSee(u, c->level, c->flag);
}
// More than one area to choose from: only then is "change area" offered.
static bool severalAreas(const User &u) {
  uint8_t n = 0;
  for (const auto &c : board.conferences) if (canJoinConference(u, c.id) && ++n > 1) return true;
  return false;
}
