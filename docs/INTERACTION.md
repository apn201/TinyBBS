# Interaction contract

What every screen promises a caller, on Telnet and in the browser alike. It
comes from the usability loop in Last Caller (`../Last_Caller/docs/PLATFORM_UX.md`,
items Q20 to Q22), a game built on this engine, where testers who had never
used a BBS called the board. The 1990s look, the art and the wording stay;
the friction goes.

"Key" means one character. "Line" means characters plus ENTER.

## Rules

**C1. Keys and lines, one rule.** At a menu, a list, a pager or a yes/no
question, a letter acts at once. No ENTER. At a prompt that wants text or a
number, ENTER ends the line, and the prompt says "number, then ENTER" or
similar. Digits start a number: they are never a command, and ENTER ends
them. A menu prompt never says "ENTER" to leave.

**C2. Global keys.** On every key screen, and never inside text entry:

| Key | Meaning |
| --- | --- |
| Q | back one level |
| X | main menu, from anywhere |
| ? | the keys of this screen |
| N | next (page, message, file page, user page) |
| P | previous |
| SPACE | next page, at the end the next item |
| ENTER | the screen's forward step (next page, next message, first new); in a menu it draws the menu again. It never leaves a level. |
| ESC | the same as Q (the browser; Telnet clients that send ESC alone reach the pager only) |
| B | back one page, in the pager |

**C3. Same kind of action, same key.** L list, W write a post, R reply, N next
or new, P previous, O page the SysOp, K kill, E edit, H history, T thread,
D download, V view, U upload. A new screen takes its letters from this list.
If a letter would need a second meaning, move the odd action to another
letter and record it below under "Moved keys".

**C4. One pager.** Every text a caller reads goes through the pager: a message
of one line or of a hundred, a bulletin, a file, WHO, the system info, the
key help. The footer looks the same everywhere: title, rows (left out when the
text fits on one screen), then the keys, which always fit in 79 columns. A
screen adds its own keys (`R reply  T thread`). Menus and lists of options are
not documents: they are screens of keys.

**C5. Yes or no is one key.** `[Y/N]`, no ENTER. A destructive confirmation
that needs a word (deleting an account) keeps the word and says so.

**C6. Continue is any key.** "Press ENTER" is gone. The prompt is "Press any
key" and any key does it, ENTER too.

**C7. A timed event never swallows a key or cuts a line.** A SysOp page, a
node message or the idle warning arrives on its own line, and the prompt and
whatever the caller had typed are drawn again under it.

**C8. How a chat or a door is left.** Every chat and door says how on entry
and takes Q (in chat, a lone Q or `/Q`), and the words QUIT and EXIT where it
reads words. X goes to the main menu. HELP and ? list the commands.

**C9. The browser adds buttons, never rules.** A button sends the key a
keyboard would. At a key screen the browser sends every key at once, as it
does in the pager.

