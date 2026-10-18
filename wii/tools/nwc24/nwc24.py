#!/usr/bin/env python3
"""WiiConnect24 test data for PictoCard-Wii.

Builds the files the NWC24 library keeps in /shared2/wc24 so the app can be
tested with friends and Message Board mail, either straight into a Dolphin
NAND or as input for the RevoEX NWC24Editor tool on a Wii.

  wiino      check or generate Wii Numbers (with a valid checksum)
  friends    write or dump nwc24fl.bin (the Wii Message Board address book)
  config     show or set this Wii's own Wii Number in nwc24msg.cfg
  mail       build a Message Board letter the way PictoCard's server does
  inbox      add letters to, list or empty the receive box (wc24recv.ctl/.mbx)
  editor-sd  write /template and /misc for NWC24Editor onto an SD folder

Only the Python standard library is used. See tools/nwc24/README.md.
"""

import argparse
import base64
import binascii
import datetime
import json
import os
import random
import struct
import sys

# ── Wii Numbers ──────────────────────────────────────────────────────────────
# A Wii Number is a scrambled 53-bit value with an 11-bit CRC, as in
# RiiConnect24's wiino. source/wiino.c and the website use the same check.

MASK53 = (1 << 53) - 1
_TABLE2 = [0x1, 0x5, 0x0, 0x4, 0x2, 0x3, 0x6, 0x7]
_TABLE1_INV = [0xD, 0x5, 0x9, 0x7, 0x0, 0xF, 0xA, 0x2, 0xC, 0x3, 0xE, 0x1, 0x8, 0x6, 0xB, 0x4]


def _get_byte(v, i):
    return (v >> (i * 8)) & 0xFF


def _put_byte(v, i, b):
    return (v & ~(0xFF << (i * 8))) | (b << (i * 8))


def _crc_reduce(v):
    for i in range(43):
        if (v >> (52 - i)) & 1:
            v ^= 0x635 << (42 - i)
    return v


def _unscramble(v, mask_low=True):
    v &= MASK53
    v ^= 0x5E5E5E5E5E5E
    v &= MASK53
    v |= ((v << 5) & 0x20) << 48
    v >>= 1
    copy = v
    for i in range(6):
        v = _put_byte(v, i, _get_byte(copy, _TABLE2[i]))
    for i in range(6):
        b = _get_byte(v, i)
        v = _put_byte(v, i, (_TABLE1_INV[b >> 4] << 4) | _TABLE1_INV[b & 0xF])
    hi = v >> 32
    a = (v >> 0x16) | ((hi & 0x7FF) << 10)
    b = v * 0x400
    if mask_low:
        b &= 0xFFFFFFFF
    else:
        b &= (1 << 64) - 1
    b |= (hi >> 0xB) & 0x3FF
    return (((a << 32) | b) & ((1 << 64) - 1) ^ 0xB3B3B3B3B3B3) & MASK53


def wiino_valid(digits):
    """True when digits is a 16-digit Wii Number with a valid checksum.

    The number must pass both the website's check (32-bit low half) and the
    app's (64-bit arithmetic in source/wiino.c), so test data works in both."""
    digits = str(digits).replace(" ", "").replace("-", "")
    if len(digits) != 16 or not digits.isdigit():
        return False
    v = int(digits)
    if not v or v > MASK53:
        return False
    for mask_low in (True, False):
        d = _unscramble(v, mask_low)
        if _crc_reduce(d) != 0:
            return False
        if ((d >> 47) & 7) > 2 or ((d >> 50) & 7) > 6:
            return False
    return True


def wiino_generate(count, seed=None):
    """Valid Wii Numbers, reproducible for a given seed."""
    rng = random.Random(seed)
    out = []
    while len(out) < count:
        v = rng.randrange(10 ** 15, MASK53 + 1)
        s = "%016d" % v
        if wiino_valid(s) and s not in out:
            out.append(s)
    return out


def pretty(digits):
    return " ".join(digits[i:i + 4] for i in range(0, 16, 4))


# ── Friend list: /shared2/wc24/nwc24fl.bin ───────────────────────────────────
# From the RevoEX 2.4 NWC24 library (NWC24FriendList.o) and its manual:
#   0x000 'WcFl', u32 version 2, u32 max entries (100), u32 registered count
#   0x010 u32[4] established bitmask: friend i is bit (i & 31) of word i >> 5
#   0x03C u32 registration-message flag
#   0x040 u64[100] friend codes (Wii Number, or a hash of an email address)
#   0x360 NWC24FriendInfo[100], 0x140 bytes each:
#         +0x00 u32 type (1 Wii, 2 email)   +0x04 u32 status (1 pending, 2 established)
#         +0x08 u16[12] nickname, UTF-16BE, 10 characters at most
#         +0x20 u64 Mii ID (RFLCreateID) of the friend's Mii, or 0
#         +0x28 u32[6] reserved
#         +0x40 address: u64 Wii Number, or a NUL-terminated email address (256 bytes)

FL_PATH = "shared2/wc24/nwc24fl.bin"
FL_MAGIC = b"WcFl"
FL_MAX = 100
FL_SIZE = 0x360 + FL_MAX * 0x140
FI_TYPE_WII, FI_TYPE_PUBLIC = 1, 2
FI_STATE_PENDING, FI_STATE_ESTABLISHED = 1, 2


def email_friend_code(addr):
    """The 64-bit key the library files an email friend under."""
    v = 0x80
    for i in range(7):
        if i < len(addr):
            v |= ord(addr[i].lower())
        v = (v << 8) & ((1 << 64) - 1)
    return v | (len(addr) & 0xFF)


