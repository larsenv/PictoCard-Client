#!/usr/bin/env python3
"""Builds the letterhead thumbnails built into the Wii and Wii U apps:

  wii/data/letterheads.jpg, wiiu/data/letterheads.jpg   a grid of 120x88 thumbnails
  wii/source/letterhead_ids.h, wiiu/source/letterhead_ids.h   ids and groups in grid order

Each thumbnail is the letterhead's nine letter tiles with the envelope icon from its
thumbnail always in the bottom-left corner. The apps download a letterhead's .arc only
when a letter using it is sent.

    wii/tools/make-letterheads.py <dir with <id>.arc files> <manifest.json>

The manifest is a list of {id, name, group} (or {"letterheads": [...]}). Needs wszst
(Wiimms SZS Tools), ndspy_lz10 and Pillow.
"""
import json, os, re, shutil, subprocess, sys, tempfile
from PIL import Image

W, H, COLS = 120, 88, 8
LW, LH = 512, 376
TILES = {'a': (0, 0), 'b': (64, 0), 'c': (448, 0), 'd': (0, 144), 'e': (64, 144), 'f': (448, 144),
         'g': (0, 312), 'h': (64, 312), 'i': (448, 312)}
SIZES = {'a': (64, 144), 'b': (384, 144), 'c': (64, 144), 'd': (64, 168), 'e': (384, 168), 'f': (64, 168),
         'g': (64, 64), 'h': (384, 64), 'i': (64, 64)}
# the Animal Crossing papers sort after everything else, apart from the first two letterheads
MORE_PREFIX, MORE_GROUP = 'animal-crossing-city-folk-paper-', 'City Folk Stationery'
ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), '..', '..'))


def natural(s):
    return [int(t) if t.isdigit() else t.lower() for t in re.split(r'(\d+)', s)]


def unpack(arc, tmp):
    shutil.copy(arc, os.path.join(tmp, 'a.arc'))
    subprocess.run(['wszst', 'xx', 'a.arc'], cwd=tmp, check=True, capture_output=True)
    letters = os.path.join(tmp, 'a.d', 'letter_LZ.bin.d', 'img')
    thumb = os.path.join(tmp, 'a.d', 'thumbnail_LZ.bin.d', 'img', 'my_LetterS_b.tpl.png')
    if not os.path.isdir(os.path.join(tmp, 'a.d', 'letter_LZ.bin.d')) and not os.path.exists(os.path.join(tmp, 'a.d', 'letter_LZ.bin')):
        letters = None  # only an envelope: the letter is the plain default one
    elif not os.path.isdir(letters):
        for name in ('letter', 'thumbnail'):
            u8 = os.path.join(tmp, name + '.u8')
            subprocess.run(['ndspy_lz10', 'decompress', os.path.join(tmp, 'a.d', name + '_LZ.bin'), u8], check=True)
            subprocess.run(['wszst', 'extract', u8, '--decode', '--dest', os.path.join(tmp, name)],
                           check=True, capture_output=True)
        letters = os.path.join(tmp, 'letter', 'img')
        thumb = os.path.join(tmp, 'thumbnail', 'img', 'my_LetterS_b.tpl.png')
    return letters, thumb


def thumbnail(arc):
    tmp = tempfile.mkdtemp(prefix='lh-')
    try:
        letters, thumb = unpack(arc, tmp)
        page = Image.new('RGBA', (LW, LH), (255, 255, 255, 255))
        if letters is None:
            page.alpha_composite(Image.open(os.path.join(os.path.dirname(__file__), 'default-letter.png')).convert('RGBA'))
        else:
            for k, (x, y) in TILES.items():
                t = Image.open(os.path.join(letters, f'my_Letter_{k}.tpl.png')).convert('RGBA').resize(SIZES[k], Image.BILINEAR)
                page.alpha_composite(t, (x, y))
        if os.path.exists(thumb):
            e = Image.open(thumb).convert('RGBA')
            page.alpha_composite(e, (8, LH - e.height - 8))
        else:
            print('no envelope in', arc, file=sys.stderr)
        return page.convert('RGB').resize((W, H), Image.LANCZOS)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    arcs, manifest = sys.argv[1:]
    items = json.load(open(manifest))
    items = items['letterheads'] if isinstance(items, dict) else items
    for l in items:
        if l['id'].startswith(MORE_PREFIX):
            l['group'] = MORE_GROUP
    items.sort(key=lambda l: (l['group'] == MORE_GROUP, natural(l['group']), natural(l['name'])))
    rows = (len(items) + COLS - 1) // COLS
    sheet = Image.new('RGB', (COLS * W, rows * H), (255, 255, 255))
    for i, l in enumerate(items):
        sheet.paste(thumbnail(os.path.join(arcs, l['id'] + '.arc')), ((i % COLS) * W, (i // COLS) * H))
    header = ['/* Letterheads in data/letterheads.jpg, a %d-column grid of %dx%d thumbnails, with the group each' % (COLS, W, H),
              ' * is shown under. Made with tools/make-letterheads.py. */',
              '#pragma once',
              '#define LH_SHEET_W %d' % W, '#define LH_SHEET_H %d' % H, '#define LH_SHEET_COLS %d' % COLS,
              'static const struct { const char *id, *group; } LH_SHEET[] = {']
    header += ['    { "%s", "%s" },' % (l['id'], l['group'].replace('"', '\\"')) for l in items]
    header += ['};', '#define LH_SHEET_COUNT (int)(sizeof LH_SHEET / sizeof LH_SHEET[0])', '']
    for app in ('wii', 'wiiu'):
        sheet.save(os.path.join(ROOT, app, 'data', 'letterheads.jpg'), quality=82, optimize=False, progressive=False, subsampling=2)
        open(os.path.join(ROOT, app, 'source', 'letterhead_ids.h'), 'w').write('\n'.join(header))
    print('%d letterheads, %dx%d' % (len(items), sheet.width, sheet.height))


main()
