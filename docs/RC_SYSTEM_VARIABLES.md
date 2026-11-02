# Device state in RemoteCompose: a proposal

RemoteCompose documents see the host through **system variables**. These are fixed ids
that the player keeps current: time, window size, touch, sensors. Nothing covers what
small devices care about most, such as battery, memory and network. This proposal adds
that in three layers, each built on something the format already has:

1. **Four system variable ids, 38–41,** for the most universal state.
2. **A naming convention for host-filled named variables,** for everything more specific.
   The player fills only the names a document declares. This is how
   `AndroidFloatSystemVariables` already fills in `system.font_weight`.
3. **`hostAction(0, "name[=value]")`,** for a document that wants to change the device.

It also lists the gaps found in the JSON format and the players while building this.
Each gap comes with a short API proposal.

Reference: androidx `compose/remote` at `9a589fa61f86` (2026-10-16), checked out at
`~/code/androidx`. The reference implementation is the M5Stack Tab5 (ESP32-P4) player in
this repository.

## What exists today

`RemoteContext.java` defines ids 1–35:
- **Time:** 1–4, 9–12, 32, 34, 35
- **Window and component size:** 5–8
- **Touch:** 13–16, 29
- **Accelerometer, gyroscope, magnetometer and light:** 17–26
- **Density, API level, animation time and font size:** 27, 28, 30, 31, 33

Ids from 42 up belong to documents (`RemoteComposeState.START_ID`), so **36–41 are the
only system ids left**. The TypeScript player in remotecompose-experiments uses 36 and 37
for `FIRST_BASELINE` and `LAST_BASELINE`. The reference doesn't define those, so this
proposal leaves them alone and takes 38–41.

## 1. System variables 38–41

| Id | Java constant | Expression name | Value | Unknown |
|---|---|---|---|---|
| 38 | `ID_BATTERY_LEVEL` | `batteryLevel` | 0–100 % | -1, also when there is no battery |
| 39 | `ID_POWER_STATE` | `powerState` | 0 on battery, 1 charging, 2 on external power with no battery or a full one | -1 |
| 40 | `ID_MEMORY_FREE` | `memoryFree` | free memory in MB, the memory available to the player | -1 |
| 41 | `ID_NETWORK_SIGNAL` | `networkSignal` | connection quality 0–100 %; 0 = offline | 0 |

**Why these four:**
- Every device class has them, from a coin-cell sensor to a phone.
- Documents need them to make a decision, not just to display them. Examples: dim when
  the battery is low, choose a lighter layout when memory is short, show stale-data
  styling when offline.
- Each fits in one float, with a sentinel for "this device can't say".

**Update rules:**
- These are **not** clock variables. A player updates them when they change, at most
  about once a second. A static document only repaints when one actually moves.
- The value is in the variable before the document's first data pass, so `batteryLevel`
  is never a stale 0 on the first frame.

**Implementing it:**

| Platform | Battery level | Power state | Memory free | Network signal |
|---|---|---|---|---|
| Android | `BatteryManager.BATTERY_PROPERTY_CAPACITY` | `ACTION_BATTERY_CHANGED` `EXTRA_STATUS` and `EXTRA_PLUGGED` | `ActivityManager.MemoryInfo.availMem` | `ConnectivityManager` plus `WifiManager.calculateSignalLevel` (or the cellular level), scaled to 0–100 |
| Tab5 | INA226 pack voltage on a 2S Li-ion curve | charger status pin | `heap_caps_get_free_size` (internal + PSRAM) | `2 × (RSSI + 100)` |

The shared sampler is `components/rcstage/src/device_state.cpp`; the Tab5's battery reading is `boards/m5stack_tab5/components/board/src/power.cpp`.

## 2. Named variables filled by the host

Anything specific to one class of device is a **named variable** that the document
declares with a default. The player overrides it if, and only if, the document declared
it. The pull model is the same one `AndroidFloatSystemVariables.loadSystemVariables`
uses, so no ids are spent and nothing is sent to documents that don't ask.

The convention:
- `system.*` for device state, extending the existing `system.font_size` and
  `system.font_weight`.
- An app prefix (here `muse.*`) for app state.
- The value is a **host override** (`setNamed*Override`). Once set, the document's own
  data pass can't overwrite it.

