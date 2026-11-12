#pragma once
// Uploads, browser only. A caller types a description, picks a file, and the
// file waits in /BBS/UPLOADS/INCOMING until the SysOp approves or deletes it.
// Approved files move into the area (and sub-folder) they were sent to, with
// the description added to that folder's FILES.BBS.
// Part of the single TinyBBS translation unit: included in order from main.cpp.

static constexpr const char *UPLOAD_DIR = "/BBS/UPLOADS/INCOMING";
static constexpr const char *UPLOAD_LOG = "/BBS/DATA/INDEX/UPLOADS.DAT";

// One line per change, the last line for an id wins:
// id|status|area folder|sub-folder|name|size|uploader id|handle|when|description
// status: P waiting, A approved, D deleted.
struct Upload {
  uint32_t id = 0, size = 0, userId = 0;
  char status = 'P';
  String folder, subPath, name, handle, when, desc;
};
static String uploadLine(const Upload &u) {
  return String(u.id) + "|" + u.status + "|" + enc(u.folder) + "|" + enc(u.subPath) + "|" + enc(u.name) + "|" + u.size + "|" +
         u.userId + "|" + enc(u.handle) + "|" + enc(u.when) + "|" + enc(u.desc);
}
static bool parseUpload(String line, Upload &u) {
  line.trim();
  if (!line.length()) return false;
  u.id = strtoul(field(line, 0).c_str(), nullptr, 10);
  String st = field(line, 1);
  u.status = st.length() ? st[0] : 'P';
  u.folder = dec(field(line, 2)); u.subPath = dec(field(line, 3)); u.name = dec(field(line, 4));
  u.size = strtoul(field(line, 5).c_str(), nullptr, 10);
  u.userId = strtoul(field(line, 6).c_str(), nullptr, 10);
  u.handle = dec(field(line, 7)); u.when = dec(field(line, 8)); u.desc = dec(field(line, 9));
  return u.id > 0;
}
// Every upload in its latest state, oldest first.
static std::vector<Upload> allUploads() {
  std::vector<Upload> list;
  File f = sdReady ? SD_MMC.open(UPLOAD_LOG, FILE_READ) : File();
  while (f && f.available()) {
    Upload u;
    if (!parseUpload(f.readStringUntil('\n'), u)) continue;
    bool found = false;
    for (auto &o : list) if (o.id == u.id) { o = u; found = true; break; }
    if (!found) list.push_back(u);
  }
  if (f) f.close();
  return list;
}
static std::vector<Upload> waitingUploads() {
  std::vector<Upload> out;
  for (auto &u : allUploads()) if (u.status == 'P') out.push_back(u);
  return out;
}
static size_t uploadsWaiting() { return waitingUploads().size(); }
static bool logUpload(const Upload &u) {
  File f = SD_MMC.open(UPLOAD_LOG, FILE_APPEND);
  if (!f) return false;
  f.print(uploadLine(u) + "\n");
  f.close();
  return true;
}
static String incomingPath(const Upload &u) { return String(UPLOAD_DIR) + "/" + u.id + "_" + u.name; }
// The caller's file name, made safe for the card: no path, plain characters.
static String uploadName(String name) {
  int slash = max(name.lastIndexOf('/'), name.lastIndexOf('\\'));
  if (slash >= 0) name = name.substring(slash + 1);
  String out;
  for (size_t i = 0; i < name.length() && out.length() < 40; ++i) {
    char c = name[i];
    out += isalnum((unsigned char)c) || c == '.' || c == '-' || c == '_' || c == '!' ? c : '_';
  }
  while (out.startsWith(".")) out.remove(0, 1);
  while (out.endsWith(".")) out.remove(out.length() - 1);
  return out;
}
static bool uploadsHere(const Session &s) {
  return s.web && board.uploadMax && s.fileArea < fileAreas.size() && fileAreas[s.fileArea].uploads && canUseArea(s, s.fileArea);
}

