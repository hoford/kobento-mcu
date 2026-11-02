#!/usr/bin/env python3
"""A screenshot: the frame the player last drew (sFrame, 480 x 480 ARGB8888), read over the
debugger with probe-rs while the board runs, saved as PNG.

  tools/snap.py out.png [--elf build/rcplayer.elf]
"""
import argparse, os, re, shutil, struct, subprocess, sys, zlib

ap = argparse.ArgumentParser()
ap.add_argument("out")
ap.add_argument("--elf", default=os.path.join(os.path.dirname(__file__), "../build/rcplayer.elf"))
ap.add_argument("--chip", default="STM32U5A9NJ")
a = ap.parse_args()
W = H = 480
# arm-none-eabi-nm from the toolchain: $ARM_GCC_BIN / $ARM_GCC if set, else PATH, else the
# convenience default (adjust to where you unpacked the Arm GNU toolchain).
_arm_bin = os.environ.get("ARM_GCC_BIN") or os.environ.get("ARM_GCC")
if _arm_bin:
    nm = os.path.join(_arm_bin, "arm-none-eabi-nm")
elif shutil.which("arm-none-eabi-nm"):
    nm = "arm-none-eabi-nm"
else:
    nm = os.path.expanduser("~/tools/arm-gnu-toolchain-15.2.rel1-darwin-arm64-arm-none-eabi/bin/arm-none-eabi-nm")
sym = subprocess.run([nm, "-C", a.elf], capture_output=True, text=True).stdout
m = re.search(r"^([0-9a-f]+) [bBdD] sFrame$", sym, re.M)
if not m: sys.exit("sFrame not in " + a.elf)
addr = int(m.group(1), 16)
term = os.path.join(os.path.dirname(__file__), "term.py")
# Hold the player so the frame isn't half drawn, read it, and let it run again.
subprocess.run([sys.executable, term, "--cmd", "hold", "--seconds", "0.5"], capture_output=True)
r = subprocess.run(["probe-rs", "read", "--chip", a.chip, "b32", hex(addr), str(W * H)],
                   capture_output=True, text=True)
subprocess.run([sys.executable, term, "--cmd", "run", "--seconds", "0.1"], capture_output=True)
if r.returncode: sys.exit(r.stderr)
words = [int(w, 16) for w in r.stdout.split() if not w.endswith(":")]
raw = bytearray()
for y in range(H):
    raw.append(0)
    for p in words[y * W:(y + 1) * W]:
        raw += bytes(((p >> 16) & 255, (p >> 8) & 255, p & 255))
def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)) \
    + chunk(b"IDAT", zlib.compress(bytes(raw), 6)) + chunk(b"IEND", b"")
open(a.out, "wb").write(png)
print("saved", a.out)
