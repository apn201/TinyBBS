#pragma once
// SD card folders, INI lookup and the event log.
// Part of the single TinyBBS translation unit: included in order from main.cpp.

static bool mkdirs() {
  // A freshly formatted card has no BBS tree at all.  TinyIRC only needed a
  // root-level log, but TinyBBS owns a directory hierarchy, so provision the
  // root first rather than treating a missing /BBS as a dead card.
  const char *paths[] = {"/BBS", "/BBS/DATA", "/BBS/DATA/USERS", "/BBS/DATA/MSGS", "/BBS/DATA/STATE", "/BBS/DATA/DOORS", "/BBS/DATA/INDEX", "/BBS/LOGS", "/BBS/TEMP", "/BBS/TEMP/UPLOADS"};
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
  if (!ensureFile(USERS_FILE) || !ensureFile(MSG_FILE)) return false;
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
// Record files hold one record per line, its id first ("id|..."). They are
// rewritten the safe way: the new version goes to NAME.NEW, the old one is
// kept as NAME.BAK until the swap is done, and a lone .BAK is put back.
static String sidePath(const String &path, const char *ext) { return path.substring(0, path.lastIndexOf('.')) + ext; }
static bool recoverFile(const String &path) {
  String bak = sidePath(path, ".BAK");
  return SD_MMC.exists(path) || !SD_MMC.exists(bak) || SD_MMC.rename(bak, path);
}
// Puts `line` in place of the record with this id, or appends it when there
// is none (and `add`). An empty `line` removes the record. False when the
// card could not be written, or when there was no such record to replace
// and `add` is off.
static bool putRecord(const String &path, uint32_t id, const String &line, bool add = true) {
  if (!sdReady || !id || !recoverFile(path)) return false;
  const String tmp = sidePath(path, ".NEW"), bak = sidePath(path, ".BAK");
  const bool exists = SD_MMC.exists(path);
  File in = exists ? SD_MMC.open(path, FILE_READ) : File();
  if (exists && !in) return false;
  SD_MMC.remove(tmp);
  File out = SD_MMC.open(tmp, FILE_WRITE);
  if (!out) { if (in) in.close(); return false; }
  bool found = false, ok = true;
  auto put = [&](const String &l) { return out.println(l) == l.length() + 2; };
  while (in && in.available() && ok) {
    String l = in.readStringUntil('\n');
    while (l.endsWith("\r")) l.remove(l.length() - 1);  // println() wrote CRLF
    if (!l.length()) continue;
    if (strtoul(l.c_str(), nullptr, 10) != id) ok = put(l);
    else { if (!found && line.length()) ok = put(line); found = true; }
  }
  if (ok && !found && add && line.length()) ok = put(line);
  if (in) in.close();
  out.close();
  if (!ok || (!found && !add)) { SD_MMC.remove(tmp); return false; }
  SD_MMC.remove(bak);
  if (exists && !SD_MMC.rename(path, bak)) { SD_MMC.remove(tmp); return false; }
  if (!SD_MMC.rename(tmp, path)) { if (exists) SD_MMC.rename(bak, path); return false; }
  if (exists) SD_MMC.remove(bak);
  return true;
}

// The event log: logins, logoffs, what callers do, SysOp actions. The SysOp
// reads it with L in the console. At 512 KB it becomes EVENTS.OLD.
static constexpr const char *EVENT_LOG = "/BBS/LOGS/EVENTS.LOG";
static void event(const String &tag, const String &detail) {
  if (!sdReady) return;
  File f = SD_MMC.open(EVENT_LOG, FILE_APPEND);
  if (!f) return;
  bool full = f.size() > 512UL * 1024;
  f.println(nowText() + "|" + tag + "|" + enc(detail));
  f.close();
  if (full) { SD_MMC.remove("/BBS/LOGS/EVENTS.OLD"); SD_MMC.rename(EVENT_LOG, "/BBS/LOGS/EVENTS.OLD"); }
}
