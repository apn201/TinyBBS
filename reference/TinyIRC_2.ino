/*
  TinyIRC — ESP32-S3 standalone access-point chat node
  Hardware retained from the working build:
    ST7735 80x160: SCLK=10, MOSI=11, CS=12, DC=13, RST=14
    SD_MMC 1-bit: CLK=17, CMD=18, D0=16
    BOOT key: GPIO0

  Libraries (Library Manager): Arduino GFX Library
  Target: ESP32-S3 Dev Module
*/

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <FS.h>
#include <SD_MMC.h>
#include <Arduino_GFX_Library.h>

// ---------- Hardware / network ------------------------------------------------
static const int LCD_SCLK = 10;
static const int LCD_MOSI = 11;
static const int LCD_CS   = 12;
static const int LCD_DC   = 13;
static const int LCD_RST  = 14;
static const int SD_CLK   = 17;
static const int SD_CMD   = 18;
static const int SD_D0    = 16;
// ESP32 Arduino core 3.3.x already owns the name BOOT_PIN.
static const int UI_BOOT_PIN = 0;

static const char *AP_SSID = "TINY-IRC";
static const uint8_t AP_CHANNEL = 1;
static const uint8_t AP_MAX_CLIENTS = 8;  // association ceiling, not chat-history ceiling
static const char *ADMIN_USER = "admin";
static const char *ADMIN_PASSWORD = "apinakapina";
static const char *LOG_PATH = "/chat.txt";
static const char *TEMP_PATH = "/upload.tmp";

// Exact constructor settings retained from the known-working sketch.  The
// landscape orientation is selected with setRotation(1) during setup.
Arduino_DataBus *lcdBus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCLK, LCD_MOSI, GFX_NOT_DEFINED);
Arduino_GFX *gfx = new Arduino_ST7735(lcdBus, LCD_RST, 0, true, 80, 160, 26, 1, 26, 1);

WebServer server(80);
DNSServer dnsServer;

// ---------- Node state ---------------------------------------------------------
static bool sdReady = false;
static uint32_t messageId = 0;
static uint32_t messageCount = 0;
static String latestMessage = "AWAITING TRAFFIC";
static String latestNick = "SYSTEM";
static uint32_t lastMessageAt = 0;

static volatile uint32_t requestCount = 0;
static uint32_t requestCountLast = 0;
static uint16_t requestsPerSecond = 0;
static uint32_t lastRateTick = 0;
static uint32_t lastRequestAt = 0;

// The recent ring makes normal incremental polls RAM-only.  SD is read for a
// new browser's history or if it asks for messages older than this cache.
static const uint8_t RECENT_CAPACITY = 64;
static const size_t RECORD_BYTES = 286;
struct RecentRecord { uint32_t id; char text[RECORD_BYTES]; };
RecentRecord recent[RECENT_CAPACITY];
static uint8_t recentWrite = 0;
static uint8_t recentUsed = 0;

struct ScanRecord { String ssid; int32_t rssi; int32_t channel; uint8_t auth; };
static const uint8_t SCAN_CAPACITY = 6;
ScanRecord scanCache[SCAN_CAPACITY];
static uint8_t scanUsed = 0;
static bool scanInFlight = false;
static uint32_t lastScanStarted = 0;
static uint32_t lastScanFinished = 0;

enum DisplayMode : uint8_t { MODE_AUTO, MODE_RADIO, MODE_SYSTEM, MODE_HEX, MODE_CONNECT };
static DisplayMode displayMode = MODE_AUTO;
static uint32_t rxUntil = 0;
static uint32_t lastFrame = 0;
static uint32_t lastLCDPaint = 0;
static DisplayMode lastLCDMode = (DisplayMode)255;
static bool lastLCDWasRx = false;
static uint32_t lastButtonAt = 0;
static bool previousButton = true;

struct RateSlot {
  String address;
  uint32_t windowStarted;
  uint32_t lastPost;
  uint8_t posts;
};
static RateSlot rateSlots[16];

// ---------- Small utilities ----------------------------------------------------
static void countRequest() {
  requestCount++;
  lastRequestAt = millis();
}

static void addRecent(uint32_t id, const String &line) {
  recent[recentWrite].id = id;
  line.toCharArray(recent[recentWrite].text, RECORD_BYTES);
  recentWrite = (recentWrite + 1) % RECENT_CAPACITY;
  if (recentUsed < RECENT_CAPACITY) recentUsed++;
}

static uint8_t recentStart() {
  return (recentUsed < RECENT_CAPACITY) ? 0 : recentWrite;
}

static uint32_t recentOldestId() {
  return recentUsed ? recent[recentStart()].id : 0;
}

static String uptimeText() {
  uint32_t total = millis() / 1000;
  char b[16];
  snprintf(b, sizeof(b), "%02lu:%02lu:%02lu", total / 3600, (total / 60) % 60, total % 60);
  return String(b);
}

static String timeStamp() { return uptimeText(); }

