# TinyBBS - notes for coding agents

## Workspace purpose

An offline ESP32-S3 BBS on a Pocket-Dongle-S3 USB stick. The experience is a
1990s ANSI BBS: browser and Telnet are two transports into the same BBS core,
not separate applications.

## Read first

1. `docs/ROADMAP.md` - ground rules and the slice plan. Work slice by slice;
   each slice ends with a test on the real board.
2. `README.md` - what exists now, technical overview and SD-card setup.
3. `docs/HARDWARE.md` - board, pins, display offsets, flashing and recovery.
4. `docs/CURATION.md` - archive policy and provenance.
5. `docs/MENU_AUDIT.md` - every screen, its keys and who gets them.
6. `docs/SPEC_1.0.md` - the original 1.0 specification. Where it disagrees
   with `docs/ROADMAP.md`, the roadmap wins.

## Firmware layout

`src/main.cpp` holds `setup()` and `loop()` and includes the modules in order.
The firmware is one translation unit split into headers, so a module may use
anything included before it.

| Folder | Contents |
| --- | --- |
| `src/core` | Globals, helpers, SD storage (`putRecord` for safe record rewrites), INI lookup, clock, users, message store, card message history, sessions, output pacing, the reader |
| `src/bbs` | Settings, menus, file areas (`files.h`), ZIP reading (`zip.h`), viewers (`viewers.h`), uploads, messages, SysOp tools, and one `cmd_*.h` per command area |
| `src/doors` | One header per door plus `doors.h`, the door table |
| `src/net` | Browser terminal page, HTTP routes, SysOp file manager, Telnet |
| `src/hw` | LCD and splash logo |

Adding a door: write `src/doors/<name>.h` with `enter`, `owns` and `handle`
functions, include it in `main.cpp` before `doors/doors.h`, and add one line
to the `DOORS` table.

Build: `pio run` (profile `esp32-s3-devkitc1-n16r8`, build folder
`.pio-tinybbs`). Flash with `pio run -t upload`, or the DIO recovery command
in `README.md`.

SD-card source content is in `sd/BBS/`. `tools/prepare-sd.ps1` copies it to a
card and creates the folders the board needs. Content that is not in the
repository is made by the scripts in `tools`: `fidonet-seed.py` (message
history), `fetch-modules.py` and `fetch-sids.py` (music areas). They share
`tools/bbscard.py`.

## Constraints

- Keep the BBS core transport-independent. Telnet and browser sessions stay
  adjacent nodes in the same session pool.
- AP-only. The board never joins another network.
- Content and settings belong on the SD card, not in firmware. Prefer
  discovering things from the card over hard-coding them.
- Never show a menu option that does not work for that caller on that
  transport.
- Do not format an SD card or expose SD paths to callers.
- Do not edit `/BBS/DATA`, `/BBS/LOGS` or `/BBS/TEMP` by hand.
- Preserve original archive names and FILE_ID.DIZ, DESC.SDI and DESCRIPT.ION
  data. `sd/BBS/FILES` and `sd/BBS/MSGBASE` are git-ignored and stay out of
  the repository.
- Kouvosto provenance lives in `/BBS/DATA/INDEX/KOUVOSTO_ORIGINS.json`, not in
  a caller-visible folder.

## Verification

PlatformIO is installed on the development PC and the firmware has been built
and flashed to the Pocket-Dongle-S3. A slice counts as done only after its
test in `docs/ROADMAP.md` passes on the board.
