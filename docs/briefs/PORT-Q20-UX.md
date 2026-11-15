# Brief - Port the Last Caller usability work (Q20 to Q22) to TinyBBS

From: Claude Code (planner), at Juha's request, 14 Nov 2026. To: Kiro.

Last Caller (`../Last_Caller`) is a game built on this engine. In its item
Q20 it ran a usability loop with testers who never used a BBS and changed
the shared engine: the same key means the same action everywhere, letters
act at once, ENTER only for typed text and numbers, one pager for all text,
clear ways out of chat and doors. Port the engine part here, so TinyBBS and
`kouvosto.helppox.com` get the same interface.

## Read first

1. `../Last_Caller/docs/PLATFORM_UX.md`: the audit, the interaction
   contract (C1 to C10), the moved keys, and the section "Port to TinyBBS",
   which lists each change, its Last Caller commits, the files, and what is
   engine (port) and what is HALCYON story (do not port).
2. `../Last_Caller/.kiro/steering/interaction.md` (the contract as rules).
3. `../Last_Caller/docs/reviews/UX-LOOP.md` (why each change was made).
4. This repository's `AGENTS.md`, `docs/ROADMAP.md`, `docs/MENU_AUDIT.md`.

Read Last Caller only; never edit it.

## Scope

The port list now runs past P47 (Q20 keys and pager, Q21 notebook engine:
notes table, collect on show, notebook viewer, scene replay). Q22 is still
adding items in Last Caller while you work. Port everything on the list
when you start; at the end, re-read the list and port what was added, and
write the last P number ported in the report. The notebook engine is
useful on an ordinary board only if a card uses it: port it as an engine
feature that does nothing without a notes table, and do not add HALCYON
notes.

## What to do

1. Work through the port list in order. For each engine change, read its
   Last Caller commits (`git -C ../Last_Caller show <sha>`) and apply the
   same behaviour here. The two trees have drifted, so port by hand where a
   patch does not apply; never copy HALCYON story text, scenes, ELIZA,
   the prologue, the manifest or card content from `../Last_Caller/sd/`.
2. TinyBBS has doors and areas Last Caller removed (Legend, dice, VolaMUD,
   the FidoNet message history, music areas). Give each the same contract:
   Q leaves, X goes to the main menu, letters at once, ENTER only for typed
   input, the shared pager for text. Record any door that cannot follow it
   and why.
3. Where a key moved in Last Caller (for example post W, read L, page
   SysOp O, earlier/later P/N), move it here too and update every help
   screen, menu file and text on `sd/BBS/` that names the old key (key
   names only). Never show a menu option that does not work for that caller
   on that transport.
4. Copy the contract into this repository as a document
   (`docs/INTERACTION.md`) and update `docs/MENU_AUDIT.md` and `README.md`.
5. Small commits, prefixed `UX port:`, each naming the Last Caller commits
   it ports.

## Checks

`pio run` (report RAM and flash before and after), the server build and
smoke test from `AGENTS.md` on a disposable copied card, and a Telnet walk
through every menu and door on that server copy: log in, read and post a
message, files, every door in and out, chat in and out, `?` and `X` from
deep inside, log off. Never claim a check passed if it did not run.

No Docker or WSL on this PC: do not start Docker. The Linux server build
runs where Linux is; if none is reachable from this run, say in the report
that the server build and Telnet walk did not run here and will run at
deploy. `pio run` must pass here.

## Done when

Every engine item on the port list is ported or recorded as not needed,
all checks pass, and `docs/briefs/PORT-Q20-UX.md` ends with a short report:
what was ported, what was skipped and why, RAM and flash before and after,
the commit range. Push to `origin main` only when all checks pass.
Redeploying Kouvosto is Juha's step, not part of this item.

## Report (Kiro, 15 Nov 2026)

