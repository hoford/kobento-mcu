# Host actions in RemoteCompose: patterns

**Status:** a draft for review. These are the patterns the ESP32 players use so that a
document can drive its device: the Wi-Fi screen, the start-up screen, and the Muse gadget.
Once they are settled they can be proposed for androidx. The variables they read are in
[RC_SYSTEM_VARIABLES.md](RC_SYSTEM_VARIABLES.md).

The idea behind all of them: **the document draws and asks; the player decides and does.**
A document never holds device state of its own. It reads state from variables the player
fills in, and it sends requests the player is free to ignore.

## 1. One action op, one string

```
hostAction(0, "namespace.verb[=value]")
```

- **Action id 0** is reserved for this form. A `HostNamedAction(name, value)` with the same
  name and value means the same thing, so a document can use whichever op its authoring tool
  emits.
- **The name is `namespace.verb`,** for example `wifi.scan`, `doc.back` or
  `system.backlight`. The namespace matches the named variables that report the same thing,
  so a document that sends `system.backlight=0.4` reads `system.backlight` back.
- **The value is a short literal:** a number, a keyword (`cycle`), a row index or a document
  name. Never a string the document read from a variable (see pattern 4).
- **Unknown actions are logged and ignored.** A document written for a richer player still
  loads on a simpler one; its buttons just do nothing.

In JSON (an extension, proposal 4b in RC_SYSTEM_VARIABLES.md):

```json
{"onClick": {"type": "hostAction", "actionId": 0, "metadata": "wifi.scan"}}
```

## 2. State comes back as named variables, never as a reply

