#pragma once
// ANSI gallery, file areas and the file viewer.
// Part of the single TinyBBS translation unit: included in order from main.cpp.

static bool fileCommand(Session &s, String &line, const String &cmd) {
  if (s.state == ANSI_GALLERY) {
    if (cmd == "Q" || !cmd.length()) { mainMenu(s); return true; }
    if (cmd == "G" && canUseArea(s, 1)) {
      s.galleryCabinet = true;
      s.fileArea = 1;
      s.fileOffset = 0;
      listArea(s);
      return true;
    }
    if (cmd == "A") art(s, "/BBS/ANSI/LOGIN/WELCOME.ANS");
    else if (cmd == "F") art(s, "/BBS/ANSI/MENUS/FILES.ANS");
    else say(s, "Try A, F, G, or Q.");
    prompt(s, "[A] Login splash  [F] File cabinet  [G] Browse art  [Q] Main board: "); return true;
  }
  if (s.state == FILE_MENU) {
    if (cmd == "Q" || !cmd.length()) { mainMenu(s); return true; }
    if (cmd.length() == 1 && cmd[0] >= '1' && cmd[0] <= '8') { uint8_t area = cmd[0] - '1'; if (canUseArea(s, area)) { s.fileArea = area; s.fileOffset = 0; listArea(s); } else { say(s, "That cabinet is locked."); prompt(s, "File area? "); } }
    else { say(s, "Choose a cabinet number, or Q."); prompt(s, "File area? "); } return true;
  }
  if (s.state == FILE_LIST) {
    if (cmd == "Q" || !cmd.length()) {
      if (s.galleryCabinet) { s.galleryCabinet = false; ansiGallery(s); }
      else fileMenu(s);
      return true;
    }
    if (tokenAt(cmd, 0) == "V") {
      uint32_t number;
      if (!readNumber(afterTokens(line, 1), number) || !number || number > UINT16_MAX)
        prompt(s, "Use V <file number>: ");
      else openFileViewer(s, number);
      return true;
    }
    if (cmd == "L") { listArea(s); return true; }
    if (cmd == "N" || cmd == "P") {
      if (cmd == "N" && s.moreFiles && s.fileOffset <= UINT16_MAX - 32) s.fileOffset += 16;
      else if (cmd == "P" && s.fileOffset) s.fileOffset -= 16;
      else { prompt(s, "End of list. File command? "); return true; }
      listArea(s);
      return true;
    }
    if (cmd.startsWith("D")) { uint16_t wanted = line.substring(1).toInt(); String path, name, reason; size_t bytes = 0; if (!areaFile(s, wanted, path, name, bytes)) say(s, "No such file in this cabinet."); else if (!downloadAllowed(s.user, bytes, reason)) say(s, reason + ". Ask the SysOp."); else if (s.web) { String url = String("/download?n=") + s.node + "&q=" + s.token + "&i=" + wanted; write(s, "\x1b]9;DL;" + url + ";" + name + "\x07\r\n"); say(s, "\x1b[1;36mDownload link armed for " + name + ".\x1b[0m"); } else say(s, "Use the web terminal for downloads from this ESP32 node."); prompt(s, "File command? "); return true; }
    say(s, s.web ? "Use V <number>, D <number>, N, P, L, or Q."
                 : "Use V <number>, N, P, L, or Q."); prompt(s, "File command? "); return true;
  }
  if (s.state == FILE_VIEW) {
    if (cmd == "R") { openFileViewer(s, s.viewer.number); return true; }
    listArea(s); return true;
  }
  return false;
}
