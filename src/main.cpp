/*
  TinyBBS - transport-independent offline BBS core.
  Hardware: Pocket-Dongle-S3 (ESP32-S3 N16R8, ST7735 LCD, microSD).
*/
// The machine first, then the shared BBS core, then the ESP32-only parts:
// web and Telnet transports, WiFi and DNS, the LCD.
#include "platform/platform.h"
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <Arduino_GFX_Library.h>
#include <esp_wifi.h>
#include "bbs_core.h"

static const int SD_CLK = 17, SD_CMD = 18, SD_D0 = 16, UI_BOOT_PIN = 0;
DNSServer dns;
#include "hw/scene_logo.h"
#include "net/web_terminal.h"
#include "net/sysop_page.h"
#include "net/routes.h"
#include "net/esp_web.h"
#include "net/telnet.h"
#include "hw/lcd.h"

void setup() {
  pinMode(UI_BOOT_PIN, INPUT_PULLUP); Serial.begin(115200); delay(100); trace("BOOT", String("reset_reason=") + (int)esp_reset_reason()); gfx->begin(); gfx->setRotation(1); drawLcdLogo(); lcdText(4, 64, "TINYBBS BOOTING", 0xFE66);
  SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0); sdReady = SD_MMC.begin("/sdcard", true);
  trace("SETTINGS", bbsBegin());
  drawLcdLogo();  // again: the card may have its own logo
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
  telnet.begin(telnetPort); event("BOOT", "TinyBBS started"); trace("NETWORK_READY", WiFi.softAPIP().toString());
}
void loop() {
  dns.processNextRequest();
  {
    BbsLock lock;
    bbsTick(tickTelnet);
  }
  tickWebSocket();
  {
    BbsLock lock;
    drawLcd();
  }
  delay(1);
}
