#pragma once
// Message bases the way a 1990s board read them: pick an area, read one
// message at a time with next/previous, reply into a linked thread, and see
// a whole thread on one screen. Messages are written with a line editor.
// Part of the single TinyBBS translation unit: included in order from main.cpp.

static constexpr uint8_t DRAFT_LINES = 100;
static constexpr uint16_t DRAFT_CHARS = 8000;

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
// Walk the messages oldest first, each as it is now. fn returns false to stop.
// A line whose id is not above every id before it is a revision (an edit or a
// kill) of an earlier message; its latest revision is shown in its place.
// Killed messages are skipped unless withDeleted.
// History from /BBS/MSGBASE comes first (headers only: getMessage() adds the
// body), then the messages callers posted.
template <typename F> static void eachMessage(F fn, bool withDeleted = false) {
  if (!sdReady) return;
  for (uint32_t i = 0; i < historyCount; ++i) {
    const HistoryMsg &h = historyMsgs[i];
    Message m;
    m.id = HISTORY_BASE + i; m.conference = h.conference; m.replyTo = h.replyTo; m.history = true;
    historyHeader(h, m.from, m.to, m.subject, m.when);
    if (!fn(m)) return;
  }
  File f = SD_MMC.open(MSG_FILE, FILE_READ);
  if (!f) return;
  struct Rev { uint32_t id, at; uint8_t count; };
  std::vector<Rev> revs;
  uint32_t high = 0;
  while (f.available()) {
    uint32_t at = f.position();
    String line = f.readStringUntil('\n');
    uint32_t id = strtoul(line.c_str(), nullptr, 10);
    if (!id) continue;
    if (id > high) { high = id; continue; }
    bool found = false;
    for (auto &r : revs) if (r.id == id) { r.at = at; if (r.count < 255) r.count++; found = true; break; }
    if (!found) revs.push_back({id, at, 1});
  }
  f.seek(0);
  high = 0;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    uint32_t id = strtoul(line.c_str(), nullptr, 10);
    if (!id || id <= high) continue;  // a revision line: shown at its original's place
    high = id;
    uint8_t versions = 1;
    for (auto &r : revs) if (r.id == id) {
      uint32_t back = f.position();
      if (f.seek(r.at)) line = f.readStringUntil('\n');
      f.seek(back);
      versions = r.count + 1;
      break;
    }
    Message m;
    if (!parseMessage(line, m)) continue;
    m.versions = versions;
    if (m.deleted && !withDeleted) continue;
    if (!fn(m)) break;
  }
  f.close();
}
static bool getMessage(uint32_t id, Message &out, bool withDeleted = false) {
  bool ok = false;
  eachMessage([&](const Message &m) { if (m.id == id) { out = m; ok = true; return false; } return true; }, withDeleted);
  if (ok && out.history) out.body = historyBody(id);
  return ok;
}
// Every stored version of a message, oldest first: its change history.
static std::vector<Message> messageVersions(uint32_t id) {
  std::vector<Message> list;
  File f = sdReady ? SD_MMC.open(MSG_FILE, FILE_READ) : File();
  while (f && f.available()) {
    String line = f.readStringUntil('\n');
    if (strtoul(line.c_str(), nullptr, 10) != id) continue;
    Message m;
    if (parseMessage(line, m)) list.push_back(m);
  }
  if (f) f.close();
  return list;
}
// The author and the SysOp may kill or edit a message.
// History from the card is read-only for everyone.
static bool canModify(const Session &s, const Message &m) { return !m.history && (isSysop(s) || up(m.from) == up(s.user.handle)); }
// Messages and new ones in every conference, in one pass over the message
// file (it is read twice per pass, so one pass per area adds up).
struct AreaCount { uint32_t total = 0, fresh = 0; };
static void allAreaCounts(const Session &s, AreaCount counts[256]) {
  std::unique_ptr<uint64_t[]> marks(new uint64_t[256]);  // heap: web input runs on a small stack
  for (int c = 0; c < 256; ++c) marks[c] = msgOrder(lastReadIn(s, c));
  eachMessage([&](const Message &m) {
    if (!canRead(s, m)) return true;
    AreaCount &a = counts[m.conference];
    a.total++;
    if (!m.history && msgOrder(m.id) > marks[m.conference]) a.fresh++;
    return true;
  });
}
static void areaCounts(const Session &s, uint8_t conference, uint32_t &total, uint32_t &fresh) {
  total = fresh = 0;
  uint64_t mark = msgOrder(lastReadIn(s, conference));
  eachMessage([&](const Message &m) {
    if (m.conference == conference && canRead(s, m)) { total++; if (!m.history && msgOrder(m.id) > mark) fresh++; }
    return true;
  });
}
// The next readable message in the current area after `id`, or the one
// before it when going back. 0 when there is none.
static uint32_t stepMessage(const Session &s, uint32_t id, bool forward) {
  uint32_t found = 0;
  eachMessage([&](const Message &m) {
    if (m.conference != s.conference || !canRead(s, m)) return true;
    if (forward) { if (msgOrder(m.id) > msgOrder(id)) { found = m.id; return false; } return true; }
    if (msgOrder(m.id) >= msgOrder(id)) return false;
    found = m.id;
    return true;
  });
  return found;
}

