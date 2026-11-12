#pragma once
// ANSI gallery, file areas and the file viewer.
// Part of the single TinyBBS translation unit: included in order from main.cpp.

// The file area numbered `wanted` in this caller's file menu, or -1.
static int menuArea(const Session &s, uint32_t wanted) {
  uint32_t n = 0;
  for (size_t i = 0; i < fileAreas.size(); ++i) if (listedArea(s, i) && ++n == wanted) return i;
  return -1;
}
static void openArea(Session &s, uint8_t area) {
  s.fileArea = area; s.filePath = ""; s.fileOffset = 0;
  listArea(s);
}

static bool fileCommand(Session &s, String &line, const String &cmd) {
  if (s.state == ANSI_GALLERY) {
    if (cmd == "Q" || !cmd.length()) { mainMenu(s); return true; }
    int gallery = galleryArea(s);
    if (cmd == "G" && gallery >= 0) { s.galleryCabinet = true; openArea(s, gallery); return true; }
    if (cmd == "A" && s.ansi) art(s, "/BBS/ANSI/LOGIN/WELCOME.ANS");
    else if (cmd == "F" && s.ansi) art(s, "/BBS/ANSI/MENUS/FILES.ANS");
    else say(s, String("Try ") + (s.ansi ? "A, F, " : "") + (gallery >= 0 ? "G, " : "") + "or Q.");
    prompt(s, String(s.ansi ? "[A] Login splash  [F] File cabinet  " : "") + (gallery >= 0 ? "[G] Browse art  " : "") + "[Q] Main board: ");
    return true;
  }
  if (s.state == FILE_MENU) {
    uint32_t n;
    if (cmd == "Q" || !cmd.length()) { mainMenu(s); return true; }
    int area = readNumber(cmd, n) ? menuArea(s, n) : -1;
    if (area >= 0) openArea(s, area);
    else { say(s, "Choose an area number, or Q."); prompt(s, "File area? "); }
    return true;
  }
  if (s.state == FILE_LIST) {
    uint32_t number;
    if (cmd == "Q" || !cmd.length()) {
      if (s.filePath.length()) { leaveFolder(s); return true; }
      if (s.galleryCabinet) { s.galleryCabinet = false; ansiGallery(s); }
      else fileMenu(s);
      return true;
    }
    // A number alone opens it: a folder is entered, a file is viewed.
    if (readNumber(cmd, number) && number && number <= UINT16_MAX) {
      auto list = folderEntries(s);
      if (number > list.size()) prompt(s, "No such entry. File command? ");
      else if (list[number - 1].dir) enterFolder(s, list[number - 1].name);
      else openFileViewer(s, number);
      return true;
    }
    if (tokenAt(cmd, 0) == "V") {
      if (!readNumber(afterTokens(line, 1), number) || !number || number > UINT16_MAX) prompt(s, "Use V <file number>: ");
      else openFileViewer(s, number);
      return true;
    }
    if (cmd == "L") { listArea(s); return true; }
    if (cmd == "U" && uploadsHere(s)) { startUpload(s); return true; }
    if (cmd == "N" || cmd == "P") {
      if (cmd == "N" && s.moreFiles) s.fileOffset += FILES_PER_PAGE;
      else if (cmd == "P" && s.fileOffset) s.fileOffset = s.fileOffset > FILES_PER_PAGE ? s.fileOffset - FILES_PER_PAGE : 0;
      else { prompt(s, "End of list. File command? "); return true; }
      listArea(s);
      return true;
    }
    if (tokenAt(cmd, 0) == "D" || (cmd.startsWith("D") && cmd.length() > 1 && isdigit((unsigned char)cmd[1]))) {
      String path, name, reason;
      size_t bytes = 0;
      uint16_t wanted = afterTokens(line, 1).length() ? afterTokens(line, 1).toInt() : line.substring(1).toInt();
      if (!s.web || !fileAreas[s.fileArea].downloads) say(s, "Downloads are not available here.");
      else if (!areaFile(s, wanted, path, name, bytes)) say(s, "No such file here.");
      else if (!downloadAllowed(s.user, bytes, reason)) say(s, reason + ". Ask the SysOp.");
      else {
        String url = String("/download?n=") + s.node + "&q=" + s.token + "&i=" + wanted;
        write(s, "\x1b]9;DL;" + url + ";" + name + "\x07\r\n");
        say(s, "\x1b[1;36mDownload link armed for " + name + ".\x1b[0m");
      }
      prompt(s, "File command? "); return true;
    }
    say(s, "Type a number to open it, or use the keys listed above.");
    prompt(s, "File command? "); return true;
  }
  if (s.state == FILE_VIEW) {
    if (cmd == "R") { openFileViewer(s, s.viewer.number); return true; }
    if (cmd == "N" || cmd == "P") {
      if (!stepViewer(s, cmd == "N"))
        write(s, cmd == "N" ? "\r\nLast viewable file in this folder. [P] Prev  [R] Replay  [Q] File list: "
                            : "\r\nFirst viewable file in this folder. [N] Next  [R] Replay  [Q] File list: ");
      return true;
    }
    listArea(s); return true;
  }
  return false;
}
