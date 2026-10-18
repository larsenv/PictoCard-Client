# PictoCard for Wii U

Send [PictoCards](https://pictocard.net) from your Wii U. Pick any photo from your SD card or take one with the GamePad camera, add a Mii from your Wii U or from Wii mode, and send it by email or as a letter to a Wii Message Board with WiiLink - with the same fonts, colors, backgrounds, letterheads and live preview as the website.

This is the Wii U port of [PictoCard for Wii](https://pictocard.net/wii-app).

**Download:** [PictoCard-WiiU.zip](https://github.com/larsenv/PictoCard-WiiU/releases/latest/download/PictoCard-WiiU.zip)

## Installing

You need the [Aroma](https://aroma.foryour.cafe) environment.

1. Extract `PictoCard-WiiU.zip` to the root of your SD card. It adds `wiiu/apps/PictoCard/PictoCard.wuhb`.
2. Open PictoCard from the Wii U Menu. The Wii U needs to be connected to the Internet.

To receive letters, a Wii adds PictoCard as a friend once. The app can send that friend request for Wii mode's Wii Number.

## Controls

The GamePad's touch screen works like the Wii Remote pointer: tap to select, drag a list or the form to scroll it, and draw a doodle with your finger or the stylus. The TV and the GamePad show the same picture.

| Controller | Select | Back | Review / Done | Shift / Theme | Steps / Symbols |
| --- | --- | --- | --- | --- | --- |
| Wii U GamePad (touch, buttons, left stick) | Tap or A | B | + or R / ZR | − or L / ZL | Y / X |
| Wii Remote (pointer or D-Pad, Nunchuk stick) | A | B | + | − | 1 / 2 |
| Classic Controller / Wii U Pro Controller | a | b | + or R / ZR | − or L / ZL | y / x |

Analog sticks move like the D-Pad. On a USB keyboard you can type straight into any field; arrows, Enter, Tab, Esc and Backspace work as you'd expect. HOME exits to the Wii U Menu.

Photos can be JPEG (rotated using their EXIF orientation), PNG, WebP, GIF or BMP. They are resized on the Wii U before uploading.

## The GamePad camera

When you choose a picture, pick **Take a photo with the GamePad camera**. The GamePad shows what its camera sees, mirrored. Tap the picture or press A to take the photo, then **Use photo** (A or +) or **Retake** (B).

## Wii mode and the Wii U's Miis

The Wii U keeps Wii mode's (the vWii's) system memory on its own storage. PictoCard reads it through the Mocha payload that Aroma runs, so it can use what the Wii version reads from a Wii:

- **Miis:** your Wii U Miis from Mii Maker, and Wii mode's Miis from the Mii Channel. A Wii U Mii shows on PictoCards. Wii Message Board letters can only show a Wii mode Mii.
- **Wii Number and address book:** Wii mode's own Wii Number (for the friend request) and the friends in its Message Board address book, to pick a recipient.
- **Message Board pictures:** pictures from messages on Wii mode's Message Board (and Message Board messages saved to the SD card).
- **Queue in the Wii mode outbox:** on the review screen, a Wii letter can go into Wii mode's own WiiConnect24 send box instead of being sent by PictoCard. It goes out from Wii mode's Wii Number, so the recipient needs it as a friend. WiiConnect24 only runs in Wii mode, so the letter waits until the next time you open the Wii Menu (with WiiLink's WiiConnect24 set up) and it checks for mail. **Send it!** sends right away through PictoCard instead.

Without Mocha the app still works, just without these.

## Settings

`sd:/wiiu/apps/PictoCard/settings.txt` remembers your name, email and light/dark mode. Add a `server=` line to use another PictoCard server, for example a local one while developing:

```
server=http://192.168.1.10:3000
```

## Building

You need devkitPro with devkitPPC, [wut](https://github.com/devkitPro/wut), `wiiu-sdl2` and these portlibs: `ppc-freetype ppc-libpng ppc-libjpeg-turbo ppc-libwebp ppc-zlib ppc-bzip2 ppc-brotli`.

```bash
./scripts/build_portlibs.sh   # mbedTLS 3.6 and libmocha, into ./portlibs
make                          # PictoCard.rpx and PictoCard.wuhb
make dist                     # dist/PictoCard-WiiU.zip for the SD card
```

`make AUTOTEST=1` builds a test version that replays `sd:/autotest.txt` (button presses, typing, pointer moves, and GamePad `tap X Y` and `drag X1 Y1 X2 Y2 FRAMES`) and saves screenshots to `sd:/shots`, for exercising the app in Cemu. Cemu has no Mocha, so the Wii mode and Mii features can't be tried there.

Every push to `main` builds the app on GitHub Actions and updates the `latest` release; tags like `v1.0.0` get their own release.

## How it works

The app talks to PictoCard's JSON API (`/api/v1`) over HTTPS (mbedTLS, with the root certificates in `source/certs.h`). The server renders the cards, so previews look exactly like the website's. Because a captcha can't run on a Wii U, sends are protected by a proof-of-work challenge the console solves (a few seconds of SHA-256), plus rate limits, moderation and opt-outs on the server; emailed cards still need the code sent to your own address.

Wii Numbers are checked with the same checksum a real Wii's number carries (as in RiiConnect24's wiino), so typos are caught before anything is sent.

Drawing is done in software on an 852×480 canvas that the GPU (through SDL2) scales to the TV and the GamePad. `source/nand.c` mounts Wii mode's NAND (`/vol/storage_slccmpt01`) and the Wii U's storage with libmocha. The Wii code's NAND calls run on top of it unchanged: Miis from `RFL_DB.dat`, the Wii Number from `nwc24msg.cfg`, friends from `nwc24fl.bin`, the Message Board from `cdb.vff`, and the send box in `wc24send.ctl/.mbx`. The Wii U's Miis come from Mii Maker's `FFL_ODB.dat`, converted to the 3DS byte order the server reads.

## Credits

- [wut](https://github.com/devkitPro/wut), [SDL2 for Wii U](https://github.com/devkitPro/SDL) and [libmocha](https://github.com/wiiu-env/libmocha)
- [mbedTLS](https://github.com/Mbed-TLS/mbedtls), [cJSON](https://github.com/DaveGamble/cJSON), FreeType, libjpeg-turbo, libpng and libwebp
- Pictochat font by Kirby3 (CC BY-SA 3.0), see `data/pictochat-license.txt`
- Wii pointer cursor from [PrimmR's Wii Pointer Cursors](https://primmr.dev/projects/wii-pointer-cursors/)
- Message Board reading (VFF and picture formats) from cdbackup and tiny-AES-c
- Mii faces rendered by Arian Kordi's Mii renderer
- Menu sound effects (`data/sfx_*.pcm`) from [RiftWii](https://github.com/KakarottoCake/Riftwii) (GPL-3.0-or-later), made by its `tools/make_sounds.py`

Licensed under the GNU GPL v3.
