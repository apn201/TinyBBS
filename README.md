# TinyBBS

A 1990s-style BBS running on a $10 ESP32-S3 USB dongle,
the [Pocket-Dongle-S3](https://www.aliexpress.com/item/1005008896509314.html). No Internet, no modem, no
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
| Board | [Pocket-Dongle-S3](https://www.aliexpress.com/item/1005008896509314.html), USB-stick form factor, get the variant with the SD slot |
| MCU | ESP32-S3 N16R8, 16 MB flash, 8 MB PSRAM, PlatformIO profile `esp32-s3-devkitc1-n16r8` |
| Display | On-board 0.96 inch ST7735, 80x160 native, used as 160x80 landscape |
| Storage | On-board microSD slot, SD_MMC 1-bit mode, FAT32 |
| Input | BOOT button only |
| Network | 2.4 GHz soft AP, channel 1 by default |
| Power | USB, from a computer or a phone charger |

Nothing is attached and no GPIO is exposed. No modem, no keyboard, no audio
out. Pins and display offsets are in [`HARDWARE.md`](HARDWARE.md).

### Firmware layout

The firmware is Arduino on ESP32, about 2400 lines. `src/main.cpp` has
`setup()` and `loop()` and includes the modules in order, as one translation
unit:

| Folder | Contents |
| --- | --- |
| `src/core` | Globals, helpers, SD storage, users, message store, sessions |
| `src/bbs` | File areas and viewer, SysOp tools, menus, login, one `cmd_*.h` per command area, CP437 conversion |
| `src/doors` | Legend of Kouvola, VolaMUD, dice cabinet and `doors.h`, the door table |
| `src/net` | Browser terminal page (compiled into flash), HTTP routes, Telnet |
| `src/hw` | Status LCD and the splash logo as DOS palette indexes |

A door is one header with `enter`, `owns` and `handle` functions plus one
line in the door table.

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
- There is no real-time clock and the board never sees NTP, so it invents
  one. The board clock starts from a 1990s date in `SYSTEM.INI`, only runs
  while the stick has power, and is saved to `DATA/STATE/CLOCK.DAT` once a
  minute. With `clock_sync=browser` it is pulled forward to a browser
  caller's local time, moved back in steps of 28 years, because the calendar
  repeats every 28 years and the weekday then still matches. It never runs
  backwards.
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
and live duels between two callers on different nodes. Forest fights come
back at midnight on the board clock. It does not run or
include the original LORD.

**VolaMUD** is a native C++ MUD door with a fixed Vola City hub and an
endless deterministic wilderness. City locals provide directions, trading,
banking and a few deliberate tasks. Wilderness rooms are usually empty, but
can contain a fixed seeded monster, a rare traveller, or a short-lived random
skeleton, orc or goblin encounter. Enemies are announced before `FIGHT` can
engage them; `FIGHT NPC` is possible but intentionally unwise.

Characters persist their position, task, gold and equipment. `INV`, `WEAR`
and `SELL` cover the current small inventory: boots, blade, shield, rings,
amulet and the Black Crown. Worn loot changes HP or combat. The Black Crown
of Kouvosto is a real archive task, held by seeded Lord Korja at `13,-8`.
Gold and bank funds are one shared purse across VolaMUD, Legend of Kouvola
and the dice cabinet. It is a playable vertical slice rather than a deep MUD:
there is no inventory UI, trading economy, party system, NPC memory or
multi-stage quest engine yet. See [`VOLA_MUD.md`](VOLA_MUD.md).

### LCD

The 160x80 screen draws the Kouvosto logo from palette-indexed rows, then
three status lines: SD state and node count, AP IP address and the latest
caller or page.

## Getting started

1. Install PlatformIO with the Espressif32 platform.
2. Build and flash: `pio run -t upload`. On Windows,
   `tools\build-platformio.cmd` does the same. The proven target is the
   Pocket-Dongle-S3 with the N16R8 profile in `platformio.ini`; see
   **Flash and recovery** below before trying another board profile.
3. Format a microSD card as FAT32. Extract `TinyBBS_SD_CARD_EMPTY_TREE.zip`
   to the card root, then copy this repository's `sd/BBS` into the card's
   `BBS` folder. Or run:

   ```powershell
   .\tools\prepare-sd.ps1 -CardRoot X:\
   ```

   Add `-PresentationOnly` to update screens, static door data and VolaMUD's
   seed configuration on a card that already has users and messages. The
   script does not format the card.
4. Insert the card and power the board from USB.
5. Join `THE_BOARD`. Press DIAL in the browser, or Telnet to
   `192.168.4.1:23`.
6. Create a user named `SYSOP` first. While no SysOp exists, that handle
   becomes the level-255 SysOp. Change its password.

Settings live in `sd/BBS/CONFIG`, each file explained in its comments:

- `SYSTEM.INI`: board name, node limit, new-user level, modem speed for ANSI
  pictures, the board clock, art paths.
- `CONFERENCES.INI`: message bases with their security level and flag.
- `BULLETINS.INI`: bulletins, and which ones are shown at every login.
- `NETWORK.INI`: SSID, password, channel, Telnet port.

After editing (on the card, or in the SysOp file manager) type `RELOAD` as
SysOp, or `Z` then `R`. `NETWORK.INI` needs a restart.

## Flash and recovery

The verified board is the Pocket-Dongle-S3 with an N16R8 module: Quad flash, Octal PSRAM, 16 MB flash and
8 MB PSRAM. Use the board profile `esp32-s3-devkitc1-n16r8`. Its runtime image
uses the board's QIO/OPI arrangement, but the serial write must use **DIO**.
Do not force QIO in a direct `esptool` command: the ROM can reset before the
bootloader loads, producing repeated USB connect/disconnect sounds.

If a normal PlatformIO upload stalls because Windows cannot print ESP32 upload
glyphs, build without uploading and use the direct, hash-verifying recovery
path below. Replace `COM10` with the actual port. It rewrites only firmware
regions; it does not touch the SD card.

```powershell
pio run
$env:PYTHONIOENCODING = 'utf-8'
& "$env:USERPROFILE\.platformio\penv\Scripts\esptool.exe" --chip esp32s3 --port COM10 --baud 921600 `
  write-flash --flash-mode dio --flash-freq 80m --flash-size 16MB `
  0x0 .pio-tinybbs\tinybbs_s3\bootloader.bin `
  0x8000 .pio-tinybbs\tinybbs_s3\partitions.bin `
  0xe000 "$env:USERPROFILE\.platformio\packages\framework-arduinoespressif32\tools\partitions\boot_app0.bin" `
  0x10000 .pio-tinybbs\tinybbs_s3\firmware.bin
```

`Hash of data verified` for every segment and a reset without repeating USB
chimes are the expected result. Confirm that `THE_BOARD` appears as a 2.4 GHz
SSID before trying browser or Telnet tests. The ESP ROM loader remains
reachable over USB even after a bad application flash; do not erase the chip
as a first response.

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
- AP only. The board does not join any other network.

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

Dial-up sound: a generic modem handshake recorded by William Termini on his
iMac G3, from Oona Raisanen's
["The sound of the dialup, pictured"](https://www.windytan.com/2012/11/the-sound-of-dialup-pictured.html).
Mentioned because we are nice.
GoMud was evaluated as a mechanics/world reference. See
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

## Licence

MIT, see [`LICENSE`](LICENSE). That covers the code, the ANSI screens and the
Kouvosto splash image. GoMud's notice is in
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
