#pragma once
// Viewing a file: text and ANSI in the reader, ZIP contents in the reader,
// pictures and music on a browser screen. N and P step through the folder.
// Part of the single TinyBBS translation unit: included in order from main.cpp.

static bool viewableName(const String &name) {
  String u = up(name);
  int dot = u.lastIndexOf('.');
  if (dot < 0) return true;  // FAQ, SITELIST: old text files had no extension
  String ext = u.substring(dot + 1);
  static const char *exts[] = {"ANS", "ANSI", "ASC", "NFO", "DIZ", "TXT", "ION", "SDI", "DOC", "ME", "1ST", "BAS", "LST", "C", "H", "PAS", "ASM", "BBS", "LOG", "FAQ", "LAW", "MAD", "MS", "OM"};
  for (const char *e : exts) if (ext == e) return true;
  return false;
}
static bool fileReaderKey(Session &s, const String &k);
static void fileReaderQuit(Session &s);

// ---------- browser screens: pictures and music -----------------------------------------
// The board names the file in an OSC message (tag IMG or MOD); the browser
// shows it over the terminal and sends keys back. N and P step to the next or
// previous file of the same kind, any other key goes back to the list.
static bool imageName(const String &name) {
  String u = up(name);
  return u.endsWith(".GIF") || u.endsWith(".JPG") || u.endsWith(".JPEG") || u.endsWith(".PNG") || u.endsWith(".BMP");
}
static bool sidName(const String &name) { return up(name).endsWith(".SID"); }
static bool musicName(const String &name) {
  String u = up(name);
  return u.endsWith(".MOD") || u.endsWith(".S3M") || u.endsWith(".XM") || u.endsWith(".IT") || sidName(name);
}
// The player for this kind of tune is on the card: libopenmpt, or jsSID for SIDs.
static bool playerReady(const String &name) {
  if (!sdReady || !SD_MMC.exists("/BBS/ASSETS/PLAYER/player.js")) return false;
  return SD_MMC.exists(sidName(name) ? "/BBS/ASSETS/PLAYER/jssid.sidplayer.js" : "/BBS/ASSETS/PLAYER/libopenmpt.wasm");
}
// The link the browser fetches the file from: only good for this node.
static String mediaLink(const Session &s, const char *route, uint16_t number) {
  return String(route) + "?n=" + s.node + "&q=" + s.token + "&i=" + number;
}
static void startMedia(Session &s, uint16_t number, const String &name, State state, const char *doing) {
  s.viewer.number = number;
  s.state = state;
  activity(s, String(doing) + clean(name, 30));
}
static bool openImage(Session &s, uint16_t number, const String &name, size_t bytes) {
  if (!s.web) return false;
  startMedia(s, number, name, FILE_IMAGE, "VIEWING ");
  write(s, "\x1b]9;IMG;" + mediaLink(s, "/view", number) + ";" + clean(name, 40) + ";" + sizeShort(bytes) + "\x07");
  return true;
}
static bool openMusic(Session &s, uint16_t number, const String &name, size_t bytes) {
  if (!s.web || !playerReady(name)) return false;
  startMedia(s, number, name, FILE_MUSIC, "LISTENING ");
  // The player shows the file's description too. ; would end the field.
  std::vector<std::pair<String, String>> want{{name, ""}};
  folderDescriptions(areaDir(s), want);
  String desc = want[0].second;
  desc.replace(';', ',');
  write(s, "\x1b]9;MOD;" + mediaLink(s, "/music", number) + ";" + clean(name, 40) + ";" + sizeShort(bytes) + ";" + desc + "\x07");
  return true;
}
static void mediaKey(Session &s, const String &bytes, bool (*kind)(const String &),
                     bool (*open)(Session &, uint16_t, const String &, size_t), const char *tag) {
  for (size_t i = 0; i < bytes.length(); ++i) {
    char c = toupper((unsigned char)bytes[i]);
    if (c == 0 || c == '\n' || c == ' ') continue;  // SPACE pauses music in the browser
    if (c != 'N' && c != 'P') { write(s, String("\x1b]9;") + tag + ";close\x07"); listArea(s); return; }
    auto list = folderEntries(s);
    int step = c == 'N' ? 1 : -1;
    for (int k = s.viewer.number + step; k >= 1 && k <= (int)list.size(); k += step) {
      if (list[k - 1].dir || !kind(list[k - 1].name)) continue;
      open(s, k, list[k - 1].name, list[k - 1].size);
      break;
    }
  }
}
static void imageKey(Session &s, const String &bytes) { mediaKey(s, bytes, imageName, openImage, "IMG"); }
static void musicKey(Session &s, const String &bytes) { mediaKey(s, bytes, musicName, openMusic, "MOD"); }

// ---------- ZIP contents in the reader ---------------------------------------------------
static bool openZipList(Session &s, uint16_t number, const String &path, const String &name, size_t bytes, bool quiet) {
  std::vector<String> rows;
  rows.push_back("\x1b[1;36m" + clean(name, 40) + "\x1b[0m  " + sizeShort(bytes));
  rows.push_back("");
  if (!zipContents(path, rows)) { if (!quiet) prompt(s, "Not a ZIP this board can read. File command? "); return false; }
  s.viewer.number = number;
  activity(s, "LISTING " + clean(name, 30));
  openRows(s, rows, clean(name, 24), "SPACE page  B back  \x18\x19 line  N/P file  Q quit", fileReaderQuit, fileReaderKey);
  return true;
}