Ported the engine part of the Last Caller port list, P1 to P48 (P48 was
added by Q22 while this ran; it needs the scene engine and is not needed
here). The full item-by-item table is in `docs/INTERACTION.md`, "Port
status"; the moved keys are in the same file.

**Ported.** Letters act at once at every menu, list, pager and yes/no, on
Telnet and in the browser (key mode 3, Backspace), with a typed word still
read as a word when it arrives in one go (P2). ENTER never leaves a level; Q
back, X main menu and ? keys on every key screen; one pager for messages,
threads, bulletins, WHO, system info and the key help; O pages the SysOp, W
writes, L lists; V and D ask for the file number; Y/N and "Press any key" are
one key (P3). One-page texts drop the row counter, the pager's keys always fit
79 columns, ? names S slow (P10, P21, P47). Chat leaves with a lone Q, QUIT,
EXIT or /Q and says so (P11). A reply ends like a post (P12); own posts are
never new (P13). The key hint under the first main menu (P17), number prompts
that say Q goes back at once (P18), password stars on Telnet (P26). Pages,
refusals, SysOp messages and upload notices arrive on their own line with the
prompt drawn again (P15), and the idle warning 30 s before the line closes
(P43). The notebook engine: `/BBS/CONFIG/NOTES.INI`, collected on show,
K on the main menu; it does nothing and shows nothing without the file, and
the repository's card has none (P39, P40). A Telnet walk,
`tools/test/telnet_walk.py`, stands in for Last Caller's e2e (P7).

**Doors and Kouvosto screens given the contract.** Legend of Kouvola: letters
at once, Q one level up, X main menu from any screen (a duel is called off and
the other fighter told), ? lists the keys, C, W/A (shop) and D/W (bank) ask
for the name, tier or amount, the results and the arena offer take one key.
The dice cabinet: letters at once, ENTER no longer leaves. VolaMUD stays a
line door (it reads words): it says on entry that Q, QUIT or EXIT leave and X
goes to the main menu, and takes X and ?. SysOp screens: ENTER draws the
screen again instead of leaving; the user record takes N, P, D, Q, X at once;
L asks for the log word. The FidoNet history and the music areas are read
through the shared pager and file list, so they follow it without changes.
Every door follows the contract; none was left out.

**Skipped, and why.** Last Caller story (HALCYON) items: P4, P14, P33 to P35,
P37, P42. Items that need its scene and trigger engine, which TinyBBS does not
have: P8, P16, P24, P25, P32, P36, P38, P41, P44, P45, P48. Snake (P9, P30):
TinyBBS has no Snake. P6: no read-only areas here. P46: the reader here takes
no message number. P28: N)ew already logs in at once. P20 and P23 (tester
agents and their MCP tool) drive Last Caller's staging; the Telnet walk covers
their task list. P19/P27: the C label stays "Node chat". P31 needed no change.
No text on `sd/BBS/` named a key that moved (`MENUS/DOORS_V2.ANS` lists only
door keys, which did not move).

**Bug found by the walk and fixed.** A key run by the hotkey tick set
`lastAt` after the tick had read the time, so the idle check saw hours of
silence and hung up a line now and then (`src/bbs/runtime.h`).

**Checks.**
- `pio run`: passes. Before: RAM 58156 bytes, flash 1482319 bytes. After:
  RAM 58612 bytes (+456), flash 1505479 bytes (+23160).
- Server build (`cmake -S server -B server/build`, `cmake --build server/build`)
  and `tinybbs-phase1-smoke` on a disposable copied card: pass (exit 0, and
  `LOGIN OK: SYSOP` with the account the walk made). Run on the Rocky host
  `helppox.com` in a scratch folder, since this PC has no Linux; Docker was
  not used.
