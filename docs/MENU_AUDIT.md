# Menu audit

Every screen, the keys it advertises, and who gets them. The rule: never show
an option that does not work for that caller on that transport. Checked
against the code on 2026-11-15, after the port of the Last Caller usability
work (`docs/INTERACTION.md`). "Browser" and "Telnet" are the two transports;
"SysOp" is level 255 with the SYSOP flag.

"Key screen" means a letter acts at once, no ENTER; digits and typed words
still end with ENTER. "Line screen" means everything is typed, then ENTER.
On every key screen Q goes back one level, X goes to the main menu, ? shows
the keys, and ENTER draws the screen again (or takes the forward step listed);
ENTER never leaves a level. The browser sends every key at once on a key
screen, as in the pager.

Hidden commands (typed but not advertised) are listed where they exist. They
are allowed: they never appear on screen.

## Login

| Screen | Kind | Keys | Notes |
| --- | --- | --- | --- |
| `(N)ew user, (L)ogin, or (G)oodbye?` | key | N, L, G | |
| Handle, password, real name, location | line | typed, ENTER | An empty handle or password backs out of signing up. A password shows a star per key on both transports. |
| After login | key | any key | "Press any key for the Main Board". |

## Main board

| Key | Shown when | Notes |
| --- | --- | --- |
| M, N, A, D, C, W, U, S, ?, G | always | W opens the node monitor (kick, M) for the SysOp, the WHO pager for everyone else. |
| F | an area is open to the caller (an empty upload area counts in the browser) | |
| B | a bulletin is visible to the caller | |
| J | more than one message area can be joined | |
| O | a SysOp is online | Was P. |
| K | the card has `CONFIG/NOTES.INI` | The notebook. |
| Z | SysOp only | Hidden for the SysOp: `/USER`, `/RELOAD` (also `USER`, `RELOAD` when they arrive in one go). |
| Q | not shown | Says "G logs off". |

The first main menu of a call has one hint line under it: "Press a letter. No
ENTER needed. ? shows the keys, X comes back here."

W (WHO), S (system info), bulletins and ? (key help) open in the pager.

## Messages

| Screen | Kind | Keys | Notes |
| --- | --- | --- | --- |
| Area menu | key | L, N, W, Q; A when more than one area | L was R, W was P. |
| Area list | key | number, ENTER; Q at once | "Area #, then ENTER (Q goes back at once; now in ...)". |
| Message list | key | number, ENTER; ENTER alone = first new; Q at once | |
| Reading (any length, the pager) | pager | SPACE, B, N/P, R, T, Q, X, ?; K, E for the author and SysOp; H when edited | One pager whatever the length. A single-page message has no row counter. |
| After a notice in the reader's place | key | any key | Shows the message again. |
| Kill, private | key | Y/N, one key | |
| To, subject | line | typed, ENTER | |
| Editor | line | /S /A /L /? /H /C /D n /R n | |

A caller's own posts are never counted or listed as new to them. A reply ends
at the message menu, like a new post.

## Files

| Screen | Kind | Keys | Notes |
| --- | --- | --- | --- |
| File menu | key | area number, ENTER; Q | |
| File list | key | number, ENTER opens; V asks for the number; D (asks for the number) in the browser where downloads are on; U in the browser where uploads are on; N/P with more than one page; ENTER next page; Q up a folder or back; X | `V 3` and `D 3` typed in one go still work. |
| View/download which file # | key | number, ENTER; Q cancels at once | |
| Text and ANSI reader | pager | SPACE B arrows S N/P Q X ? | S slow (modem speed). |
| ZIP contents | pager | SPACE B arrows N/P Q X ? | |
| Image viewer | browser | N P ESC, arrows, taps | Browser only; Telnet is told pictures open in the browser. |
| Music player | browser | N P SPACE ESC, arrows, taps | Browser only, and only when the player is on the card; Telnet is told music plays in the browser. |
| Files that cannot be viewed | | | Browser: "D, then #". Telnet: "from the browser terminal". |
| Upload | line | description, CHOOSE FILE; Q cancels while waiting | Browser only. |

