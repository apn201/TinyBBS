# TinyBBS

A 1990s-style BBS running on a $10 ESP32-S3 board. No Internet, no modem, no
phone line. The board makes its own WiFi network and you call it from a phone
browser or a Telnet client.

The working board name is Kouvosto.

## Why

This is an experiment on what can be done with a $10 ESP32 board with no
GPIO. The first iteration was TinyIRC, a local IRC server. Now it is a local
BBS, completely isolated from the Internet.

I tried sticking it to a USB charger and plugging it in the toilet of a bar,
creating the SSID. Nobody joined.

It is a nod to my history with BBSes before the Internet. The board only
contains some example content, but it should show how advanced and arty the
scene already was in the 1990s. Maybe even more than now.

Language was rough, art was questionable, rules were not yet made and sysops
were dickheads. Yet it was still mostly more civil than today's social media.
And people discussed important things, like religion and UFO cow mutilations.

In the secret sections bad scans from Playboy were shared, and images drawn
by Hajime Sorayama. Text files often contained "secret documents" from CIA
projects, UFO reports, song lyrics and so on.

It was not the Internet, but almost. You could download shareware and PD
games, device drivers etc. And play door games like LORD.

TinyBBS has the same parts in small: ANSI screens, message conferences, a
back room for trusted callers, file areas with ratios, a SysOp who can lock
you out, and two door games. Browser and Telnet callers are on the same
board at the same time and see each other in WHO and chat.

## What it does now

- Opens an open WiFi access point, `THE_BOARD` by default. Phones get a
  captive portal that lands on the terminal.
- Browser terminal at `http://192.168.4.1`: press DIAL, get a node.
- Telnet on port 23 for PuTTY, SyncTERM and friends. 80 columns.
- Eight nodes, shared by browser and Telnet callers.
- New-user signup, logins, security levels and flags.
- Public and private messages in four conferences, one of them invite-only
  and one for the SysOp.
- Multi-node chat, WHO, page the SysOp, bulletins, ANSI gallery.
- File areas with paged ANSI and text viewing, and browser downloads that
  check quota and ratio.
- SysOp user editor in the terminal.
- Two native doors: Legend of Kouvola and VolaMUD.
- Optional dial-up sound in the browser, after you click for it.
- A small LCD on the board shows the logo, SD state, IP and the last caller.

## Technical details

### Hardware

| Part | Detail |
| --- | --- |
| MCU | ESP32-S3, board profile `esp32-s3-devkitc1-n16r8` (16 MB flash, 8 MB PSRAM) |
| Display | ST7735, 80x160 native, used as 160x80 landscape |
| Storage | microSD over SD_MMC, 1-bit mode, FAT32 |
| Input | BOOT button only |
| Network | 2.4 GHz soft AP, channel 1 by default |
| Power | USB |

Nothing else is attached. No modem, no keyboard, no audio out. Pins and
display offsets are in [`HARDWARE.md`](HARDWARE.md).

### Firmware layout

The firmware is Arduino on ESP32, one main file of about 2000 lines:

| File | Contents |
| --- | --- |
| `src/main.cpp` | BBS core, storage, doors, HTTP and Telnet transports, LCD |
| `src/web_terminal.h` | The browser terminal page, compiled into flash |
| `src/file_viewer.h` | CP437 to UTF-8 conversion for archive ANSI |
| `src/scene_logo.h` | LCD splash logo as DOS palette indexes |

Libraries: Arduino GFX Library, the core `WebServer`, `DNSServer`, `SD_MMC`
and mbedTLS for SHA-256. There are no extra RTOS tasks. `loop()` serves DNS,
serves HTTP, polls Telnet, expires idle sessions and refreshes the LCD once a
second.

### One BBS, two transports

The core does not know how a caller connected. There is a fixed pool of
eight `Session` structs. Each has a state machine (about 40 states, from
login prompt to door combat), an input line and an output buffer capped at
14 KB. A transport only pushes bytes into `input()` and drains the output
buffer.

- **Telnet** uses a `WiFiServer` on port 23. A small IAC state machine strips
  option negotiation and answers WILL and DO with refusals, except ECHO and
  SUPPRESS-GO-AHEAD. CR, LF and CRLF all end a line, so PuTTY and raw
  clients behave the same.
- **Browser** loads one page from flash. It draws an 80x25 terminal on a
  640x400 canvas with 8x16 cells, the 16-colour DOS palette and an ANSI
  parser in JavaScript. The carrier is plain HTTP polling: `POST
  /api/connect` returns a node number and a random token, `GET /api/out`
  drains output (204 when there is nothing), `POST /api/in` sends keys.
  An `X-BBS-Password` header tells the page to hide input while a password
  is typed. WebSocket transport is not done yet.

Because both sides share the same session pool, a Telnet caller and a phone
caller can chat, duel in the door game and see each other in WHO.

Timeouts: two minutes at the login prompt, three minutes for a browser that
stops polling, 30 minutes idle after login.

### Captive portal

A `DNSServer` answers every name with the AP address and any unknown HTTP
path redirects to `/`. Phones detect the portal and open the terminal
without the caller typing an address.

### Storage

