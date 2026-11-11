#pragma once
// Message bases: area list, area menu, reader and posting.
// Part of the single TinyBBS translation unit: included in order from main.cpp.

static bool messageCommand(Session &s, String &line, const String &cmd) {
  if (s.state == JOIN_CONF) {
    if (line.length()) {
      uint32_t c;
      if (!readNumber(line, c) || c > 255 || !canJoinConference(s.user, c)) { say(s, "That area is not available to you."); areaList(s); return true; }
      s.conference = c;
    }
    messageArea(s); return true;
  }
  if (s.state == MSG_MENU) {
    if (cmd == "R" || cmd == "L") listMessages(s);
    else if (cmd == "N") readNew(s);
    else if (cmd == "P") startPost(s);
    else if (cmd == "A" || cmd == "J") areaList(s);
    else if (cmd == "Q" || !cmd.length()) mainMenu(s);
    else { say(s, "Use R, N, P, A or Q."); prompt(s, "Message command? "); }
    return true;
  }
  if (s.state == MSG_READ) {
    uint32_t n;
    if (cmd == "Q") messageArea(s);
    else if (!line.length()) {
      uint32_t id = stepMessage(s, lastReadIn(s, s.conference), true);
      if (!id) id = stepMessage(s, 0, true);
      if (id) showMessage(s, id); else messageArea(s);
    }
    else if (readNumber(line, n)) showMessage(s, n);
    else prompt(s, "Read # (ENTER = first new, Q = back): ");
    return true;
  }
  if (s.state == MSG_VIEW) {
    uint32_t n;
    if (!cmd.length() || cmd == "N") {
      uint32_t next = stepMessage(s, s.msgId, true);
      if (next) showMessage(s, next);
      else { say(s, "\r\nEnd of messages in " + confName(s.conference) + "."); messageArea(s); }
    } else if (cmd == "P") {
      uint32_t prev = stepMessage(s, s.msgId, false);
      if (prev) showMessage(s, prev);
      else { say(s, "This is the first message."); readerPrompt(s); }
    }
    else if (cmd == "R") startReply(s);
    else if (cmd == "T") showThread(s);
    else if (cmd == "Q") messageArea(s);
    else if (readNumber(line, n)) showMessage(s, n);
    else { say(s, "Use N, P, R, T, Q or a message number."); readerPrompt(s); }
    return true;
  }
  if (s.state == POST_TO) {
    s.pendingTo = clean(line, 16, true);
    if (s.pendingTo.length()) { s.state = POST_PRIVATE; prompt(s, "Private, only you and " + s.pendingTo + " can read it? [y/N]: "); }
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
    if (cmd == "/A") { s.draft = ""; say(s, "Message aborted."); if (s.replyTo) { s.state = MSG_VIEW; readerPrompt(s); } else messageArea(s); return true; }
    if (cmd == "/S") {
      if (!s.draft.length()) { say(s, "Nothing to save yet. Type your message, or /A to abort."); prompt(s, " 1: "); return true; }
      saveDraft(s); return true;
    }
    if (s.draftLines >= DRAFT_LINES || s.draft.length() >= DRAFT_CHARS) { say(s, "The message is full. /S saves, /A aborts."); prompt(s, "  : "); return true; }
    if (s.draftLines) s.draft += '\n';
    s.draft += textLine(line, 200);
    s.draftLines++;
    char p[8]; snprintf(p, sizeof(p), "%2u: ", s.draftLines + 1);
    prompt(s, p);
    return true;
  }
  return false;
}