static String cleanField(const String &in, size_t maximum, bool nickname) {
  String out;
  out.reserve(maximum);
  for (size_t i = 0; i < in.length() && out.length() < maximum; ++i) {
    uint8_t c = (uint8_t)in[i];
    if (c < 32 || c == '|' || c == '\\' || c == '\r' || c == '\n') {
      if (out.length() < maximum) out += ' ';
      continue;
    }
    // Preserve valid UTF-8 sequences (Finnish ä/ö included) as bytes.  The
    // old ASCII-only filter turned each UTF-8 byte into a question mark.
    if (c >= 0x80) {
      uint8_t width = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : (c >= 0xC0) ? 2 : 1;
      bool valid = width > 1 && i + width <= in.length() && out.length() + width <= maximum;
      for (uint8_t n = 1; valid && n < width; ++n) valid = (((uint8_t)in[i + n] & 0xC0) == 0x80);
      if (valid) { out += in.substring(i, i + width); i += width - 1; }
      else if (out.length() < maximum) out += '?';
      continue;
    }
    if (nickname && !(isalnum(c) || c == '_' || c == '-' || c == ' ')) continue;
    out += (char)c;
  }
  out.trim();
  return out;
}

static String clientTimestamp(const String &value) {
  // Date.prototype.toISOString(): 2026-10-29T12:34:56.789Z (UTC).
  // The node is intentionally offline, so the phone contributes real time.
  if (value.length() == 24 && value[4] == '-' && value[7] == '-' &&
      value[10] == 'T' && value[13] == ':' && value[16] == ':' &&
      value[19] == '.' && value[23] == 'Z') {
    for (uint8_t i = 0; i < 24; ++i) {
      if (i == 4 || i == 7 || i == 10 || i == 13 || i == 16 || i == 19 || i == 23) continue;
      if (value[i] < '0' || value[i] > '9') return String("UP/") + uptimeText();
    }
    return value;
  }
  return String("UP/") + uptimeText();
}

static bool postAllowed() {
  String address = server.client().remoteIP().toString();
  uint32_t now = millis();
  int chosen = -1;
  for (uint8_t i = 0; i < 16; ++i) {
    if (rateSlots[i].address == address) { chosen = i; break; }
    if (chosen < 0 && (rateSlots[i].address.length() == 0 || now - rateSlots[i].windowStarted > 30000)) chosen = i;
  }
  if (chosen < 0) chosen = 0;
  RateSlot &slot = rateSlots[chosen];
  if (slot.address != address || now - slot.windowStarted > 10000) {
    slot.address = address;
    slot.windowStarted = now;
    slot.posts = 0;
    slot.lastPost = 0;
  }
  if (slot.lastPost && now - slot.lastPost < 650) return false;
  if (slot.posts >= 6) return false;
  slot.posts++;
  slot.lastPost = now;
  return true;
}

static uint32_t idFromRecord(const String &line) {
  int bar = line.indexOf('|');
  if (bar < 1) return 0;
  return (uint32_t) strtoul(line.substring(0, bar).c_str(), nullptr, 10);
}

// Version 1 of TinyIRC stored plain text such as "Ada: hello".  Keep that
// archive readable without forcing the owner to convert or discard chat.txt.
static uint32_t normaliseRecord(const String &line, uint32_t &sequence, String &normal) {
  uint32_t id = idFromRecord(line);
  if (id) {
    sequence = max(sequence, id);
    normal = line;
    return id;
  }
  if (!line.length()) return 0;
  id = ++sequence;
  String nick = "ARCHIVE";
  String text = line;
  int colon = line.indexOf(": ");
  if (colon > 0) {
    nick = cleanField(line.substring(0, colon), 16, true);
    text = line.substring(colon + 2);
  }
  if (!nick.length()) nick = "ARCHIVE";
  normal = String(id) + "|ARCHIVE|" + nick + "|" + cleanField(text, 220, false);
  return id;
}

static void loadLogStats() {
  messageCount = 0;
  messageId = 0;
  recentWrite = 0;
  recentUsed = 0;
  if (!sdReady) return;
  File f = SD_MMC.open(LOG_PATH, FILE_READ);
  if (!f) return;
  uint32_t sequence = 0;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    String record;
    uint32_t id = normaliseRecord(line, sequence, record);
    if (!id) continue;
    messageId = max(messageId, id);
    messageCount++;
    addRecent(id, record);
    int a = record.indexOf('|');
    int b = record.indexOf('|', a + 1);
    int c = record.indexOf('|', b + 1);
    if (c > 0) {
      latestNick = record.substring(b + 1, c);
      latestMessage = record.substring(c + 1);
    }
  }
  f.close();
}

// ---------- HTTP message protocol ---------------------------------------------
// Response: !<current-last-id> then records, then #<cursor-sent>|<more:0/1>.
// A client keeps only its cursor; there is no HTTP session or held connection.
static void streamStart() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/plain; charset=utf-8", "");
  server.sendContent("!" + String(messageId) + "\n");
}

static void streamFinish(uint32_t cursor, bool more) {
  server.sendContent("#" + String(cursor) + "|" + (more ? "1" : "0") + "\n");
}

