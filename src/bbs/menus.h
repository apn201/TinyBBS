#pragma once
// Generated menus. The art stays in the .ANS files on the card; the options
// come from what exists and what this caller may use, and are written into
// the art at the place MENUS.INI gives (or below it when it gives none).
// Part of the single TinyBBS translation unit: included in order from main.cpp.

struct MenuItem { String key, label; const char *color = "\x1b[1;33m"; };

// "[K] Label" padded to the column width, with one space left before the next.
static String menuText(const MenuItem &it, uint8_t width) {
  String key = "[" + it.key + "]";
  int room = (int)width - (int)key.length() - 2;
  if (room < 0) room = 0;
  String label = it.label.length() > (size_t)room ? it.label.substring(0, room) : it.label;
  String out = String(it.color) + key + "\x1b[0m " + label;
  for (int pad = room - (int)label.length() + 1; pad > 0; --pad) out += ' ';
  return out;
}
static void showMenu(Session &s, const String &name, const std::vector<MenuItem> &items, const String &title) {
  const MenuLayout *m = findMenu(name);
  // Without a MENUS.INI section: the usual picture, options below it.
  String path = m ? m->art : name == "MAIN" ? board.mainMenu : "/BBS/ANSI/MENUS/" + name + ".ANS";
  bool haveArt = path.length() && sdReady && SD_MMC.exists(path);
  bool slots = haveArt && s.ansi && m && m->rows && m->height;
  if (haveArt) {
    if (slots) write(s, "\x1b[0m\x1b[2J\x1b[H");  // the slots are screen positions in the art
    art(s, path.c_str());
  } else say(s, "\r\n\x1b[1;36m" + title + "\x1b[0m");
  size_t i = 0;
  if (slots) {
    size_t cap = (size_t)m->rows * m->columns;
    for (; i < items.size() && i < cap; ++i) {
      int row = m->row + i / m->columns, col = m->col + (i % m->columns) * m->width;
      write(s, "\x1b[" + String(row) + ";" + String(col) + "H" + menuText(items[i], m->width));
    }
    write(s, "\x1b[" + String(m->height + 1) + ";1H");
  }
  uint8_t cols = m ? m->columns : 2, width = m ? m->width : 26;
  while (i < items.size()) {  // what did not fit, or no slots at all
    String row = "  ";
    for (uint8_t c = 0; c < cols && i < items.size(); ++c, ++i) row += menuText(items[i], width);
    say(s, row);
  }
}
