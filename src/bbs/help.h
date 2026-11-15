#pragma once
// ? on a key screen (docs/INTERACTION.md, C2): the keys that work everywhere,
// in the pager. Q returns to the screen ? was pressed on.
// Part of the shared core: included in order from src/bbs_core.h.

static void keysHelpBack(Session &s) {
  switch (s.helpBack) {
    case MSG_MENU: messageArea(s); break;
    case MSG_READ: listMessages(s); break;
    case JOIN_CONF: areaList(s); break;
    case DOOR_MENU: doorMenu(s); break;
    case FILE_MENU: fileMenu(s); break;
    case FILE_LIST: listArea(s); break;
    case ANSI_GALLERY: ansiGallery(s); break;
    case USER_LIST: userList(s); break;
    case BULLETIN_LIST: bulletinMenu(s); break;
    case SYSOP_MENU: sysopMenu(s); break;
    default: mainMenu(s); break;
  }
}
static void keysHelp(Session &s) {
  s.helpBack = s.state;
  std::vector<String> rows;
  rows.push_back("");
  rows.push_back("\x1b[1;36mKEYS\x1b[0m");
  rows.push_back("");
  rows.push_back("A letter works at once. You do not press ENTER.");
  rows.push_back("The keys of this screen are on it. These work on every screen:");
  rows.push_back("");
  rows.push_back("  \x1b[1;33mQ\x1b[0m       back one level");
  rows.push_back("  \x1b[1;33mX\x1b[0m       the main menu, from anywhere");
  rows.push_back("  \x1b[1;33m?\x1b[0m       this list");
  rows.push_back("  \x1b[1;33mN\x1b[0m \x1b[1;33mP\x1b[0m     next, previous (page, message, file page)");
  rows.push_back("  \x1b[1;33mSPACE\x1b[0m   next page      \x1b[1;33mB\x1b[0m  back a page");
  rows.push_back("");
  rows.push_back("A number or a word is typed, then ENTER. A prompt that wants one says so.");
  if (s.web) rows.push_back("In this terminal ESC is the same as Q.");
  if (isSysop(s) && s.helpBack == MAIN) rows.push_back("SysOp: Z opens the console. /USER and /RELOAD work here too.");
  rows.push_back("");
  openRows(s, rows, "Keys", "SPACE next  Q back to the screen", keysHelpBack);
}
