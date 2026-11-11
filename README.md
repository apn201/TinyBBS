# TinyBBS

TinyBBS is a local, offline-first BBS for an ESP32-S3. It creates its own WiFi
network. A phone can open `192.168.4.1` in a browser and use an ANSI terminal.
A terminal program can connect to the same board over Telnet on port 23.

The browser and Telnet are two ways into the same BBS. They use one shared
node pool, see the same caller list, chat, messages and SysOp pages. This is
not a web dashboard with a retro skin on it.

The working board name is Kouvosto.

## Current state

The first vertical slice is in the repository. It has:

- isolated WiFi AP with captive DNS, HTTP on port 80 and Telnet on port 23;
- eight shared browser and Telnet BBS nodes;
- browser terminal using HTTP polling and a real Telnet terminal path;
- account creation, returning logins, salted SHA-256 password records and
  persistent user records on SD;
- public and private messages, conferences, chat, WHO, SysOp page, caller
  and event logs, and a small LCD status screen;
- editable ANSI, ASCII, bulletin and configuration files on the SD card;
- browser-side modem audio after an explicit user click;
- paged ANSI and text viewing, and a browser-only download route for listed
  files that checks the current quota and ratio fields;
- separate native Legend of Kouvola and VolaMUD doors with persistent game
  data.

The browser transport is still polling. WebSocket reconnect, upload and
approval flow, Telnet transfer protocols, QWK, a general door framework and
AP plus station mode are not complete. The browser download route is not a
complete file-transfer subsystem and has not been profiled on physical
hardware.

The eight-node ceiling is a conservative firmware limit. It has not yet been
validated with eight active callers on the target board.

## Hardware and software

The target is `esp32-s3-devkitc-1` with an ST7735 display and a FAT32 SD card.
The pin contract is fixed from the earlier TinyIRC build. Read
[`HARDWARE.md`](HARDWARE.md) before changing any wiring.

Firmware entry point: `src/main.cpp`.

Frozen reference: `reference/TinyIRC_2.ino`. Do not modify it.

Build definitions: `platformio.ini`, `platformio.flash.ini` and
`platformio.mp.ini`.

The firmware uses the Arduino ESP32 framework and Arduino GFX Library. The
browser page, terminal renderer and most presentation assets are compiled
into firmware or copied from the SD source tree. BBS content and state belong
on the card.

## Start here

1. Install PlatformIO with the ESP32 platform and Arduino GFX Library
   available. A host build is not a hardware or flash confirmation.
2. Build and flash with `pio run -t upload`, or use
   `tools\\build-platformio.cmd` on the development PC.
3. Prepare a FAT32 SD card. Extract `TinyBBS_SD_CARD_EMPTY_TREE.zip` to the
   card root, then copy the repository `sd/BBS` tree into the card's `BBS`
   directory. Existing board data needs care. Do not overwrite it casually.
4. For a presentation-only update, run:

   ```powershell
   .\prepare-sd.ps1 -CardRoot X:\ -PresentationOnly
   ```

   Without `-PresentationOnly`, the script copies the whole repository
   `sd/BBS` tree. It does not format the card.
5. Insert the card and power the board. The default AP is `THE_BOARD` on
   channel 1 with no password. Browse to `http://192.168.4.1` and press DIAL,
   or connect a Telnet client to `192.168.4.1:23` with an 80-column terminal.
6. Create the first account. A handle named `SYSOP` becomes the bootstrap
   level-255 SysOp while no SysOp record exists. Change that password after
   the first login.

`sd/BBS/CONFIG/NETWORK.INI` controls the AP name, password, channel and WiFi
client limit. `mode=ap_sta` is reserved only. Version 0.1 always starts an
isolated AP and never joins an upstream network.

## SD card layout

`sd/BBS` is the source tree to place under `/BBS` on the card.

| Path | Purpose |
| --- | --- |
| `/BBS/ANSI`, `/BBS/ASCII`, `/BBS/BULLETINS`, `/BBS/ASSETS` | Replaceable caller-facing presentation and modem audio |
| `/BBS/CONFIG` | Board and content configuration |
| `/BBS/FILES` | Curated downloadable archive areas |
| `/BBS/DATA` | Runtime users, messages, door state and indexes |
| `/BBS/LOGS` | Caller and event logs |
| `/BBS/TEMP` | Runtime temporary files |
| `/BBS/UPLOADS` | Incoming material and staging |

Do not edit `/BBS/DATA`, `/BBS/LOGS`, `/BBS/TEMP` or `/BBS/QWK/WORK` by hand.
Do not expose SD paths to callers. Archive rules and provenance are in
[`CURATION.md`](CURATION.md).

The repository intentionally ignores `sd/BBS/FILES`, generated runtime
directories and PlatformIO build output. The archive stays on the SD card or
in its own managed storage, not in the firmware source repository.

## SysOp notes

At the main board, a level-255 account with the `SYSOP` flag can open the user
editor with `USER`, or `Z` then `U`. The editor can list users, inspect one
record, change security and flags, set quota, ratio or credits, lock or unlock
an account, add an account and delete it with confirmation.

The controls are visible in the terminal. They are not a substitute for a
finished approval queue or file-area manager.

Legend of Kouvola is TinyBBS's own native door. It is inspired by daily-turn
BBS RPG mechanics. It does not run, include or claim compatibility with an
original LORD distribution.

VolaMUD is a separate native room-based MUD door. It currently has persistent
characters, a small shared room map, directional movement, room chat and
presence. It is not a GoMud binary on the ESP32. See `VOLA_MUD.md` for its
source and licensing boundary.

## Repository map

| Path | Contents |
| --- | --- |
| `src/main.cpp` | Firmware, BBS core and both transports |
| `reference/TinyIRC_2.ino` | Unchanged hardware baseline |
| `sd/BBS` | SD-card source content and configuration |
| `tools` | SD preparation and archive-curation helpers |
| `TinyBBS_READY_TO_CODE_SPEC.md` | 1.0 target and acceptance criteria |
| `ROADMAP.md` | Honest boundary of the current build |
| `HARDWARE.md` | Wiring, display, SD and network contract |
| `CURATION.md` | Archive and provenance policy |

## Project boundaries

TinyBBS is AP-only at present. It does not silently format a card, connect to
an upstream WiFi network, or turn arbitrary caller input into filesystem
paths. Keep presentation assets on SD. Keep the BBS core independent of its
transport so a browser caller and a Telnet caller remain adjacent nodes.

The 1.0 target is described in `TinyBBS_READY_TO_CODE_SPEC.md`. Do not call
that target complete until its acceptance scenario has been run on hardware.
