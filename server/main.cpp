// Phase 2 Linux process: mount the shared /BBS tree, start the shared BBS
// core, and keep its tick running. Network bindings are deliberately split
// into later phases so the core remains transport-independent.
#include "platform/platform.h"
#include "bbs_core.h"
#include "net/transport_loop.h"

#include <csignal>
#include <cstdio>
#include <string>

namespace {
volatile std::sig_atomic_t stopRequested = 0;

void requestStop(int) { stopRequested = 1; }

struct Options {
  std::string root = "/srv/tinybbs";
  std::string http = "127.0.0.1:8080";
  std::string telnet;
};

void usage(const char *program) {
  std::printf("Usage: %s [--root <folder-containing-BBS>] [--http <address:port>] [--telnet <address:port>]\n", program);
}

bool endpointIsValid(const std::string &endpoint) {
  const size_t colon = endpoint.rfind(':');
  if (colon == std::string::npos || colon == 0 || colon + 1 == endpoint.size()) return false;
  unsigned long port = 0;
  for (size_t at = colon + 1; at < endpoint.size(); ++at) {
    const char c = endpoint[at];
    if (c < '0' || c > '9') return false;
    port = port * 10 + static_cast<unsigned long>(c - '0');
    if (port > 65535) return false;
  }
  return port > 0;
}

bool parseOptions(int argc, char **argv, Options &options, bool &showHelp) {
  for (int at = 1; at < argc; ++at) {
    const std::string argument = argv[at];
    if (argument == "--help" || argument == "-h") { usage(argv[0]); showHelp = true; return false; }
    if ((argument == "--root" || argument == "--http" || argument == "--telnet") && at + 1 < argc) {
      std::string value = argv[++at];
      if (argument == "--root") options.root = std::move(value);
      else if (argument == "--http") options.http = std::move(value);
      else options.telnet = std::move(value);
      continue;
    }
    std::fprintf(stderr, "Unknown or incomplete option: %s\n", argument.c_str());
    usage(argv[0]);
    return false;
  }
  if (!endpointIsValid(options.http) || (!options.telnet.empty() && !endpointIsValid(options.telnet))) {
    std::fprintf(stderr, "Endpoints must be address:port with a port from 1 to 65535.\n");
    return false;
  }
  return true;
}
}  // namespace

int main(int argc, char **argv) {
  Options options;
  bool showHelp = false;
  if (!parseOptions(argc, argv, options, showHelp)) return showHelp ? 0 : 64;

  Serial.begin(115200);
  if (!tinybbsSetRoot(options.root)) {
    std::fprintf(stderr, "TinyBBS needs a folder containing BBS/: %s\n", options.root.c_str());
    return 1;
  }
  sdReady = SD_MMC.begin();
  const String settings = bbsBegin();
  if (!sdReady) {
    std::fprintf(stderr, "TinyBBS could not prepare BBS storage: %s\n", storageFault.c_str());
    return 1;
  }

  trace("SERVER_START", String("root=") + options.root.c_str() + " platform=" + platformName());
  trace("SETTINGS", settings);
  if (!clockSyncSystem)
    trace("CLOCK_HINT", "set clock_sync=system in BBS/CONFIG/SYSTEM.INI for host time");

  ServerTransports transports({options.http, options.telnet});
  transports.start();
  event("BOOT", "TinyBBS server started");

  std::signal(SIGINT, requestStop);
  std::signal(SIGTERM, requestStop);
  while (!stopRequested) {
    {
      BbsLock lock;
      bbsTick([&transports]() { transports.tickLocked(); });
    }
    delay(2);
  }
  trace("SERVER_STOP", "signal received");
  return 0;
}