static void streamFromRecent(uint32_t after, uint16_t limit) {
  uint32_t cursor = after;
  uint16_t sent = 0;
  bool more = false;
  streamStart();
  uint8_t start = recentStart();
  for (uint8_t n = 0; n < recentUsed; ++n) {
    const RecentRecord &r = recent[(start + n) % RECENT_CAPACITY];
    if (r.id <= after) continue;
    if (sent >= limit) { more = true; break; }
    server.sendContent(String(r.text) + "\n");
    cursor = r.id;
    sent++;
  }
  streamFinish(cursor, more);
}

static void streamFromSD(uint32_t after, uint16_t limit) {
  uint32_t cursor = after;
  uint16_t sent = 0;
  bool more = false;
  streamStart();
  if (sdReady) {
    File f = SD_MMC.open(LOG_PATH, FILE_READ);
    if (f) {
      uint32_t sequence = 0;
      while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        String record;
        uint32_t id = normaliseRecord(line, sequence, record);
        if (!id || id <= after) continue;
        if (sent >= limit) { more = true; break; }
        server.sendContent(record + "\n");
        cursor = id;
        sent++;
      }
      f.close();
    }
  }
  streamFinish(cursor, more);
}

// A phone can retain the original TinyIRC page in its browser cache.  That
// page calls /messages with no cursor and expects plain visible text, not the
// efficient cursor protocol used by the replacement client.
static void streamLegacyPageMessages() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/plain; charset=utf-8", "");
  if (!sdReady) {
    uint8_t start = recentStart();
    for (uint8_t n = 0; n < recentUsed; ++n) {
      String record = recent[(start + n) % RECENT_CAPACITY].text;
      int a = record.indexOf('|'), b = record.indexOf('|', a + 1), c = record.indexOf('|', b + 1);
      server.sendContent(c > 0 ? record.substring(b + 1, c) + ": " + record.substring(c + 1) + "\n" : record + "\n");
    }
    return;
  }
  File f = SD_MMC.open(LOG_PATH, FILE_READ);
  if (!f) return;
  uint32_t sequence = 0;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    String record;
    if (!normaliseRecord(line, sequence, record)) continue;
    int a = record.indexOf('|'), b = record.indexOf('|', a + 1), c = record.indexOf('|', b + 1);
    server.sendContent(c > 0 ? record.substring(b + 1, c) + ": " + record.substring(c + 1) + "\n" : record + "\n");
  }
  f.close();
}

static void handleMessages() {
  countRequest();
  if (!server.hasArg("after")) { streamLegacyPageMessages(); return; }
  uint32_t after = (uint32_t) server.arg("after").toInt();
  uint16_t limit = (uint16_t) constrain(server.arg("limit").toInt(), 1, 180);
  if (!server.hasArg("limit")) limit = 90;
  // Exact or newer cursors are safely served by the ring.  Older requests get SD history.
  if (!sdReady || (recentUsed && after >= recentOldestId() && after <= messageId)) streamFromRecent(after, limit);
  else streamFromSD(after, limit);
}

static void handleSend() {
  countRequest();
  if (!postAllowed()) { server.send(429, "application/json", "{\"ok\":false,\"error\":\"slow down\"}"); return; }
  String nick = cleanField(server.hasArg("n") ? server.arg("n") : server.arg("nick"), 16, true);
  String text = cleanField(server.hasArg("m") ? server.arg("m") : server.arg("msg"), 220, false);
  if (!nick.length()) nick = "ANON";
  if (!text.length()) { server.send(400, "application/json", "{\"ok\":false,\"error\":\"empty message\"}"); return; }

  uint32_t id = ++messageId;
  String record = String(id) + "|" + clientTimestamp(server.arg("t")) + "|" + nick + "|" + text;
  bool persisted = false;
  if (sdReady) {
    File f = SD_MMC.open(LOG_PATH, FILE_APPEND);
    if (f && f.println(record)) persisted = true;
    if (f) f.close();
    if (!persisted) sdReady = false;
  }
  messageCount++;
  latestNick = nick;
  latestMessage = text;
  lastMessageAt = millis();
  rxUntil = millis() + 4200;
  addRecent(id, record);
  server.send(200, "application/json", String("{\"ok\":true,\"id\":") + id + ",\"persisted\":" + (persisted ? "true" : "false") + "}");
}

