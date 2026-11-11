# TinyBBS roadmap

The current repository is a working vertical slice. It boots an AP, serves a
browser terminal, accepts Telnet, stores accounts and messages on SD, and lets
callers see each other in the same BBS. It is not the complete 1.0 described
in `TinyBBS_READY_TO_CODE_SPEC.md`.

## Done in the current slice

- ESP32-S3, ST7735 and SD_MMC bring-up on the Pocket-Dongle-S3.
- Isolated AP, captive DNS, HTTP polling terminal and Telnet.
- Shared eight-node session pool.
- Login, new-user flow, salted SHA-256 password records and persistent user
  records.
- Message areas, basic conferences, chat, WHO, SysOp page, event logs and LCD
  status.
- SD-hosted ANSI, ASCII, bulletins, configuration and modem audio.
- Paged ANSI and text views, plus a browser-only listed-file download route.
- Separate native Legend of Kouvola and VolaMUD doors. VolaMUD has a static
  city hub, an SD-defined deterministic wilderness, visible seeded/random
  encounters, persistent equipment, NPC talk, trading and small tasks. Its
  gold and bank are shared with Legend and the dice cabinet.

The browser download route should be treated as an early convenience feature.
Uploads, approval queues, archive indexing, Telnet transfer protocols and
hardware performance checks still need work.

## Ground rules for the next work

- **Two callers, not eight.** Realistically there are never more than two
  people on the board at once. Design and test for that. The eight-node pool
  stays as a ceiling, but nothing gets optimised for it.
- **The SD card is the control panel.** As much as possible is changed by
  editing the card on a PC, without reflashing. Drop a folder into
  `/BBS/FILES` and it shows up as a file area. Edit an INI and the menu
  changes.
- **Form over function, but no dead features.** It has to look like a real
  1990s board. Under the hood it can take shortcuts. But a menu never offers
  something that does not work for that caller on that transport. If Telnet
  cannot download, Telnet does not see `[D] Download`.
- **Built for a one-minute visit.** A typical tester spends about a minute in
  the file and message areas and five minutes in the doors, then leaves. The
  first screens must be full, fast and working.
- **Standalone.** The board stays AP-only. If it ever needs the Internet, the
  BBS gets ported to Python or PHP on a server instead.

## Next work, in slices

Each slice is small enough to build, flash and confirm on its own. "Done
when" is the test on the real board, with one phone browser and one Telnet
client unless it says otherwise. A slice is not done until that test passes.

### Slice 0: split and tidy (no behaviour change)

Split `src/main.cpp` into modules: storage, INI reader, session core, menus,
messages, files, web transport, Telnet transport, LCD, and one file per door.
Add a small door interface (name, enter, handle line, leave) and a door table,
so a new door is one new file plus one table line. Remove leftovers: the
unused async TCP flag and LittleFS setting, the old `/out` and `/in` routes,
the duplicate PlatformIO files and the root `prepare-sd.ps1` wrapper. Bring
`AGENTS.md` up to date.

Done when: the board builds and flashes, and a smoke test passes: log in from
the browser and Telnet, both appear in WHO, chat, post and read a message,
open a file area, play one round of each door, reboot and find the account
and message still there.

### Slice 1: async web server and WebSocket carrier

Replace the synchronous web server with an async one and move the browser
terminal to a WebSocket. Keep HTTP polling as the fallback for browsers or
captive-portal windows where the socket does not open. Downloads stream
without blocking the other caller.

Done when: the browser terminal runs over WebSocket (visible in the browser's
network view); typing feels immediate; a Telnet caller keeps working while
the browser downloads a multi-megabyte file; and a captive-portal popup on
iOS and Android still reaches a working terminal.

### Slice 2: reconnect

The browser keeps its node token. If the connection drops or the phone
sleeps, the page reconnects to the same node within the carrier timeout and
redraws the screen. After the timeout it says NO CARRIER, like it should.

Done when: lock the phone for 30 seconds in the middle of a door game,
unlock, and the game continues on the same node. Lock it for over three
minutes and the page shows NO CARRIER and the node is free again.

### Slice 3: real INI reader and SD-driven board settings

One INI reader that understands `[SECTION]` headers, used everywhere.
`SYSTEM.INI` (board name, node limit up to 8, new-user level, starting
credits), `CONFERENCES.INI` and `BULLETINS.INI` are read at boot and again
with a SysOp `RELOAD` command. Settings that the firmware does not use are
removed from the INI files, so nothing on the card pretends to work.

