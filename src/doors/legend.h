#pragma once
// Legend of Kouvola door, shared wallet and dice cabinet.
// Part of the single TinyBBS translation unit: included in order from main.cpp.

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
  q.day = strtoul(field(line, 14).c_str(), nullptr, 10);
  return q.id > 0;
}
static String questLine(const Quest &q) {
  return String(q.id) + "|" + q.level + "|" + q.hp + "|" + q.maxHp + "|" +
         q.gold + "|" + q.bank + "|" + q.exp + "|" + q.fights + "|" + q.weapon + "|" + q.armor +
         "|" + q.kills + "|" + q.pvpWins + "|" + q.pvpLosses + "|" + q.fame + "|" + q.day;
}
static constexpr const char *QUEST_DATA = "/BBS/DATA/DOORS/KOUVQUEST.DAT";
static constexpr const char *WALLET_DATA = "/BBS/DATA/DOORS/WALLET.DAT";
static bool parseWallet(const String &line, Wallet &wallet) {
  wallet = Wallet();
  wallet.id = strtoul(field(line, 0).c_str(), nullptr, 10);
  wallet.gold = strtoul(field(line, 1).c_str(), nullptr, 10);
  wallet.bank = strtoul(field(line, 2).c_str(), nullptr, 10);
  return wallet.id > 0;
}
static String walletLine(const Wallet &wallet) {
  return String(wallet.id) + "|" + wallet.gold + "|" + wallet.bank + "|V1";
}
static bool saveSharedWallet(const Wallet &wallet) { return putRecord(WALLET_DATA, wallet.id, walletLine(wallet)); }
static bool loadSharedWallet(uint32_t id, Wallet &wallet) {
  wallet = Wallet(); wallet.id = id;
  if (!sdReady || !id || !recoverFile(WALLET_DATA)) return false;
  File file = SD_MMC.open(WALLET_DATA, FILE_READ);
  if (file) {
    Wallet candidate;
    while (file.available()) {
      if (parseWallet(file.readStringUntil('\n'), candidate) && candidate.id == id) {
        wallet = candidate;
        file.close();
        return true;
      }
    }
    file.close();
  } else if (SD_MMC.exists(WALLET_DATA)) return false;
  // One-time migration: existing Legend of Kouvola purses become the shared
  // purse when a caller first reaches any of the three doors.
  File questFile = SD_MMC.open(QUEST_DATA, FILE_READ);
  if (questFile) {
    Quest old;
    while (questFile.available()) {
      if (parseQuest(questFile.readStringUntil('\n'), old) && old.id == id) {
        wallet.gold = old.gold; wallet.bank = old.bank;
        break;
      }
    }
    questFile.close();
  }
  return saveSharedWallet(wallet);
}
static bool loadQuest(uint32_t id, Quest &quest) {
  quest = Quest();
  quest.id = id;
  if (!sdReady || !recoverFile(QUEST_DATA)) return false;
  File file = SD_MMC.open(QUEST_DATA, FILE_READ);
  if (!file && SD_MMC.exists(QUEST_DATA)) return false;
  Quest candidate;
  while (file && file.available()) {
    if (parseQuest(file.readStringUntil('\n'), candidate) && candidate.id == id) {
      quest = candidate;
      break;
    }
  }
  if (file) file.close();
  Wallet wallet;
  if (!loadSharedWallet(id, wallet)) return false;
  quest.gold = wallet.gold;
  quest.bank = wallet.bank;
  return true;
}
static bool saveQuest(const Quest &quest) {
  Wallet wallet;  // gold and bank live in the purse the doors share
  wallet.id = quest.id; wallet.gold = quest.gold; wallet.bank = quest.bank;
  return saveSharedWallet(wallet) && putRecord(QUEST_DATA, quest.id, questLine(quest));
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
  if (loadQuest(s.user.id, s.quest)) {
    // A new day on the board clock brings a fresh set of forest fights.
    if (s.quest.day != boardDay()) {
      if (s.quest.day && s.quest.fights < Quest().fights) say(s, "\x1b[1;32mA new day dawns over Kouvola. Your forest fights are restored.\x1b[0m");
      s.quest.fights = max(s.quest.fights, Quest().fights);
      s.quest.day = boardDay();
      saveQuest(s);
    }
    return true;
  }
  say(s, "Door storage is unavailable.");
  return false;
}
static bool duelLocation(const Session &s);
// Adventurers in town who can be challenged right now, by handle.
static String duelRivals(const Session &s) {
  String out;
  for (auto &o : sessions)
    if (&o != &s && o.used && o.authenticated && duelLocation(o)) { if (out.length()) out += ", "; out += o.user.handle; }
  return out;
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
  String rivals = duelRivals(s);
  say(s, String("[F]orest [H]ealer [B]ank [T]avern [M]aster [S]hop [L]egend board ") + (rivals.length() ? "[C]hallenge " : "") + "[Q]uit");
  if (rivals.length()) say(s, "In town: " + rivals + ". C <name> challenges one to the arena.");
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
  say(s, String("[R]umor  [W]ho is in town  [L]egend board  ") + (duelRivals(s).length() ? "[C]hallenge  " : "") + "[M]eal  [Q]Town");
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
  // The other fighter sits at a waiting prompt: their news starts a new line.
  if (swing <= 0) {
    say(s, "Your blade glances off " + other->user.handle + "'s guard.");
    say(*other, "\r\n" + s.user.handle + "'s blade glances off your guard.");
  } else {
    const uint16_t damage = min<uint16_t>(other->quest.hp, swing);
    other->quest.hp -= damage;
    say(s, "\x1b[1;33mYou strike " + other->user.handle + " for " + damage + "!\x1b[0m");
    say(*other, "\r\n\x1b[1;31m" + s.user.handle + " strikes you for " + damage + "!\x1b[0m");
    if (!other->quest.hp) { finishDuel(s, *other); return; }
  }
  s.duelTurnNode = other->node;
  other->duelTurnNode = s.duelTurnNode;
  prompt(s, "Waiting for " + other->user.handle + "... ([R] retreats) ");
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
    say(s, "No forest fights remaining. They return at midnight, board time.");
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

static bool legendCommand(Session &s, String &line, const String &cmd) {
  if (s.state == QUEST_MENU) {
    if (cmd == "Q") { doorMenu(s); return true; }
    if (cmd == "F") { enterForest(s); return true; }
    if (cmd == "M") { questTrain(s); return true; }
    if (cmd == "S") { questShop(s); return true; }
    if (cmd == "T") { questTavern(s, true); return true; }
    if (cmd == "L") { questLeaderboard(s); questMenu(s); return true; }
    if (cmd == "C" || cmd.startsWith("C ")) { questChallenge(s, line); return true; }
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
      return true;
    }
    if (cmd == "B") { s.state=QUEST_BANK; activity(s,"KOUVOLA BANK"); say(s,"Bank holds "+String(s.quest.bank)+" gold. Use D <amount>, W <amount>, or Q."); prompt(s,"Bank command? "); return true; }
    say(s, duelRivals(s).length() ? "Use F, H, B, T, M, S, L, C <name>, or Q." : "Use F, H, B, T, M, S, L, or Q."); prompt(s,"Town command? "); return true;
  }
  if (s.state == QUEST_TAVERN) {
    if (cmd == "Q" || !cmd.length()) { questMenu(s); return true; }
    if (cmd == "R") { tavernRumor(s); return true; }
    if (cmd == "W") { tavernWho(s); return true; }
    if (cmd == "L") { questLeaderboard(s); questTavern(s); return true; }
    if (cmd == "M") { questMeal(s); return true; }
    if (cmd == "C" || cmd.startsWith("C ")) { questChallenge(s, line); return true; }
    say(s, duelRivals(s).length() ? "Use R, W, L, C <name>, M, or Q." : "Use R, W, L, M, or Q."); prompt(s, "Tavern command? "); return true;
  }
  if (s.state == QUEST_DUEL_WAIT) {
    if (cmd == "Q") duelCancel(s, "you withdrew the challenge");
    else prompt(s, "Waiting for the other adventurer. [Q] Cancel: ");
    return true;
  }
  if (s.state == QUEST_DUEL_OFFER) {
    if (cmd == "Y" || cmd == "YES") {
      Session *other = sessionByNode(s.duelNode);
      if (!other || other->state != QUEST_DUEL_WAIT) { duelCancel(s, "the challenge expired"); return true; }
      s.state = other->state = QUEST_DUEL;
      s.duelTurnNode = other->duelTurnNode = other->node;
      activity(s, "ARENA DUEL"); activity(*other, "ARENA DUEL");
      say(s, "\r\nThe arena gate opens. " + other->user.handle + " has the initiative.");
      say(*other, "\r\nThe arena gate opens. You have the initiative.");
      prompt(s, "Waiting for " + other->user.handle + "... ([R] retreats) ");
      prompt(*other, "[A]ttack  [R]etreat: ");
    } else if (cmd == "N" || cmd == "NO" || cmd == "Q") duelCancel(s, "the challenge was declined");
    else prompt(s, "Accept? [Y]es / [N]o: ");
    return true;
  }
  if (s.state == QUEST_DUEL) {
    if (cmd == "R") { duelCancel(s, s.user.handle + " retreated"); return true; }
    if (cmd == "A") duelAttack(s);
    else prompt(s, s.node == s.duelTurnNode ? "[A]ttack  [R]etreat: " : "Wait for your initiative... ([R] retreats) ");
    return true;
  }
  if (s.state == QUEST_DUEL_RESULT) {
    if (!cmd.length() || cmd == "Q") questMenu(s);
    else prompt(s, "[ENTER] Return to town: ");
    return true;
  }
  if (s.state == QUEST_FIGHT) {
    if (cmd == "A") { questAttack(s); return true; }
    if (cmd == "R" && saveQuest(s)) {
      say(s, "You escape with " + String(s.quest.hp) + " HP. The forest keeps the turn.");
      s.state = QUEST_RESULT;
      prompt(s, "[ENTER] Town  [F] Search the forest again: ");
      return true;
    }
    prompt(s, "[A]ttack  [R]un? "); return true;
  }
  if (s.state == QUEST_RESULT) {
    if (cmd == "F" && s.quest.fights) enterForest(s);
    else if (!cmd.length() || cmd == "Q") questMenu(s);
    else prompt(s, s.quest.fights ? "[ENTER] Town  [F] Search again: " : "No fights left. [ENTER] Town: ");
    return true;
  }
  if (s.state == QUEST_SHOP) {
    if (cmd == "Q" || !cmd.length()) { questMenu(s); return true; }
    String verb = tokenAt(cmd, 0);
    uint32_t tier;
    if ((verb != "W" && verb != "A") || !readNumber(afterTokens(line, 1), tier) || tier < 1 || tier > MAX_GEAR) {
      prompt(s, "Use W 1-3, A 1-3, or Q: "); return true;
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
    prompt(s, "[W #] Weapon  [A #] Armour  [Q] Town: "); return true;
  }
  if (s.state == QUEST_BANK) {
    if (cmd == "Q" || !cmd.length()) { questMenu(s); return true; }
    String verb = tokenAt(cmd, 0);
    uint32_t amount;
    if ((verb != "D" && verb != "W") || !readNumber(afterTokens(line, 1), amount) || !amount) {
      prompt(s, "Use D <amount>, W <amount>, or Q: "); return true;
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
    prompt(s, "Bank command? "); return true;
  }
  return false;
}

// Door interface hooks.
static bool ownsLegend(const Session &s) { return playingQuest(s) && s.state != DICE_BET; }
static void handleLegend(Session &s, String &line, const String &cmd) { legendCommand(s, line, cmd); }
