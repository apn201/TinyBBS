# TinyBBS --- Ready-to-Code Specification

**Target:** ESP32-S3 + 1 GB SD card\
**Project goal:** A self-contained, realistic 1990s multi-line BBS that
creates its own Wi-Fi network and can be called from a browser or a real
Telnet terminal. The browser is a terminal transport, not the
application. The BBS remains usable without Internet access.

## 1. Product definition

TinyBBS is a complete BBS appliance in a tiny ESP32-S3 device. It should
feel like calling a real PCBoard-era board rather than visiting a
retro-styled website.

Authenticity is preferred over modern convenience. Preserve the period
interaction model: ANSI/CP437 screens, command prompts, conferences,
numbered messages, private mail, quoting, new-message scans, security
levels, transfer ratios, caller logs, bulletins, SysOp pages, multi-node
chat, offline mail packets, file areas, door games, time limits, user
statistics, and deliberately terse prompts.

Modern conveniences may exist only underneath the experience where
required by the transport. Browser upload/download dialogs are
acceptable; replacing the BBS file menu with a modern web file manager
is not.

The system must work with no WAN connection.

## 2. Non-negotiable architecture

The BBS core MUST be independent of the client transport.

``` text
                         +-----------------------+
                         |       BBS CORE        |
                         |-----------------------|
                         | sessions / users      |
                         | menus / commands      |
                         | messages / QWK        |
                         | files / ratios        |
                         | chat / paging         |
                         | doors / games         |
                         | sysop / logs / stats  |
                         +-----------+-----------+
                                     |
                              Virtual terminal
                                     |
                  +------------------+------------------+
                  |                  |                  |
            WebSocket/HTTP        TCP/23             future
             Browser ANSI          Telnet              SSH
                  |                  |                  |
             phone/tablet       real terminal       real terminal
```

No feature may depend directly on HTML UI. A Telnet caller and browser
caller must be able to occupy adjacent nodes and see one another in
WHO/chat.

## 3. Build principles

1.  Fork the known-working Tiny IRC ESP32 project rather than rebuilding
    Wi-Fi/AP/WebSocket infrastructure from zero.
2.  Extract networking, persistent storage and connection handling into
    reusable services.
3.  Build a BBS session state machine above the transport layer.
4.  Send terminal bytes/ANSI outward and receive keystrokes/lines
    inward.
5.  Keep content on SD wherever practical so the archive can be edited
    on a PC.
6.  Keep firmware assets/configuration small enough that changing BBS
    content does not normally require recompiling firmware.
7.  Prefer append/journal/atomic-replace storage patterns. A power loss
    must not casually destroy the user or message database.
8.  PSRAM is working memory, not permanent storage.
9.  Avoid loading large file lists, message bases, QWK packets or
    archives completely into RAM. Stream.
10. Every major subsystem gets an interface so it can be replaced
    without rewriting the BBS shell.

## 4. Network behaviour

### 4.1 Access point

Default configuration:

``` ini
ssid=THE_BOARD
password=
ip=192.168.4.1
max_wifi_clients=16
```

SSID and limits are configurable on SD.

The board does not require Internet access. DNS should resolve arbitrary
hostnames to the BBS landing page where practical, producing a
captive-portal-like experience.

### 4.2 Services

  Service               Default Purpose
  ----------- ----------------- ------------------------------------------------------
  HTTP                       80 landing page, browser terminal, file transfer bridge
  WebSocket        HTTP upgrade interactive browser terminal
  Telnet                     23 real ANSI BBS connection
  SSH           disabled/future optional secure terminal transport

### 4.3 Node allocation

A successful interactive connection receives a BBS node number.

Target initial limit: **8 active BBS sessions**. Design for 16 if memory
permits after profiling.

Wi-Fi association count and active BBS node count are separate concepts.

Inactive sessions should be timed out conservatively. Browser
reconnection should be capable of reclaiming a recently disconnected
authenticated session using a short-lived opaque reconnect token.

## 5. Browser dialer

The browser client must look like a terminal, not a web application.

### Required