// ---------- Browser application ------------------------------------------------
static const char APP_PAGE[] PROGMEM = R"TINYHTML(
<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover"><meta name="theme-color" content="#060b0d"><title>TINY-IRC // NODE 01</title>
<style>
:root{--bg:#030708;--line:#246563;--cyan:#7afbf3;--dim:#76a8a3;--amber:#ffc65b;--red:#ff6475}*{box-sizing:border-box}html,body{margin:0;background:var(--bg);color:var(--cyan);font:15px/1.4 "Courier New",Courier,monospace;height:100%;overflow:hidden}body{height:100dvh;display:grid;grid-template-rows:auto minmax(0,1fr) auto;background-image:repeating-linear-gradient(0deg,rgba(100,255,245,.025) 0,rgba(100,255,245,.025) 1px,transparent 1px,transparent 4px);letter-spacing:.015em}header{padding:.72rem .85rem .58rem;border-bottom:2px solid var(--line);background:linear-gradient(90deg,#061315,#091416 75%,#0c2425);box-shadow:0 2px 18px #000}h1{font-family:"Courier New",Courier,monospace;font-size:clamp(16px,4.2vw,21px);letter-spacing:.12em;margin:0;color:var(--amber);text-shadow:0 0 12px #ffb300}#status{font-size:11px;color:var(--dim);margin-top:.32rem;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.sig{color:#83fff7}#stage{min-height:0;position:relative;border-left:1px solid rgba(80,255,241,.12);border-right:1px solid rgba(80,255,241,.12);margin:0 .25rem}#feed{height:100%;overflow-y:auto;overscroll-behavior:contain;padding:.35rem .78rem 1rem;scrollbar-color:var(--line) var(--bg)}.entry{padding:.48rem .1rem .42rem;border-bottom:1px dashed rgba(80,255,241,.17);overflow-wrap:anywhere;text-shadow:0 0 8px rgba(80,255,241,.12)}.stamp{color:var(--dim)}.nick{color:var(--amber);font-weight:bold}.system{color:#ffc65b}.hint{color:var(--dim);padding:1.2rem 0;text-align:center}#lost{display:none;position:absolute;z-index:5;left:.7rem;right:.7rem;bottom:.7rem;border:1px solid var(--red);background:rgba(22,4,8,.95);box-shadow:0 0 22px rgba(255,70,95,.25);padding:.65rem;color:#ffd7dc}#lost strong{color:var(--red)}#lost button{float:right}#composer{padding:.52rem .62rem calc(.6rem + env(safe-area-inset-bottom));border-top:2px solid var(--line);background:linear-gradient(90deg,#061113,#0a1b1d);display:grid;grid-template-columns:minmax(68px,22%) 1fr auto;gap:.4rem;position:relative;z-index:2}input,button{font-family:"Courier New",Courier,monospace!important;font-size:14px;border:1px solid var(--line);border-radius:0;background:#030c0d;color:var(--cyan);padding:.62rem .5rem;min-width:0}input::placeholder{color:#53817d}input:focus{outline:1px solid var(--cyan);box-shadow:0 0 10px rgba(80,255,241,.35)}button{color:var(--amber);font-weight:bold;cursor:pointer;background:#102628;border-color:#4d8e89}button:active{background:#1b4444}@media(min-width:700px){body{max-width:900px;margin:auto;border-left:1px solid var(--line);border-right:1px solid var(--line)}}
</style></head><body><header><h1>TINY-IRC <span class="sig">// NODE 01</span></h1><div id="status">INITIALISING RADIO LINK…</div></header><main id="stage"><section id="feed" aria-live="polite"><div class="hint">SYNCING PERSISTENT TRANSCRIPT…</div></section><aside id="lost"><strong>LINK LOST.</strong> Your loaded transcript is safe here. Rejoin the open Wi-Fi network <b>TINY-IRC</b>, then return to this page. <button id="retry" type="button">RETRY</button></aside></main><form id="composer" autocomplete="off"><input id="nick" maxlength="16" aria-label="handle" placeholder="HANDLE"><input id="message" maxlength="220" aria-label="message" placeholder="TRANSMIT MESSAGE" required><button id="send">SEND</button></form>
<script>
(()=>{'use strict';const feed=document.querySelector('#feed'),status=document.querySelector('#status'),lost=document.querySelector('#lost'),form=document.querySelector('#composer'),nick=document.querySelector('#nick'),msg=document.querySelector('#message'),send=document.querySelector('#send');
let cursor=0,busy=false,timer=0,errors=0,lastTouch=Date.now(),first=true;nick.value=localStorage.tinyIrcNick||'ANON';
const nearBottom=()=>feed.scrollHeight-feed.scrollTop-feed.clientHeight<48;const escText=(el,s)=>el.textContent=s;
function showLost(on,why){lost.style.display=on?'block':'none';if(why)status.textContent=why;}
function add(time,name,text,sys=false){const pin=nearBottom(),row=document.createElement('div'),a=document.createElement('span'),b=document.createElement('span');row.className='entry'+(sys?' system':'');a.className='stamp';a.textContent='['+time+'] ';b.className='nick';b.textContent=name+': ';row.append(a,b,document.createTextNode(text));feed.append(row);if(pin||first)feed.scrollTop=feed.scrollHeight;}
function delay(){if(document.hidden)return 12000;const idle=Date.now()-lastTouch;return idle>120000?5000:idle>30000?2500:900;}
function schedule(ms){clearTimeout(timer);timer=setTimeout(poll,ms===undefined?delay():ms);}
async function poll(){if(busy)return;busy=true;let timeout;try{const c=new AbortController;timeout=setTimeout(()=>c.abort(),7000);const r=await fetch('/messages?after='+cursor+'&limit=90&t='+Date.now(),{cache:'no-store',signal:c.signal});if(!r.ok)throw Error(r.status);const lines=(await r.text()).split('\n');let more=false,received=0;for(const line of lines){if(!line)continue;if(line[0]=='!'){const high=Number(line.slice(1));if(high<cursor){cursor=0;add('SYSTEM','LINK','SERVER LOG WAS ROTATED; RESYNCING',true);}continue;}if(line[0]=='#'){const p=line.slice(1).split('|');cursor=Number(p[0])||cursor;more=p[1]==='1';continue;}const p=line.split('|');if(p.length>=4){add(p[1],p[2],p.slice(3).join('|'));received++;}}
if(first){first=false;if(!received)add('SYSTEM','NODE','NO STORED TRAFFIC — YOU ARE THE FIRST SIGNAL',true);feed.scrollTop=feed.scrollHeight;}errors=0;showLost(false);status.textContent='LINK NOMINAL // CURSOR '+cursor+(document.hidden?' // STANDBY':'');schedule(more?40:undefined);
}catch(e){errors++;if(errors>=2)showLost(true,navigator.onLine?'LINK LOST // REJOIN TINY-IRC':'DEVICE OFFLINE // REJOIN TINY-IRC');schedule(Math.min(15000,1200*errors));}finally{clearTimeout(timeout);busy=false;}}
form.addEventListener('submit',async e=>{e.preventDefault();const text=msg.value.trim();if(!text)return;lastTouch=Date.now();localStorage.tinyIrcNick=nick.value.trim()||'ANON';send.disabled=true;try{const body=new URLSearchParams({n:nick.value,m:text,t:new Date().toISOString()});const r=await fetch('/send',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});if(!r.ok)throw Error(await r.text());msg.value='';poll();}catch(e){showLost(true,'TRANSMIT FAILED // REJOIN TINY-IRC');}finally{send.disabled=false;msg.focus();}});
['pointerdown','keydown','touchstart'].forEach(x=>document.addEventListener(x,()=>lastTouch=Date.now(),{passive:true}));document.addEventListener('visibilitychange',()=>schedule(document.hidden?12000:50));window.addEventListener('online',()=>schedule(50));document.querySelector('#retry').onclick=()=>{errors=0;schedule(10)};poll();
})();</script></body></html>
)TINYHTML";

