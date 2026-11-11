#pragma once
// Message bases: reading and posting.
// Part of the single TinyBBS translation unit: included in order from main.cpp.

static bool messageCommand(Session &s, String &line, const String &cmd) {
  if (s.state == MSG_MENU) { if (cmd == "R") { showMessages(s, false); prompt(s, "Read message #: "); s.state = MSG_READ; } else if (cmd == "N") { showMessages(s, true); prompt(s, "Read message #: "); s.state = MSG_READ; } else if (cmd == "P") { s.state = POST_TO; activity(s, "POSTING MESSAGE"); prompt(s, "To (ENTER = ALL): "); } else mainMenu(s); return true; }
  if (s.state == MSG_READ) { if (line.length()) { if (!readMessage(s, line.toInt())) say(s, "No such message."); } s.state = MSG_MENU; prompt(s, "(R)ead headers  (N)ew scan  (P)ost  (Q)uit: "); return true; }
  if (s.state == POST_TO) { s.pendingTo = clean(line, 16, true); s.state = POST_SUBJECT; prompt(s, "Subject: "); return true; }
  if (s.state == POST_SUBJECT) { s.pendingSubject = clean(line, 55); s.state = POST_BODY; prompt(s, "Text (one line in 0.1): "); return true; }
  if (s.state == POST_BODY) { Message m; m.conference = s.conference; m.from = s.user.handle; m.to = s.pendingTo; m.subject = s.pendingSubject.length() ? s.pendingSubject : "No subject"; m.body = clean(line, 900); m.when = nowText(); if (m.body.length() && appendMessage(m)) { s.user.messages++; touchUser(s.user); event("MESSAGE", s.user.handle + " #" + m.id); say(s, "Message #" + String(m.id) + " posted."); } else say(s, "Message not posted."); mainMenu(s); return true; }
  return false;
}
