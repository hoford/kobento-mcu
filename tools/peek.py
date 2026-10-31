#!/usr/bin/env python3
"""Read-only serial capture that does NOT reset the board (no DTR/RTS toggle).
usage: tools/peek.py PORT [seconds]"""
import os, termios, time, sys
fd = os.open(sys.argv[1], os.O_RDONLY | os.O_NOCTTY | os.O_NONBLOCK)
a = termios.tcgetattr(fd)
a[0] = a[1] = a[3] = 0                    # raw; UART ports (Core2) at 115200, USB-JTAG ignores it
a[2] = (a[2] & ~(termios.HUPCL | termios.CSIZE | termios.PARENB | termios.CRTSCTS)) | termios.CS8 | termios.CREAD | termios.CLOCAL
a[4] = a[5] = termios.B115200
termios.tcsetattr(fd, termios.TCSANOW, a)
end = time.monotonic() + float(sys.argv[2] if len(sys.argv) > 2 else 10)
while time.monotonic() < end:
    try:
        c = os.read(fd, 4096)
        if c: sys.stdout.write(c.decode(errors="replace")); sys.stdout.flush()
    except BlockingIOError: time.sleep(0.05)
