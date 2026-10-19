'use strict';

// Builds apps/PictoCard/letterheads/ from the PictoCard site repo: every letterhead's
// .arc, a manifest.json (id, name, group in gallery order, Animal Crossing last) and
// sheet.jpg (a grid of thumbnails in the same order). The app reads these from the SD
// card instead of downloading them.
//
//   node tools/make-letterheads.js /path/to/PictoCard

const fs = require('fs');
const path = require('path');

const site = path.resolve(process.argv[2] || '');
if (!fs.existsSync(path.join(site, 'data', 'letterheads', 'manifest.json'))) {
  console.error('usage: node tools/make-letterheads.js /path/to/PictoCard');
  process.exit(1);
}
const sharp = require(path.join(site, 'node_modules', 'sharp'));
const wiiMail = require(path.join(site, 'lib', 'wiiMail.js'));

const W = 120, H = 88, COLS = 8;
const out = path.join(__dirname, '..', 'apps', 'PictoCard', 'letterheads');

(async () => {
  fs.rmSync(out, { recursive: true, force: true });
  fs.mkdirSync(out, { recursive: true });
  const list = wiiMail.letterheadGroups().flatMap((g) => g.items.map((l) => ({ id: l.id, name: l.name, group: g.group })));
  const tiles = [];
  for (const [i, l] of list.entries()) {
    fs.copyFileSync(path.join(site, 'data', 'letterheads', `${l.id}.arc`), path.join(out, `${l.id}.arc`));
    const input = await sharp(path.join(site, 'public', 'images', 'letterheads', `${l.id}.png`))
      .resize(W, H, { fit: 'cover', position: 'top' }).png().toBuffer();
    tiles.push({ input, left: (i % COLS) * W, top: Math.floor(i / COLS) * H });
  }
  const rows = Math.ceil(list.length / COLS);
  await sharp({ create: { width: COLS * W, height: rows * H, channels: 3, background: '#ffffff' } })
    .composite(tiles)
    .jpeg({ quality: 82, progressive: false, chromaSubsampling: '4:2:0' })
    .toFile(path.join(out, 'sheet.jpg'));
  fs.writeFileSync(path.join(out, 'manifest.json'), JSON.stringify({ w: W, h: H, cols: COLS, letterheads: list }));
  console.log(`${list.length} letterheads -> ${out}`);
})();
