#pragma once
// The invented board clock: started from epoch= in SYSTEM.INI, saved to
// CLOCK.DAT so it carries on after a power cut, and optionally pulled forward
// to a browser caller's evening (clock_sync=browser).
// Part of the single TinyBBS translation unit: included in order from main.cpp.

static constexpr const char *CLOCK_FILE = "/BBS/DATA/STATE/CLOCK.DAT";
static String clockEpoch;          // epoch= text the running clock was started from
static bool clockSyncBrowser = false;
static uint32_t clockSavedAt = 0;  // millis() of the last save

static void saveClock() {
  clockSavedAt = millis();
  if (!sdReady) return;
  File f = SD_MMC.open(CLOCK_FILE, FILE_WRITE);
  if (!f) return;
  f.print(String(boardNow()) + "|" + clockEpoch + "\n");
  f.close();
}
// Called from loadSettings() at boot and on RELOAD. The clock continues from
// CLOCK.DAT. A changed epoch= restarts it there: that is how the SysOp resets it.
static String startClock(const String &epochText) {
  String epoch = parseClock(epochText) ? epochText : String("1997-03-14 21:00");
  if (clockEpoch == epoch) return clockText(boardNow());
  bool booting = !clockEpoch.length();
  uint32_t t = 0;
  if (booting && sdReady) {
    File f = SD_MMC.open(CLOCK_FILE, FILE_READ);
    if (f) {
      String x = f.readStringUntil('\n'); f.close(); x.trim();
      if (field(x, 1) == epoch) t = strtoul(field(x, 0).c_str(), nullptr, 10);
    }
  }
  clockEpoch = epoch;
  setBoardNow(t ? t : parseClock(epoch));
  saveClock();
  return clockText(boardNow());
}
// Keeps the base fresh (millis() wraps after 49 days) and saves once a minute,
// so unplugging the stick loses at most a minute of board time.
static void tickClock() {
  uint32_t secs = (millis() - clockBaseMs) / 1000;
  clockBase += secs; clockBaseMs += secs * 1000;
  if (millis() - clockSavedAt >= 60000) saveClock();
}
// A browser caller's local time, "YYYY-MM-DD HH:MM:SS". It is moved back in
// steps of 28 years, because the calendar repeats every 28 years: 2026 becomes
// 1998 and the weekday still matches. The clock only ever moves forward.
static void syncClock(const String &phone) {
  if (!clockSyncBrowser) return;
  uint32_t t = parseClock(phone);
  if (!t) return;
  int y, eraYear; unsigned m, d;
  civilFromDays(t / 86400, y, m, d);
  civilFromDays(parseClock(clockEpoch) / 86400, eraYear, m, d);
  civilFromDays(t / 86400, y, m, d);
  int shift = (int)lroundf((y - eraYear) / 28.0f) * 28;
  if (y - shift < 1970) return;
  uint32_t shifted = (uint32_t)daysFromCivil(y - shift, m, d) * 86400UL + t % 86400;
  if (shifted <= boardNow()) return;
  event("CLOCK", "forward to " + clockText(shifted));
  setBoardNow(shifted);
  saveClock();
}
