#pragma once
// The notebook's engine (ported from Last Caller Q21, docs/INTERACTION.md). A
// card may have /BBS/CONFIG/NOTES.INI: one [SECTION] per note, a short line the
// board writes down for a caller the moment it shows them the thing it is about
// (a message, a file, a picture, a bulletin, a door, a screen). Without the file
// there is no notebook: nothing is collected and no menu shows it.
//
//   [RULES]                 the note's id: letters, digits and _, 14 at most
//   on=screen               login, read, file, art, door or screen
//   match=BULLETIN BOARD RULES
//                           read: a message number, or ~ and the start of its subject
//                           file, art: the file name; door: the door's menu key
//                           screen: WHO, USERS, or BULLETIN and the bulletin's title
//                           login: (none, every login)
//   area=0                  read only: the message area (optional)
//   has=FLAG lacks=FLAG     the caller's access flags, or notes (by id) held already (optional)
//   src=Board rules         where it came from, as the notebook shows it
//   text=No flaming...      the line kept (110 characters at most)
//   where=Bulletins         where to read it again (optional; messages are looked up)
//
// What a caller holds is kept in /BBS/DATA/STATE/NOTES_<user id>.DAT, the ids in
// the order they were seen. Showing a thing twice writes its note once.
// Part of the shared core: included in order from src/bbs_core.h.

enum NoteKind : uint8_t { NK_NONE, NK_LOGIN, NK_READ, NK_FILE, NK_ART, NK_DOOR, NK_SCREEN };
struct Note {
  String id, match, has, lacks, src, text, where;
  NoteKind kind = NK_NONE;
  int16_t area = -1;
};
static constexpr const char *NOTES_INI = "/BBS/CONFIG/NOTES.INI";
static constexpr size_t MAX_NOTES = 200, NOTE_ID_MAX = 14, NOTE_TEXT_MAX = 110, NOTE_SRC_MAX = 30, NOTE_WHERE_MAX = 40;
static std::vector<Note> notesTable;

static NoteKind noteKind(String v) {
  v.trim(); v.toLowerCase();
  if (v == "login") return NK_LOGIN;
  if (v == "read") return NK_READ;
  if (v == "file") return NK_FILE;
  if (v == "art") return NK_ART;
  if (v == "door") return NK_DOOR;
  if (v == "screen") return NK_SCREEN;
  return NK_NONE;
}
static bool noteIdOk(const String &id) {
  if (!id.length() || id.length() > NOTE_ID_MAX) return false;
  for (size_t i = 0; i < id.length(); ++i) if (!isalnum((unsigned char)id[i]) && id[i] != '_') return false;
  return true;
}
// Comma lists of flags or note ids, any case.
static bool noteListHas(const String &list, const String &word) { return word.length() && ("," + up(list) + ",").indexOf("," + up(word) + ",") >= 0; }
static bool noteListAll(const String &list, const String &want) {
  int start = 0;
  while (start <= (int)want.length()) {
    int comma = want.indexOf(',', start); if (comma < 0) comma = want.length();
    String w = want.substring(start, comma); w.trim();
    if (w.length() && !noteListHas(list, w)) return false;
    start = comma + 1;
  }
  return true;
}
static bool noteListAny(const String &list, const String &want) {
  int start = 0;
  while (start <= (int)want.length()) {
    int comma = want.indexOf(',', start); if (comma < 0) comma = want.length();
    String w = want.substring(start, comma); w.trim();
    if (w.length() && noteListHas(list, w)) return true;
    start = comma + 1;
  }
  return false;
}
static const Note *findNote(const String &id) {
  for (const auto &n : notesTable) if (n.id == id) return &n;
  return nullptr;
}

// From loadSettings(), at boot and on RELOAD. A section that does not make a
// note is skipped and named in the event log; the rest still load.
static String loadNotes() {
  notesTable.clear();
  Ini ini;
  if (!iniLoad(NOTES_INI, ini)) return "no notebook";
  unsigned skipped = 0;
  for (const String &sec : iniSections(ini)) {
    Note n;
    n.id = up(sec);
    n.kind = noteKind(iniGet(ini, sec, "on"));
    n.match = up(iniGet(ini, sec, "match"));
    n.has = iniGet(ini, sec, "has"); n.lacks = iniGet(ini, sec, "lacks");
    n.src = iniGet(ini, sec, "src").substring(0, NOTE_SRC_MAX);
    n.text = iniGet(ini, sec, "text").substring(0, NOTE_TEXT_MAX);
    n.where = iniGet(ini, sec, "where").substring(0, NOTE_WHERE_MAX);
    String area = iniGet(ini, sec, "area");
    uint32_t a = 0;
    String why;
    if (!noteIdOk(n.id)) why = "bad note name";
    else if (findNote(n.id)) why = "duplicate note name";
    else if (n.kind == NK_NONE) why = "missing or unknown on=";
    else if (!n.text.length()) why = "no text";
    else if (!n.src.length()) why = "no src";
    else if (!n.match.length() && n.kind != NK_LOGIN) why = "needs match=";
    else if (area.length() && (n.kind != NK_READ || !readNumber(area, a) || a > 255)) why = "bad area";
    else if (notesTable.size() >= MAX_NOTES) why = "too many notes";
    if (why.length()) { skipped++; event("NOTE_SKIP", sec + ": " + why); continue; }
    if (area.length()) n.area = a;
    notesTable.push_back(n);
  }
  String out = String((unsigned)notesTable.size()) + " notes";
  if (skipped) out += " (" + String(skipped) + " skipped, see the event log)";
  return out;
}

static String notesPath(uint32_t id) { return String("/BBS/DATA/STATE/NOTES_") + id + ".DAT"; }
// At login: the notes this caller holds.
static void loadUserNotes(Session &s) {
  s.notes = "";
  if (notesTable.empty() || !sdReady || !s.user.id) return;
  File f = SD_MMC.open(notesPath(s.user.id).c_str(), FILE_READ);
  if (!f) return;
  s.notes = f.readStringUntil('\n'); s.notes.trim();
  f.close();
}
// Something was shown to this caller. The notes it carries are written down,
// in table order, and never again. A failed save is logged; the show goes on.
static void notesOn(Session &s, NoteKind kind, const String &match) {
  if (notesTable.empty() || kind == NK_NONE || !s.authenticated || !s.user.id) return;
  String m = up(match); m.trim();
  String gate = s.user.flags + "," + s.notes, add;
  for (const auto &n : notesTable) {
    if (n.kind != kind || (n.match.length() && n.match != m)) continue;
    if (n.area >= 0 && n.area != s.conference) continue;
    if (n.has.length() && !noteListAll(gate, n.has)) continue;
    if (n.lacks.length() && noteListAny(gate, n.lacks)) continue;
    if (noteListHas(s.notes, n.id) || noteListHas(add, n.id)) continue;
    if (add.length()) add += ',';
    add += n.id;
  }
  if (!add.length()) return;
  s.notes += (s.notes.length() ? "," : "") + add;
  File f = sdReady ? SD_MMC.open(notesPath(s.user.id).c_str(), FILE_WRITE) : File();
  if (!f) { event("NOTE_FAIL", s.user.handle + ": notes not saved: " + add); return; }
  f.print(s.notes + "\n");
  f.close();
  event("NOTE", add + " " + s.user.handle + " node " + s.node);
}