// ---------- the caller's side ------------------------------------------------------
static void startUpload(Session &s) {
  if (!uploadsHere(s)) { prompt(s, "Uploads are not open here. File command? "); return; }
  if (s.user.locked) { prompt(s, "Account locked. File command? "); return; }
  say(s, "\r\n\x1b[1;36mUpload to " + fileAreas[s.fileArea].name + (s.filePath.length() ? " / " + s.filePath : String()) +
         "\x1b[0m  (largest " + sizeText(board.uploadMax) + ")");
  say(s, "Describe the file in one line. A ZIP with a FILE_ID.DIZ can be left empty.");
  s.state = FILE_UPLOAD_DESC;
  prompt(s, "Description (Q cancels): ");
}
static void uploadDescription(Session &s, String line) {
  line.trim();
  if (up(line) == "Q") { listArea(s); return; }
  s.uploadDesc = oneLine(line, 120);
  s.uploadArea = s.fileArea; s.uploadPath = s.filePath;
  s.uploadTicket = salt();
  s.state = FILE_UPLOAD_WAIT;
  activity(s, "UPLOADING");
  write(s, "\x1b]9;UP;/upload?n=" + String(s.node) + "&q=" + s.token + "&u=" + s.uploadTicket + ";" + board.uploadMax + "\x07");
  say(s, "\x1b[1;36mTap CHOOSE FILE below the terminal and pick the file.\x1b[0m");
  prompt(s, "Waiting for your file. ENTER cancels: ");
}
static void cancelUpload(Session &s) {
  s.uploadTicket = "";
  write(s, "\x1b]9;UP;close\x07");
  say(s, "Upload cancelled.");
  listArea(s);
}
// Called from the web server once the file is on the card.
static void uploadArrived(Session &s, const String &part, const String &name, uint32_t size) {
  uint32_t id = 1;
  for (auto &u : allUploads()) if (u.id >= id) id = u.id + 1;
  Upload u;
  u.id = id; u.status = 'P'; u.folder = fileAreas[s.uploadArea].folder; u.subPath = s.uploadPath;
  u.name = name; u.size = size; u.userId = s.user.id; u.handle = s.user.handle; u.when = nowText(); u.desc = s.uploadDesc;
  String target = incomingPath(u);
  SD_MMC.remove(target);
  if (!SD_MMC.rename(part, target) || !logUpload(u)) {
    SD_MMC.remove(part);
    say(s, "\r\nThe upload could not be stored. Ask the SysOp.");
  } else {
    event("UPLOAD", s.user.handle + " " + name + " " + size);
    say(s, "\r\n\x1b[1;32mReceived " + name + " (" + sizeText(size) + ").\x1b[0m It waits for the SysOp to approve it.");
    for (auto &n : sessions)
      if (n.used && &n != &s && isSysop(n)) say(n, "\r\n\x1b[1;35m*** New upload waiting: " + name + " from " + s.user.handle + "\x1b[0m");
  }
  s.uploadTicket = "";
  s.fileArea = s.uploadArea; s.filePath = s.uploadPath;
  listArea(s);
}

