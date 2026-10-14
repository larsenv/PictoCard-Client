# PictoCard for Wii

Send [PictoCards](https://pictocard.net) from your Wii. Pick any photo from your SD card or USB drive, add a Mii from your Wii, and send it by email or as a letter to a Wii Message Board with WiiLink - with the same fonts, colors, backgrounds, letterheads and live preview as the website.

**Download:** [PictoCard-Wii.zip](https://github.com/larsenv/PictoCard-Wii/releases/latest/download/PictoCard-Wii.zip) (or see [pictocard.net/wii-app](https://pictocard.net/wii-app))

## Installing

1. Extract `PictoCard-Wii.zip` to the root of your SD card or USB drive. It adds `apps/PictoCard`.
2. Open PictoCard from the Homebrew Channel. The Wii needs to be connected to the Internet.

To receive letters, a Wii adds PictoCard as a friend once. The app can send that friend request to the Wii it runs on.

## Controls

Every controller GlowWii supports works here, plus the Wii Remote pointer, a USB mouse and a USB keyboard. You can also draw a doodle (pointer, mouse or D-Pad) and send it as the card instead of a PictoCard:

| Controller | Select | Back | Review / Done | Shift / Theme | Steps / Symbols |
| --- | --- | --- | --- | --- | --- |
| Wii Remote (pointer or D-Pad, Nunchuk stick) | A | B | + | − | 1 / 2 |
| Classic Controller / Wii U Pro Controller | a | b | + or ZR | − or ZL | y / x |
| GameCube controller | A | B | R or Z | L | Y / X |
| Wii U GamePad (Wii U vWii, patched fw.img) | A | B | + or R | − or L | Y / X |
| DualShock 3 (USB) | ✕ | ○ | Start or R1 | Select or L1 | □ / △ |

A USB mouse moves the pointer; left click selects, right click goes back, middle click is Review / Done, and the wheel scrolls. Analog sticks move like the D-Pad. On a USB keyboard you can type straight into any field; arrows, Enter, Tab, Esc and Backspace work as you'd expect. HOME (PS on a DualShock 3, Start on a GameCube controller) exits to the Homebrew Channel.

Photos can be JPEG (rotated using their EXIF orientation), PNG, WebP, GIF or BMP. They are resized on the Wii before uploading.

## Settings

`apps/PictoCard/settings.txt` remembers your name, email and light/dark mode. Add a `server=` line to use another PictoCard server, for example a local one while developing:

```
server=http://192.168.1.10:3000
```

## Building

You need devkitPro with devkitPPC and these portlibs: `ppc-freetype ppc-libpng ppc-libjpeg-turbo ppc-libwebp ppc-zlib ppc-bzip2 ppc-brotli`.

```bash
./scripts/build_portlibs.sh   # mbedTLS 3.6 and the extra controller libraries, into ./portlibs
make                          # PictoCard.dol
make dist                     # dist/PictoCard-Wii.zip for the Homebrew Channel
```

`make WITH_WIIDRC=0 WITH_WUPC=0 WITH_SICKSAXIS=0` leaves out the Wii U GamePad, Wii U Pro Controller and DualShock 3 support.

`make AUTOTEST=1` builds a test version that replays `sd:/autotest.txt` (button presses, typing and pointer moves) and saves screenshots to `sd:/shots`, for exercising the app in Dolphin.

Every push to `main` builds the app on GitHub Actions and updates the `latest` release; tags like `v1.1.0` get their own release.

## How it works

The app talks to PictoCard's JSON API (`/api/v1`) over HTTPS (mbedTLS, with the root certificates in `source/certs.h`). The server renders the cards, so previews look exactly like the website's. Because a captcha can't run on a Wii, sends are protected by a proof-of-work challenge the Wii solves (a few seconds of SHA-256), plus rate limits, moderation and opt-outs on the server; emailed cards still need the code sent to your own address.

Wii Numbers are checked with the same checksum a real Wii's number carries (as in RiiConnect24's wiino), so typos are caught before anything is sent.

Your Miis come from the Mii Channel database and the Wii Number from the WiiConnect24 configuration, read from the Wii's NAND.

## Credits

- Controller support from [GlowWii](https://github.com/larsenv/GlowWii), using [libwiidrc](https://github.com/FIX94/libwiidrc), [libwupc](https://github.com/FIX94/libwupc) and [libsicksaxis](https://github.com/xerpi/libsicksaxis)
- [mbedTLS](https://github.com/Mbed-TLS/mbedtls), [cJSON](https://github.com/DaveGamble/cJSON), FreeType, libjpeg-turbo, libpng and libwebp
- Pictochat font by Kirby3 (CC BY-SA 3.0), see `data/pictochat-license.txt`
- Wii pointer cursor from [PrimmR's Wii Pointer Cursors](https://primmr.dev/projects/wii-pointer-cursors/)
- Message Board reading (VFF and picture formats) from cdbackup, [libpatcher](https://github.com/WiiLink24/libpatcher) (MIT) and tiny-AES-c
- Mii faces rendered by Arian Kordi's Mii renderer

Licensed under the GNU GPL v3.