An action has no return value. Its result appears in the named variables the document
already reads, for example:
- `wifi.scan` → `system.wifi_scanning` is 1 while it runs; then the rows change.
- `wifi.join=2` → row 2's `state` goes to 2 (connecting), then to 1 (connected) or
  6 (couldn't connect).
- `system.backlight=cycle` → `system.backlight` holds the new level.

The document draws whatever the variables say. It doesn't need to know an action happened,
so it can't get out of step: the same picture comes back after a reload, after another
document changed things, or when the change didn't come from a tap at all.

## 3. Busy and progress are variables too

A slow action sets a flag while it runs (`system.wifi_scanning`), and the document swaps
the button's label ("Scan" → "···"). It also puts a status line in one string variable
(`system.wifi_status`: "Connecting to HomeWiFi…", "Couldn't join GuestNet"). The player owns
the wording, so it can explain a failure without the document knowing every case.

## 4. Lists: fixed rows, and the tap names the row

RC has no list type that a host can fill, so a list of unknown length goes out as fixed
**rows**:

```
system.wifi_<i>_ssid     string   what to show (shortened by the player)
system.wifi_<i>_state    float    0 = empty row (draws nothing), then one value per kind
system.wifi_<i>_signal   float
system.wifi_<i>_lock     float
```

- **The document lays the rows out with RC's layout** (a column of fixed-height boxes), so
  the tap targets follow the screen at any size.
- **The player fills only the rows that fit** on its screen, and leaves the rest empty.
- **A tap sends the row, not the item:** `wifi.join=<i>`. The player keeps the
  row-to-item map, because the text a row shows may be shortened, and a document can't
  build an action string from a variable anyway.
- **More items than rows:** the player pages them. It publishes `system.wifi_page` ("1/3")
  and `system.wifi_pages`, and the document shows a page button that sends `wifi.page`.

The state value carries the meaning, and the document maps it to a label and colour. That
keeps the wording in the document, so it can be restyled or translated without firmware
changes.

## 5. Screens: show, back, and return when done

```
doc.show=<name>   open a document built into the firmware
doc.back          the document shown before this one
doc.next          the next built-in document
launch.open=<i>   the launcher: open row i (Wi-Fi, the pinned document, a built-in one)
launch.page       the launcher: next page
launch.back       the pinned document (the start screen when nothing is pinned)
```

- **A screen opened for a task returns by itself when the task is done.** The start-up
  screen's Wi-Fi corner sends `doc.show=wifi_settings`. Picking a network joins it, shows
  "Connected" for 1.5 s, and the player goes back. **Done** (`doc.back`) leaves without
  picking.
- **Opening a screen can start its work:** showing `wifi_settings` starts a scan, so the
  list is fresh without a tap.
- **The player can open a screen when it needs a decision.** On the Muse gadget:
  - **At start** (a restart or the power button) the start screen stays up while Wi-Fi
    joins. Once it joins, the pinned document opens. With no network within 60 s, or a touch
    on the start screen, the launcher (`launcher`, `tools/splash/make_launcher.py`) opens instead. It lists
    Wi-Fi first, then the pinned document, then every built-in one; **Back** opens the
    pinned document. A long press anywhere opens it while running too.
  - **Later**, if Wi-Fi is lost and no saved network can be joined, it opens
    `wifi_settings`, and goes back to the document that was showing once a network joins.
  - That is the player's choice, not the document's (pattern 7).
- **Documents that need the player behind them live in `documents/system/`** and are built
  only into players that implement their actions.
- **A screen can come in variants for particular screens.** A round board shows
  `<name>_round` wherever `<name>` is asked for (`doc.show=wifi_settings` opens
  `wifi_settings_round`), and an e-ink board shows `<name>_light`, the same screen in a white
  palette. The long-press cycle shows each screen once, in this board's variant. A document
  therefore asks for a screen by its name, never by its shape or palette.
  - When one layout can adapt by scaling (the start-up screen), it chooses inside the
    document from the window size and `system.screen_round`. The same goes for the kind of
    panel: on e-ink (`system.screen_eink`) the start-up screen draws in a white paper
    palette.
  - When the tap targets themselves move (the Wi-Fi list), it is a second document: RC
    layout components can't be switched by a condition. So is a palette for a layout
    (`wifi_settings_light`), since a layout's background colours are fixed in it.
  - The player also sizes what it fills to the variant: three rows per page on the round
    screen, and shorter names.

## 6. Actions run after the tap, not during it

The engine raises an action from inside its click handling. The player queues it and runs
it once the touch is finished. An action may replace the document (`doc.show`,
`doc.back`), and doing that while the old document is still handling the tap broke Done.
Any player should do the same.

## 7. A tap is the only trigger

Actions come from click modifiers only. A document doesn't fire actions on load, on a
timer or from an animation: the player decides when work starts (pattern 5) and what runs
in the background. That keeps a document safe to show anywhere: on its own, it can't change
the device.

## 8. A value goes out as a variable the player listens to

A host action carries a literal string only (json2rc writes `HostNamedAction` values as
text), so it can't send a number the document computed, such as a set point plus 0.5. To
send one, the document exports a variable in a class the player listens to, and changes it
with a click's `valueFloatExpressionChange`. The player registers for the class with
`rcstage::listenNamed("Control.*", fn)`, and `fn(name, value)` runs when the document
changes one. Values the player sets itself with `setNamed` don't count. The player acts, then
sets the variable back to its resting value (0), so the next change is seen again and the
document can show "asked, not yet confirmed" in between.

Example (`ha_thermostat_pages`): + on the HeatPump page sets
`Control.climate.heatpump.temperature` to the shown set point + 0.5. The player calls Home
Assistant's `climate.set_temperature`, publishes the entity's new `ha.*` values, and puts the
variable back to 0. Taps within 150 ms go as one call.

The rule from pattern 7 still holds: only a touch changes these (a tap or a drag).

A drag works the same way. The value is reported once the gesture is over: 0.3 s after the
finger lifts, so an animation started by the release can settle first. Changes made while no
finger is down (clocks, `setNamed`, button paging) are taken as the new baseline and not
reported. In `ha_thermostat_pages`, a `touchExpression` (absolutePos) on the dial turns the
finger's angle into a set point. A canvas variable `Control.dial.climate.<id>` carries it out;
it is the dial value on the shown page and 0 on the others. The player sets
`Control.climate.<id>.temperature` from it and calls Home Assistant. While
`system.touch_down` is 1, the page shows the dial's value instead of HA's.

Exporting a canvas variable needs a json2rc patch (`"export": true` on a canvas `variable`
names it; vendored in `tools/splash/json2rc.js`, not upstream yet).

## Actions in use

| Action | Value | Player | Effect |
|---|---|---|---|
| `system.backlight` | 0–1, or `cycle` | both | Sets or steps (100 → 60 → 30 %) the backlight |
| `doc.show` | document name | both | Opens a built-in document |
| `doc.next` | | both | Next built-in document |
| `doc.back` | | both | The previous document |
| `wifi.scan` | | both | Scans; `system.wifi_scanning` while it runs |
| `wifi.join` | row | both | Joins a saved network or an open one, then returns to the previous screen |
| `wifi.page` | | both | The next page of the network list |
| `muse.press` | | muse-gadget | The setup button: confirm pairing, open the setup window |

On the Muse gadget, every action a document raises also reaches the agent through
`rc.events` (`docs/MUSE_COMMANDS.md`), whether or not the player handles it. A document can
therefore give the agent its own buttons, such as `hostAction(0, "dashboard.refresh")`,
with no change to the player: it logs "unknown host action" and the agent acts on the
event. Proposed: name such actions `<document>.<verb>` so they don't collide with the
player's own.

## Open questions

- **A list type.** Fixed rows work, but every list repeats the same row variables. A
  host-filled list (count plus per-item values) would make pattern 4 one declaration.
- **Long press on a row.** For example, `wifi.forget=<i>`. The player's long press
  currently means "next document" everywhere, which conflicts.
- **Text input.** A password can't be typed on the device yet; secured networks are added
  from a computer (`wifi add SSID PASSWORD` on the console). On-device entry needs either a
  keyboard document or a host-side text-entry action.
- **Exporting touch values upstream.** The json2rc patch that exports a canvas variable
  (pattern 8) should go to json2rc itself, or json2rc could take `"export": true` on
  `touchExpression` directly.
- **Discovery.** Nothing yet tells a document which actions the player supports. A
  `system.actions` string ("wifi,doc,system.backlight") would let it hide buttons that would
  do nothing.
