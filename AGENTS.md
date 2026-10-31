# AGENTS.md: getting up to speed on kobento-mcu

This is for an agent (or person) picking up this repository cold. Read it before changing
anything. README.md has the family overview and the layout. `esp32/README.md` has the board
table and the ESP32 user-facing commands; `stm32/README.md` covers the STM32 side. Each
`esp32/boards/<board>/README.md` has that board's hardware facts. The `docs/` directory holds
the RC conventions under review.

## What this is

**Kobento** is an implementation of the **RemoteCompose** (RC) spec defined by Android/androidx.
RemoteCompose is the spec and the `.rc` format; Kobento is the implementation brand (see
README.md). This repo, **kobento-mcu**, renders RC documents on microcontrollers: **ESP32** and
**STM32U5**. The players are:
- `esp32/firmware/rcplayer`, a standalone ESP32 player with Wi-Fi.
- `esp32/firmware/muse-gadget`, the Muse Gadget SDK app with an RC screen, on the Tab5 and the
  PaperS3.
  The SDK owns Wi-Fi there. The Wi-Fi screen asks it to join through
  `gadget_ext_wifi_join()` (in `../muse-gadget-sdk`); see the Wi-Fi bullet below.
- `stm32/`, a bare-metal STM32U5A9J-DK player (single-threaded, no RTOS).

The engine is the C++ `rccore` (built from `../remotecompose-experiments`, the future
kobento-cpp), rendered with **KobentoVG** — our ThorVG fork and its `ThorvgPaintContext`
backend (`components/kobentovg`, namespace `kobentovg`). Device UI (the start-up screen and the
Wi-Fi list) is written as RC documents; the firmware does the work behind it. That is a
deliberate choice by the owner: **new device UI should be an RC document, not C++ drawing.**

The androidx Java implementation is the source of truth for RC. Conventions this repo
invents (host actions, system variables, screen variants) are drafts in `docs/`, for the
owner to review, adjust and then propose upstream.

### Repository layout (read this before following paths below)

Shared code is at the top level (`components/`, `third_party/`, `documents/`, `assets/`,
`docs/`, `tools/`); the ESP32 tree lives under **`esp32/`** (`boards/`, `firmware/`, `host/`,
`cmake/`) and the STM32 tree under **`stm32/`**. **Most instructions below were written for the
ESP32 tree and use paths relative to `esp32/`** (e.g. `firmware/rcplayer`, `host/build/rc2png`,
`boards/<board>`). Run them from `esp32/`, or prepend `esp32/`. Paths to shared dirs
(`tools/`, `documents/`, `components/`) are repo-root-relative and unchanged. *(This guide has
not yet had a full per-line path pass for the monorepo move — treat ESP32 paths as
`esp32/`-relative.)*

## Sibling repositories (expected next to this one)

| Path | What | Notes |
|---|---|---|
| `../remotecompose-experiments` | `players/cpp/lib/rccore`, the engine (future kobento-cpp) | Built through `components/rccore`. **Owned by other agents:** report engine bugs (below), don't change it from here. |
| `../muse-gadget-sdk` | Muse SDK, branch `tab5-p4-hosted` | Only for `esp32/firmware/muse-gadget`. |
| `../STM32CubeU5` | ST's STM32CubeU5 HAL + BSP | Only for the `stm32/` build. |

## Toolchain (the original machine: macOS)

- ESP-IDF v6.0.1 at `~/esp/esp-idf-v6`. Run `. ~/esp/esp-idf-v6/export.sh` in the same shell
  command as `idf.py`, because shell state doesn't persist between tool calls.
- The IDF Python, which has esptool and pyserial, is at
  `~/.espressif/python_env/idf6.0_py3.14_env/bin/python`.
- The shell is zsh: quote arrays as `"${arr[@]}"`, and use `$=var` for word splitting.
- The host renderer is built with `cmake -S host -B host/build -G Ninja && ninja -C host/build`.
  - Run it as `host/build/rc2png in.rc out.png --fit W H --density D`.
  - `--named NAME=VALUE` and `--sys ID=VALUE` fill in variables.
  - `--click X,Y` (dp) runs a tap and prints the host actions it raises. This is the fastest
    way to test a document's buttons without hardware.

