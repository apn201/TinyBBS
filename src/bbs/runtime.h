#pragma once
// Starting the board and keeping it running. Both builds call these: the
// ESP32 firmware from setup() and loop(), the server from its own main loop.
// Transports (web, Telnet) and hardware (LCD, WiFi) are started by the
// caller, outside the core.
// Part of the shared core: included last from src/bbs_core.h.

// Card, user and message counters, settings and message history. Call once,
// after the card is mounted (sdReady says whether it is).
static String bbsBegin() {
  bbsMutex.init();
  bootAt = millis();
  if (sdReady && prepareStorage()) { userCounters(); messageCounters(); }
  else { if (!storageFault.length()) storageFault = "storage setup failed"; sdReady = false; trace("SD_OFFLINE", storageFault); }
  return loadSettings();
}
// Paced output, the board clock and idle timeouts. Call often (every loop),
// holding the BBS lock. `transports` runs in between: it may open sessions,
// so the time is read again after it.
template <typename F> static void bbsTick(F transports) {
  uint32_t now = millis();
  for (auto &s : sessions) if (s.used) pace(s, now);
  transports();
  tickClock();
  now = millis();
  hotkeyTick(now);
  for (auto &s : sessions) {
    if (!s.used) continue;
    // P43: thirty seconds before an idle line closes it says so once, on its own line, with the
    // prompt and what was typed drawn again under it. Any key keeps the line (input()).
    uint32_t idleLimit = s.authenticated ? SIGNED_IN_TIMEOUT_MS : LOGIN_TIMEOUT_MS;
    uint32_t quiet = now - s.lastAt;
    if (quiet + IDLE_WARN_MS < idleLimit) s.idleWarned = false;
    else if (!s.idleWarned && s.state != GOODBYE && !streaming(s)) {
      s.idleWarned = true;
      timedLine(s, "\x1b[0;33mStill there? The line closes in 30 seconds. Press any key to stay.\x1b[0m");
    }
    if (quiet >= idleLimit || (s.web && now - s.lastPoll >= WEB_CARRIER_TIMEOUT_MS))
      closeSession(s);
  }
}
