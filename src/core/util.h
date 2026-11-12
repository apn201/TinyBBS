#pragma once
// Small string, escaping and hashing helpers.
// Part of the single TinyBBS translation unit: included in order from main.cpp.

// ---------- small, intentionally boring helpers --------------------------------
static void trace(const char *tag, const String &detail = "") {
  Serial.printf("[TINYBBS %08lu] %s", (unsigned long)millis(), tag);
  if (detail.length()) Serial.printf(" :: %s", detail.c_str());
  Serial.println();
}
static String up(String s) { s.trim(); s.toUpperCase(); return s; }
// ---------- the board clock ------------------------------------------------------
// The stick has no real-time clock, so the board keeps an invented one: seconds
// since 1970 on a 1990s timeline that only runs while it has power. core/clock.h
// starts it from SYSTEM.INI and CLOCK.DAT and keeps it saved.
static uint32_t clockBase = 858373200;  // 1997-03-14 21:00
static uint32_t clockBaseMs = 0;
static uint32_t boardNow() { return clockBase + (millis() - clockBaseMs) / 1000; }
static void setBoardNow(uint32_t t) { clockBase = t; clockBaseMs = millis(); }
static int32_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  int era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int32_t)doe - 719468;
}
static void civilFromDays(int32_t z, int &y, unsigned &m, unsigned &d) {
  z += 719468;
  int era = (z >= 0 ? z : z - 146096) / 146097;
  unsigned doe = (unsigned)(z - era * 146097);
  unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  y = (int)yoe + era * 400;
  unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  unsigned mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp < 10 ? mp + 3 : mp - 9;
  y += m <= 2;
}
// "1997-03-14 21:00" or "1997-03-14 21:00:30" -> seconds, 0 when unreadable.
static uint32_t parseClock(const String &text) {
  int y, mo, d, h = 0, mi = 0, s = 0;
  if (sscanf(text.c_str(), "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &s) < 3) return 0;
  if (y < 1970 || y > 2099 || mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 23 || mi < 0 || mi > 59 || s < 0 || s > 59) return 0;
  return (uint32_t)daysFromCivil(y, mo, d) * 86400UL + h * 3600UL + mi * 60UL + s;
}
static uint32_t boardDay() { return boardNow() / 86400; }
static String clockText(uint32_t t, bool weekday = false) {
  static const char *days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  int y; unsigned m, d;
  civilFromDays(t / 86400, y, m, d);
  char b[24];
  snprintf(b, sizeof(b), "%s%s%04d-%02u-%02u %02lu:%02lu", weekday ? days[(t / 86400 + 4) % 7] : "", weekday ? " " : "",
           y, m, d, (unsigned long)(t / 3600 % 24), (unsigned long)(t / 60 % 60));
  return String(b);
}
// Sizes as people write them: 500K, 2M, 1.5G.
static String sizeText(uint32_t bytes) {
  if (bytes < 1024) return String(bytes) + " B";
  if (bytes < 1048576) return String((bytes + 512) / 1024) + " KB";
  char b[16];
  if (bytes >= 1073741824UL) snprintf(b, sizeof(b), "%.1f GB", bytes / 1073741824.0);
  else snprintf(b, sizeof(b), "%.1f MB", bytes / 1048576.0); return String(b);
}
// "500", "64K", "1.5M", "2MB" -> bytes.
static bool readSize(String v, uint32_t &out) {
  v = up(v);
  if (v.endsWith("B") && v.length() > 1 && !isdigit((unsigned char)v[v.length() - 2])) v.remove(v.length() - 1);
  double mult = 1;
  if (v.endsWith("K")) mult = 1024; else if (v.endsWith("M")) mult = 1048576; else if (v.endsWith("G")) mult = 1073741824.0;
  if (mult > 1) v.remove(v.length() - 1);
  v.trim();
  if (!v.length()) return false;
  char *end; double n = strtod(v.c_str(), &end);
  if (*end || n < 0 || n * mult > 4294967295.0) return false;
  out = (uint32_t)(n * mult + 0.5);
  return true;
}
static String nowText() { return clockText(boardNow()); }
static String clean(const String &in, uint16_t max, bool handle = false) {
  String out; out.reserve(max);
  for (uint16_t i = 0; i < in.length() && out.length() < max; ++i) {
    char c = in[i];
    if ((uint8_t)c < 32 || c == '|' || c == '%' || c == '\\' || c == '/') continue;
    if (handle && !(isalnum((unsigned char)c) || c == '_' || c == '-' || c == ' ')) continue;
    out += c;
  }
  out.trim(); return out;
}
static String enc(const String &in) { // stable journal delimiter escaping
  const char *hex = "0123456789ABCDEF"; String out;
  for (uint16_t i = 0; i < in.length(); ++i) { uint8_t c = in[i];
    if (c == '%' || c == '|' || c == '\r' || c == '\n') { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    else out += (char)c;
  } return out;
}
static String dec(const String &in) {
  String out; for (uint16_t i = 0; i < in.length(); ++i) {
    if (in[i] == '%' && i + 2 < in.length()) { char h[3] = {in[i + 1], in[i + 2], 0}; out += (char)strtoul(h, nullptr, 16); i += 2; }
    else out += in[i];
  } return out;
}
static String field(const String &line, uint8_t wanted) {
  uint8_t n = 0; int start = 0;
  for (int i = 0; i <= line.length(); ++i) if (i == line.length() || line[i] == '|') {
    if (n++ == wanted) return line.substring(start, i); start = i + 1;
  } return "";
}
static String sha256hex(const String &s) {
  uint8_t digest[32]; mbedtls_sha256_context c; mbedtls_sha256_init(&c);
  mbedtls_sha256_starts(&c, 0); mbedtls_sha256_update(&c, (const unsigned char*)s.c_str(), s.length());
  mbedtls_sha256_finish(&c, digest); mbedtls_sha256_free(&c);
  char out[65]; for (uint8_t i = 0; i < 32; ++i) sprintf(out + i * 2, "%02x", digest[i]); out[64] = 0; return String(out);
}
static String salt() { char b[17]; snprintf(b, sizeof(b), "%08lx%08lx", (unsigned long)esp_random(), (unsigned long)esp_random()); return String(b); }

// Command-line parsing for typed BBS commands.
static String tokenAt(String text, uint8_t wanted) {
  text.trim();
  while (wanted--) { int cut = text.indexOf(' '); if (cut < 0) return ""; text = text.substring(cut + 1); text.trim(); }
  int cut = text.indexOf(' '); return cut < 0 ? text : text.substring(0, cut);
}
static String afterTokens(String text, uint8_t count) {
  text.trim();
  while (count--) { int cut = text.indexOf(' '); if (cut < 0) return ""; text = text.substring(cut + 1); text.trim(); }
  return text;
}
// "sysop, backroom,SYSOP" -> "SYSOP,BACKROOM": upper case, no junk, no repeats.
static String cleanFlags(const String &in) {
  String out, word;
  auto take = [&]() {
    if (word.length() && ("," + out + ",").indexOf("," + word + ",") < 0 && out.length() + word.length() < 63)
      out += (out.length() ? "," : "") + word;
    word = "";
  };
  for (uint16_t i = 0; i < in.length(); ++i) {
    char c = in[i];
    if (isalnum((unsigned char)c) || c == '_') word += (char)toupper((unsigned char)c);
    else if (c == ',') take();
  }
  take();
  return out;
}
static uint32_t numberAt(const String &line, uint8_t position) { return strtoul(tokenAt(line, position).c_str(), nullptr, 10); }
static bool readNumber(const String &value, uint32_t &out) {
  if (!value.length()) return false;
  out = 0;
  for (size_t i = 0; i < value.length(); ++i) {
    char digit = value[i];
    if (digit < '0' || digit > '9' || out > (UINT32_MAX - (digit - '0')) / 10) return false;
    out = out * 10 + (digit - '0');
  }
  return true;
}