## Build, flash, talk

```sh
cd firmware/rcplayer
idf.py -B build/<board> -DBOARD=<board> build          # <board> = a directory under boards/
idf.py -B build/<board> -DBOARD=<board> -p PORT app-flash   # app only: keeps NVS (saved Wi-Fi)
```

- **Board selection:**
  - `cmake/board.cmake` picks the board.
  - Each board gets its own build directory and its own sdkconfig, kept in the build
    directory.
  - The defaults are layered: board `sdkconfig.defaults`, then the app's, then
    `boards/<b>/sdkconfig.<app>.defaults`, then the gitignored `<app>/sdkconfig.secrets`.
  - **After changing any sdkconfig.defaults, delete `build/<board>/sdkconfig`,** or the old
    values stay.
- **Talking to a running device:** use `tools/snap.py` and `tools/peek.py`, not `idf.py
  monitor` or pyserial.
  - Opening the port with pyserial toggles DTR/RTS and resets the chip.
  - The tools use raw `os.open` plus termios.
  - `snap.py --cmd "..." --wait N` sends a console line and prints the reply.
    `snap.py out.png` takes a screenshot.
- **rcplayer console:**
  - documents: `list`, `doc N`, `next`, `prev`
  - screenshots and timing: `snap`, `stats`, `bench`, `res full|half`
  - Wi-Fi: `wifi add SSID PASS`, `wifi join SSID`, `wifi forget SSID`, `wifi list`,
    `wifi SSID [PASS]` (save and join)
  - network: `net`, `scan`, `ble`
  - **`act NAME[=VALUE]`** runs a host action exactly as a tap would, so `act doc.show=wifi_settings`
    tests UI flows without touching the screen.
  - Each board adds diagnostics (`touch`, `i2c`, `power`, `epd ...`, ...).
- **Ports move.** List them with `ls /dev/cu.usbmodem*`. With two boards plugged in, give
  `tools/update-muse.sh --port PORT`. To tell them apart, `snap.py --port P --prefix '>'
  --cmd sys` prints the battery: the Tab5's 2S pack reads about 8 V, the PaperS3's about 4 V.

## Rules the owner has set (keep to them)

- **The Tab5 is paired with Muse: confirm with the owner before every install on it.**
  - Install only with `tools/update-muse.sh`, which writes the app partition only.
  - Never `erase-flash` it, and never flash rcplayer onto it. rcplayer's layout overwrites the
    Muse NVS (the pairing).
- **The PaperS3 runs the Muse gadget too (paired 2026-10-19).** Update it with
  `tools/update-muse.sh --board m5stack_papers3` (app only); flashing rcplayer or erasing it
  would lose the pairing.
- **The Core2, the Waveshare round board and the CrowPanel 5.79" (e-paper, since 2026-10-23) are dev boards:** flashing them freely is
  fine.
  - Prefer `app-flash` so the saved Wi-Fi networks in NVS survive.
  - Factory images are in `backups/` (gitignored); each board README says how to restore.
- **Secrets:**
  - The Muse SDK token and the dev Wi-Fi credentials are in
    `firmware/muse-gadget/sdkconfig.secrets` (gitignored). Never print the token, and never
    commit either.
  - The dev network, NIXIOT, is the owner's test network for IoT, Muse and RC devices: its
    password showing up in a log is not a concern.
  - To give a board Wi-Fi, a script reads the file and writes `wifi add SSID PASS` to the port
    with raw `os.open`, masking the password in anything it prints.
- **Commit or push only when the owner asks.** End commit messages with the
  `Co-Authored-By` line your harness specifies.
- **Every new host action or device-UI convention goes into `docs/RC_ACTION_PATTERNS.md`**
  (variables into `docs/RC_SYSTEM_VARIABLES.md`) for the owner's review.

## Architecture in one page

- **`board/board.h`** is the only path to hardware.
  - Every board implements all of it:
    - display init and present
    - touch
    - backlight
    - power readings
    - console hook
    - constants `kWidth/kHeight/kDensity/kRound/kEink/kName/kDisplayName`
  - Copy an existing board's header when adding one. Documents lay out in dp = px /
    `kDensity`: 360x640 dp on the Tab5 and the PaperS3, 320x240 on the Core2, 240x240 on the
    round board, 245x299 on the AMOLED board.
