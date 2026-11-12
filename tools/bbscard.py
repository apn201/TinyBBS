"""Shared bits for the tools that put content on the TinyBBS card tree.

Imported by fetch-modules.py, fetch-sids.py and fidonet-seed.py.
"""
import os
import time
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
FILES = os.path.join(HERE, '..', 'sd', 'BBS', 'FILES')


def fetch(url, polite=1.0):
    """Download a URL and return its bytes, then wait a moment for the next one."""
    req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
    with urllib.request.urlopen(req, timeout=120) as r:
        data = r.read()
    time.sleep(polite)
    return data


def fetch_to(url, path):
    """Download a big file to `path` once, via path.part so a broken run leaves nothing half-done."""
    if os.path.exists(path):
        return
    os.makedirs(os.path.dirname(path), exist_ok=True)
    req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
    with urllib.request.urlopen(req) as r, open(path + '.part', 'wb') as w:
        while True:
            block = r.read(1 << 20)
            if not block:
                break
            w.write(block)
    os.replace(path + '.part', path)


def write_files_bbs(folder, lines):
    """FILES.BBS: one "NAME  description" line per file, DOS text."""
    with open(os.path.join(folder, 'FILES.BBS'), 'w', encoding='cp437', errors='replace', newline='\r\n') as w:
        w.write('\n'.join(lines) + '\n')


def write_area_ini(folder, name, min_security=0, flag='', downloads=1, uploads=0):
    """AREA.INI for a new file area. An existing one is the SysOp's and is left alone."""
    path = os.path.join(folder, 'AREA.INI')
    if os.path.exists(path):
        return
    with open(path, 'w', newline='\r\n') as w:
        w.write('; File area settings. Edit, then RELOAD as SysOp.\n[AREA]\n'
                f'; Shown in the file menu. Empty = made from the folder name.\nname={name}\n'
                f'; Lowest security level that sees this area.\nmin_security={min_security}\n'
                f'; A user flag the caller must also have, e.g. BACKROOM. Empty = none.\nrequired_flag={flag}\n'
                f'; 0 = files can be viewed but not downloaded.\ndownloads={downloads}\n'
                f'; 1 = browser callers may upload here, for the SysOp to approve.\nuploads={uploads}\n')