-   80-column logical ANSI terminal.
-   CP437 glyph mapping.
-   16 ANSI colors.
-   bold/high intensity.
-   blink where practical.
-   cursor addressing.
-   clear screen/line.
-   cursor show/hide.
-   CR/LF handling.
-   backspace.
-   terminal scrollback outside strict full-screen screens.
-   WebSocket transport.
-   keyboard input.
-   mobile soft keys for ESC, CTRL, arrows, TAB, ENTER and function keys
    where needed.
-   landscape-friendly layout.
-   browser file upload/download bridge.
-   reconnect screen when Wi-Fi/session is lost.

### Dial sequence

Optional per-client setting:

1.  black screen;
2.  modem off-hook/dial/ring/handshake audio;
3.  fake negotiation text if configured;
4.  `CONNECT 28800` or configured speed;
5.  BBS terminal session starts.

The sound is presentation only. It must never delay Telnet connections.

Audio assets live on SD and may be replaced by the owner.

## 6. Terminal/ANSI compatibility

Implement a deliberately bounded terminal parser rather than a complete
VT emulator.

Required ANSI/VT sequences:

-   SGR colors 0--7 / bright 8--15.
-   reset/bold/blink/reverse where feasible.
-   cursor position.
-   cursor up/down/left/right.
-   erase display.
-   erase line.
-   save/restore cursor if used by art.
-   home.
-   CR, LF, TAB, BS.
-   optional ANSI music is out of scope initially.

Store art as `.ANS`. Preserve CP437 byte semantics where possible.

Terminal capability record per session:

``` text
ansi: yes/no
color: yes/no
columns: 80
rows: 25/variable
transport: web/telnet/ssh
expert_mode: yes/no
```

ASCII fallback must remain possible for a non-ANSI Telnet client.

## 7. Connection and login flow

### New connection

1.  allocate node;
2.  detect/ask ANSI;
3.  display `CONNECT`/welcome screen as appropriate;
4.  show system identification;
5.  ask new/returning caller;
6.  authenticate or run new-user application;
7.  display bulletins required for caller security level;
8.  show last-call information;
9.  show caller number and time remaining;
10. scan new private mail;
11. optionally scan conferences for new messages;
12. enter Main Board.

### New-user application

Configurable fields:

-   handle;
-   real name;
-   password;
-   location;
-   computer;
-   modem speed fiction;
-   screen length;
-   ANSI;
-   date of birth optional;
-   email optional and local-only;
-   comment/application text;
-   challenge question optional;
-   acceptance of board rules.

The SysOp can configure whether new users are automatically validated or
receive a restricted security level.

Passwords must be salted and hashed. Never store plaintext passwords.

## 8. User database

Minimum user record:

``` text
user_id
handle
handle_normalized
real_name
password_hash
location
created_at
first_call_at
last_call_at
last_logoff_at
total_calls
total_minutes
security_level
flags
validated
locked
expiration_date
daily_time_limit_minutes
time_used_today
messages_posted
private_messages_sent
uploads
upload_bytes
downloads
download_bytes
credits
ratio_exempt
qwk_enabled
qwk_last_packet_at
ansi_enabled
screen_rows
expert_mode
preferred_protocol
last_node
last_ip_hash_optional
comment
```

Per-conference user state:

``` text
user_id
conference_id
last_read_message_id
joined
qwk_selected
```

Door state belongs to each door, not the core user record.

## 9. Security levels

Use numeric PCBoard-style levels, e.g. 0--255.

Example defaults:

``` text
0   locked
10  new/unvalidated
20  normal
50  trusted
100 co-sysop
255 sysop
```

Every conference, file area, door, bulletin and SysOp function may
define a minimum security level and optional flags.

Do not hard-code these example values into feature logic.

## 10. Main command shell

The Main Board is command-driven.

Example default commands:

``` text
B  Bulletins
C  Chat
D  Doors
F  Files
G  Goodbye
J  Join conference
M  Messages
N  New message scan
O  Offline/QWK
P  Page SysOp
S  System information
U  User list
W  Who is online
X  Expert mode
?  Help
```

Command aliases may be configured.

