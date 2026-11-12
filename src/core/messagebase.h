#pragma once
// Message records in /BBS/DATA/MSGS/MESSAGES.DAT, one per line, oldest first:
// id|conference|from|to|subject|when|body|replyTo|private|deleted|editedAt|editedBy
// Fields after `body` were added later; older lines without them read as 0 or
// empty. The file is append-only: an edit or a kill is a new line with the
// same id, and the last line for an id is the message as it is now. The
// earlier lines are its change history.
// Part of the single TinyBBS translation unit: included in order from main.cpp.

static constexpr const char *MSG_FILE = "/BBS/DATA/MSGS/MESSAGES.DAT";

struct Message {
  uint32_t id = 0; uint8_t conference = 0;
  String from, to, subject, body, when;
  uint32_t replyTo = 0;  // the message this one answers, 0 = starts a thread
  bool priv = false;     // only sender, recipient and SysOp may read it
  bool deleted = false;  // killed by its author or the SysOp
  String editedAt, editedBy;  // last edit or kill
  uint8_t versions = 1;  // lines in the file for this id
  bool history = false;  // from /BBS/MSGBASE on the card: read-only (core/history.h)
};
static bool parseMessage(const String &raw, Message &m) {
  // println() ends each line with CRLF: the CR would stick to the last field.
  String line = raw;
  while (line.endsWith("\r")) line.remove(line.length() - 1);
  m.id = field(line, 0).toInt(); m.conference = field(line, 1).toInt();
  m.from = dec(field(line, 2)); m.to = dec(field(line, 3)); m.subject = dec(field(line, 4));
  m.when = dec(field(line, 5)); m.body = dec(field(line, 6));
  m.replyTo = strtoul(field(line, 7).c_str(), nullptr, 10);
  m.priv = field(line, 8) == "1";
  m.deleted = field(line, 9) == "1";
  m.editedAt = dec(field(line, 10)); m.editedBy = dec(field(line, 11));
  return m.id > 0;
}
static String messageLine(const Message &m) {
  return String(m.id) + "|" + m.conference + "|" + enc(m.from) + "|" + enc(m.to) + "|" + enc(m.subject) + "|" +
         enc(m.when) + "|" + enc(m.body) + "|" + m.replyTo + "|" + (m.priv ? "1" : "0") + "|" + (m.deleted ? "1" : "0") +
         "|" + enc(m.editedAt) + "|" + enc(m.editedBy);
}
static void messageCounters() { if (!sdReady) return; File f = SD_MMC.open(MSG_FILE, FILE_READ); if (!f) return; uint32_t high = 0; while (f.available()) { Message m; if (parseMessage(f.readStringUntil('\n'), m)) high = max(high, m.id); } f.close(); nextMessageId = high + 1; }
static bool appendMessage(Message &m) { if (!sdReady) return false; m.id = nextMessageId++; File f = SD_MMC.open(MSG_FILE, FILE_APPEND); if (!f) return false; bool ok = f.println(messageLine(m)); f.close(); return ok; }
// An edit or a kill: the message's new state under its old id.
static bool appendRevision(const Message &m) { if (!sdReady || !m.id) return false; File f = SD_MMC.open(MSG_FILE, FILE_APPEND); if (!f) return false; bool ok = f.println(messageLine(m)); f.close(); return ok; }
