#!/usr/bin/env python3
"""Telnet walk through every menu and door (docs/INTERACTION.md).

Run against a disposable server card copy, never a live board:

    ./server/build/tinybbs-server --root /tmp/card --http 127.0.0.1:18471 --telnet 127.0.0.1:12371 &
    python3 tools/test/telnet_walk.py 127.0.0.1 12371

It signs up a SysOp (the first SYSOP account on a fresh card) and a caller,
walks the main board, messages, files, every door in and out, chat in and
out, the SysOp console, ? and X from deep inside, a SysOp page, and logs off.
Keys are sent the way a person types them: one letter, no ENTER, at a key
screen; a line and ENTER where a prompt asks for one. Exit code 0 = every
step saw what it expected. -v prints the whole transcript.
"""
import re
import socket
import sys
import time

IAC, DONT, DO, WONT, WILL, SB, SE = 255, 254, 253, 252, 251, 250, 240
ANSI = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]|\x1b\][^\x07]*\x07|\x1b[78]")


class Caller:
    def __init__(self, host, port, name):
        self.name = name
        self.sock = socket.create_connection((host, port), timeout=5)
        self.sock.setblocking(False)
        self.seen = ""      # text since the last send, ANSI stripped
        self.log = []
        self.key_at = 0.0   # when the last single key went out

    def _read(self, quiet=0.35, limit=6.0):
        data = bytearray()
        start = last = time.time()
        while time.time() - start < limit:
            try:
                chunk = self.sock.recv(65536)
                if not chunk:
                    break
                data += chunk
                last = time.time()
            except BlockingIOError:
                # Wait up to 2 s for the first byte, then until the line is quiet.
                if (data and time.time() - last >= quiet) or (not data and time.time() - start >= 2.0):
                    break
                time.sleep(0.02)
        out, i = bytearray(), 0
        while i < len(data):   # answer Telnet options with no, keep the text
            b = data[i]
            if b == IAC and i + 1 < len(data):
                cmd = data[i + 1]
                if cmd in (DO, DONT, WILL, WONT) and i + 2 < len(data):
                    if cmd in (DO, WILL):
                        self.sock.send(bytes([IAC, WONT if cmd == DO else DONT, data[i + 2]]))
                    i += 3
                    continue
                if cmd == SB:
                    j = data.find(bytes([IAC, SE]), i)
                    i = j + 2 if j >= 0 else len(data)
                    continue
                i += 2
                continue
            out.append(b)
            i += 1
        text = ANSI.sub("", out.decode("utf-8", "replace"))
        self.log.append(text)
        return text

    def send(self, keys, quiet=0.35):
        self.sock.send(keys.encode())
        self.seen = self._read(quiet)
        return self.seen

    def _after_key(self):
        # The board takes an ENTER within 0.7 s of a key as that key's own
        # (a line-mode client sends one after each key); a person is slower.
        wait = self.key_at + 1.0 - time.time()
        if wait > 0:
            time.sleep(wait)

    def key(self, k):
        """One key, as a person presses it: the board acts on it alone."""
        self.key_at = time.time()
        return self.send(k)

    def enter(self):
        self._after_key()
        return self.send("\r")

    def line(self, text):
        """Typed text, then ENTER: characters one by one, like a keyboard."""
        self._after_key()
        for ch in text:
            self.sock.send(ch.encode())
            time.sleep(0.03)
        return self.send("\r")

    def close(self):
        self.sock.close()


failures = []
verbose = "-v" in sys.argv


def expect(caller, step, *needles):
    text = caller.seen
    missing = [n for n in needles if n not in text]
    status = "ok  " if not missing else "FAIL"
    print(f"{status} {caller.name}: {step}")
    if missing:
        failures.append(f"{caller.name}: {step}: missing {missing!r}")
        print("     got: " + text[-600:].replace("\r", "").replace("\n", " | "))
    return not missing


def refuse(caller, step, *needles):
    present = [n for n in needles if n in caller.seen]
    if present:
        failures.append(f"{caller.name}: {step}: unexpected {present!r}")
        print(f"FAIL {caller.name}: {step}: unexpected {present!r}")
        print("     got: " + caller.seen[-600:].replace("\r", "").replace("\n", " | "))


