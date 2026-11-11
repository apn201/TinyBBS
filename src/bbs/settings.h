#pragma once
// Board settings from the SD card: SYSTEM.INI, CONFERENCES.INI, BULLETINS.INI.
// Read at boot and again with the SysOp RELOAD command.
// Part of the single TinyBBS translation unit: included in order from main.cpp.

struct Conference { uint8_t id = 0; String name, description, flag; uint8_t level = 0; };
struct Bulletin { String title, path, flag; uint8_t level = 0; bool mandatory = false; };
struct BoardSettings {
  String name = "KOUVOSTO BBS", location, sysop = "SYSOP";
  uint8_t maxNodes = MAX_NODES, newUserLevel = 20;
  uint32_t baud = 28800;
  String welcome = "/BBS/ANSI/LOGIN/WELCOME.ANS";
  String mainMenu = "/BBS/ANSI/MENUS/MAIN.ANS";
  String goodbye = "/BBS/ANSI/GOODBYE/GOODBYE.ANS";
  std::vector<Conference> conferences;
  std::vector<Bulletin> bulletins;
};
static BoardSettings board;

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
static String loadSettings() {
  BoardSettings b;
  Ini ini;
  if (iniLoad("/BBS/CONFIG/SYSTEM.INI", ini)) {
    b.name = iniGet(ini, "SYSTEM", "name", b.name);
    b.location = iniGet(ini, "SYSTEM", "location", b.location);
    b.sysop = iniGet(ini, "SYSTEM", "sysop", b.sysop);
    b.maxNodes = (uint8_t)constrain(iniGet(ini, "SYSTEM", "max_nodes", String(MAX_NODES)).toInt(), 1L, (long)MAX_NODES);
    b.newUserLevel = (uint8_t)constrain(iniGet(ini, "SYSTEM", "new_user_level", "20").toInt(), 0L, 254L);
    b.baud = (uint32_t)max(0L, iniGet(ini, "SYSTEM", "baud", "28800").toInt());
    b.welcome = boardPath(iniGet(ini, "SYSTEM", "welcome"), b.welcome);
    b.mainMenu = boardPath(iniGet(ini, "SYSTEM", "main_menu"), b.mainMenu);
    b.goodbye = boardPath(iniGet(ini, "SYSTEM", "goodbye"), b.goodbye);
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
      if (!bl.path.length()) continue;
      bl.flag = iniGet(ini, sec, "required_flag");
      bl.level = iniLevel(ini, sec);
      bl.mandatory = iniGet(ini, sec, "mandatory", "0").toInt() != 0;
      b.bulletins.push_back(bl);
    }
  }
  board = b;
  lineCps = board.baud / 10;
  return String(board.conferences.size()) + " conferences, " + board.bulletins.size() + " bulletins, " +
         board.maxNodes + " nodes, " + (board.baud ? String(board.baud) + " baud" : String("no speed limit"));
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