// ---------- the SysOp's queue ------------------------------------------------------
static void uploadQueue(Session &s) {
  s.state = SYSOP_UPLOADS; activity(s, "UPLOAD QUEUE");
  auto list = waitingUploads();
  say(s, "\r\n\x1b[1;35mINCOMING UPLOADS\x1b[0m");
  if (list.empty()) say(s, "Nothing is waiting.");
  else say(s, "\x1b[0;36m  #  Name                  Size  From         Sent to\x1b[0m");
  for (const auto &u : list) {
    String to = u.folder + (u.subPath.length() ? "/" + u.subPath : String());
    char row[160];
    snprintf(row, sizeof(row), "\x1b[1;33m%3lu\x1b[0m  \x1b[1;37m%-20.20s\x1b[0m %6s  %-12.12s %.30s", (unsigned long)u.id, u.name.c_str(),
             sizeShort(u.size).c_str(), u.handle.c_str(), to.c_str());
    say(s, row);
    say(s, "     \x1b[0;37m" + (u.desc.length() ? u.desc.substring(0, 72) : String("(no description)")) + "\x1b[0m  \x1b[0;32m" + u.when + "\x1b[0m");
  }
  prompt(s, list.empty() ? "[Q] Back: " : "[A #] Approve  [D #] Delete  [Q] Back: ");
}
static bool findWaiting(uint32_t id, Upload &out) {
  for (auto &u : waitingUploads()) if (u.id == id) { out = u; return true; }
  return false;
}
static void approveUpload(Session &s, Upload u) {
  int area = -1;
  for (size_t i = 0; i < fileAreas.size(); ++i) if (fileAreas[i].folder == u.folder) area = i;
  String dir = String(FILES_ROOT) + "/" + u.folder + (u.subPath.length() ? "/" + u.subPath : String());
  if (area < 0 || !SD_MMC.exists(dir)) { say(s, "Its file area is gone. Delete it, or move it with the file manager."); return; }
  String target = dir + "/" + u.name;
  if (SD_MMC.exists(target)) { say(s, "A file named " + u.name + " is already there. Rename one with the file manager."); return; }
  if (!SD_MMC.rename(incomingPath(u), target)) { say(s, "Could not move the file."); return; }
  if (u.desc.length()) {
    File d = SD_MMC.open(dir + "/FILES.BBS", FILE_APPEND);
    if (d) { d.print(u.name + "  " + u.desc + "\r\n"); d.close(); }
  }
  u.status = 'A';
  logUpload(u);
  if (fileAreas[area].files < 60000) fileAreas[area].files++;
  // The uploader's counters and credit, saved even when they are not online.
  User who;
  if (findUserById(u.userId, who)) {
    who.uploadBytes = (uint32_t)min<uint64_t>((uint64_t)who.uploadBytes + u.size, UINT32_MAX);
    if (who.uploads != UINT16_MAX) who.uploads++;
    uint64_t earned = (uint64_t)(u.size / 1024) * board.creditsPerUploadKb;
    who.credits = (uint32_t)min<uint64_t>((uint64_t)who.credits + earned, UINT32_MAX);
    touchUser(who);
    refreshLiveUser(who);
    for (auto &n : sessions)
      if (n.used && n.user.id == who.id && &n != &s) say(n, "\r\n\x1b[1;32m*** Your upload " + u.name + " was approved by the SysOp.\x1b[0m");
  }
  event("UPLOAD_OK", s.user.handle + " " + u.name);
  say(s, "\x1b[1;32m" + u.name + " is now in " + fileAreas[area].name + ".\x1b[0m");
}
static void deleteUpload(Session &s, Upload u) {
  SD_MMC.remove(incomingPath(u));
  u.status = 'D';
  logUpload(u);
  event("UPLOAD_DEL", s.user.handle + " " + u.name);
  say(s, u.name + " deleted.");
}
static bool uploadCommand(Session &s, String &line, const String &cmd) {
  if (s.state == FILE_UPLOAD_DESC) { uploadDescription(s, line); return true; }
  if (s.state == FILE_UPLOAD_WAIT) { cancelUpload(s); return true; }
  if (s.state != SYSOP_UPLOADS) return false;
  if (!isSysop(s)) { mainMenu(s); return true; }
  if (cmd == "Q" || !cmd.length()) { sysopMenu(s); return true; }
  String verb = tokenAt(cmd, 0);
  uint32_t id = 0;
  Upload u;
  if ((verb == "A" || verb == "D") && readNumber(afterTokens(line, 1), id) && findWaiting(id, u)) {
    if (verb == "A") approveUpload(s, u); else deleteUpload(s, u);
  } else say(s, "Use A <number> or D <number>.");
  uploadQueue(s);
  return true;
}
