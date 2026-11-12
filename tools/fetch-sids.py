#!/usr/bin/env python3
"""Fetch a starter set of Commodore 64 SID tunes from the HVSC.

The High Voltage SID Collection adds tunes "released into the public domain"
and keeps them under "the same principles of free and unrestricted
distribution as public domain software" (https://hvsc.c64.org/faq). Each tune
is copied byte for byte from https://hvsc.c64.org/download/C64Music/<path>.

Only PSID files are kept: the browser player (jsSID) does not run RSID tunes.

Usage:  python tools/fetch-sids.py [--out sd/BBS/FILES/016_SID_MUSIC] [--count 30]
Writes the tunes, a FILES.BBS (title, composer, release, HVSC path) and AREA.INI.
"""
import argparse
import os

from bbscard import FILES, fetch, write_area_ini, write_files_bbs

BASE = 'https://hvsc.c64.org/download/C64Music/'
CANDIDATES = """
MUSICIANS/H/Hubbard_Rob/Commando.sid
MUSICIANS/H/Hubbard_Rob/Monty_on_the_Run.sid
MUSICIANS/H/Hubbard_Rob/Sanxion.sid
MUSICIANS/H/Hubbard_Rob/Delta.sid
MUSICIANS/H/Hubbard_Rob/International_Karate.sid
MUSICIANS/H/Hubbard_Rob/Lightforce.sid
MUSICIANS/H/Hubbard_Rob/Thing_on_a_Spring.sid
MUSICIANS/H/Hubbard_Rob/Warhawk.sid
MUSICIANS/H/Hubbard_Rob/Spellbound.sid
MUSICIANS/H/Hubbard_Rob/Crazy_Comets.sid
MUSICIANS/G/Galway_Martin/Wizball.sid
MUSICIANS/G/Galway_Martin/Parallax.sid
MUSICIANS/G/Galway_Martin/Comic_Bakery.sid
MUSICIANS/G/Galway_Martin/Arkanoid.sid
MUSICIANS/G/Galway_Martin/Times_of_Lore.sid
MUSICIANS/D/Daglish_Ben/Last_Ninja.sid
MUSICIANS/D/Daglish_Ben/Trap.sid
MUSICIANS/D/Daglish_Ben/Deflektor.sid
MUSICIANS/D/Daglish_Ben/Gauntlet.sid
MUSICIANS/T/Tel_Jeroen/Cybernoid_II.sid
MUSICIANS/T/Tel_Jeroen/Myth.sid
MUSICIANS/T/Tel_Jeroen/Supremacy.sid
MUSICIANS/T/Tel_Jeroen/Hawkeye.sid
MUSICIANS/G/Gray_Matt/Last_Ninja_2.sid
MUSICIANS/G/Gray_Matt/Driller.sid
MUSICIANS/G/Gray_Matt/Tusker.sid
MUSICIANS/H/Huelsbeck_Chris/Great_Giana_Sisters.sid
MUSICIANS/W/Whittaker_David/Glider_Rider.sid
MUSICIANS/W/Whittaker_David/Lazy_Jones.sid
MUSICIANS/F/Follin_Tim/Ghouls_n_Ghosts.sid
MUSICIANS/D/Dunn_Jonathan/Robocop.sid
MUSICIANS/G/Gray_Fred/Mutants.sid
MUSICIANS/H/Hubbard_Rob/Zoids.sid
MUSICIANS/G/Galway_Martin/Green_Beret.sid
MUSICIANS/D/Daglish_Ben/Krakout.sid
"""


def text(b):
    return b.split(b'\0', 1)[0].decode('latin-1').strip()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(FILES, '016_SID_MUSIC'))
    ap.add_argument('--count', type=int, default=30)
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    desc, kept = [], 0
    for rel in CANDIDATES.split():
        if kept >= args.count:
            break
        name = rel.rsplit('/', 1)[1]
        path = os.path.join(args.out, name)
        if os.path.exists(path):
            data = open(path, 'rb').read()
        else:
            try:
                data = fetch(BASE + rel)
            except Exception as e:
                print(f'{rel}: skipped ({e})')
                continue
            if data[:4] != b'PSID':
                print(f'{rel}: skipped ({data[:4]!r}, not PSID)')
                continue
            with open(path, 'wb') as w:
                w.write(data)
        title, author, released = text(data[0x16:0x36]), text(data[0x36:0x56]), text(data[0x56:0x76])
        songs = int.from_bytes(data[0x0e:0x10], 'big')
        extra = f', {songs} songs' if songs > 1 else ''
        desc.append(f'{name}  {title} by {author} ({released}{extra}) HVSC {rel}')
        kept += 1
        print(f'{name:32s} {len(data):6d}  {title} / {author}')
    write_files_bbs(args.out, desc)
    write_area_ini(args.out, 'C64 SID Music')
    print(kept, 'tunes in', os.path.relpath(args.out))


if __name__ == '__main__':
    main()
