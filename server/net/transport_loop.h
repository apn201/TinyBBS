#pragma once
// Server transport lifecycle for phase 2. HTTP/WebSocket binding lands in
// phase 3 and POSIX Telnet binding in phase 4; keeping their configuration
// here makes main.cpp's BBS loop independent of either implementation.

#include <string>
#include <utility>
#include "net/http.h"

struct ServerTransportOptions {
  std::string http, web;
  std::string telnet;
};

class ServerTransports {
 public:
  explicit ServerTransports(ServerTransportOptions options) : options_(std::move(options)) {}

  bool start() {
    if (!http_.start(options_.http, options_.web)) return false;
    trace("HTTP_READY", options_.http.c_str());
    if (options_.telnet.empty()) trace("TELNET_OFF", "not configured");
    else {
      const size_t colon = options_.telnet.rfind(':');
      const std::string host = options_.telnet.substr(0, colon);
      const uint16_t port = static_cast<uint16_t>(std::stoul(options_.telnet.substr(colon + 1)));
      telnet.setBindAddress(host);
      if (!telnet.begin(port)) { http_.stop(); return false; }
      trace("TELNET_READY", options_.telnet.c_str());
    }
    return true;
  }

  // Called while BbsLock is held. Future transports put only queued socket
  // events here, just as the ESP32 WebSocket handler does; they never mutate
  // sessions or card state in a socket callback.
  void tickLocked() { if (!options_.telnet.empty()) tickTelnet(); }
  void tickWebSocket() { http_.tickWebSocket(); }
  void stop() { if (!options_.telnet.empty()) telnet.end(); http_.stop(); }

 private:
  ServerTransportOptions options_;
  ServerHttp http_;
};
