# PictoCard Client

**Version:** 1.3.2

Homebrew apps for [PictoCard](https://pictocard.net): pick a photo, add a Mii, and send a PictoCard by email or as a letter to a Wii Message Board with WiiLink, from your console. They look and behave like the website, with the same fonts, colors, backgrounds, letterheads and live preview. All the clients live in this one repository so a change can be made to every console at once.

| Console | Folder | Download | Install |
| --- | --- | --- | --- |
| Wii | [`wii/`](wii) | `PictoCard-Wii.zip` | Extract to the SD card or USB drive, open from the Homebrew Channel |
| Wii U | [`wiiu/`](wiiu) | `PictoCard-WiiU.zip` | Extract to the SD card, open from the Wii U Menu (Aroma) |
| Switch | [`switch/`](switch) | `PictoCard-Switch.zip` or `PictoCard.nro` | Extract to the SD card, open from the Homebrew Menu (hbmenu) |
| 3DS | [`3ds/`](3ds) | `PictoCard-3DS.zip` or `PictoCard.cia` | Extract to the SD card for the Homebrew Launcher, or install the CIA with FBI |

Grab the builds from the [latest release](../../releases/latest). Pushing a version tag builds all four in one workflow and publishes it on the [releases page](../../releases). The same downloads are on [pictocard.net/app](https://pictocard.net/app).

See each folder's README for controls, settings and build instructions. Each client builds on its own with `make` from inside its folder (`git clone --recurse-submodules`: the Wii app uses [libwiiinput](https://github.com/quatric/libwiiinput) for controllers, USB keyboard and mouse, [libwiiupdate](https://github.com/quatric/libwiiupdate) for the update check and [libwiiosk](https://github.com/quatric/libwiiosk) for the on-screen keyboard).

PictoCard is not affiliated with Nintendo. Nintendo, Wii, Wii U, Switch and 3DS are trademarks of Nintendo.
