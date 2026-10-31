# Writing RC documents that sleep (PaperS3)

How a RemoteCompose document decides when an e-ink gadget wakes up, and how to write one
that lets it sleep. This covers the M5Stack PaperS3 running the Muse gadget firmware, with
auto sleep on (`device.sleep {"auto": true}`, see `docs/MUSE_COMMANDS.md`). Status: works
on the device as of 2026-10-20; the rules below are this player's, drawn from the Java
player's.

## The idea in one paragraph

The document decides when the next frame is needed; the board sleeps until then. Nothing
in the document says "sleep". The player reads the schedule the RC engine already keeps
for every player (the Java one included): which time variables the document reads, and any
`wakeIn` it asks for. An e-ink panel keeps its picture without power, so between frames the
board can be switched off entirely, and the screen still shows the document.

## What sets the next wake

The player takes the soonest of these that your document uses:

| Your document reads or does | Next frame | On the PaperS3 |
|---|---|---|
| nothing time-related (static) | never | sleeps until the next Muse check-in |
| `timeInHr` | just past the next hour | wakes hourly |
| `timeInMin` | just past the next minute | wakes each minute (about 3 s awake) |
| `wakeIn` with N seconds | in N seconds | wakes every N seconds |
| `timeInSec` | just past the next second | **never sleeps** |
| `continuousSec`, `animTime`, any running animation | at once (every frame) | **never sleeps** |

Notes:
- **"Reads"** means anything in the document that depends on the variable: an expression,
  a text, a rotation, a condition.
- **The variables:**
  - `timeInSec` is the second within the hour (0–3599), as the Java player defines it.
    The C++ engine currently gives seconds since midnight; that's reported to the engine
    owners.
  - `timeInMin` is minutes since midnight.
  - `timeInHr` is the hour.
  - All three are local time.
- **One variable can keep it awake:** a single seconds hand or a single animated value,
  anywhere in the document, does this, even off-screen.
- **Short waits aren't slept:** waits under 8 s aren't worth a wake (the board takes about
  3 s to boot and draw), so it stays awake for them.
- **`timeInHr` waking hourly is this player's addition.** Java has no case for hours, so in
  Java a document that reads only the hour never redraws. To be portable, add
  `timeInMin` or a `wakeIn`.
- **Calendar values don't wake anything:** day of month, weekday, month and year. A date
  that must turn over at midnight needs one of the wakes above too.

## wakeIn

```json
{ "type": "canvas", "commands": [ { "type": "wakeIn", "seconds": 300 } ] }
```

`wakeIn` asks for the next frame in that many seconds, from each frame that runs it. Use it
when the document needs a frame on a schedule of its own, such as:
- reading a sensor every 5 minutes;
- changing a picture every hour;
- turning the date over at midnight (compute the seconds to midnight in an expression).

Two rules come from the Java player's `wakeIn`:
- **It can only shorten the wait.** If the document also reads `timeInMin`, the frame
  comes at whichever is sooner.
- **Once a delay has been used, a later, longer `wakeIn` doesn't take effect.** Asking for
  1 s once and 300 s later leaves it waking every second. Pick one interval and keep to it.

## What a wake looks like to your document

The board has two kinds of wake. Your document can't tell them apart, but they differ in
what's around it.

**A frame wake** happens when your schedule says so. It doesn't start Wi-Fi or Muse; the
board:
1. boots in about 0.7 s;
2. sets the clock from its RTC;
3. loads the pinned document fresh;
4. waits about 1.5 s for the device values (battery, temperature);
5. draws, waiting for the boundary if it woke early;
6. switches off again.

**A check-in** happens every `check_in_s` (default 15 min). It is a full start: Wi-Fi and
Muse connect, and the agent gets `awake_s` (default 60 s) to push values or change the
document. Touches and commands keep it awake longer. With `hold` on, it waits for the agent
to send `resume` instead. On USB power, or for 5 minutes after the power button, it doesn't
sleep at all.

## What survives a sleep

The default mode (`off`) and `deep` start your document from scratch on every wake:

| Survives | Doesn't survive |
|---|---|
| the document (it must be pinned: `rc.pin`, or `persist` on `rc.show_url`) | anything your document computed or accumulated: counters, arrays, history |
| named variables the agent set with `persist: true` | values set without `persist` |
| the time (from the RTC) | animation state, touch state |
| fresh device values each wake: `system.battery_voltage`, `system.charging`, `system.temperature`, battery level | `system.wifi_ssid` and `system.ip` on a frame wake: Wi-Fi isn't up, so they're empty |

So:
- **Keep state on the agent's side.** For history, such as a temperature graph or the last
  day of prices, have the agent keep it and push it at check-in with `rc.set` and
  `persist`. Put "last updated" text in the document, so the reader knows how fresh it is.
- **Or use `light` mode** (`device.sleep {"auto": true, "mode": "light"}`). The CPU only
  pauses, so everything survives, and a document can build its own history: `wakeIn(300)`,
  read `system.temperature`, append. It draws more power than `off`; the battery cost
  hasn't been measured yet.
- **Don't show Wi-Fi status as if it were live.** Between check-ins the board is offline
  by design.

## Designing for it

- **Choose the coarsest time that works.** Hours and minutes are cheap; seconds and
  animation stop the board from sleeping at all.
- **Avoid animated transitions.** On e-ink you'd see them as a slow series of partial
  updates, and while they run the board can't sleep.
- **Expect the whole panel to refresh on every wake** in `off` and `deep`, a full e-ink
  flash, because the board forgets what the panel shows. Fewer wakes look calmer.
- **Taps work only while it's awake.** Touch doesn't wake a sleeping PaperS3; the power
  button does (a full start with an awake window). Buttons in your document work during
  check-ins and after a button wake. Their `hostAction`s reach the agent as `rc.events`.
- **Design for 16 greys.** Colours render as greys, dithered. Use the `_light` variant
  convention if the document should look different on e-ink.
- **Check the schedule.** `rc.info` shows `sleep.next_frame_ms`: `-1` static, `0` never
  sleeps, otherwise when the next frame comes. If it says `0` and you didn't expect it,
  something reads seconds or animates.

## Examples

| Document | Reads | Behaviour |
|---|---|---|
| A weather card the agent updates | named variables only | static: sleeps between check-ins; the agent pushes new values every 15 min |
| A clock with hour and minute hands | `timeInMin` | wakes each minute, about 3 s awake per minute |
| The dashboard (clock plus agent values) | `timeInMin` + named variables | as the clock; the agent refreshes values at each check-in |
| A temperature logger | `wakeIn` 300 + `system.temperature` | wakes every 5 min; in `light` it keeps its own graph, in `off` the agent must keep it |
| A clock with a seconds hand | `timeInSec` | never sleeps; remove the seconds hand for e-ink |

## Known gaps

- The C++ engine currently redraws continuously for the "time since" values (seconds or
  minutes since now, since load, or since a given time), where Java waits a second or a
  minute. A document using them never sleeps on the ESP32 players. Reported to the engine
  owners; until it's fixed, use `timeInMin` / `timeInSec` arithmetic instead.
- No board but the PaperS3 sleeps yet.
