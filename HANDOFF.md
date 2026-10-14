# Handoff

Working notes for whoever picks this up next. Keep this file current: update the
**Status** and **Next steps** sections whenever work lands, and move finished items down to **Done**.

Last updated: 2026-10-14

Repos (both owned by `larsenv`, commit and push as `larsenv`):

- `larsenv/PictoCard-Wii` — this repo, the Homebrew Channel app (C, devkitPPC). Local: `/Volumes/SSD/larsen/Documents/Git/PictoCard-Wii`.
- `larsenv/PictoCard` — the website and JSON API (Node). Local: `/Volumes/SSD/larsen/Documents/Git/PictoCard`. Push with `git push git@github.com:larsenv/PictoCard.git main` (the repo was renamed; `gh` is logged in as `quatric`, which cannot administer `larsenv/*`).

## Status

### Done and verified

| Area | Where | How it was verified |
| --- | --- | --- |
| USB mouse (pointer, left = A, right = B, middle = +, wheel = Up/Down) | `source/input.c` (libogc `usbmouse.h`) | Compiles; not tried on hardware |
| Wii Number checksum (from RiiConnect24/wiino) | `source/wiino.c`, `lib/wiino.js` (site), used in `app.c` `validate()` and all site/API send paths | 20 000 numbers from wiino's generator accepted in C, 300 in JS; typos rejected; `tests/wiino.cjs` |
| Doodle card style (the drawing is the whole card) | Web: `public/js/doodle.js`, `views/index.ejs`, `lib/cardGenerator.js` (`doodle`), `routes/index.js`, `routes/api.js` (`cardType: "doodle"`). Wii: `source/doodle.c`, `app.c` (`F_STYLE`) | Playwright (`tests/doodle.spec.js`), API test, Dolphin screenshots. Input: Wii Remote pointer, USB mouse, D-Pad, USB keyboard |
| Wii pointer cursor | `data/pointer.png` (Player 1 hand from PrimmR's Wii Pointer Cursors), `source/gfx.c` | Dolphin screenshot. Credit PrimmR (https://primmr.dev/projects/wii-pointer-cursors/) when releasing |
| "Light mode" header text vertically centred | `source/ui.c` | Dolphin screenshot |
| Mii faces in the Mii picker (Arian Kordi's renderer) | Server: `POST /api/v1/mii-faces` (`routes/api.js`). Wii: `JOB_FACES` in `api.c`, `request_faces()` in `app.c`, icons in `picker.c`. Web: `public/js/wii-mii-face.js` (Wii tab Mii file) | Dolphin screenshot with real Miis from RFL_DB.dat; Playwright |
| Picture-only Wii mail (Wii app/API only, never the website) | `lib/wiiMail.js` (`pictureOnly`), `routes/api.js` `sendWii`, `app.c` `validate()` | `tests/api.cjs` |
| Site tweaks | "New" badge removed; Wii page: thicker image borders, "The Wii app, not the website" captions, title case | Visual |
| Message Board pictures: list, preview, VFF reader | `source/mb.c`, `source/odh.cpp` (AJPG decoder from cdbackup), `source/aes.c`, `source/libpatcher.c` | Dolphin with a crafted `cdb.vff` (list, dates, newest first, JPEG preview). The AJPG/ODH path is **not** tested yet |

### In progress

- Message Board pick is verified in Dolphin (the picture shows in the field and the live preview).
- Card picture source: with Wii attachment "PictoCard" the picture field asks "Where is the picture?" (SD/USB or Message Board).
  Compiles; not yet tried in Dolphin. The website/email path still uses the file browser only.
- Message Board entries are listed by date and time only, because Message Board photos have no names.

### Next steps (asked for, not started)

1. ~~Use a JPEG/PNG from SD or the Message Board as the PictoCard picture~~ (implemented, see In progress; verify in Dolphin).
   Add a picture-source choice for the card picture (SD/USB file browser vs Message Board) next to `F_PICTURE`. Keep optimising the
   JPEGs: `store_picture()` in `app.c` re-encodes (quality 88, max 1600 px); the server (`wiiMail.toBaselineJpeg`) makes the final
   small baseline JPEG for the Wii. Consider capping the upload at about 1280 px.
2. **Read the Wii's friends from its address book and send to them.** The friend list is `/shared2/wc24/nwc24fl.bin`
   (magic `WcFl`, version 2, max 100 entries; friend codes are 64-bit big-endian values near the start). The copy in the Dolphin NAND
   has no friends, so the entry layout (nicknames, Mii data) could not be checked. The `wii_development_package`
   (`/Volumes/SSD/larsen/Downloads/wii_development_package`) has only a stub `NWC24Mini.h`; there is no documentation of the file in it.
   Needs a real `nwc24fl.bin` from a console to confirm offsets. Friend codes read this way must still pass `wiino_valid()`.
3. Verify the ODH (AJPG) decoder end to end: compile `source/odh.cpp` on the host with a small shim, use its own compressor to make
   an AJPG, decode with `odh_decode()`, compare. Also verify SD card messages (`sd:/private/wii/title/HAEA`, AES-CBC, zero key).
4. (Done) README credits.
5. Website: the Wii app page screenshots (`public/images/wii-app/*.png`) predate the doodle, Mii picker and Message Board
   features; retake them with the Dolphin harness.
6. Real hardware testing for: USB mouse, Message Board access (needs the AHBPROT/ISFS patches from libpatcher; Dolphin skips them).

## How to build and test

```bash
# Wii app
make clean && make                 # PictoCard.dol
make clean && make AUTOTEST=1      # test build that replays sd:/autotest.txt and saves sd:/shots/*.bmp

# Site
npm run test:unit                  # 2 Wii tests in tests/backend.cjs fail on main; they failed before these changes too
# Playwright specs need a browser; Chrome works: use a throwaway config with use: { channel: 'chrome' }.
# Run specs one at a time: many /create requests in parallel trip the rate limiter.
```

### Dolphin test harness (kept off the user's real Dolphin profile)

Everything lives in `<scratch tmp dir>/dolphin-test` (its own `-u` user dir with SD folder sync on):

- `Load/WiiSDSync/apps/PictoCard/boot.dol` — copy the AUTOTEST build here; `settings.txt` there points the app at `server=http://127.0.0.1:3011`.
- `Load/WiiSDSync/autotest.txt` — the script. Commands: `wait N`, `press A|B|UP|DOWN|LEFT|RIGHT|PLUS|MINUS|1|2|HOME`, `hold X`, `release X`,
  `point X Y`, `nopoint`, `key HEXCODE`, `type TEXT`, `shot NAME`, `quit`. Always `wait` a few frames after `press`.
- `Wii/shared2/menu/FaceLib/RFL_DB.dat` — real Miis (copied from the user's profile). `Wii/title/00000001/00000002/data/cdb.vff` — a small crafted Message Board image with two JPEG messages.
- `run.sh` — runs Dolphin (90 s cap), converts shots to PNG in `Load/WiiSDSync/shots/`.
- Server: `cd /Volumes/SSD/larsen/Documents/Git/PictoCard && PORT=3011 NODE_ENV=test node server.js` (restart it after changing API code).

## Things worth knowing

- Screenshots from the harness include the pointer (`gfx_present` hook), but `hold A` only applies to `point` lines because of how the harness returns early.
- `picker.c` now supports a preview image and a close callback (`picker_open_preview`); Mii faces and Message Board entries use it.
- The VFF is read lazily through ISFS (the real image is 20 MB). Layout: 0x20 header, FAT12/16 right after it, 128-entry root directory, 512-byte clusters; names are 8.3 (`XXXXXXXX.000`, hex seconds since 2000). Message = 0x400 attribute header + `CDBFILE` header; attachment offset/size at +0x12C/+0x130 of the `CDBFILE` header.
- Website-side rule from the user: never allow sending only an image from the website; only the Wii app (API) may.
- Keep all work, caches and data on the external SSD (`/Volumes/SSD`), not the internal drive.