def friends_build(friends):
    """friends: list of dicts with name, and wii (16 digits) or email; optional
    established (default True), mii_id (int)."""
    if len(friends) > FL_MAX:
        raise ValueError("at most %d friends" % FL_MAX)
    buf = bytearray(FL_SIZE)
    struct.pack_into(">4sIII", buf, 0, FL_MAGIC, 2, FL_MAX, len(friends))
    for i, f in enumerate(friends):
        name = f.get("name", "")
        if len(name) > 10:
            raise ValueError("nickname %r is longer than 10 characters" % name)
        established = f.get("established", True)
        entry = 0x360 + i * 0x140
        if f.get("wii"):
            digits = str(f["wii"]).replace(" ", "").replace("-", "")
            if not wiino_valid(digits):
                raise ValueError("%s is not a valid Wii Number" % f["wii"])
            code = int(digits)
            ftype = FI_TYPE_WII
            struct.pack_into(">Q", buf, entry + 0x40, code)
        elif f.get("email"):
            addr = f["email"].encode("ascii")
            if len(addr) > 255:
                raise ValueError("email address too long")
            code = email_friend_code(f["email"])
            ftype = FI_TYPE_PUBLIC
            buf[entry + 0x40:entry + 0x40 + len(addr)] = addr
        else:
            raise ValueError("friend %r needs wii or email" % name)
        struct.pack_into(">Q", buf, 0x40 + i * 8, code)
        status = FI_STATE_ESTABLISHED if established else FI_STATE_PENDING
        struct.pack_into(">II", buf, entry, ftype, status)
        raw = name.encode("utf-16-be")
        buf[entry + 8:entry + 8 + len(raw)] = raw
        struct.pack_into(">Q", buf, entry + 0x20, int(f.get("mii_id", 0)))
        if established:
            word = 0x10 + (i >> 5) * 4
            (bits,) = struct.unpack_from(">I", buf, word)
            struct.pack_into(">I", buf, word, bits | (1 << (i & 31)))
    return bytes(buf)


def friends_parse(data):
    if len(data) < FL_SIZE or data[:4] != FL_MAGIC:
        raise ValueError("not an nwc24fl.bin")
    _, version, maxn, count = struct.unpack_from(">4sIII", data, 0)
    est = struct.unpack_from(">4I", data, 0x10)
    out = []
    for i in range(min(maxn, FL_MAX)):
        (code,) = struct.unpack_from(">Q", data, 0x40 + i * 8)
        if not code:
            continue
        entry = 0x360 + i * 0x140
        ftype, status = struct.unpack_from(">II", data, entry)
        name = data[entry + 8:entry + 0x20].decode("utf-16-be").split("\0")[0]
        (mii_id,) = struct.unpack_from(">Q", data, entry + 0x20)
        f = {"index": i, "name": name, "type": ftype, "status": status,
             "established": bool(est[i >> 5] & (1 << (i & 31))), "mii_id": mii_id}
        if ftype == FI_TYPE_WII:
            f["wii"] = "%016d" % struct.unpack_from(">Q", data, entry + 0x40)[0]
        else:
            f["email"] = data[entry + 0x40:entry + 0x140].split(b"\0")[0].decode("ascii", "replace")
        out.append(f)
    return {"version": version, "max": maxn, "registered": count, "friends": out}


def rfl_mii_ids(rfl_db):
    """(name, Mii ID) for each Mii in RFL_DB.dat. The ID is RFLCharData +0x18..0x20."""
    out = []
    for i in range(100):
        m = rfl_db[4 + i * 74:4 + (i + 1) * 74]
        if len(m) < 74 or m[2:4] == b"\0\0":
            continue
        name = m[2:22].decode("utf-16-be").split("\0")[0]
        out.append((name, struct.unpack_from(">Q", m, 0x18)[0]))
    return out


# ── nwc24msg.cfg (this Wii's own number) ─────────────────────────────────────
# 0x400 bytes: 'WcCf', u32 version 8, u64 Wii Number, u32 id generation,
# u32 creation stage (0 none, 1 generated, 2 registered), ..., u32 checksum at
# 0x3FC = sum of the first 0xFF big-endian words.

# Layout as in Dolphin's NWC24Config.h: email at 0x18, password at 0x58,
# mlchkid at 0x78, then five 0x80-byte URLs from 0x9C: account, check,
# receive, delete and send (KD posts the send box to the last one).

CFG_PATH = "shared2/wc24/nwc24msg.cfg"
CFG_PASSWORD, CFG_MLCHKID, CFG_URLS = (0x58, 0x20), (0x78, 0x24), 0x9C
CFG_URL_NAMES = ["account", "check", "receive", "delete", "send"]


def cfg_checksum(buf):
    return sum(struct.unpack_from(">255I", buf, 0)) & 0xFFFFFFFF


def _cfg_str(buf, off, size, value=None):
    if value is not None:
        raw = value.encode("ascii")
        if len(raw) >= size:
            raise ValueError("%r is too long (at most %d characters)" % (value, size - 1))
        buf[off:off + size] = raw + bytes(size - len(raw))
    return bytes(buf[off:off + size]).split(b"\0")[0].decode("ascii", "replace")


def cfg_update(data, digits=None, password=None, mlchkid=None, server=None, urls=None):
    """Set this Wii's number, mail credentials and/or the mail server URLs.
    server is a base like http://127.0.0.1:8099 (gets /cgi-bin/<name>.cgi);
    urls maps a name from CFG_URL_NAMES to a full URL."""
    buf = bytearray(data)
    if buf[:4] != b"WcCf" or len(buf) != 0x400:
        raise ValueError("not an nwc24msg.cfg")
    if digits:
        struct.pack_into(">Q", buf, 8, int(digits))
        (stage,) = struct.unpack_from(">I", buf, 0x14)
        struct.pack_into(">I", buf, 0x14, max(stage, 2))
    if password is not None:
        _cfg_str(buf, *CFG_PASSWORD, password)
    if mlchkid is not None:
        _cfg_str(buf, *CFG_MLCHKID, mlchkid)
    urls = dict(urls or {})
    if server:
        for name in CFG_URL_NAMES:
            urls.setdefault(name, "%s/cgi-bin/%s.cgi" % (server.rstrip("/"), name))
    for name, url in urls.items():
        _cfg_str(buf, CFG_URLS + CFG_URL_NAMES.index(name) * 0x80, 0x80, url)
    struct.pack_into(">I", buf, 0x3FC, cfg_checksum(buf))
    return bytes(buf)