| Name | Type | Unit or meaning |
|---|---|---|
| `system.battery_voltage` | float | V |
| `system.battery_current` | float | mA, + into the battery |
| `system.charging` | float | 0 or 1 |
| `system.wifi_ssid` | string | |
| `system.wifi_rssi` | float | dBm |
| `system.ip` | string | |
| `system.memory_internal_kb` | float | ESP32: internal RAM |
| `system.memory_psram_kb` | float | ESP32: PSRAM |
| `system.temperature` | float | °C, the chip's own sensor |
| `system.uptime` | float | s |
| `system.headphones` | float | 0 or 1 |
| `system.backlight` | float | 0–1 |
| `system.board` | string | The board's display name |
| `system.screen_round` | float | 1 on a round panel (only the inscribed circle shows) |
| `system.screen_eink` | float | 1 on an e-ink panel: 16 greys, about 0.5 s per refresh. A document picks a white paper palette and leaves out animation. |
| `system.muse` | float | 1 when the player is the Muse gadget, 0 on the plain RC player. The start-up screen shows the Muse mark only when it is 1. |
| `system.banner_title`, `system.banner_detail` | string | The host's status line. Declaring `banner_title` tells the Tab5 player to hand its status banner to the document instead of drawing it on top. |
| `system.banner_color` | color | The status line's accent colour |
| `muse.link` | float | -1 error, 0 not paired, 1 connecting, 2 connected |
| `muse.agent`, `muse.device` | string | the Muse agent's title, the gadget's BLE name |
| `system.wifi_status` | string | The Wi-Fi screen's status line ("Connected to HomeWiFi") |
| `system.wifi_scanning` | float | 1 while a scan runs |
| `system.wifi_page`, `system.wifi_pages` | string, float | "1/2", and the page count, when the list is longer than the screen |
| `system.wifi_<i>_ssid` | string | Row *i* (0–9) of the network list |
| `system.wifi_<i>_state` | float | 0 empty, 1 connected, 2 connecting, 3 saved, 4 open (not saved), 5 saved but not in range, 6 couldn't connect, 7 needs a password |
| `system.wifi_<i>_signal`, `system.wifi_<i>_lock` | float | 0–100; 1 when the network needs a password |
| `ha.online` | float | 1 once Home Assistant has reached the player, or the player's last poll of it worked. rcplayer gets these two ways: it polls HA's REST API itself (`ha.cpp`, set up with the `ha` console command), or HA pushes them with `POST /named` on the rcNet port ("name=value" lines, from a `rest_command` automation), for when the display's network can't open connections to HA. |
| `ha.<entity_id>` | string or float | A Home Assistant entity's state, e.g. `ha.climate.ecobee` = "heat_cool" |
| `ha.<entity_id>.<attribute>` | string or float | Each number or string attribute, e.g. `ha.climate.ecobee.current_temperature`; null comes as "" (0 for a float). The document picks the type when it declares the name. |
| `Control.next`, `Control.back` | float | Press counts of the board's buttons (Core2: B or C, and A), raised once per press. A document that declares either takes the buttons, and the player stops using them to step through documents. The document decides what a press does (`ha_thermostat_pages` steps its pages by `next - back`). Sticky: counts keep rising across documents. |
| `Control.climate.<object_id>.<attr>` | float | **Written by the document**, read by the player (`rcstage::listenNamed("Control.*")`). `attr` is `temperature`, or `target_temp_low` and `target_temp_high` together for a range. When the document changes one (a click's `valueFloatExpressionChange`), rcplayer calls Home Assistant's `climate.set_temperature` for `climate.<object_id>`, reads the entity back, and sets the variable back to 0. 0 means nothing is asked, so the document shows `ha.*` again. |
| `Control.dial.climate.<object_id>` | float | **Written by the document**: the set point a drag on the dial chose, 0 when this entity's page isn't the one shown. Reported when the drag ends (0.3 s after the finger lifts). The player sets `Control.climate.<object_id>.temperature` to it and calls Home Assistant. |
| `system.touch_down` | float | 1 while a finger is on the screen, set by the player. A document uses it to show a dragged value before it is sent. |

A document declares the names it uses in JSON. Named strings are proposal 4a below:

```json
{"resources": {"variables": [
  {"name": "muse.link", "value": 1, "export": true},
  {"name": "system.wifi_ssid", "type": "string", "value": "", "export": true}
]}}
```

## 3. Host actions

A document changes the device through the existing host-action ops. They run from a
click, and the player decides what each one means:
- `HostAction(id, metadata)`, ops 209 and 216, reaches `RemoteContext.runAction(id, metadata)`.
- `HostNamedAction(name, value)`, op 210, reaches `runNamedAction`.

Action id **0** is reserved for a `"name[=value]"` string, with the same meaning as
`HostNamedAction(name, value)`. A document can therefore use whichever op its authoring
tool emits. The names match the variables above, so a document that writes
`system.backlight` can also read it back.

| Action | Effect on the Tab5 |
|---|---|
| `system.backlight=0.4` | Sets the backlight to 0.4 (any value 0–1). |
| `system.backlight=cycle` | Steps the backlight 100 → 60 → 30 %. |
| `muse.press` | Same as the setup button: confirm pairing, open the setup window. |
| `doc.show=NAME` | Shows a document built into the firmware. |
| `doc.next` | Shows the next built-in document. |
| `doc.back` | Shows the document shown before this one (rcplayer). |
| `wifi.join=I` | Joins the network in row *I* of the Wi-Fi list (rcplayer). |
| `wifi.scan` | Scans again (rcplayer). |
| `wifi.page` | Shows the next page of the Wi-Fi list (rcplayer). |
| `launch.open=I` | Opens row *I* of the launcher (Muse gadget). |
| `launch.page` | Shows the next page of the launcher (Muse gadget). |
| `launch.back` | Shows the pinned document, or the start screen (Muse gadget). |

The launcher's rows are `system.launch_<i>_kind` (0 empty, 1 Wi-Fi, 2 a document, 3 the
pinned one), `_name` and `_detail`, with `system.launch_status`, `system.launch_wifi`,
`system.launch_page` and `system.launch_pages` (see `tools/splash/make_launcher.py`).

