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
- Browser terminal at `http://192.168.4.1`: press DIAL, get a node. A page
  reload rejoins the same node, and the screen has 1000 lines of scrollback.
- Telnet on port 23 for PuTTY, SyncTERM and friends. 80 columns.
- Eight nodes, shared by browser and Telnet callers.
- New-user signup, logins, security levels and flags.
- Message areas read the 1990s way: one message at a time with next and
  previous, replies linked into threads, a thread view, a line editor, and
  "new" counted per area. Private mail is opt-in.
- 1990s FidoNet traffic as read-only history in seven conferences, with every
  name, address and BBS replaced by invented ones. Callers' posts come after
  it, and it never counts as new.
- Multi-node chat, WHO, page the SysOp, bulletins (ANSI files or a line of
  text), ANSI gallery.
- File areas made from the folders on the card, with sub-folders,
  descriptions from `FILES.BBS`, `DESCRIPT.ION` and `FILE_ID.DIZ`, file dates,
  per-file download counts, ZIP contents listed with `V`, and browser
  downloads that check quota and ratio.
- Tracker music (MOD, S3M, XM, IT) plays with `V` in the browser: a
  tracker-style screen with the live pattern and channel meters, played by
  libopenmpt from the card. Fifty modules from The Mod Archive to start with.
- C64 SID tunes play the same way with jsSID, on a C64-style screen with the
  three voices live. Thirty HVSC classics to start with. The player shows each
  tune's description.
- Pictures (GIF, JPG, PNG, BMP) open with `V` in a full-screen image viewer
  in the browser; `N`/`P` step through the folder.
- Uploads from the browser to areas that allow them, into a queue the SysOp
  approves from the console. Approved uploads earn ratio and credit.
- Download credit: new accounts start with credit and a ratio from
  `SYSTEM.INI`, and posting messages earns more.
- ANSI pictures opened with `V` scroll in at modem speed. Any key skips,
  space pauses, and the browser's SPEED button picks the baud rate.
- An invented 1990s board clock that only runs while the stick has power.
- SysOp tools: user editor, node monitor with disconnect and one-way
  messages, answering a page into chat, and a file manager page in the
  browser for the SD card.
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
out. Pins and display offsets are in [`docs/HARDWARE.md`](docs/HARDWARE.md).

### Firmware layout

The firmware is Arduino on ESP32, about 6000 lines of C++ plus the two web
pages. `src/main.cpp` has `setup()` and `loop()` and includes the modules in
order, as one translation unit:

| Folder | Contents |
| --- | --- |
| `src/core` | Globals, helpers, the board clock, SD storage and INI reader, users, message store, message history from the card, sessions, paced output, the reader |
| `src/bbs` | Settings, menus, file areas, ZIP reading, viewers, uploads, messages, SysOp tools and user editor, login, one `cmd_*.h` per command area, CP437 conversion |
| `src/doors` | Legend of Kouvola, VolaMUD, dice cabinet and `doors.h`, the door table |
| `src/net` | Browser terminal and SysOp file manager pages (compiled into flash), HTTP and WebSocket routes, Telnet |
| `src/hw` | Status LCD and the splash logo as DOS palette indexes |

The music player is not in the firmware. It is `player.js` plus libopenmpt
and jsSID in `/BBS/ASSETS/PLAYER` on the card, and the board only serves the
files.

A door is one header with `enter`, `owns` and `handle` functions plus one
line in the door table.

Libraries: Arduino GFX Library, ESPAsyncWebServer and AsyncTCP, `DNSServer`,
`SD_MMC`, mbedTLS for SHA-256 and the inflater in the ESP32-S3 ROM for
`FILE_ID.DIZ`. HTTP and WebSocket run in the async server's task. One
recursive mutex guards sessions, users and the card. WebSocket keys are
queued and applied in `loop()`, which also serves DNS, polls Telnet, paces
output, expires idle sessions, keeps the clock and refreshes the LCD.

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
  parser in JavaScript. `POST /api/connect` returns a node number and a
  random token. The carrier is then a WebSocket at `/ws`, with one-letter
  frames: keys, output, password masking, the modem speed and the phone's
  clock. If the socket fails, the page falls back to HTTP polling
  (`/api/out`, `/api/in`). Node and token are kept in `sessionStorage`, so a
  reload or a dropped WiFi rejoins the node until the three-minute carrier
  timeout.