static void messageArea(Session &s);

// ---------- area list and area menu ---------------------------------------------
static void areaList(Session &s) {
  s.state = JOIN_CONF; activity(s, "MESSAGE AREAS");
  say(s, "\r\n\x1b[1;36mMESSAGE AREAS\x1b[0m");
  std::unique_ptr<AreaCount[]> counts(new AreaCount[256]);  // 2 KB: off the stack
  allAreaCounts(s, counts.get());
  for (const auto &c : board.conferences) {
    if (!canJoinConference(s.user, c.id)) continue;
    uint32_t total = counts[c.id].total, fresh = counts[c.id].fresh;
    say(s, String(c.id == s.conference ? "\x1b[1;33m* " : "  ") + "[" + c.id + "] " + c.name + "\x1b[0m  (" + total +
           (fresh ? ", \x1b[1;32m" + String(fresh) + " new\x1b[0m" : String()) + ")" +
           (c.description.length() ? "  " + c.description : String()));
  }
  prompt(s, "Area # (ENTER = stay in " + confName(s.conference) + "): ");
}
static void messageArea(Session &s) {
  s.state = MSG_MENU; activity(s, "MESSAGES");
  std::vector<MenuItem> items;
  items.push_back({"R", "Read / list"});
  items.push_back({"N", "Read new"});
  items.push_back({"P", "Post a message"});
  if (severalAreas(s.user)) items.push_back({"A", "Change area"});
  items.push_back({"Q", "Main Board"});
  showMenu(s, "MESSAGES", items, "MESSAGE BASES");
  uint32_t total, fresh;
  areaCounts(s, s.conference, total, fresh);
  say(s, "\x1b[1;36mArea: " + confName(s.conference) + "\x1b[0m  " + total + " messages, " + fresh + " new");
  prompt(s, "Message command? ");
}
// The last 20 headers of the area, new ones marked.
static void listMessages(Session &s) {
  struct Row { uint32_t id; String when, from, to, subject; bool fresh; };
  std::vector<Row> rows;
  uint32_t mark = lastReadIn(s, s.conference);
  eachMessage([&](const Message &m) {
    if (m.conference != s.conference || !canRead(s, m)) return true;
    rows.push_back({m.id, m.when, m.from, m.to.length() ? m.to : String("All"), m.subject, !m.history && msgOrder(m.id) > msgOrder(mark)});
    if (rows.size() > 20) rows.erase(rows.begin());
    return true;
  });
  char head[100];
  snprintf(head, sizeof(head), "  %-5s %-16s %-14s %-12s %s", "#", "Date", "From", "To", "Subject");
  say(s, "\r\n\x1b[1;36m" + String(head) + "\x1b[0m");
  for (const auto &r : rows) {
    char row[120];
    snprintf(row, sizeof(row), "%s%-5lu %-16.16s %-14.14s %-12.12s %.26s", r.fresh ? "\x1b[1;32m*\x1b[0m " : "  ",
             (unsigned long)msgNo(r.id), r.when.c_str(), r.from.c_str(), r.to.c_str(), r.subject.c_str());
    say(s, row);
  }
  if (rows.empty()) say(s, "No messages in this area yet.");
  s.state = MSG_READ;
  prompt(s, "Read # (ENTER = first new, Q = back): ");
}

