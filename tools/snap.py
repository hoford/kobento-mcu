#!/usr/bin/env python3
"""Grab a screenshot (or send a command) to an rcplayer or muse-gadget build over USB serial.

  tools/snap.py [--port P] out.png        # send 'snap', save the frame as PNG
  tools/snap.py [--port P] --cmd next     # send any console command, print the reply
  tools/snap.py --prefix '>' out.png       # the same against firmware/muse-gadget

The port is opened with raw os.open + termios and the modem lines are never touched:
pyserial's open() briefly drives RTS high with DTR low, which the P4's USB-Serial-JTAG (and
the auto-reset circuit behind a UART bridge, as on the Core2) treats as a reset request.
UART ports are set to raw 8N1 at --baud; USB-Serial-JTAG ignores the rate. Needs Pillow for
PNG output.
"""
import argparse, base64, glob, os, struct, sys, termios, time

ap = argparse.ArgumentParser()
ap.add_argument("out", nargs="?")
ap.add_argument("--port", default=None)
ap.add_argument("--cmd", default=None)
ap.add_argument("--prefix", default="", help="prepended to commands; '>' for the Muse build (firmware/muse-gadget)")
ap.add_argument("--wait", type=float, default=2.0, help="seconds to collect a --cmd reply")
ap.add_argument("--baud", type=int, default=115200, help="UART ports (the Core2's bridge)")
a = ap.parse_args()
port = a.port or (sorted(glob.glob("/dev/cu.usbmodem*")) + sorted(glob.glob("/dev/cu.usbserial*"))
                  or [None])[0]
if not port: sys.exit("no /dev/cu.usbmodem* or /dev/cu.usbserial* port")
BAUD = getattr(termios, "B%d" % a.baud)

class Port:
    def __init__(self, path):
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        t = termios.tcgetattr(self.fd)
        t[0] = 0                          # raw 8N1, no flow control
        t[1] = 0
        t[2] = (t[2] & ~(termios.HUPCL | termios.CSIZE | termios.PARENB | termios.CSTOPB
                         | termios.CRTSCTS)) | termios.CS8 | termios.CREAD | termios.CLOCAL
        t[3] &= ~(termios.ICANON | termios.ECHO | termios.ISIG | termios.IEXTEN)
        t[4] = t[5] = BAUD
        termios.tcsetattr(self.fd, termios.TCSANOW, t)
        self.buf = b""
        while self._read(): pass          # drain stale input
    def _read(self):
        try: return os.read(self.fd, 65536)
        except BlockingIOError: return b""
    def write(self, b): os.write(self.fd, b)
    def readline(self, timeout=0.5):
        end = time.time() + timeout
        while b"\n" not in self.buf and time.time() < end:
            chunk = self._read()
            if chunk: self.buf += chunk
            else: time.sleep(0.005)
        if b"\n" in self.buf:
            line, self.buf = self.buf.split(b"\n", 1)
            return line + b"\n"
        line, self.buf = self.buf, b""
        return line

s = Port(port)

if a.cmd:
    s.write((a.prefix + a.cmd + "\n").encode())
    end = time.time() + a.wait
    while time.time() < end:
        line = s.readline()
        if line: sys.stdout.write(line.decode(errors="replace"))
    sys.exit(0)

s.write((a.prefix + "snap\n").encode())
w = h = None; rows = []
deadline = time.time() + 60
while time.time() < deadline:
    line = s.readline().decode(errors="replace").strip()
    if not line: continue
    if line.startswith("SNAP BEGIN"):
        _, _, w, h = line.split(); w, h = int(w), int(h); rows = []
    elif line == "SNAP END":
        break
    elif w is not None:
        try: rows.append(base64.b64decode(line))
        except Exception: pass   # an interleaved log line
else:
    sys.exit("timed out waiting for SNAP END")

from PIL import Image
img = Image.new("RGB", (w, h))
px = img.load()
for y, r in enumerate(rows[:h]):
    for x in range(min(w, len(r) // 2)):
        v = struct.unpack_from("<H", r, x * 2)[0]
        px[x, y] = (((v >> 11) & 31) << 3, ((v >> 5) & 63) << 2, (v & 31) << 3)
out = a.out or "snap.png"
img.save(out)
print(f"{out}: {w}x{h}, {len(rows)} rows")