Output goes through a small pipeline. Menus and text go out at once. A file
opened with `V` is streamed from the card in chunks, at the caller's modem
speed for ANSI, and chunks never end inside an escape sequence or a UTF-8
character. Text written while a file streams waits until it has finished.

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
  swapped in, with a `.BAK` for recovery. One function does this for every
  record file.
- Messages carry the message they answer, so replies form threads.
- Per-user last-read pointers, one per message area, are small files in
  `DATA/STATE`.
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

### File areas

Every folder in `/BBS/FILES` is a file area, in the order of its number
prefix. `014_BACKROOM_IMAGES` becomes "Backroom Images". Folders starting
with `_` or `.` are hidden, and so is an area with no files in it (unless
it takes uploads). Areas behind a flag, like the back room, are always last
in the list.
Sub-folders nest as deep as the card has them. A number opens a folder or
views a file, and `Q` goes up one level.

An optional `AREA.INI` in the folder sets the name, the lowest security
level, a required user flag such as `BACKROOM`, and whether downloads and
uploads are allowed. Areas are read at boot and on `RELOAD`.

Descriptions come from `FILES.BBS` or `DESCRIPT.ION` in the folder. For a ZIP
without one, the board reads `FILE_ID.DIZ` from the archive's central
directory, inflates it with the ROM inflater and caches the first lines in
`DATA/INDEX/ZIPINFO.DAT`. DOS line-drawing characters in descriptions show as
they did on a DOS screen. Archives are never unpacked or renamed.

Listings show each file's size, its date (MM-DD-YY) and how many
times it has been downloaded. A ZIP shows the date of the newest file inside
it, which is when the archive was made; other files show their date on the
card. The counts are kept in
`DATA/INDEX/DOWNLOADS.DAT`. `V` on a ZIP lists what is inside from the
central directory, with sizes and dates, then the full `FILE_ID.DIZ`, any
NFO inside and the archive comment.

In the viewer, `N` and `P` open the next or previous viewable file in the
same folder, so a folder of ANSI art can be flipped through. Pictures and
music work the same way, on a screen of their own in the browser.

Uploads are browser only. The caller types a description and picks a file,
it waits in `/BBS/UPLOADS/INCOMING`, and the SysOp approves or deletes it
from the console. An approved file moves to the folder it was sent to and its
description goes into that folder's `FILES.BBS`.

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
multi-stage quest engine yet. See [`docs/VOLA_MUD.md`](docs/VOLA_MUD.md).

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
3. Optional content that is not in the repository. Each script downloads
   once and writes into `sd/BBS`:

   ```powershell
   python tools\fidonet-seed.py
   python tools\fetch-modules.py
   python tools\fetch-sids.py
   ```

   The first one is the FidoNet message history (150 MB download), the other
   two the Tracker Music and C64 SID Music areas.
4. Format a microSD card as FAT32 and copy this repository's `sd/BBS` into
   the card's `BBS` folder. Or run:

   ```powershell
   .\tools\prepare-sd.ps1 -CardRoot X:\
   ```

   It also creates the folders the board needs. Add `-PresentationOnly` to
   update screens, static door data and VolaMUD's seed configuration on a
   card that already has users and messages. The script does not format the
   card.
5. Insert the card and power the board from USB.
6. Join `THE_BOARD`. Press DIAL in the browser, or Telnet to
   `192.168.4.1:23`.
7. Create a user named `SYSOP` first. While no SysOp exists, that handle
   becomes the level-255 SysOp. Change its password.

Settings live in `sd/BBS/CONFIG`, each file explained in its comments:

- `SYSTEM.INI`: board name, node limit, new-user level, modem speed for ANSI
  pictures, the board clock, art paths, download credit and ratio for new
  users, credit per message, the upload size limit.
- `CONFERENCES.INI`: message bases with their security level and flag.
- `BULLETINS.INI`: bulletins, and which ones are shown at every login.
- `NETWORK.INI`: SSID, password, channel, Telnet port.
- `MENUS.INI`: the art for each menu and where in it the options go. The
  options themselves are generated: only what exists and what the caller may
  use is listed, so a new door or file area shows up without editing ANSI.