Menus should be content files (`.ANS` plus menu definition), not
hard-coded art.

## 11. Conferences

A conference is the primary BBS partition.

Fields:

``` text
conference_id
name
description
minimum_security
flags
message_areas[]
file_areas[]
doors[]
welcome_screen
menu_screen
```

Conference 0 is the default Main Board.

Users may join conferences with `J`.

New-message scans must respect per-user pointers.

## 12. Message system

This must behave like a real BBS message base, not a modern forum.

### Message fields

``` text
message_id
conference_id
area_id
thread_root_id
reply_to_id
from_user_id
from_display
to_user_id nullable
to_display
subject
created_at
body
private
deleted
sysop_only
```

### Required operations

-   list/scan message headers;
-   read by number;
-   next/previous;
-   read new;
-   scan all selected conferences;
-   post;
-   reply;
-   quote;
-   private message;
-   public message;
-   kill own message if policy allows;
-   SysOp kill/move;
-   search subject/from/to/text;
-   reset last-read pointer;
-   thread navigation;
-   download selected message text;
-   upload/import prepared messages through offline mail subsystem.

Display remains classic:

``` text
Msg #: 1482
From : THE SHADOW
To   : ALL
Subj : ANYONE TRIED OS/2 WARP?
Date : 11-01-96 22:48

<body>

[1482] Next, Reply, Quote, Kill, Again, Thread?
```

Bodies are plain text. Preserve line wrapping predictably at BBS width.

## 13. QWK offline mail

Implement real QWK packet generation/import closely enough to
interoperate with conventional QWK readers where feasible.

Required:

-   conference selection;
-   new-message pointers;
-   packet creation;
-   `CONTROL.DAT`;
-   `MESSAGES.DAT`;
-   conference mapping;
-   bulletin/welcome/news inclusion where applicable;
-   downloadable archive;
-   reply packet upload;
-   reply import;
-   duplicate/import validation;
-   pointer reset.

Use ZIP as the initial packet container if compatible with the selected
QWK implementation/tooling.

QWK construction must stream to SD. Do not build the entire packet in
RAM.

Test interoperability against at least one desktop QWK reader during
development.

## 14. File system

The SD card is the real file archive.

Each file area has:

``` text
area_id
conference_id
name
description
path
minimum_security
upload_allowed
download_allowed
ratio_policy
```

Per-file metadata:

``` text
file_id
area_id
filename
size
uploaded_at
uploader_user_id
uploader_name
downloads
description
extended_description
crc32
sha256 optional
approved
hidden
```

### Required commands

-   list areas;
-   list files;
-   new-file scan since last call / specified date;
-   search filename;
-   search description;
-   view description;
-   download;
-   upload;
-   upload description;
-   SysOp approve/reject/move/rename/delete;
-   top uploads/downloads;
-   user transfer statistics.

### FILE_ID.DIZ

When an uploaded ZIP contains `FILE_ID.DIZ`, inspect it and offer/use
its text as the description.

Do not extract arbitrary archive contents into the filesystem. Only
inspect bounded metadata safely.

### Browser transfers

The terminal command initiates a browser-native transfer overlay/dialog,
then returns to the ANSI terminal.

The browser UI must not expose arbitrary SD paths.

### Telnet transfers

Phase 1: file download may be unavailable or use a temporary HTTP token
displayed to the caller.

Later: implement XMODEM/YMODEM/ZMODEM behind a transfer interface.
ZMODEM is the preferred eventual target.

## 15. Upload/download ratios and credits

Support both ratio and credit models.

Configurable examples:

``` text
download_ratio_files=10
download_ratio_bytes=10
upload_credit_multiplier=1.0
new_user_free_bytes=1048576
```

Security levels/flags may exempt users.

Transfers are committed transactionally: interrupted downloads should
not increment completed-download statistics; interrupted uploads remain
in `/TEMP` and are cleaned later.

Provide classic statistics:

``` text
Files Uploaded
Bytes Uploaded
Files Downloaded
Bytes Downloaded
Current Ratio
Credits
```

## 16. Chat and paging

