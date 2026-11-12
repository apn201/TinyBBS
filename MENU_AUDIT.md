# Menu audit (slice 9)

Every screen, the keys it advertises, and who gets them. The rule: never show
an option that does not work for that caller on that transport. Checked
against the code on 2026-11-12. "Browser" and "Telnet" are the two
transports; "SysOp" is level 255 with the SYSOP flag.

Hidden commands (typed but not advertised) are listed where they exist. They
are allowed: they never appear on screen.

## Login

| Keys | Who | Notes |
| --- | --- | --- |
| N, L, G | everyone | Signing up: ENTER at the handle or the password backs out. **Fixed.** The old hidden `ASCII` toggle is removed. |

## Main board

| Key | Shown when | Notes |
| --- | --- | --- |
| M, N, A, D, C, W, U, S, ?, G | always | W opens the node monitor (kick, M) for the SysOp. |
| F | an area is open to the caller (an empty upload area counts in the browser) | |
| B | a bulletin is visible to the caller | |
| J | more than one message area can be joined | |
| P | a SysOp is online | |
| Z | SysOp only | Hidden for the SysOp: `USER`, `RELOAD`. |

## Messages

| Screen | Keys | Notes |
| --- | --- | --- |
| Area menu | R, N, P, Q; A when more than one area | Error text lists A only when shown. **Fixed.** |
| Area list | number, ENTER | |
| Reading (short message) | N, P, R, T, Q; K, E for the author and SysOp; H when edited | |
| Reading (long message, reader) | same keys in the status bar | k, e, h used to leave the reader for a message the caller cannot change; now they only act when offered, and k scrolls otherwise. **Fixed.** |
| Editor | /S /A /L /? /H /C /D n /R n | |

## Files

| Screen | Keys | Notes |
| --- | --- | --- |
| File menu | area numbers, Q | |
| File list | #, V #, Q; D # in the browser where downloads are on; U in the browser where uploads are on; N/P with more than one page | |
| Text and ANSI reader | SPACE B arrows S N/P Q | S plays at full speed on Telnet. |
| ZIP contents | SPACE B arrows N/P Q | |
| Image viewer | N P ESC, arrows, taps | Browser only; Telnet is told pictures open in the browser. |
| Music player | N P SPACE ESC, arrows, taps | Browser only, and only when the player is on the card; Telnet is told music plays in the browser. |
| Files that cannot be viewed | | Browser: "D #". Telnet: "from the browser terminal". |
| Upload | description, CHOOSE FILE | Browser only. |

## ANSI gallery

| Key | Shown when | Notes |
| --- | --- | --- |
| A, F | always | |
| G | an ANSI area is open to the caller | |
| Q | always | |

## Doors

| Screen | Keys | Notes |
| --- | --- | --- |
| Door menu | one per door in the door table, Q | |
| Legend of Kouvola, town | F H B T M S L Q; C when someone in town can be challenged | C used to show with nobody to challenge. Now the town also lists who is in town. **Fixed.** |
| Legend, tavern | R W L M Q; C as above | **Fixed.** |
| Legend, arena | A, R on your turn; R while waiting | Strikes and misses reach the waiting fighter on a new line, and the side without the initiative is told to wait. **Fixed.** |
| Legend, forest, shop, bank | as prompted | |
| VolaMUD | HELP lists LOOK, N/S/E/W, GO, TALK, BUY/SELL, INV, WEAR, TASK, BANK, SAY, FIGHT, WHO, STATS, QUIT | All handled. |
| Dice cabinet | bet, Q | An empty purse is told on entry, and when the last coin is lost, instead of asking for a bet it cannot take. **Fixed.** |

## SysOp

| Screen | Keys | Notes |
| --- | --- | --- |
| Console | U D W I S R Q; F in the browser | |
| User editor list | #, handle, A, Q; ENTER and P with more than one page | |
| User record | 1-11 (11 = gold, shared by all doors), N, P, Q; D for other accounts | D used to show on your own record and then refuse. **Fixed.** |
| Node monitor | node #, M <node or ALL> text, ENTER | |
| Door settings | F handle count, V handle, Q | |
| Upload queue | A #, D #, Q | A and D only when something waits. |
