#!/usr/bin/env python3
"""Seed the message bases with 1990s FidoNet traffic, names anonymised.

Source: the ExecPC BBS FidoNet capture (1993-1999) archived by Josh Renaud at
https://breakintochat.com/collections/messages/fidonet/ . The 150 MB ZIP is
downloaded once into tools/.cache/ (git-ignored).

For each conference below, the newest messages of its echoes are written to
sd/BBS/MSGBASE/<conference id>/<ECHO>.TXT. Every real name, handle, node
address, BBS name, phone number, e-mail address and web address is replaced
with an invented one. The same real person always gets the same invented
name, so threads still read naturally.

Usage:  python tools/fidonet-seed.py [--per-conference 150] [--out sd/BBS/MSGBASE]

Output format, one file per echo:

    ; comment lines
    #MSG
    From: Ray Lindqvist
    To: All
    Subject: Re: Wonder Man
    Date: 1998-03-13 03:31
    Reply: 12            (optional: number of the message in this file it answers)

    body...

The board reads these files at boot and on RELOAD as read-only history.
"""
import argparse
import hashlib
import os
import re
import sys
import zipfile

from bbscard import fetch_to

ZIP_URL = 'https://breakintochat.com/collections/messages/fidonet/fidonet-messages-execpc.zip'
HERE = os.path.dirname(os.path.abspath(__file__))
CACHE = os.path.join(HERE, '.cache')

# conference id -> (echo name used for the file, [ExecPC topic numbers])
CONFERENCES = {
    1: ('MUSIC', [166]),
    4: ('DOORGAMES', [103, 124]),
    5: ('VIDEOGAMES', [170]),
    6: ('STARTREK', [108]),
    7: ('MODEMS', [116, 242, 267]),
    8: ('LINUX', [241]),
    9: ('COMICS', [101]),
}

FIRST = """Aaron Adam Al Alan Albert Alex Andy Arnie Barry Ben Bert Bob Brad Brian Bruce Bud Carl Chad Chris
Chuck Clark Cliff Clint Craig Curt Dale Dan Darren Dean Denny Derek Don Doug Duane Dwight Ed Eddie Elliot
Eric Ernie Floyd Fred Gary Gene Glen Gordon Greg Gus Hal Hank Harold Herb Howard Ira Jake Jay Jeff Jerry Jim
Joel Jon Kevin Keith Kirk Kurt Lance Larry Lee Len Leon Lloyd Lou Lyle Marty Matt Max Mel Mitch Morris Nate
Neil Norm Otis Owen Pat Perry Phil Ralph Randy Ray Rex Rick Rob Rod Roger Ron Ross Roy Russ Sam Scott Seth
Shane Sid Stan Steve Stu Ted Terry Todd Tom Tony Troy Vance Vern Vic Wade Walt Wayne Wes Zack
Amy Ann Barb Beth Carol Cindy Dana Deb Donna Ellen Gail Gwen Heidi Holly Irene Jan Jean Jill Joan Judy Karen
Kim Laura Linda Lisa Liz Lori Lynn Marge Mary Meg Nancy Nina Pam Patty Peg Rita Rose Ruth Sally Sue Tina
Trish Val Vicky Wendy""".split()
LAST = """Abbott Ackerly Aldane Alvarado Ambrose Ashby Bancroft Barlow Beckett Benning Birch Blaylock Boone
Bramwell Brennan Brook Calloway Carver Castell Chambers Colby Conklin Corwin Crandall Crowley Dalton Danvers
Darrow Decker Dillard Dorsey Draper Dunmore Eastman Elkins Ellery Embry Farley Fenwick Fielding Fisk Fletcher
Forsythe Galloway Garrison Gilmore Gorman Grady Grisham Halvorsen Hammond Harlan Hartwell Hastings Hayward
Hendrix Hollis Holt Hutton Ingram Jarvis Jessup Kessler Kimball Kingsley Kinkaid Lambert Langley Larkin
Lindqvist Lockwood Lowell Maddox Marsden Mayhew McAllister Mercer Merritt Monroe Morrow Nash Newcomb Norwood
Oakley Odell Ormsby Pace Packard Parrish Pemberton Pickett Platt Prescott Quill Radford Ramsey Redding Ridley
Rourke Rowland Sadler Sawyer Shelby Sinclair Slade Stanton Sterling Stroud Sutter Talbot Tanner Thorne Tolliver
Upton Vail Vance Vickers Wakefield Walker Ward Waverly Weston Whitlock Wilder Winslow Wolcott Yardley York""".split()
BBS_A = 'Midnight Neon Copper Silver Crimson Iron Static Lunar Hidden Rusty Electric Velvet Broken Frozen Twin Lost Blue Black'.split()
BBS_B = 'Modem Castle Tower Basement Garage Lounge Outpost Harbor Lair Station Asylum Workshop Den Shack Vault Attic Junction'.split()

