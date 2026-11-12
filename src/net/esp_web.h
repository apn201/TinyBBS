#pragma once
// ESPAsyncWebServer binding for the shared routes in routes.h. This file owns
// only ESP32 request conversion, WebSocket framing and captive-portal redirect.

AsyncWebServer http(80);
AsyncWebSocket ws("/ws");

static WebRouteRequest espRequest(AsyncWebServerRequest *request) {
  WebRouteRequest route;
  route.method = request->method() == HTTP_GET ? "GET" : request->method() == HTTP_POST ? "POST" : "";
  route.path = request->url();
  route.contentLength = request->contentLength();
  for (size_t i = 0; i < request->params(); ++i) {
    const AsyncWebParameter *parameter = request->getParam(i);
    route.args.push_back({parameter->name(), parameter->value()});
  }
  return route;
}
static void espSend(AsyncWebServerRequest *request, const WebRouteResponse &route) {
  AsyncWebServerResponse *response;
  if (route.page == WEB_PAGE_TERMINAL)
    response = request->beginResponse(route.status, route.type.c_str(), (const uint8_t *)kWebTerminal, strlen(kWebTerminal));
  else if (route.page == WEB_PAGE_SYSOP)
    response = request->beginResponse(route.status, route.type.c_str(), (const uint8_t *)kSysopPage, strlen(kSysopPage));
  else if (route.cardPath.length())
    response = request->beginResponse(SD_MMC, route.cardPath, route.type.c_str(), route.download);
  else response = request->beginResponse(route.status, route.type.c_str(), route.text);
  if (route.cacheControl.length()) response->addHeader("Cache-Control", route.cacheControl);
  for (const auto &header : route.headers) response->addHeader(header.first, header.second);
  request->send(response);
}
static void espRoute(AsyncWebServerRequest *request) { espSend(request, webRoute(espRequest(request))); }

static void espUploadChunk(AsyncWebServerRequest *request, const String &filename, size_t index,
                           uint8_t *data, size_t length, bool final) {
  WebUpload *upload = static_cast<WebUpload *>(request->_tempObject);
  if (!index) {
    if (upload) delete upload;
    upload = new WebUpload(); request->_tempObject = upload;
  }
  if (upload) webUploadChunk(espRequest(request), *upload, filename, index, data, length, final);
}
static void espUploadDone(AsyncWebServerRequest *request) {
  WebUpload *upload = static_cast<WebUpload *>(request->_tempObject);
  WebRouteResponse response = upload ? webUploadFinish(*upload) : webText(400, "text/plain", "Upload missing");
  delete upload; request->_tempObject = nullptr; espSend(request, response);
}
static void espSysopRoute(AsyncWebServerRequest *request) { espSend(request, webSysopRoute(espRequest(request))); }
static void espSysopUploadChunk(AsyncWebServerRequest *request, const String &filename, size_t index,
                                uint8_t *data, size_t length, bool final) {
  WebSysopUpload *upload = static_cast<WebSysopUpload *>(request->_tempObject);
  if (!index) { if (upload) delete upload; upload = new WebSysopUpload(); request->_tempObject = upload; }
  if (upload) webSysopUploadChunk(espRequest(request), *upload, filename, index, data, length, final);
}
static void espSysopUploadDone(AsyncWebServerRequest *request) {
  WebSysopUpload *upload = static_cast<WebSysopUpload *>(request->_tempObject);
  WebRouteResponse response = upload ? webSysopUploadFinish(*upload) : webText(400, "text/plain", "Upload missing");
  delete upload; request->_tempObject = nullptr; espSend(request, response);
}