# ── Message Board letters ────────────────────────────────────────────────────
# Same layout as PictoCard's lib/wiiMail.js buildWiiMail(): Date and Message-Id
# like a letter written on a real Wii, X-Wii-AppId 2-48414541-0001 (Wii Message
# Board), UTF-16BE text in base64, optional JPEG and Mii (X-WiiFace).

CRLF = "\r\n"
WII_MAIL_DOMAIN = "wii.com"


def _b64_lines(data, width=76):
    s = base64.b64encode(data).decode()
    return CRLF.join(s[i:i + width] for i in range(0, len(s), width))


def word_wrap(text, width=38):
    out = []
    for para in text.replace("\r\n", "\n").split("\n"):
        lines, line = [], ""
        for word in para.split(" "):
            while len(word) > width:
                if line:
                    lines.append(line)
                    line = ""
                lines.append(word[:width])
                word = word[width:]
            if not line:
                line = word
            elif len(line) + 1 + len(word) <= width:
                line += " " + word
            else:
                lines.append(line)
                line = word
        lines.append(line)
        out.append(CRLF.join(lines))
    return CRLF.join(out)


def build_letter(sender, recipient, text, name=None, jpeg=None, mii=None, date=None,
                 envelope=False, domain=WII_MAIL_DOMAIN, no_reply=True, boundary=None):
    """A Message Board letter as bytes. With envelope=True it starts with the
    MAIL FROM / RCPT TO / DATA lines PictoCard sends to send.cgi; a receive box
    stores the message without them."""
    date = date or datetime.datetime.now(datetime.timezone.utc)
    month = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"][date.month - 1]
    date_str = "%d %s %d %02d:%02d:%02d -0000" % (date.day, month, date.year, date.hour, date.minute, date.second)
    frm = "w%s@%s" % (sender, domain)
    to = "w%s@%s" % (recipient, domain)
    msg_id = "<%s@%s>" % (binascii.hexlify(os.urandom(15)).decode().upper(), domain)
    body = word_wrap(text.strip()) if text and text.strip() else " "
    headers = []
    if envelope:
        headers += ["MAIL FROM: " + frm, "RCPT TO: " + to, "DATA"]
    headers += ["Date: " + date_str, "From: " + frm, "To: " + to, "Message-Id: " + msg_id,
                "X-Wii-AppId: 2-48414541-0001", "X-Wii-Cmd: 00044001"]
    if name:
        headers.append("X-Wii-AltName: " + base64.b64encode(name.encode("utf-16-be")).decode())
    if no_reply:
        headers.append("X-Wii-MB-NoReply: 1")
    if mii:
        face = base64.b64encode(mii[:74]).decode()
        cut = 67 - len("X-WiiFace: ")
        headers.append("X-WiiFace: " + face[:cut] + CRLF + " " + face[cut:])
    headers.append("MIME-Version: 1.0")
    text_part = ["Content-Type: text/plain; charset=utf-16be", "Content-Transfer-Encoding: base64"]
    if not jpeg:
        headers += text_part + [""]
        mail = CRLF.join(headers) + CRLF + _b64_lines(body.encode("utf-16-be")) + CRLF * 3
    else:
        boundary = boundary or "Boundary-NWC24-%X%X" % (int(date.timestamp() * 1000), random.randrange(0xFFFF))
        headers += ["Content-Type: multipart/mixed;" + CRLF + ' boundary="%s"' % boundary, ""]
        parts = [CRLF.join(text_part + ["", _b64_lines(body.encode("utf-16-be")), "", ""]),
                 CRLF.join(["Content-Type: image/jpeg;", " name=image.jpg", "Content-Transfer-Encoding: base64",
                            "Content-Disposition: attachment;", " filename=image.jpg", "",
                            _b64_lines(jpeg), "", ""])]
        mail = CRLF.join(headers) + CRLF + "".join("--%s%s%s" % (boundary, CRLF, p) for p in parts)
        mail += "--%s--%s" % (boundary, CRLF)
    return mail.encode("ascii")


# ── NWC24Editor inputs ───────────────────────────────────────────────────────

def editor_templates(numbers, subjects, bodies):
    """/template files: num.txt (decimal numbers, one per line, ASCII) and
    txt.txt (UTF-16BE, items separated by lines holding only '-')."""
    num = "".join(n + "\r\n" for n in numbers).encode("ascii")
    items = list(subjects) + list(bodies)
    txt = "\r\n-\r\n".join(items) + "\r\n"
    return {"num.txt": num, "txt.txt": txt.encode("utf-16-be")}


# ── command line ─────────────────────────────────────────────────────────────

def nand_file(nand, rel):
    return os.path.join(nand, *rel.split("/"))


def write_file(path, data):
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


def load_friends_json(path):
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    return data["friends"] if isinstance(data, dict) else data


def cmd_wiino(a):
    if a.check:
        bad = 0
        for n in a.check:
            ok = wiino_valid(n)
            bad += not ok
            print("%s  %s" % (n, "valid" if ok else "INVALID"))
        return 1 if bad else 0
    for n in wiino_generate(a.count, a.seed):
        print(pretty(n) if a.pretty else n)
    return 0


def cmd_friends(a):
    if a.action == "dump":
        path = a.file or nand_file(a.nand, FL_PATH)
        with open(path, "rb") as f:
            info = friends_parse(f.read())
        if a.json:
            print(json.dumps(info, indent=2))
        else:
            print("%d registered of %d" % (info["registered"], info["max"]))
            for fr in info["friends"]:
                addr = pretty(fr["wii"]) if "wii" in fr else fr["email"]
                print("%3d  %-10s  %-19s  %s%s" % (fr["index"], fr["name"], addr,
                                                 "established" if fr["established"] else "pending",
                                                 "  Mii %016X" % fr["mii_id"] if fr["mii_id"] else ""))
        return 0
    friends = load_friends_json(a.json_file)
    if a.rfl_db:
        with open(a.rfl_db, "rb") as f:
            miis = rfl_mii_ids(f.read())
        for fr in friends:
            if "mii" in fr and not fr.get("mii_id"):
                fr["mii_id"] = miis[int(fr["mii"])][1]
    data = friends_build(friends)
    out = a.out or nand_file(a.nand, FL_PATH)
    write_file(out, data)
    print("wrote %d friends to %s" % (len(friends), out))
    return 0