static bool requireAdmin() {
  if (server.authenticate(ADMIN_USER, ADMIN_PASSWORD)) return true;
  server.requestAuthentication();
  return false;
}

static void handleRoot() {
  countRequest();
  // Ensure a phone receives the new cursor-aware UI after one refresh.
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  server.sendHeader("Pragma", "no-cache");
  server.send_P(200, "text/html; charset=utf-8", APP_PAGE);
}

static void handleStatus() {
  countRequest();
  String json = String("{\"clients\":") + WiFi.softAPgetStationNum() +
    ",\"messages\":" + messageCount + ",\"requests\":" + requestCount +
    ",\"heap\":" + ESP.getFreeHeap() + ",\"sd\":" + (sdReady ? "true" : "false") + "}";
  server.send(200, "application/json", json);
}

static void handleAdmin() {
  countRequest();
  if (!requireAdmin()) return;
  String p = F("<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'><style>body{background:#071012;color:#8ff;font:16px monospace;padding:1rem}button,input{padding:.6rem;margin:.3rem;background:#102;color:#8ff;border:1px solid #5cc}a{color:#fc6}</style><h2>TINY-IRC // ADMIN</h2>");
  p += "<p>SD: " + String(sdReady ? "ONLINE" : "UNAVAILABLE") + " &nbsp; records: " + String(messageCount) + " &nbsp; last id: " + String(messageId) + "</p>";
  p += F("<p><a href='/admin/download'>Download chat.txt</a></p><form method=POST action='/admin/clear' onsubmit=\"return confirm('Clear persistent log?')\"><button>Clear persistent log</button></form><form method=POST action='/admin/upload' enctype='multipart/form-data'><p>Replace log with uploaded chat.txt: <input type=file name=log accept='.txt'><button>Upload</button></p></form><p><a href='/'>Return to terminal</a></p>");
  server.send(200, "text/html", p);
}

static void handleDownload() {
  countRequest();
  if (!requireAdmin()) return;
  if (!sdReady) { server.send(503, "text/plain", "SD unavailable"); return; }
  File f = SD_MMC.open(LOG_PATH, FILE_READ);
  if (!f) { server.send(404, "text/plain", "No log"); return; }
  server.sendHeader("Content-Disposition", "attachment; filename=chat.txt");
  server.streamFile(f, "text/plain");
  f.close();
}