**C10. Typed words still work.** A word typed at a key screen (a SysOp's
`RELOAD`, `L <text>`, `V 3`, Legend's `C <name>`) works when it arrives in one
go: pasted, sent by a tool, or by a Telnet client in line mode. A single
letter on its own acts at once. The board tells the two apart by time: a
letter with no key behind it within 120 ms is a key.

Never show a key that does not work for that caller on that transport.

A change to a key or a prompt updates `docs/MENU_AUDIT.md`, the key help
(`src/bbs/help.h`), `tools/test/telnet_walk.py` and any text on the card that
names the key.

## Moved keys

| Screen | Old | New | Why |
| --- | --- | --- | --- |
| Main | P page the SysOp | O | P is previous everywhere else |
| Message menu | P post | W | same; W is write |
| Message menu | R read or list | L | R is reply |
| Main, menus, doors, SysOp console | ENTER leaves | ENTER draws the screen again, Q leaves | ENTER never leaves a level |
| Every screen | (none) | X main menu, ? keys | there was no way back to the top from deep inside |
| All "Press ENTER" | ENTER | any key | C6 |
| Yes or no | Y or N, then ENTER | one key | C5 |
| File list | `V #`, `D #` | V and D ask for the number; a bare number still opens | a letter acts at once |
| User list | ENTER next page | N (ENTER too) | N is next |
| Chat | `/Q` only | `/Q`, a lone Q, QUIT or EXIT | C8 |
| Legend, town and tavern | `C <name>` | C asks for the name | a letter acts at once |
| Legend, shop | `W #`, `A #` | W and A ask for the tier | same |
| Legend, bank | `D <gold>`, `W <gold>` | D and W ask for the amount | same |
| Legend, results | ENTER town | any key town, F forest again | C6 |
| SysOp user editor list | ENTER next page | N (ENTER too) | N is next |
| SysOp console | `L <text>` | L asks for the word (`L <text>` in one go still works) | a letter acts at once |
| Main (SysOp) | `RELOAD`, `USER` | also `/RELOAD`, `/USER` | a letter acts at once; a slash starts a typed word |
| Main | (none) | K notebook, only on a card with `NOTES.INI` | K is kill only in the message reader, a different screen |

Not moved, on purpose: D is the door menu on the main board and download in
a file list; U is the user list on the main board and upload in a file list;
S is system info on the main board and slow in the pager. The pairs never
share a screen. SysOp tools that take handles or command words (user editor
list, node monitor, door settings, upload queue) and VolaMUD stay line
screens: they take Q, X and ?, and ENTER draws them again.

## Before the port (audit, 14 Nov 2026)

The engine came from TinyBBS, so Last Caller's audit
(`../Last_Caller/docs/PLATFORM_UX.md`, section 1) holds here for the shared
screens. The rows for what only Kouvosto has:

| Screen | Input before | ENTER did | Q did | Leave by |
| --- | --- | --- | --- | --- |
| Legend, town | line | "Use F, H, ..." | door menu | Q |
| Legend, tavern | line | back to town | back to town | Q or ENTER |
| Legend, after a fight | line | back to town | back to town | ENTER |
| Legend, shop and bank | line, `W 2`, `D 50` | back to town | back to town | Q or ENTER |
| Legend, arena | line | asked again | retreat or decline | Q, R |
| Dice cabinet | line, a bet | door menu | door menu | Q or ENTER |
| VolaMUD | line, words | HELP | door menu | Q, QUIT, EXIT |
| SysOp door settings, upload queue, node monitor | line | back | back | Q or ENTER |
| SysOp user editor list | line | next page | console | Q |
| SysOp user record | line | user list | user list | Q or ENTER |

No door said on entry how to leave it, and none took X.

## Port status

The Last Caller port list (`../Last_Caller/docs/PLATFORM_UX.md`, section 4),
P1 to P50, as it stands in TinyBBS. "Not needed" items are Last Caller story
(HALCYON) or need its scene and trigger engine, which TinyBBS does not have.

| # | Change | Here |
| --- | --- | --- |
| P1 | Contract and audit | This file |
| P2 | Letters act at once (hotkey tick, browser key mode 3, Backspace) | Ported |
| P3 | ENTER never leaves; Q, X, ?; one pager; O, W, L; FILE_ASK | Ported; doors, SysOp screens and Legend's typed commands added |
| P4 | Manifest keys, shell leave words | Not needed (HALCYON) |
| P5 | Story lint `check_keys` | Not needed: no story text names keys here |
| P6 | W in a read-only area says why | Not needed: TinyBBS has no read-only areas |
| P7 | e2e follows the new keys | Ported as `tools/test/telnet_walk.py` (TinyBBS had no e2e) |
| P8 | Numbered scene choices act at once | Not needed (no scenes) |
| P9 | Snake keys | Not needed (no Snake); the same rule given to Legend and the dice |
| P10 | One-page texts drop the row counter | Ported |
| P11 | Chat leaves with Q, QUIT, EXIT, /Q | Ported |
| P12 | Reply save ends like a post | Ported |
| P13 | Own posts are never new | Ported (no read-only login part) |
| P14 | Front page puzzle hint | Not needed (HALCYON) |
| P15 | A page draws the prompt again | Ported as `timedLine()`: pages, refusals, SysOp messages, upload notices |
| P16 | A scene starts on a fresh line | Not needed (no scenes) |
| P17 | Key hint under the first main menu | Ported |
| P18 | Number prompts say Q goes back at once | Ported |
| P19, P27 | C label | Kept "Node chat": there is no scene before the chat here |
| P20 | UX tester agents | Not ported: they drive Last Caller's `bbs-caller` MCP tool and HALCYON staging; the Telnet walk covers the fixed task list |
| P21 | Pager keys always fit in 79 columns | Ported |
| P22 | X leaves a line door | Ported to VolaMUD, Legend and the dice |
| P23 | Testers one at a time | Not needed (no `bbs-caller` tool) |
| P24, P25, P32, P36, P38, P41, P44, P45, P48 | Scene and trigger timing, scene choices, replay, typed_else, type-ahead | Not needed (no scene or trigger engine) |
| P26 | Password stars on Telnet | Ported |
| P28 | Sign-up in a door logs in at once | Not needed: N)ew already logs in at once |
| P29 | A door's yes or no is one key | Ported (Legend arena offer) |
| P30 | Snake prompt says no ENTER | Not needed (no Snake) |
| P31 | Chat echo order after a fast reply | Not changed: no scripted replies here; a SysOp reply clears the line and draws the prompt again (`broadcastChat`) |
| P33, P34, P35, P37, P42 | Crew accounts, manifest notebook, story notes and their lint | Not needed (HALCYON) |
| P39 | Notes table, collect on show | Ported: `/BBS/CONFIG/NOTES.INI`, `src/core/notes.h`; does nothing without the file |
| P40 | Notebook screens | Ported: `src/bbs/notebook.h`, K on the main menu; no scene list or logins page (no scenes or crew logins here) |
| P43 | Idle warning, any key keeps the line | Ported (no Login: after a sign-in here, so no 10-minute case) |
| P46 | Digits in the reader redraw only the bar | Not needed: the TinyBBS reader takes no message number |
| P47 | ? line names S | Ported as "S slow" |
| P49 | SPACE at "Press any key" is a key; Telnet keeps output a full send buffer did not take; LOGOFF says why the line closed; server retries EINTR | Ported (the powered console is HALCYON, not needed) |
| P50 | Idle and timer checks use `msSince(now, at)` | Ported: idle check, web carrier check, hotkey delay, LOGOFF and the Telnet send timer. The other `millis()` subtractions take a fresh `millis()` or a stamp the same code just set |