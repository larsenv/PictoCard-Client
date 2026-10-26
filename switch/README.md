# PictoCard for Switch

Send [PictoCards](https://pictocard.net) from your Nintendo Switch. Pick any photo from your SD card, or draw a doodle, and send it by email or as a letter to a Wii Message Board with WiiLink - with the same fonts, colors, backgrounds, letterheads and live preview as the website.

This is the Switch port of the [Wii U app](../wiiu), which is the port of [PictoCard for Wii](https://pictocard.net/wii-app).

**Download:** [PictoCard-Switch.zip](https://github.com/larsenv/PictoCard-Client/releases/latest/download/PictoCard-Switch.zip)

## Installing

You need a Switch running custom firmware with the Homebrew Menu (hbmenu).

1. Extract `PictoCard-Switch.zip` to the root of your SD card. It adds `switch/PictoCard/PictoCard.nro`.
2. Open PictoCard from the Homebrew Menu (start hbmenu by holding R while launching a game, rather than from the Album, so the app gets all the memory). The Switch needs to be connected to the Internet.

## Controls

The touch screen (handheld mode) works like a pointer: tap to select, drag a list or the form to scroll it, and draw a doodle with your finger. Docked, the D-Pad or left stick moves a cursor and A selects.

| Select | Back | Review / Done | Shift / Theme | Steps / Symbols | Quit |
| --- | --- | --- | --- | --- | --- |
| Tap or A | B | + or R / ZR | − or L / ZL | Y / X | Click the right stick |

The Home button belongs to the system, so the right stick's click quits to the Homebrew Menu. Analog sticks move like the D-Pad. On a USB keyboard you can type straight into any field; arrows, Enter, Tab, Esc and Backspace work as you'd expect.

Photos can be JPEG (rotated using their EXIF orientation), PNG, WebP, GIF or BMP. They are resized on the Switch before uploading.

## What the Switch doesn't have

The Wii and Wii U versions read a few things from Wii mode's own data, and the Switch has no Wii mode, so these are left out:

- **Miis, the Wii Number and the address book.** Type a recipient's Wii Number in the form (it is still checked with the Wii's checksum), or use email. Cards and letters go out without a Mii.
- **Wii Message Board pictures and the Wii mode outbox.** Letters are sent by PictoCard (**Send it!**).
- **The camera.** The Switch gives programs no camera, so pictures come from the SD card or a doodle.

## Settings

`sd:/switch/PictoCard/settings.txt` remembers your name, email and light/dark mode. Add a `server=` line to use another PictoCard server, for example a local one while developing:

```
server=http://192.168.1.10:3000
```

## Building

You need devkitPro with devkitA64, libnx, `switch-sdl2` and these portlibs: `switch-freetype switch-harfbuzz switch-libpng switch-libjpeg-turbo switch-libwebp switch-zlib switch-bzip2`.

```bash
./scripts/build_portlibs.sh   # mbedTLS 3.6, into ./portlibs
make                          # PictoCard.nro
make dist                     # dist/PictoCard-Switch.zip for the SD card
```

`make AUTOTEST=1` builds a test version that replays `sd:/autotest.txt` and saves screenshots to `sd:/shots`.

Every push to `main` builds the app on GitHub Actions and updates the `latest` release; tags like `v1.0.0` get their own release.

## How it works

The app talks to PictoCard's JSON API (`/api/v1`) over HTTPS (mbedTLS, with the root certificates in `source/certs.h`). The server renders the cards, so previews look exactly like the website's. Because a captcha can't run on a Switch, sends are protected by a proof-of-work challenge the console solves (a few seconds of SHA-256), plus rate limits, moderation and opt-outs on the server; emailed cards still need the code sent to your own address.

Wii Numbers are checked with the same checksum a real Wii's number carries (as in RiiConnect24's wiino), so typos are caught before anything is sent.

Drawing is done in software on an 852×480 canvas that the GPU (through SDL2) scales to the Switch's screen. The code is shared with the Wii U app; `source/nand.c` and `source/camera.c` are empty stand-ins for Wii mode's data and the GamePad camera, so the Wii code that reads them just finds nothing. The Wii's data is big-endian and the Switch isn't, so the sounds are byte-swapped as they are mixed.

## Credits

- [libnx](https://github.com/switchbrew/libnx) and [SDL2 for Switch](https://github.com/devkitPro/SDL)
- [mbedTLS](https://github.com/Mbed-TLS/mbedtls), [cJSON](https://github.com/DaveGamble/cJSON), FreeType, libjpeg-turbo, libpng and libwebp
- Pictochat font by Kirby3 (CC BY-SA 3.0), see `data/pictochat-license.txt`
- Wii pointer cursor from [PrimmR's Wii Pointer Cursors](https://primmr.dev/projects/wii-pointer-cursors/)
- Message Board reading (VFF and picture formats) from cdbackup and tiny-AES-c
- Mii faces rendered by Arian Kordi's Mii renderer
- Menu sound effects (`data/sfx_*.pcm`) from [RiftWii](https://github.com/KakarottoCake/Riftwii) (GPL-3.0-or-later), made by its `tools/make_sounds.py`

Licensed under the GNU GPL v3.
