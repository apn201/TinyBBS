#pragma once
// SysOp file manager: a plain admin page and a small JSON API for the SD card.
// Part of the single TinyBBS translation unit: included in order from main.cpp.
//
// Every request needs a live admin token (bbs/admin.h). Paths are normalised
// and must stay under /BBS. /BBS/DATA and /BBS/LOGS are read-only here so live
// users, messages and logs cannot be broken by accident.

static bool fmNameOk(const String &n) {
  if (!n.length() || n.length() > 120 || n == "." || n == "..") return false;
  for (size_t i = 0; i < n.length(); ++i) {
    char c = n[i];
    if ((uint8_t)c < 32 || strchr("<>:\"|?*\\/", c)) return false;
  }
  char last = n[n.length() - 1];
  return last != ' ' && last != '.';
}
static bool fmPath(String in, String &out) {
  in.replace('\\', '/');
  out = "";
  int i = 0;
  while (i <= (int)in.length()) {
    int j = in.indexOf('/', i);
    if (j < 0) j = in.length();
    String seg = in.substring(i, j);
    i = j + 1;
    if (!seg.length()) continue;
    if (!fmNameOk(seg)) return false;
    out += "/" + seg;
  }
  String u = out; u.toUpperCase();
  return u == "/BBS" || u.startsWith("/BBS/");
}
static bool fmUnder(const String &path, const char *root) {
  String u = path; u.toUpperCase();
  size_t n = strlen(root);
  return u.startsWith(root) && (u.length() == n || u[n] == '/');
}
static bool fmReadOnly(const String &path) { return fmUnder(path, "/BBS/DATA") || fmUnder(path, "/BBS/LOGS"); }
static bool fmIsRoot(const String &path) { return path.equalsIgnoreCase("/BBS"); }
static bool fmMkdirs(const String &path) {
  int i = 1;
  while (true) {
    int j = path.indexOf('/', i);
    String part = j < 0 ? path : path.substring(0, j);
    if (!SD_MMC.exists(part) && !SD_MMC.mkdir(part)) return false;
    if (j < 0) return true;
    i = j + 1;
  }
}
static bool fmRemove(const String &path, uint8_t depth = 0) {
  File f = SD_MMC.open(path);
  if (!f) return false;
  if (!f.isDirectory()) { f.close(); return SD_MMC.remove(path); }
  std::vector<String> kids;
  File e;
  while ((e = f.openNextFile())) { kids.push_back(e.path()); e.close(); }
  f.close();
  if (depth > 8) return false;
  for (auto &k : kids) if (!fmRemove(k, depth + 1)) return false;
  return SD_MMC.rmdir(path);
}
static String fmJson(const String &in) { return jsonText(in); }
static String fmBase(const String &path) { int cut = path.lastIndexOf('/'); return cut >= 0 ? path.substring(cut + 1) : path; }
static String fmParent(const String &path) { int cut = path.lastIndexOf('/'); return cut > 0 ? path.substring(0, cut) : String("/BBS"); }

// Checks the token and the "p" path. Sends the error response itself.
static bool fmBegin(AsyncWebServerRequest *r, String &path, bool write) {
  if (!adminTokenValid(requestArg(r, "t"))) { r->send(403, "text/plain", "Link expired. Open the file manager from the SysOp menu again."); return false; }
  if (!sdReady) { r->send(503, "text/plain", "SD card offline"); return false; }
  if (!fmPath(requestArg(r, "p"), path)) { r->send(400, "text/plain", "Bad path"); return false; }
  if (write && fmReadOnly(path)) { r->send(403, "text/plain", "DATA and LOGS are read-only here"); return false; }
  return true;
}