# First names that are also ordinary words: replaced only in clear name positions.
AMBIGUOUS = set('WILL MARK BILL GRANT RICH FRANK SANDY DAWN APRIL MAY JUNE GENE ART GUY EARL JACK MATT PAT ROB RAY '
                'SUE JOY HOPE FAITH ROSE BUD CHUCK DON GLEN HANK LANCE LEE MAX NORM ROD WADE WES CLIFF DEAN REX '
                'NICK BOB SKIP RUSTY WOODY CHIP BUCK DUKE KING SONNY LOU BEN JAN KIM VAL'.split())
COMMON_WORDS = set('ALL SYSOP THE AND YOU HELLO DEAR WHO WHAT THIS THAT THERE HERE WELL STILL BROWN WHITE BLACK GREEN '
                   'YOUNG LONG SMALL LITTLE BIG HALL HILL STONE WOOD LAKE FIELD BANKS LOVE KING BELL RICE SHORT HOLE '
                   'PAGE CASH GOLD SILVER FOX WOLF BIRD FISH HART CROSS PRICE LAW BAKER COOK CARTER MASON MILLER'.split())


def stable(seed, n):
    return int(hashlib.sha1(seed.encode()).hexdigest(), 16) % n


class Anonymiser:
    def __init__(self):
        self.people = {}       # real full name (upper) -> fake full name
        self.used = set()
        self.first = {}        # real first name (upper) -> fake first
        self.last = {}         # real last name (upper) -> fake last
        self.boards = {}
        self.nodes = {}

    def person(self, real, first=True):
        key = ' '.join(real.upper().split())
        if not key or key in ('ALL', 'SYSOP', 'EVERYONE', 'EVERYBODY'):
            return key.title() if key else 'All'
        if key in self.people:
            if first:
                self.first.setdefault(key.split()[0], self.people[key].split()[0])
            return self.people[key]
        for salt in range(1000):
            fake = FIRST[stable(key + str(salt), len(FIRST))] + ' ' + LAST[stable(key + 'L' + str(salt), len(LAST))]
            if fake.upper() != key and fake not in self.used:
                break
        self.used.add(fake)
        self.people[key] = fake
        parts, fparts = key.split(), fake.split()
        if parts:
            if first:
                self.first.setdefault(parts[0], fparts[0])
            if len(parts) > 1 and len(parts[-1]) >= 3 and parts[-1] not in COMMON_WORDS:
                self.last.setdefault(parts[-1], fparts[-1])
        return fake

    def board(self, real):
        key = real.strip().upper()
        if key not in self.boards:
            a = BBS_A[stable(key, len(BBS_A))]
            b = BBS_B[stable(key + 'b', len(BBS_B))]
            self.boards[key] = f'The {a} {b} BBS'
        return self.boards[key]

    def node(self, real):
        if real not in self.nodes:
            h = stable(real, 10**8)
            self.nodes[real] = f'1:{100 + h % 899}/{1 + (h // 1000) % 999}'
        return self.nodes[real]


def initials(name):
    return ''.join(p[0] for p in name.split() if p[0].isalpha()).upper()[:3]


NODE_RE = re.compile(r'\b[1-6]:\d{1,5}/\d{1,5}(?:\.\d{1,5})?(?:@\w+)?\b')
PHONE_RE = re.compile(r'(?<!\d)(?:\+?1[-. ]?)?\(?\d{3}\)?[-. /]\d{3}[-. ]\d{4}(?!\d)')
PHONE7_RE = re.compile(r'(?<![\d/])\d{3}[-.]\d{4}(?!\d)')
EMAIL_RE = re.compile(r'[\w.+-]+@[\w-]+(?:\.[\w-]+)+')
URL_RE = re.compile(r'(?:https?://|ftp://|www\.)[^\s"\'<>)]+', re.I)
ORIGIN_RE = re.compile(r'^\s*\*\s*Origin:.*$', re.I)


def scrub_line(line, anon, words):
    if ORIGIN_RE.match(line):
        nodes = NODE_RE.findall(line)
        body = re.sub(r'\([^()]*\)\s*$', '', line.split(':', 1)[1]).strip()
        return ' * Origin: ' + anon.board(body) + ' (' + anon.node(nodes[-1] if nodes else body) + ')'
    line = EMAIL_RE.sub(lambda m: 'user@bbs.invalid', line)
    line = URL_RE.sub('www.example.com', line)
    line = NODE_RE.sub(lambda m: anon.node(m.group(0)), line)
    line = PHONE_RE.sub('555-0199', line)
    line = PHONE7_RE.sub('555-0199', line)
    return words(line)


