#pragma once
// VolaMUD door.
// Part of the single TinyBBS translation unit: included in order from main.cpp.

// VolaMUD is an endless native C++ MUD. Every coordinate is generated from
// one SD-card seed, so all callers see the same world without storing a room
// database in firmware or allocating one in RAM.
static constexpr const char *MUD_DATA = "/BBS/DATA/DOORS/VOLAMUD.DAT";
static constexpr const char *MUD_NEW = "/BBS/DATA/DOORS/VOLAMUD.NEW";
static constexpr const char *MUD_BACKUP = "/BBS/DATA/DOORS/VOLAMUD.BAK";
static constexpr const char *MUD_CONFIG = "/BBS/CONFIG/VOLAMUD.INI";
static constexpr const char *MUD_WORLD = "/BBS/DOORS/VOLAMUD/WORLD.TXT";
enum MudItem : uint16_t {
  MUD_BOOTS = 1, MUD_IRON_BLADE = 2, MUD_MOON_RING = 4, MUD_PACKET_AMULET = 8,
  MUD_COPPER_SHIELD = 16, MUD_BLACK_CROWN = 32
};

static uint32_t mudSeed() {
  static bool loaded = false;
  static uint32_t seed = 19840715UL;
  if (!loaded) {
    seed = strtoul(config(MUD_CONFIG, "seed", String(seed)).c_str(), nullptr, 10);
    if (!seed) seed = 19840715UL;
    loaded = true;
  }
  return seed;
}
static uint32_t mudHash(int16_t x, int16_t y, uint32_t salt = 0) {
  uint32_t h = mudSeed() ^ salt;
  h ^= uint32_t(int32_t(x)) * 0x45d9f3bU;
  h ^= uint32_t(int32_t(y)) * 0x119de1f3U;
  h ^= h >> 16; h *= 0x7feb352dU; h ^= h >> 15; h *= 0x846ca68bU; h ^= h >> 16;
  return h;
}
static String mudWorldValue(const char *kind, uint32_t pick, const String &fallback = "") {
  if (!sdReady) return fallback;
  File f = SD_MMC.open(MUD_WORLD, FILE_READ);
  if (!f) return fallback;
  const String prefix = String(kind) + "|";
  uint16_t count = 0;
  while (f.available()) { String line = f.readStringUntil('\n'); line.trim(); if (line.startsWith(prefix)) ++count; }
  f.close();
  if (!count) return fallback;
  f = SD_MMC.open(MUD_WORLD, FILE_READ);
  if (!f) return fallback;
  const uint16_t wanted = pick % count;
  uint16_t current = 0;
  while (f.available()) {
    String line = f.readStringUntil('\n'); line.trim();
    if (line.startsWith(prefix) && current++ == wanted) { String value = line.substring(prefix.length()); f.close(); return value; }
  }
  f.close();
  return fallback;
}
static String mudStaticValue(int16_t x, int16_t y, uint8_t wanted) {
  if (!sdReady) return "";
  File f = SD_MMC.open(MUD_WORLD, FILE_READ);
  if (!f) return "";
  while (f.available()) {
    String line = f.readStringUntil('\n'); line.trim();
    if (field(line, 0) == "ROOM" && field(line, 1).toInt() == x && field(line, 2).toInt() == y) {
      String value = dec(field(line, wanted)); f.close(); return value;
    }
  }
  f.close();
  return "";
}
static String mudRoomName(int16_t x, int16_t y) {
  String fixed = mudStaticValue(x, y, 3);
  if (fixed.length()) return fixed;
  return mudWorldValue("PLACE", mudHash(x, y, 0x766f6c61U), "Unmapped Ground");
}
static String mudRoomDescription(int16_t x, int16_t y) {
  String fixed = mudStaticValue(x, y, 4);
  if (fixed.length()) return fixed;
  uint32_t h = mudHash(x, y, 0x6d756431U);
  return mudWorldValue("BIOME", h, "The land is quiet.") + " " +
         mudWorldValue("SCENE", h >> 8, "A cold wind passes by.") + " " +
         mudWorldValue("DETAIL", h >> 16, "The way continues in every direction.");
}
static String mudNpcName(int16_t x, int16_t y) {
  String fixed = mudStaticValue(x, y, 5);
  if (fixed.length()) return fixed;
  // Most wilderness rooms are empty. A traveller is a rare, fixed feature
  // of the shared seed rather than a greeter at every coordinate.
  if (mudHash(x, y, 0x6e706331U) % 10) return "";
  return mudWorldValue("NPC", mudHash(x, y, 0x6e706331U), "a quiet traveller");
}
static bool mudIsCity(int16_t x, int16_t y) { return mudStaticValue(x, y, 3).length(); }
static bool mudHasSeededEnemy(int16_t x, int16_t y) {
  return !mudIsCity(x, y) && ((x == 13 && y == -8) || mudHash(x, y, 0x656e6d79U) % 11 == 0);
}
static String mudRandomEnemy(uint8_t pick) {
  static const char *names[] = {"restless skeleton", "road orc", "goblin scavenger"};
  return names[pick % (sizeof(names) / sizeof(names[0]))];
}
static const char *mudItemName(uint16_t item) {
  switch (item) {
    case MUD_BOOTS: return "trail boots";
    case MUD_IRON_BLADE: return "iron byteblade";
    case MUD_MOON_RING: return "moon ring";
    case MUD_PACKET_AMULET: return "packet amulet";
    case MUD_COPPER_SHIELD: return "copper shield";
    case MUD_BLACK_CROWN: return "Black Crown of Kouvosto";
    default: return "unknown item";
  }
}
static uint16_t mudItemFromWord(const String &word) {
  String key = up(word); key.replace("_", ""); key.replace("-", "");
  if (key == "BOOTS") return MUD_BOOTS;
  if (key == "BLADE" || key == "BYTEBLADE") return MUD_IRON_BLADE;
  if (key == "RING" || key == "MOONRING") return MUD_MOON_RING;
  if (key == "AMULET" || key == "PACKETAMULET") return MUD_PACKET_AMULET;
  if (key == "SHIELD") return MUD_COPPER_SHIELD;
  if (key == "CROWN" || key == "BLACKCROWN") return MUD_BLACK_CROWN;
  return 0;
}
static uint16_t mudItemSellValue(uint16_t item) {
  switch (item) { case MUD_BOOTS: return 6; case MUD_IRON_BLADE: return 14; case MUD_MOON_RING: return 22;
    case MUD_PACKET_AMULET: return 30; case MUD_COPPER_SHIELD: return 18; case MUD_BLACK_CROWN: return 120; default: return 0; }
}
static uint8_t mudItemHpBonus(uint16_t item) {
  return item == MUD_MOON_RING ? 8 : item == MUD_PACKET_AMULET ? 6 : item == MUD_BLACK_CROWN ? 12 : 0;
}
static uint8_t mudItemHitBonus(uint16_t item) { return item == MUD_IRON_BLADE ? 4 : item == MUD_BLACK_CROWN ? 2 : 0; }

