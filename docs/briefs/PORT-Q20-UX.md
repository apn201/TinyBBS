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

## Done when

Every engine item on the port list is ported or recorded as not needed,
all checks pass, and `docs/briefs/PORT-Q20-UX.md` ends with a short report:
what was ported, what was skipped and why, RAM and flash before and after,
the commit range. Push to `origin main` only when all checks pass.
Redeploying Kouvosto is Juha's step, not part of this item.
