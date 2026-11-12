#!/usr/bin/env python3
"""Fetch a starter set of tracker modules from The Mod Archive.

The Mod Archive upload terms (v2.2, section 1.2) let anyone redistribute a
module "provided it has stayed in its original, unmodified, unbundled form":
https://modarchive.org/index.php?terms-upload . So each module goes on the
card as its own file, byte for byte, never zipped.

The picks below came from the "most recently favourited" list on
https://modarchive.org/index.php?request=view_members_with_favourites
(November 2026). Each line: module id | file name | title.

Usage:  python tools/fetch-modules.py [--out sd/BBS/FILES/015_MUSIC]
Writes the modules, a FILES.BBS with titles and Mod Archive ids, and AREA.INI.
"""
import argparse
import os
import time
import urllib.request

PICKS = """
130596|skipmem.mod|mod.skipmem
113197|very_late.mod|very late
78577|gnome_alone.mod|gnome alone
95037|pio_pio.mod|MOD.PIO PIO
162314|trillian_-_cosmic_dance.mod|cosmic dance
63520|zak.mod|FUNKY-ZAK
201549|temperament.mod|temperament
215685|viraxor_-_backdoor1.mod|backdoor #1
215686|viraxor_-_backdoor2.mod|backdoor #2
215687|thumbknack.mod|thumbknack
215661|gathering-334.mod|hammer->gathering
215382|walkman_-_5.mod|5
81730|glory_of_life.mod|glory of life(baby)
198977|picture_box.mod|picture box
211266|usual_-_wheres_my_chaos.mod|where's my chaos?
82130|holy_tunes.mod|holy tunes
161155|strobo_-_jamaica.mod|jamaica
67776|heatbeat-tuomiomies.mod|tuomiomies
99411|seikkailumies.mod|seikkailumies
86757|space-light_theme.mod|space-light theme
88194|rolgrim_2.mod|rolgrim 2
86512|reduction.mod|reduction
85966|portasound.mod|portasound
81610|nomore.mod|nomore
87781|nedla.mod|nedla
215647|october_rust.mod|october rust
110534|truehalfsome2.mod|truehalfsome2
94083|tested_and_proven.mod|tested_and_proven
89423|step_it_up.mod|step_it_up
92252|sound_of_vantaa.mod|sound_of_vantaa!?
79006|whats_the_problem.mod|whats the problem?
89299|gathering.mod|gathering
89072|savoy.mod|savoy
95447|mignon.mod|Mignon
70948|bolz-ex_1.s3m|MConv 1.00 (C)'96
215678|sm-dirty.s3m|Dirty Laundry
215652|techbites.s3m|Techie Bites
215650|elektric_diffusion.s3m|Elektric Diffusion
215651|wiretapp.s3m|Wire Tapped
42548|GONE.XM|Gone ...
36663|chip1499.xm|nuotiopiiri
38752|brg_day.xm|To save the day /brg
61256|4g_crim.xm|4g : Blue
140530|demo_scene_calling.xm|dEMO sCENE cALLING
136751|cosmic_hysteria.xm|Cosmic Hysteria
136694|xstra_va_ganza.xm|Xstra Va Ganza
154104|kaviarkalas.xm|kaviarkalas
215675|no-cjoe2.it|Crazy Ol Joe Strikes Back
215683|dx-inner.it|The Inner Light
155174|8000_miles_away.it|8000 years ago
"""
URL = 'https://api.modarchive.org/downloads.php?moduleid={}'
HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(HERE, '..', 'sd', 'BBS', 'FILES', '015_MUSIC'))
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    picks = [l.split('|') for l in PICKS.strip().splitlines()]
    desc = []
    for mid, name, title in picks:
        path = os.path.join(args.out, name)
        if not os.path.exists(path):
            req = urllib.request.Request(URL.format(mid), headers={'User-Agent': 'Mozilla/5.0'})
            with urllib.request.urlopen(req, timeout=60) as r:
                data = r.read()
            with open(path, 'wb') as w:
                w.write(data)
            print(f'{name:40s} {len(data):8d}')
            time.sleep(1)  # be polite
        desc.append(f'{name}  {title} (The Mod Archive #{mid})')
    with open(os.path.join(args.out, 'FILES.BBS'), 'w', encoding='cp437', errors='replace', newline='\r\n') as w:
        w.write('\n'.join(desc) + '\n')
    ini = os.path.join(args.out, 'AREA.INI')
    if not os.path.exists(ini):
        with open(ini, 'w', newline='\r\n') as w:
            w.write('; File area settings. Edit, then RELOAD as SysOp.\n[AREA]\n'
                    '; Shown in the file menu. Empty = made from the folder name.\nname=Tracker Music\n'
                    '; Lowest security level that sees this area.\nmin_security=0\n'
                    '; A user flag the caller must also have, e.g. BACKROOM. Empty = none.\nrequired_flag=\n'
                    '; 0 = files can be viewed but not downloaded.\ndownloads=1\n'
                    '; 1 = browser callers may upload here, for the SysOp to approve.\nuploads=0\n')
    print(len(picks), 'modules in', os.path.relpath(args.out))


if __name__ == '__main__':
    main()
