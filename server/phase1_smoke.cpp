// Phase 1 compatibility check. It intentionally starts only the shared core:
// transports come in later phases.
#include "platform/platform.h"
#include "bbs_core.h"

int main(int argc, char **argv) {
  if (argc < 2 || !tinybbsSetRoot(argv[1])) {
    std::fprintf(stderr, "Usage: %s <folder-containing-BBS> [handle password]\n", argv[0]);
    return 64;
  }
  sdReady = SD_MMC.begin();
  const String summary = bbsBegin();
  std::printf("SETTINGS: %s\n", summary.c_str());
  if (argc == 4) {
    User user;
    if (!findUser(up(String(argv[2])), user) || sha256hex(user.salt + ":" + String(argv[3])) != user.hash) {
      std::fprintf(stderr, "Login failed for %s\n", argv[2]);
      return 1;
    }
    std::printf("LOGIN OK: %s (#%lu)\n", user.handle.c_str(), static_cast<unsigned long>(user.id));
  }
  return sdReady ? 0 : 1;
}