def cmd_config(a):
    path = nand_file(a.nand, CFG_PATH)
    with open(path, "rb") as f:
        data = f.read()
    if a.set and not wiino_valid(a.set.replace(" ", "")):
        print("%s is not a valid Wii Number" % a.set, file=sys.stderr)
        return 1
    if a.set or a.password is not None or a.mlchkid is not None or a.server or a.send_url:
        data = cfg_update(data, a.set.replace(" ", "") if a.set else None, a.password, a.mlchkid,
                          a.server, {"send": a.send_url} if a.send_url else None)
        write_file(path, data)
    buf = bytearray(data)
    (num,) = struct.unpack_from(">Q", buf, 8)
    print("This Wii:  %s" % (pretty("%016d" % num) if num else "(no Wii Number)"))
    print("password:  %s" % ("(set)" if _cfg_str(buf, *CFG_PASSWORD) else "(none)"))
    print("mlchkid:   %s" % ("(set)" if _cfg_str(buf, *CFG_MLCHKID) else "(none)"))
    for i, name in enumerate(CFG_URL_NAMES):
        print("%-9s  %s" % (name + ":", _cfg_str(buf, CFG_URLS + i * 0x80, 0x80)))
    return 0


def _read_opt(path):
    if not path:
        return None
    with open(path, "rb") as f:
        return f.read()


def _mii(a):
    if a.mii_file:
        return _read_opt(a.mii_file)[:74]
    if a.rfl_db is not None and a.mii is not None:
        db = _read_opt(a.rfl_db)
        live = [i for i in range(100) if db[4 + i * 74 + 2:4 + i * 74 + 4] != b"\0\0"]
        i = live[a.mii]
        return db[4 + i * 74:4 + (i + 1) * 74]
    return None


def cmd_mail(a):
    mail = build_letter(a.sender, a.to, a.text, name=a.name, jpeg=_read_opt(a.jpeg), mii=_mii(a),
                        envelope=a.envelope)
    write_file(a.out, mail)
    print("wrote %s (%d bytes)" % (a.out, len(mail)))
    return 0


def cmd_editor_sd(a):
    numbers = []
    if a.friends:
        numbers += [f["wii"] for f in load_friends_json(a.friends) if f.get("wii")]
    numbers += a.number or []
    subjects = a.subject or ["PictoCard test"]
    bodies = a.body or ["Hello from PictoCard! This letter was put in the inbox with NWC24Editor."]
    for name, data in editor_templates(numbers, subjects, bodies).items():
        write_file(os.path.join(a.sd, "template", name), data)
    for path in a.attach or []:
        with open(path, "rb") as f:
            write_file(os.path.join(a.sd, "misc", os.path.basename(path)), f.read())
    print("wrote %s/template (%d numbers) and %d file(s) in %s/misc"
          % (a.sd, len(numbers), len(a.attach or []), a.sd))
    return 0


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)

    s = sub.add_parser("wiino", help="check or generate Wii Numbers")
    s.add_argument("--check", nargs="+", metavar="NUMBER")
    s.add_argument("--count", type=int, default=5)
    s.add_argument("--seed", default=None)
    s.add_argument("--pretty", action="store_true", help="group digits in fours")
    s.set_defaults(fn=cmd_wiino)

    s = sub.add_parser("friends", help="write or dump nwc24fl.bin")
    s.add_argument("action", choices=["write", "dump"])
    s.add_argument("json_file", nargs="?", help="friends JSON (for write)")
    s.add_argument("--nand", help="Dolphin NAND root (the folder holding shared2)")
    s.add_argument("--out", help="write here instead of the NAND")
    s.add_argument("--file", help="dump this file instead of the NAND's")
    s.add_argument("--rfl-db", help="RFL_DB.dat, to link friends' \"mii\" index to a Mii ID")
    s.add_argument("--json", action="store_true")
    s.set_defaults(fn=cmd_friends)

    s = sub.add_parser("config", help="show or set this Wii's Wii Number")
    s.add_argument("--nand", required=True)
    s.add_argument("--set", metavar="NUMBER", help="this Wii's Wii Number")
    s.add_argument("--password", help="mail password (sent with every send.cgi post)")
    s.add_argument("--mlchkid", help="mail check ID")
    s.add_argument("--server", metavar="URL", help="mail server base, e.g. http://127.0.0.1:8099")
    s.add_argument("--send-url", help="only change the send.cgi URL")
    s.set_defaults(fn=cmd_config)

    s = sub.add_parser("mail", help="build a Message Board letter")
    s.add_argument("--from", dest="sender", required=True, help="sender Wii Number")
    s.add_argument("--to", required=True, help="recipient Wii Number")
    s.add_argument("--text", default="Hello from PictoCard!")
    s.add_argument("--name", help="sender name shown on the envelope (X-Wii-AltName)")
    s.add_argument("--jpeg", help="baseline JPEG to attach")
    s.add_argument("--mii-file", help="74/76-byte Mii shown as the sender's face")
    s.add_argument("--rfl-db", help="RFL_DB.dat to take the Mii from")
    s.add_argument("--mii", type=int, help="index of the Mii in --rfl-db (0 = first)")
    s.add_argument("--envelope", action="store_true", help="keep the MAIL FROM/RCPT TO/DATA lines")
    s.add_argument("--out", required=True)
    s.set_defaults(fn=cmd_mail)

    s = sub.add_parser("editor-sd", help="write NWC24Editor's /template and /misc onto an SD folder")
    s.add_argument("--sd", required=True, help="SD card root (Dolphin: <user>/Load/WiiSDSync)")
    s.add_argument("--friends", help="friends JSON: their Wii Numbers go into num.txt")
    s.add_argument("--number", action="append", help="extra Wii Number for num.txt")
    s.add_argument("--subject", action="append")
    s.add_argument("--body", action="append")
    s.add_argument("--attach", action="append", help="file to offer as an attachment (/misc)")
    s.set_defaults(fn=cmd_editor_sd)

    box_register(sub)

    a = p.parse_args(argv)
    if getattr(a, "cmd", None) == "friends":
        if a.action == "write" and not a.json_file:
            p.error("friends write needs a JSON file")
        if not (a.nand or a.out or a.file):
            p.error("give --nand, --out or --file")
    return a.fn(a)