// ---------- the reader ----------------------------------------------------------
// The keys for the current message: Kill and Edit for its author and the
// SysOp, History once it has been edited.
static String messageKeys(Session &s, bool reader) {
  Message m;
  bool known = getMessage(s.msgId, m);
  bool mine = known && canModify(s, m), history = known && m.versions > 1;
  if (reader) return String("SPACE page  N/P msg  R reply  T thread  ") + (mine ? "K kill  E edit  " : "") + (history ? "H hist  " : "") + "Q quit";
  const char *y = "\x1b[1;33m", *n = "\x1b[0m";
  return String(y) + "[N]" + n + "ext " + y + "[P]" + n + "rev " + y + "[R]" + n + "eply " + y + "[T]" + n + "hread " +
         (mine ? String(y) + "[K]" + n + "ill " + y + "[E]" + n + "dit " : String()) +
         (history ? String(y) + "[H]" + n + "istory " : String()) + y + "[Q]" + n + "uit (ENTER = next): ";
}
static void readerPrompt(Session &s) { prompt(s, messageKeys(s, false)); }
// Message text as screen rows, wrapped at 79 columns (UTF-8 aware).
static void bodyRows(std::vector<String> &rows, const String &body) {
  int start = 0;
  while (start <= (int)body.length()) {
    int nl = body.indexOf('\n', start);
    if (nl < 0) nl = body.length();
    String line = body.substring(start, nl), row;
    uint8_t cols = 0;
    for (size_t i = 0; i < line.length(); ++i) {
      uint8_t c = line[i];
      if ((c & 0xc0) != 0x80) { if (cols == 79) { rows.push_back(row); row = ""; cols = 0; } cols++; }
      row += (char)c;
    }
    rows.push_back(row);
    start = nl + 1;
  }
}
static bool msgReaderKey(Session &s, const String &k);
static void msgReaderQuit(Session &s) { messageArea(s); }
// Rows that fit on one screen are printed with the reader prompt below them;
// longer ones open in the reader.
static void showRows(Session &s, std::vector<String> &rows, const String &title) {
  if (rows.size() <= 20) {
    for (auto &r : rows) say(s, r);
    s.state = MSG_VIEW;
    readerPrompt(s);
    return;
  }
  openRows(s, rows, title, messageKeys(s, true), msgReaderQuit, msgReaderKey);
}
static void showMessage(Session &s, uint32_t id) {
  Message m;
  if (!getMessage(id, m) || m.conference != s.conference || !canRead(s, m)) {
    say(s, "No such message in this area.");
    if (s.state == MSG_VIEW) readerPrompt(s); else messageArea(s);
    return;
  }
  String replies;
  eachMessage([&](const Message &r) { if (r.replyTo == m.id && canRead(s, r)) replies += " #" + String(msgNo(r.id)); return true; });
  std::vector<String> rows;
  uint32_t at = parseClock(m.when);
  rows.push_back("");
  rows.push_back("\x1b[1;36m" + confName(m.conference) + "\x1b[0m   \x1b[1;33mMsg #" + String(msgNo(m.id)) + "\x1b[0m   \x1b[1;37m" +
                 (at ? clockText(at, true) : m.when) + "\x1b[0m");
  if (m.history) rows.push_back("\x1b[0;35mFidoNet echo " + historyEcho(m.id) + "\x1b[0m");
  rows.push_back("From : " + m.from);
  rows.push_back("To   : " + (m.to.length() ? m.to : String("ALL")) + (m.priv ? "  \x1b[1;31m(private)\x1b[0m" : ""));
  rows.push_back("Subj : " + m.subject);
  if (m.editedAt.length())
    rows.push_back("\x1b[0;33m[edited " + m.editedAt + (up(m.editedBy) != up(m.from) ? " by " + m.editedBy : String()) + "]\x1b[0m");
  if (m.replyTo) {
    Message parent;
    bool gone = getMessage(m.replyTo, parent, true) && parent.deleted;
    rows.push_back("\x1b[0;36mReply to #" + String(msgNo(m.replyTo)) + (gone ? " (deleted)" : "") + "\x1b[0m");
  }
  rows.push_back("\x1b[0;34m------------------------------------------------------------------------------\x1b[0m");
  bodyRows(rows, m.body);
  if (replies.length()) rows.push_back("\x1b[0;36mReplies:" + replies + "  (T shows the thread)\x1b[0m");
  rows.push_back("");
  s.msgId = m.id;
  markRead(s, m.conference, m.id);
  activity(s, "READING MESSAGES");
  showRows(s, rows, "Msg #" + String(msgNo(m.id)));
}
// The whole conversation the current message belongs to, oldest first, like
// an e-mail chain.
static void showThread(Session &s) {
  std::vector<std::pair<uint32_t, uint32_t>> links;  // id -> replyTo, ascending id
  eachMessage([&](const Message &m) {  // killed messages keep their place in the chain
    if (m.conference == s.conference && canRead(s, m)) links.push_back({m.id, m.replyTo});
    return true;
  }, true);
  std::sort(links.begin(), links.end());  // history ids come first but are numbered higher
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
  std::vector<String> rows;
  rows.push_back("");
  rows.push_back("\x1b[1;36mTHREAD\x1b[0m");
  eachMessage([&](const Message &m) {
    if (m.conference != s.conference || !canRead(s, m) || rootOf(m.id) != root) return true;
    if (m.deleted) { rows.push_back("\x1b[0;37m-- #" + String(msgNo(m.id)) + "  (deleted)\x1b[0m"); rows.push_back(""); return ++shown < 40; }
    rows.push_back("\x1b[1;33m-- #" + String(msgNo(m.id)) + "  " + m.from + " -> " + (m.to.length() ? m.to : String("ALL")) + "  " + m.when +
                   (m.replyTo ? "  (re #" + String(msgNo(m.replyTo)) + ")" : String()) + (m.editedAt.length() ? "  [edited]" : "") + "\x1b[0m");
    if (!m.replyTo) rows.push_back("\x1b[1;37m" + m.subject + "\x1b[0m");
    bodyRows(rows, m.history ? historyBody(m.id) : m.body);
    rows.push_back("");
    return ++shown < 40;
  }, true);
  showRows(s, rows, "Thread of #" + String(msgNo(root)));
}
// Message keys in the reader: N and P step through the area (SPACE at the end
// goes on to the next message, like ENTER in the plain reader), R replies,
// T shows the thread.
// H: every stored version of the current message, oldest first.
static void showHistory(Session &s) {
  auto versions = messageVersions(s.msgId);
  std::vector<String> rows;
  rows.push_back("");
  rows.push_back("\x1b[1;36mHISTORY OF #" + String(msgNo(s.msgId)) + "\x1b[0m  " + String(versions.size()) + " versions");
  for (size_t i = 0; i < versions.size(); ++i) {
    const Message &v = versions[i];
    String who = i == 0 ? "written by " + v.from + "  " + v.when : (v.deleted ? "killed by " : "edited by ") + v.editedBy + "  " + v.editedAt;
    rows.push_back("\x1b[1;33m-- Version " + String(i + 1) + ": " + who + "\x1b[0m");
    if (!v.deleted) bodyRows(rows, v.body);
    rows.push_back("");
  }
  showRows(s, rows, "History of #" + String(msgNo(s.msgId)));
}
static void startReply(Session &s);
static void startKill(Session &s);
static void startEdit(Session &s);
static bool msgReaderKey(Session &s, const String &k) {
  // K, E and H only where the status bar offers them; otherwise k scrolls.
  Message cur;
  bool known = getMessage(s.msgId, cur), mine = known && canModify(s, cur), history = known && cur.versions > 1;
  if ((mine && (k == "k" || k == "K" || k == "e" || k == "E")) || (history && (k == "h" || k == "H"))) {
    s.reader = Reader();
    write(s, "\x1b[0m\x1b[2J\x1b[H");
    s.state = MSG_VIEW;
    if (k == "k" || k == "K") startKill(s); else if (k == "e" || k == "E") startEdit(s); else showHistory(s);
    return true;
  }
  bool next = k == "n" || k == "N" || ((k == " " || k == "\r") && readerAtEnd(s));
  if (next || k == "p" || k == "P") {
    uint32_t id = stepMessage(s, s.msgId, next);
    if (!id) { showPage(s, next ? "Last message in this area" : "First message in this area"); return true; }
    s.reader = Reader();
    write(s, "\x1b[0m\x1b[2J\x1b[H");
    s.state = MSG_VIEW;
    showMessage(s, id);
    return true;
  }
  if (k == "r" || k == "R" || k == "t" || k == "T") {
    s.reader = Reader();
    write(s, "\x1b[0m\x1b[2J\x1b[H");
    s.state = MSG_VIEW;
    if (k == "r" || k == "R") startReply(s); else showThread(s);
    return true;
  }
  return false;
}