## ANSI gallery

| Key | Shown when | Notes |
| --- | --- | --- |
| A, F | ANSI callers | |
| G | an ANSI area is open to the caller | |
| Q | always | |

## Bulletins

| Screen | Kind | Keys | Notes |
| --- | --- | --- | --- |
| Bulletin list | key | number, ENTER; Q at once | Only with more than one bulletin; one bulletin opens at once. |
| A bulletin | pager | SPACE B Q X ? | ASCII callers get the `.ASC` twin under `/BBS/ASCII/` when there is one, else the title and text. |

## Doors

Every door says how to leave on entry and takes Q (one level up) and X (the
main menu).

| Screen | Kind | Keys | Notes |
| --- | --- | --- | --- |
| Door menu | key | one per door in the door table, Q | |
| Legend of Kouvola, town | key | F H B T M S L Q ?; C when someone in town can be challenged | C asks for the handle (typed, ENTER; an empty line cancels). `C name` in one go still works. |
| Legend, tavern | key | R W L M Q ?; C as above | ENTER stays in the tavern (it used to go back to town). |
| Legend, forest fight | key | A; R or Q run; ? | |
| Legend, after a fight | key | F searches again, any other key: town | Was ENTER. |
| Legend, shop | key | W, A (then the tier, ENTER), Q | Was `W #`, `A #`. |
| Legend, bank | key | D, W (then the gold, ENTER), Q | Was `D <amount>`, `W <amount>`. |
| Legend, arena | key | A, R or Q on your turn; R while waiting; Y/N to an offer; any key after the result | X calls the duel off and tells the other fighter. |
| VolaMUD | line | HELP or ? lists LOOK, N/S/E/W, GO, TALK, BUY/SELL, INV, WEAR, TASK, BANK, SAY, FIGHT, WHO, STATS, QUIT; X | A door that reads words: Q, QUIT, EXIT leave for the door menu, X for the main menu. |
| Dice cabinet | key | bet (digits, ENTER); Q, X, ? | ENTER alone no longer leaves. An empty purse is told on entry. |

## Chat

| Screen | Kind | Keys | Notes |
| --- | --- | --- | --- |
| Node chat | line | type to talk; a lone Q, QUIT, EXIT or /Q leaves; /W; /H for the SysOp | A lone Q acts at once. The bar on the last row says `Q quit`. |

## Notebook (only with `CONFIG/NOTES.INI`)

| Screen | Kind | Keys | Notes |
| --- | --- | --- | --- |
| List | key | 1-6 open at once; N/P pages; Q; X; ? | Six notes a page, in the order seen. |
| One note | key | N, P, Q (back to the list), X, ? | Says where to read it again. |

## SysOp

| Screen | Kind | Keys | Notes |
| --- | --- | --- | --- |
| Console | key | U D W I L S R Q X ?; F in the browser | L asks for a word to filter the event log (ENTER alone: all). `L <text>` in one go still works. |
| Event log | pager | SPACE B N/P Q X ? | |
| User editor list | line | #, handle, A, N or ENTER next page, P, Q, X | Handles are typed, so this screen reads lines. |
| User record | key | 1-11 then ENTER (11 = gold, shared by all doors); N, P, Q, X at once; D for other accounts | Delete: "Type YES, then ENTER". |
| Node monitor | line | node # ENTER, M <node or ALL> text, Q, X; ENTER draws it again | |
| Door settings | line | F handle count, V handle, Q, X; ENTER draws it again | |
| Upload queue | line | A #, D #, Q, X; ENTER draws it again | A and D only when something waits. |

## Timed lines

A SysOp page, a refused page, a SysOp message, an upload notice and the idle
warning arrive on a line of their own; the prompt and what the caller had
typed are drawn again under them. An idle line is warned 30 seconds before it
closes (2 minutes before signing in, 30 minutes after); any key keeps it.