Reuse/refactor Tiny IRC concepts into the BBS node system.

Required:

-   public multi-node chat;
-   node/handle identification;
-   direct/private node chat;
-   page another user;
-   page SysOp;
-   SysOp answer;
-   broadcast system message;
-   enter/leave notifications configurable;
-   chat scrollback bounded in RAM and optionally logged.

`W`/WHO must show real activity:

``` text
Node  User       Activity
1     SYSOP      Available
2     APN201     Reading messages
3     FRODO      Downloading DOOM1.ZIP
4     ACIDBURN   Playing a door
```

Every subsystem updates session activity.

## 17. Physical SysOp integration

The device LCD is a live BBS status panel.

Normal rotation may show:

-   BBS name;
-   uptime;
-   active/maximum nodes;
-   total calls;
-   messages today;
-   uploads/downloads;
-   current node events;
-   Wi-Fi clients;
-   SD free space.

When a caller pages the SysOp:

``` text
*** SYSOP PAGE ***
NODE 4
ACIDBURN
```

If hardware buttons/touch are available, allow `ANSWER` / `IGNORE`.

Answering creates a direct SysOp chat path associated with that node.

## 18. Doors framework

Doors are native TinyBBS applications, not DOS executables.

Define a stable interface:

``` text
DoorContext
  session/node
  user identity
  security level/flags
  time remaining
  credits
  terminal capabilities
  input/output callbacks
  persistent storage namespace
  BBS event API
```

Lifecycle:

``` text
door_init()
door_enter(context)
door_input(context, key/line)
door_tick(context)
door_leave(context, reason)
```

A door owns the terminal until it exits.

Maintain an internal `DOOR.SYS`-inspired snapshot for
debugging/authenticity and future compatibility.

### Initial door set

1.  **Fantasy RPG** inspired by classic daily-turn BBS RPG mechanics,
    with original implementation/data.
2.  **Space trader** with a small persistent shared universe.
3.  **Trivia**.
4.  **Blackjack/slots** using BBS credits only; no real-money
    functionality.
5.  **Graffiti wall**.
6.  **Voting booth/polls**.

Daily-turn games use BBS-local date rollover.

## 19. Bulletins and news

Bulletins are numbered content entries with:

``` text
id
title
path
minimum_security
mandatory
changed_at
```

Support:

-   mandatory new-user bulletin;
-   changed-since-last-call marker;
-   numbered bulletin menu;
-   ANSI or text content.

Content lives on SD.

## 20. Caller log and statistics

Append a caller/event log.

Events include:

-   connect;
-   login success/failure;
-   new user;
-   logoff;
-   timeout;
-   message post/delete;
-   upload/download;
-   QWK export/import;
-   door enter/exit;
-   SysOp action;
-   node page;
-   errors.

Daily aggregate statistics:

-   callers;
-   unique callers;
-   new users;
-   messages;
-   upload/download bytes;
-   peak nodes;
-   door plays;
-   uptime.

Expose period-style stats screens and top lists.

## 21. SysOp console

Logging in with SysOp privilege exposes additional commands.

Required management:

-   users: search/edit/validate/lock/security;
-   nodes: inspect/page/chat/kick;
-   messages: delete/move;
-   conferences/areas: inspect;
-   files: approve/move/edit/delete;
-   uploads: pending queue;
-   bulletins;
-   doors: enable/disable/reset where supported;
-   broadcast;
-   logs;
-   system stats;
-   SD/storage status;
-   configuration reload where safe;
-   graceful reboot/shutdown.

Dangerous operations require explicit confirmation.

## 22. Time accounting

Track session time and optional daily limits.

Time spent in file transfer may optionally not count, configurable.

Warnings at configurable thresholds:

``` text
10 minutes remaining
5 minutes remaining
1 minute remaining
```

SysOp/exempt users ignore limits.

## 23. Goodbye/logoff

`G` displays session statistics:

``` text
Time online
Messages read
Messages posted
Files uploaded/downloaded
Door time
```

Then show configurable goodbye ANSI and disconnect after a short delay.