def make_word_replacer(anon):
    """Replace known real full names, last names and first names in text."""
    full = sorted(anon.people.items(), key=lambda kv: -len(kv[0]))
    full_re = re.compile(r'\b(' + '|'.join(re.escape(k).replace(r'\ ', r'\s+') for k, _ in full) + r')\b', re.I) if full else None
    fullmap = {k: v for k, v in full}
    lasts = {k: v for k, v in anon.last.items() if k not in AMBIGUOUS}
    last_re = re.compile(r'\b(' + '|'.join(map(re.escape, sorted(lasts, key=len, reverse=True))) + r')\b', re.I) if lasts else None
    firsts = {k: v for k, v in anon.first.items() if len(k) >= 2 and k not in COMMON_WORDS}
    first_re = re.compile(r'\b(' + '|'.join(map(re.escape, sorted(firsts, key=len, reverse=True))) + r')\b') if firsts else None

    def case_like(src, fake):
        if src.isupper() and len(src) > 1:
            return fake.upper()
        return fake

    def words(line):
        if full_re:
            line = full_re.sub(lambda m: case_like(m.group(0), fullmap[' '.join(m.group(0).upper().split())]), line)
        if last_re:
            def lr(m):
                w = m.group(0)
                if not (w[0].isupper()):
                    return w
                return case_like(w, lasts[w.upper()])
            line = last_re.sub(lr, line)
        if first_re:
            def fr(m):
                w = m.group(0)
                if not w[0].isupper():
                    return w
                key = w.upper()
                if w.isupper() and len(w) > 1 and key in AMBIGUOUS:
                    return w
                if key in AMBIGUOUS:
                    # Only in name positions: "Hi Mark", "Mark," at line start, "-Mark", "Mark wrote".
                    before = line[max(0, m.start() - 8):m.start()].lower()
                    after = line[m.end():m.end() + 7].lower()
                    if not (re.search(r'(hi|hello|hey|dear|thanks|thank you|-)\s*$', before) or
                            re.match(r'\s*(wrote|said|,\s*$|!|:)', after) or line.strip() == w):
                        return w
                return case_like(w, firsts[key])
            line = first_re.sub(fr, line)
        return line
    return words


HEAD_RE = re.compile(r'==> TOPIC: (\d+) [^\r\n]*?\s+Ref: \S+ Date: (\d\d)/(\d\d)/(\d\d)')
FROM_RE = re.compile(r'^From:\s*(.*?)\s{2,}Time:\s*(\d{1,2}):(\d\d)\s*([ap]m)', re.I)
TO_RE = re.compile(r'^\W*To:\s*(.*?)\s{2,}(?:\(|$)', re.I)
SUBJ_RE = re.compile(r'^Subj:\s*(.*)$', re.I)


def read_messages(zpath, topics):
    """Yield (topic, message dict) for the wanted topics, in archive order."""
    z = zipfile.ZipFile(zpath)
    with z.open('fidonet-messages-execpc.txt') as f:
        cur = None
        for raw in f:
            line = raw.decode('cp437', 'replace').rstrip('\r\n')
            h = HEAD_RE.search(line)
            if h:
                if cur:
                    yield cur
                t = int(h.group(1))
                if t not in topics:
                    cur = None
                    continue
                mm, dd, yy = h.group(2), h.group(3), int(h.group(4))
                year = 1900 + yy if yy >= 80 else 2000 + yy
                cur = {'topic': t, 'date': f'{year}-{mm}-{dd}', 'time': '00:00', 'from': '', 'to': 'All',
                       'subject': '', 'body': [], 'state': 'head'}
                continue
            if cur is None:
                continue
            if cur['state'] == 'head':
                m = FROM_RE.match(line)
                if m:
                    hh = int(m.group(2)) % 12 + (12 if m.group(4).lower() == 'pm' else 0)
                    cur['from'] = m.group(1).strip()
                    cur['time'] = f'{hh:02d}:{m.group(3)}'
                    continue
                m = TO_RE.match(line)
                if m:
                    cur['to'] = m.group(1).strip() or 'All'
                    continue
                m = SUBJ_RE.match(line)
                if m:
                    cur['subject'] = m.group(1).strip()
                    cur['state'] = 'subj'
                    continue
                continue
            if cur['state'] == 'subj':
                cur['state'] = 'body'
                if not line.strip():
                    continue
            if line.startswith('---------------') and not line.startswith('--- '):
                yield cur
                cur = None
                continue
            if line.startswith('Next conference:') or line.startswith('Next topic:'):
                continue
            cur['body'].append(line)
        if cur:
            yield cur


