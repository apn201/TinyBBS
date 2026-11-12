# TinyBBS roadmap

The current repository is a working vertical slice. It boots an AP, serves a
browser terminal, accepts Telnet, stores accounts and messages on SD, and lets
callers see each other in the same BBS. It is not the complete 1.0 described
in `SPEC_1.0.md`.

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

### Slice 1b: SysOp file manager in the browser

Taking the SD card out of the stick is cumbersome. So the SysOp gets an
admin mode that breaks out of the period look on purpose: a plain HTML file
manager for the card, served by the board.

- Opened from the SysOp menu in the browser terminal (`Z` then `F`). The
  terminal hands the page a one-time admin token tied to the logged-in
  level-255 SysOp session. No separate password, and no admin page for
  anybody else. Telnet does not offer it.
- Browse `/BBS`, upload files (several at once, drag and drop), create and
  rename folders, delete with confirmation, download, and edit text files
  (`.INI`, `.ANS` as raw text, `.TXT`, `FILES.BBS`, `DESCRIPT.ION`) in a
  textarea.
- `/BBS/DATA` and `/BBS/LOGS` are read-only here, so live users and messages
  cannot be broken by accident.
- Changes take effect at once for file areas and art, and through `RELOAD`
  for INI files (slice 3).

This also makes the later slices quicker to test: new art, areas and
messages go on the card without unplugging anything.

Done when: as SysOp on a phone, open the file manager, upload a folder of
images into `/BBS/FILES`, edit `SYSTEM.INI`, delete a test file, and a
normal user's terminal has no way into the page. Calling the admin URL
without a valid token returns nothing useful.

### Slice 2: reconnect

The browser keeps its node token. If the connection drops or the phone
sleeps, the page reconnects to the same node within the carrier timeout and
redraws the screen. After the timeout it says NO CARRIER, like it should.

Done when: lock the phone for 30 seconds in the middle of a door game,
unlock, and the game continues on the same node. Lock it for over three
minutes and the page shows NO CARRIER and the node is free again.

### Slice 2b: ANSI at modem speed instead of pages

On a real board nobody paged through an ANSI. It arrived at modem speed and
scrolled past while it was drawn. Do the same for ANSI files opened with `V`
in a file area: they are sent at an emulated line speed and the terminal
scrolls by itself. Menus and other screens appear at once. Telnet uses
`baud=` in `SYSTEM.INI`; the browser has a SPEED button next to DIAL that
cycles 2400 to 57600 and MAX and is remembered in the browser. This works the same on Telnet, because
any terminal scrolls when text arrives slowly. ANSI animations that use cursor
movement play as intended.

- The file streams from the SD card as the output drains, so a file of any
  size works with the 14 KB output buffer.
- Any key stops the scroll and drops to the prompt. Space pauses and resumes.
  A one-minute visitor must never be stuck in a long file.
- The browser terminal gets a scrollback buffer, so a finished ANSI can be
  scrolled back up with a finger or the mouse wheel. Telnet users use their
  own client's scrollback.
- The old page-by-page viewer stays only for long plain text where reading
  at your own pace matters, if it turns out to be needed.

Done when: a tall multi-screen ANSI scrolls smoothly at the set speed in the
browser and in PuTTY, a key press stops it at once, and in the browser the
whole picture can be scrolled back after it finishes.

### Slice 3: real INI reader and SD-driven board settings

One INI reader that understands `[SECTION]` headers, used everywhere.
`SYSTEM.INI` (board name, node limit up to 8, new-user level, starting
credits), `CONFERENCES.INI` and `BULLETINS.INI` are read at boot and again
with a SysOp `RELOAD` command. Settings that the firmware does not use are
removed from the INI files, so nothing on the card pretends to work.

Done when: rename a conference and add a bulletin on the card, run `RELOAD`
(or reboot), and both show up without reflashing.

### Slice 3c: a message reader that flows

