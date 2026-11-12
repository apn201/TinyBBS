# TinyBBS on a server: implementation plan

This is the plan for running TinyBBS on a Linux server behind Apache, next
to the ESP32 stick, from the same code and the same `/BBS` card tree. It is
written for a coding agent: work the phases in order, and finish each one
with its check before starting the next.

`APACHE_PORT_PLAN.md` in the repository root (not tracked) is an earlier
sketch. Its sync design is still useful for phase 7; where the two disagree,
this file wins.

## Decisions

- **One repository.** Firmware, server, web pages, card tree and tools stay
  together, so a change lands on both at once.
- **One BBS core.** The server compiles the same C++ (`src/bbs_core.h`) as
  the firmware. No second implementation in another language. The core
  reaches the machine only through `src/platform/platform.h`; the server
  provides the same names on Linux.
- **Same card tree.** The server's data folder is a `/BBS` tree with the
  same layout and file formats as the SD card. A card can be copied to the
  server and back.
- **Apache in front.** Apache terminates TLS and proxies HTTP and WebSocket
  to the TinyBBS server process on localhost. It serves nothing itself.
- **Same frontend.** The browser terminal and the SysOp page are the files
  in `shared/web/`. The firmware embeds them at build time; the server serves
  them as they are.
- **Telnet included.** The server runs the same Telnet code. Its port is
  closed in the firewall unless the SysOp opens it.
- **Identity on the card.** Name, short name, art, door names, logos and
  content come from the `/BBS` tree. Rebranding never touches source.
- **Host:** the owner's own server, where a long-running process is
  allowed. (Plain PHP web hosting would need a rewrite and is not planned.)

## What is already done

The groundwork is in the repository and builds for the ESP32:

| Piece | Where |
| --- | --- |
| Platform seam: random, big buffers, the lock, the Telnet socket type | `src/platform/platform.h`, `src/platform/esp32.h` |
| The shared core as one include, ESP32 objects kept out of it | `src/bbs_core.h`, `src/core/globals.h` |
| Start-up and the main tick in the core | `bbsBegin()`, `bbsTick()` in `src/bbs/runtime.h` |
| Web pages as plain files, embedded at build time | `shared/web/*.html`, `tools/embed_web.py` (PlatformIO pre-build step) |
| Identity from the card | `SYSTEM.INI` name, `short_name`, `lcd_logo`, `[DOORS]`; `GET /api/board` |
| ESP32 status logo from the card | `ASSETS/LOGO.RGB565`, `tools/make-lcd-logo.py` |
| Safe record rewrites in one place | `putRecord()` in `src/core/storage.h` |

## Target layout

```
src/                 firmware (PlatformIO builds this as before)
  bbs_core.h         the shared core: core/, bbs/, doors/
  platform/          platform.h (the contract), esp32.h
  net/               ESP32 transports (ESPAsyncWebServer, WiFiServer)
  hw/                ESP32 LCD
shared/web/          terminal.html, sysop.html: the one copy of each page
server/
  CMakeLists.txt     builds tinybbs-server on Linux
  platform/          tinybbs_platform.h + Arduino look-alikes: String, File, SD_MMC, millis, WiFiServer/WiFiClient
  net/               server transport: HTTP, WebSocket, Telnet bindings
  main.cpp           start-up, the loop, command-line options
  deploy/            systemd unit, Apache vhost example
tests/transcripts/   key sequences and expected screens, run against both builds
sd/BBS/              the card tree: content, settings, identity
tools/               card tools, embed_web.py, later the sync tool
```

## Rules that keep both builds identical

Put these in `AGENTS.md` once the server exists, and keep to them:

1. The shared core (`src/core`, `src/bbs`, `src/doors`) includes no ESP32 or
   Linux header. It uses the Arduino-style names listed in
   `src/platform/platform.h` and nothing else from the machine.
2. A new platform need means a new function in `platform.h`, implemented in
   both `src/platform/esp32.h` and `server/platform/`.
