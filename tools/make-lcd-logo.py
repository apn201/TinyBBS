#!/usr/bin/env python3
"""Make the ESP32 status screen's logo, /BBS/ASSETS/LOGO.RGB565.

The file is raw RGB565, little-endian, 160 pixels wide, up to 53 rows (the
status lines go below). SYSTEM.INI lcd_logo= names it; without the file the
board draws the logo built into the firmware.

  python tools/make-lcd-logo.py                 the built-in Kouvosto logo
  python tools/make-lcd-logo.py --png my.png    your own picture (needs Pillow)

The server build has no LCD and ignores it.
"""
import argparse
import os
import re
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'sd', 'BBS', 'ASSETS', 'LOGO.RGB565')
WIDTH, MAX_ROWS = 160, 53


def builtin():
    """Render src/hw/scene_logo.h the way drawLcdLogo() draws it."""
    src = open(os.path.join(HERE, '..', 'src', 'hw', 'scene_logo.h'), encoding='utf-8').read()
    palette = [int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{4})', src.split('kSceneLogo')[0])]
    rows = re.findall(r'"([0-9a-f]+)"', src.split('kSceneLogo')[1])
    height = 50 + max(0, len(rows) - 25)
    img = [[0] * WIDTH for _ in range(height)]
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            color = int(ch, 16)
            if not color:
                continue
            px = 2 + x * 2
            if y < 25:  # top part at double size
                for dy in (0, 1):
                    for dx in (0, 1):
                        if px + dx < WIDTH:
                            img[y * 2 + dy][px + dx] = palette[color]
            else:  # the rest one line each, double width
                for dx in (0, 1):
                    if px + dx < WIDTH:
                        img[50 + y - 25][px + dx] = palette[color]
    return img


def from_png(path):
    from PIL import Image
    im = Image.open(path).convert('RGB')
    h = min(MAX_ROWS, round(im.height * WIDTH / im.width))
    im = im.resize((WIDTH, h))
    return [[((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3) for r, g, b in (im.getpixel((x, y)) for x in range(WIDTH))]
            for y in range(h)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--png')
    ap.add_argument('--out', default=OUT)
    args = ap.parse_args()
    img = from_png(args.png) if args.png else builtin()
    with open(args.out, 'wb') as w:
        for row in img[:MAX_ROWS]:
            w.write(struct.pack('<%dH' % WIDTH, *row))
    print(f'{os.path.relpath(args.out)}: {WIDTH}x{min(len(img), MAX_ROWS)}')


if __name__ == '__main__':
    main()