Conferences are the message areas, as on PCBoard or Wildcat. `M` opens the
current area with its message count and new count, and `[A]` (or `J` from
the main board) changes area. Messages are read one at a time with
`[N]ext [P]rev [R]eply [T]hread [Q]uit`, ENTER for next. Replies are linked
to the message they answer; the header shows "Reply to #n" and "Replies:".
`T` shows the whole thread on one screen, oldest first, like an e-mail
chain. Messages are written with a line editor (`/S` saves, `/A` aborts).
Messages addressed to someone are public unless the writer says private.
Last-read pointers are kept per area. Bulletins can be a line of text
(`text=` in `BULLETINS.INI`) instead of an art file.

Done when: post, reply to the reply, read the thread with `T`, step through
an area with ENTER and `P`, and see a text bulletin at login.

### Slice 3b: an invented board clock

Built. The browser shift is in steps of 28 years (2026 becomes 1998), so the
weekday matches the visitor's. The clock is saved once a minute. Last-call
times wait for the user list rework; uploads and their dates come in slice 8.

The board has no real-time clock, so it invents one. The board clock is a
date and time in the 1990s that only runs while the stick has power. When it
is unplugged, time stops. When it is plugged in again, time continues from
where it left off.

- The start date is set in `SYSTEM.INI`, for example `epoch=1997-03-14 21:00`.
- The clock is saved to `/BBS/DATA/STATE/CLOCK.DAT` every few minutes and on
  every write that carries a timestamp, so a power cut loses at most a few
  minutes.
- Optional browser sync (`clock_sync=browser` in `SYSTEM.INI`): when a
  browser connects, it sends the phone's local time. The board shifts it back
  to the configured era year (2026 becomes 1997, for example) and moves the
  board clock forward to that point if it is behind. The clock never moves
  backwards, so message and caller order stays correct. Day of week and
  time of day then match the visitor's evening, which looks right.
- Messages, last-call times, file dates for uploads, the caller log and the
  `S` system info screen all use the board clock instead of `UP hh:mm:ss`.
- The Legend of Kouvola daily turns can then reset at board midnight instead
  of waiting for the SysOp.

Done when: post a message, unplug the stick for ten minutes, plug it in, and
the next message is stamped a minute or two after the first, not ten minutes
later and not back at the epoch. With browser sync on, connect from a phone
at 21:30 and the board clock shows 21:30 on a 1990s date.

### Slice 4: file areas from folders

Built. On first boot the firmware writes the settings from an old
`FILEAREAS.INI` into an `AREA.INI` in each folder and renames the old file to
`FILEAREAS.OLD`. Areas with no files are not listed. A number opens a folder
or views a file.

Every folder in `/BBS/FILES` is a file area, sorted by its number prefix.
The display name comes from the folder name (`014_BACKROOM_IMAGES` becomes
"Backroom Images") unless an optional `AREA.INI` in the folder sets name,
security level, required flag, and whether downloads and uploads are
allowed. Folders inside an area are browsable sub-directories. Folders
starting with `_` or `.` are hidden. `FILEAREAS.INI` goes away. Sub-folders
nest as deep as the card has them, so the SysOp can sort a big area into
folders without making new areas.

The viewer gets `[N]ext` and `[P]rev`: after viewing a file with `V`, they
open the next or previous viewable file in the same folder, so a caller can
flip through a folder of ANSI art without going back to the list each time.

Descriptions are part of this slice, because a listing without them does not
look like a BBS. In order of preference: `FILES.BBS` or `DESCRIPT.ION` in the
folder, then `FILE_ID.DIZ` from inside a ZIP (read on demand and cached in
`/BBS/DATA/INDEX`), then nothing.