Browser returns to the dialer. Telnet socket closes.

## 24. Persistence design

Prefer a simple embedded database/index layer with explicit crash
recovery.

Requirements:

-   atomic critical updates;
-   journal or append log;
-   startup integrity check;
-   periodic checkpoint;
-   bounded indexes;
-   no full-database load into RAM;
-   backup/export to SD;
-   rebuildable file index from filesystem;
-   human-readable configuration.

Suggested split:

``` text
/BBS/DATA/USERS      core user database
/BBS/DATA/MSGS       message records/indexes
/BBS/DATA/STATE      pointers, counters, node-independent state
/BBS/LOGS            append logs
/BBS/FILES           actual downloadable content
```

If SQLite on the selected ESP32 stack proves reliable and memory-safe,
it is acceptable. Otherwise use purpose-built append-only records +
compact indexes. Do not choose the database before benchmarking it on
the actual board.

## 25. SD-card content model

All replaceable historical/presentation content belongs on SD:

-   ANSI screens;
-   ASCII screens;
-   modem audio;
-   bulletins;
-   rules;
-   help;
-   file archive;
-   door assets/data;
-   QWK temporary packets;
-   user uploads;
-   menu definitions;
-   configuration.

Firmware should boot with sensible emergency text screens if optional
art is missing.

## 26. Configuration

Use human-editable INI/TOML-like files rather than recompilation.

Example groups:

``` text
SYSTEM.INI
NETWORK.INI
SECURITY.INI
RATIOS.INI
CONFERENCES.INI
FILEAREAS.INI
DOORS.INI
BULLETINS.INI
```

Validate configuration at boot. Invalid optional entries are disabled
and logged rather than crashing the board.

## 27. SD path safety

All user-facing file operations are sandboxed to configured file areas.

Reject:

-   `..`;
-   absolute paths;
-   embedded path separators in upload filename;
-   device names/problematic filenames;
-   overlong names;
-   duplicate overwrite without policy;
-   content exceeding configured upload/file-area limits.

Write uploads to `/BBS/TEMP/UPLOADS/<token>.part`, validate, then
atomically move.

## 28. Web transfer bridge

Browser terminal sends a BBS transfer request.

Server returns a short-lived opaque token bound to:

-   authenticated session;
-   node;
-   file ID or upload target;
-   direction;
-   expiration;
-   one-time use.

HTTP transfer endpoint validates token and streams bytes.

Never put raw filesystem paths in URLs.

## 29. Performance targets

Initial targets, to be measured on hardware:

-   8 simultaneous active BBS nodes without visible typing lag.
-   16 associated Wi-Fi clients.
-   terminal keystroke echo \<100 ms locally under normal load.
-   file transfer must not starve interactive nodes.
-   QWK creation may be slower but must yield to session/network work.
-   SD operations chunked.
-   no individual node may allocate an unbounded buffer.
-   ANSI screens streamed from SD.
-   file listings paginated/indexed.
-   chat history bounded.

Use cooperative tasks/queues rather than long blocking loops.

## 30. Concurrency model

Suggested tasks:

``` text
network/AP task
HTTP/WebSocket task
Telnet acceptor
session scheduler
storage worker
file-transfer worker
maintenance task
LCD/UI task
```

Session state machines should be non-blocking.

Storage mutations should pass through a serialized storage service where
practical to reduce corruption/race risk.

## 31. Session state machine

Illustrative top-level states:

``` text
CONNECTED
TERMINAL_DETECT
WELCOME
LOGIN
NEW_USER
BULLETINS
MAIN
CONFERENCE
MESSAGES
FILES
CHAT
QWK
DOOR
SYSOP
GOODBYE
DISCONNECTED
```

Each subsystem may have nested states.

Never implement a menu as a blocking `while()` that owns the CPU waiting
for serial input.

## 32. Activity abstraction

Every session publishes a short activity enum/text:

``` text
LOGIN
MAIN_MENU
READING_MESSAGES
POSTING_MESSAGE
FILE_LIST
UPLOADING
DOWNLOADING
CHAT
DOOR:<id>
QWK
SYSOP
IDLE
```