- `FILES/<area>/AREA.INI`: name, level, flag, downloads and uploads for one
  file area. Optional.

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
| `/BBS/CONFIG` | Board, conference, bulletin and network config |
| `/BBS/FILES` | File areas, one folder each, with an optional `AREA.INI` |
| `/BBS/MSGBASE` | Read-only message history, a folder per conference id, written by `tools/fidonet-seed.py` |
| `/BBS/DATA` | Users, messages, door saves, indexes |
| `/BBS/LOGS` | Caller and event logs |
| `/BBS/TEMP`, `/BBS/UPLOADS` | Runtime temp and upload staging |

Do not edit `DATA`, `LOGS` or `TEMP` by hand while the board is in use.
Callers never see SD paths.

The file archive itself is not in this repository. `sd/BBS/FILES` is ignored
by git. Archive rules are in [`docs/CURATION.md`](docs/CURATION.md).

`sd/BBS/MSGBASE` is not in the repository either. Make it with
`python tools/fidonet-seed.py`, which downloads the 150 MB source archive once
into `tools/.cache`.

## SysOp

A level-255 account with the `SYSOP` flag gets `Z`, the SysOp console.

- **User editor** (`Z` then `U`, or `USER`): a table of every account with
  level, flags, calls, messages, downloads against the limit and ratio. A
  number or handle opens a record where each setting has a number: level,
  flags (with what each flag opens), lock, download limit, credits, ratio,
  password, forest fights, real name and location. Sizes can be typed as
  `500K` or `2M`. `N` and `P` step through users.
- **Node monitor** (`W`): who is on and doing what. A node number
  disconnects that caller, `M <node|ALL> <text>` sends a one-way message.
- **Page answering**: when a caller pages, `/A` from any prompt pulls both
  into chat and `/R` refuses.
- **Upload queue** (`Z` then `I`): uploads waiting for approval. `A #`
  approves, `D #` deletes. The uploader gets the credit when it is approved.
- **File manager** (`Z` then `F`, browser only): a separate page for the SD
  card. Upload, download, rename, delete and edit text files without
  pulling the card. `DATA` and `LOGS` are read-only there.
- **`RELOAD`** rereads the settings and file areas.

## Limits

- Telnet has no file transfers (no ZMODEM) and there is no QWK mail. Both
  are on the backburner in the roadmap.
- SID tunes play with jsSID, which does not run RSID tunes.
- Eight nodes is a firmware limit. It has not been tested with eight real
  callers on the board.
- AP only. The board does not join any other network.

More in [`docs/ROADMAP.md`](docs/ROADMAP.md). The original 1.0 target is in
[`docs/SPEC_1.0.md`](docs/SPEC_1.0.md).

## Repository map

| Path | Contents |
| --- | --- |
| `src/` | Firmware |
| `sd/BBS/` | SD card content and config, including the music player |
| `tools/` | SD preparation, content download and archive curation scripts |
| `docs/` | Hardware, roadmap, curation policy, menu audit, VolaMUD notes, the 1.0 spec |
| `art/` | Source images for the splash screen |

## Credits

Dial-up sound: a generic modem handshake recorded by William Termini on his
iMac G3, from Oona Raisanen's
["The sound of the dialup, pictured"](https://www.windytan.com/2012/11/the-sound-of-dialup-pictured.html).
Mentioned because we are nice.

Message history: FidoNet traffic captured on the ExecPC BBS (1993-1999) and
archived by Josh Renaud at
[Break Into Chat](https://breakintochat.com/collections/messages/fidonet/).
Names and addresses are replaced before anything reaches the card.

Music: modules from [The Mod Archive](https://modarchive.org/) and SID tunes
from the [High Voltage SID Collection](https://hvsc.c64.org/), copied as
they are. Played with [libopenmpt](https://lib.openmpt.org/) and
[jsSID](https://github.com/jhohertz/jsSID).

GoMud was evaluated as a mechanics/world reference. Licences for all of the
above are in [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

## Licence

MIT, see [`LICENSE`](LICENSE). That covers the code, the ANSI screens and the
Kouvosto splash image. The player libraries on the card keep their own
licences (libopenmpt BSD, jsSID GPL v2), listed in
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
