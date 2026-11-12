#pragma once
// Transport-neutral browser routes. ESPAsyncWebServer and the Linux CivetWeb
// binding convert their request/response objects at the edge; every decision
// that touches sessions, card content or SysOp files stays here.

struct WebRouteRequest {
  String method, path;
  size_t contentLength = 0;
  std::vector<std::pair<String, String>> args;

  String arg(const char *name) const {
    for (const auto &entry : args) if (entry.first == name) return entry.second;
    return String();
  }
};

enum WebRoutePage : uint8_t { WEB_PAGE_NONE, WEB_PAGE_TERMINAL, WEB_PAGE_SYSOP };
struct WebRouteResponse {
  int status = 200;
  String type = "text/plain";
  String text, cardPath, cacheControl;
  bool download = false;
  WebRoutePage page = WEB_PAGE_NONE;
  std::vector<std::pair<String, String>> headers;

  void header(const char *name, const String &value) { headers.push_back({name, value}); }
};

static WebRouteResponse webText(int status, const char *type, const String &text = "") {
  WebRouteResponse response;
  response.status = status; response.type = type; response.text = text;
  return response;
}
static WebRouteResponse webCardFile(const String &path, const char *type, bool download, const char *cache) {
  WebRouteResponse response;
  response.type = type; response.cardPath = path; response.download = download; response.cacheControl = cache;
  return response;
}
static String webArg(const WebRouteRequest &request, const char *name) { return request.arg(name); }
static Session *webRequestSession(const WebRouteRequest &request) {
  return webSession(webArg(request, "n").toInt(), webArg(request, "q"));
}

// The browser's SPEED button: modem speed in bps for paced files, 0 = no limit.
static void setSpeed(Session &s, long bps) {
  if (bps <= 0) s.cps = 0;
  else s.cps = constrain(bps, 300L, 115200L) / 10;
}
static WebRouteResponse webTerminalOutput(Session &s) {
  s.lastPoll = millis();
  WebRouteResponse response = s.output.length() && !s.wsClient
      ? webText(200, "text/plain; charset=utf-8", s.output) : webText(204, "text/plain");
  if (s.output.length() && !s.wsClient) s.output = "";
  response.cacheControl = "no-store";
  response.header("X-BBS-Password", s.hideInput ? "1" : "0");
  response.header("X-BBS-Stream", String(keyMode(s)));
  if (s.state == GOODBYE && outputDone(s)) closeSession(s);
  return response;
}
// A file in the caller's current area, while the caller is on the matching
// browser screen. These routes never accept a card path from the browser.
static Session *webAreaFile(const WebRouteRequest &request, State state, bool (*kind)(const String &),
                            String &path, String &name, size_t &bytes) {
  Session *s = webRequestSession(request);
  if (!s || !s->authenticated || s->state != state || !canUseArea(*s, s->fileArea) ||
      !areaFile(*s, webArg(request, "i").toInt(), path, name, bytes) ||
      (kind && !kind(name)) || !SD_MMC.exists(path)) return nullptr;
  return s;
}