Done when: copy `014_BACKROOM_IMAGES` with its nested folders onto the card,
reboot, and it appears as an area. Its sub-folders can be entered and left.
Add an `AREA.INI` with `required_flag=BACKROOM` and it disappears for a
normal user and reappears for one with the flag. A ZIP with a `FILE_ID.DIZ`
shows its description in the listing. View an ANSI file in a folder of
several and step through them with `N` and `P`.

After this slice the SysOp adds more ANSI and ASCII art straight on the card.

### Slice 4c: one reader for everything

Built, in `src/core/reader.h`. Pictures play when opened (any key stops them
in the reader), text opens on its first page, messages use the reader only
when longer than a screen. Telnet has no modem speed, so pictures open on
their first page there.

Text files open in a pager (built), ANSI pictures play at modem speed, and
messages scroll past. One reader replaces all three:

- ANSI art is drawn into an 80-column screen buffer on the board (PSRAM has
  room for very tall pictures), so it can be paged like text: each screen row
  is sent with its colours.
- In the reader, `[S]` plays the file from the current line at modem speed,
  the way V does now. Any key stops it and leaves you in the reader where it
  stopped.
- Messages and threads open in the same reader when they are longer than a
  screen. Short ones stay as they are, with the reader prompt below.
- Same keys everywhere: SPACE next page, B back, arrows by line, N and P
  next and previous file or message, Q quit.

Done when: a tall ANSI picture, a long text file and a long message all
open in the same reader with the same keys, and S plays the picture.

### Slice 4d: killing and editing messages

Built, history included. `MESSAGES.DAT` stays append-only: an edit or a kill
is a new line with the same id, the last one wins, and the earlier lines are
the history. The line editor gained `/L`, `/D n`, `/R n text` and `/C`.

Boards of the time let the SysOp kill any message and callers kill their
own (PCBoard's K, for example). Some let the author edit the text too.

- `[K]ill` in the reader prompt, for the author and the SysOp, after a
  y/N question. A killed message is marked deleted in the store, never
  removed, so ids, replies and read pointers stay valid. Replies to it show
  "Reply to #9 (deleted)".
- `[E]dit` for the author and the SysOp: the line editor opens with the old
  text. The message then shows `[edited]` with the date, and by whom when the
  SysOp did it.
- Deluxe: the earlier versions kept in the store, and `H` in the reader
  shows the change history.

Done when: a caller kills their own message and cannot kill someone
else's, the SysOp kills and edits anything, an edited message shows
`[edited]`, and a reply to a killed message still opens.

### Slice 5: generated menus with SD art

Built. `MENUS.INI` names each menu's art and the place in it (row, column,
columns, width, rows) where the board writes the options; the old options
were blanked out of the pictures. Main, messages, gallery and doors use it.
The file menu lists its areas below `FILES.ANS`, since there can be many.

Menus that list things become header art plus a generated list in matching
colours: file areas, conferences, bulletins and doors. The art stays in the
`.ANS` files on the card. The list comes from what actually exists and what
the caller may use.

Done when: adding a file area folder or a conference changes the menu with no
ANSI editing, and a normal user's menus show no Back Room or SysOp entries.

### Slice 6: file listings that look the part

Built. Listings have a column header and show size, the card's date for the
file (MM-DD-YY), a download counter (`DATA/INDEX/DOWNLOADS.DAT`) and the
description. `V` or the number on a ZIP opens its contents in the reader,
read from the central directory, with totals, the full FILE_ID.DIZ and any NFO inside
(inflated, CP437 glyphs, cut at 80 columns) and the archive comment.

