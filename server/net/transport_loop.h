#pragma once
// Server transport lifecycle for phase 2. HTTP/WebSocket binding lands in
// phase 3 and POSIX Telnet binding in phase 4; keeping their configuration
// here makes main.cpp's BBS loop independent of either implementation.

#include <string>
#include <utility>
#include "net/http.h"

struct ServerTransportOptions {
  std::string http;
  std::string telnet;
};

class ServerTransports {
 public:
  explicit ServerTransports(ServerTransportOptions options) : options_(std::move(options)) {}

  bool start() {
    if (!http_.start(options_.http)) return false;
    trace("HTTP_READY", options_.http.c_str());
    if (options_.telnet.empty()) trace("TELNET_OFF", "not configured");
    else trace("TELNET_PENDING", String(options_.telnet.c_str()) + " (binding in phase 4)");
    return true;
  }

  // Called while BbsLock is held. Future transports put only queued socket
  // events here, just as the ESP32 WebSocket handler does; they never mutate
  // sessions or card state in a socket callback.
  void tickLocked() {}
  void tickWebSocket() { http_.tickWebSocket(); }
  void stop() { http_.stop(); }

 private:
  ServerTransportOptions options_;
  ServerHttp http_;
};