# ── Mailboxes: /shared2/wc24/mbox/wc24{recv,send}.{ctl,mbx} ──────────────────
# Each box is a .mbx VFF holding the messages and a .ctl index.
#
# The VFF is a 0x20-byte header ('VFF ', 0xFEFF, volume size, cluster size / 16)
# followed by a little-endian FAT volume with 512-byte clusters: two FATs of
# (volume size / 512) * 2 bytes each, a 128-entry root directory, then data.
# The receive box (7 MiB) uses FAT16, the send box (2 MiB) FAT12. Messages are
# mb/rNNNNNNN.msg (receive) or mb/sNNNNNNN.msg (send), NNNNNNN being the
# message ID, zero-padded to 32 bytes. Received messages are stored without the
# SMTP envelope; outgoing ones keep the MAIL FROM / RCPT TO / DATA lines, which
# is what the mail server's send.cgi gets. Dolphin's default NAND ships 48-byte
# stubs of both files that the library cannot mount, so they are rebuilt first.
#
# The .ctl is a 0x80-byte 'WcTf' header and 0x80-byte entries (255 in the
# receive box, 127 in the send box), laid out as in Dolphin's MailCommon.h.
# The values are what the RevoEX NWC24 library writes (NWC24MBoxCtrl.o,
# NWC24Parser.o), checked against letters committed with NWC24Editor: packed
# fields are (length << 20) | offset into the message file.

CLUSTER = 512
FAT_OFF = 0x20


class Box:
    def __init__(self, name, prefix, vff_size, ctl_size, entries, flag):
        self.ctl = "shared2/wc24/mbox/wc24%s.ctl" % name
        self.mbx = "shared2/wc24/mbox/wc24%s.mbx" % name
        self.prefix = prefix
        self.vff_size = vff_size
        self.ctl_size = ctl_size
        self.entries = entries
        self.flag = flag  # NWC24CommitMsg marks the box a message went to

    def msg_name(self, msg_id):
        return "%s%07d.msg" % (self.prefix, msg_id)


RECV = Box("recv", "r", 0x700000, 0x8000, 255, 0x20)
SEND = Box("send", "s", 0x200000, 0x4000, 127, 0x10)

MIME = {"text/plain": 0x10000, "text/html": 0x10001, "image/jpeg": 0x20000,
        "image/x-wii-picture": 0x20001, "application/octet-stream": 0x30000,
        "application/x-wii-msgboard": 0x30001, "application/x-wii-minidata": 0x30002,
        "multipart/mixed": 0xF0000, "multipart/alternative": 0xF0001, "multipart/related": 0xF0002}

# entry flags
F_WII, F_PUBLIC, F_APPTYPE_1, F_APPTYPE_2 = 0x1, 0x2, 0x4, 0x8
F_ATTACHED, F_FROM_ADDR, F_INIT, F_NOREPLY = 0x10000, 0x100000, 0x200000, 0x80000000
# 0x100 and 0x200 are set by NWC24CommitMsg on Wii letters (the LED pattern
# setter requires one of them); kept so entries match what the library writes.
F_COMMIT_WII = 0x300