// N: start reading at the first new message of the current area.
// The first unread post in the current area. History never counts as new.
static uint32_t firstNew(const Session &s) {
  uint64_t mark = msgOrder(lastReadIn(s, s.conference));
  uint32_t found = 0;
  eachMessage([&](const Message &m) {
    if (m.conference != s.conference || m.history || !canRead(s, m) || msgOrder(m.id) <= mark) return true;
    found = m.id;
    return false;
  });
  return found;
}
static void readNew(Session &s) {
  uint32_t id = firstNew(s);
  if (!id) { say(s, "\r\nNo new messages in " + confName(s.conference) + "."); messageArea(s); return; }
  showMessage(s, id);
}

// ---------- posting -------------------------------------------------------------
// The draft as lines, and back.
static std::vector<String> draftLineList(const Session &s) {
  std::vector<String> lines;
  if (!s.draftLines) return lines;
  int start = 0;
  while (start <= (int)s.draft.length()) {
    int nl = s.draft.indexOf('\n', start);
    if (nl < 0) nl = s.draft.length();
    lines.push_back(s.draft.substring(start, nl));
    start = nl + 1;
  }
  return lines;
}
static void setDraft(Session &s, const std::vector<String> &lines) {
  s.draft = "";
  for (size_t i = 0; i < lines.size(); ++i) { if (i) s.draft += '\n'; s.draft += lines[i]; }
  s.draftLines = lines.size();
}
static void editorPrompt(Session &s) {
  // The key list scrolls away in a long message: a short reminder every 15 lines.
  if (s.draftLines && s.draftLines % 15 == 0) say(s, "\x1b[0;36m/S save  /A abort  /L list  /? all editor keys\x1b[0m");
  char p[8]; snprintf(p, sizeof(p), "%2u: ", s.draftLines + 1);
  prompt(s, p);
}
static void listDraft(Session &s) {
  auto lines = draftLineList(s);
  for (size_t i = 0; i < lines.size(); ++i) { char n[8]; snprintf(n, sizeof(n), "%2u: ", (unsigned)(i + 1)); say(s, String("\x1b[0;36m") + n + "\x1b[0m" + lines[i]); }
  if (lines.empty()) say(s, "(empty)");
}
static void editorHelp(Session &s) {
  say(s, "\x1b[1;36mOne line at a time. /S saves, /A aborts, /L lists, /D n deletes line n,\x1b[0m");
  say(s, "\x1b[1;36m/R n text replaces line n, /C clears all, /? shows this. (" + String(DRAFT_LINES) + " lines max)\x1b[0m");
}
static void startEditor(Session &s) {
  s.draft = ""; s.draftLines = 0; s.msgEdit = 0;
  s.state = POST_BODY; activity(s, "WRITING A MESSAGE");
  editorHelp(s);
  editorPrompt(s);
}
// E: the line editor opens with the old text; new lines are added after it.
static void startEdit(Session &s) {
  Message m;
  if (!getMessage(s.msgId, m) || !canModify(s, m)) { say(s, "You can only edit your own messages."); readerPrompt(s); return; }
  s.state = POST_BODY; activity(s, "EDITING A MESSAGE");
  s.draft = m.body;
  s.draftLines = 1;  // draftLineList() reads the draft only when it has lines
  s.draftLines = m.body.length() ? draftLineList(s).size() : 0;
  s.msgEdit = m.id;
  say(s, "\r\n\x1b[1;33mEditing #" + String(msgNo(m.id)) + ": " + m.subject + "\x1b[0m");
  listDraft(s);
  editorHelp(s);
  editorPrompt(s);
}
// K: kill after a y/N question. The message stays in the file, marked deleted.
static void startKill(Session &s) {
  Message m;
  if (!getMessage(s.msgId, m) || !canModify(s, m)) { say(s, "You can only kill your own messages."); readerPrompt(s); return; }
  s.state = MSG_KILL;
  prompt(s, "\x1b[1;31mKill message #" + String(msgNo(m.id)) + " (" + m.subject + ")? [y/N]: \x1b[0m");
}
static void killMessage(Session &s, bool yes) {
  Message m;
  if (!yes || !getMessage(s.msgId, m) || !canModify(s, m)) { say(s, "Not killed."); s.state = MSG_VIEW; readerPrompt(s); return; }
  m.deleted = true; m.editedAt = nowText(); m.editedBy = s.user.handle;
  if (!appendRevision(m)) { say(s, "Not killed: the message base could not be written."); s.state = MSG_VIEW; readerPrompt(s); return; }
  event("MESSAGE_KILL", s.user.handle + " #" + m.id);
  say(s, "\x1b[1;32mMessage #" + String(msgNo(m.id)) + " killed.\x1b[0m");
  uint32_t next = stepMessage(s, m.id, true);
  if (next) showMessage(s, next); else messageArea(s);
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
  say(s, "\r\nReplying to #" + String(msgNo(m.id)) + " by " + m.from + ": " + s.pendingSubject);
  startEditor(s);
}
static void saveDraft(Session &s) {
  if (s.msgEdit) {  // an edit: the new text under the old id
    Message m;
    uint32_t id = s.msgEdit;
    s.msgEdit = 0;
    if (getMessage(id, m) && canModify(s, m)) {
      m.body = s.draft; m.editedAt = nowText(); m.editedBy = s.user.handle;
      if (appendRevision(m)) { event("MESSAGE_EDIT", s.user.handle + " #" + id); say(s, "\x1b[1;32mMessage #" + String(msgNo(id)) + " updated.\x1b[0m"); }
      else say(s, "Not saved: the message base could not be written.");
    } else say(s, "That message can no longer be edited.");
    s.draft = "";
    s.state = MSG_VIEW;
    showMessage(s, id);
    return;
  }
  Message m;
  m.conference = s.conference; m.from = s.user.handle; m.to = s.pendingTo;
  m.subject = s.pendingSubject.length() ? s.pendingSubject : String("No subject");
  m.body = s.draft; m.when = nowText(); m.replyTo = s.replyTo; m.priv = s.pendingPrivate && m.to.length();
  if (appendMessage(m)) {
    s.user.messages++;
    // Posting earns download credit (SYSTEM.INI credits_per_message).
    if (board.creditsPerMessage && s.user.credits < UINT32_MAX - board.creditsPerMessage) s.user.credits += board.creditsPerMessage;
    touchUser(s.user);
    event("MESSAGE", s.user.handle + " #" + m.id);
    say(s, "\x1b[1;32mMessage #" + String(msgNo(m.id)) + " posted.\x1b[0m" +
           (board.creditsPerMessage ? "  +" + sizeText(board.creditsPerMessage) + " download credit." : String()));
  } else say(s, "Message not posted: the message base could not be written.");
  s.draft = "";
  if (s.replyTo) { s.state = MSG_VIEW; readerPrompt(s); }
  else messageArea(s);
}
