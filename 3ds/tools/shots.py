#!/usr/bin/env python3
"""Join the AUTOTEST build's screenshots (NAME-top.bmp, NAME-bottom.bmp) into
one PNG per shot, laid out like the console: top screen above the bottom one.

    python3 tools/shots.py <sdmc>/3ds/PictoCard/shots out/
"""
import glob
import os
import sys

from PIL import Image


def main():
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    for top in sorted(glob.glob(os.path.join(src, '*-top.bmp'))):
        name = os.path.basename(top)[:-len('-top.bmp')]
        bottom = top[:-len('-top.bmp')] + '-bottom.bmp'
        sheet = Image.new('RGB', (400, 480), (0, 0, 0))
        sheet.paste(Image.open(top), (0, 0))
        if os.path.exists(bottom):
            sheet.paste(Image.open(bottom), (40, 240))
        sheet.save(os.path.join(out, name + '.png'))
        print(name)


if __name__ == '__main__':
    main()
