#pragma once
// HTTP routes and the WebSocket carrier for the browser terminal, downloads
// and the captive portal.
// Part of the single TinyBBS translation unit: included in order from main.cpp.
//
// The async web server runs these handlers in its own task. HTTP handlers
// take the BBS lock. WebSocket events only queue work; the main loop applies
// it under the lock and sends frames after releasing it, so the web server's
// internal locks and ours are never held together.

static String requestArg(AsyncWebServerRequest *r, const char *name) {
  return r->hasArg(name) ? r->arg(name) : String();
}
static Session *requestSession(AsyncWebServerRequest *r) {
  return webSession(requestArg(r, "n").toInt(), requestArg(r, "q"));
}

// The browser's SPEED button: modem speed in bps for paced files, 0 = no limit.
static void setSpeed(Session &s, long bps) {
  if (bps <= 0) s.cps = 0;
  else s.cps = constrain(bps, 300L, 115200L) / 10;
}
static void apiSpeed(AsyncWebServerRequest *r) {
  BbsLock lock;
  Session *s = requestSession(r);
  if (!s) { r->send(404, "text/plain", "Lost carrier"); return; }
  setSpeed(*s, requestArg(r, "b").toInt());
  r->send(204);
}

static void root(AsyncWebServerRequest *r) {
  AsyncWebServerResponse *res = r->beginResponse(200, "text/html; charset=utf-8",
                                                 (const uint8_t *)kWebTerminal, strlen(kWebTerminal));
  res->addHeader("Cache-Control", "no-store");
  r->send(res);
}
static void apiConnect(AsyncWebServerRequest *r) {
  BbsLock lock;
  Session *s = allocate(true);
  if (!s) { r->send(503, "text/plain", "All BBS nodes are busy"); return; }
  AsyncWebServerResponse *res = r->beginResponse(200, "application/json",
      String("{\"node\":") + s->node + ",\"token\":\"" + s->token + "\"}");
  res->addHeader("Cache-Control", "no-store");
  r->send(res);
}
static void apiHangup(AsyncWebServerRequest *r) {
  BbsLock lock;
  Session *s = requestSession(r);
  if (s) closeSession(*s);
  r->send(204);
}
// Polling fallback: drain the output buffer into the HTTP response. A session
// that has a live WebSocket gets its output there instead.
static void sendTerminalOutput(AsyncWebServerRequest *r, Session &s) {
  s.lastPoll = millis();
  AsyncWebServerResponse *res;
  if (s.output.length() && !s.wsClient) { res = r->beginResponse(200, "text/plain; charset=utf-8", s.output); s.output = ""; }
  else res = r->beginResponse(204);
  res->addHeader("Cache-Control", "no-store");
  res->addHeader("X-BBS-Password", s.hideInput ? "1" : "0");
  res->addHeader("X-BBS-Stream", streaming(s) ? "1" : "0");
  r->send(res);
  if (s.state == GOODBYE && outputDone(s)) closeSession(s);
}
static void apiOut(AsyncWebServerRequest *r) {
  BbsLock lock;
  Session *s = requestSession(r);
  if (!s) { r->send(404, "text/plain", "Lost carrier"); return; }
  sendTerminalOutput(r, *s);
}
static void apiIn(AsyncWebServerRequest *r) {
  BbsLock lock;
  Session *s = requestSession(r);
  if (!s || s->state == GOODBYE) { r->send(404, "text/plain", "Lost carrier"); return; }
  input(*s, requestArg(r, "k"));
  if (s->used) sendTerminalOutput(r, *s);
  else r->send(404, "text/plain", "Lost carrier");
}
// Downloads stream from the SD card in the web server's task, so the other
// caller keeps working. The download is counted when it starts.
static void downloadFile(AsyncWebServerRequest *r) {
  BbsLock lock;
  Session *s = requestSession(r); String path, name; size_t bytes = 0;
  if (!s || !s->authenticated || s->state != FILE_LIST || !areaFile(*s, requestArg(r, "i").toInt(), path, name, bytes) || !SD_MMC.exists(path)) {
    r->send(404, "text/plain", "File unavailable"); return;
  }
  String reason;
  if (!downloadAllowed(s->user, bytes, reason)) { r->send(403, "text/plain", reason); return; }
  s->user.downloadBytes += bytes;
  if (s->user.downloads != UINT16_MAX) s->user.downloads++;
  if (!touchUser(s->user)) trace("DOWNLOAD_ACCOUNT_WARN", s->user.handle);
  refreshLiveUser(s->user);
  event("DOWNLOAD", s->user.handle + " " + name);
  trace("DOWNLOAD", name);
  AsyncWebServerResponse *res = r->beginResponse(SD_MMC, path, "application/octet-stream", true);
  res->addHeader("Cache-Control", "no-store");
  r->send(res);
}
static void modemAudio(AsyncWebServerRequest *r) {
  if (!sdReady || !SD_MMC.exists(MODEM_AUDIO)) { r->send(404, "text/plain", "Modem audio unavailable"); return; }
  AsyncWebServerResponse *res = r->beginResponse(SD_MMC, MODEM_AUDIO, "audio/mpeg");
  res->addHeader("Cache-Control", "public, max-age=86400");
  r->send(res);
}
static void status(AsyncWebServerRequest *r) {
  BbsLock lock;
  uint8_t active = 0; for (auto &s : sessions) if (s.used) active++;
  r->send(200, "application/json", String("{\"nodes\":") + active + ",\"max_nodes\":" + board.maxNodes + ",\"sd\":" + (sdReady ? "true" : "false") + "}");
}
static void notFound(AsyncWebServerRequest *r) {
  r->redirect(String("http://") + WiFi.softAPIP().toString() + "/");
}

