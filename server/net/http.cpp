#include "platform/platform.h"
#include "bbs_core.h"
#include "net/http.h"
#include "net/routes.h"

#include <civetweb.h>
#include <atomic>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>

#ifndef TINYBBS_WEB_ROOT
#define TINYBBS_WEB_ROOT "shared/web"
#endif

struct ServerHttp::Impl {
  mg_context *context = nullptr;
  std::mutex mutex;
  uint32_t nextClient = 1;
  struct Event { uint32_t client; char kind; String data; };
  std::vector<Event> inbox;
  std::map<uint32_t, mg_connection *> clients;
  std::map<const mg_connection *, uint32_t> clientIds;
};

static String decodeForm(const std::string &value) {
  std::vector<char> out(value.size() + 1);
  int count = mg_url_decode(value.c_str(), (int)value.size(), out.data(), (int)out.size(), 1);
  return count >= 0 ? String(out.data(), (size_t)count) : String();
}
static void addFormArgs(WebRouteRequest &request, const char *form) {
  if (!form) return;
  std::string text(form);
  size_t at = 0;
  while (at <= text.size()) {
    size_t end = text.find('&', at); if (end == std::string::npos) end = text.size();
    size_t eq = text.find('=', at);
    if (eq != std::string::npos && eq < end) request.args.push_back({decodeForm(text.substr(at, eq - at)), decodeForm(text.substr(eq + 1, end - eq - 1))});
    else if (at < end) request.args.push_back({decodeForm(text.substr(at, end - at)), String()});
    if (end == text.size()) break;
    at = end + 1;
  }
}
static bool readBody(mg_connection *connection, size_t bytes, std::vector<uint8_t> &body) {
  body.resize(bytes); size_t at = 0;
  while (at < bytes) { int got = mg_read(connection, body.data() + at, bytes - at); if (got <= 0) return false; at += (size_t)got; }
  return true;
}
static WebRouteRequest httpRequest(mg_connection *connection, std::vector<uint8_t> *body = nullptr) {
  const mg_request_info *info = mg_get_request_info(connection);
  WebRouteRequest request;
  request.method = info->request_method ? info->request_method : "";
  request.path = info->local_uri ? info->local_uri : "/";
  request.contentLength = info->content_length > 0 ? (size_t)info->content_length : 0;
  addFormArgs(request, info->query_string);
  const char *type = mg_get_header(connection, "Content-Type");
  if (body && request.contentLength && type && std::strstr(type, "application/x-www-form-urlencoded") && readBody(connection, request.contentLength, *body))
    addFormArgs(request, std::string((const char *)body->data(), body->size()).c_str());
  return request;
}
static void writeBytes(mg_connection *connection, const void *data, size_t bytes) { if (bytes) mg_write(connection, data, bytes); }
static void writeResponse(mg_connection *connection, const WebRouteResponse &response) {
  std::string body;
  if (response.page == WEB_PAGE_TERMINAL || response.page == WEB_PAGE_SYSOP) {
    const char *name = response.page == WEB_PAGE_TERMINAL ? "terminal.html" : "sysop.html";
    std::ifstream page(std::string(TINYBBS_WEB_ROOT) + "/" + name, std::ios::binary);
    body.assign(std::istreambuf_iterator<char>(page), std::istreambuf_iterator<char>());
  } else body = static_cast<std::string>(response.text);
  if (response.cardPath.length()) {
    File file = SD_MMC.open(response.cardPath, FILE_READ);
    if (!file || file.isDirectory()) { writeResponse(connection, webText(404, "text/plain", "Not found")); return; }
    mg_printf(connection, "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %lu\r\n", response.type.c_str(), (unsigned long)file.size());
    if (response.cacheControl.length()) mg_printf(connection, "Cache-Control: %s\r\n", response.cacheControl.c_str());
    if (response.download) mg_printf(connection, "Content-Disposition: attachment\r\n");
    mg_printf(connection, "\r\n");
    uint8_t buffer[4096]; while (file.available()) { size_t got = file.read(buffer, sizeof(buffer)); if (!got) break; writeBytes(connection, buffer, got); }
    file.close(); return;
  }
  mg_printf(connection, "HTTP/1.1 %d\r\nContent-Type: %s\r\nContent-Length: %lu\r\n", response.status, response.type.c_str(), (unsigned long)body.size());
  if (response.cacheControl.length()) mg_printf(connection, "Cache-Control: %s\r\n", response.cacheControl.c_str());
  for (const auto &header : response.headers) mg_printf(connection, "%s: %s\r\n", header.first.c_str(), header.second.c_str());
  mg_printf(connection, "\r\n"); writeBytes(connection, body.data(), body.size());
}
static bool multipartFile(const std::vector<uint8_t> &body, const char *type, String &name, const uint8_t *&data, size_t &size) {
  if (!type) return false; const char *mark = std::strstr(type, "boundary="); if (!mark) return false;
  std::string boundary = "--" + std::string(mark + 9); std::string text((const char *)body.data(), body.size());
  size_t header = text.find("\r\n\r\n"), filename = text.find("filename=\"");
  if (header == std::string::npos || filename == std::string::npos) return false;
  filename += 10; size_t quote = text.find('"', filename); if (quote == std::string::npos) return false;
  size_t begin = header + 4, end = text.find("\r\n" + boundary, begin); if (end == std::string::npos || end < begin) return false;
  name = String(text.substr(filename, quote - filename)); data = body.data() + begin; size = end - begin; return true;
}
static int httpHandler(mg_connection *connection, void *) {
  std::vector<uint8_t> body; WebRouteRequest request = httpRequest(connection, &body);
  if (request.method == "POST" && request.path == "/upload") {
    if (request.contentLength > board.uploadMax + 8192 || !readBody(connection, request.contentLength, body)) { writeResponse(connection, webText(400, "text/plain", "Bad upload")); return 400; }
    String name; const uint8_t *data = nullptr; size_t size = 0;
    if (!multipartFile(body, mg_get_header(connection, "Content-Type"), name, data, size)) { writeResponse(connection, webText(400, "text/plain", "Bad upload")); return 400; }
    WebRouteResponse response = webUpload(request, name, data, size); writeResponse(connection, response); return response.status;
  }
  if (request.method == "POST" && request.path == "/sysop/api/upload") {
    if (request.contentLength > 64UL * 1024UL * 1024UL || !readBody(connection, request.contentLength, body)) { writeResponse(connection, webText(400, "text/plain", "Bad upload")); return 400; }
    String name; const uint8_t *data = nullptr; size_t size = 0;
    if (!multipartFile(body, mg_get_header(connection, "Content-Type"), name, data, size)) { writeResponse(connection, webText(400, "text/plain", "Bad upload")); return 400; }
    BbsLock lock; String dir; WebRouteResponse error;
    if (!webFmBegin(request, dir, true, error)) { writeResponse(connection, error); return error.status; }
    int slash = max(name.lastIndexOf('/'), name.lastIndexOf('\\')); if (slash >= 0) name = name.substring(slash + 1);
    if (!webFmName(name) || !webFmMkdirs(dir)) { WebRouteResponse response = webText(400, "text/plain", "Bad target"); writeResponse(connection, response); return response.status; }
    String part = dir + "/" + name + ".part"; SD_MMC.remove(part); File file = SD_MMC.open(part, FILE_WRITE);
    if (!file || (size && file.write(data, size) != size)) { if (file) file.close(); SD_MMC.remove(part); WebRouteResponse response = webText(500, "text/plain", "Write failed, card full?"); writeResponse(connection, response); return response.status; }
    file.close(); String target = dir + "/" + name; if (SD_MMC.exists(target)) SD_MMC.remove(target);
    if (!SD_MMC.rename(part, target)) { SD_MMC.remove(part); WebRouteResponse response = webText(500, "text/plain", "Could not finish file"); writeResponse(connection, response); return response.status; }
    event("SYSOP_FM_UPLOAD", target); WebRouteResponse response = webText(200, "text/plain", "ok"); writeResponse(connection, response); return response.status;
  }
  if (request.path.startsWith("/sysop/api/")) { WebRouteResponse response = webSysopRoute(request); writeResponse(connection, response); return response.status; }
  WebRouteResponse response = webRoute(request); writeResponse(connection, response); return response.status;
}
static void wsReady(mg_connection *connection, void *data) {
  auto *impl = static_cast<ServerHttp::Impl *>(data); std::lock_guard<std::mutex> lock(impl->mutex);
  uint32_t id = impl->nextClient++; if (!id) id = impl->nextClient++;
  impl->clients[id] = connection; impl->clientIds[connection] = id;
}
static int wsData(mg_connection *connection, int bits, char *data, size_t length, void *opaque) {
  auto *impl = static_cast<ServerHttp::Impl *>(opaque);
  if ((bits & 0x0f) != MG_WEBSOCKET_OPCODE_TEXT || !length || length > 1024) return 1;
  char kind = data[0]; if (kind != 'a' && kind != 'k' && kind != 'b' && kind != 'c') return 1;
  std::lock_guard<std::mutex> lock(impl->mutex); auto it = impl->clientIds.find(connection);
  if (it != impl->clientIds.end() && impl->inbox.size() < 64) impl->inbox.push_back({it->second, kind, String(data + 1, length - 1)});
  return 1;
}
static void wsClose(const mg_connection *connection, void *opaque) {
  auto *impl = static_cast<ServerHttp::Impl *>(opaque); std::lock_guard<std::mutex> lock(impl->mutex);
  auto it = impl->clientIds.find(connection); if (it == impl->clientIds.end()) return;
  impl->inbox.push_back({it->second, 'd', String()}); impl->clients.erase(it->second); impl->clientIds.erase(it);
}
bool ServerHttp::start(const std::string &endpoint) {
  stop();
  if (!mg_init_library(MG_FEATURES_DEFAULT) || !(mg_check_feature(MG_FEATURES_WEBSOCKET) & MG_FEATURES_WEBSOCKET)) return false;
  impl_ = new Impl();
  const char *options[] = {"listening_ports", endpoint.c_str(), "num_threads", "4", "enable_keep_alive", "yes", nullptr};
  impl_->context = mg_start(nullptr, impl_, options);
  if (!impl_->context) { delete impl_; impl_ = nullptr; mg_exit_library(); return false; }
  mg_set_request_handler(impl_->context, "/", httpHandler, impl_);
  mg_set_websocket_handler(impl_->context, "/ws", nullptr, wsReady, wsData, wsClose, impl_);
  return true;
}
void ServerHttp::stop() { if (!impl_) return; if (impl_->context) mg_stop(impl_->context); delete impl_; impl_ = nullptr; mg_exit_library(); }
void ServerHttp::tickWebSocket() {
  if (!impl_) return; std::vector<Impl::Event> events; std::map<uint32_t, mg_connection *> clients;
  { std::lock_guard<std::mutex> lock(impl_->mutex); events.swap(impl_->inbox); clients = impl_->clients; }
  std::vector<std::pair<uint32_t, String>> outgoing; std::vector<uint32_t> hangups;
  { BbsLock lock;
    for (auto &event : events) {
      if (event.kind == 'a') { int colon = event.data.indexOf(':'); Session *s = colon > 0 ? webSession(event.data.substring(0, colon).toInt(), event.data.substring(colon + 1)) : nullptr;
        if (!s) { hangups.push_back(event.client); continue; } if (s->wsClient && s->wsClient != event.client) hangups.push_back(s->wsClient);
        s->wsClient = event.client; s->wsSecret = -1; s->wsStream = -1; s->lastPoll = millis(); trace("WS_ATTACH", String("node ") + s->node);
      } else for (auto &s : sessions) if (s.used && s.wsClient == event.client) { if (event.kind == 'd') { s.wsClient = 0; trace("WS_DETACH", String("node ") + s.node); } else if (event.kind == 'b') setSpeed(s, event.data.toInt()); else if (event.kind == 'c') syncClock(event.data); else { s.lastPoll = millis(); if (s.state != GOODBYE) input(s, event.data); } break; }
    }
    uint32_t now = millis(); for (auto &s : sessions) { if (!s.used || !s.wsClient) continue; if (!clients.count(s.wsClient)) { s.wsClient = 0; continue; } s.lastPoll = now;
      if (s.wsSecret != (int8_t)s.hideInput) { outgoing.push_back({s.wsClient, s.hideInput ? "p1" : "p0"}); s.wsSecret = s.hideInput; }
      if (s.wsStream != (int8_t)keyMode(s)) { s.wsStream = keyMode(s); outgoing.push_back({s.wsClient, "s" + String(s.wsStream)}); }
      if (s.output.length() && (s.output.length() >= 512 || now - s.sentAt >= 40)) { outgoing.push_back({s.wsClient, "o" + s.output}); s.output = ""; s.sentAt = now; }
      if (s.state == GOODBYE && outputDone(s)) closeSession(s); }
    hangups.insert(hangups.end(), wsHangups.begin(), wsHangups.end()); wsHangups.clear(); }
  for (auto &message : outgoing) if (clients.count(message.first)) mg_websocket_write(clients[message.first], MG_WEBSOCKET_OPCODE_TEXT, message.second.c_str(), message.second.length());
  for (uint32_t id : hangups) if (clients.count(id)) { mg_websocket_write(clients[id], MG_WEBSOCKET_OPCODE_TEXT, "x", 1); mg_close_connection(clients[id]); }
}