WHO, LCD and logs consume this common state.

## 33. Content authenticity

Prefer genuine archived material supplied by the owner.

Useful content:

-   old TXT/FAQ documents;
-   ANSI/ASCII art;
-   shareware/freeware archives;
-   old source code;
-   demos;
-   MOD/S3M/XM/MIDI;
-   GIF/JPEG collections;
-   BBS utilities;
-   README/NFO/DIZ files;
-   period documentation;
-   old BBS lists;
-   locally written bulletins.

Preserve original filenames and timestamps when importing where
practical.

Create an offline PC-side indexing utility later that scans the SD
archive, extracts safe `FILE_ID.DIZ` metadata, calculates hashes and
generates TinyBBS file indexes.

## 34. Authenticity details to retain

Do not modernize away:

-   caller number;
-   node number;
-   security level;
-   daily time remaining;
-   `More?` paging;
-   `(Y/N)` prompts;
-   command letters;
-   numbered conferences;
-   numbered messages;
-   new-message scan;
-   private mail;
-   quoting;
-   upload/download ratios;
-   top uploader lists;
-   new-file scans;
-   bulletins;
-   user lists;
-   SysOp page;
-   caller history;
-   transfer protocol preference;
-   expert mode;
-   terse errors;
-   fake modem speed;
-   optional modem sound;
-   ANSI-heavy menus;
-   door transitions;
-   logoff statistics.

## 35. Modern features that must stay invisible

Use modern engineering internally for:

-   password hashing;
-   storage integrity;
-   reconnect tokens;
-   HTTP upload/download;
-   Unicode-to-CP437 handling;
-   browser mobile keyboard;
-   hashing/checksums;
-   crash recovery;
-   configuration validation.

Do not expose these as a modern dashboard during normal BBS use.

## 36. Boot sequence

1.  initialize serial diagnostics;
2.  initialize LCD;
3.  mount SD;
4.  validate `/BBS`;
5.  load config;
6.  recover incomplete transactions;
7.  open/check databases;
8.  build/load indexes;
9.  initialize door registry;
10. start AP;
11. start DNS/HTTP/WebSocket;
12. start Telnet;
13. start maintenance tasks;
14. show `WAITING FOR CALL` on LCD.

If SD is missing/corrupt, enter a clear maintenance mode instead of
formatting automatically.

## 37. Maintenance

Periodic jobs:

-   expire reconnect tokens;
-   clean abandoned upload parts;
-   rotate logs;
-   checkpoint databases;
-   daily counters rollover;
-   daily door rollover;
-   recompute free space warning;
-   remove expired QWK/temp files;
-   optionally write a compact status snapshot.

## 38. Backup

Provide a SysOp operation that quiesces writes briefly and creates a
consistent backup set under `/BBS/BACKUP`.

Never include large downloadable file archives in routine metadata
backup unless explicitly requested; the archive itself can be copied
separately.

## 39. Logging/debug modes

Serial debug output must never leak into caller terminal streams.

Compile/runtime log levels:

``` text
ERROR
WARN
INFO
DEBUG
TRACE
```

Subsystem tags:

``` text
NET
WEB
TELNET
SESSION
AUTH
MSG
FILE
QWK
CHAT
DOOR
DB
SD
LCD
SYSOP
```

## 40. Firmware source structure

Recommended Arduino/PlatformIO source tree:

``` text
src/
  main.cpp
  app/
    TinyBBSApp.*
  config/
    Config.*
  net/
    AccessPoint.*
    CaptiveDns.*
    HttpServer.*
    WebSocketTransport.*
    TelnetServer.*
  terminal/
    TerminalSession.*
    AnsiWriter.*
    Cp437.*
    Pager.*
  bbs/
    Session.*
    SessionManager.*
    StateMachine.*
    CommandRouter.*
    Activity.*
    menus/
    auth/
    users/
    conferences/
    messages/
    files/
    qwk/
    chat/
    bulletins/
    stats/
    sysop/
  doors/
    Door.*
    DoorRegistry.*
    fantasy/
    trader/
    trivia/
    casino/
    graffiti/
    voting/
  storage/
    StorageService.*
    AtomicFile.*
    Journal.*
    Index.*
  display/
    LcdStatus.*
    SysopPage.*
  util/
    Clock.*
    Hash.*
    Random.*
    Text.*
    RingBuffer.*
web/
  index.html
  terminal.js
  ansi.js
  cp437.js
  terminal.css
  audio.js
```