static void handleClear() {
  countRequest();
  if (!requireAdmin()) return;
  if (!sdReady) { server.send(503, "text/plain", "SD unavailable"); return; }
  SD_MMC.remove(LOG_PATH);
  File f = SD_MMC.open(LOG_PATH, FILE_WRITE);
  if (f) f.close();
  messageCount = 0;
  latestNick = "SYSTEM";
  latestMessage = "LOG CLEARED";
  recentUsed = 0;
  recentWrite = 0;
  server.sendHeader("Location", "/admin");
  server.send(303, "text/plain", "Log cleared");
}

static File uploadFile;
static bool uploadFailed = false;
static bool uploadAuthorized = false;
static size_t uploadBytes = 0;
static const size_t MAX_UPLOAD_BYTES = 1024 * 1024;
static void handleUploadData() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    // The upload handler receives chunks before the normal POST handler.  Check
    // authentication here too, and stage data in a temporary file so a failed
    // upload can never destroy a good persistent log.
    uploadAuthorized = server.authenticate(ADMIN_USER, ADMIN_PASSWORD);
    uploadFailed = !uploadAuthorized || !sdReady;
    uploadBytes = 0;
    if (!uploadFailed) {
      SD_MMC.remove(TEMP_PATH);
      uploadFile = SD_MMC.open(TEMP_PATH, FILE_WRITE);
      if (!uploadFile) uploadFailed = true;
    }
  } else if (up.status == UPLOAD_FILE_WRITE) {
    uploadBytes += up.currentSize;
    if (uploadBytes > MAX_UPLOAD_BYTES) uploadFailed = true;
    if (!uploadFailed && uploadFile.write(up.buf, up.currentSize) != up.currentSize) uploadFailed = true;
  } else if (up.status == UPLOAD_FILE_END || up.status == UPLOAD_FILE_ABORTED) {
    if (uploadFile) uploadFile.close();
    if (up.status == UPLOAD_FILE_ABORTED) uploadFailed = true;
  }
}
static void handleUploadDone() {
  countRequest();
  if (!requireAdmin() || !uploadAuthorized) {
    if (uploadFile) uploadFile.close();
    if (sdReady) SD_MMC.remove(TEMP_PATH);
    return;
  }
  if (uploadFailed || !SD_MMC.exists(TEMP_PATH)) {
    SD_MMC.remove(TEMP_PATH);
    server.send(500, "text/html", "Upload failed. Existing log was not changed. <a href='/admin'>Back</a>");
    return;
  }
  if (SD_MMC.exists(LOG_PATH)) SD_MMC.remove(LOG_PATH);
  if (!SD_MMC.rename(TEMP_PATH, LOG_PATH)) {
    server.send(500, "text/html", "Could not activate uploaded log. <a href='/admin'>Back</a>");
    return;
  }
  loadLogStats();
  server.sendHeader("Location", "/admin");
  server.send(303, "text/plain", "Log replaced");
}

static void handleNotFound() {
  countRequest();
  // Captive-portal probes and misspelled paths are sent to the chat client.
  server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
  server.send(302, "text/plain", "TINY-IRC captive portal");
}

// ---------- LCD ----------------------------------------------------------------
static const uint16_t C_BG = 0x0000, C_GRID = 0x1265, C_CYAN = 0x67FF, C_DIM = 0x3D8B;
static const uint16_t C_AMBER = 0xFE66, C_RED = 0xF8AA, C_WHITE = 0xFFFF;

static void textAt(int16_t x, int16_t y, const String &s, uint16_t colour = C_CYAN) {
  gfx->setTextColor(colour);
  gfx->setTextSize(1);
  gfx->setCursor(x, y);
  gfx->print(s);
}

static void terminalBase(const char *title) {
  gfx->fillScreen(C_BG);
  for (int y = 0; y < 80; y += 4) gfx->drawFastHLine(0, y, 160, C_GRID);
  gfx->drawRect(0, 0, 160, 80, C_CYAN);
  gfx->fillRect(1, 1, 158, 9, 0x020E);
  textAt(4, 2, title, C_AMBER);
  textAt(130, 2, uptimeText(), C_DIM);
}

static const char *authTag(uint8_t auth) {
  switch (auth) {
    case WIFI_AUTH_OPEN: return "OPEN";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA";
    case WIFI_AUTH_WPA2_PSK: return "W2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "W+2";
    case WIFI_AUTH_WPA3_PSK: return "W3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "2+3";
    default: return "SEC";
  }
}

static void drawRadio() {
  terminalBase("RF SCAN // CACHE");
  if (scanInFlight) textAt(4, 13, "SCANNING SPECTRUM...", C_AMBER);
  else textAt(4, 13, String("LAST ") + ((millis() - lastScanFinished) / 1000) + "s  / " + scanUsed + " SIGNALS", C_DIM);
  textAt(4, 23, "SSID       CH  RSSI AUTH", C_CYAN);
  for (uint8_t i = 0; i < scanUsed && i < 5; ++i) {
    String ssid = scanCache[i].ssid;
    if (ssid.length() > 10) ssid = ssid.substring(0, 10);
    char row[34];
    snprintf(row, sizeof(row), "%-10s %2ld %4ld %-3s", ssid.c_str(), (long)scanCache[i].channel, (long)scanCache[i].rssi, authTag(scanCache[i].auth));
    textAt(4, 32 + i * 8, String(row), scanCache[i].rssi > -60 ? C_CYAN : C_DIM);
  }
  if (!scanUsed && !scanInFlight) textAt(4, 39, "NO RF CACHE YET", C_DIM);
  textAt(4, 72, "AP CH " + String(AP_CHANNEL) + "  [BOOT] MODE", C_AMBER);
}

