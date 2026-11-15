#pragma once
// The notebook screens (ported from Last Caller Q21): K on the main menu, only
// on a card with a notes table (core/notes.h). The notes the caller holds, in
// the order seen, six to a page; one note with where it came from and where to
// read it again. It keeps what was shown and never adds an answer.
//   list   keys: 1-6 open, N next page, P previous page, Q back, X main, ? keys
//   note   keys: N next, P previous, Q back to the list, X main, ? keys
// Part of the shared core: included in order from src/bbs_core.h, after bbs/messages.h.

static constexpr uint8_t NB_PAGE = 6;

static bool notebookListed() { return !notesTable.empty(); }
static std::vector<const Note *> notebookHeld(const Session &s) {
  std::vector<const Note *> out;
  int start = 0;
  while (start < (int)s.notes.length()) {
    int comma = s.notes.indexOf(',', start); if (comma < 0) comma = s.notes.length();
    if (const Note *n = findNote(s.notes.substring(start, comma))) out.push_back(n);
    start = comma + 1;
  }
  return out;
}
// Where a message is now: its area and number. Other notes name the place on the card (where=).
static String notebookWhere(const Session &s, const Note &n) {
  if (n.kind != NK_READ) return n.where;
  String want = n.match;
  bool bySubject = want.startsWith("~");
  if (bySubject) want = want.substring(1);
  uint32_t number = bySubject ? 0 : (uint32_t)want.toInt();
  String found;
  eachMessage([&](const Message &m) {
    if (!canRead(s, m) || (n.area >= 0 && m.conference != n.area)) return true;
    bool hit = bySubject ? up(m.subject.substring(0, 39)).startsWith(want) : (number && msgNo(m.id) == number);
    if (!hit) return true;
    found = confName(m.conference) + ", message " + String(msgNo(m.id));
    return false;
  });
  return found.length() ? found : n.where;
}
// A note's text, wrapped on word breaks at 68 columns.
static void notebookRows(Session &s, const String &text, const char *color, const char *indent) {
  int start = 0;
  while (start < (int)text.length()) {
    int end = min((int)text.length(), start + 68);
    if (end < (int)text.length()) { int sp = end; while (sp > start && text[sp] != ' ') sp--; if (sp > start) end = sp; }
    String row = text.substring(start, end); row.trim();
    say(s, String(color) + indent + row + "\x1b[0m");
    start = end;
    while (start < (int)text.length() && text[start] == ' ') start++;
  }
}
static void notebookList(Session &s) {
  s.state = NOTEBOOK; s.nbNote = -1; activity(s, "NOTEBOOK");
  auto held = notebookHeld(s);
  size_t pages = held.empty() ? 1 : (held.size() + NB_PAGE - 1) / NB_PAGE;
  if (s.nbPage >= pages) s.nbPage = (uint8_t)(pages - 1);
  size_t first = (size_t)s.nbPage * NB_PAGE, last = std::min(held.size(), first + NB_PAGE);
  say(s, "\r\n\x1b[1;36m  NOTEBOOK\x1b[0m" + (held.empty() ? String() : "  \x1b[0;36min the order you saw them, " + String((unsigned)held.size()) + " kept" +
         (pages > 1 ? ", page " + String((unsigned)s.nbPage + 1) + " of " + String((unsigned)pages) : String()) + "\x1b[0m"));
  if (held.empty()) say(s, "  \x1b[0;36mNothing written down yet. The board writes down what it shows you.\x1b[0m");
  for (size_t i = first; i < last; ++i) {
    say(s, "  \x1b[1;33m[" + String((unsigned)(i - first + 1)) + "]\x1b[0m \x1b[0;36m" + held[i]->src + "\x1b[0m");
    notebookRows(s, held[i]->text, "\x1b[0;37m", "      ");
  }
  String keys = "  ";
  if (last > first) keys += "\x1b[1;33m[1-" + String((unsigned)(last - first)) + "]\x1b[0m Open   ";
  if (s.nbPage + 1 < pages) keys += "\x1b[1;33m[N]\x1b[0m Next page   ";
  if (s.nbPage > 0) keys += "\x1b[1;33m[P]\x1b[0m Previous page   ";
  keys += "\x1b[1;33m[Q]\x1b[0m Back";
  say(s, keys);
  prompt(s, "Notebook command? ");
}
static void notebookNote(Session &s) {
  auto held = notebookHeld(s);
  if (s.nbNote < 0 || (size_t)s.nbNote >= held.size()) { notebookList(s); return; }
  s.state = NOTEBOOK;
  const Note &n = *held[s.nbNote];
  say(s, "\r\n\x1b[1;36m  NOTEBOOK\x1b[0m  \x1b[0;36m" + String(s.nbNote + 1) + " of " + String((unsigned)held.size()) + "\x1b[0m");
  say(s, "  \x1b[1;33m" + n.src + "\x1b[0m");
  notebookRows(s, n.text, "\x1b[1;37m", "  ");
  String where = notebookWhere(s, n);
  if (where.length()) say(s, "  \x1b[0;36mTo read it again: " + where + "\x1b[0m");
  String keys = "  ";
  if ((size_t)s.nbNote + 1 < held.size()) keys += "\x1b[1;33m[N]\x1b[0m Next   ";
  if (s.nbNote > 0) keys += "\x1b[1;33m[P]\x1b[0m Previous   ";
  keys += "\x1b[1;33m[Q]\x1b[0m Back";
  say(s, keys);
  prompt(s, "Notebook command? ");
}
static void notebookOpen(Session &s) { s.nbPage = 0; notebookList(s); }
static void notebookBack(Session &s) { if (s.nbNote >= 0) notebookNote(s); else notebookList(s); }
static void notebookCommand(Session &s, const String &cmd) {
  if (cmd == "X") { mainMenu(s); return; }
  if (cmd == "?") { keysHelp(s); return; }
  auto held = notebookHeld(s);
  if (s.nbNote >= 0) {   // one note
    if (cmd == "Q") { notebookList(s); return; }
    if (cmd == "N" && (size_t)s.nbNote + 1 < held.size()) { s.nbNote++; notebookNote(s); return; }
    if (cmd == "P" && s.nbNote > 0) { s.nbNote--; notebookNote(s); return; }
    if (!cmd.length()) { notebookNote(s); return; }
    say(s, "Keys: N, P, Q back. ? shows the keys."); prompt(s, "Notebook command? ");
    return;
  }
  if (cmd == "Q") { mainMenu(s); return; }
  if (!cmd.length()) { notebookList(s); return; }
  if (cmd == "N" && (size_t)(s.nbPage + 1) * NB_PAGE < held.size()) { s.nbPage++; notebookList(s); return; }
  if (cmd == "P" && s.nbPage > 0) { s.nbPage--; notebookList(s); return; }
  long n = cmd.toInt();
  size_t at = (size_t)s.nbPage * NB_PAGE + (size_t)n - 1;
  if (n >= 1 && n <= NB_PAGE && String(n) == cmd && at < held.size()) { s.nbNote = (int16_t)at; notebookNote(s); return; }
  say(s, "Keys: a number to open, N, P, Q back."); prompt(s, "Notebook command? ");
}
