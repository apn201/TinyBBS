#pragma once
// The door table. A new door is one header plus one line here.
// Part of the single TinyBBS translation unit: included in order from main.cpp.

// keys: menu keys that open the door (first one is shown in prompts).
// owns: true while the caller is inside the door.
// handle: one typed line while the caller is inside the door.
struct Door {
  const char *keys;
  const char *name;
  void (*enter)(Session &);
  bool (*owns)(const Session &);
  void (*handle)(Session &, String &, const String &);
};

static const Door DOORS[] = {
  {"LK", "Legend of Kouvola", enterQuest, ownsLegend, handleLegend},
  {"V", "VolaMUD", enterMud, playingMud, handleMud},
  {"D", "Dice cabinet", enterDice, ownsDice, handleDice},
};

static const Door *doorFor(const Session &s) {
  for (const Door &d : DOORS) if (d.owns(s)) return &d;
  return nullptr;
}
