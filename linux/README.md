# Kobento RC player — Linux (Raspberry Pi and desktop)

A display-only RemoteCompose player for full-Linux targets: the Raspberry Pi (including the Pi
Zero) and any desktop. It runs the **same `rccore` + `kobentovg` (ThorVG) engine** as the
ESP32 and STM32 players, and is structured like the reference `rcviewer`: a **resizable desktop
window** (SDL2), a player loop, keyboard navigation, and a stdin console.

A Raspberry Pi is a Linux board, not a microcontroller, so nothing here touches ESP-IDF or
`rcstage`. It draws into a normal window on the desktop (Wayland/PIXEL or X11) via SDL2 — it does
**not** write the raw framebuffer, so the desktop is left running and usable. Dependencies are
just **zlib**, **SDL2**, and a C++17 compiler with threads.

> **Why ThorVG and not Skia here?** The reference `rcviewer` renders with Skia, but Skia is only
> distributed prebuilt for `linux-arm64`/`x64` — there is no build for 32-bit ARMv6 (Pi Zero /
> Zero W), and Skia is impractical to compile on that hardware. ThorVG builds natively on ARMv6,
> so this player uses the ThorVG backend. On a 64-bit Pi you can also build the Skia `rcviewer`
> from the engine repo directly.

## Build

On the target (or any Linux/macOS box; without SDL2 it builds headless-only):

```sh
sudo apt install -y cmake ninja-build zlib1g-dev libsdl2-dev     # Debian/Raspberry Pi OS
cmake -S linux -B linux/build -G Ninja
cmake --build linux/build
linux/build/rcplayer documents                                   # play every .rc under documents/
```

On a Pi Zero (single-core ARMv6) the first build is slow (~30–40 min for ThorVG + rccore) but
needs no special setup. The engine comes from the sibling `remotecompose-experiments` checkout;
override with `-DRC_EXPERIMENTS=/path/to/remotecompose-experiments` if it lives elsewhere.

Cross-compiling is possible with `cmake/rpi-cross.cmake` (a Linux glibc cross toolchain, not the
STM32 bare-metal one) — see that file — but native-on-Pi is simplest for a one-off.

## Running

```sh
linux/build/rcplayer documents                 # a window on the desktop
linux/build/rcplayer path/to/examples          # scans recursively; fit/ and 3d/ laid out to fit
linux/build/rcplayer ~/examples --pi            # fullscreen kiosk, tuned for the Pi Zero
linux/build/rcplayer ~/examples --auto 7        # auto-advance every 7s (a slideshow)
linux/build/rcplayer ~/examples --square --list # a square player with the document list beside it
linux/build/rcplayer documents --once           # render the first doc to rcplayer-once.png, exit
```

Options (full list in `src/main.cpp`): `--size W H`, `--fullscreen`, `--pi`, `--square`, `--list`,
`--render-scale N`,
`--auto SEC`, `--density D`, `--theme light|dark`, `--round`, `--eink`, `--board NAME`,
`--named NAME=VALUE`, `--start NAME`, `--fps N`, `--headless`.

If SDL2 can't open a window (no desktop session), it falls back to **headless** — every feature
works and `snap` writes the current frame to a PNG (how it is smoke-tested in CI).

### Square player and document list

`--square` keeps the player square: the largest square that fits, centred on black. `--list` puts
a scrolling list of the documents on the right and pushes the player to the left: tap a row to open
it, drag or use the wheel to scroll, and the current document stays highlighted. Together they give
a square player at the left and a list in all the width that's left. In a window, `--size` is the
player's size, and the list widens the window. With `--list`, `snap` writes the whole window.

### Starting on boot

`scripts/kobento-kiosk.desktop` starts `scripts/kobento-kiosk.sh` when the desktop session logs in.
Edit the paths in its `Exec`, then copy it to `~/.config/autostart/`; the Pi must auto-login to the
desktop. A crash restarts the player, while `Q`/`Esc` and `pkill rcplayer` stop it until the next
boot. The log is `~/kiosk.log`.

### Fullscreen and Pi performance

`--fullscreen` (or the `F`/`F11` key) gives **borderless-desktop fullscreen** — it fills the
screen but stays on the compositor, so the PIXEL desktop keeps running underneath. The cursor is
hidden while fullscreen.

The player prefers a **GPU renderer** (VideoCore, with vsync) and falls back to software; it
reports which in its banner (`gpu` / `software`). The real cost on the Pi Zero is ThorVG's CPU
rasterizer at full panel resolution, so `--render-scale N` rasterizes at **1/N** of the window and
lets the GPU upscale — e.g. `--render-scale 2` is ~4× fewer pixels to rasterize. `--pi` bundles the
kiosk preset: `--fullscreen` + `--render-scale 2` + nearest-neighbour upscale. Tune the scale per
document (`res DIV` at runtime, or `--render-scale`); 1 is crispest, 2–3 is smooth on the Zero.

> Note: the reference `rcviewer` has no "Pi mode"; `--pi` here is our own preset. A faster path
> still would be direct KMSDRM/GBM rendering, but that takes over the display and tears down the
> PIXEL desktop, so this player stays windowed on the compositor by design.

### Showing it on a remote Pi's desktop over SSH

The window must attach to the running desktop session. For a Wayland desktop (PIXEL/`labwc`):

```sh
ssh pi@host
XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0 DISPLAY=:0 \
  ~/.../rcplayer ~/examples --pi
```

`DISPLAY=:0` lets SDL fall back to X11 via XWayland if the native Wayland path isn't used.

## Controls

**Window keys** (like `rcviewer`): `Left`/`Right` or `P`/`N` previous/next · `Space` pause ·
`R` reload · `S` snap · `F`/`F11` fullscreen · `Q`/`Esc` quit. **Click/drag** (mouse) or **touch**
drives the document — taps hit click areas (buttons), drags scroll, and a flick flings. Resizing
the window re-lays-out the document.

**Console** (stdin, one command per line — the same vocabulary as the ESP32 player):

```
list            list documents (* = current, "(fit)" = scaled-to-fit)
next / prev     step through documents
doc N           open document N
stats           render size, last-frame time, fps, paint count
bench [N]       time N raster passes of the current document (default 30)
act NAME[=VALUE]  host action: doc.next · doc.back · doc.show=NAME · system.backlight=…
named [PREFIX]  the current document's named variables and their values
snap [FILE]     write the current frame to a PNG
res DIV         render at 1/DIV of the window and scale up (DIV 1 = native)
help · quit
```

Documents drive the player through `hostAction(0, "name[=value]")` as on the device
(`doc.next`, `doc.back`, `doc.show=NAME`, `system.backlight`). `system.backlight` has no panel to
dim on a generic host, so it only updates the named variable.

## Not yet here

Panel backlight and the device-state system variables (battery/memory/network) aren't wired up —
but the engine, windowing, touch/mouse input, fullscreen, host actions, and the full
document/console feature set are. Networked delivery lives in **kobento-net**; integrations in
**kobento-mcuApps**.