3. Route logic lives once (phase 3 moves it to `src/net/routes.h`); each
   transport only binds it to its web server.
4. Web pages are edited only in `shared/web/`. The generated headers in
   `src/net/` are never edited by hand.
5. Content, settings and identity go on the card, never into code.
6. Before merging: `pio run` and the server build both succeed, and the
   transcript tests pass on the server.
7. Real differences are settings, not `#ifdef`s in the core: node count,
   clock source, upload size and the like are read from the card.

## Phases

### Phase 1: Arduino look-alikes for Linux (`server/platform/`)

Make `src/bbs_core.h` compile and run on Linux without changing it.

- `tinybbs_platform.h`, found by `#include <tinybbs_platform.h>` in
  `src/platform/platform.h`. It defines `platformName()` (`"server"`),
  `bbsRandom()` (`getrandom`), `bigAlloc`/`bigRealloc` (`malloc`/`realloc`),
  `bigMemory()` (true), `PlatformMutex` (`std::recursive_mutex`) and
  `NetClient`.
- `String`: the subset the core uses. Get the list with
  `grep -ohE "\.(substring|indexOf|lastIndexOf|startsWith|endsWith|replace|remove|trim|toInt|toUpperCase|toLowerCase|reserve|concat|c_str|length|charAt)\(" src -r | sort | uniq -c`
  and match Arduino's behaviour exactly, including `String(int)`, `String(char)`,
  `+=` with numbers and `operator+` chains. Write it on `std::string`.
- `File` and an `SD_MMC` object with `open`, `exists`, `mkdir`, `remove`,
  `rename`, `totalBytes`, `usedBytes`, plus `File::read`, `write`, `print`,
  `println` (CRLF, like Arduino), `readStringUntil`, `available`, `seek`,
  `position`, `size`, `isDirectory`, `openNextFile`, `name`, `path`,
  `getLastWrite`, `close`. Paths starting `/BBS` map to the data folder given
  on the command line.
- `millis()`, `delay()`, `Serial` (prints to stdout for `trace()`),
  `constrain`, `min`/`max` as Arduino defines them.
- mbedTLS: use the distribution's `libmbedtls` for `mbedtls/sha256.h`, so
  password hashes match the stick's byte for byte.
- `miniz.h`: the ESP32 has the inflater in ROM. On Linux put upstream miniz
  (MIT, the same `tinfl_*` API) in `server/platform/` so `src/bbs/zip.h`
  compiles unchanged.

Check: a test program includes `platform.h` and `bbs_core.h`, points at a copy
of `sd/BBS`, calls `bbsBegin()` and prints the settings summary. Logging in
an existing user from a copied card works, which proves the hashes match.

### Phase 2: server main loop (`server/main.cpp`)

- Options: `--root /srv/tinybbs` (the folder holding `BBS`),
  `--http 127.0.0.1:8080`, `--telnet 0.0.0.0:2323` (off when not given).
- Start: set `sdReady` after checking the folder, call `bbsBegin()`, start
  the transports, then loop `bbsTick(...)` about every 1-5 ms under
  `BbsLock`, as `src/main.cpp` does.
- The WebSocket queue (keys in, frames out) works as in `src/net/web.h`:
  socket events only queue, the loop applies them under the lock.
- Clock: `clock_sync=browser` keeps working. `clock_sync=system` follows the
  host's configured local clock in the same 28-year `epoch=` era (so it keeps
  the BBS in the 1990s with matching weekdays); it is a core setting, not a
  server-only path.
- Node count: `MAX_NODES` is a compile-time array size. Make the array size a
  constant per platform (8 on the ESP32, 32 on the server) and keep
  `max_nodes` in `SYSTEM.INI` as the limit callers see.

Check: `tinybbs-server --root ...` starts, logs the settings summary and
keeps running.

### Phase 3: one set of routes, two web servers

`src/net/web.h` and `src/net/sysop_web.h` are written against
ESPAsyncWebServer. Split each into:

