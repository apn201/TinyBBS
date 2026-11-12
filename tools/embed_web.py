"""Turn the shared web pages into firmware headers.

The browser pages live once, as plain files, in shared/web/:
  terminal.html  the BBS terminal (both builds serve it at /)
  sysop.html     the SysOp file manager (/sysop)
The ESP32 has no file system for its own code, so the firmware carries them
as C strings in src/net/web_terminal.h and src/net/sysop_page.h. Those
headers are generated from the shared files and should not be edited.
The server build serves shared/web/ directly.

PlatformIO runs this before every build (extra_scripts in platformio.ini).
Run it by hand with `python tools/embed_web.py` if you build another way.
A header is only rewritten when it would change, so builds stay incremental.
"""
import os
import sys

try:
    ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
except NameError:  # PlatformIO runs extra scripts without __file__, from the project folder
    ROOT = os.getcwd()
PAGES = [
    # shared file, header, C name, raw-string delimiter, one-line description
    ('shared/web/terminal.html', 'src/net/web_terminal.h', 'kWebTerminal', 'TERM', 'The browser terminal.'),
    ('shared/web/sysop.html', 'src/net/sysop_page.h', 'kSysopPage', 'PAGE',
     'The SysOp file manager page. Deliberately modern and plain: this is the admin backstage, not part of the period BBS.'),
]


def header_for(src, name, delim, about):
    with open(os.path.join(ROOT, src), encoding='utf-8') as f:
        html = f.read().replace('\r\n', '\n')
    if ')' + delim + '"' in html:
        sys.exit(f'{src} contains ){delim}" and cannot be embedded')
    return ('#pragma once\n'
            f'// {about}\n'
            f'// GENERATED from {src} by tools/embed_web.py. Edit the shared file, not this one.\n\n'
            f'static const char {name}[] PROGMEM = R"{delim}(' + html.rstrip('\n') + f'){delim}";\n')


def main():
    for src, dst, name, delim, about in PAGES:
        text = header_for(src, name, delim, about)
        path = os.path.join(ROOT, dst)
        old = open(path, encoding='utf-8').read().replace('\r\n', '\n') if os.path.exists(path) else None
        if old != text:
            with open(path, 'w', encoding='utf-8', newline='\n') as f:
                f.write(text)
            print(f'embed_web: wrote {dst}')


main()