- **`components/rcstage`** puts one document on screen.
  - Its player task does three things:
    - polls touch: touchDown/Drag/Up, onClick, a 1.5 s long press → `Hooks::longPress`;
    - renders only when something is dirty or the engine's repaint delay is due;
    - hands the ARGB frame to `board::presentAsync`.
  - Actions a document raises during a click are **queued and run after the touch
    handling** (`sRaised`): an action that loads another document inside onClick was a
    use-after-free.
  - `device_state.cpp` samples power, network and memory once a second into system and
    named variables.
    It also sets the clock: SNTP (`CONFIG_RCSTAGE_NTP_SERVER`, pool.ntp.org) starts once
    Wi-Fi has an address, and the time zone is `CONFIG_RCSTAGE_TIMEZONE` (US Pacific). The
    SDK and rcplayer don't set the time themselves. The log says `clock set: ...`.
- **`firmware/rcplayer/main/main.cpp`** owns the host side:
  - It handles the host actions `doc.show/next/back`, `system.backlight`, and
    `wifi.join/scan/page`.
  - It publishes the Wi-Fi list (`publishWifi`) and returns from the Wi-Fi screen after a
    join.
  - Hardware buttons step through the documents: `board::buttonPressed()` returns +1
    (forward, like a long press), -1 (back) or 0. The Core2's side power key is +1; on the
    AMOLED board PWR is +1 and BOOT -1. Boards without buttons return 0.
  - It sets `system.muse` to 0, so the start-up screen shows Remote Compose alone (the Muse
    build sets 1). The Core2 is the owner's RC demo board: no Muse on it.
  - It picks document variants: `<name>_round` on round boards and `<name>_light` on e-ink,
    wherever `<name>` is asked for. The long-press cycle shows each document once, in this
    board's variant (`otherScreen`).
- **`firmware/rcplayer/main/net.cpp`** is the Wi-Fi manager.
  - It keeps up to 8 saved networks in NVS (namespace `wifi`, blob `nets`).
  - A `wifi_mgr` task works through an op queue. It auto-joins in this order: the last
    network (if seen), then networks with passwords, then open ones. It falls back after
    4 tries, and rescans every 30 s while unjoined.
  - `view()` is what the Wi-Fi screen shows, using states 1–7 (see make_wifi.py).
- **`firmware/muse-gadget/components/muse_rc/muse_rc.cpp`** is the Muse player's host side.
  - It has the same document actions and variants as rcplayer, and the same Wi-Fi screen
    rows, but the SDK does the Wi-Fi work:
    - it scans with `wifi_mgr_scan`;
    - a pick goes to `gadget_ext_wifi_join`, which joins, saves the network first in the
      SDK's list (`wifi_known`), and restarts the agent session from the heartbeat.
  - At boot the SDK tries the dev network (`CONFIG_HOMEHUB_WIFI_SSID`), then the saved
    ones. If none joins, the status goes to ERROR with Wi-Fi down, and muse_rc opens the
    Wi-Fi screen by itself (not over the launcher). Opened by itself, it goes back to what
    was showing, by the document's bytes (`sBackBytes`, so a downloaded pin works), once any
    network joins. Banners are hidden while that screen or the launcher is up.
  - Start sequence (2026-10-21, owner's design): after a restart or the power button (not a
    timed wake) `sStart` is Waiting: the splash stays up while Wi-Fi joins. Joined (and the
    pin load is in, `sPinResolved`): the pinned document (`sHeldPin`). No Wi-Fi within 60 s
    (`kStartWaitUs`), the SDK's ERROR, or any touch on the splash: the launcher (the touch
    is swallowed, `rcstage::swallowTouch`, so its release doesn't click the launcher row
    under it; a long press's release no longer clicks either). The PaperS3's GT911
    calibrates away a finger held through power-up, so holding while pressing the button
    does nothing: lift and tap. Unpaired boards stay
    on the splash (the pairing banner). An agent's `rc.show` during Waiting ends it.
  - The launcher (`launcher` / `launcher_light`, 16 row slots, `publishLauncher`): Wi-Fi,
    the pinned document, then each built-in one (not system docs, not other screens'
    variants). Long press opens it at any time (it used to step to the next document).
    `launch.back` and the pinned row reload the pin from flash (`showPinTask`, internal
    stack) so a newer `rc.pin` and its persisted values are what shows. Pins reach the
    player task as a `PinMail`.
  - Roaming (`roam()`, on the `rc_wifi` task, started from `pinTask`): the SDK without
    `CONFIG_MUSE_ENABLED` has no Wi-Fi keeper, so after a drop wifi_mgr only retries the
    lost network. After 30 s down, roam scans and calls `gadget_ext_wifi_join` on the
    strongest saved network in range (backoff 30 s to 5 min). `device.wifi` adds, forgets
    and joins saved networks (so password networks can be saved by the agent).
  - Trap (hit 2026-10-21): `config_setup_complete()` reads NVS, so the player task (PSRAM
    stack on the S3) mustn't call it; `sSetupDone` caches it from `gadget_ext_status`.
  - Open networks and saved ones can be joined; there's no on-device password entry.
