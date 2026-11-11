/*
  TinyBBS - transport-independent offline BBS core.
  Hardware: Pocket-Dongle-S3 (ESP32-S3 N16R8, ST7735 LCD, microSD).
*/
#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <FS.h>
#include <SD_MMC.h>
#include <Arduino_GFX_Library.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <mbedtls/sha256.h>
#include <memory>
#include <vector>
#include "hw/scene_logo.h"
#include "net/web_terminal.h"
#include "bbs/cp437.h"

// The firmware is one translation unit split into module headers. Order
// matters: each module may use anything included before it.
#include "core/globals.h"
#include "core/util.h"
#include "core/storage.h"
#include "core/ini.h"
#include "core/users.h"
#include "core/messagebase.h"
#include "core/session.h"
#include "core/stream.h"
#include "bbs/settings.h"
#include "bbs/files.h"
#include "doors/volamud.h"
#include "doors/legend.h"
#include "doors/dice.h"
#include "doors/doors.h"
#include "bbs/sysop.h"
#include "bbs/board.h"
#include "bbs/messages.h"
#include "bbs/admin.h"
#include "bbs/cmd_login.h"
#include "bbs/cmd_main.h"
#include "bbs/cmd_sysop.h"
#include "bbs/cmd_files.h"
#include "bbs/cmd_messages.h"
#include "bbs/cmd_doors.h"
#include "bbs/commands.h"
#include "net/web.h"
#include "net/sysop_page.h"
#include "net/sysop_web.h"
#include "net/telnet.h"
#include "hw/lcd.h"

void setup() {
  bbsMutex = xSemaphoreCreateRecursiveMutex();
  pinMode(UI_BOOT_PIN, INPUT_PULLUP); Serial.begin(115200); delay(100); bootAt = millis(); trace("BOOT", String("reset_reason=") + (int)esp_reset_reason()); gfx->begin(); gfx->setRotation(1); drawLcdLogo(); lcdText(4, 64, "TINYBBS BOOTING", 0xFE66);
  SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0); sdReady = SD_MMC.begin("/sdcard", true);
  if (sdReady && prepareStorage()) { userCounters(); messageCounters(); }
  else { if (!storageFault.length()) storageFault = "storage setup failed"; sdReady = false; trace("SD_OFFLINE", storageFault); }
  trace("SETTINGS", loadSettings());
  // Network settings are read once here; they need a restart to change.
  Ini net;
  iniLoad("/BBS/CONFIG/NETWORK.INI", net);
  String ssid = iniGet(net, "NETWORK", "ssid", "THE_BOARD"), password = iniGet(net, "NETWORK", "password", "");
  constexpr int apLimit = ESP_WIFI_MAX_CONN_NUM < 17 - CONFIG_ESP_WIFI_ESPNOW_MAX_ENCRYPT_NUM
                        ? ESP_WIFI_MAX_CONN_NUM : 17 - CONFIG_ESP_WIFI_ESPNOW_MAX_ENCRYPT_NUM;
  int apClients = constrain(iniGet(net, "NETWORK", "max_wifi_clients", "10").toInt(), 1L, (long)apLimit);
  int channel = constrain(iniGet(net, "NETWORK", "channel", "1").toInt(), 1L, 13L);
  uint16_t telnetPort = constrain(iniGet(net, "NETWORK", "telnet_port", "23").toInt(), 1L, 65535L);
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(ssid.c_str(), password.length() >= 8 ? password.c_str() : nullptr, channel, false, apClients))
    trace("AP_FAIL", "could not start access point");
  if (password.length() && password.length() < 8) trace("AP_OPEN", "WiFi passwords need 8+ characters; starting an open network");
  dns.start(53, "*", WiFi.softAPIP());
  startWeb();
  startSysopWeb();
  telnet.begin(telnetPort); event("BOOT", "TinyBBS started"); trace("NETWORK_READY", WiFi.softAPIP().toString());
}
void loop() {
  dns.processNextRequest();
  {
    BbsLock lock;
    uint32_t now = millis();
    for (auto &s : sessions) if (s.used) pace(s, now);
    tickTelnet();
    // Take the time again: a session opened by tickTelnet() is newer than the
    // first reading, and the idle check below would wrap around and close it.
    now = millis();
    for (auto &s : sessions) {
      if (!s.used) continue;
      uint32_t idleLimit = s.authenticated ? 30UL * 60UL * 1000UL : LOGIN_TIMEOUT_MS;
      if (now - s.lastAt >= idleLimit || (s.web && now - s.lastPoll >= WEB_CARRIER_TIMEOUT_MS))
        closeSession(s);
    }
  }
  tickWebSocket();
  {
    BbsLock lock;
    drawLcd();
  }
  delay(1);
}
