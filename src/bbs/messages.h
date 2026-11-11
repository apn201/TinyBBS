#pragma once
// Message bases the way a 1990s board read them: pick an area, read one
// message at a time with next/previous, reply into a linked thread, and see
// a whole thread on one screen. Messages are written with a line editor.
// Part of the single TinyBBS translation unit: included in order from main.cpp.

static constexpr uint8_t DRAFT_LINES = 30;
static constexpr uint16_t DRAFT_CHARS = 2400;

// Message text keeps every printable character; storage escapes % and |.
static String textLine(const String &in, uint16_t max) {
  String out;
  for (size_t i = 0; i < in.length() && out.length() < max; ++i) {
    uint8_t c = in[i];
    if (c >= 32 && c != 127) out += (char)c;
  }
  out.trim();
  return out;
}
static bool canRead(const Session &s, const Message &m) {
  if (!m.priv) return true;
  String me = up(s.user.handle);
  return up(m.to) == me || up(m.from) == me || isSysop(s);
}
// Walk MESSAGES.DAT oldest first. fn returns false to stop.
template <typename F> static void eachMessage(F fn) {
  if (!sdReady) return;
  File f = SD_MMC.open(MSG_FILE, FILE_READ);
  while (f && f.available()) {
    Message m;
    if (parseMessage(f.readStringUntil('\n'), m) && !fn(m)) break;
  }
  if (f) f.close();
}
static bool getMessage(uint32_t id, Message &out) {
  bool ok = false;
  eachMessage([&](const Message &m) { if (m.id == id) { out = m; ok = true; return false; } return true; });
  return ok;
}
static void areaCounts(const Session &s, uint8_t conference, uint32_t &total, uint32_t &fresh) {
  total = fresh = 0;
  uint32_t mark = lastReadIn(s, conference);
  eachMessage([&](const Message &m) {
    if (m.conference == conference && canRead(s, m)) { total++; if (m.id > mark) fresh++; }
    return true;
  });
}
// The next readable message in the current area after `id`, or the one
// before it when going back. 0 when there is none.
static uint32_t stepMessage(const Session &s, uint32_t id, bool forward) {
  uint32_t found = 0;
  eachMessage([&](const Message &m) {
    if (m.conference != s.conference || !canRead(s, m)) return true;
    if (forward) { if (m.id > id) { found = m.id; return false; } return true; }
    if (m.id >= id) return false;
    found = m.id;
    return true;
  });
  return found;
}
static void sayBody(Session &s, const String &body) {
  int start = 0;
  while (start <= (int)body.length()) {
    int nl = body.indexOf('\n', start);
    if (nl < 0) nl = body.length();
    say(s, body.substring(start, nl));
    start = nl + 1;
  }
}

static void messageArea(Session &s);

// ---------- area list and area menu ---------------------------------------------
static void areaList(Session &s) {
  s.state = JOIN_CONF; activity(s, "MESSAGE AREAS");
  say(s, "\r\n\x1b[1;36mMESSAGE AREAS\x1b[0m");
  for (const auto &c : board.conferences) {
    if (!canJoinConference(s.user, c.id)) continue;
    uint32_t total, fresh;
    areaCounts(s, c.id, total, fresh);
    say(s, String(c.id == s.conference ? "\x1b[1;33m* " : "  ") + "[" + c.id + "] " + c.name + "\x1b[0m  (" + total +
           (fresh ? ", \x1b[1;32m" + String(fresh) + " new\x1b[0m" : String()) + ")" +
           (c.description.length() ? "  " + c.description : String()));
  }
  prompt(s, "Area # (ENTER = stay in " + confName(s.conference) + "): ");
}
static void messageArea(Session &s) {
  s.state = MSG_MENU; activity(s, "MESSAGES");
  art(s, "/BBS/ANSI/MENUS/MESSAGES.ANS");
  uint32_t total, fresh;
  areaCounts(s, s.conference, total, fresh);
  say(s, "\x1b[1;36mArea: " + confName(s.conference) + "\x1b[0m  " + total + " messages, " + fresh + " new");
  say(s, "[R] Read / list  [N] Read new  [P] Post  [A] Change area  [Q] Main board");
  prompt(s, "Message command? ");
}
// The last 20 headers of the area, new ones marked.
static void listMessages(Session &s) {
  struct Row { uint32_t id; String from, to, subject; bool fresh; };
  std::vector<Row> rows;
  uint32_t mark = lastReadIn(s, s.conference);
  eachMessage([&](const Message &m) {
    if (m.conference != s.conference || !canRead(s, m)) return true;
    rows.push_back({m.id, m.from, m.to.length() ? m.to : String("All"), m.subject, m.id > mark});
    if (rows.size() > 20) rows.erase(rows.begin());
    return true;
  });
  say(s, "\r\n\x1b[1;36m  #     From             To               Subject\x1b[0m");
  for (const auto &r : rows) {
    char row[120];
    snprintf(row, sizeof(row), "%s%-5lu %-16.16s %-16.16s %.40s", r.fresh ? "\x1b[1;32m*\x1b[0m " : "  ",
             (unsigned long)r.id, r.from.c_str(), r.to.c_str(), r.subject.c_str());
    say(s, row);
  }
  if (rows.empty()) say(s, "No messages in this area yet.");
  s.state = MSG_READ;
  prompt(s, "Read # (ENTER = first new, Q = back): ");
}