- **`firmware/muse-gadget/components/muse_rc/commands.cpp`** holds the Muse commands
  (`rc.*`, `device.time/beep/motion`, `display.refresh`, and the `device.health` extras
  through the SDK hook `gadget_ext_health`).
  - **`docs/MUSE_COMMANDS.md` documents every one: keep it in step with the code.**
  - Test them without Muse using `tools/muse_api.py <command> '<json>'`. It runs the console
    line `>api ...` through the same code and prints the result; PNGs are saved.
  - `link.register` must fit in 8 KB. Ours use about 2.9 KB (`muse_api.py register`), so
    keep descriptions short.
  - Pinned documents: NVS namespace `rcpin`. Downloaded ones go in a 4 MB flash region at
    12 MB, above the partition table, registered at run time, so the app-only update still
    works.
- **Documents:**
  - `documents/*.rc` and `documents/system/*.rc` are embedded by globbing (both players). `system/` holds
    the documents that need this player's actions. `documents/3d` is embedded only with
    `CONFIG_KOBENTOVG_3D`.

## Making or changing device UI

- **The generators** are Python scripts in `tools/splash/`:
  - `make_splash.py` builds the start-up screen, one document for every shape. It picks a
    layout inside the document from the window aspect ratio, `system.screen_round` and
    `system.screen_eink`.
  - `make_wifi.py` builds `wifi_settings`, `wifi_settings_light` and `wifi_settings_round`.
  - `tools/dashboard/make_dashboard.py` builds `dashboard`, a template for agents (clock,
    weather, stock); its README lists the authoring rules (text size, glyphs, time).
  - They emit JSON, and `tools/splash/compile.mjs` (json2rc, vendored from
    RemoteCompose-Examples) compiles it to `.rc`. Run the script; it writes the .rc files
    itself.
- **Check a change on the Mac first:** `rc2png` with `--named` values for every variable the
  document reads, then `--click` for every button. When a refactor shouldn't change a
  document, confirm the output `.rc` is byte-identical with `git diff --stat`.
- **Patterns** (details in `docs/RC_ACTION_PATTERNS.md`):
  - one action string `hostAction(0, "ns.verb=value")`, and results come back as named
    variables, never as a reply;
  - lists are fixed rows `system.wifi_<i>_*`, and a tap sends the row index, not the item;
  - layouts that differ in tap targets or background colours are separate variant
    documents. RC layout components can't be switched by a condition; canvas drawing can.

## When a document looks wrong on a device

1. **Capture it:** on the Muse build, `tools/getdoc.py out.rc` saves the document being
   shown (console `>doc`). The log also records each `rc.show_url` with its URL.
2. **Render it off the device:**
   - `host/build/rc2png out.rc a.png --fit 720 1280 --density 2` draws it at Tab5 scale.
   - The desktop viewer, `../remotecompose-experiments/players/cpp/build/apps/viewer/rcviewer
     --screenshot out.rc b.png 720 1280`, draws at density 1.
   - Both use the boards' engine (rccore), so agreement means the player is consistent, not
     that it's right.
