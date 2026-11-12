#pragma once
// The machine the BBS runs on. The shared core (src/core, src/bbs, src/doors)
// uses only what this header provides: Arduino-style String, File, SD_MMC
// and millis(), plus the few calls below. The ESP32 build gets them from the
// Arduino core; the server build gets the same names from server/platform
// (see docs/APACHE_PORT.md). Keep it that way: no ESP32 or Linux calls in the
// shared core, so one change works on both.
//
//   uint32_t bbsRandom()                  hardware random number
//   void *bigAlloc(size_t)                big buffers (PSRAM on the ESP32)
//   void *bigRealloc(void *, size_t)
//   bool bigMemory()                      is there room for big buffers?
//   PlatformMutex                         recursive lock: init(), lock(), unlock()
//   NetClient                             a Telnet caller's socket (WiFiClient API)
//   platformName()                        "esp32" or "server", for logs
//   platformMaxNodes()                    compile-time session-pool capacity
//   platformSystemClock(uint32_t&)        host wall-clock time when available
#if defined(ARDUINO_ARCH_ESP32)
#include "esp32.h"
#else
#include <tinybbs_platform.h>  // server build: server/platform/tinybbs_platform.h
#endif
#include <memory>
#include <functional>
#include <algorithm>
#include <vector>