static bool webFmName(const String &name) { if (!name.length() || name.length() > 120 || name == "." || name == "..") return false; for (size_t i=0;i<name.length();++i) if ((uint8_t)name[i] < 32 || strchr("<>:\"|?*\\/", name[i])) return false; return name[name.length()-1] != ' ' && name[name.length()-1] != '.'; }
static bool webFmPath(String in, String &out) { in.replace('\\','/'); out=""; int at=0; while(at <= (int)in.length()) { int end=in.indexOf('/',at); if(end<0) end=in.length(); String part=in.substring(at,end); at=end+1; if(!part.length()) continue; if(!webFmName(part)) return false; out += "/" + part; } String upper=out; upper.toUpperCase(); return upper=="/BBS" || upper.startsWith("/BBS/"); }
static bool webFmUnder(const String &path, const char *root) { String upper=path; upper.toUpperCase(); size_t n=strlen(root); return upper.startsWith(root) && (upper.length()==n || upper[n]=='/'); }
static bool webFmReadOnly(const String &path) { return webFmUnder(path,"/BBS/DATA") || webFmUnder(path,"/BBS/LOGS"); }
static bool webFmMkdirs(const String &path) { int at=1; while(true) { int end=path.indexOf('/',at); String part=end<0?path:path.substring(0,end); if(!SD_MMC.exists(part) && !SD_MMC.mkdir(part)) return false; if(end<0) return true; at=end+1; } }
static String webFmParent(const String &path) { int at=path.lastIndexOf('/'); return at>0?path.substring(0,at):String("/BBS"); }
static String webFmBase(const String &path) { int at=path.lastIndexOf('/'); return at>=0?path.substring(at+1):path; }
static bool webFmRemove(const String &path, uint8_t depth=0) { File f=SD_MMC.open(path); if(!f) return false; if(!f.isDirectory()) { f.close(); return SD_MMC.remove(path); } if(depth>8) { f.close(); return false; } std::vector<String> kids; File e; while((e=f.openNextFile())) { kids.push_back(e.path()); e.close(); } f.close(); for(auto &kid:kids) if(!webFmRemove(kid,depth+1)) return false; return SD_MMC.rmdir(path); }
static bool webFmBegin(const WebRouteRequest &request, String &path, bool write, WebRouteResponse &error) { if(!adminTokenValid(webArg(request,"t"))) { error=webText(403,"text/plain","Link expired. Open the file manager from the SysOp menu again."); return false; } if(!sdReady) { error=webText(503,"text/plain","SD card offline"); return false; } if(!webFmPath(webArg(request,"p"),path)) { error=webText(400,"text/plain","Bad path"); return false; } if(write && webFmReadOnly(path)) { error=webText(403,"text/plain","DATA and LOGS are read-only here"); return false; } return true; }
static WebRouteResponse webSysopRoute(const WebRouteRequest &request) {
  BbsLock lock; String path; WebRouteResponse error;
  if (request.path == "/sysop/api/list" && request.method == "GET") { if(!webFmBegin(request,path,false,error)) return error; File dir=SD_MMC.open(path); if(!dir || !dir.isDirectory()) return webText(404,"text/plain","No such folder"); String out=String("{\"path\":\"")+jsonText(path)+"\",\"ro\":"+(webFmReadOnly(path)?"true":"false")+",\"items\":["; bool first=true; File e; while((e=dir.openNextFile())) { if(!first) out+=","; first=false; out += String("{\"n\":\"")+jsonText(webFmBase(e.path()))+"\",\"d\":"+(e.isDirectory()?"1":"0")+",\"s\":"+(unsigned long)e.size()+"}"; e.close(); } dir.close(); out += "]}"; WebRouteResponse r=webText(200,"application/json",out); r.cacheControl="no-store"; return r; }
  if (request.path == "/sysop/api/get" && request.method == "GET") { if(!webFmBegin(request,path,false,error)) return error; File f=SD_MMC.open(path); if(!f || f.isDirectory()) return webText(404,"text/plain","No such file"); f.close(); return webCardFile(path,"application/octet-stream",webArg(request,"dl")=="1","no-store"); }
  if (request.path == "/sysop/api/mkdir" && request.method == "POST") { if(!webFmBegin(request,path,true,error)) return error; if(SD_MMC.exists(path)) return webText(409,"text/plain","Already exists"); if(!webFmMkdirs(path)) return webText(500,"text/plain","Could not create folder"); event("SYSOP_FM_MKDIR",path); return webText(200,"text/plain","ok"); }
  if (request.path == "/sysop/api/rename" && request.method == "POST") { if(!webFmBegin(request,path,true,error)) return error; String to=webArg(request,"to"), target=webFmParent(path)+"/"+to; if(path.equalsIgnoreCase("/BBS") || !webFmName(to)) return webText(400,"text/plain","Bad name"); if(webFmReadOnly(target)) return webText(403,"text/plain","DATA and LOGS are read-only here"); if(!SD_MMC.exists(path)) return webText(404,"text/plain","No such file"); if(SD_MMC.exists(target) && !target.equalsIgnoreCase(path)) return webText(409,"text/plain","That name is taken"); if(!SD_MMC.rename(path,target)) return webText(500,"text/plain","Rename failed"); event("SYSOP_FM_RENAME",path+" -> "+to); return webText(200,"text/plain","ok"); }
  if (request.path == "/sysop/api/delete" && request.method == "POST") { if(!webFmBegin(request,path,true,error)) return error; if(path.equalsIgnoreCase("/BBS")) return webText(400,"text/plain","Refusing to delete /BBS"); if(!SD_MMC.exists(path)) return webText(404,"text/plain","No such file"); if(!webFmRemove(path)) return webText(500,"text/plain","Delete failed or only partly done"); event("SYSOP_FM_DELETE",path); return webText(200,"text/plain","ok"); }
  return webText(404,"text/plain","Not found");
}

