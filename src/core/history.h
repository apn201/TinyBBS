#pragma once
// Message history from the card: /BBS/MSGBASE/<conference id>/*.TXT, written
// by tools/fidonet-seed.py (or by hand). Read-only; shown in its conference
// before the messages callers post. Indexed at boot and on RELOAD: headers go
// into PSRAM, bodies are read from the card when a message is opened.
//
//   #MSG
//   From: name
//   To: name
//   Subject: text
//   Date: 1998-03-13 03:31
//   Reply: 12        (optional: the message in the same file it answers)
//   <blank line>
//   body ... up to the next #MSG line
//
// Lines starting with ; before the first #MSG are comments.
// Part of the single TinyBBS translation unit: included in order from main.cpp.

static constexpr const char *HISTORY_ROOT = "/BBS/MSGBASE";
// History message ids start here, far above any id a caller's post gets.
static constexpr uint32_t HISTORY_BASE = 3000000000UL;

struct HistoryMsg {
  uint32_t bodyAt, bodyLen;  // in its file
  uint32_t text;             // offset of "from\0to\0subject\0when\0" in historyText
  uint32_t replyTo;          // history id, 0 = none
  uint16_t file;
  uint8_t conference;
};
static HistoryMsg *historyMsgs = nullptr;
static uint32_t historyCount = 0, historyCap = 0;
static char *historyText = nullptr;
static uint32_t historyTextLen = 0, historyTextCap = 0;
static std::vector<String> historyFiles;  // path of each file
static std::vector<String> historyEchoes; // echo name of each file (its name without .TXT)

static bool isHistory(uint32_t id) { return id >= HISTORY_BASE; }
// Sort key: history first (in file order), then callers' posts by id.
static uint64_t msgOrder(uint32_t id) {
  if (!id) return 0;
  return isHistory(id) ? 1 + (uint64_t)(id - HISTORY_BASE) : (1ULL << 32) + id;
}
// Number a caller sees: history 1..n, then posts after it.
static uint32_t msgNo(uint32_t id) { return isHistory(id) ? id - HISTORY_BASE + 1 : id ? historyCount + id : 0; }
static uint32_t msgFromNo(uint32_t no) {
  if (!no) return 0;
  return no <= historyCount ? HISTORY_BASE + no - 1 : no - historyCount;
}

static bool historyGrowText(uint32_t need) {
  if (historyTextLen + need <= historyTextCap) return true;
  uint32_t cap = max<uint32_t>(historyTextCap * 2, 65536);
  while (cap < historyTextLen + need) cap *= 2;
  char *p = (char *)ps_realloc(historyText, cap);
  if (!p) return false;
  historyText = p; historyTextCap = cap;
  return true;
}
static bool historyAdd(const HistoryMsg &m, const String &from, const String &to, const String &subject, const String &when) {
  if (historyCount == historyCap) {
    uint32_t cap = historyCap ? historyCap * 2 : 512;
    HistoryMsg *p = (HistoryMsg *)ps_realloc(historyMsgs, cap * sizeof(HistoryMsg));
    if (!p) return false;
    historyMsgs = p; historyCap = cap;
  }
  uint32_t need = from.length() + to.length() + subject.length() + when.length() + 4;
  if (!historyGrowText(need)) return false;
  HistoryMsg h = m;
  h.text = historyTextLen;
  for (const String *s : {&from, &to, &subject, &when}) {
    memcpy(historyText + historyTextLen, s->c_str(), s->length() + 1);
    historyTextLen += s->length() + 1;
  }
  historyMsgs[historyCount++] = h;
  return true;
}
// The four header strings of a history message.
static void historyHeader(const HistoryMsg &h, String &from, String &to, String &subject, String &when) {
  const char *p = historyText + h.text;
  from = p; p += strlen(p) + 1;
  to = p; p += strlen(p) + 1;
  subject = p; p += strlen(p) + 1;
  when = p;
}

