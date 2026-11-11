#pragma once
// HTTP routes for the browser terminal, downloads and captive portal.
// Part of the single TinyBBS translation unit: included in order from main.cpp.

// ---------- network adapters ----------------------------------------------------
static void root() {
  http.sendHeader("Cache-Control", "no-store");
  http.send_P(200, "text/html; charset=utf-8", kWebTerminal);
}
static void apiConnect() {
  Session *s = allocate(true);
  http.sendHeader("Cache-Control", "no-store");
  if (!s) { http.send(503, "text/plain", "All BBS nodes are busy"); return; }
  http.send(200, "application/json", String("{\"node\":") + s->node + ",\"token\":\"" + s->token + "\"}");
}
static void apiHangup() {
  Session *s = webSession(http.arg("n").toInt(), http.arg("q"));
  if (s) closeSession(*s);
  http.send(204, "text/plain", "");
}
static void sendTerminalOutput(Session &s) {
  s.lastPoll = millis();
  http.sendHeader("Cache-Control", "no-store");
  http.sendHeader("X-BBS-Password", s.hideInput ? "1" : "0");
  if (s.output.length()) http.send(200, "text/plain; charset=utf-8", s.output);
  else http.send(204, "text/plain", "");
  s.output = "";
  if (s.state == GOODBYE) closeSession(s);
}
static void apiOut() {
  Session *s = webSession(http.arg("n").toInt(), http.arg("q"));
  if (!s) { http.send(404, "text/plain", "Lost carrier"); return; }
  sendTerminalOutput(*s);
}
static void apiIn() {
  Session *s = webSession(http.arg("n").toInt(), http.arg("q"));
  if (!s || s->state == GOODBYE) { http.send(404, "text/plain", "Lost carrier"); return; }
  input(*s, http.arg("k"));
  if (s->used) sendTerminalOutput(*s);
  else http.send(404, "text/plain", "Lost carrier");
}
static void downloadFile() {
  Session *s = webSession(http.arg("n").toInt(), http.arg("q")); String path, name; size_t bytes = 0;
  if (!s || !s->authenticated || s->state != FILE_LIST || !areaFile(*s, http.arg("i").toInt(), path, name, bytes)) { http.send(404, "text/plain", "File unavailable"); return; }
  String reason; if (!downloadAllowed(s->user, bytes, reason)) { http.send(403, "text/plain", reason); return; }
  File f = SD_MMC.open(path, FILE_READ); if (!f) { http.send(404, "text/plain", "File unavailable"); return; }
  http.sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\""); http.sendHeader("Cache-Control", "no-store"); size_t sent = http.streamFile(f, "application/octet-stream"); f.close();
  if (sent == bytes) { s->user.downloadBytes += bytes; if (s->user.downloads != UINT16_MAX) s->user.downloads++; if (!touchUser(s->user)) trace("DOWNLOAD_ACCOUNT_WARN", s->user.handle); refreshLiveUser(s->user); event("DOWNLOAD", s->user.handle + " " + name); trace("DOWNLOAD", name); }
  else trace("DOWNLOAD_SHORT", name);
}
static void modemAudio() {
  if (!sdReady) { http.send(404, "text/plain", "Modem audio unavailable"); return; }
  File f = SD_MMC.open(MODEM_AUDIO, FILE_READ);
  if (!f || f.isDirectory()) { http.send(404, "text/plain", "Modem audio unavailable"); return; }
  http.sendHeader("Cache-Control", "public, max-age=86400");
  http.streamFile(f, "audio/mpeg");
  f.close();
}
static void status() { uint8_t active = 0; for (auto &s : sessions) if (s.used) active++; http.send(200, "application/json", String("{\"nodes\":") + active + ",\"max_nodes\":" + MAX_NODES + ",\"sd\":" + (sdReady ? "true" : "false") + "}"); }
static void notFound() { http.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true); http.send(302, "text/plain", "TinyBBS captive portal"); }
