#pragma once
// Limits and the core's few globals. Hardware (pins, display, servers) lives
// in src/hw, src/net and src/main.cpp, outside the shared core.
// Part of the shared core: included in order from src/bbs_core.h.

// Eight active BBS nodes is the current, deliberately conservative target.
// Browser and Telnet callers share this one pool.
static const uint8_t MAX_NODES = 8;
static constexpr uint32_t LOGIN_TIMEOUT_MS = 120000;
static constexpr uint32_t WEB_CARRIER_TIMEOUT_MS = 180000;
static const char *ROOT = "/BBS";
static const char *MODEM_AUDIO = "/BBS/ASSETS/DIALUP.MP3";

static bool sdReady = false;
static uint32_t bootAt = 0, callers = 0, nextUserId = 1, nextMessageId = 1;
static String storageFault;
static uint32_t lineCps = 2880;  // default modem speed for paced files: baud= in SYSTEM.INI / 10

// Web handlers can run in another task or thread. Anything that touches
// sessions, users or the card holds this lock.
static PlatformMutex bbsMutex;  // init() first thing at start-up
struct BbsLock {
  BbsLock() { bbsMutex.lock(); }
  ~BbsLock() { bbsMutex.unlock(); }
};
// WebSocket clients to hang up after the lock is released.
static std::vector<uint32_t> wsHangups;
// The two files every part of the board reads.
static constexpr const char *USERS_FILE = "/BBS/DATA/USERS/USERS.DAT";
static constexpr const char *MSG_FILE = "/BBS/DATA/MSGS/MESSAGES.DAT";
