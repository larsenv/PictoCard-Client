# WiiConnect24 test data (friends, inbox, outbox)

`nwc24.py` writes the files the NWC24 library keeps in `/shared2/wc24` of a Wii
NAND, so PictoCard-Wii can be tested against a friend list and Message Board
mail. It needs only Python 3. `sendcgi.py` is a stand-in mail server that
catches what KD posts from the send box.

```bash
N=<dolphin user dir>/Wii                     # the NAND root (holds shared2)
python3 nwc24.py config  --nand $N --set 3529920276399831     # this Wii's number
python3 nwc24.py friends write friends.example.json --nand $N --rfl-db $N/shared2/menu/FaceLib/RFL_DB.dat
python3 nwc24.py friends dump --nand $N
python3 nwc24.py mail --from 1753053886132727 --to 3529920276399831 --name PictoCard \
        --text "Hello" --jpeg card.jpg --rfl-db $N/shared2/menu/FaceLib/RFL_DB.dat --mii 0 --out in.eml
python3 nwc24.py inbox add in.eml --nand $N   # receive box (letters that arrived)
python3 nwc24.py mail ... --envelope --out out.eml
python3 nwc24.py outbox add out.eml --nand $N # send box (KD posts these to send.cgi)
python3 nwc24.py inbox list --nand $N --dump ./msgs
python3 nwc24.py wiino --count 5 --seed test  # valid test Wii Numbers
```

Run these while Dolphin is closed: Dolphin rewrites the NAND files on exit.
`inbox`/`outbox` also take `clear` and `init`. Dolphin's default NAND has 48-byte
stub mailboxes the library cannot mount; `add` rebuilds them at full size.

## What is verified

Everything was checked in Dolphin with the RevoEX 2.4 NWC24Editor, which uses
the real NWC24 library:

- **Friend list** (`nwc24fl.bin`): the editor shows all friends, the established and
  pending states, the nicknames, Mii IDs and Wii Numbers (`docs/nwc24/friend-list.png`).
- **Receive box**: the library lists and opens host-written letters: type
  WII_MENU, app HAEA, AltName, NoReply, a JPEG attachment, and the UTF-16 body
  (`docs/nwc24/mailboxes.png`, `letter.png`). Replaying a letter the editor itself
  committed gives byte-identical `.ctl` and `.mbx` output, apart from the FAT timestamps.
- **Send box**: the same applies; the editor lists host-written outgoing letters
  (`docs/nwc24/outbox.png`). With `config --server http://127.0.0.1:8099` and
  `sendcgi.py` running, "send now" (B + − on the editor's top screen) makes
  Dolphin post the send box to `send.cgi`.
  **Known issue:** Dolphin (2609) reads the 2 MiB send-box VFF as FAT16, but the
  library formats it as FAT12 (4055 data clusters). A one-cluster letter (≤ 512
  bytes) still sends; longer ones fail in Dolphin with "Failed to read file … in
  VFF: 2", and KD drops them. This tool writes FAT12 like the library, which is
  what a real Wii expects. See `Core/IOS/Network/KD/VFF/VFFUtil.cpp`
  (`read_vff_header` uses volume_size / cluster_size instead of the data cluster count).

Not checked yet: whether the Wii Menu moves host-written inbox letters onto the
Message Board (`cdb.vff`). The test profile booted the System Menu as far as its
"Press A" screen.

## In the app

PictoCard-Wii reads the same friend list (`source/wiidata.c`) and offers it when you choose the
recipient Wii Number. On the review screen, "Send from this Wii (outbox)" queues the letter in the send box
with `source/outbox.c` and asks KD to send it straight away (`docs/nwc24/app-outbox-button.png`). The letter
comes from this Wii's own number, so the recipient must have this Wii as a friend. In Dolphin, a text letter
reached `sendcgi.py`. A picture letter was written correctly (its entry is identical to the library's,
NWC24Editor opens it, and the JPEG is intact), but Dolphin's FAT bug above stops it from being sent there.

## Formats (from the RevoEX NWC24 library and Dolphin)

- `nwc24fl.bin` (0x8060 bytes): 'WcFl', version 2, max 100, registered count;
  established bitmask `u32[4]` at 0x10 (friend *i* = bit *i & 31* of word *i >> 5*);
  `u64` friend codes at 0x40; `NWC24FriendInfo[100]` (0x140 bytes) at 0x360:
  type (1 Wii, 2 email), status (1 pending, 2 established), UTF-16BE nickname
  (10 characters), u64 Mii ID (RFLCharData +0x18), then the u64 Wii Number or an
  email address. For an email friend, the code is 0x80|c0, c1…c6, then the length.
- `nwc24msg.cfg`: u64 number at 8, creation stage at 0x14, password at 0x58,
  mlchkid at 0x78, URLs at 0x9C + n×0x80 (account, check, receive, delete,
  send); checksum at 0x3FC = sum of the first 255 words.
- Mailboxes: see the comments in `nwc24.py` (VFF layout, `.ctl` entry fields,
  flag bits, MIME codes).

## Running NWC24Editor in Dolphin

The SDK's `NWC24Editor.wad` is signed for development consoles. Re-sign it with
rvthtool's `wadresign resign -k retail NWC24Editor.wad NWC24Editor-retail.wad`, then
boot it with `Dolphin -u <user dir> -b -e NWC24Editor-retail.wad`. The `.elf`
version returns to the menu straight away in Dolphin. Do not commit any SDK
files.

- The From/To pickers list the friends from `nwc24fl.bin`; text is typed with the D-Pad.
- To drive the editor without a mouse, use Dolphin's pipe input: set
  `Device = Pipe/0/wii` for Wiimote1, map IR to `Axis MAIN X/Y ±`, make a FIFO
  at `<user dir>/Pipes/wii`, set `[Input] BackgroundInput = True`, and write
  `SET MAIN x y`, `PRESS A` and `RELEASE A` to the FIFO. With IR Vertical Offset 30
  and Total Pitch 30, the screen y ≈ 228 + (v − 0.5) × 675 and x ≈ 320 + (u − 0.5) × 1130.
- If the editor reports `NWC24_ERR_INTERNAL_VF`, the mailboxes are Dolphin's stubs:
  run `nwc24.py inbox init` / `outbox init`, or tool → NWC24 → initialize NWC24
  (that also clears the friends and the Wii Number).
