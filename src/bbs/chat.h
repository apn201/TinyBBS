#pragma once
// Node chat. Lines scroll in rows 1-23; row 24 is a bar with who is here and
// the commands, kept in place with a scroll region (ESC[1;23r). Every line is
// saved to /BBS/LOGS/CHAT.LOG; the SysOp sees the latest lines on entering
// and the longer history with /H.
// Part of the shared core: included in order from src/bbs_core.h.

static constexpr const char *CHAT_LOG = "/BBS/LOGS/CHAT.LOG";
static constexpr const char *CHAT_OLD = "/BBS/LOGS/CHAT.OLD";
static constexpr uint32_t CHAT_LOG_MAX = 128 * 1024;  // then CHAT.OLD, a fresh log
static constexpr uint8_t CHAT_SYSOP_BACKLOG = 20, CHAT_HISTORY = 200;

static String chatHere() {
  String names;
  for (auto &n : sessions) if (n.used && n.state == CHAT) { if (names.length()) names += ", "; names += n.user.handle; }
  return names;
}
static void chatBar(Session &s) {
  if (!s.ansi) return;
  String bar = " NODE CHAT  Here: " + chatHere();
  String keys = String("  /Q quit  /W who") + (isSysop(s) ? "  /H history" : "") + " ";
  if (bar.length() + keys.length() > 80) bar = bar.substring(0, 77 - keys.length()) + "...";
  while (bar.length() + keys.length() < 80) bar += ' ';
  // Save the cursor, draw the bar on row 24, put the cursor back.
  write(s, "\x1b[s\x1b[24;1H\x1b[0;30;46m" + bar + keys + "\x1b[0m\x1b[u");
}
static void chatBars() { for (auto &n : sessions) if (n.used && n.state == CHAT) chatBar(n); }

static void chatLog(const String &who, const String &text) {
  if (!sdReady) return;
  File f = SD_MMC.open(CHAT_LOG, FILE_APPEND);
  if (!f) return;
  bool full = f.size() > CHAT_LOG_MAX;
  f.print(nowText() + "|" + enc(who) + "|" + enc(text) + "\n");
  f.close();
  if (full) { SD_MMC.remove(CHAT_OLD); SD_MMC.rename(CHAT_LOG, CHAT_OLD); }
}
// The last `count` lines of the log, oldest first, as screen rows.
static std::vector<String> chatHistory(size_t count) {
  std::vector<String> rows;
  File f = sdReady ? SD_MMC.open(CHAT_LOG, FILE_READ) : File();
  while (f && f.available()) {
    String line = f.readStringUntil('\n');
    if (!line.length()) continue;
    String when = field(line, 0);
    rows.push_back("\x1b[0;37m" + when.substring(5) + "\x1b[0m \x1b[0;36m[" + dec(field(line, 1)) + "]\x1b[0m " + dec(field(line, 2)));
    if (rows.size() > count) rows.erase(rows.begin());
  }
  if (f) f.close();
  return rows;
}

// To everyone in chat but the sender, who already sees the line (it was
// echoed as they typed): their prompt line is cleared, the message printed,
// and the prompt drawn again with whatever they had half typed.
static void broadcastChat(Session &from, const String &line) {
  for (auto &s : sessions) {
    if (!s.used || s.state != CHAT || &s == &from) continue;
    write(s, "\r\x1b[2K");
    say(s, "\x1b[0;36m[" + from.user.handle + "]\x1b[0m " + line);
    prompt(s, "> ");
    if (s.input.length() && !s.hideInput) write(s, s.input);
  }
}
// The chat screen: cleared, the scroll region set, the bar drawn.
static void chatScreen(Session &s) {
  if (s.ansi) write(s, "\x1b[0m\x1b[2J\x1b[1;23r\x1b[23;1H");
}
static void chatPrompt(Session &s) { chatBar(s); prompt(s, "> "); }
static void startChat(Session &s, const String &banner) {
  s.hideInput = false;
  s.state = CHAT; activity(s, "CHAT");
  chatScreen(s);
  if (isSysop(s)) {
    auto rows = chatHistory(CHAT_SYSOP_BACKLOG);
    if (!rows.empty()) { say(s, "\x1b[0;37m-- earlier --\x1b[0m"); for (auto &r : rows) say(s, r); say(s, "\x1b[0;37m-------------\x1b[0m"); }
  }
  say(s, banner);
  broadcastChat(s, "joined the chat.");
  chatBars();
  prompt(s, "> ");
}
// Leaving chat, or losing carrier in it: the others are told and their bars
// updated. The caller's own screen gets the whole screen back.
static void leaveChat(Session &s, const char *why) {
  broadcastChat(s, why);
  s.state = MAIN;
  if (s.ansi) write(s, "\x1b[r\x1b[0m\x1b[2J\x1b[H");
  chatBars();
}
static void chatResume(Session &s) { s.state = CHAT; chatScreen(s); chatBars(); prompt(s, "> "); }
static void chatCommand(Session &s, const String &line, const String &cmd) {
  if (cmd == "/Q") { leaveChat(s, "left the chat."); mainMenu(s); return; }
  if (cmd == "/W") { say(s, "\x1b[0;37mIn chat: " + chatHere() + "\x1b[0m"); chatPrompt(s); return; }
  if (cmd == "/H" && isSysop(s)) {
    std::vector<String> rows = chatHistory(CHAT_HISTORY);
    if (rows.empty()) { say(s, "No chat saved yet."); chatPrompt(s); return; }
    if (s.ansi) write(s, "\x1b[r");  // the reader needs the whole screen
    broadcastChat(s, "is reading the chat history.");
    openRows(s, rows, "Chat history", "SPACE page  B back  \x18\x19 line  Q back to chat", chatResume, nullptr, true);
    chatBars();
    return;
  }
  if (!line.length()) { prompt(s, "> "); return; }
  chatLog(s.user.handle, line);
  broadcastChat(s, line);
  prompt(s, "> ");
}