static void bar(int x, int y, int width, int value, int maximum, uint16_t colour) {
  gfx->drawRect(x, y, width, 5, C_DIM);
  int n = constrain(map(value, 0, maximum, 0, width - 2), 0, width - 2);
  if (n) gfx->fillRect(x + 1, y + 1, n, 3, colour);
}

static void drawSystem() {
  terminalBase("NODE TELEMETRY");
  uint16_t clients = WiFi.softAPgetStationNum();
  textAt(4, 14, "AP  TINY-IRC  OPEN", C_AMBER);
  textAt(4, 24, "IP  " + WiFi.softAPIP().toString() + "  CH " + AP_CHANNEL, C_CYAN);
  textAt(4, 34, "CLIENTS " + String(clients) + " / " + AP_MAX_CLIENTS + "   MSG " + String(messageCount), C_CYAN);
  textAt(4, 44, "REQ/s   " + String(requestsPerSecond) + "   ALL " + String(requestCount), C_DIM);
  textAt(4, 54, "HEAP " + String(ESP.getFreeHeap() / 1024) + "K / LOW " + String(ESP.getMinFreeHeap() / 1024) + "K", C_DIM);
  textAt(4, 64, String("SD ") + (sdReady ? "ONLINE" : "FAULT") + "  LOG# " + String(messageId), sdReady ? C_CYAN : C_RED);
  bar(106, 34, 48, clients, AP_MAX_CLIENTS, C_AMBER);
  bar(106, 44, 48, requestsPerSecond, 20, C_CYAN);
  bar(106, 54, 48, ESP.getFreeHeap() / 1024, 220, C_CYAN);
  textAt(4, 72, clients ? "NETWORK NOMINAL" : "AWAITING HANDSHAKE", C_AMBER);
}

static String hexBytes(uint8_t offset, uint8_t amount) {
  String out;
  for (uint8_t i = 0; i < amount; ++i) {
    uint8_t index = offset + i;
    if (index >= latestMessage.length()) break;
    char h[4];
    snprintf(h, sizeof(h), "%02X ", (uint8_t) latestMessage[index]);
    out += h;
  }
  return out;
}

static void drawHex() {
  terminalBase("RX MEMORY DUMP");
  textAt(4, 14, "SRC: " + latestNick + "  ID:" + String(messageId), C_AMBER);
  textAt(4, 25, "00  " + hexBytes(0, 8), C_CYAN);
  textAt(4, 35, "08  " + hexBytes(8, 8), C_CYAN);
  textAt(4, 45, "10  " + hexBytes(16, 8), C_CYAN);
  textAt(4, 55, "ASCII: " + latestMessage.substring(0, 19), C_DIM);
  uint32_t age = lastMessageAt ? (millis() - lastMessageAt) / 1000 : 0;
  textAt(4, 66, lastMessageAt ? ("PACKET AGE " + String(age) + "s") : "NO PACKETS IN BUFFER", C_AMBER);
  textAt(4, 72, "[BOOT] MODE / AUTO ROTATE", C_DIM);
}

static void drawConnect() {
  terminalBase("UNLICENSED RELAY");
  bool pulse = ((millis() / 450) & 1) == 0;
  textAt(18, 17, pulse ? "[  TINY-IRC  ]" : "  /TINY-IRC/  ", C_AMBER);
  textAt(16, 31, "JOIN OPEN WI-FI", C_CYAN);
  textAt(16, 42, "SSID: TINY-IRC", C_WHITE);
  textAt(16, 53, "OPEN ANY BROWSER", C_CYAN);
  uint16_t clients = WiFi.softAPgetStationNum();
  textAt(16, 66, clients ? (String("LINKED OPERATORS: ") + clients) : "NO PASSWORD // NO TRACE", C_DIM);
  textAt(16, 72, "BOOT: SCREEN MODE", C_AMBER);
}

static void drawRx() {
  terminalBase("<<< INCOMING SIGNAL >>>");
  uint32_t left = rxUntil > millis() ? rxUntil - millis() : 0;
  int width = map((int)left, 0, 4200, 0, 148);
  gfx->drawRect(5, 16, 150, 8, C_DIM);
  gfx->fillRect(6, 17, width, 6, C_AMBER);
  textAt(5, 31, "DECODED FROM: " + latestNick, C_AMBER);
  String shown = latestMessage;
  if (shown.length() > 24) shown = shown.substring(0, 24);
  textAt(5, 43, shown, C_CYAN);
  textAt(5, 55, "ID " + String(messageId) + "  BYTES " + String(latestMessage.length()), C_DIM);
  textAt(5, 67, hexBytes((millis() / 250) % max((int)latestMessage.length(), 1), 6), C_CYAN);
  textAt(5, 73, "RX COMPLETE / ARCHIVED TO SD", C_AMBER);
}