- Telnet walk on a disposable server card copy (loopback ports): 0 failures,
  ten runs in a row after the idle fix (one earlier miss was the walk reading
  too briefly on a busy host; it now waits up to 2 s for the first byte). It signs up a SysOp and a caller, reads,
  posts and replies, opens a file area, goes into and out of every door
  (Legend town, tavern, shop, bank, forest; dice; VolaMUD), chat in and out on
  both nodes, a SysOp page and its refusal, ? and X from inside a message, a
  file list, a door and the SysOp console, the user editor and node monitor,
  and logs off. A notebook check on another copy with a test `NOTES.INI`
  (not committed) and the idle warning on a line not signed in: pass.
- Not run: a test on the Pocket-Dongle-S3 itself (the firmware was built, not
  flashed) and a browser session; the browser part (key mode 3) is covered only
  by the same board code the walk drives.

**Last item.** P48, re-read at the end. Last Caller has newer Q22 commits that
are not on the port list yet (`d69d8bc` SPACE at "Press any key", `e597dc4`
why a line closed in the event log, `5334cec` the server retries EINTR on a
socket); they were not ported. `5334cec` touches the server platform shared
with this repository and is worth a port item.

**Commits.** `6027bd5` to the last `UX port:` commit (14 commits), all
prefixed `UX port:`.

PORT DONE

## Addition 1 (Claude Code, 15 Nov 2026): P49 and P50

Last Caller Q22 added two engine items after this port: P49 (SPACE at
"Press any key" is a key; Telnet keeps output a full send buffer did not
take; LOGOFF in the event log says why the line closed) and P50 (every
idle and timer check measures time with `msSince(now, at)`, because a key
run in the same tick stamps `lastAt` after `now` and plain unsigned
`now - lastAt` wraps to about 49 days and closes the line). Read their
rows in `../Last_Caller/docs/PLATFORM_UX.md` and the commits 814b876,
1b09d29, 5334cec, e597dc4, d69d8bc and a2105d8 (engine parts only; the
powered console is HALCYON). Port them, check every `millis()` subtraction
in the shared code, run `pio run`, the server build and smoke test and the
Telnet walk on helppox.com as before (no Docker), push to `origin main`
when they pass, and end this file with a short report and a line starting
`PORT 2 DONE`.


## Report, Addition 1 (Kiro, 15 Nov 2026)

Ported P49 and P50 from Last Caller Q22 (814b876, 1b09d29, 5334cec, e597dc4, d69d8bc, a2105d8, engine parts only).

**Ported.** `msSince(now, at)` in `src/core/util.h`, used by the idle check, the web carrier check, the hotkey delay and the LOGOFF text. This tree already clamped the idle check by hand; the other three were plain unsigned subtractions. The other `millis()` subtractions in shared code (Telnet and WebSocket send timers, board clock, goodbye minutes, pacing) take a fresh `millis()` or a stamp set by the same code, so they cannot wrap. LOGOFF in the event log now says why the line closed and how long since the last key. Telnet keeps the part of the output a full send buffer did not take. The server platform retries EINTR on a socket and logs `[NET] drop ... errno=` to stderr. SPACE at the bulletin "Press any key" is a key, not a pause.

**Skipped.** The powered console (`CONSOLE_ON`, maintenance login) and the scene keys notice: HALCYON.

**Checks.**
- `pio run`: passes. RAM 58612 bytes (unchanged), flash 1505827 bytes (+348 against the end of the first port).
- Server build and `tinybbs-phase1-smoke` on a disposable copy on helppox.com (scratch folder, removed afterwards, user `lastcaller`, no Docker): pass, exit 0.
- Telnet walk on disposable server card copies, with a test file area added to each copy: 0 failures in 4 runs. One further run found no server because I reused a port; it is not counted. The event log showed `goodbye (last key 0 s ago)` for each logoff and the server log held no `[NET]` drop lines.
- Not run: the SPACE-at-banner key (the walk does not send SPACE at a drawing bulletin), a test on the Pocket-Dongle-S3, a browser session. `../Last_Caller` was read only.

**Commits.** `383e449` to the `UX port:` commit after it (P50, LOGOFF, Telnet, server, SPACE, docs).

PORT 2 DONE
