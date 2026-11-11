#pragma once
// Door menu and dispatch into the door table.
// Part of the single TinyBBS translation unit: included in order from main.cpp.

static bool doorCommand(Session &s, String &line, const String &cmd) {
  if (s.state == DOOR_MENU) {
    if (cmd == "Q" || !cmd.length()) { mainMenu(s); return true; }
    if (cmd.length() == 1)
      for (const Door &d : DOORS)
        if (strchr(d.keys, cmd[0])) { d.enter(s); return true; }
    String keys;
    for (const Door &d : DOORS) { keys += d.keys[0]; keys += ", "; }
    say(s, "Choose " + keys + "or Q."); prompt(s, "Door command? "); return true;
  }
  if (const Door *d = doorFor(s)) { d->handle(s, line, cmd); return true; }
  return false;
}