## 41. SD-card directory structure

The supplied companion ZIP contains this empty tree.

``` text
BBS/
├── ANSI/
│   ├── LOGIN/
│   ├── MENUS/
│   ├── CONFERENCES/
│   ├── FILES/
│   ├── DOORS/
│   ├── SYSOP/
│   └── GOODBYE/
├── ASCII/
│   ├── LOGIN/
│   ├── MENUS/
│   └── HELP/
├── AUDIO/
│   └── MODEM/
├── BULLETINS/
├── CONFIG/
├── DATA/
│   ├── USERS/
│   ├── MSGS/
│   ├── STATE/
│   └── INDEX/
├── CONFERENCES/
│   ├── 000_MAIN/
│   │   └── MESSAGES/
│   └── 001_GENERAL/
│       └── MESSAGES/
├── FILES/
│   ├── 001_BBS_SOFTWARE/
│   ├── 002_DOS_UTILITIES/
│   ├── 003_WINDOWS/
│   ├── 004_GAMES/
│   ├── 005_DEMOS/
│   ├── 006_ANSI_ASCII/
│   ├── 007_MUSIC/
│   ├── 008_PROGRAMMING/
│   ├── 009_INTERNET/
│   ├── 010_TEXTFILES/
│   ├── 011_UPLOADS/
│   └── 012_SYSOP/
├── UPLOADS/
│   ├── INCOMING/
│   ├── APPROVED/
│   └── REJECTED/
├── QWK/
│   ├── OUT/
│   ├── IN/
│   └── WORK/
├── DOORS/
│   ├── FANTASY/
│   │   ├── ANSI/
│   │   ├── DATA/
│   │   └── ASSETS/
│   ├── TRADER/
│   │   ├── ANSI/
│   │   ├── DATA/
│   │   └── ASSETS/
│   ├── TRIVIA/
│   │   ├── ANSI/
│   │   ├── DATA/
│   │   └── ASSETS/
│   ├── CASINO/
│   │   ├── ANSI/
│   │   ├── DATA/
│   │   └── ASSETS/
│   ├── GRAFFITI/
│   │   ├── ANSI/
│   │   └── DATA/
│   └── VOTING/
│       ├── ANSI/
│       └── DATA/
├── HELP/
├── LOGS/
│   ├── CALLERS/
│   ├── EVENTS/
│   ├── TRANSFERS/
│   └── ERRORS/
├── STATS/
├── TEMP/
│   ├── UPLOADS/
│   ├── DOWNLOADS/
│   ├── QWK/
│   └── WORK/
└── BACKUP/
```

## 42. SD content placement rules

Put historical downloadable material in `/BBS/FILES/<area>/`.

Put ANSI art in `/BBS/ANSI/...`.

Put modem recordings in `/BBS/AUDIO/MODEM/`.

Do **not** manually populate `/BBS/DATA`, `/BBS/TEMP`, `/BBS/QWK/WORK`,
logs or generated indexes unless specifically instructed by tooling.

`011_UPLOADS` is the public file area after approval. Raw incoming
caller uploads first land under `/BBS/UPLOADS/INCOMING`.

## 43. Implementation slices

### Slice 0 --- freeze Tiny IRC baseline

-   archive known-working firmware;
-   document board, pins, display, SD, PSRAM and libraries;
-   verify AP/browser/chat baseline;
-   create branch `tinybbs`.

**Done when:** original Tiny IRC still builds and runs from the frozen
tag.

### Slice 1 --- terminal transport skeleton

-   transport interface;
-   session manager;
-   WebSocket transport;
-   Telnet transport;
-   ANSI writer;
-   CP437 browser renderer;
-   80×25 terminal;
-   node allocation;
-   activity state.

