#!/usr/bin/env python3
"""A stand-in WiiConnect24 mail server for testing the send box.

Point the Wii's send URL at it (nwc24.py config --server http://127.0.0.1:8099)
and every letter KD posts to /cgi-bin/send.cgi is saved to --out as
<time>-m<ID>.eml and answered with cd=100, so KD removes it from the send box.
Other endpoints answer an empty, successful reply.

  python3 sendcgi.py --port 8099 --out ./sent
"""

import argparse
import datetime
import email.parser
import email.policy
import http.server
import os


def parse_form(content_type, body):
    """Fields of a multipart/form-data body as {name: bytes}."""
    msg = email.parser.BytesParser(policy=email.policy.HTTP).parsebytes(
        b"Content-Type: " + content_type.encode() + b"\r\n\r\n" + body)
    out = {}
    for part in msg.iter_parts():
        name = part.get_param("name", header="content-disposition")
        if name:
            out[name] = part.get_payload(decode=True) or b""
    return out


class Handler(http.server.BaseHTTPRequestHandler):
    out = "."

    def reply(self, text):
        data = text.encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_POST(self):
        body = self.rfile.read(int(self.headers.get("Content-Length", 0)))
        if not self.path.endswith("/send.cgi"):
            self.reply("cd=100\nmsg=Success.\n")
            return
        form = parse_form(self.headers.get("Content-Type", ""), body)
        auth = form.pop("mlid", b"").decode("ascii", "replace").split("\r\n")[0]
        stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
        lines = ["cd=100", "msg=Success."]
        for name, mail in form.items():
            path = os.path.join(self.out, "%s-%s.eml" % (stamp, name))
            with open(path, "wb") as f:
                f.write(mail)
            print("%s: %s, %d bytes -> %s" % (auth, name, len(mail), path), flush=True)
            n = name[1:]
            lines += ["cd%s=100" % n, "msg%s=Success." % n]
        self.reply("\n".join(lines) + "\n")

    do_GET = do_POST


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--port", type=int, default=8099)
    p.add_argument("--out", default="sent")
    a = p.parse_args()
    os.makedirs(a.out, exist_ok=True)
    Handler.out = a.out
    print("send.cgi on http://127.0.0.1:%d/cgi-bin/send.cgi, saving to %s" % (a.port, a.out), flush=True)
    http.server.ThreadingHTTPServer(("127.0.0.1", a.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
