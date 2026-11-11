# TinyBBS — Codex handoff

## Workspace purpose

This is an offline-first ESP32-S3 BBS project. The requested experience is a
genuine 1990s ANSI BBS: browser and Telnet are transports for the same BBS
core, not separate applications.

## Read first

1. `TinyBBS_READY_TO_CODE_SPEC.md` — the authoritative 1.0 target.
2. `README.md` — current implementation and SD-card setup.
3. `HARDWARE.md` — fixed, proven pin contract from TinyIRC.
4. `ROADMAP.md` — honest current boundary and intended sequencing.
5. `CURATION.md` — archive policy, provenance and Kouvosto area rules.

## Current implementation

- Firmware entry point: `src/main.cpp`.
- Build definition: `platformio.ini` for `esp32-s3-devkitc-1`.
- Frozen hardware reference: `reference/TinyIRC_2.ino`; preserve it unchanged.
- SD-card source content: `sd/BBS/`.
- SD-card preparation helper: `tools/prepare-sd.ps1`.
- `TinyBBS_SD_CARD_EMPTY_TREE.zip` is the supplied complete empty card tree.
- `_sd_tree/` is its extracted local reference; it is not generated runtime
  data and should not be treated as the deployed card.

The implemented vertical slice includes AP/captive DNS, browser polling
terminal transport, Telnet, up to eight nodes, login/new-user persistence,
salted SHA-256 password records, message posting/reading, basic conferences,
chat, WHO, SysOp page, logs, LCD status, and editable ANSI/ASCII presentation
assets.

Kouvosto is the working board name. Its selected archive files are placed in
native Kouvosto areas; source/capture metadata lives only in
`/BBS/DATA/INDEX/KOUVOSTO_ORIGINS.json`. Do not add a caller-visible MPOLI
folder.

## Important constraints

- Keep the BBS core transport-independent. Telnet and browser sessions must
  remain adjacent nodes in the same session manager.
- The board is deliberately AP-only at present. `ap_sta` in
  `sd/BBS/CONFIG/NETWORK.INI` is a future LAN/WAN seam, not permission to
  join an upstream network now.
- Do not silently format an SD card or expose SD paths to callers.
- Store replaceable presentation/content on SD, not embedded in firmware.
- Do not claim QWK, file transfers, approval queues, door games, WebSocket
  transport, or modem protocols are complete: those are next-stage work.
- Do not modify user-maintained archive/data directories by hand:
  `/BBS/DATA`, `/BBS/LOGS`, `/BBS/TEMP`, and `/BBS/QWK/WORK`.
- Preserve original archive names and FILE_ID.DIZ/DESC.SDI/DESCRIPT.ION data.
  Use the curator scripts instead of unpacking archives into file areas.

## Verification status

The repository and referenced SD assets were checked for presence on
2026-11-10. PlatformIO is not installed in this workspace environment, so
there is no on-device compile/flash confirmation yet. Install/use PlatformIO
before treating the firmware as hardware-verified.

## Recommended next work

Follow the sequence in `ROADMAP.md`: WebSocket/reconnect transport first,
then safe file-area transfers and ratios, QWK, door framework, physical
profiling, and only then AP+STA/LAN/WAN bridging.