Everything lives as plain text on the FAT32 card under `/BBS`, so the SysOp
can open it on a PC.

- Users and messages are pipe-delimited lines with escaping.
  `USERS/USERS.DAT` and `MSGS/MESSAGES.DAT` under `/BBS/DATA`.
- Messages are append-only.
- User updates and door saves are written to a `.NEW` file first and then
  swapped in, with a `.BAK` for recovery.
- Per-user last-read pointers are small files in `DATA/STATE`.
- Passwords are salted SHA-256. The salt comes from the hardware RNG.
- There is no real-time clock and the board never sees NTP, so timestamps are
  uptime, `UP hh:mm:ss`.
- ANSI screens, bulletins, menus, config and the modem sound are all files
  on the card. Change the look without reflashing.
- The firmware creates its runtime folders if they are missing. It never
  formats the card.

Archive ANSI is CP437. The server converts it to UTF-8 for both transports,
so box drawing and shading survive in a modern Telnet client and in the
browser.

### Doors

**Legend of Kouvola** is a daily-turn style RPG written for TinyBBS: forest
fights, tavern rumours, bank, weapon and armour shop, dice, a leaderboard,
and live duels between two callers on different nodes. There is no clock, so
when forest fights run out you ask the SysOp for more. It does not run or
include the original LORD.

**VolaMUD** is a small room-based MUD with persistent characters, shared
rooms, movement, room chat and a training fight. The world is a C++ port of
the GoMud tutorial zone. See [`VOLA_MUD.md`](VOLA_MUD.md) and
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

### LCD

The 160x80 screen draws the Kouvosto logo from palette-indexed rows, then
three status lines: SD state and node count, AP IP address and the latest
caller or page.

## Getting started

1. Install PlatformIO with the Espressif32 platform.
2. Build and flash: `pio run -t upload`. On Windows,
   `tools\build-platformio.cmd` does the same.
3. Format a microSD card as FAT32. Extract `TinyBBS_SD_CARD_EMPTY_TREE.zip`
   to the card root, then copy this repository's `sd/BBS` into the card's
   `BBS` folder. Or run:

   ```powershell
   .\tools\prepare-sd.ps1 -CardRoot X:\
   ```

   Add `-PresentationOnly` to update only screens and config on a card that
   already has users and messages. The script does not format the card.
4. Insert the card and power the board from USB.
5. Join `THE_BOARD`. Press DIAL in the browser, or Telnet to
   `192.168.4.1:23`.
6. Create a user named `SYSOP` first. While no SysOp exists, that handle
   becomes the level-255 SysOp. Change its password.

AP name, password, channel and WiFi client limit are in
`sd/BBS/CONFIG/NETWORK.INI`. Board name and default levels are in
`SYSTEM.INI` next to it.

## SD card layout

| Path | Purpose |
| --- | --- |
| `/BBS/ANSI`, `/BBS/ASCII`, `/BBS/BULLETINS`, `/BBS/ASSETS` | Screens, art and modem audio |
| `/BBS/CONFIG` | Board, conference, file area and network config |
| `/BBS/FILES` | Downloadable file areas |
| `/BBS/DATA` | Users, messages, door saves, indexes |
| `/BBS/LOGS` | Caller and event logs |
| `/BBS/TEMP`, `/BBS/UPLOADS` | Runtime temp and upload staging |

Do not edit `DATA`, `LOGS` or `TEMP` by hand while the board is in use.
Callers never see SD paths.

The file archive itself is not in this repository. `sd/BBS/FILES` is ignored
by git. Archive rules are in [`CURATION.md`](CURATION.md).

## SysOp

A level-255 account with the `SYSOP` flag opens the user editor with `USER`
or `Z` then `U`. It lists users and can change security level, flags, quota,
ratio and credits, lock or unlock an account, add one, and delete one after
confirmation.

## Limits

- The browser carrier is polling. WebSocket and reconnect are not done.
- Uploads, the SysOp approval queue, Telnet file transfers (no ZMODEM yet)
  and QWK are not done.
- Eight nodes is a firmware limit. It has not been tested with eight real
  callers on the board.
- AP only. `mode=ap_sta` in `NETWORK.INI` is reserved and ignored. The board
  does not join any other network.

More in [`ROADMAP.md`](ROADMAP.md). The full 1.0 target is in
[`TinyBBS_READY_TO_CODE_SPEC.md`](TinyBBS_READY_TO_CODE_SPEC.md).

## Repository map

| Path | Contents |
| --- | --- |
| `src/` | Firmware |
| `sd/BBS/` | SD card content and config |
| `tools/` | SD preparation and archive curation scripts |
| `HARDWARE.md` | Pins, display, SD and network details |
| `ROADMAP.md` | What is done and what is next |
| `CURATION.md` | Archive and provenance policy |
| `VOLA_MUD.md` | VolaMUD notes |
| `TinyBBS_READY_TO_CODE_SPEC.md` | 1.0 specification |

## Credits

Dial-up sound: [Oona Raisanen, "The sound of the dialup, pictured"](https://www.windytan.com/2012/11/the-sound-of-dialup-pictured.html).
VolaMUD tutorial world: ported from [GoMud](https://github.com/GoMudEngine/GoMud), MIT.
