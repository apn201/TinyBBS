#pragma once
// Telnet listener and IAC filtering.
// Part of the single TinyBBS translation unit: included in order from main.cpp.

static void telnetInput(Session &s, uint8_t byte) {
  // Drop negotiation and subnegotiation before handing bytes to the BBS.
  switch (s.telnetState) {
    case 0:
      if (byte == 255) s.telnetState = 255;
      else input(s, String((char)byte));
      break;
    case 255:
      s.telnetState = (byte >= 250 && byte <= 254) ? byte : 0;
      break;
    case 250:
      if (byte == 255) s.telnetState = 240;
      break;
    case 240:
      s.telnetState = byte == 240 ? 0 : 250;
      break;
    default:
      if (s.telnetState == 251 || (s.telnetState == 253 && byte != 1 && byte != 3)) {
        const uint8_t response[] = {255, uint8_t(s.telnetState == 251 ? 254 : 252), byte};
        s.socket.write(response, sizeof(response));
      }
      s.telnetState = 0;
      break;
  }
}
static void tickTelnet() {
  WiFiClient incoming = telnet.accept();
  if (incoming) {
    if (allocate(false, incoming)) {
      const uint8_t options[] = {255, 251, 1, 255, 251, 3}; // WILL ECHO, WILL SGA
      incoming.write(options, sizeof(options));
    } else { incoming.print("All BBS nodes are busy.\r\n"); incoming.stop(); }
  }
  for (auto &s : sessions) if (s.used && !s.web) {
    if (!s.socket.connected()) { closeSession(s); continue; }
    for (size_t budget = 256; budget && s.used && s.socket.available() && s.state != GOODBYE; --budget)
      telnetInput(s, s.socket.read());
    if (s.output.length() && (s.output.length() >= 512 || millis() - s.sentAt >= 40)) { s.socket.print(s.output); s.output = ""; s.sentAt = millis(); }
    if (s.state == GOODBYE && outputDone(s)) closeSession(s);
  }
}