static WebRouteResponse webRoute(const WebRouteRequest &request) {
  if (request.method == "GET" && request.path == "/") {
    WebRouteResponse response; response.type = "text/html; charset=utf-8";
    response.page = WEB_PAGE_TERMINAL; response.cacheControl = "no-store"; return response;
  }
  if (request.method == "GET" && request.path == "/sysop") {
    BbsLock lock;
    if (!adminTokenValid(webArg(request, "t"))) return webText(403, "text/plain", "Link expired. Open the file manager from the SysOp menu again.");
    WebRouteResponse response; response.type = "text/html; charset=utf-8";
    response.page = WEB_PAGE_SYSOP; response.cacheControl = "no-store"; return response;
  }
  if (request.method == "GET" && request.path == "/api/board") {
    BbsLock lock; return webText(200, "application/json", boardJson());
  }
  if (request.method == "POST" && request.path == "/api/connect") {
    BbsLock lock;
    Session *s = allocate(true);
    if (webArg(request, "t").length()) syncClock(webArg(request, "t"));
    if (!s) return webText(503, "text/plain", "All BBS nodes are busy");
    WebRouteResponse response = webText(200, "application/json", String("{\"node\":") + s->node + ",\"token\":\"" + s->token + "\"}");
    response.cacheControl = "no-store"; return response;
  }
  if (request.method == "POST" && request.path == "/api/hangup") {
    BbsLock lock; Session *s = webRequestSession(request); if (s) closeSession(*s); return webText(204, "text/plain");
  }
  if (request.method == "POST" && request.path == "/api/speed") {
    BbsLock lock; Session *s = webRequestSession(request);
    if (!s) return webText(404, "text/plain", "Lost carrier");
    setSpeed(*s, webArg(request, "b").toInt()); return webText(204, "text/plain");
  }
  if (request.method == "GET" && request.path == "/api/out") {
    BbsLock lock; Session *s = webRequestSession(request);
    return s ? webTerminalOutput(*s) : webText(404, "text/plain", "Lost carrier");
  }
  if (request.method == "POST" && request.path == "/api/in") {
    BbsLock lock; Session *s = webRequestSession(request);
    if (!s || s->state == GOODBYE) return webText(404, "text/plain", "Lost carrier");
    input(*s, webArg(request, "k"));
    return s->used ? webTerminalOutput(*s) : webText(404, "text/plain", "Lost carrier");
  }
  if (request.method == "GET" && request.path == "/download") {
    BbsLock lock; String path, name, reason; size_t bytes = 0;
    Session *s = webAreaFile(request, FILE_LIST, nullptr, path, name, bytes);
    if (!s || !fileAreas[s->fileArea].downloads) return webText(404, "text/plain", "File unavailable");
    if (!downloadAllowed(s->user, bytes, reason)) return webText(403, "text/plain", reason);
    s->user.downloadBytes += bytes; if (s->user.downloads != UINT16_MAX) s->user.downloads++;
    if (!touchUser(s->user)) trace("DOWNLOAD_ACCOUNT_WARN", s->user.handle);
    refreshLiveUser(s->user); countDownload(path); event("DOWNLOAD", s->user.handle + " " + name);
    return webCardFile(path, "application/octet-stream", true, "no-store");
  }
  if (request.method == "GET" && request.path == "/view") {
    BbsLock lock; String path, name; size_t bytes = 0;
    if (!webAreaFile(request, FILE_IMAGE, imageName, path, name, bytes)) return webText(404, "text/plain", "Picture unavailable");
    String u = up(name);
    return webCardFile(path, u.endsWith(".GIF") ? "image/gif" : u.endsWith(".PNG") ? "image/png" : u.endsWith(".BMP") ? "image/bmp" : "image/jpeg", false, "no-store");
  }
  if (request.method == "GET" && request.path == "/music") {
    BbsLock lock; String path, name; size_t bytes = 0;
    if (!webAreaFile(request, FILE_MUSIC, musicName, path, name, bytes)) return webText(404, "text/plain", "Tune unavailable");
    return webCardFile(path, "application/octet-stream", false, "no-store");
  }
  if (request.method == "GET" && request.path.startsWith("/player/")) {
    String name = request.path.substring(8);
    bool ok = name.length() && name.length() < 40 && name.indexOf('/') < 0 && name.indexOf("..") < 0;
    String path = String("/BBS/ASSETS/PLAYER/") + name;
    if (!ok || !sdReady || !SD_MMC.exists(path)) return webText(404, "text/plain", "Not found");
    String u = up(name); const char *type = u.endsWith(".WASM") ? "application/wasm" : u.endsWith(".JS") ? "text/javascript" : "text/plain";
    return webCardFile(path, type, false, u == "LIBOPENMPT.WASM" || u == "LIBOPENMPT.JS" ? "max-age=86400" : "no-cache, no-store");
  }
  if (request.method == "GET" && request.path == "/modem.mp3") {
    if (!sdReady || !SD_MMC.exists(MODEM_AUDIO)) return webText(404, "text/plain", "Modem audio unavailable");
    return webCardFile(MODEM_AUDIO, "audio/mpeg", false, "public, max-age=86400");
  }
  if (request.method == "GET" && request.path == "/status") {
    BbsLock lock; uint8_t active = 0; for (auto &s : sessions) if (s.used) active++;
    return webText(200, "application/json", String("{\"nodes\":") + active + ",\"max_nodes\":" + board.maxNodes + ",\"sd\":" + (sdReady ? "true" : "false") + "}");
  }
  return webText(404, "text/plain", "Not found");
}