static bool parseMud(const String &line, MudCharacter &character) {
  character = MudCharacter();
  character.id = strtoul(field(line, 0).c_str(), nullptr, 10);
  // Eight fields is the first tutorial-door format. Preserve its location
  // approximately while moving the character into the procedural map.
  if (!field(line, 8).length()) {
    const uint8_t oldRoom = constrain(field(line, 1).toInt(), 0L, 3L);
    static const int16_t oldX[] = {0, 1, 1, 0};
    static const int16_t oldY[] = {0, 0, 1, 1};
    character.worldX = oldX[oldRoom]; character.worldY = oldY[oldRoom];
    character.level = constrain(field(line, 2).toInt(), 1L, 99L);
    character.hp = constrain(field(line, 3).toInt(), 1L, 65535L);
    character.maxHp = constrain(field(line, 4).toInt(), long(character.hp), 65535L);
    character.experience = strtoul(field(line, 5).c_str(), nullptr, 10);
    character.coins = strtoul(field(line, 6).c_str(), nullptr, 10);
    return character.id > 0;
  }
  character.worldX = constrain(field(line, 1).toInt(), -30000L, 30000L);
  character.worldY = constrain(field(line, 2).toInt(), -30000L, 30000L);
  character.level = constrain(field(line, 3).toInt(), 1L, 99L);
  character.hp = constrain(field(line, 4).toInt(), 1L, 65535L);
  character.maxHp = constrain(field(line, 5).toInt(), long(character.hp), 65535L);
  character.experience = strtoul(field(line, 6).c_str(), nullptr, 10);
  character.coins = strtoul(field(line, 7).c_str(), nullptr, 10);
  // V2 records have a marker in field 8. V3 adds one small active task.
  if (field(line, 11) == "V3" || field(line, 12) == "V4" || field(line, 14) == "V5") {
    character.taskType = constrain(field(line, 8).toInt(), 0L, 3L);
    character.taskX = constrain(field(line, 9).toInt(), -30000L, 30000L);
    character.taskY = constrain(field(line, 10).toInt(), -30000L, 30000L);
  }
  if (field(line, 12) == "V4" || field(line, 14) == "V5") character.boots = constrain(field(line, 11).toInt(), 0L, 1L);
  if (field(line, 14) == "V5") { character.items = strtoul(field(line, 12).c_str(), nullptr, 10); character.worn = strtoul(field(line, 13).c_str(), nullptr, 10); }
  if (character.boots) character.items |= MUD_BOOTS;
  return character.id > 0;
}
static String mudLine(const MudCharacter &character) {
  return String(character.id) + "|" + character.worldX + "|" + character.worldY + "|" + character.level + "|" +
         character.hp + "|" + character.maxHp + "|" + character.experience + "|" + character.coins + "|" +
         character.taskType + "|" + character.taskX + "|" + character.taskY + "|" + character.boots + "|" +
         character.items + "|" + character.worn + "|V5";
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
static String mudEncounterName(const Session &s) {
  if (s.mudEnemyKind == 1) {
    if (s.mud.worldX == 13 && s.mud.worldY == -8) return "Lord Korja, Crown-Bearer";
    return mudWorldValue("ENEMY", mudHash(s.mud.worldX, s.mud.worldY, 0x66696768U) >> 8, "wire hound");
  }
  if (s.mudEnemyKind == 2) return mudRandomEnemy(s.mudEnemyPick);
  if (s.mudEnemyKind == 3) return mudNpcName(s.mud.worldX, s.mud.worldY);
  return "";
}
static uint16_t mudEffectiveMaxHp(const Session &s) {
  uint16_t bonus = 0;
  for (uint16_t bit = 1; bit; bit <<= 1) if (s.mud.worn & bit) bonus += mudItemHpBonus(bit);
  return min<uint16_t>(240, s.mud.maxHp + bonus);
}
static uint8_t mudHitBonus(const Session &s) {
  uint8_t bonus = 0;
  for (uint16_t bit = 1; bit; bit <<= 1) if (s.mud.worn & bit) bonus += mudItemHitBonus(bit);
  return bonus;
}
static void mudRollEncounter(Session &s) {
  s.mudEnemyHp = 0; s.mudEnemyDefeated = false; s.mudEnemyKind = 0; s.mudEnemyPick = 0;
  if (mudIsCity(s.mud.worldX, s.mud.worldY)) return;
  if (s.mud.taskType == 2 && s.mud.worldX == s.mud.taskX && s.mud.worldY == s.mud.taskY) {
    s.mudEnemyKind = 1; return;
  }
  if (mudHasSeededEnemy(s.mud.worldX, s.mud.worldY)) { s.mudEnemyKind = 1; return; }
  // A light, session-local encounter: ordinary skeletons, orcs and goblins
  // are not fixtures of the shared map and show up about two rooms in ten.
  if (esp_random() % 10 < 2) { s.mudEnemyKind = 2; s.mudEnemyPick = esp_random() % 3; }
}
static bool loadMud(Session &s) {
  for (auto &other : sessions) {
    if (&other != &s && other.used && other.user.id == s.user.id && playingMud(other)) {
      say(s, "That VolaMUD character is already on another node.");
      return false;
    }
  }
  if (loadMud(s.user.id, s.mud) && loadSharedWallet(s.user.id, s.wallet)) return true;
  say(s, "VolaMUD storage is unavailable.");
  return false;
}
static void mudLook(Session &s) {
  const bool city = mudIsCity(s.mud.worldX, s.mud.worldY);
  say(s, String("\x1b[1;") + (city ? "36m" : "32m") + "\r\n[ " + (city ? "VOLA CITY // " : "WILDERNESS // ") + mudRoomName(s.mud.worldX, s.mud.worldY) + " ]\x1b[0m");
  say(s, mudRoomDescription(s.mud.worldX, s.mud.worldY));
  say(s, "\x1b[0;37mMap: " + String(s.mud.worldX) + "," + s.mud.worldY + "  Exits: N S E W\x1b[0m");
  String npc = mudNpcName(s.mud.worldX, s.mud.worldY);
  if (npc.length()) say(s, "\x1b[1;33mHere: " + npc + ".\x1b[0m TALK, or FIGHT NPC if you really mean it.");
  if (s.mudEnemyKind && !s.mudEnemyDefeated) {
    const bool fixed = s.mudEnemyKind == 1;
    say(s, String("\x1b[1;31m") + (fixed ? "A fixed danger waits here: " : "An encounter blocks the way: ") +
           mudEncounterName(s) + ". FIGHT to engage.\x1b[0m");
  } else if (!city && !npc.length()) say(s, "\x1b[0;37mThe wilderness is empty for now.\x1b[0m");
  uint8_t present = 0;
  String names;
  for (auto &other : sessions) if (&other != &s && other.used && other.authenticated && playingMud(other) &&
      other.mud.worldX == s.mud.worldX && other.mud.worldY == s.mud.worldY) {
    if (names.length()) names += ", ";
    names += other.user.handle;
    ++present;
  }
  if (present) say(s, "Here: " + names);
}
static void mudPrompt(Session &s) {
  activity(s, "VOLAMUD: " + mudRoomName(s.mud.worldX, s.mud.worldY));
  prompt(s, "\x1b[1;33m[" + String(s.mud.hp) + "/" + mudEffectiveMaxHp(s) + " HP " + String(s.wallet.gold) + "g " + String(s.wallet.bank) + "b]\x1b[0m > ");
}
static void mudMenu(Session &s, bool entering = false) {
  if (entering) art(s, "/BBS/ANSI/DOORS/VOLAMUD.ANS", "VOLAMUD");
  s.state = MUD_MENU;
  mudLook(s);
  say(s, "Commands: LOOK, N/S/E/W, GO <direction>, TALK, BUY/SELL, INV, WEAR, TASK, BANK, SAY <text>, FIGHT [NPC], WHO, STATS, HELP, QUIT");
  mudPrompt(s);
}
static void mudBroadcastRoom(Session &s, const String &text) {
  for (auto &other : sessions) if (other.used && other.authenticated && playingMud(other) &&
      other.mud.worldX == s.mud.worldX && other.mud.worldY == s.mud.worldY) {
    say(other, text);
    if (&other != &s) mudPrompt(other);
  }
}
static void mudMove(Session &s, const String &direction) {
  int8_t dx = 0, dy = 0;
  if (direction == "N" || direction == "NORTH") dy = -1;
  else if (direction == "S" || direction == "SOUTH") dy = 1;
  else if (direction == "E" || direction == "EAST") dx = 1;
  else if (direction == "W" || direction == "WEST") dx = -1;
  else { say(s, "Use N, S, E or W."); mudPrompt(s); return; }
  if ((dx && (s.mud.worldX == (dx > 0 ? 30000 : -30000))) ||
      (dy && (s.mud.worldY == (dy > 0 ? 30000 : -30000)))) {
    say(s, "The map counter cannot go farther that way."); mudPrompt(s); return;
  }
  mudBroadcastRoom(s, s.user.handle + " leaves.");
  MudCharacter before = s.mud;
  s.mud.worldX += dx;
  s.mud.worldY += dy;
  if (!saveMud(s.mud)) { s.mud = before; say(s, "The world could not save your move."); mudPrompt(s); return; }
  mudRollEncounter(s);
  if (s.mud.taskType == 1 && s.mud.worldX == s.mud.taskX && s.mud.worldY == s.mud.taskY) {
    s.mud.taskType = 0;
    s.mud.experience += 18;
    s.wallet.gold += 5;
    if (!saveMud(s.mud) || !saveSharedWallet(s.wallet)) say(s, "Your survey reward could not be saved.");
    else say(s, "Your survey is complete. XP +18, gold +5.");
  }
  mudBroadcastRoom(s, s.user.handle + " arrives.");
  mudLook(s);
  mudPrompt(s);
}
static void mudFight(Session &s, bool targetNpc = false) {
  if (targetNpc) {
    if (!mudNpcName(s.mud.worldX, s.mud.worldY).length()) { say(s, "There is nobody here to challenge."); mudPrompt(s); return; }
    if (s.mudEnemyKind && s.mudEnemyKind != 3) { say(s, "Deal with the creature in front of you first."); mudPrompt(s); return; }
    if (!s.mudEnemyKind) { s.mudEnemyKind = 3; s.mudEnemyDefeated = false; s.mudEnemyHp = 0; }
  }
  if (!s.mudEnemyKind) { say(s, "\x1b[0;37mThere is nothing here to fight.\x1b[0m"); mudPrompt(s); return; }
  const uint32_t local = mudHash(s.mud.worldX, s.mud.worldY, 0x66696768U);
  String enemy = mudEncounterName(s);
  if (s.mudEnemyDefeated) { say(s, "The " + enemy + " has already left this place."); mudPrompt(s); return; }
  if (!s.mudEnemyHp) {
    if (s.mudEnemyKind == 1) s.mudEnemyHp = (s.mud.worldX == 13 && s.mud.worldY == -8) ? 120 + s.mud.level * 8 : 24 + (local % 25) + s.mud.level * 3;
    else if (s.mudEnemyKind == 2) s.mudEnemyHp = 7 + (esp_random() % 9) + s.mud.level;
    else s.mudEnemyHp = 120 + s.mud.level * 12;
    say(s, String(s.mudEnemyKind == 3 ? "\x1b[1;31mYou attack " : "\x1b[1;31mA ") + enemy +
           (s.mudEnemyKind == 3 ? ". This is a terrible idea.\x1b[0m" : " turns toward you.\x1b[0m"));
  }
  const uint8_t hit = 3 + esp_random() % 7 + mudHitBonus(s);
  s.mudEnemyHp = hit >= s.mudEnemyHp ? 0 : s.mudEnemyHp - hit;
  say(s, "\x1b[1;32mYou hit " + enemy + " for " + String(hit) + ".\x1b[0m");
  if (!s.mudEnemyHp) {
    MudCharacter before = s.mud;
    Wallet walletBefore = s.wallet;
    s.mudEnemyDefeated = true;
    uint16_t xp = s.mudEnemyKind == 1 ? (s.mud.worldX == 13 && s.mud.worldY == -8 ? 90 : 25) : s.mudEnemyKind == 2 ? 6 : 0;
    uint16_t gold = s.mudEnemyKind == 1 ? (s.mud.worldX == 13 && s.mud.worldY == -8 ? 40 : 6 + (local % 8)) : s.mudEnemyKind == 2 ? 1 + (esp_random() % 3) : 0;
    s.mud.experience += xp;
    s.wallet.gold += gold;
    if (s.mudEnemyKind == 1 && s.mud.worldX == 13 && s.mud.worldY == -8) s.mud.items |= MUD_BLACK_CROWN;
    else if (s.mudEnemyKind == 1 && (local & 3) == 0) s.mud.items |= (local & 4) ? MUD_MOON_RING : MUD_PACKET_AMULET;
    if (s.mud.taskType == 2 && s.mud.worldX == s.mud.taskX && s.mud.worldY == s.mud.taskY) {
      s.mud.taskType = 0;
      s.mud.experience += 20;
      s.wallet.gold += 7;
      say(s, "The contract is fulfilled. XP +20, gold +7.");
    }
    if (s.mud.experience >= uint32_t(s.mud.level) * 50u && s.mud.level < 99) {
      s.mud.level++;
      s.mud.maxHp = min<uint16_t>(200, s.mud.maxHp + 4);
      s.mud.hp = mudEffectiveMaxHp(s);
      say(s, "You reach level " + String(s.mud.level) + ".");
    }
    if (!saveMud(s.mud) || !saveSharedWallet(s.wallet)) { s.mud = before; s.wallet = walletBefore; s.mudEnemyDefeated = false; s.mudEnemyHp = 0; say(s, "The fight result could not be saved."); }
    else if (s.mudEnemyKind == 3) say(s, "\x1b[1;33m" + enemy + " calmly disarms you and lets you live. No reward.\x1b[0m");
    else say(s, "\x1b[1;33mThe " + enemy + " falls. XP +" + String(xp) + ", gold +" + String(gold) +
               (s.mud.worldX == 13 && s.mud.worldY == -8 ? ". You claim the Black Crown of Kouvosto!" : "") + "\x1b[0m");
    mudPrompt(s);
    return;
  }
  const uint8_t retaliation = s.mudEnemyKind == 3 ? 10 + esp_random() % 9 :
                              s.mudEnemyKind == 1 ? 3 + esp_random() % 6 : 1 + esp_random() % 4;
  if (retaliation >= s.mud.hp) {
    s.mud.hp = mudEffectiveMaxHp(s);
    s.mud.worldX = 0;
    s.mud.worldY = 0;
    s.mudEnemyHp = 0;
    s.mudEnemyKind = 0;
    s.mudEnemyDefeated = false;
    say(s, "\x1b[1;31mThe " + enemy + " wins. You wake at the Vola City Gate.\x1b[0m");
  } else {
    s.mud.hp -= retaliation;
    say(s, "\x1b[1;31mThe " + enemy + " hits you for " + String(retaliation) + ".\x1b[0m");
  }
  if (!saveMud(s.mud)) say(s, "Your condition could not be saved.");
  mudPrompt(s);
}
static void mudWho(Session &s) {
  say(s, "\x1b[1;36mNode  Traveller         Location\x1b[0m");
  uint8_t count = 0;
  for (auto &other : sessions) if (other.used && other.authenticated && playingMud(other)) {
    char row[100];
    snprintf(row, sizeof(row), "%-5u %-16.16s %d,%d", other.node, other.user.handle.c_str(), other.mud.worldX, other.mud.worldY);
    say(s, row);
    ++count;
  }
  if (!count) say(s, "No one is in VolaMUD.");
  mudPrompt(s);
}
static void enterMud(Session &s) {
  if (!loadMud(s)) { prompt(s, "Door command? "); return; }
  mudRollEncounter(s);
  mudMenu(s, true);
  mudBroadcastRoom(s, s.user.handle + " arrives from the BBS.");
}
static void mudTalk(Session &s) {
  const String npc = mudNpcName(s.mud.worldX, s.mud.worldY);
  if (!npc.length()) { say(s, "\x1b[0;37mThere is nobody here to talk to.\x1b[0m"); mudPrompt(s); return; }
  const bool city = mudIsCity(s.mud.worldX, s.mud.worldY);
  if (city) {
    if (s.mud.worldX == 0 && s.mud.worldY == 0)
      say(s, String("\x1b[1;33mSigrid says, 'Boots first. ") + (s.mud.boots ? "Those will do. Keep to the road." : "Mikko sells proper trail boots in the Tinker's Yard.'") + "\x1b[0m");
    else if (s.mud.worldX == 1 && s.mud.worldY == -1)
      say(s, "\x1b[1;33mMikko says, 'Twelve gold buys trail boots. BUY BOOTS. I also sell RATIONS for four.'\x1b[0m");
    else if (s.mud.worldX == -1 && s.mud.worldY == -2)
      say(s, "\x1b[1;33mAnja says, 'The ledger is shared with Legend and dice. Type BANK.'\x1b[0m");
    else if (s.mud.worldX == 2 && s.mud.worldY == -1)
      say(s, "\x1b[1;33mLeena says, 'The iron byteblade is 30 gold; a copper shield is 24. BUY BLADE or BUY SHIELD.'\x1b[0m");
    else if (s.mud.worldX == 1 && s.mud.worldY == -2)
      say(s, "\x1b[1;35mTero whispers, 'Lord Korja wears the Black Crown. Ask me for TASK if you dare.'\x1b[0m");
    else if (s.mud.worldX == 0 && s.mud.worldY == -3 || s.mud.worldX == 1 && s.mud.worldY == -3)
      say(s, "\x1b[1;33m" + npc + " says, 'There is paid work outside the lamps. Type TASK if you want it.'\x1b[0m");
    else
      say(s, "\x1b[1;33m" + npc + " says, '" + mudStaticValue(s.mud.worldX, s.mud.worldY, 6) + "'\x1b[0m");
    mudPrompt(s); return;
  }
  const String dialogue = mudWorldValue("DIALOG", mudHash(s.mud.worldX, s.mud.worldY, 0x74616c6bU),
                                        "The traveller nods, then returns to the road.");
  say(s, "\x1b[1;33m" + npc + " says, '" + dialogue + "'\x1b[0m");
  if (!(mudHash(s.mud.worldX, s.mud.worldY, 0x6a6f6273U) & 1)) say(s, "They have work. Type TASK if you want it.");
  mudPrompt(s);
}
static void mudTask(Session &s) {
  if (s.mud.taskType) {
    if (s.mud.taskType == 3) {
      if (s.mud.worldX == 1 && s.mud.worldY == -2 && (s.mud.items & MUD_BLACK_CROWN)) {
        s.mud.items &= ~MUD_BLACK_CROWN; s.mud.worn &= ~MUD_BLACK_CROWN; s.mud.taskType = 0;
        s.mud.experience += 120; s.wallet.gold += 100;
        if (!saveMud(s.mud) || !saveSharedWallet(s.wallet)) say(s, "The archive reward could not be saved.");
        else say(s, "\x1b[1;33mThe archive accepts the Black Crown. XP +120, gold +100.\x1b[0m");
      } else say(s, "\x1b[1;35mCrown task: Lord Korja, Crown-Bearer waits at 13,-8. Bring the Black Crown to the BBS Archive.\x1b[0m");
      mudPrompt(s); return;
    }
    String kind = s.mud.taskType == 1 ? "Survey" : "Hunt";
    say(s, kind + " task: " + String(s.mud.taskX) + "," + s.mud.taskY +
           (s.mud.taskType == 1 ? ". Reach that place." : ". Defeat one enemy there."));
    mudPrompt(s);
    return;
  }
  const bool cityGiver = (s.mud.worldX == 0 && s.mud.worldY == -3) || (s.mud.worldX == 1 && s.mud.worldY == -3) ||
                          (s.mud.worldX == 1 && s.mud.worldY == -2);
  const bool wildGiver = !mudIsCity(s.mud.worldX, s.mud.worldY) && mudNpcName(s.mud.worldX, s.mud.worldY).length() &&
                          !(mudHash(s.mud.worldX, s.mud.worldY, 0x6a6f6273U) & 1);
  if (!cityGiver && !wildGiver) { say(s, "\x1b[0;37mNo one here is offering work. Ask a scout, guild member or rare traveller.\x1b[0m"); mudPrompt(s); return; }
  if (s.mud.worldX == 1 && s.mud.worldY == -2) {
    s.mud.taskType = 3; s.mud.taskX = 13; s.mud.taskY = -8;
    if (!saveMud(s.mud)) { s.mud.taskType = 0; say(s, "The archive ledger could not be saved."); }
    else say(s, "\x1b[1;35mArchive task accepted: bring the Black Crown of Kouvosto from Lord Korja at 13,-8.\x1b[0m");
    mudPrompt(s); return;
  }
  const uint32_t h = mudHash(s.mud.worldX, s.mud.worldY, 0x7461736bU);
  const int16_t distance = 2 + (h % 5);
  const int8_t dx = (h & 1) ? distance : -distance;
  const int8_t dy = (h & 2) ? distance : -distance;
  s.mud.taskType = (h & 4) ? 2 : 1;
  s.mud.taskX = constrain(long(s.mud.worldX) + dx, -30000L, 30000L);
  s.mud.taskY = constrain(long(s.mud.worldY) + dy, -30000L, 30000L);
  if (!saveMud(s.mud)) { s.mud.taskType = 0; say(s, "The task ledger could not be saved."); mudPrompt(s); return; }
  String flavour = mudStaticValue(s.mud.worldX, s.mud.worldY, 6);
  if (!flavour.length()) flavour = mudWorldValue("TASK", h >> 8, "Map the road and come back alive.");
  if (flavour.length()) say(s, flavour);
  say(s, String(s.mud.taskType == 1 ? "Survey" : "Hunt") + " accepted: " + s.mud.taskX + "," + s.mud.taskY + ".");
  mudPrompt(s);
}
static void mudBuy(Session &s, const String &args) {
  String item = tokenAt(up(args), 0);
  uint32_t price = 0;
  if (s.mud.worldX == 1 && s.mud.worldY == -1 && item == "BOOTS") price = 12;
  else if (s.mud.worldX == 1 && s.mud.worldY == -1 && (item == "RATIONS" || item == "RATION")) price = 4;
  else if (s.mud.worldX == 2 && s.mud.worldY == -1 && item == "BLADE") price = 30;
  else if (s.mud.worldX == 2 && s.mud.worldY == -1 && item == "SHIELD") price = 24;
  else { say(s, "\x1b[0;37mThere is nothing suitable for sale here.\x1b[0m"); mudPrompt(s); return; }
  uint16_t gear = item == "BOOTS" ? MUD_BOOTS : item == "BLADE" ? MUD_IRON_BLADE : item == "SHIELD" ? MUD_COPPER_SHIELD : 0;
  if (gear && (s.mud.items & gear)) { say(s, "You already own that."); mudPrompt(s); return; }
  if (s.wallet.gold < price) { say(s, "\x1b[1;31mYou need " + String(price) + " gold.\x1b[0m"); mudPrompt(s); return; }
  Wallet walletBefore = s.wallet; MudCharacter before = s.mud;
  s.wallet.gold -= price;
  if (item == "BOOTS") {
    s.mud.boots = 1; s.mud.items |= MUD_BOOTS;
  } else if (gear) s.mud.items |= gear;
  else s.mud.hp = min<uint16_t>(mudEffectiveMaxHp(s), s.mud.hp + 10);
  if (!saveMud(s.mud) || !saveSharedWallet(s.wallet)) { s.wallet = walletBefore; s.mud = before; say(s, "The sale could not be saved."); }
  else say(s, "\x1b[1;32mPurchased " + item + " for " + String(price) + " gold.\x1b[0m");
  mudPrompt(s);
}
static void mudInventory(Session &s) {
  say(s, "\x1b[1;36mInventory / worn items\x1b[0m");
  bool any = false;
  for (uint16_t bit = 1; bit; bit <<= 1) if (s.mud.items & bit) { say(s, String(s.mud.worn & bit ? "\x1b[1;32m* " : "\x1b[0;37m- ") + mudItemName(bit) + "\x1b[0m"); any = true; }
  if (!any) say(s, "\x1b[0;37mYour pack is empty.\x1b[0m");
  say(s, "WEAR <item> equips it. At the Tinker's Yard, SELL <item> turns most loot into gold.");
  mudPrompt(s);
}
static void mudWear(Session &s, const String &args) {
  uint16_t item = mudItemFromWord(tokenAt(args, 0));
  if (!item || !(s.mud.items & item)) { say(s, "You do not have that item."); mudPrompt(s); return; }
  s.mud.worn |= item;
  s.mud.hp = min<uint16_t>(s.mud.hp, mudEffectiveMaxHp(s));
  if (!saveMud(s.mud)) say(s, "Could not save your equipment.");
  else say(s, "\x1b[1;32mYou equip the " + String(mudItemName(item)) + ".\x1b[0m");
  mudPrompt(s);
}
static void mudSell(Session &s, const String &args) {
  if (s.mud.worldX != 1 || s.mud.worldY != -1) { say(s, "Mikko buys salvage at the Tinker's Yard."); mudPrompt(s); return; }
  uint16_t item = mudItemFromWord(tokenAt(args, 0));
  if (!item || !(s.mud.items & item)) { say(s, "You do not have that item."); mudPrompt(s); return; }
  if (item == MUD_BLACK_CROWN && s.mud.taskType == 3) { say(s, "That crown belongs at the Archive until your task is settled."); mudPrompt(s); return; }
  uint16_t value = mudItemSellValue(item); if (!value) { say(s, "Mikko will not buy that."); mudPrompt(s); return; }
  MudCharacter before = s.mud; Wallet walletBefore = s.wallet;
  s.mud.items &= ~item; s.mud.worn &= ~item; if (item == MUD_BOOTS) s.mud.boots = 0; s.wallet.gold += value;
  if (!saveMud(s.mud) || !saveSharedWallet(s.wallet)) { s.mud = before; s.wallet = walletBefore; say(s, "The sale could not be saved."); }
  else say(s, "\x1b[1;32mMikko pays " + String(value) + " gold for the " + mudItemName(item) + ".\x1b[0m");
  mudPrompt(s);
}
static void mudBank(Session &s, const String &args) {
  if (s.mud.worldX != -1 || s.mud.worldY != -2) {
    say(s, "The Bank of Kouvosto is at -1,-2. Its ledger is not carried into the wild.");
    mudPrompt(s);
    return;
  }
  String action = tokenAt(up(args), 0);
  uint32_t amount = strtoul(tokenAt(args, 1).c_str(), nullptr, 10);
  if (!action.length() || action == "BALANCE") {
    say(s, "Anja checks the shared ledger: purse " + String(s.wallet.gold) + " gold, bank " + String(s.wallet.bank) + ".");
    say(s, "BANK DEPOSIT <gold> or BANK WITHDRAW <gold>.");
  } else if (action == "DEPOSIT" && amount && amount <= s.wallet.gold) {
    if (UINT32_MAX - s.wallet.bank < amount) say(s, "The ledger refuses a number that large.");
    else {
      Wallet before = s.wallet; s.wallet.gold -= amount; s.wallet.bank += amount;
      if (!saveSharedWallet(s.wallet)) { s.wallet = before; say(s, "The ledger would not close. No gold moved."); }
      else say(s, "Deposited " + String(amount) + " gold. This purse is shared with Legend and the dice cabinet.");
    }
  } else if (action == "WITHDRAW" && amount && amount <= s.wallet.bank) {
    if (UINT32_MAX - s.wallet.gold < amount) say(s, "Your purse cannot hold that much gold.");
    else {
      Wallet before = s.wallet; s.wallet.bank -= amount; s.wallet.gold += amount;
      if (!saveSharedWallet(s.wallet)) { s.wallet = before; say(s, "The ledger would not close. No gold moved."); }
      else say(s, "Withdrew " + String(amount) + " gold.");
    }
  } else say(s, "Use BANK, BANK DEPOSIT <gold>, or BANK WITHDRAW <gold> within your balance.");
  mudPrompt(s);
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
  if (cmd == "TALK" || cmd == "SPEAK") { mudTalk(s); return; }
  if (cmd == "BUY") { mudBuy(s, args); return; }
  if (cmd == "SELL") { mudSell(s, args); return; }
  if (cmd == "INVENTORY" || cmd == "INV" || cmd == "I") { mudInventory(s); return; }
  if (cmd == "WEAR" || cmd == "EQUIP") { mudWear(s, args); return; }
  if (cmd == "TASK" || cmd == "TASKS" || cmd == "JOB") { mudTask(s); return; }
  if (cmd == "BANK") { mudBank(s, args); return; }
  if (cmd == "FIGHT" || cmd == "ATTACK" || cmd == "KILL") {
    String target = tokenAt(up(args), 0);
    mudFight(s, target == "NPC" || target == "PERSON" || target == "CITIZEN");
    return;
  }
  if (cmd == "STATS" || cmd == "STATUS") {
    say(s, "\x1b[1;36mLevel " + String(s.mud.level) + "  HP " + s.mud.hp + "/" + mudEffectiveMaxHp(s) + "  Gold " + s.wallet.gold + "  Bank " + s.wallet.bank + "  XP " + s.mud.experience +
           "  Map " + s.mud.worldX + "," + s.mud.worldY + "\x1b[0m");
    mudPrompt(s);
    return;
  }
  if (cmd == "HELP" || !cmd.length()) {
    say(s, "LOOK, N/S/E/W, GO <direction>, TALK, BUY/SELL, INV, WEAR, TASK, BANK, SAY <text>, FIGHT [NPC], WHO, STATS and QUIT.");
    mudPrompt(s);
    return;
  }
  say(s, "Unknown VolaMUD command. Type HELP.");
  mudPrompt(s);
}