3. **Read the bytes:** `rc2json rc2json out.rc out.json` (built in the same tree) decodes
   it. This is the engine-independent check. For example, `tab5_dashboard_v3.rc` (2026-10-19)
   had small text because no `PAINT_VALUES` set `TEXT_SIZE` (tag 1): the agent's writer had
   dropped it, and no player could have drawn it bigger.

## The ThorVG backend (components/kobentovg) against Skia

The boards draw with ThorVG; the desktop players with Skia (`../remotecompose-experiments/
players/cpp/lib/rcskia`). Colour filters (SkColorFilters::Blend semantics, every blend mode)
and JPEG images work since 2026-10-19; `host/build/paintcheck out.png [photo.jpg]` checks
them pixel by pixel. Still missing or approximate: sweep gradients (a flat colour), runtime
(AGSL) and bitmap shaders, Porter-Duff blend modes for drawing (SrcOver), inverse fills,
path combining, meshes, text on a path (drawn straight), fonts beyond Roboto regular/bold/mono
(no fallback: CJK, emoji and ▲ draw nothing), WebP/GIF. When changing the backend, render
every document before and after (`rc2png --time` for a fixed clock) and diff the pixels.

## Board-specific traps (each board's README has more)

- **Waveshare round (CH343P USB bridge) on macOS:**
  - The built-in CDC driver corrupts packets of about 512 bytes and more, so `idf.py flash`
    fails.
  - Flash with `tools/esptool256.py --no-stub` (256-byte blocks) and the IDF Python; the
    README has the exact command.
  - It has only 2 MB of PSRAM.
- **ESP32 and ESP32-S3 with the full engine:** set `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=0`
  and `RESERVE_INTERNAL=65536` (see `sdkconfig.rcplayer.defaults`). Otherwise the engine's
  thousands of small allocations fill internal RAM and Wi-Fi fails.
- **PaperS3 (e-ink):**
  - The driver is ours. M5GFX's bus timing, power sequence and waveforms are ported into
    `boards/m5stack_papers3/.../board.cpp`. The pins come from M5GFX; M5's docs page
    disagrees on PWR and OE.
  - It refreshes only changed pixels: about 0.5 s, about 18 frames.
  - The player never blocks on the panel: presentAsync converts and returns, and the newest
    frame wins.
  - Animated documents look jumpy.
  - About 1.4 MB of PSRAM is free.
  - **Touch orientation hasn't been confirmed with a real tap yet.** The GT911 reports
    540x960 and is mapped as identity in `touchMap()`.
  - GPIO44 is the power-off line, so the console is USB Serial/JTAG.
- **Internal RAM on the Muse builds:**
  - The Tab5's Muse build sends allocations under 16 KB to internal RAM first (the SDK sets
    `SPIRAM_MALLOC_ALWAYSINTERNAL=16384`). A parsed document is thousands of small objects:
    the 73 KB start screen took all 300 KB. The C6 link (SDIO) then failed at boot with "no
    mem for dma descriptors", and the board reset over and over.
  - `rcstage` therefore loads documents with PSRAM first (`PsramFirst` in `stage.cpp`).
    Anything else that allocates per document, or per frame, should do the same.
  - Flash reads and writes turn the cache off, so the calling task's stack must be in
    internal RAM. The player task's stack is in PSRAM on the S3 boards, so flash work goes
    on a task of its own (`pinTask`, `showTask`).
  - Deflate (`rc.screenshot`) needs a 16 KB stack, so it runs in PSRAM
    (`xTaskCreateWithCaps`).
  - The SDK console drops lines over `COMMAND_LINE_MAX`, raised from 96 to 384 for `>api`.
- **Tab5 (ESP32-P4 rev v1.x):** IDF v6 needs `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y`.
  - Wi-Fi and BLE go through the ESP32-C6 with esp_hosted over SDIO.
  - PSRAM bandwidth sets the frame rate; see README "Learnings".
- **Core2:** the classic ESP32. AXP192 or AXP2101 is detected at run time. It has no 3D.

## Engine (rccore) fixes made from here, in ../remotecompose-experiments

- Click handling: the converter writes DATA_TEXT inside MODIFIER_CLICK before
  HostActionMetadata, so those are applied before runAction (`CoreDocument::runClickActions`).
  Without this every tap sent empty metadata.
- `LayoutOperations` ConditionalOp fix.
- CoreText's default font size is 36, as writers assume (they leave out a 36). It was 16,
  so text at exactly 36 drew at 16.
