# Handoff

Working notes for whoever picks this up next. Keep this file current: update the
**Status** and **Next steps** sections whenever work lands.

Last updated: 2026-10-15 (the port to Wii U)

Repos:

- `larsenv/PictoCard-WiiU` — this repo, the Aroma app (C, wut + SDL2). Commit and push as `larsenv`.
- PictoCard for Wii — the Homebrew Channel app it was ported from. Most of `source/` is shared with it (the files that
  differ are listed below), so fixes there can be carried across by hand.
- PictoCard — the website and JSON API (`/api/v1`).

## What changed from the Wii version

| Area | Wii | Wii U |
| --- | --- | --- |
| Video | GX framebuffer, 640/852 wide | SDL2 textures, 852x480 canvas scaled to the TV (1280x720) and GamePad (`gfx.c`) |
| Input | WPAD, PAD, libwiidrc/libwupc/sicksaxis, USB mouse | VPAD (buttons, stick, touch) and KPAD read directly, USB keyboard through SDL (`input.c`). No USB mouse |
| Touch | — | A tap (let go without dragging) is A; a drag scrolls (`scroll.c`); in the doodle pad a touch holds A (`input_touch_draws`) |
| Sound | ASND | a small SDL mixer (`sfx.c`) |
| Network | `if_config`, `net_*` | nn::ac, BSD sockets, `getaddrinfo` (`net.c`) |
| Threads | LWP | pthreads (`platform.h` `thread_start`) |
| NAND | ISFS with libpatcher's permission patches | `nand.c`: the same ISFS calls over stdio on Wii mode's NAND, mounted with libmocha (`slccmpt01:`) |
| Outbox | writes the send box, then KD suspend/resume/send-now ioctls | writes the send box only; KD runs only in Wii mode, so the letter goes out at the Wii Menu's next mail check. "Send it!" (PictoCard) is the default on the review screen, the outbox is the second button |
| Miis | Mii Channel (`RFL_DB.dat`) | Mii Channel from Wii mode, plus Wii U Miis from Mii Maker's `FFL_ODB.dat` (`wiidata.c` `load_wiiu_miis`) |
| Files | `sd:/`, `usb:/` | the SD card only, `fs:/vol/external01/` (shown as `sd:/`) |
| Camera | — | `camera.c`: the GamePad camera (wut `camera.h`, NV12 640x480 into three rotating surfaces), offered with the SD card wherever a photo is picked |
| Settings | next to boot.dol | `sd:/wiiu/apps/PictoCard/settings.txt` |

### Wii U Miis

`FFL_ODB.dat` lives in Mii Maker's save, `/vol/storage_mlc01/usr/save/00050010/1004a{0,1,2}00/user/common/db/` (JPN/USA/EUR),
read through a Mocha mount of `/vol/storage_mlc01` (`mlc_read` in `nand.c`). Layout, checked on a real console's file
(61 Miis): `FFOC`, 4 bytes, then 3000 slots of 0x5C-byte
`FFLiMiiDataOfficial`, big-endian (names are UTF-16BE). The server (`lib/miiToStudio.mjs`) reads the 3DS byte order. Swapping the
u32 at 0 and every u16 from 0x16 on makes all 61 valid under `MiiToStudio.isValid` (a brute force over u16/u32/none for each
block found only that layout), and the server renders them correctly. Not certain: whether 0x2E/0x2F (height, build) are one u16
or two u8s. They are swapped as a u16, so if they are really two u8s, a Wii U Mii's height and build come out swapped. That only
shows on full-body renders.

The server accepts 92-byte data for the card Mii and `/mii-faces`. A Wii letter's `X-WiiFace` (`wiiMii`, outbox) only takes a
74-byte Wii Mii, so a Wii U Mii is left off letters and the form says so.

## Status

| Area | How it was verified |
| --- | --- |
| Builds with no warnings (`make`, `make AUTOTEST=1`) | devkitPPC + wut + wiiu-sdl2 on macOS |
| Connect, live preview, form, D-Pad, touch tap, touch drag scrolling, list picker, number keyboard, SD file browser with thumbnail, photo as card picture, doodle drawn by finger used as the card | Cemu 2.6 autotest screenshots against a local server (`PORT=3011 NODE_ENV=test node server.js` in the site repo) |
| GamePad camera: open, live frames, take, retake, use as the card picture | Cemu 2.6 (it emulates the camera; without a webcam the frames are blank, so the colour conversion was only checked by hand: blank NV12 gives green as expected) |
| NAND layer + Wii Miis + Wii Number + friends + outbox writer | Host test: `nand.c`, `wiidata.c`, `outbox.c` built for macOS against a copy of the Dolphin test NAND with `tools/nwc24/nwc24.py config/friends` data; the written letter was read back with `nwc24.py outbox list` |
| Wii U Miis | Same host test with the real `FFL_ODB.dat`: 61 Miis, names and colours right; 12 sent to `/api/v1/mii-faces` all rendered correctly |
| Mocha mounts, Message Board, outbox on a console, letters going out from the Wii Menu | **Not tried on hardware.** Cemu has no Mocha, so none of it runs there |

## Next steps

1. Try the camera on hardware (real frames, colours, mirroring, and that closing it frees it for the next open).
2. Test on a Wii U with Aroma: Mocha mounts (`slccmpt01` with `/dev/slccmpt01`, falling back to the existing mount; `pc_mlc01` on
   the existing `/vol/storage_mlc01`), Wii mode's Miis, number and friends, Message Board pictures, Wii U Miis.
3. Outbox on hardware: queue a letter, boot the Wii Menu (vWii) with WiiLink's WiiConnect24, and check it is sent. Check that
   writing `wc24send.ctl/.mbx` through FSA keeps the files usable by vWii IOS (they are written in place; never deleted or made again).
4. The website's `/wii-app` page and the "newer version" alert point at the Wii app; add a Wii U section/link.

## How to build and test

```bash
./scripts/build_portlibs.sh
make clean && make                 # PictoCard.rpx, PictoCard.wuhb
make clean && make AUTOTEST=1      # replays sd:/autotest.txt; tap X Y and drag X1 Y1 X2 Y2 FRAMES simulate the GamePad
```

Cemu (portable mode): run the AUTOTEST `PictoCard.rpx` with `Cemu -g PictoCard.rpx`, with the script in
`portable/sdcard/autotest.txt` and `server=http://127.0.0.1:3011` in `portable/sdcard/wiiu/apps/PictoCard/settings.txt`.
Screenshots land in `portable/sdcard/shots`. Cemu emulates the GamePad camera (blank green frames without a webcam set up).

## Things worth knowing

- The canvas is 852x480; touch coordinates come in as 1280x720 and are scaled.
- `nand.h` keeps the libogc ISFS names so `wiidata.c`, `mb.c` and `outbox.c` stay close to the Wii versions; `ISFS_CreateFile`
  truncates an existing file in place instead of making a new node, to keep the vWii's owner and permissions.
- NAND u64s are read with `be64()` so the same code also runs in host tests.
- Keep all work, caches and data on the external SSD (`/Volumes/SSD`), not the internal drive.