Listings show name, size, date (from the card's file timestamps) and the
description from slice 4. A download counter per file is shown, like a real
board. `[V]` on a ZIP lists the archive contents from its central directory,
without unpacking.

Done when: dates and download counts look right and viewing a ZIP lists its
files.

### Slice 7: viewing and downloading by file type

Built. `V` on a GIF, JPG, PNG or BMP opens a full-screen image viewer in the
browser: the picture is drawn in top to bottom, a blue status bar shows name,
size and resolution, `N`/`P` (or the arrows, or a tap on the right/left)
step through the pictures in the folder, any other key goes back. Telnet
says pictures open in the browser. Files that cannot be viewed say `D #` in
the browser and "from the browser terminal" on Telnet. New accounts get
`new_user_credits` and `new_user_ratio`; each message earns
`credits_per_message`; `credits_per_upload_kb` waits for uploads (slice 8).
The S screen and the file list show the caller's download credit.

- ANSI, ASCII and text (`.ANS`, `.ASC`, `.TXT`, `.NFO`, `.DIZ`, `.BAS`):
  paged view on both transports, as now.
- Images (`.GIF`, `.JPG`, `.PNG`, `.BMP`): in the browser, `[V]` opens a full
  screen image viewer styled like a period GIF viewer. Telnet does not offer
  `[V]` for images.
- Everything else: download only, in the browser.
- Ratios still show in the user stats, but new users start with enough
  credits (from `SYSTEM.INI`) that a one-minute visitor can download
  something. Credits are earned by uploads and by posting messages, both
  amounts set in `SYSTEM.INI` (`credits_per_upload_kb`,
  `credits_per_message`). Setting one to 0 turns that way of earning off.

Done when: open a BMP from the back room in the browser viewer, page a text
file from Telnet, download a ZIP from the browser, and confirm Telnet shows
only the options it can use.

### Slice 8: uploads (browser only, optional per area)

Built. `upload_max` in `SYSTEM.INI` (0 = off) and `uploads=1` in an area's
`AREA.INI` turn it on. `[U]` in that area's list asks for a one-line
description, then a CHOOSE FILE button and a transfer meter appear below the
terminal. The file waits in `UPLOADS/INCOMING`; the queue is
`DATA/INDEX/UPLOADS.DAT`. SysOp console `I` lists it: `A #` moves the file
into the area and sub-folder it was sent to, adds the description to that
folder's `FILES.BBS` and credits the uploader (counters, ratio and
`credits_per_upload_kb`); `D #` deletes it. An online SysOp is told when an
upload arrives, an online uploader when it is approved. An empty area that
takes uploads is listed for browser callers.

A browser caller can upload to an area whose `AREA.INI` allows it. Files land
in `/BBS/UPLOADS/INCOMING` with the caller's description. The SysOp approves
or deletes them from the terminal, and approved files move to the target
area. Size limit from `SYSTEM.INI`. Off by default.

Done when: upload a file from a phone, see it in the SysOp queue, approve it,
and find it listed in the area with its description.

### Slice 9: menu audit

Built. The audit is in `MENU_AUDIT.md`: every screen, its keys and who gets
them. Fixed: Legend `[C]hallenge` only with someone to challenge, Dice on an
empty purse, message reader K/E/H only when offered, no `[D]` on the SysOp's
own record, message menu error text, signup can be cancelled with ENTER,
duel messages on their own lines, and the hidden ASCII toggle removed.

Go through every screen on both transports and check that every advertised
key works for that caller. Remove or hide anything that does not.

Done when: the audit list is clean for a new user, a Back Room user and the
SysOp, on browser and Telnet.

### Slice 10: a board that feels alive

Built. `tools/fidonet-seed.py` takes the newest 150 messages of each chosen
ExecPC echo (MUSIC into General Discussion; new conferences 4-9: Door Games,
Video Games, Star Trek, Modems, Unix & Linux, Comics), renames everyone and
writes `/BBS/MSGBASE/<id>/<ECHO>.TXT`. The board indexes the headers into
PSRAM at boot and on RELOAD and reads bodies on demand. History is numbered
first, callers' posts after it; it is read-only, never counts as new, and can
be replied to. Each history message shows its FidoNet echo.