def clean_body(lines):
    out = []
    for l in lines:
        if l.startswith('\x01') or l.startswith('SEEN-BY') or l.startswith('PATH:'):
            continue
        l = l.replace('\t', '    ')
        out.append(l[:79])
    while out and not out[-1].strip():
        out.pop()
    while out and not out[0].strip():
        out.pop(0)
    return out


def norm_subject(s):
    s = s.strip()
    while re.match(r'(?i)re:\s*', s):
        s = re.sub(r'(?i)^re:\s*', '', s)
    return s.upper()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--per-conference', type=int, default=150)
    ap.add_argument('--out', default=os.path.join(HERE, '..', 'sd', 'BBS', 'MSGBASE'))
    ap.add_argument('--zip', default=os.path.join(CACHE, 'fidonet-messages-execpc.zip'))
    args = ap.parse_args()

    if not os.path.exists(args.zip):
        print('Downloading', ZIP_URL, '(150 MB, once) ...')
        fetch_to(ZIP_URL, args.zip)

    wanted = {t: cid for cid, (_, ts) in CONFERENCES.items() for t in ts}
    per = {cid: [] for cid in CONFERENCES}
    everyone = set()
    print('Reading the archive ...')
    for m in read_messages(args.zip, set(wanted)):
        everyone.update(n for n in (m['from'], m['to']) if n)
        if not m['from'] or not m['body']:
            continue
        per[wanted[m['topic']]].append(m)

    anon = Anonymiser()
    # Everyone who ever posted in these echoes: their full and last names are
    # replaced wherever they turn up. First names only for the chosen messages
    # (below), so famous first names in the text are left alone.
    for name in sorted(everyone):
        anon.person(name, first=False)
    chosen = {}
    for cid, msgs in per.items():
        msgs.sort(key=lambda m: (m['date'], m['time']))
        pick = msgs[-args.per_conference:]
        chosen[cid] = pick
        for m in pick:
            m['fake_from'] = anon.person(m['from'])
            m['fake_to'] = anon.person(m['to'])
    words = make_word_replacer(anon)

    os.makedirs(args.out, exist_ok=True)
    total = 0
    for cid, pick in chosen.items():
        echo = CONFERENCES[cid][0]
        folder = os.path.join(args.out, str(cid))
        os.makedirs(folder, exist_ok=True)
        for old in os.listdir(folder):
            if old.upper().endswith('.TXT'):
                os.remove(os.path.join(folder, old))
        seen = []  # (normalised subject, fake from) per message number
        lines = [f'; FidoNet echo {echo}, from the ExecPC BBS capture archived at breakintochat.com.',
                 '; Names, handles, node addresses, BBS names, phone numbers and addresses are invented.',
                 '; Written by tools/fidonet-seed.py. The board shows these as read-only history.', '']
        for n, m in enumerate(pick, 1):
            subj = scrub_line(m['subject'], anon, words)[:60]
            reply = 0
            if re.match(r'(?i)re:', m['subject']):
                ns = norm_subject(m['subject'])
                for k in range(len(seen) - 1, -1, -1):
                    if seen[k][0] == ns and (seen[k][1] == m['fake_to'] or reply == 0):
                        reply = k + 1
                        if seen[k][1] == m['fake_to']:
                            break
            seen.append((norm_subject(m['subject']), m['fake_from']))
            # Quote initials: the real people in this message -> their invented initials.
            swaps = {}
            for real, fake in ((m['from'], m['fake_from']), (m['to'], m['fake_to'])):
                ri, fi = initials(real), initials(fake)
                if len(ri) >= 2 and ri != fi:
                    swaps[ri] = fi
            body = []
            for l in clean_body(m['body']):
                if swaps:
                    l = re.sub(r'^(\s{0,3})([A-Z]{2,3})>', lambda q: q.group(1) + swaps.get(q.group(2), q.group(2)) + '>', l)
                l = scrub_line(l, anon, words)
                if l.startswith('#MSG'):
                    l = ' ' + l
                body.append(l)
            lines += ['#MSG', 'From: ' + m['fake_from'], 'To: ' + m['fake_to'], 'Subject: ' + (subj or '(no subject)'),
                      'Date: ' + m['date'] + ' ' + m['time']]
            if reply:
                lines.append(f'Reply: {reply}')
            lines.append('')
            lines += body
            lines.append('')
        path = os.path.join(folder, echo + '.TXT')
        with open(path, 'w', encoding='cp437', errors='replace', newline='\r\n') as w:
            w.write('\n'.join(lines))
        total += len(pick)
        print(f'  conference {cid}: {len(pick):4d} messages -> {os.path.relpath(path)}')
    print(f'{total} messages, {len(anon.people)} people renamed.')


if __name__ == '__main__':
    sys.exit(main())