static DisplayMode activeMode() {
  if (displayMode != MODE_AUTO) return displayMode;
  if (WiFi.softAPgetStationNum() == 0) return MODE_CONNECT;
  return (DisplayMode)(MODE_RADIO + ((millis() / 6500) % 4));
}

static void renderLCD() {
  bool showingRx = millis() < rxUntil;
  DisplayMode mode = activeMode();
  // This display has no framebuffer: a full SPI repaint erases to black first.
  // Refresh stable pages slowly to avoid a painful visible strobe; RX remains
  // animated, but at a deliberately gentle rate.
  uint32_t interval = showingRx ? 650 : 2500;
  if (showingRx == lastLCDWasRx && mode == lastLCDMode && millis() - lastLCDPaint < interval) return;
  lastLCDPaint = millis();
  lastLCDMode = mode;
  lastLCDWasRx = showingRx;
  if (showingRx) { drawRx(); return; }
  switch (mode) {
    case MODE_RADIO: drawRadio(); break;
    case MODE_SYSTEM: drawSystem(); break;
    case MODE_HEX: drawHex(); break;
    case MODE_CONNECT: drawConnect(); break;
    default: drawConnect(); break;
  }
}

static void updateButton() {
  bool down = digitalRead(UI_BOOT_PIN) == LOW;
  if (down && previousButton && millis() - lastButtonAt > 220) {
    displayMode = (DisplayMode)((displayMode + 1) % 5);
    lastButtonAt = millis();
    rxUntil = 0;
  }
  previousButton = !down;
}

static void updateScan() {
  uint32_t now = millis();
  int result = WiFi.scanComplete();
  if (scanInFlight && result >= 0) {
    scanUsed = min((int)SCAN_CAPACITY, result);
    for (uint8_t i = 0; i < scanUsed; ++i) {
      scanCache[i].ssid = WiFi.SSID(i);
      scanCache[i].rssi = WiFi.RSSI(i);
      scanCache[i].channel = WiFi.channel(i);
      scanCache[i].auth = (uint8_t) WiFi.encryptionType(i);
    }
    WiFi.scanDelete();
    scanInFlight = false;
    lastScanFinished = now;
  }
  // A scan is asynchronous, cached, and postponed during fresh HTTP traffic so
  // it does not repeatedly steal airtime from the access point.
  if (!scanInFlight && now - lastScanStarted > 20000 && now - lastRequestAt > 1300) {
    WiFi.scanNetworks(true, true);
    scanInFlight = true;
    lastScanStarted = now;
  }
}

// ---------- Setup / main loop --------------------------------------------------
void setup() {
  pinMode(UI_BOOT_PIN, INPUT_PULLUP);
  Serial.begin(115200);
  gfx->begin();
  gfx->setRotation(1);  // 160x80 landscape; retained from the working display setup
  gfx->fillScreen(C_BG);
  textAt(8, 25, "TINY-IRC NODE 01", C_AMBER);
  textAt(8, 40, "BOOTING RADIO...", C_CYAN);

  SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0);
  sdReady = SD_MMC.begin("/sdcard", true);  // true = 1-bit bus
  if (sdReady) {
    File f = SD_MMC.open(LOG_PATH, FILE_APPEND);
    if (f) f.close();
    loadLogStats();
  }

  WiFi.mode(WIFI_AP_STA);  // STA side is used only for low-frequency RF scans.
  // nullptr password deliberately creates an OPEN access point.
  WiFi.softAP(AP_SSID, nullptr, AP_CHANNEL, false, AP_MAX_CLIENTS);
  dnsServer.start(53, "*", WiFi.softAPIP());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/messages", HTTP_GET, handleMessages);
  server.on("/send", HTTP_POST, handleSend);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/admin", HTTP_GET, handleAdmin);
  server.on("/admin/download", HTTP_GET, handleDownload);
  server.on("/admin/clear", HTTP_POST, handleClear);
  server.on("/admin/upload", HTTP_POST, handleUploadDone, handleUploadData);
  server.onNotFound(handleNotFound);
  server.begin();
  lastScanStarted = millis() - 19000; // get the first cache shortly after boot
  lastRateTick = millis();
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
  updateButton();
  updateScan();
  if (millis() - lastRateTick >= 1000) {
    requestsPerSecond = requestCount - requestCountLast;
    requestCountLast = requestCount;
    lastRateTick += 1000;
  }
  // ST7735 panels visibly flicker when a complete 160x80 frame is constantly
  // cleared over SPI.  Static terminal pages therefore update gently; only the
  // short incoming-packet animation refreshes more often.
  uint16_t frameInterval = (millis() < rxUntil) ? 220 : 1400;
  if (millis() - lastFrame >= frameInterval) {
    lastFrame = millis();
    renderLCD();
  }
}