static void fmPage(AsyncWebServerRequest *r) {
  BbsLock lock;
  if (!adminTokenValid(requestArg(r, "t"))) { r->send(403, "text/plain", "Link expired. Open the file manager from the SysOp menu again."); return; }
  AsyncWebServerResponse *res = r->beginResponse(200, "text/html; charset=utf-8", (const uint8_t *)kSysopPage, strlen(kSysopPage));
  res->addHeader("Cache-Control", "no-store");
  r->send(res);
}
static void fmList(AsyncWebServerRequest *r) {
  BbsLock lock; String path;
  if (!fmBegin(r, path, false)) return;
  File dir = SD_MMC.open(path);
  if (!dir || !dir.isDirectory()) { r->send(404, "text/plain", "No such folder"); return; }
  AsyncResponseStream *out = r->beginResponseStream("application/json");
  out->addHeader("Cache-Control", "no-store");
  out->print("{\"path\":\""); out->print(fmJson(path)); out->print("\",\"ro\":"); out->print(fmReadOnly(path) ? "true" : "false");
  out->print(",\"items\":[");
  bool first = true; File e;
  while ((e = dir.openNextFile())) {
    if (!first) out->print(",");
    first = false;
    out->print("{\"n\":\""); out->print(fmJson(fmBase(e.path()))); out->print("\",\"d\":"); out->print(e.isDirectory() ? "1" : "0");
    out->print(",\"s\":"); out->print((unsigned long)e.size()); out->print("}");
    e.close();
  }
  dir.close();
  out->print("]}");
  r->send(out);
}
static void fmGet(AsyncWebServerRequest *r) {
  BbsLock lock; String path;
  if (!fmBegin(r, path, false)) return;
  File f = SD_MMC.open(path);
  if (!f || f.isDirectory()) { r->send(404, "text/plain", "No such file"); return; }
  f.close();
  AsyncWebServerResponse *res = r->beginResponse(SD_MMC, path, "application/octet-stream", requestArg(r, "dl") == "1");
  res->addHeader("Cache-Control", "no-store");
  r->send(res);
}
static void fmMkdir(AsyncWebServerRequest *r) {
  BbsLock lock; String path;
  if (!fmBegin(r, path, true)) return;
  if (SD_MMC.exists(path)) { r->send(409, "text/plain", "Already exists"); return; }
  if (!fmMkdirs(path)) { r->send(500, "text/plain", "Could not create folder"); return; }
  event("SYSOP_FM_MKDIR", path);
  r->send(200, "text/plain", "ok");
}
static void fmRename(AsyncWebServerRequest *r) {
  BbsLock lock; String path;
  if (!fmBegin(r, path, true)) return;
  String to = requestArg(r, "to");
  if (fmIsRoot(path) || !fmNameOk(to)) { r->send(400, "text/plain", "Bad name"); return; }
  String target = fmParent(path) + "/" + to;
  if (fmReadOnly(target)) { r->send(403, "text/plain", "DATA and LOGS are read-only here"); return; }
  if (!SD_MMC.exists(path)) { r->send(404, "text/plain", "No such file"); return; }
  if (SD_MMC.exists(target) && !target.equalsIgnoreCase(path)) { r->send(409, "text/plain", "That name is taken"); return; }
  if (!SD_MMC.rename(path, target)) { r->send(500, "text/plain", "Rename failed"); return; }
  event("SYSOP_FM_RENAME", path + " -> " + to);
  r->send(200, "text/plain", "ok");
}
static void fmDelete(AsyncWebServerRequest *r) {
  BbsLock lock; String path;
  if (!fmBegin(r, path, true)) return;
  if (fmIsRoot(path)) { r->send(400, "text/plain", "Refusing to delete /BBS"); return; }
  if (!SD_MMC.exists(path)) { r->send(404, "text/plain", "No such file"); return; }
  if (!fmRemove(path)) { r->send(500, "text/plain", "Delete failed or only partly done"); return; }
  event("SYSOP_FM_DELETE", path);
  r->send(200, "text/plain", "ok");
}

// Upload: POST multipart to /sysop/api/upload?t=&p=<folder>. The folder is
// created if needed. Data goes to <name>.part and is renamed when complete,
// so an interrupted upload never replaces a good file.
static void fmUploadFail(AsyncWebServerRequest *r, const char *why) {
  if (r->_tempFile) r->_tempFile.close();
  if (!r->_tempObject) r->_tempObject = strdup(why);
}
static void fmUploadChunk(AsyncWebServerRequest *r, const String &filename, size_t index, uint8_t *data, size_t len, bool final) {
  BbsLock lock;
  if (r->_tempObject) return;
  String dir, name = filename;
  int slash = max(name.lastIndexOf('/'), name.lastIndexOf('\\'));
  if (slash >= 0) name = name.substring(slash + 1);
  if (!index) {
    if (r->_tempFile) r->_tempFile.close();
    if (!adminTokenValid(requestArg(r, "t"))) return fmUploadFail(r, "Link expired");
    if (!sdReady || !fmPath(requestArg(r, "p"), dir) || fmReadOnly(dir) || !fmNameOk(name)) return fmUploadFail(r, "Bad target");
    if (!fmMkdirs(dir)) return fmUploadFail(r, "Could not create folder");
    String part = dir + "/" + name + ".part";
    SD_MMC.remove(part);
    r->_tempFile = SD_MMC.open(part, FILE_WRITE);
    if (!r->_tempFile) return fmUploadFail(r, "Could not open file");
  }
  if (!r->_tempFile) return;
  if (len && r->_tempFile.write(data, len) != len) {
    String part = r->_tempFile.path();
    fmUploadFail(r, "Write failed, card full?");
    SD_MMC.remove(part);
    return;
  }
  if (final) {
    String part = r->_tempFile.path();
    r->_tempFile.close();
    String target = part.substring(0, part.length() - 5);
    if (SD_MMC.exists(target)) SD_MMC.remove(target);
    if (!SD_MMC.rename(part, target)) { SD_MMC.remove(part); return fmUploadFail(r, "Could not finish file"); }
    event("SYSOP_FM_UPLOAD", target);
  }
}
static void fmUploadDone(AsyncWebServerRequest *r) {
  BbsLock lock;
  if (r->_tempFile) r->_tempFile.close();
  if (r->_tempObject) { r->send(400, "text/plain", (const char *)r->_tempObject); return; }
  if (!adminTokenValid(requestArg(r, "t"))) { r->send(403, "text/plain", "Link expired"); return; }
  r->send(200, "text/plain", "ok");
}

static void startSysopWeb() {
  // "/sysop" also matches "/sysop/...", so the page route goes last.
  http.on("/sysop/api/list", HTTP_GET, fmList);
  http.on("/sysop/api/get", HTTP_GET, fmGet);
  http.on("/sysop/api/mkdir", HTTP_POST, fmMkdir);
  http.on("/sysop/api/rename", HTTP_POST, fmRename);
  http.on("/sysop/api/delete", HTTP_POST, fmDelete);
  http.on("/sysop/api/upload", HTTP_POST, fmUploadDone, fmUploadChunk);
  http.on("/sysop", HTTP_GET, fmPage);
}
