#pragma once
// Pins, limits and the few global objects (display, servers, SD state).
// Part of the single TinyBBS translation unit: included in order from main.cpp.

static const int LCD_SCLK = 10, LCD_MOSI = 11, LCD_CS = 12, LCD_DC = 13, LCD_RST = 14;
static const int SD_CLK = 17, SD_CMD = 18, SD_D0 = 16, UI_BOOT_PIN = 0;
// Eight active BBS nodes is the current, deliberately conservative target.
// Browser and Telnet callers share this one pool.
static const uint8_t MAX_NODES = 8;
static constexpr uint32_t LOGIN_TIMEOUT_MS = 120000;
static constexpr uint32_t WEB_CARRIER_TIMEOUT_MS = 180000;
static const char *ROOT = "/BBS";
static const char *MODEM_AUDIO = "/BBS/ASSETS/DIALUP.MP3";

Arduino_DataBus *lcdBus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCLK, LCD_MOSI, GFX_NOT_DEFINED);
Arduino_GFX *gfx = new Arduino_ST7735(lcdBus, LCD_RST, 0, true, 80, 160, 26, 1, 26, 1);
AsyncWebServer http(80);
AsyncWebSocket ws("/ws");
DNSServer dns;
WiFiServer telnet(23);
static bool sdReady = false;
static uint32_t bootAt = 0, callers = 0, nextUserId = 1, nextMessageId = 1;
static String storageFault;
static uint32_t lineCps = 2880;  // default modem speed for paced files: baud= in SYSTEM.INI / 10

// The async web server runs its handlers in its own task. Anything that
// touches sessions, users or the SD card holds this lock.
static SemaphoreHandle_t bbsMutex = nullptr;  // created first thing in setup()
struct BbsLock {
  BbsLock() { xSemaphoreTakeRecursive(bbsMutex, portMAX_DELAY); }
  ~BbsLock() { xSemaphoreGiveRecursive(bbsMutex); }
};
// WebSocket clients to hang up after the lock is released.
static std::vector<uint32_t> wsHangups;
// The two files every part of the board reads.
static constexpr const char *USERS_FILE = "/BBS/DATA/USERS/USERS.DAT";
static constexpr const char *MSG_FILE = "/BBS/DATA/MSGS/MESSAGES.DAT";
