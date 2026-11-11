#pragma once
// SD card folders, INI lookup and the event log.
// Part of the single TinyBBS translation unit: included in order from main.cpp.

static bool mkdirs() {
  // A freshly formatted card has no BBS tree at all.  TinyIRC only needed a
  // root-level log, but TinyBBS owns a directory hierarchy, so provision the
  // root first rather than treating a missing /BBS as a dead card.
  const char *paths[] = {"/BBS", "/BBS/DATA", "/BBS/DATA/USERS", "/BBS/DATA/MSGS", "/BBS/DATA/STATE", "/BBS/DATA/DOORS", "/BBS/LOGS", "/BBS/TEMP", "/BBS/TEMP/UPLOADS"};
  bool ok = true;
  for (auto p : paths) {
    if (!SD_MMC.exists(p) && !SD_MMC.mkdir(p)) { trace("SD_MKDIR_FAIL", p); ok = false; }
  }
  return ok;
}
static bool ensureFile(const char *path) {
  if (SD_MMC.exists(path)) return true;
  File f = SD_MMC.open(path, FILE_WRITE);
  if (!f) { storageFault = String("cannot create ") + path; trace("SD_FILE_FAIL", storageFault); return false; }
  f.close(); return true;
}
static bool prepareStorage() {
  if (!mkdirs()) return false;
  if (!ensureFile("/BBS/DATA/USERS/USERS.DAT") || !ensureFile("/BBS/DATA/MSGS/MESSAGES.DAT")) return false;
  // Keep the probe FAT 8.3-safe. A leading-dot long name made some SD stacks
  // report a harmless cleanup issue as a fatal storage failure.
  const char *probe = "/BBS/DATA/STATE/TBPROBE.TMP";
  File f = SD_MMC.open(probe, FILE_WRITE);
  if (!f) { storageFault = "cannot open write probe"; trace("SD_PROBE_FAIL", "open"); return false; }
  bool wrote = f.print("OK") == 2; f.close();
  bool removed = SD_MMC.remove(probe);
  if (!wrote) { storageFault = "write probe failed"; trace("SD_PROBE_FAIL", "write"); return false; }
  if (!removed) trace("SD_PROBE_WARN", "probe cleanup failed");
  trace("SD_READY", String("total=") + (unsigned long)(SD_MMC.totalBytes() / 1024 / 1024) + "MiB free=" + (unsigned long)(SD_MMC.totalBytes() - SD_MMC.usedBytes()) / 1024 / 1024 + "MiB");
  return true;
}
static String config(const char *file, const char *key, const String &fallback) {
  if (!sdReady) return fallback; File f = SD_MMC.open(file, FILE_READ); if (!f) return fallback;
  String needle = String(key) + "=";
  while (f.available()) { String line = f.readStringUntil('\n'); line.trim(); if (line.startsWith(needle)) { f.close(); return line.substring(needle.length()); } }
  f.close(); return fallback;
}
static void event(const String &tag, const String &detail) {
  if (!sdReady) return; File f = SD_MMC.open("/BBS/LOGS/EVENTS.LOG", FILE_APPEND);
  if (f) { f.println(nowText() + "|" + tag + "|" + enc(detail)); f.close(); }
}