// Multipart adapters pass one completed file here. The state checks and card
// writes are shared, irrespective of whether the transport streamed chunks.
static WebRouteResponse webUpload(const WebRouteRequest &request, const String &filename,
                                  const uint8_t *data, size_t length) {
  BbsLock lock;
  Session *s = webRequestSession(request);
  String ticket = webArg(request, "u");
  if (!s || !s->authenticated || s->state != FILE_UPLOAD_WAIT || !ticket.length() || ticket != s->uploadTicket)
    return webText(400, "text/plain", "Upload not expected");
  String name = uploadName(filename);
  if (!name.length() || hiddenEntry(name)) return webText(400, "text/plain", "Bad file name");
  if (!length) return webText(400, "text/plain", "Empty file");
  if (length > board.uploadMax) return webText(400, "text/plain", "File too large");
  SD_MMC.mkdir("/BBS/UPLOADS"); SD_MMC.mkdir(UPLOAD_DIR);
  String part = String(UPLOAD_DIR) + "/" + s->node + ".part";
  SD_MMC.remove(part);
  File file = SD_MMC.open(part, FILE_WRITE);
  if (!file) return webText(500, "text/plain", "Could not open file");
  bool wrote = file.write(data, length) == length; file.close();
  if (!wrote) { SD_MMC.remove(part); return webText(500, "text/plain", "Write failed, card full?"); }
  write(*s, "\x1b]9;UP;close\x07");
  uploadArrived(*s, part, name, length);
  return webText(200, "text/plain", "ok");
}