// ---------- WebSocket carrier ---------------------------------------------------
// Browser -> board text frames: "a<node>:<token>" attaches the socket to a
// node, "k<keys>" types keys. Board -> browser: "o<ansi>" output, "p1"/"p0"
// password masking on/off, "x" no carrier.
struct WsEvent { uint32_t client; char kind; String data; };
static std::vector<WsEvent> wsInbox;
static SemaphoreHandle_t wsInboxMutex = nullptr;

static void onWsEvent(AsyncWebSocket *, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  WsEvent e{client->id(), 0, String()};
  if (type == WS_EVT_CONNECT) { client->keepAlivePeriod(15); return; }
  if (type == WS_EVT_DISCONNECT) e.kind = 'd';
  else if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo *)arg;
    if (!info->final || info->index || info->len != len || info->opcode != WS_TEXT || !len || len > 1024) return;
    e.kind = (char)data[0];
    if (e.kind != 'a' && e.kind != 'k' && e.kind != 'b') return;
    e.data = String((const char *)data + 1, len - 1);
  } else return;
  xSemaphoreTake(wsInboxMutex, portMAX_DELAY);
  if (wsInbox.size() < 64) wsInbox.push_back(e);
  xSemaphoreGive(wsInboxMutex);
}

static void tickWebSocket() {
  std::vector<WsEvent> events;
  xSemaphoreTake(wsInboxMutex, portMAX_DELAY);
  events.swap(wsInbox);
  xSemaphoreGive(wsInboxMutex);
  std::vector<std::pair<uint32_t, String>> outgoing;
  std::vector<uint32_t> hangups;
  {
    BbsLock lock;
    for (auto &e : events) {
      if (e.kind == 'a') {
        int colon = e.data.indexOf(':');
        Session *s = colon > 0 ? webSession(e.data.substring(0, colon).toInt(), e.data.substring(colon + 1)) : nullptr;
        if (!s) { hangups.push_back(e.client); continue; }
        if (s->wsClient && s->wsClient != e.client) hangups.push_back(s->wsClient);
        s->wsClient = e.client; s->wsSecret = -1; s->wsStream = -1; s->lastPoll = millis();
        trace("WS_ATTACH", String("node ") + s->node);
      } else {
        for (auto &s : sessions) {
          if (!s.used || s.wsClient != e.client) continue;
          if (e.kind == 'd') { s.wsClient = 0; trace("WS_DETACH", String("node ") + s.node); }
          else if (e.kind == 'b') setSpeed(s, e.data.toInt());
          else { s.lastPoll = millis(); if (s.state != GOODBYE) input(s, e.data); }
          break;
        }
      }
    }
    uint32_t now = millis();
    for (auto &s : sessions) {
      if (!s.used || !s.wsClient) continue;
      if (!ws.hasClient(s.wsClient)) { s.wsClient = 0; continue; }
      s.lastPoll = now;
      if (s.wsSecret != (int8_t)s.hideInput) { outgoing.push_back({s.wsClient, s.hideInput ? "p1" : "p0"}); s.wsSecret = s.hideInput; }
      if (s.wsStream != (int8_t)streaming(s)) { outgoing.push_back({s.wsClient, streaming(s) ? "s1" : "s0"}); s.wsStream = streaming(s); }
      if (s.output.length() && (s.output.length() >= 512 || now - s.sentAt >= 40) && ws.availableForWrite(s.wsClient)) { outgoing.push_back({s.wsClient, "o" + s.output}); s.output = ""; s.sentAt = now; }
      if (s.state == GOODBYE && outputDone(s)) closeSession(s);
    }
    hangups.insert(hangups.end(), wsHangups.begin(), wsHangups.end());
    wsHangups.clear();
  }
  for (auto &o : outgoing) ws.text(o.first, o.second);
  for (uint32_t id : hangups) { ws.text(id, "x"); ws.close(id); }
  ws.cleanupClients();
}

static void startWeb() {
  wsInboxMutex = xSemaphoreCreateMutex();
  ws.onEvent(onWsEvent);
  http.addHandler(&ws);
  http.on("/", HTTP_GET, root);
  http.on("/api/connect", HTTP_POST, apiConnect);
  http.on("/api/hangup", HTTP_POST, apiHangup);
  http.on("/api/speed", HTTP_POST, apiSpeed);
  http.on("/api/out", HTTP_GET, apiOut);
  http.on("/api/in", HTTP_POST, apiIn);
  http.on("/download", HTTP_GET, downloadFile);
  http.on("/status", HTTP_GET, status);
  http.on("/modem.mp3", HTTP_GET, modemAudio);
  http.onNotFound(notFound);
  http.begin();
}
