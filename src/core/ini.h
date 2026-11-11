#pragma once
// INI files on the SD card: [SECTION] headers, key=value lines, ; or # comments.
// Section and key names are case-insensitive. Values are trimmed.
// Part of the single TinyBBS translation unit: included in order from main.cpp.

struct IniEntry { String section, key, value; };
typedef std::vector<IniEntry> Ini;

static bool iniLoad(const char *path, Ini &out) {
  out.clear();
  if (!sdReady) return false;
  File f = SD_MMC.open(path, FILE_READ);
  if (!f || f.isDirectory()) return false;
  String section;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (!line.length() || line[0] == ';' || line[0] == '#') continue;
    if (line[0] == '[') {
      int close = line.indexOf(']');
      section = close > 1 ? line.substring(1, close) : String();
      section.trim();
      section.toUpperCase();
      continue;
    }
    int eq = line.indexOf('=');
    if (eq <= 0) continue;
    IniEntry e;
    e.section = section;
    e.key = line.substring(0, eq); e.key.trim(); e.key.toLowerCase();
    e.value = line.substring(eq + 1); e.value.trim();
    out.push_back(e);
  }
  f.close();
  return true;
}

static String iniGet(const Ini &ini, const String &section, const char *key, const String &fallback = "") {
  String sec = section; sec.toUpperCase();
  String k = key; k.toLowerCase();
  for (const auto &e : ini) if (e.section == sec && e.key == k) return e.value;
  return fallback;
}

// Section names in file order, each once.
static std::vector<String> iniSections(const Ini &ini) {
  std::vector<String> out;
  for (const auto &e : ini) {
    bool seen = false;
    for (const auto &s : out) if (s == e.section) { seen = true; break; }
    if (!seen && e.section.length()) out.push_back(e.section);
  }
  return out;
}

// One value from one section of one file. Reads the file each time: for
// settings that are read once, at boot or on RELOAD.
static String config(const char *file, const char *section, const char *key, const String &fallback) {
  Ini ini;
  if (!iniLoad(file, ini)) return fallback;
  return iniGet(ini, section, key, fallback);
}