**Demo:** browser Node 1 and Telnet Node 2 see the same ANSI welcome and
WHO list.

### Slice 2 --- login and user database

-   new user;
-   returning login;
-   password hashing;
-   security levels;
-   caller number;
-   caller log;
-   time accounting;
-   last call;
-   goodbye.

**Demo:** create user by phone, log off, reconnect via Telnet with same
account.

### Slice 3 --- menus/conferences/messages

-   SD ANSI menus;
-   command router;
-   conferences;
-   message areas;
-   post/read/reply/quote/private;
-   last-read pointers;
-   new scan.

**Demo:** Node 1 posts; Node 2 scans and replies; Node 1 sees new reply.

### Slice 4 --- chat/paging/physical status

-   public chat;
-   direct chat;
-   WHO;
-   SysOp page;
-   LCD events;
-   broadcast.

**Demo:** phone pages SysOp; physical display alerts; SysOp answers from
another node.

### Slice 5 --- files

-   file areas;
-   indexes;
-   browser upload/download;
-   ratios/credits;
-   descriptions;
-   FILE_ID.DIZ;
-   new-file scan;
-   SysOp approval.

**Demo:** upload a ZIP, extract DIZ description, approve it, download
from another account, ratio changes.

### Slice 6 --- QWK

-   conference selection;
-   packet generator;
-   download;
-   reply upload/import;
-   pointer handling.

**Demo:** messages leave the ESP32, are read/replied offline, replies
return to the board.

### Slice 7 --- doors framework

-   Door API;
-   registry;
-   door menu;
-   persistent door namespace;
-   time accounting;
-   first simple door.

**Demo:** two users enter same persistent game world and affect shared
state.

### Slice 8 --- flagship doors

-   fantasy RPG;
-   space trader;
-   trivia;
-   graffiti;
-   voting;
-   optional casino/credits.

### Slice 9 --- archive/time capsule

-   PC-side file indexing utility;
-   bulk metadata import;
-   real archived files;
-   real ANSI;
-   historical timestamps;
-   DIZ indexing;
-   search optimization.

### Slice 10 --- authenticity/polish

-   modem audio;
-   fake negotiation;
-   expert mode;
-   detailed stats;
-   top lists;
-   mandatory bulletins;
-   better ASCII fallback;
-   period-correct prompts;
-   browser mobile polish.

### Slice 11 --- advanced transfers/transports

-   investigate ZMODEM;
-   X/YMODEM if useful;
-   SSH feasibility;
-   performance profiling at max nodes.

Do not allow Slice 11 to delay the complete browser + Telnet BBS.

## 44. Acceptance test --- "full shit"

Release 1.0 is not complete until this scenario works:

1.  power the standalone ESP32;
2.  phone joins its Wi-Fi with no Internet;
3.  browser dialer plays optional modem handshake;
4.  user creates an account through ANSI prompts;
5.  laptop simultaneously connects over Telnet as another node;
6.  both appear in WHO;
7.  first user joins a conference and posts a message;
8.  second user performs new-message scan, reads it, quotes it and
    replies;
9.  first user pages second user and enters chat;
10. first user uploads a real ZIP with description/DIZ;
11. SysOp validates/moves the upload;
12. second user downloads it;
13. ratios/statistics update correctly;
14. user generates and downloads a QWK packet;
15. a reply packet can be uploaded/imported;
16. users enter a persistent door game;
17. LCD reflects node activity and a SysOp page;
18. caller logs and daily statistics record the activity;
19. both users log off and receive session statistics;
20. power cycle;
21. accounts, messages, file metadata, ratios, pointers and door state
    remain correct.

If this works, TinyBBS is a BBS, not a themed web demo.

## 45. Definition of success

A person familiar with 1990s BBS systems should be able to connect by
Telnet without being told it is an ESP32 web project and immediately
recognize the interaction model.

A person with only a phone should be able to experience essentially the
same board through Wi-Fi and the browser terminal.

The ESP32, SD card and its own Wi-Fi network must be sufficient to
contain the whole experience.