// ---------- the reader ----------------------------------------------------------
static void readerPrompt(Session &s) {
  prompt(s, "\x1b[1;33m[N]\x1b[0mext \x1b[1;33m[P]\x1b[0mrev \x1b[1;33m[R]\x1b[0meply \x1b[1;33m[T]\x1b[0mhread \x1b[1;33m[Q]\x1b[0muit (ENTER = next): ");
}
static void showMessage(Session &s, uint32_t id) {
  Message m;
  if (!getMessage(id, m) || m.conference != s.conference || !canRead(s, m)) {
    say(s, "No such message in this area.");
    if (s.state == MSG_VIEW) readerPrompt(s); else messageArea(s);
    return;
  }
  String replies;
  eachMessage([&](const Message &r) { if (r.replyTo == m.id && canRead(s, r)) replies += " #" + String(r.id); return true; });
  say(s, "");
  say(s, "\x1b[1;36m" + confName(m.conference) + "\x1b[0m   \x1b[1;33mMsg #" + String(m.id) + "\x1b[0m");
  say(s, "From : " + m.from);
  say(s, "To   : " + (m.to.length() ? m.to : String("ALL")) + (m.priv ? "  \x1b[1;31m(private)\x1b[0m" : ""));
  say(s, "Subj : " + m.subject);
  say(s, "Date : " + m.when);
  if (m.replyTo) say(s, "\x1b[0;36mReply to #" + String(m.replyTo) + "\x1b[0m");
  say(s, "\x1b[0;34m------------------------------------------------------------------------------\x1b[0m");
  sayBody(s, m.body);
  if (replies.length()) say(s, "\x1b[0;36mReplies:" + replies + "  (T shows the thread)\x1b[0m");
  say(s, "");
  s.msgId = m.id;
  markRead(s, m.conference, m.id);
  s.state = MSG_VIEW; activity(s, "READING MESSAGES");
  readerPrompt(s);
}
// The whole conversation the current message belongs to, oldest first, like
// an e-mail chain.
static void showThread(Session &s) {
  std::vector<std::pair<uint32_t, uint32_t>> links;  // id -> replyTo, ascending id
  eachMessage([&](const Message &m) {
    if (m.conference == s.conference && canRead(s, m)) links.push_back({m.id, m.replyTo});
    return true;
  });
  auto parentOf = [&](uint32_t id) -> uint32_t {
    auto it = std::lower_bound(links.begin(), links.end(), std::make_pair(id, (uint32_t)0));
    return it != links.end() && it->first == id ? it->second : 0;
  };
  auto rootOf = [&](uint32_t id) -> uint32_t {
    for (uint8_t i = 0; i < 32; ++i) { uint32_t p = parentOf(id); if (!p) break; id = p; }
    return id;
  };
  uint32_t root = rootOf(s.msgId);
  uint8_t shown = 0;
  say(s, "\r\n\x1b[1;36mTHREAD\x1b[0m");
  eachMessage([&](const Message &m) {
    if (m.conference != s.conference || !canRead(s, m) || rootOf(m.id) != root) return true;
    say(s, "\x1b[1;33m-- #" + String(m.id) + "  " + m.from + " -> " + (m.to.length() ? m.to : String("ALL")) + "  " + m.when +
           (m.replyTo ? "  (re #" + String(m.replyTo) + ")" : String()) + "\x1b[0m");
    if (!m.replyTo) say(s, "\x1b[1;37m" + m.subject + "\x1b[0m");
    sayBody(s, m.body);
    say(s, "");
    return ++shown < 40;
  });
  readerPrompt(s);
}

// N: start reading at the first new message of the current area.
static void readNew(Session &s) {
  uint32_t id = stepMessage(s, lastReadIn(s, s.conference), true);
  if (!id) { say(s, "\r\nNo new messages in " + confName(s.conference) + "."); messageArea(s); return; }
  showMessage(s, id);
}

// ---------- posting -------------------------------------------------------------
static void startEditor(Session &s) {
  s.draft = ""; s.draftLines = 0;
  s.state = POST_BODY; activity(s, "WRITING A MESSAGE");
  say(s, "\x1b[1;36mWrite your message one line at a time. /S saves, /A aborts. (" + String(DRAFT_LINES) + " lines max)\x1b[0m");
  prompt(s, " 1: ");
}
static void startPost(Session &s) {
  s.replyTo = 0; s.pendingTo = ""; s.pendingPrivate = false;
  s.state = POST_TO; activity(s, "POSTING MESSAGE");
  prompt(s, "To (ENTER = ALL): ");
}
static void startReply(Session &s) {
  Message m;
  if (!getMessage(s.msgId, m)) { say(s, "That message is gone."); readerPrompt(s); return; }
  s.replyTo = m.id; s.pendingTo = m.from; s.pendingPrivate = m.priv;
  s.pendingSubject = m.subject.startsWith("Re: ") ? m.subject : textLine("Re: " + m.subject, 55);
  say(s, "\r\nReplying to #" + String(m.id) + " by " + m.from + ": " + s.pendingSubject);
  startEditor(s);
}
static void saveDraft(Session &s) {
  Message m;
  m.conference = s.conference; m.from = s.user.handle; m.to = s.pendingTo;
  m.subject = s.pendingSubject.length() ? s.pendingSubject : String("No subject");
  m.body = s.draft; m.when = nowText(); m.replyTo = s.replyTo; m.priv = s.pendingPrivate && m.to.length();
  if (appendMessage(m)) {
    s.user.messages++; touchUser(s.user);
    event("MESSAGE", s.user.handle + " #" + m.id);
    say(s, "\x1b[1;32mMessage #" + String(m.id) + " posted.\x1b[0m");
  } else say(s, "Message not posted: the message base could not be written.");
  s.draft = "";
  if (s.replyTo) { s.state = MSG_VIEW; readerPrompt(s); }
  else messageArea(s);
}