Right now the message bases are empty and the board does not seem alive. Fill
them with real period traffic: FidoNet echo messages from the
[Break Into Chat collection](https://breakintochat.com/collections/messages/fidonet/),
with names anonymised.

- A curator script in `tools` fetches selected echoes, replaces every real
  name, handle, node address and signature with consistent invented ones,
  and writes plain text message files under `/BBS/MSGBASE/<conference>/`.
- The firmware shows these as read-only history in the matching conference,
  below the live messages callers post. Editing or adding files on the card
  changes the board without reflashing, like everything else.
- Dates are kept as period dates (1990s), since the board has no clock
  anyway.

Done when: a new user opens a conference and finds dozens of believable
1990s messages with no real names in them, and a caller's new post appears
on top.

### Slice 11: MOD and SID music in the browser

MOD, S3M, XM and IT built: libopenmpt 0.8.10 (WebAssembly, BSD) and
`player.js` in `/BBS/ASSETS/PLAYER`, played with a ScriptProcessorNode because
plain-HTTP pages may not use AudioWorklet. Fifty Mod Archive modules in
`015_MUSIC` from `tools/fetch-modules.py`. SID built with jsSID (GPL v2,
reSID engine) as separate files next to it; 30 PSID tunes from the HVSC in
`016_SID_MUSIC` from `tools/fetch-sids.py`. The player shows the file's
description. Areas behind a flag sort last in the file menu.

In reality you downloaded a MOD file from the board and played it on your PC.
Here the browser plays it directly, which is a shortcut, but the point is to
hear it. `[V]` on a `.MOD`, `.S3M`, `.XM`, `.IT` or `.SID` file in the browser
opens a player screen styled like a period tracker or SID player. Telnet does
not offer it.

The player JavaScript and any WebAssembly live on the SD card under
`/BBS/ASSETS/PLAYER`, served by the board. Nothing loads from the Internet.
Pick players with a licence that allows bundling, and list them in
`THIRD_PARTY_NOTICES.md`.

Done when: open a MOD and a SID from a music area on a phone, both play, and
the Telnet listing shows them without a `[V]` option.

## Backburner

In priority order. The Apache port is the biggest one and is planned on its
own in `APACHE_PORT_PLAN.md`.

1. **The Apache port.** The board on an Apache-hosted server, for when it
   should be on the Internet: the same browser terminal, the same `/BBS`
   folder and data, the server's own clock. Running DOS programs from the
   file areas in the browser belongs there. Keep the core
   transport-independent and the content on the card so the port stays
   possible.
2. **Full-screen message editor**, nano-like (arrow keys, Ctrl-O save, Ctrl-X
   exit) and maybe a vi mode, as an option the caller switches to. Not period
   correct; the line editor stays the default.
3. **File area maintenance: mass move.** In the file manager, select several
   files and move them to another area or folder in one go, with their
   descriptions carried along in `FILES.BBS`, so the SysOp can sort approved
   uploads out of Caller Uploads into the right areas easily. Download counts
   follow the files.
4. **Music that keeps playing.** The player can be shrunk to a strip at the
   top of the screen and left playing while the caller browses the board.
5. **Richer door content**: more VolaMUD world, task chains, NPC state. The
   door interface from slice 0 is the hook for this.
6. **What else did popular boards have?** Go through
   [Break Into Chat](https://breakintochat.com/) and similar archives and list
   the small features that made boards feel inhabited, then pick the cheap
   ones. Candidates to check: last callers list, oneliners or graffiti wall,
   voting booth, top uploaders and posters, new-files scan since last call,
   BBS lists and ads, time bank, SysOp chat request with a buzzer, matrix or
   pre-login screens, and random login quotes. Each one that survives becomes
   its own slice, driven from SD like the rest.

Low priority:

- ZMODEM downloads over Telnet.
- Swappable tools: the reader, editor and similar parts behind a small
  interface, picked in the config, so others could bring their own as
  plugins. Only worth it if this became a real product.
- QWK offline mail packets. Not important.

## Not planned

- ATASCII animations. Decided against.
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
