#pragma once
// The dice cabinet. Shares the Legend of Kouvola purse.
// Part of the single TinyBBS translation unit: included in order from main.cpp.

static void enterDice(Session &s) { if (!loadQuest(s)) { prompt(s, "Door command? "); return; } s.state=DICE_BET; activity(s,"DICE CABINET"); say(s,"\x1b[1;33mThe neon dice cabinet takes 1-50 gold. Roll 4-6 to double your stake.\x1b[0m"); prompt(s,"Bet (Q quits): "); }
static bool ownsDice(const Session &s) { return s.state == DICE_BET; }
static void handleDice(Session &s, String &line, const String &cmd) { if (cmd == "Q" || !cmd.length()) { doorMenu(s); return; } uint32_t bet=line.toInt(); if (!bet || bet>50 || bet>s.quest.gold) { say(s,"Bet 1-50 gold, within your purse."); prompt(s,"Bet (Q quits): "); return; } s.quest.gold-=bet; uint8_t roll=1+(esp_random()%6); say(s,"The cube lands on \x1b[1;33m"+String(roll)+"\x1b[0m."); if (roll>=4) { s.quest.gold+=bet*2; say(s,"\x1b[1;32mWinner. The machine pays double.\x1b[0m"); } else say(s,"\x1b[1;31mThe cabinet keeps the stake.\x1b[0m"); saveQuest(s); prompt(s,"Bet again (Q quits): "); return; }