- `src/net/routes.h` (shared): the handler logic, written against a small
  interface: `Req` (method, path, query and form arguments, body chunks for
  uploads) and `Resp` (status, headers, text, a file from the card with a
  content type, streaming). Routes to carry over, with the same behaviour:
  `/`, `/api/board`, `/api/connect`, `/api/hangup`, `/api/speed`,
  `/api/out`, `/api/in`, `/ws`, `/download`, `/view`, `/music`, `/upload`,
  `/player/*`, `/modem.mp3`, `/status`, `/sysop`, `/sysop/api/*`, and the
  captive-portal redirect for unknown paths (ESP32 only).
- `src/net/esp_web.h`: binds `routes.h` to ESPAsyncWebServer. The ESP32
  must behave exactly as now.
- `server/net/http.cpp`: binds `routes.h` to a small embeddable HTTP and
  WebSocket library. Use CivetWeb (MIT, one C file, HTTP, WebSocket,
  multipart) unless there is a reason not to. Serve `/` and `/sysop` from
  `shared/web/` rather than from the generated headers.

Check: the browser terminal works against the server through `curl` and a
browser on the server's localhost: DIAL, log in, read a message, open a file,
download, upload, the music player. Then the same on the ESP32: nothing may
have changed there.

### Phase 4: Telnet on the server

`src/net/telnet.h` uses `WiFiServer` and `NetClient`. Provide both in
`server/platform` on POSIX sockets (non-blocking `accept`, `available`,
`read`, `write`, `connected`, `stop`), and compile the same `telnet.h`.
`--telnet` is the explicit listener address; omitting it leaves Telnet off.

Check: `telnet localhost 2323` gets the login screen, and a Telnet caller
and a browser caller see each other in WHO.

### Phase 5: transcript tests

- `tests/transcripts/*.txt`: a key sequence and the text expected on screen
  after each step (ANSI codes stripped, the clock masked).
- `tests/run_transcripts.py`: plays them through the HTTP polling API
  (`/api/connect`, `/api/in`, `/api/out`) against a URL. Same runner for the
  server and for the stick (from a PC joined to `THE_BOARD`).
- Start with: new user, login, message post and read, file listing, ZIP
  view, a door turn, SysOp user edit.
- CI (GitHub Actions): build the firmware with PlatformIO, build the server
  with CMake, start the server on a copy of `sd/BBS`, run the transcripts.

Check: CI is green, and the same transcripts pass on the stick.

### Phase 6: deployment (`server/deploy/`)

- `tinybbs.service` (systemd): runs `tinybbs-server --root /srv/tinybbs
  --http 127.0.0.1:8080`, as its own user, restart on failure.
- `apache-tinybbs.conf`: a virtual host with TLS, `ProxyPass /ws
  ws://127.0.0.1:8080/ws`, `ProxyPass / http://127.0.0.1:8080/`, the matching
  `ProxyPassReverse`, and no caching of `/api/*`. Needs `mod_proxy`,
  `mod_proxy_http`, `mod_proxy_wstunnel`, `mod_ssl`. Target host is Rocky
  Linux (RHEL family): the file goes in `/etc/httpd/conf.d/`, and SELinux must
  allow Apache to connect to the local port (`httpd_can_network_connect`).
- Backups: `/srv/tinybbs/BBS/DATA` nightly. The rest is content and can be
  copied from the repository or a card.

Check: the board answers on the public domain over HTTPS, and a reboot of the
server brings it back by itself.

### Phase 7: record origins, then the sync tool

Message and user ids are counters on each copy, so the stick and the server
will both make message #12. Before the server takes real posts:

- Each install gets a board id at first start, stored in
  `/BBS/DATA/STATE/BOARD.ID` (random, never copied with the config).
- New records get an `origin` field at the end of their line: the board id
  that wrote them. Older lines without it read as "this board", the same way
  earlier fields were added.
- The sync tool (`tools/bbs-sync.py`) then merges two `/BBS` trees both ways:
  messages by (origin, id) with ids renumbered on import and `replyTo`
  mapped, uploads and file areas by path and size, `FILES.BBS` and
  `DOWNLOADS.DAT` merged. Users need a policy decision first (see
  `APACHE_PORT_PLAN.md`).

