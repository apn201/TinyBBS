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
static String nowText() {
  uint32_t n = millis() / 1000; char b[18];
  snprintf(b, sizeof(b), "UP %02lu:%02lu:%02lu", n / 3600, (n / 60) % 60, n % 60); return String(b);
}
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
static String cleanFlags(const String &in) {
  String out; bool comma = true;
  for (uint16_t i = 0; i < in.length() && out.length() < 63; ++i) {
    char c = in[i];
    if (isalnum((unsigned char)c) || c == '_') { out += (char)toupper((unsigned char)c); comma = false; }
    else if (c == ',' && !comma) { out += ','; comma = true; }
  }
  if (out.endsWith(",")) out.remove(out.length() - 1); return out;
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