def _fat_datetime(t):
    date = ((t.year - 1980) << 9) | (t.month << 5) | t.day
    time = (t.hour << 11) | (t.minute << 5) | (t.second // 2)
    return time, date


def _lfn_checksum(short):
    c = 0
    for b in short:
        c = (((c & 1) << 7) + (c >> 1) + b) & 0xFF
    return c


def _dir_entries(long_name, short, attr, cluster, size, when):
    """A one-slot LFN entry (names up to 13 characters) and the 8.3 entry."""
    name = long_name.encode("utf-16-le") + b"\0\0"
    name += b"\xff" * (26 - len(name))
    lfn = bytearray(32)
    lfn[0] = 0x41
    lfn[1:11] = name[0:10]
    lfn[11] = 0x0F
    lfn[13] = _lfn_checksum(short)
    lfn[14:26] = name[10:22]
    lfn[28:32] = name[22:26]
    t, d = _fat_datetime(when)
    sfn = bytearray(32)
    sfn[0:11] = short
    sfn[11] = attr
    sfn[13] = 0x01
    struct.pack_into("<HHHHHHHI", sfn, 14, t, d, d, 0, t, d, cluster, size)
    return bytes(lfn) + bytes(sfn)


class Vff:
    def __init__(self, data, size):
        self.size = size
        self.fat_size = size // CLUSTER * 2
        self.root = FAT_OFF + 2 * self.fat_size
        self.data = self.root + 128 * 32
        self.n_clusters = (size - self.data) // CLUSTER
        self.fat12 = self.n_clusters < 4085  # FAT type follows the cluster count
        self.buf = bytearray(data or b"")
        if self.buf[:4] != b"VFF " or len(self.buf) < size:
            self.format()

    def format(self, when=None):
        when = when or datetime.datetime.now()
        self.buf = bytearray(self.size)
        struct.pack_into(">4sHHIH", self.buf, 0, b"VFF ", 0xFEFF, 0x0100, self.size, CLUSTER // 16)
        for n, v in ((0, 0xFFF0), (1, 0xFFFF), (2, 0xFFFF)):
            self.set_fat(n, v)
        self.buf[self.root:self.root + 64] = _dir_entries("mb", b"MB         ", 0x10, 2, 0, when)
        dot = _dir_entries("", b".          ", 0x10, 2, 0, when)[32:]
        dotdot = _dir_entries("", b"..         ", 0x10, 0, 0, when)[32:]
        c = self.cluster_off(2)
        self.buf[c:c + 64] = dot + dotdot

    def free_bytes(self):
        """The VF free size the library keeps in the .ctl header (0x24)."""
        return (self.n_clusters - 2 - self.used_clusters()) * CLUSTER

    def cluster_off(self, n):
        return self.data + (n - 2) * CLUSTER

    def fat(self, n):
        if not self.fat12:
            v = struct.unpack_from("<H", self.buf, FAT_OFF + n * 2)[0]
            return v if v < 0xFFF0 else 0xFFFF
        o = FAT_OFF + n * 3 // 2
        v = self.buf[o] | self.buf[o + 1] << 8
        v = v >> 4 if n & 1 else v & 0xFFF
        return v if v < 0xFF0 else 0xFFFF

    def set_fat(self, n, v):
        for fat in (FAT_OFF, FAT_OFF + self.fat_size):
            if not self.fat12:
                struct.pack_into("<H", self.buf, fat + n * 2, v)
                continue
            v12 = v & 0xFFF
            o = fat + n * 3 // 2
            if n & 1:
                self.buf[o] = (self.buf[o] & 0x0F) | (v12 << 4 & 0xF0)
                self.buf[o + 1] = v12 >> 4
            else:
                self.buf[o] = v12 & 0xFF
                self.buf[o + 1] = (self.buf[o + 1] & 0xF0) | (v12 >> 8)

    def used_clusters(self):
        return sum(1 for n in range(2, self.n_clusters + 2) if self.fat(n))

    def mb_cluster(self):
        for off in range(self.root, self.data, 32):
            if self.buf[off:off + 11] == b"MB         ":
                return struct.unpack_from("<H", self.buf, off + 26)[0]
        raise ValueError("no mb directory in the VFF")

    def chain(self, start):
        out, n = [], start
        while 2 <= n < 0xFFF0:
            out.append(n)
            n = self.fat(n)
        return out

    def dir_slots(self):
        for c in self.chain(self.mb_cluster()):
            base = self.cluster_off(c)
            for off in range(base, base + CLUSTER, 32):
                yield off

    def files(self):
        out = {}
        for off in self.dir_slots():
            if self.buf[off] in (0, 0xE5) or self.buf[off + 11] in (0x0F, 0x10):
                continue
            name = bytes(self.buf[off:off + 8]).rstrip().decode() + "." + \
                bytes(self.buf[off + 8:off + 11]).rstrip().decode()
            cluster, size = struct.unpack_from("<HI", self.buf, off + 26)
            out[name.lower()] = (off, cluster, size)
        return out

    def read(self, name):
        off, cluster, size = self.files()[name.lower()]
        data = b"".join(bytes(self.buf[self.cluster_off(c):self.cluster_off(c) + CLUSTER])
                        for c in self.chain(cluster))
        return data[:size]

    def alloc(self, count):
        free = [n for n in range(2, self.n_clusters + 2) if not self.fat(n)][:count]
        if len(free) < count:
            raise ValueError("mailbox is full")
        for a, b in zip(free, free[1:] + [0xFFFF]):
            self.set_fat(a, b)
        return free

    def write(self, long_name, short, data, when):
        # KD reuses the IDs of deleted letters: drop a leftover file of the same name
        stale = long_name.split(".")[0][:8] + "." + long_name.split(".")[1]
        if stale.lower() in self.files():
            self.delete(stale)
        clusters = self.alloc(max(1, -(-len(data) // CLUSTER)))
        for i, c in enumerate(clusters):
            o = self.cluster_off(c)
            chunk = data[i * CLUSTER:(i + 1) * CLUSTER]
            self.buf[o:o + CLUSTER] = chunk + bytes(CLUSTER - len(chunk))
        entries = _dir_entries(long_name, short, 0x20, clusters[0], len(data), when)
        slots = list(self.dir_slots())
        for i in range(len(slots) - 1):
            if self.buf[slots[i]] in (0, 0xE5) and self.buf[slots[i + 1]] in (0, 0xE5) \
                    and slots[i + 1] == slots[i] + 32:
                self.buf[slots[i]:slots[i] + 64] = entries
                return
        # grow the directory by one cluster
        last = self.chain(self.mb_cluster())[-1]
        (new,) = self.alloc(1)
        self.set_fat(last, new)
        o = self.cluster_off(new)
        self.buf[o:o + CLUSTER] = bytes(CLUSTER)
        self.buf[o:o + 64] = entries

    def delete(self, name):
        off, cluster, size = self.files()[name.lower()]
        for c in self.chain(cluster):
            self.set_fat(c, 0)
        self.buf[off] = 0xE5
        if off >= 32 and self.buf[off - 32 + 11] == 0x0F:
            self.buf[off - 32] = 0xE5


def _header_fields(head):
    """Unfolded header lines: list of (name_lower, value_offset, value_end) where
    value_end excludes the line's CRLF and continuation lines are included."""
    fields, i = [], 0
    while i < len(head):
        j = head.find(b"\r\n", i)
        if j < 0:
            j = len(head)
        line = head[i:j]
        if not line:
            break
        if line[:1] in (b" ", b"\t") and fields:
            fields[-1][2] = j
        else:
            k = line.find(b":")
            if k > 0:
                v = i + k + 1
                while v < j and head[v:v + 1] == b" ":
                    v += 1
                fields.append([line[:k].strip().lower().decode(), v, j])
        i = j + 2
    return fields


def _header_block(msg, start):
    end = msg.find(b"\r\n\r\n", start)
    return (len(msg), len(msg)) if end < 0 else (end + 2, end + 4)


def _pack(off, length):
    return (length << 20) | off


def _param(msg, fields, name, param):
    """(offset, length) of a parameter in a header, through the end of its line
    (the library includes the CR)."""
    for fname, v, e in fields:
        if fname == name:
            text = msg[v:e].lower()
            k = text.find(param.encode() + b"=")
            if k >= 0:
                o = v + k + len(param) + 1
                if msg[o:o + 1] == b'"':
                    o += 1
                    return o, msg.index(b'"', o) - o
                end = msg.find(b"\n", o)
                return o, end - o
    return None


def _field(fields, name):
    for fname, v, e in fields:
        if fname == name:
            return v, e
    return None


def _decoded_len(data, encoding):
    if encoding == "base64":
        return len(base64.b64decode(b"".join(data.split())))
    return len(data)


def parse_for_ctl(msg, own_number=None):
    """What the NWC24 library would put in a mailbox entry for msg. Offsets
    count from the start of the file, SMTP envelope included."""
    hdr_start = msg.index(b"DATA\r\n") + 6 if msg.startswith(b"MAIL FROM:") else 0
    hdr_end, body = _header_block(msg, hdr_start)
    fields = [[n, v + hdr_start, e + hdr_start] for n, v, e in _header_fields(msg[hdr_start:hdr_end])]
    e = {"flag": F_INIT, "header_length": body, "tag": 0, "wii_cmd": 0,
         "app_id": 0, "app_group": 0, "from_code": 0, "minutes": 0, "num_to": 0,
         "packed": {}, "parts": [], "message_offset": body, "encoded_length": 0, "message_length": 0}

    def text(name):
        r = _field(fields, name)
        return msg[r[0]:r[1]].decode("ascii", "replace") if r else None

    for name in ("from", "to", "subject"):
        r = _field(fields, name)
        if r:
            e["packed"][name] = _pack(r[0], r[1] - r[0])
    frm = text("from") or ""
    if frm.startswith("w") and frm[1:17].isdigit():
        e["flag"] |= F_WII | F_COMMIT_WII
        e["from_code"] = int(frm[1:17])
    else:
        e["flag"] |= F_PUBLIC
    e["num_to"] = len([a for a in (text("to") or "").split(",") if a.strip()])
    appid = text("x-wii-appid")
    if appid:
        kind, app, group = appid.split("-")
        k = int(kind)
        e["flag"] |= (F_APPTYPE_1 if k & 1 else 0) | (F_APPTYPE_2 if k & 2 else 0)
        e["app_id"] = int(app, 16)
        e["app_group"] = int(group, 16)
    if text("x-wii-cmd"):
        e["wii_cmd"] = int(text("x-wii-cmd"), 16)
    if text("x-wii-tag"):
        e["tag"] = int(text("x-wii-tag"), 16)
    if text("x-wii-mb-noreply") == "1":
        e["flag"] |= F_NOREPLY
    date = text("date")
    if date:
        t = datetime.datetime.strptime(" ".join(date.split()[:4]), "%d %b %Y %H:%M:%S")
        e["minutes"] = int((t - datetime.datetime(1900, 1, 1)).total_seconds() // 60)

    ctype = (text("content-type") or "text/plain").split(";")[0].strip().lower()
    if ctype.startswith("multipart/"):
        boundary = _param(msg, fields, "content-type", "boundary")
        bnd = b"--" + msg[boundary[0]:boundary[0] + boundary[1]]
        pos = msg.index(bnd, body)
        while True:
            line_end = msg.index(b"\r\n", pos)
            if msg[pos + len(bnd):pos + len(bnd) + 2] == b"--":
                break
            p_hdr = line_end + 2
            p_hdr_end, p_body = _header_block(msg, p_hdr)
            nxt = msg.index(bnd, p_body)
            p_fields = [[n, v + p_hdr, x + p_hdr] for n, v, x in _header_fields(msg[p_hdr:p_hdr_end])]
            body_end = nxt - 2 if msg[nxt - 2:nxt] == b"\r\n" else nxt
            e["parts"].append((p_fields, p_body, body_end))
            pos = nxt
    else:
        e["parts"].append((fields, body, len(msg)))

    attachments = []
    for p_fields, start, end in e["parts"]:
        r = _field(p_fields, "content-type")
        ptype = msg[r[0]:r[1]].decode().split(";")[0].strip().lower() if r else "text/plain"
        r = _field(p_fields, "content-transfer-encoding")
        enc = msg[r[0]:r[1]].decode().strip().lower() if r else "7bit"
        data = msg[start:end]
        while data.endswith(b"\r\n"):
            data = data[:-2]
        if ptype.startswith("text/") and "text" not in e:
            e["text"] = True
            cs = _param(msg, p_fields, "content-type", "charset")
            if cs:
                e["packed"]["charset"] = _pack(*cs)
            r = _field(p_fields, "content-transfer-encoding")
            if r:
                line_end = msg.find(b"\n", r[0])
                e["packed"]["encoding"] = _pack(r[0], line_end - r[0])
            e["message_offset"] = start
            e["encoded_length"] = len(data)
            e["message_length"] = _decoded_len(data, enc)
        else:
            attachments.append((start, len(data), _decoded_len(data, enc), MIME.get(ptype, 0x30000)))
    if attachments:
        e["flag"] |= F_ATTACHED
    e["attachments"] = attachments[:2]
    return e


def ctl_entry(msg_id, msg, e):
    buf = bytearray(0x80)
    struct.pack_into(">IIIIIIII", buf, 0, msg_id, e["flag"], len(msg), e["app_id"], e["header_length"],
                     e["tag"], e["wii_cmd"], 0)
    struct.pack_into(">QII", buf, 0x20, e["from_code"], e["minutes"], 0)
    struct.pack_into(">BBH", buf, 0x30, e["num_to"], len(e["attachments"]), e["app_group"])
    pk = e["packed"]
    struct.pack_into(">IIIII", buf, 0x34, pk.get("from", 0), pk.get("to", 0), pk.get("subject", 0),
                     pk.get("charset", 0), pk.get("encoding", 0))
    struct.pack_into(">II", buf, 0x48, e["message_offset"], e["encoded_length"])
    types = [0x10000, 0x10000]
    for i, (off, size, decoded, mime) in enumerate(e["attachments"]):
        struct.pack_into(">II", buf, 0x50 + i * 8, off, size)
        struct.pack_into(">I", buf, 0x60 + i * 4, decoded)
        types[i] = mime
    struct.pack_into(">II", buf, 0x68, *types)
    struct.pack_into(">III", buf, 0x70, e["message_length"], 0, 0x80000000)
    return bytes(buf)


def ctl_new(box, vff):
    buf = bytearray(box.ctl_size)
    struct.pack_into(">4sIIIIIIIII", buf, 0, b"WcTf", 4, 0, box.entries, 0, box.ctl_size, 1, 0x80, 0, box.vff_size)
    buf[0x58:0x77] = b"0" * 31
    return buf


def box_load(nand, box):
    ctl_path, mbx_path = nand_file(nand, box.ctl), nand_file(nand, box.mbx)
    mbx = open(mbx_path, "rb").read() if os.path.exists(mbx_path) else b""
    vff = Vff(mbx, box.vff_size)
    if os.path.exists(ctl_path):
        ctl = bytearray(open(ctl_path, "rb").read())
        if ctl[:4] != b"WcTf" or len(ctl) != box.ctl_size:
            raise ValueError("%s is not a mailbox index" % ctl_path)
    else:
        ctl = ctl_new(box, vff)
    return ctl, vff


def box_save(nand, box, ctl, vff):
    write_file(nand_file(nand, box.ctl), bytes(ctl))
    write_file(nand_file(nand, box.mbx), bytes(vff.buf))


def _slots(box):
    return range(0x80, 0x80 + box.entries * 0x80, 0x80)


def box_add(box, ctl, vff, msg, when=None):
    """Store msg like NWC24CommitMsg does. The receive box drops the SMTP
    envelope; the send box needs it (build letters with mail --envelope)."""
    if box is RECV and msg.startswith(b"MAIL FROM:"):
        msg = msg[msg.index(b"DATA\r\n") + 6:]
    if box is SEND and not msg.startswith(b"MAIL FROM:"):
        raise ValueError("outgoing letters need the MAIL FROM/RCPT TO/DATA envelope (mail --envelope)")
    (count,) = struct.unpack_from(">I", ctl, 0x08)
    (total,) = struct.unpack_from(">I", ctl, 0x10)
    next_id, next_off = struct.unpack_from(">II", ctl, 0x18)
    empty = [off for off in _slots(box) if not struct.unpack_from(">I", ctl, off)[0]]
    if not empty:
        raise ValueError("mailbox index is full")
    slot = next_off if next_off in empty else empty[0]
    msg_id = next_id
    e = parse_for_ctl(msg)
    e["flag"] |= box.flag
    ctl[slot:slot + 0x80] = ctl_entry(msg_id, msg, e)
    vff.write(box.msg_name(msg_id), ("%s%07dMSG" % (box.prefix.upper(), msg_id)).encode(),
              msg + bytes(-len(msg) % 32), when or datetime.datetime.now())
    rest = [off for off in empty if off != slot]
    struct.pack_into(">I", ctl, 0x08, count + 1)
    struct.pack_into(">I", ctl, 0x10, total + len(msg))
    struct.pack_into(">II", ctl, 0x18, msg_id + 1, rest[0] if rest else 0x80)
    if count == 0:
        struct.pack_into(">I", ctl, 0x20, msg_id)
    struct.pack_into(">I", ctl, 0x24, vff.free_bytes())
    return msg_id, e


def box_entries(box, ctl):
    out = []
    for off in _slots(box):
        f = struct.unpack_from(">IIIIIIIIQII", ctl, off)
        if f[0]:
            out.append({"slot": off, "id": f[0], "flag": f[1], "size": f[2], "app_id": f[3],
                        "from": f[8], "minutes": f[9], "attachments": ctl[off + 0x31]})
    return out


def box_clear(box, ctl, vff):
    for ent in box_entries(box, ctl):
        name = box.msg_name(ent["id"])
        if name in vff.files():
            vff.delete(name)
        ctl[ent["slot"]:ent["slot"] + 0x80] = bytes(0x80)
    struct.pack_into(">III", ctl, 0x08, 0, box.entries, 0)
    struct.pack_into(">I", ctl, 0x1C, 0x80)
    struct.pack_into(">II", ctl, 0x20, 0, vff.free_bytes())


def cmd_box(a):
    box = RECV if a.cmd == "inbox" else SEND
    if a.action == "init":
        vff = Vff(b"", box.vff_size)
        box_save(a.nand, box, ctl_new(box, vff), vff)
        print("created an empty %s box" % ("receive" if box is RECV else "send"))
        return 0
    ctl, vff = box_load(a.nand, box)
    if a.action == "add":
        for path in a.files:
            with open(path, "rb") as f:
                msg_id, e = box_add(box, ctl, vff, f.read())
            print("added %s as message %d (flag %08X, %d attachment(s))"
                  % (path, msg_id, e["flag"], len(e["attachments"])))
        box_save(a.nand, box, ctl, vff)
        return 0
    if a.action == "clear":
        box_clear(box, ctl, vff)
        box_save(a.nand, box, ctl, vff)
        print("emptied")
        return 0
    (count,) = struct.unpack_from(">I", ctl, 8)
    print("%d message(s); next ID %d" % (count, struct.unpack_from(">I", ctl, 0x18)[0]))
    for ent in box_entries(box, ctl):
        when = datetime.datetime(1900, 1, 1) + datetime.timedelta(minutes=ent["minutes"])
        msg = vff.read(box.msg_name(ent["id"]))[:ent["size"]]
        e = parse_for_ctl(msg)
        to = e["packed"].get("to", 0)
        print("%4d  %s  from %s  to %s  flag %08X  %d B  %d att" % (
            ent["id"], when.strftime("%Y-%m-%d %H:%M"), pretty("%016d" % ent["from"]) if ent["from"] else "-",
            msg[to & 0xFFFFF:(to & 0xFFFFF) + (to >> 20)].decode(), ent["flag"], ent["size"], ent["attachments"]))
        if a.dump:
            write_file(os.path.join(a.dump, box.msg_name(ent["id"])), msg)
    return 0


def box_register(sub):
    for name, what in (("inbox", "receive box (letters that arrived on this Wii)"),
                       ("outbox", "send box (letters KD posts to the mail server)")):
        s = sub.add_parser(name, help="add letters to, list or empty the " + what)
        s.add_argument("action", choices=["add", "list", "clear", "init"])
        s.add_argument("files", nargs="*", help="letters from 'mail' to add")
        s.add_argument("--nand", required=True, help="Dolphin NAND root (the folder holding shared2)")
        s.add_argument("--dump", help="with list: save each message into this folder")
        s.set_defaults(fn=cmd_box)


if __name__ == "__main__":
    sys.exit(main())