Check: post on the stick and on the server, sync, and both show both posts
in order, with threads intact.

### Phase 8: the rest of the identity

- Door prose with the Kouvola and Kouvosto names (Legend's hall of fame and
  rumours, VolaMUD's world) moves to text files under `/BBS/DOORS/` with the
  current text as the default.
- The page favicon and any remaining fixed strings come from the card.

Check: `grep -ri kouvo src shared` finds only fallbacks and comments.

## Moving the live board to the server

The repository's `sd/BBS` has content and settings only. Users, messages,
door saves, read pointers and the clock are in `BBS/DATA` on the stick's SD
card, and the file areas and message history are on the card too. To carry
the current state over, copy the whole `BBS` folder from the card to
`/srv/tinybbs/BBS` (all of it, including `DATA`; `TEMP` and `LOGS` can be
left out). Password hashes are the same on both, so accounts keep working.

Do this when the server is about to go live, not before: until phase 7 there
is no sync, so after the copy the stick and the server drift apart. Decide
which one is the main board until then.

## Prompt for the server setup agent

Give this to the agent that prepares the server (OpenClaw). Replace
`<your-domain>` and `<repo-url>`. The host runs Rocky Linux. The TinyBBS
server program does not exist yet: `server/` is written in phases 1-4. Until
then the agent prepares the machine and Apache (steps 1-6) and stops; step 7
is for later.

```
Prepare this Rocky Linux server to host TinyBBS, a 1990s-style BBS.
Domain: <your-domain>. Source: <repo-url> (branch main).

TinyBBS will run as one long-running C++ process, tinybbs-server, listening
on 127.0.0.1:8080 for HTTP and WebSocket. Apache sits in front and only
proxies; it serves no files itself. Its data is a folder /srv/tinybbs/BBS
with the same layout as the repository's sd/BBS. The server program is not
written yet (the repository has no server/ folder): do steps 1-6 now and
stop; step 7 is for later.

1. Packages: dnf install epel-release, then git gcc-c++ make cmake
   mbedtls-devel civetweb-devel python3 httpd mod_ssl certbot python3-certbot-apache.
   (mod_proxy, mod_proxy_http and mod_proxy_wstunnel come with httpd.)
2. Create a system user "tinybbs" with no login shell. Create /srv/tinybbs
   owned by it. Clone the repository to /srv/tinybbs/src.
3. Do not create /srv/tinybbs/BBS yet: the owner will copy the live board's
   data there when it goes live.
4. Apache: /etc/httpd/conf.d/tinybbs.conf, a virtual host for <your-domain>
       ProxyPreserveHost On
       ProxyPass        /ws ws://127.0.0.1:8080/ws
       ProxyPassReverse /ws ws://127.0.0.1:8080/ws
       ProxyPass        /   http://127.0.0.1:8080/
       ProxyPassReverse /   http://127.0.0.1:8080/
       <Location /api/> Header set Cache-Control "no-store" </Location>
   Get TLS with certbot --apache and redirect HTTP to HTTPS.
   SELinux: setsebool -P httpd_can_network_connect 1 (Apache may not connect
   to 127.0.0.1:8080 otherwise). Enable and start httpd. Until TinyBBS runs,
   the site answers 503; that is expected.
5. firewalld: allow the http and https services. Keep the Telnet port (2323)
   closed unless asked. Do not open 8080.
6. Backups: a nightly copy of /srv/tinybbs/BBS/DATA, kept 14 days (the
   folder appears later; the job should skip it while it is missing).
7. Later, when server/CMakeLists.txt exists in the repository: git pull,
   build (cmake -S server -B build && cmake --build build), install the
   binary to /usr/local/bin/tinybbs-server, install
   server/deploy/tinybbs.service to systemd, enable and start it, and check
   that https://<your-domain>/ shows the TinyBBS terminal and DIAL connects.
Report what was installed, the Apache config used, and anything that failed.
```