// ---------- text and ANSI ------------------------------------------------------------------
static bool openFileViewer(Session &s, uint16_t number, bool quiet = false) {
  String path, name;
  size_t bytes;
  if (!areaFile(s, number, path, name, bytes)) { if (!quiet) prompt(s, "No such file. File command? "); return false; }
  if (zipName(name)) return openZipList(s, number, path, name, bytes, quiet);
  if (musicName(name)) {
    if (openMusic(s, number, name, bytes)) return true;
    if (!quiet) prompt(s, s.web ? "The player for this tune is not on the card. File command? " : "Music plays in the browser terminal. File command? ");
    return false;
  }
  if (imageName(name)) {
    if (openImage(s, number, name, bytes)) return true;
    if (!quiet) prompt(s, "Pictures open in the browser terminal. File command? ");
    return false;
  }
  String upper = up(name);
  const bool ansi = upper.endsWith(".ANS") || upper.endsWith(".ANSI");
  if (!viewableName(name) || !bytes || bytes > UINT32_MAX - 8300) {
    if (!quiet) prompt(s, s.web && fileAreas[s.fileArea].downloads ? "That file can only be downloaded: D " + String(number) + ". File command? "
                     : s.web ? String("That file cannot be viewed, and this area has no downloads. File command? ")
                             : String("That file can only be downloaded, from the browser terminal. File command? "));
    return false;
  }
  File file = SD_MMC.open(path, FILE_READ);
  if (!file) { if (!quiet) prompt(s, "File unavailable. File command? "); return false; }
  uint8_t sample[768];
  size_t got = file.read(sample, min(bytes, sizeof(sample)));
  if (memchr(sample, 0, got)) { file.close(); if (!quiet) prompt(s, "Binary file. File command? "); return false; }
  // Text that is mostly not letters is ASCII art: lines wider than the screen
  // are cut instead of wrapped, so the picture keeps its shape.
  size_t letters = 0, marks = 0;
  for (size_t i = 0; i < got; ++i) {
    if (isalpha(sample[i])) letters++;
    else if (sample[i] > ' ' && (sample[i] < 0x80 || sample[i] >= 0xc0)) marks++;
  }
  const bool artLike = upper.endsWith(".ASC") || marks > letters;
  s.viewer.number = number;
  const bool utf8 = sampleUtf8(sample, got);
  uint32_t start = got >= 3 && sample[0] == 0xef && sample[1] == 0xbb && sample[2] == 0xbf ? 3 : 0;  // UTF-8 BOM
  uint32_t end = bytes;
  // A SAUCE record (and its comment block) at the end is metadata, not text.
  if (bytes >= 128 && file.seek(bytes - 128) && file.read(sample, 128) == 128 && memcmp(sample, "SAUCE00", 7) == 0) {
    end -= 128;
    size_t comment = 5 + size_t(sample[104]) * 64;
    if (sample[104] && comment <= end && file.seek(end - comment) && file.read(sample, 5) == 5 && memcmp(sample, "COMNT", 5) == 0) end -= comment;
  }
  file.close();
  // Text and pictures open in the reader. Pictures play at once (any key
  // stops them in the reader); text starts on its first page.
  Reader &r = s.reader;
  r = Reader();
  r.path = path; r.start = start; r.end = end; r.utf8 = utf8;
  r.title = clean(name, 24);
  r.hint = "SPACE page  B back  \x18\x19 line  S play  N/P file  Q quit";
  r.onKey = fileReaderKey; r.onQuit = fileReaderQuit;
  if (ansi && s.ansi) { r.kind = READER_ANSI; renderAnsi(r); }
  else { r.kind = READER_TEXT; r.crop = artLike; indexText(r); }
  activity(s, (ansi ? "VIEWING " : "READING ") + clean(name, 30));
  openReader(s, ansi);
  return true;
}
// N and P in the viewer: the next or previous file in the folder that can be viewed.
static bool stepViewer(Session &s, bool forward) {
  auto list = folderEntries(s);
  int at = s.viewer.number;  // 1-based
  for (int i = forward ? at + 1 : at - 1; i >= 1 && i <= (int)list.size(); i += forward ? 1 : -1) {
    if (list[i - 1].dir || !(viewableName(list[i - 1].name) || zipName(list[i - 1].name))) continue;
    if (openFileViewer(s, i, true)) return true;
  }
  return false;
}

// ---------- the reader's file keys -----------------------------------------------------
// N and P open the next or previous viewable file; Q goes back to the list.
static bool fileReaderKey(Session &s, const String &k) {
  if (k != "n" && k != "N" && k != "p" && k != "P") return false;
  bool forward = k == "n" || k == "N";
  // openFileViewer() only touches the reader once a file opens.
  if (!stepViewer(s, forward)) showPage(s, forward ? "Last file in this folder" : "First file in this folder");
  return true;
}
static void fileReaderQuit(Session &s) { listArea(s); }