Done when: rename a conference and add a bulletin on the card, run `RELOAD`
(or reboot), and both show up without reflashing.

### Slice 4: file areas from folders

Every folder in `/BBS/FILES` is a file area, sorted by its number prefix.
The display name comes from the folder name (`014_BACKROOM_IMAGES` becomes
"Backroom Images") unless an optional `AREA.INI` in the folder sets name,
security level, required flag, and whether downloads and uploads are
allowed. Folders inside an area are browsable sub-directories. Folders
starting with `_` or `.` are hidden. `FILEAREAS.INI` goes away.

Done when: copy `014_BACKROOM_IMAGES` with its nested folders onto the card,
reboot, and it appears as an area. Its sub-folders can be entered and left.
Add an `AREA.INI` with `required_flag=BACKROOM` and it disappears for a
normal user and reappears for one with the flag.

### Slice 5: generated menus with SD art

Menus that list things become header art plus a generated list in matching
colours: file areas, conferences, bulletins and doors. The art stays in the
`.ANS` files on the card. The list comes from what actually exists and what
the caller may use.

Done when: adding a file area folder or a conference changes the menu with no
ANSI editing, and a normal user's menus show no Back Room or SysOp entries.

### Slice 6: file listings that look the part

Listings show name, size, date (from the card's file timestamps) and a
description from `FILES.BBS` or `DESCRIPT.ION` in the folder, falling back to
nothing. A download counter per file is shown, like a real board. `[V]` on a
ZIP lists the archive contents from its central directory, without unpacking.
Reading `FILE_ID.DIZ` from inside a ZIP is a bonus if it fits.

Done when: an area with a `FILES.BBS` shows descriptions, dates look right,
and viewing a ZIP lists its files.

### Slice 7: viewing and downloading by file type

- ANSI, ASCII and text (`.ANS`, `.ASC`, `.TXT`, `.NFO`, `.DIZ`, `.BAS`):
  paged view on both transports, as now.
- Images (`.GIF`, `.JPG`, `.PNG`, `.BMP`): in the browser, `[V]` opens a full
  screen image viewer styled like a period GIF viewer. Telnet does not offer
  `[V]` for images.
- Everything else: download only, in the browser.
- Ratios still show in the user stats, but new users start with enough
  credits (from `SYSTEM.INI`) that a one-minute visitor can download
  something.

Done when: open a BMP from the back room in the browser viewer, page a text
file from Telnet, download a ZIP from the browser, and confirm Telnet shows
only the options it can use.

### Slice 8: uploads (browser only, optional per area)

A browser caller can upload to an area whose `AREA.INI` allows it. Files land
in `/BBS/UPLOADS/INCOMING` with the caller's description. The SysOp approves
or deletes them from the terminal, and approved files move to the target
area. Size limit from `SYSTEM.INI`. Off by default.

Done when: upload a file from a phone, see it in the SysOp queue, approve it,
and find it listed in the area with its description.

### Slice 9: menu audit

Go through every screen on both transports and check that every advertised
key works for that caller. Remove or hide anything that does not.

Done when: the audit list is clean for a new user, a Back Room user and the
SysOp, on browser and Telnet.

## Backburner

Nice to have. They would make the board more period correct and impress
nerds, but no visitor will use them.

- QWK offline mail packets.
- ZMODEM downloads over Telnet.
- Richer door content (more VolaMUD world, task chains, NPC state). The door
  interface from slice 0 is the hook for this.

## Not planned

- AP plus station mode, LAN or Internet access, SSH, modem protocols. The
  board stays standalone. `NETWORK.INI` station settings are removed in
  slice 3.
- Load testing eight callers. Two is the realistic number.

## Content and archive work

Kouvosto archive material goes to its native Kouvosto areas. Preserve original
archive names and FILE_ID.DIZ, DESC.SDI and DESCRIPT.ION data. Keep provenance
in `/BBS/DATA/INDEX/KOUVOSTO_ORIGINS.json`, not in a caller-visible MPOLI
folder. See `CURATION.md` before importing material.

Never populate generated data, logs, temporary areas or QWK work directories
by hand. Use the curator scripts in `tools` for archive ingest.