- Find engine bugs with `rc2png` first; it is the same engine as the device. Since
  2026-10-19 the engine belongs to other agents: write the bug up (here, under known gaps)
  rather than fixing it.

Known gaps against the Java reference (androidx `remote-core`), not fixed:
- **Text styles are ignored.** CoreText's parameter 24 (`textStyleId`) is skipped
  (`LayoutOperations.h`, `case 24`), so a size, colour or weight a text gets from a
  `TextStyle` (opcode 242, with `parentId` inheritance) is lost: its size falls back to 36.
  Java applies the style wherever the component's own value is at the default
  (`CoreText.java` applyStyle).
- **Repaint scheduling** (compared 2026-10-20 with androidx `RemoteComposeView.onDraw` →
  `needsRepaint()` → `RemoteComposeState.getOpsToUpdate` / `wakeIn`). `getRepaintDelay`
  and `wakeIn` mirror Java, but:
  - **`TimeAttributeOp` calls `needsRepaint()` (redraw at once) where Java calls
    `wakeIn`.** Java uses 1 s for the `TIME_FROM_NOW_SEC` / `ARG_SEC` and `FROM_LOAD_SEC`
    kinds, and the rest of the minute for `TIME_FROM_NOW_MIN` (none for `ARG_MIN`). A
    document using these animates continuously on C++, so it never sleeps and costs power
    on every board.
  - **Neither Java nor C++ has a case for `ID_TIME_IN_HR`:** a document that reads only the
    hour never redraws. rcstage adds the hourly wake itself (`nextDelayMs` in
    `stage.cpp`).
  - Java computes the delay before the frame's ops run, so `WakeIn`s from frame N take
    effect after frame N+1. rcstage asks after the frame, so they take effect after N.
  - Java's `wakeIn` narrowing never resets `mRepaintSeconds` per frame, so once consumed a
    delay can only shrink: a document that once asks `WakeIn(1)` and later `WakeIn(300)`
    keeps waking every second. C++ copies that.
- **`densityBehavior` (header tag 27) isn't read.** Java scales sizes by density only in DP
  mode (2). rcstage always lays documents out in dp and scales the drawing, so DP documents
  match Java, and legacy/pixels documents draw larger than Java would (2x on the Tab5).

## Open threads (as of 2026-10-20)

**To do next**
- **Device state as named variables, not system ids.** Decided 2026-10-19: documents read
  the battery and friends as exported named variables (`system.battery_level`,
  `system.power_state`, and likewise memory free and network signal), not as system
  variables 38-41. Those ids aren't in the standard, and the standard JSON parser
  (androidx `ExpressionParser.java`) has no names for them nor any raw-id syntax, so a
  document using them only compiles with json2rc. Add the `setNamed` lines in
  `components/rcstage/src/device_state.cpp`, move the start-up screen (make_splash.py) off
  `batteryLevel`/`powerState`/`memoryFree`/`networkSignal`, and update
  `docs/RC_SYSTEM_VARIABLES.md` (proposals 38-41 and "d").
- **Core2 side button:** built and flashed, not yet confirmed by the owner (one document
  change was seen in the log). A `power` console command (volts, mA) would help too.
- **Muse commands (2026-10-20):** installed and tested on both the Tab5 and the PaperS3 over
  `tools/muse_api.py`, but not yet through a Muse agent. Not yet checked:
  - the x axis sign of `device.motion` on both boards (tilt the left edge down: x should be
    +9.8; z and y are checked);
  - whether the beeps are audible;
  - a shake gesture event for the PaperS3 ("shake to refresh").
  Both boards are unpinned, with no persisted values: the state they were in before.
- **Round board:** not reflashed since the Wi-Fi screen work; it will show the RC-only
  start-up screen.

- **Waveshare ESP32-S3-Touch-AMOLED-1.8** (`boards/waveshare_s3_amoled18`, added 2026-10-19):
  rcplayer runs (v1 board: SH8601 + FT3168, AXP2101), on NIXIOT as `amoled`. Touch works,
  orientation verified (identity); PWR (forward) and BOOT (back) work. Factory image in `backups/` (see its README).