// Browser -> board events are queued here; tickWebSocket applies them under
// BbsLock after the loop's regular BBS tick.
struct EspWsEvent { uint32_t client; char kind; String data; };
static std::vector<EspWsEvent> espWsInbox;
static SemaphoreHandle_t espWsInboxMutex = nullptr;
static void onEspWsEvent(AsyncWebSocket *, AsyncWebSocketClient *client, AwsEventType type,
                         void *arg, uint8_t *data, size_t length) {
  EspWsEvent event{client->id(), 0, String()};
  if (type == WS_EVT_CONNECT) { client->keepAlivePeriod(15); return; }
  if (type == WS_EVT_DISCONNECT) event.kind = 'd';
  else if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = static_cast<AwsFrameInfo *>(arg);
    if (!info->final || info->index || info->len != length || info->opcode != WS_TEXT || !length || length > 1024) return;
    event.kind = static_cast<char>(data[0]);
    if (event.kind != 'a' && event.kind != 'k' && event.kind != 'b' && event.kind != 'c') return;
    event.data = String(reinterpret_cast<const char *>(data) + 1, length - 1);
  } else return;
  xSemaphoreTake(espWsInboxMutex, portMAX_DELAY);
  if (espWsInbox.size() < 64) espWsInbox.push_back(event);
  xSemaphoreGive(espWsInboxMutex);
}
static void tickWebSocket() {
  std::vector<EspWsEvent> events;
  xSemaphoreTake(espWsInboxMutex, portMAX_DELAY); events.swap(espWsInbox); xSemaphoreGive(espWsInboxMutex);
  std::vector<std::pair<uint32_t, String>> outgoing;
  std::vector<uint32_t> hangups;
  {
    BbsLock lock;
    for (auto &event : events) {
      if (event.kind == 'a') {
        int colon = event.data.indexOf(':');
        Session *session = colon > 0 ? webSession(event.data.substring(0, colon).toInt(), event.data.substring(colon + 1)) : nullptr;
        if (!session) { hangups.push_back(event.client); continue; }
        if (session->wsClient && session->wsClient != event.client) hangups.push_back(session->wsClient);
        session->wsClient = event.client; session->wsSecret = -1; session->wsStream = -1; session->lastPoll = millis();
        trace("WS_ATTACH", String("node ") + session->node);
      } else for (auto &session : sessions) if (session.used && session.wsClient == event.client) {
        if (event.kind == 'd') { session.wsClient = 0; trace("WS_DETACH", String("node ") + session.node); }
        else if (event.kind == 'b') setSpeed(session, event.data.toInt());
        else if (event.kind == 'c') syncClock(event.data);
        else { session.lastPoll = millis(); if (session.state != GOODBYE) input(session, event.data); }
        break;
      }
    }
    uint32_t now = millis();
    for (auto &session : sessions) {
      if (!session.used || !session.wsClient) continue;
      if (!ws.hasClient(session.wsClient)) { session.wsClient = 0; continue; }
      session.lastPoll = now;
      if (session.wsSecret != static_cast<int8_t>(session.hideInput)) { outgoing.push_back({session.wsClient, session.hideInput ? "p1" : "p0"}); session.wsSecret = session.hideInput; }
      if (session.wsStream != static_cast<int8_t>(keyMode(session))) { session.wsStream = keyMode(session); outgoing.push_back({session.wsClient, "s" + String(session.wsStream)}); }
      if (session.output.length() && (session.output.length() >= 512 || now - session.sentAt >= 40) && ws.availableForWrite(session.wsClient)) { outgoing.push_back({session.wsClient, "o" + session.output}); session.output = ""; session.sentAt = now; }
      if (session.state == GOODBYE && outputDone(session)) closeSession(session);
    }
    hangups.insert(hangups.end(), wsHangups.begin(), wsHangups.end()); wsHangups.clear();
  }
  for (auto &message : outgoing) ws.text(message.first, message.second);
  for (uint32_t id : hangups) { ws.text(id, "x"); ws.close(id); }
  ws.cleanupClients();
}
static void startWeb() {
  espWsInboxMutex = xSemaphoreCreateMutex(); ws.onEvent(onEspWsEvent); http.addHandler(&ws);
  http.on("/", HTTP_GET, espRoute); http.on("/api/board", HTTP_GET, espRoute);
  http.on("/api/connect", HTTP_POST, espRoute); http.on("/api/hangup", HTTP_POST, espRoute);
  http.on("/api/speed", HTTP_POST, espRoute); http.on("/api/out", HTTP_GET, espRoute);
  http.on("/api/in", HTTP_POST, espRoute); http.on("/download", HTTP_GET, espRoute);
  http.on("/view", HTTP_GET, espRoute); http.on("/music", HTTP_GET, espRoute);
  http.on("/player", HTTP_GET, espRoute); http.on("/upload", HTTP_POST, espUploadDone, espUploadChunk);
  http.on("/status", HTTP_GET, espRoute); http.on("/modem.mp3", HTTP_GET, espRoute);
  http.on("/sysop/api/list", HTTP_GET, espSysopRoute); http.on("/sysop/api/get", HTTP_GET, espSysopRoute);
  http.on("/sysop/api/mkdir", HTTP_POST, espSysopRoute); http.on("/sysop/api/rename", HTTP_POST, espSysopRoute);
  http.on("/sysop/api/delete", HTTP_POST, espSysopRoute);
  http.on("/sysop/api/upload", HTTP_POST, espSysopUploadDone, espSysopUploadChunk);
  http.on("/sysop", HTTP_GET, espRoute);
  http.onNotFound([](AsyncWebServerRequest *request) { request->redirect(String("http://") + WiFi.softAPIP().toString() + "/"); });
  http.begin();
}
