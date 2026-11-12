#pragma once
// The ESP32-S3 side of src/platform/platform.h: Arduino core, SD_MMC, WiFi,
// PSRAM and FreeRTOS.
#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>
#include <WiFi.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <mbedtls/sha256.h>

static inline const char *platformName() { return "esp32"; }
static inline uint32_t bbsRandom() { return esp_random(); }
// Big buffers (message history index, ANSI pictures) go to PSRAM.
static inline void *bigAlloc(size_t n) { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM); }
static inline void *bigRealloc(void *p, size_t n) { return ps_realloc(p, n); }
static inline bool bigMemory() { return psramFound(); }
// The async web server runs its handlers in its own task, so the core's one
// lock must be recursive and work across tasks.
struct PlatformMutex {
  SemaphoreHandle_t handle = nullptr;
  void init() { handle = xSemaphoreCreateRecursiveMutex(); }
  void lock() { xSemaphoreTakeRecursive(handle, portMAX_DELAY); }
  void unlock() { xSemaphoreGiveRecursive(handle); }
};
using NetClient = WiFiClient;
