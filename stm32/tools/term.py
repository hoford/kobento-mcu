#!/usr/bin/env python3
"""The board's log, from the ST-LINK's virtual COM port (115200 8N1), and console commands.

  tools/term.py --seconds 20             # print the log for 20 s
  tools/term.py --cmd "list" --seconds 2 # send a command, print what follows
"""
import argparse, glob, os, sys, termios, time

ap = argparse.ArgumentParser()
ap.add_argument("--port", default=None)
ap.add_argument("--cmd", action="append", default=[])
ap.add_argument("--seconds", type=float, default=5)
ap.add_argument("--out", default=None, help="also append the log to this file")
a = ap.parse_args()
port = a.port or (sorted(glob.glob("/dev/cu.usbmodem*")) or [None])[0]
if not port: sys.exit("no /dev/cu.usbmodem* port")
fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
t = termios.tcgetattr(fd)
t[0] = 0; t[1] = 0
t[2] = (t[2] & ~(termios.CSIZE | termios.PARENB | termios.CSTOPB | termios.CRTSCTS)) | termios.CS8 | termios.CREAD | termios.CLOCAL
t[3] &= ~(termios.ICANON | termios.ECHO | termios.ISIG | termios.IEXTEN)
t[4] = t[5] = termios.B115200
termios.tcsetattr(fd, termios.TCSANOW, t)
for c in a.cmd:
    os.write(fd, (c + "\n").encode()); time.sleep(0.2)
out = open(a.out, "ab") if a.out else None
end = time.time() + a.seconds
while time.time() < end:
    try: b = os.read(fd, 4096)
    except BlockingIOError: b = b""
    if b:
        sys.stdout.write(b.decode(errors="replace").replace("\r", "")); sys.stdout.flush()
        if out: out.write(b); out.flush()
    else: time.sleep(0.02)
