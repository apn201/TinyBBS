#pragma once
// Admin tokens for the SysOp file manager.
// Part of the single TinyBBS translation unit: included in order from main.cpp.
//
// The terminal issues a token to a logged-in SysOp. The token belongs to the
// user account, not to the terminal session, so it survives the phone putting
// the terminal tab to sleep. Every use re-checks that the account is still a
// level-255 SysOp and slides the 30-minute expiry forward.

struct AdminToken { String token; uint32_t userId = 0; uint32_t expires = 0; };
static AdminToken adminTokens[2];
static constexpr uint32_t ADMIN_TOKEN_MS = 30UL * 60UL * 1000UL;

static bool adminTokenLive(const AdminToken &t) { return t.token.length() && (int32_t)(t.expires - millis()) > 0; }

static String issueAdminToken(const User &u) {
  AdminToken *slot = &adminTokens[0];
  for (auto &t : adminTokens) {
    if (t.userId == u.id || !adminTokenLive(t)) { slot = &t; break; }
    if ((int32_t)(t.expires - slot->expires) < 0) slot = &t;
  }
  slot->token = salt() + salt();
  slot->userId = u.id;
  slot->expires = millis() + ADMIN_TOKEN_MS;
  event("SYSOP_FILEMANAGER", u.handle);
  return slot->token;
}

static bool adminTokenValid(const String &token) {
  if (token.length() < 32) return false;
  for (auto &t : adminTokens) {
    if (!adminTokenLive(t) || t.token != token) continue;
    User u;
    if (findUserById(t.userId, u) && !u.locked && u.level == 255 && hasFlag(u, "SYSOP")) {
      t.expires = millis() + ADMIN_TOKEN_MS;
      return true;
    }
    t = AdminToken();
  }
  return false;
}