A list of unknown length goes to the document as fixed rows (`system.wifi_<i>_*`), and a
tap names the row, not the network. The host keeps the row-to-network map, so a document
never has to send a string it read back. The player fills only the rows that fit on its
screen and pages the rest. RC has no list type for this yet; it would be a good candidate
for one.

A player ignores actions it doesn't know, and logs them. The patterns these actions
follow (state returned as variables, lists as rows, screen navigation, when actions run)
are in [RC_ACTION_PATTERNS.md](RC_ACTION_PATTERNS.md).

## 4. Gaps found, with proposals

The JSON gaps apply to `RemoteComposeJsonParser` and `ResourceParser` in
`remote-creation-core`, and the same in the TypeScript port. All of the proposals are
implemented in the port at
`remotecompose-experiments/players/typescript/src/json2rc`, marked `EXTENSION`, and
none changes the bytes of existing documents.

**a. Named strings can't be declared in JSON.**
- `RemoteComposeWriter.addNamedString(name, initial)` exists, but `ResourceParser`'s
  `variables` section only makes floats, integers and `textFromFloat`. A document can't
  declare a string for the host to fill in.
- *Proposal:* `{"name": "system.wifi_ssid", "type": "string", "value": "", "export": true}`
  calls `addNamedString` when `export` is set and `addText` when it isn't.
- `"@system.wifi_ssid"` then works anywhere text is accepted.

**b. `hostAction` metadata must be an id.**
- `DefaultModifierParsers` accepts `{"type": "hostAction", "actionId": 0, "metadataId": N}`.
- A JSON author can't know which text id the parser will assign, so in practice metadata
  can't be used.
- *Proposal:* also accept `"metadata": "<string>"`, which interns the text and writes
  `HostAction(actionId, textId)`.

**c. `conditionalOperations` has no "not equal".**
- `ConditionalOperations.TYPE_NEQ` exists, but the JSON condition map only has gt, ge, lt,
  le and eq. An unknown condition silently becomes eq.
- *Proposal:* add `"ne"` → `TYPE_NEQ`.
- Consider rejecting unknown conditions instead of treating them as eq. A typo currently
  produces the opposite of what the author meant.

**d. Expression names for 38–41.** Add `batteryLevel`, `powerState`, `memoryFree` and
`networkSignal`, each also with `()`, to `ExpressionParser`'s system-variable table,
beside `light` and `fontSize`.

**e. The TypeScript port lacked two reference features.** These were port bugs, not
proposals, and are now fixed in the port:
- The `hostAction` form without a name (`actionId`/`metadataId`).
- `"ne"`.

**f. The C++ player (rccore) didn't run host actions, and had no string override.**
- `HostActionOp`, `HostActionMetadataOp` and `HostNamedActionOp` were parsed but did
  nothing. They now call `RemoteContext::hostAction` / `hostNamedAction`, the C++ form of
  `runAction` / `runNamedAction`.
- A host-set string was overwritten by the document's own `DATA_TEXT` on the next data
  pass. `overrideText` now mirrors `RemoteComposeState.overrideData`.

## Trying it

- **Preview a document in any device state on the Mac,** with the same renderer the Tab5
  uses:
  ```sh
  host/build/rc2png doc.rc out.png --fit 720 1280 --density 2 \
      --sys 38=14 --sys 39=0 --sys 41=0 --named muse.link=0 \
      --named "system.banner_title=Pair in the Muse app"
  ```
- **The Tab5 start-up screen** (`tools/splash/make_splash.py`) uses all three layers:
  - Wi-Fi bars from `networkSignal`, with `system.wifi_ssid`.
  - Free memory from `memoryFree`.
  - A battery icon from `batteryLevel` and `powerState`.
  - The Muse connection state from `muse.link` and the banner variables.
  - A tap on the left of the top bar sends `hostAction(0, "doc.show=wifi_settings")`, and
    a tap on the right sends `hostAction(0, "system.backlight=cycle")`.
- **The Wi-Fi screen** (`tools/splash/make_wifi.py`, `documents/system/wifi_settings.rc`)
  lists networks from `system.wifi_<i>_*` and sends `wifi.join=I`, `wifi.scan`,
  `wifi.page` and `doc.back`. Documents in `documents/system/` need the player behind them,
  so only rcplayer embeds them.
- **On the device console:**
  - `>sys` prints the values the sampler last read.
  - `>act system.backlight=0.3` runs a host action.
  - `>vars` lists what the showing document declares.