def sign_up(c, handle, password):
    c.seen = c._read(0.6)
    expect(c, "front page", "(N)ew user")
    c.key("N"); expect(c, "N acts at once", "Handle")
    c.line(handle); expect(c, "password prompt", "Password")
    c.line(password); expect(c, "password shows stars", "*" * len(password))
    c.line("Walk Tester"); c.line("Testville")
    expect(c, "account made, any key for the board", "Press any key")
    c.key("x"); expect(c, "any key opens the main menu, key hint", "Main Board Command", "No ENTER needed")


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    host, port = sys.argv[1], int(sys.argv[2])
    op = Caller(host, port, "SYSOP")
    sign_up(op, "SYSOP", "walkpass1")
    refuse(op, "main menu has no old P key label", "[P] Page")
    expect(op, "SysOp sees Z", "SysOp console")

    op.key("?"); expect(op, "? opens the key help in the pager", "KEYS", "back one level")
    op.key("q"); expect(op, "Q leaves the help", "Main Board Command")
    op.enter(); expect(op, "ENTER redraws the main menu", "Main Board Command")
    op.key("Q"); expect(op, "Q at the main menu says how to log off", "G logs off")

    # Messages
    op.key("M"); expect(op, "message menu keys", "List messages", "Write a message", "Message command")
    op.enter(); expect(op, "ENTER redraws the message menu", "Message command")
    op.key("W"); expect(op, "W writes", "To (ENTER = ALL)")
    op.line(""); expect(op, "subject", "Subject")
    op.line("Walk test"); op.line("Hello from the Telnet walk."); op.line("Second line.")
    op.line("/S"); expect(op, "posted, back at the message menu", "posted", "Message command")
    op.key("N"); expect(op, "own post is not new", "No new messages")
    op.key("W"); op.line(""); op.line("Second post"); op.line("Another line."); op.line("/S")
    op.key("L"); expect(op, "L lists", "Message #, then ENTER", "Walk test")
    op.enter(); expect(op, "ENTER opens a message in the pager", "Q back", "X main", "Walk test")
    op.key("?"); expect(op, "? in the pager", "G top")
    op.key("n"); expect(op, "N next message", "Second post")
    op.key("R"); expect(op, "R replies", "Replying to")
    op.line("A reply from the walk."); op.line("/S"); expect(op, "a reply ends at the message menu", "posted", "Message command")
    op.key("A")
    if "Area #" in op.seen:
        expect(op, "area list says Q goes back at once", "Q goes back at once")
        op.key("Q"); expect(op, "Q at once from the area list", "Message command")
    op.key("L"); op.enter(); op.key("x"); expect(op, "X from inside a message goes to the main menu", "Main Board Command")

    # Files
    op.key("F"); expect(op, "file cabinets", "File area #, then ENTER")
    op.line("1"); expect(op, "a file area", "File command", "[X] Main")
    op.key("V"); expect(op, "V asks for the number", "View which file #")
    op.key("q"); expect(op, "Q cancels at once", "File command")
    op.key("?"); expect(op, "? in the file list", "KEYS")
    op.key("q"); expect(op, "back to the file list", "File command")
    op.key("X"); expect(op, "X from the file list", "Main Board Command")
    op.key("A"); expect(op, "gallery", "Gallery command")
    op.key("Q"); expect(op, "Q leaves the gallery", "Main Board Command")

    # Doors
    op.key("D"); expect(op, "door menu", "Door command")
    op.enter(); expect(op, "ENTER redraws the door menu", "Door command")
    op.key("L"); expect(op, "Legend: how to leave on entry", "Q leaves the town", "Town command")
    op.key("?"); expect(op, "Legend ? keys", "Keys:", "X main menu")
    op.key("T"); expect(op, "tavern", "Tavern command")
    op.enter(); expect(op, "ENTER stays in the tavern", "Tavern command")
    op.key("Q"); expect(op, "Q tavern to town", "Town command")
    op.key("S"); expect(op, "shop", "[W] Weapon")
    op.key("W"); expect(op, "W asks for the tier", "tier 1-3")
    op.key("q"); expect(op, "Q cancels the question", "[W] Weapon")
    op.key("Q"); op.key("B"); expect(op, "bank", "[D] Deposit")
    op.key("D"); op.line("5"); expect(op, "deposit after the question", "Purse:")
    op.key("Q"); op.key("F"); expect(op, "forest fight", "[A]ttack")
    op.key("R"); expect(op, "run, any key for town", "any other key")
    op.key("k"); expect(op, "any key goes to town", "Town command")
    op.key("Q"); expect(op, "Q leaves Legend for the door menu", "Door command")
    op.key("D"); expect(op, "dice: how to leave", "Q leaves", "Bet 1-50")
    op.enter(); expect(op, "ENTER does not leave the dice", "Bet 1-50")
    op.key("Q"); expect(op, "Q leaves the dice", "Door command")
    op.key("V"); expect(op, "VolaMUD: how to leave", "QUIT or EXIT", "X for the main menu")
    op.line("LOOK"); op.line("?"); expect(op, "VolaMUD ? is HELP", "QUIT")
    op.line("X"); expect(op, "X leaves VolaMUD to the main menu", "Main Board Command")
    op.key("D"); op.key("L"); op.key("T"); op.key("X"); expect(op, "X from the tavern", "Main Board Command")

    # A second caller: WHO, page, chat
    walker = Caller(host, port, "WALKER")
    sign_up(walker, "WALKER", "walkpass2")
    walker.key("W"); expect(walker, "WHO in the pager", "Who is online", "SYSOP")
    walker.key("q"); expect(walker, "Q leaves WHO", "Main Board Command")
    walker.key("O"); expect(walker, "O pages the SysOp", "Paging the SysOp")
    op.seen = op._read(0.6)
    expect(op, "page arrives with the prompt drawn again", "SYSOP PAGE", "Main Board Command")
    walker.key("x"); expect(walker, "any key after the page", "Main Board Command")
    op.line("/R"); walker.seen = walker._read(0.6)
    expect(walker, "refusal arrives with the prompt drawn again", "busy", "Main Board Command")
    walker.key("C"); expect(walker, "chat says how to leave", "Q leaves")
    op.key("C"); walker.line("hello from walker")
    op.seen = op._read(0.6); expect(op, "chat line arrives", "hello from walker")
    walker.line("QUIT"); expect(walker, "QUIT leaves chat", "Main Board Command")
    op.key("q"); expect(op, "a lone Q leaves chat", "Main Board Command")
    walker.key("S"); expect(walker, "system info in the pager", "Your stats")
    walker.key("x"); walker.key("U"); expect(walker, "user list", "Q returns to board")
    walker.key("q"); walker.key("B"); expect(walker, "bulletin list", "Bulletin #, then ENTER")
    walker.line("1"); expect(walker, "a bulletin in the pager", "Q back")
    walker.key("q"); expect(walker, "back to the bulletin list", "Bulletin #")
    walker.key("Q"); expect(walker, "Q at once from the list", "Main Board Command")
    walker.send("M\r"); expect(walker, "a line-mode M then ENTER still works", "Message command")
    walker.key("X"); walker.key("G"); expect(walker, "log off", "Time online")
    walker.close()

    # SysOp console
    op.key("Z"); expect(op, "SysOp console", "SysOp command")
    op.enter(); expect(op, "ENTER redraws the console", "SysOp command")
    op.key("L"); expect(op, "L asks for a filter", "Show lines with this word")
    op.line("LOGIN"); expect(op, "filtered log in the pager", "Log: LOGIN")
    op.key("q"); expect(op, "back to the console", "SysOp command")
    op.key("U"); expect(op, "user editor", "User editor")
    op.line("1"); expect(op, "a record", "Field #, then ENTER")
    op.key("N"); expect(op, "N next record at once", "Field #")
    op.key("Q"); expect(op, "Q back to the list", "User editor")
    op.line("Q"); op.key("W"); expect(op, "node monitor", "Node # to disconnect")
    op.enter(); expect(op, "ENTER redraws the monitor", "Node # to disconnect")
    op.line("Q"); expect(op, "Q back to the console", "SysOp command")
    op.key("X"); expect(op, "X from the console", "Main Board Command")
    op.key("G"); expect(op, "log off", "Time online")
    op.close()

    if verbose:
        for c in (op, walker):
            print(f"===== {c.name} =====")
            print("".join(c.log).replace("\r", ""))
    print(f"\n{len(failures)} failure(s)")
    for f in failures:
        print("  " + f)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
