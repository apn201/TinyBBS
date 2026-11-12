#pragma once
// The 160x80 status LCD.
// ESP32 only: not part of the shared core. Included from src/main.cpp.

// ---------- physical SysOp panel ------------------------------------------------
static const int LCD_SCLK = 10, LCD_MOSI = 11, LCD_CS = 12, LCD_DC = 13, LCD_RST = 14;
Arduino_DataBus *lcdBus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCLK, LCD_MOSI, GFX_NOT_DEFINED);
Arduino_GFX *gfx = new Arduino_ST7735(lcdBus, LCD_RST, 0, true, 80, 160, 26, 1, 26, 1);
static void lcdText(int x, int y, const String &s, uint16_t c) { gfx->setTextSize(1); gfx->setTextColor(c); gfx->setCursor(x, y); gfx->print(s); }
static void drawLcdLogo() {
  gfx->fillScreen(0);
  for (size_t y = 0; y < sizeof(kSceneLogo) / sizeof(kSceneLogo[0]); ++y) {
    for (size_t x = 0; x < sizeof(kSceneLogo[0]) - 1; ++x) {
      char pixel = kSceneLogo[y][x];
      uint8_t color = pixel <= '9' ? pixel - '0' : pixel - 'a' + 10;
      if (!color) continue;
      if (y < 25) gfx->fillRect(2 + x * 2, y * 2, 2, 2, kScenePalette[color]);
      else gfx->drawFastHLine(2 + x * 2, 50 + y - 25, 2, kScenePalette[color]);
    }
  }
}
static void drawLcd() {
  static uint32_t last = 0;
  if (millis() - last < 1000) return;
  last = millis();
  uint8_t active = 0;
  String call = "WAITING FOR CALL";
  for (auto &s : sessions) if (s.used) {
    active++;
    if (s.paged) call = "*** SYSOP PAGE N" + String(s.node) + " ***";
  }
  String statusLine = "NODE " + String(active) + "/" + board.maxNodes + "  SD " + (sdReady ? "OK" : "ERR");
  String ipLine = "IP " + WiFi.softAPIP().toString();
  static String previous;
  String panel = statusLine + "|" + ipLine + "|" + call;
  if (panel == previous) return;
  previous = panel;
  gfx->fillRect(0, 53, 160, 27, 0);
  gfx->drawFastHLine(0, 54, 160, 0xF81F);
  lcdText(4, 56, statusLine, sdReady ? 0x07FF : 0xF8AA);
  lcdText(4, 64, ipLine, 0xFFE0);
  lcdText(4, 72, call, call.startsWith("***") ? 0xF8AA : 0xF81F);
}