- **`device.sleep` (PaperS3, 2026-10-20):**
  - Done:
    - `rcstage::nextFrameMs()` is the hook, from the engine's schedule.
    - The board's `wakeReason` / `sleepFor` (light, deep, off through the BM8563 timer).
    - Auto mode with local wakes in early init (no radio) and Muse check-ins.
  - Tested on USB: `off` really switches it off, and the RTC switches it back on (woke
    `rtc`). Auto with a minute hand: local wakes of 2.5–4.7 s each minute, check-ins about
    38 s. Light: about 20 s asleep without a reboot, then Muse reconnects.
  - Not done:
    - a document with `WakeIn` hasn't been tried;
    - the current drawn hasn't been measured;
    - the Tab5 has no sleep (its C6 radio and hosted link would need care);
    - each wake in `off` and `deep` redraws the whole e-ink, because the driver doesn't
      know what the panel holds. Saving the shown frame before sleeping would avoid that
      (flash wear: rotate it).
  - 2026-10-21: Muse couldn't reach it (awake_s had been set to 10 s, too short for an
    agent's turn). Added: hold mode (`hold`, `hold_max_s`, `resume`: a check-in waits for
    the agent), no sleep on USB power, 5 min awake after a non-timer wake, awake_s 60 min,
    `next_check_in`/`holding` in `rc.info` and `device.health`. Tested on USB (holding past
    awake_s, resume logs the check-in); the on-battery cycle with hold isn't tested yet.
  - The PaperS3 is left unpinned (start screen), auto on with hold, check-ins every 900 s.
- Fixed with it: rcstage gave `getRepaintDelay` uptime, not wall time, so minute hands
  moved up to 59 s late on every board.
- **AMOLED README:** add the motion sensor (QMI8658, `readMotion`, the `imu` console
  command, the calibrated axes) and `maze_simple`.
- **Possible bug:** `wifi add` sent about 6 s after an rcplayer boot said "saved" but the
  list stayed empty; `wifi SSID PASS` a few seconds later worked. Perhaps it raced net's
  start-up load of the saved list.

**Waiting on the owner or the rccore team**
- Commits (2026-10-20, owner's go-ahead), local and not pushed:
  - here, on `main`: `f6368da` (ThorVG colour filters, JPEG) and `3537f82` (Muse
    commands, sleep, the AMOLED board, fit documents, and the rest).
  - `../muse-gadget-sdk`, branch `tab5-p4-hosted`: the `gadget_ext_health` hook and the
    384-byte console line, which the Muse commands need.
  - Commit only when the owner asks.
- `../remotecompose-experiments` commit `6a8041c` (CoreText default font size 36): local,
  unpushed, waiting on the team. Never push it from here.
- Engine gaps for the rccore owners: see "Known gaps" above. Also found:
  - `timeInSec` differs between players. Java (`RemoteClock.getTimeInSec`) gives the
    second within the hour; rccore (`TimeVariables.cpp`) gives seconds since midnight.
    `timeInMin` agrees in both: minutes since midnight.
  - `rc2json` stops on `documents/fit/maze.rc` (unknown opcode 27).
- Nothing has been pushed in any repo.

**Later**
- Motion: the AMOLED board feeds documents (the mazes work); the Tab5 and PaperS3 BMI270s
  now do too (`readMotion`). The Core2's MPU6886 isn't read yet.
- The Core2's three printed touch buttons (rows 240-279) are unused: previous/home/next?
- rcplayer's 9.9 MB `storage` partition is unused: keep rcNet documents across reboots.
- ThorVG gaps (see "The ThorVG backend"): sweep gradients, a fallback font, Porter-Duff.
- PaperS3: it restarted once during its first pairing (cause not captured); touch
  orientation unchecked; partial refresh and automatic ghost clearing.
- Tab5: the Wi-Fi fallback away from NIXIOT and joining GoogleGuest-Legacy are untested.
- From RC_ACTION_PATTERNS.md: a host-filled list type, long press on a row, on-device text
  input, action discovery (`system.actions`).

## Writing style used in this repo

- Comments explain why, at about the density of the surrounding code.
- Docs use short declarative sentences, units on every number, and tables for comparisons.
- Board READMEs record sources and the date they were read, and mark what wasn't verified.
