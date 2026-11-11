#pragma once
// Message records in /BBS/DATA/MSGS/MESSAGES.DAT.
// Part of the single TinyBBS translation unit: included in order from main.cpp.

struct Message { uint32_t id = 0; uint8_t conference = 0; String from, to, subject, body, when; };
static bool parseMessage(const String &line, Message &m) {
  m.id = field(line, 0).toInt(); m.conference = field(line, 1).toInt(); m.from = dec(field(line, 2)); m.to = dec(field(line, 3)); m.subject = dec(field(line, 4)); m.when = dec(field(line, 5)); m.body = dec(field(line, 6)); return m.id > 0;
}
static String messageLine(const Message &m) { return String(m.id) + "|" + m.conference + "|" + enc(m.from) + "|" + enc(m.to) + "|" + enc(m.subject) + "|" + enc(m.when) + "|" + enc(m.body); }
static void messageCounters() { if (!sdReady) return; File f = SD_MMC.open("/BBS/DATA/MSGS/MESSAGES.DAT", FILE_READ); if (!f) return; uint32_t high = 0; while (f.available()) { Message m; if (parseMessage(f.readStringUntil('\n'), m)) high = max(high, m.id); } f.close(); nextMessageId = high + 1; }
static bool appendMessage(Message &m) { if (!sdReady) return false; m.id = nextMessageId++; File f = SD_MMC.open("/BBS/DATA/MSGS/MESSAGES.DAT", FILE_APPEND); if (!f) return false; bool ok = f.println(messageLine(m)); f.close(); return ok; }
