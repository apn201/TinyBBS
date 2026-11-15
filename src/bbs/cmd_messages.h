#pragma once
// Message bases: area list, area menu, reader and posting.
// Part of the single TinyBBS translation unit: included in order from main.cpp.

static bool messageCommand(Session &s, String &line, const String &cmd) {
  if (s.state == JOIN_CONF) {
    if (cmd == "Q") { messageArea(s); return true; }
    if (cmd == "X") { mainMenu(s); return true; }
    if (cmd == "?") { keysHelp(s); return true; }
    if (!line.length()) { areaList(s); return true; }
    uint32_t c;
    if (!readNumber(line, c) || c > 255 || !canJoinConference(s.user, c)) { say(s, "That area is not available to you."); areaList(s); return true; }
    s.conference = c;
    messageArea(s); return true;
  }
  if (s.state == MSG_MENU) {
    if (cmd == "L") listMessages(s);
    else if (cmd == "N") readNew(s);
    else if (cmd == "W") startPost(s);
    else if (cmd == "A" && severalAreas(s.user)) areaList(s);
    else if (cmd == "Q" || cmd == "X") mainMenu(s);
    else if (cmd == "?") keysHelp(s);
    else if (!cmd.length()) messageArea(s);
    else { say(s, "Not a key here. ? shows the keys."); prompt(s, "Message command? "); }
    return true;
  }
  if (s.state == MSG_READ) {
    uint32_t n;
    if (cmd == "Q") messageArea(s);
    else if (cmd == "X") mainMenu(s);
    else if (cmd == "?") keysHelp(s);
    else if (!line.length()) {
      uint32_t id = firstNew(s);
      if (!id) id = stepMessage(s, 0, true);
      if (id) showMessage(s, id); else messageArea(s);
    }
    else if (readNumber(line, n)) showMessage(s, msgFromNo(n));
    else prompt(s, listPrompt());
    return true;
  }
  if (s.state == MSG_VIEW) { showMessage(s, s.msgId); return true; }   // any key: the message again
  if (s.state == MSG_KILL) { killMessage(s, cmd == "Y" || cmd == "YES"); return true; }
  if (s.state == POST_TO) {
    s.pendingTo = clean(line, 16, true);
    if (s.pendingTo.length()) { s.state = POST_PRIVATE; prompt(s, "Private, only you and " + s.pendingTo + " can read it? [Y/N]: "); }
    else { s.state = POST_SUBJECT; prompt(s, "Subject: "); }
    return true;
  }
  if (s.state == POST_PRIVATE) {
    s.pendingPrivate = cmd == "Y" || cmd == "YES";
    s.state = POST_SUBJECT; prompt(s, "Subject: ");
    return true;
  }
  if (s.state == POST_SUBJECT) {
    s.pendingSubject = textLine(line, 55);
    startEditor(s);
    return true;
  }
  if (s.state == POST_BODY) {
    if (cmd == "/A") {
      bool edit = s.msgEdit;
      s.draft = ""; s.msgEdit = 0;
      say(s, edit ? "Edit aborted, the message is unchanged." : "Message aborted.");
      if (s.replyTo || edit) { s.state = MSG_VIEW; readerPrompt(s); } else messageArea(s);
      return true;
    }
    if (cmd == "/S") {
      if (!s.draft.length()) { say(s, "Nothing to save yet. Type your message, or /A to abort."); editorPrompt(s); return true; }
      saveDraft(s); return true;
    }
    if (cmd == "/L") { listDraft(s); editorPrompt(s); return true; }
    if (cmd == "/?" || cmd == "/H") { editorHelp(s); editorPrompt(s); return true; }
    if (cmd == "/C") { s.draft = ""; s.draftLines = 0; say(s, "Cleared."); editorPrompt(s); return true; }
    if (tokenAt(cmd, 0) == "/D" || tokenAt(cmd, 0) == "/R") {
      auto lines = draftLineList(s);
      uint32_t n;
      if (!readNumber(tokenAt(cmd, 1), n) || n < 1 || n > lines.size()) { say(s, "No such line. /L lists them."); editorPrompt(s); return true; }
      if (tokenAt(cmd, 0) == "/D") lines.erase(lines.begin() + (n - 1));
      else lines[n - 1] = textLine(afterTokens(line, 2), 200);
      setDraft(s, lines);
      listDraft(s); editorPrompt(s); return true;
    }
    if (s.draftLines >= DRAFT_LINES || s.draft.length() >= DRAFT_CHARS) { say(s, "The message is full. /S saves, /A aborts."); prompt(s, "  : "); return true; }
    if (s.draftLines) s.draft += '\n';
    s.draft += textLine(line, 200);
    s.draftLines++;
    editorPrompt(s);
    return true;
  }
  return false;
}