// One file: scanned in 2 KB blocks, a line at a time.
static void indexHistoryFile(const String &path, uint8_t conference, uint16_t fileNo) {
  File f = SD_MMC.open(path, FILE_READ);
  if (!f) return;
  const uint32_t first = historyCount;
  static uint8_t buf[2048];  // static: RELOAD can run on the web server's small stack
  size_t len = 0, pos = 0;
  uint32_t base = 0;     // file offset of buf[0]
  String line;
  uint32_t lineAt = 0;
  bool inHead = false, have = false;
  HistoryMsg cur{};
  String from, to, subject, when;
  uint32_t reply = 0;
  auto finish = [&](uint32_t endAt) {
    if (!have) return;
    cur.bodyLen = endAt > cur.bodyAt ? endAt - cur.bodyAt : 0;
    cur.replyTo = reply && reply <= historyCount - first + 1 ? HISTORY_BASE + first + reply - 1 : 0;
    cur.file = fileNo; cur.conference = conference;
    historyAdd(cur, from.length() ? from : String("Unknown"), to.length() ? to : String("All"),
               subject.length() ? subject : String("(no subject)"), when);
    have = false;
  };
  while (true) {
    if (pos == len) {
      base += len;
      len = f.read(buf, sizeof(buf));
      pos = 0;
      if (!len) break;
    }
    uint8_t c = buf[pos++];
    if (c != '\n') { if (c != '\r' && line.length() < 200) line += (char)c; continue; }
    uint32_t nextAt = base + pos;  // start of the following line
    if (line.startsWith("#MSG")) {
      finish(lineAt);
      have = true; inHead = true;
      from = to = subject = when = ""; reply = 0;
      cur = HistoryMsg{};
    } else if (have && inHead) {
      if (!line.length()) { inHead = false; cur.bodyAt = nextAt; }
      else if (line.startsWith("From:")) { from = line.substring(5); from.trim(); from = clean(from, 30); }
      else if (line.startsWith("To:")) { to = line.substring(3); to.trim(); to = clean(to, 30); }
      else if (line.startsWith("Subject:")) { subject = line.substring(8); subject.trim(); subject = subject.substring(0, 60); }
      else if (line.startsWith("Date:")) { when = line.substring(5); when.trim(); when = when.substring(0, 16); }
      else if (line.startsWith("Reply:")) reply = strtoul(line.substring(6).c_str(), nullptr, 10);
    }
    line = "";
    lineAt = nextAt;
  }
  // The last line may have no newline.
  if (line.startsWith("#MSG")) finish(lineAt);
  else finish(base + pos);
  f.close();
}
// Called from loadSettings(): at boot and on RELOAD.
static String loadHistory() {
  historyCount = 0; historyTextLen = 0;
  historyFiles.clear(); historyEchoes.clear();
  if (!sdReady || !psramFound()) return "no message history";
  File root = SD_MMC.open(HISTORY_ROOT);
  if (!root || !root.isDirectory()) return "no message history";
  std::vector<std::pair<long, String>> dirs;
  File e;
  while ((e = root.openNextFile())) {
    String name = e.name();
    bool dir = e.isDirectory();
    e.close();
    long id = name.toInt();
    if (dir && id >= 0 && id <= 255 && (id > 0 || name == "0")) dirs.push_back({id, name});
  }
  root.close();
  std::sort(dirs.begin(), dirs.end());
  for (const auto &d : dirs) {
    String dir = String(HISTORY_ROOT) + "/" + d.second;
    std::vector<String> files;
    File folder = SD_MMC.open(dir);
    while (folder && (e = folder.openNextFile())) {
      String name = e.name();
      bool file = !e.isDirectory();
      e.close();
      if (file && up(name).endsWith(".TXT")) files.push_back(name);
    }
    if (folder) folder.close();
    std::sort(files.begin(), files.end(), [](const String &a, const String &b) { return up(a) < up(b); });
    for (const auto &name : files) {
      if (historyFiles.size() >= 1000) break;
      historyFiles.push_back(dir + "/" + name);
      historyEchoes.push_back(up(name.substring(0, name.length() - 4)));
      indexHistoryFile(dir + "/" + name, (uint8_t)d.first, historyFiles.size() - 1);
    }
  }
  return String(historyCount) + " history messages";
}
// A history message's body, read from the card.
static String historyBody(uint32_t id) {
  if (!isHistory(id) || id - HISTORY_BASE >= historyCount) return "";
  const HistoryMsg &h = historyMsgs[id - HISTORY_BASE];
  uint32_t len = min<uint32_t>(h.bodyLen, 16000);
  File f = SD_MMC.open(historyFiles[h.file], FILE_READ);
  if (!f || !f.seek(h.bodyAt)) return "";
  std::unique_ptr<uint8_t[]> buf(new (std::nothrow) uint8_t[len + 1]);
  if (!buf) { f.close(); return ""; }
  len = f.read(buf.get(), len);
  f.close();
  String out;
  out.reserve(len);
  for (uint32_t i = 0; i < len; ++i) {
    uint8_t c = buf[i];
    if (c == '\r') continue;
    if (c >= 0x80) appendDosGlyph(out, c);  // the files are DOS text
    else if (c == '\n' || c == '\t' || c >= 0x20) out += (char)c;
  }
  while (out.endsWith("\n")) out.remove(out.length() - 1);
  return out;
}
static String historyEcho(uint32_t id) {
  if (!isHistory(id) || id - HISTORY_BASE >= historyCount) return "";
  return historyEchoes[historyMsgs[id - HISTORY_BASE].file];
}
