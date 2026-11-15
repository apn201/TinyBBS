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

Filled in as the port goes on.
