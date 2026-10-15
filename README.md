# PictoCard for 3DS

Send [PictoCards](https://pictocard.net) from your Nintendo 3DS. Take a picture with the 3DS's camera, pick one of your Nintendo 3DS Camera photos or any picture on the SD card, or draw a doodle on the touch screen; add one of your Miis, and send it by email or as a letter to a Wii Message Board with WiiLink - with the same fonts, colors, backgrounds, letterheads and live preview as the website. A port of PictoCard for Wii.

## Pictures

Tap **Picture** to choose where it comes from:

- **Take a picture with the camera**: a live viewfinder on the top screen. A, L or R takes the picture, X switches between the outer and inner camera, B cancels; then use it or retake it.
- **3DS Camera photos**: the photos the Nintendo 3DS Camera saved in `DCIM`, newest first, with the highlighted one shown on the top screen. 3D photos (`.MPO`) use the left image.
- **Browse the SD card**: any JPEG, PNG, GIF or BMP, with a preview on the top screen.

## Installing

1. Extract `PictoCard-3DS.zip` to the root of your SD card. It adds `3ds/PictoCard/PictoCard.3dsx`.
2. Open PictoCard from the Homebrew Launcher. The 3DS needs to be connected to the Internet.

## Sending to a Wii

Letters go to the Wii Number you type in, and come from PictoCard's own Wii Number, which the app shows. The Wii you send to needs PictoCard in its address book: tap **Ask PictoCard to add that Wii** and PictoCard sends that Wii a friend request, which shows up after its next WiiConnect24 check. The app remembers the last Wii Number you sent to.

A letter can carry a picture, a PictoCard or a doodle, on any of PictoCard's letterheads. 3DS Miis can be on a PictoCard, but the Mii face on a Wii letter has to be a Wii Mii, so letters from the 3DS go without one.

## Controls

| | |
| --- | --- |
| Touch screen | Tap a field to change it, drag to scroll |
| D-Pad up / down, Circle Pad | Move between fields, scroll |
| D-Pad left / right | Change the highlighted choice |
| A | Edit or choose |
| B | Back |
| L / R | Page up / down; letter pages on the review screen |
| Y | Review screen: show the letter's attachment |
| START | Review, then send |
| SELECT | Light or dark mode |
| START + SELECT | Exit |

On the doodle pad: draw with the stylus, tap a colour or a tool (draw, fill, erase, brush size, undo, clear). D-Pad left / right changes the colour, up / down the brush size, Y the tool, L undoes, START is done and B cancels.

In the camera: A, L or R takes the picture, X switches cameras, B cancels or retakes. Pictures can be up to 6 megapixels; they are resized on the 3DS before uploading.

## Settings

`sdmc:/3ds/PictoCard/settings.txt` remembers your name, email, the Wii Number you last sent to and light/dark mode. Add a `server=` line to use another PictoCard server, for example a local one while developing:

```
server=http://192.168.1.10:3000
wii=1234567890123456
```

## Building

You need devkitPro with devkitARM, libctru and citro2d (`3ds-dev`). Everything else is built here or is in `source/`:

```bash
./scripts/build_portlibs.sh   # mbedTLS 3.6, into ./portlibs/3ds
make                          # PictoCard.3dsx
make dist                     # dist/PictoCard-3DS.zip
```

`make AUTOTEST=1` builds a test version that replays `sdmc:/3ds/PictoCard/autotest.txt` (button presses, taps, keyboard answers) and saves screenshots of both screens to `sdmc:/3ds/PictoCard/shots`, for exercising the app in an emulator such as Azahar; `tools/shots.py` joins them into PNGs. `tools/stub-server.js` runs a local PictoCard server with email and Wii delivery stubbed out, so sends can be tested without anything leaving the machine.

Every push to `main` builds the app on GitHub Actions and updates the `latest` release; tags like `v1.0.0` get their own release.

## How it works

The app talks to PictoCard's JSON API (`/api/v1`) over HTTPS with mbedTLS and the root certificates in `source/certs.h`; the 3DS's own HTTPS service is too old for today's servers. The server renders the cards, so previews look exactly like the website's. Because a captcha can't run on a 3DS, sends are protected by a proof-of-work challenge the 3DS solves (a few seconds of SHA-256), plus rate limits, moderation and opt-outs on the server; emailed cards still need the code sent to your own address.

Wii Numbers are checked with the same checksum a real Wii's number carries (as in RiiConnect24's wiino), so typos are caught before anything is sent.

## Credits

- PictoCard for Wii, which this is ported from
- [mbedTLS](https://github.com/Mbed-TLS/mbedtls), [cJSON](https://github.com/DaveGamble/cJSON) and [stb_image](https://github.com/nothings/stb)
- [libctru](https://github.com/devkitPro/libctru) and [citro2d](https://github.com/devkitPro/citro2d)

Licensed under the GNU GPL v3.
