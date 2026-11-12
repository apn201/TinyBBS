#pragma once
// The invented board clock: started from epoch= in SYSTEM.INI, saved to
// CLOCK.DAT so it carries on after a power cut, and optionally pulled forward
// to a browser caller's evening (clock_sync=browser) or set from a platform
// system clock (clock_sync=system). Both sources are shifted into epoch='s
// 28-year calendar era, keeping the BBS in the 1990s with matching weekdays.
// Part of the single TinyBBS translation unit: included in order from main.cpp.

static constexpr const char *CLOCK_FILE = "/BBS/DATA/STATE/CLOCK.DAT";
static String clockEpoch;          // epoch= text the running clock was started from
static bool clockSyncBrowser = false;
static bool clockSyncSystem = false;
static uint32_t clockSavedAt = 0;  // millis() of the last save
static uint32_t clockSystemAt = 0; // millis() of the last system-clock check

// Map a real calendar timestamp into the 28-year era chosen by epoch=. The
// Gregorian weekday pattern repeats on that interval for the dates TinyBBS
// supports, so 2026 becomes 1998 without changing its weekday.
static uint32_t clockEpochEra(uint32_t t) {
  int y, eraYear; unsigned m, d;
  civilFromDays(t / 86400, y, m, d);
  civilFromDays(parseClock(clockEpoch) / 86400, eraYear, m, d);
  civilFromDays(t / 86400, y, m, d);
  int shift = (int)lroundf((y - eraYear) / 28.0f) * 28;
  if (y - shift < 1970) return 0;
  return (uint32_t)daysFromCivil(y - shift, m, d) * 86400UL + t % 86400;
}

static void saveClock() {
  clockSavedAt = millis();
  if (!sdReady) return;
  File f = SD_MMC.open(CLOCK_FILE, FILE_WRITE);
  if (!f) return;
  f.print(String(boardNow()) + "|" + clockEpoch + "\n");
  f.close();
}
// The ESP32 implementation reports no system clock; Linux supplies its
// configured local wall clock. This stays in the shared clock so clock_sync
// remains a card setting, not a server-only behaviour.
static bool syncSystemClock() {
  if (!clockSyncSystem) return false;
  uint32_t seconds = 0;
  if (!platformSystemClock(seconds)) return false;
  seconds = clockEpochEra(seconds);
  if (!seconds) return false;
  setBoardNow(seconds);
  return true;
}
// Called from loadSettings() at boot and on RELOAD. The clock continues from
// CLOCK.DAT. A changed epoch= restarts it there: that is how the SysOp resets it.
static String startClock(const String &epochText) {
  String epoch = parseClock(epochText) ? epochText : String("1997-03-14 21:00");
  if (clockEpoch == epoch) {
    if (syncSystemClock()) saveClock();
    return clockText(boardNow());
  }
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
  syncSystemClock();
  saveClock();
  return clockText(boardNow());
}
// Keeps the base fresh (millis() wraps after 49 days) and saves once a minute,
// so unplugging the stick loses at most a minute of board time.
static void tickClock() {
  uint32_t now = millis();
  if (clockSyncSystem && now - clockSystemAt >= 1000) {
    syncSystemClock();
    clockSystemAt = now;
  }
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
  uint32_t shifted = clockEpochEra(t);
  if (!shifted) return;
  if (shifted <= boardNow()) return;
  event("CLOCK", "forward to " + clockText(shifted));
  setBoardNow(shifted);
  saveClock();
}
