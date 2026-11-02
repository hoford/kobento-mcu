#!/usr/bin/env python3
"""Builds the Kobento start-up screen: the bento-box mark, the "Kobento" wordmark, the tagline
and the board name, centred and scaled to any screen. Dark by default, a light paper palette on
e-ink (the board sets system.screen_eink).

  python3 tools/splash/make_splash.py   # -> splash.json here, kobento_splash.rc in documents/

This is the only document kobento-mcu ships that the sample player looks for by name (main.cpp
opens "kobento_splash" at boot if it is built in; otherwise it opens the first document). It
draws with the engine's own vector ops, so it needs no fonts or bitmaps beyond the player's
default font.

Live device state filled in by the player (docs/RC_SYSTEM_VARIABLES.md):
  named variables  system.board (shown under the wordmark), system.screen_eink (1 picks the
                   paper palette). Both keep the defaults below until the board sets them.

No networking, battery or Muse state: kobento-mcu is a display-only library. Integrations
(Wi-Fi, Home Assistant, Muse) and their own screens live in kobento-mcuApps.
"""
import json
import pathlib
import subprocess

HERE = pathlib.Path(__file__).resolve().parent
OUT_RC = HERE.parent.parent / "documents/kobento_splash.rc"
W = H = 360                              # the mark is designed in this square, then fit to the window

_MISS = object()


def paint(*ops):
    # Paint state carries over between draws: reset the shader and alpha unless given.
    ops = list(ops)
    keys = {k for op in ops for k in op}
    if "linearGradient" not in keys:
        ops.insert(0, {"shader": 0})
    if "alpha" not in keys:
        ops.append({"alpha": 1.0})
    return {"paint": {"ops": ops}}


def _parse_argb(v):
    # A literal "#RRGGBB" (opaque) or "#AARRGGBB" -> 0xAARRGGBB. Anything else (a colour-id
    # reference or an expression) is not statically known: return None.
    if not isinstance(v, str):
        return None
    s = v.strip()
    if s.startswith("#"):
        h = s[1:]
        if len(h) in (6, 8) and all(c in "0123456789abcdefABCDEF" for c in h):
            return (0xFF000000 | int(h, 16)) if len(h) == 6 else (int(h, 16) & 0xFFFFFFFF)
    return None


def dedup_paints(cmds, state=None):
    """Drop paint ops that only restate the paint the context already holds. The context
    carries over between draws (a PAINT_VALUES bundle is a delta, not a reset), yet paint()
    re-emits a full block before every draw. COLOR and ALPHA share one register (mPaint.color:
    COLOR writes all four bytes, ALPHA only the top byte), so they share the "color" slot here;
    SHADER(0) and a gradient share the "shader" slot. State is tracked only along the
    deterministic path: after a conditionalOperations or save{} block we forget the slots it
    could have written, and a later draw re-asserts what it needs. Pixel-identical whichever
    branches run. Returns the slots this list could write."""
    if state is None:
        state = {}
    out, touched = [], set()
    for it in cmds:
        if "paint" in it:
            kept = []
            for op in it["paint"]["ops"]:
                (k, v), = op.items()
                if k == "color":
                    touched.add("color")
                    argb = _parse_argb(v)
                    if argb is None:
                        kept.append(op); state.pop("color", None)
                    elif state.get("color") != argb:
                        kept.append(op); state["color"] = argb
                elif k == "alpha":
                    touched.add("color")
                    cur = state.get("color")
                    if isinstance(v, (int, float)) and cur is not None:
                        nxt = (cur & 0x00FFFFFF) | ((int(v * 255 + 0.5) & 0xFF) << 24)
                        if nxt != cur:
                            kept.append(op); state["color"] = nxt
                    else:                                  # expression alpha, or rgb unknown
                        kept.append(op); state.pop("color", None)
                else:
                    slot = "shader" if k in ("shader", "linearGradient") else k
                    val = ("g", json.dumps(v, sort_keys=True)) if k == "linearGradient" else (
                        json.dumps(v, sort_keys=True) if isinstance(v, (dict, list)) else v)
                    touched.add(slot)
                    if state.get(slot, _MISS) != val:
                        kept.append(op); state[slot] = val
            if kept:
                out.append({"paint": {"ops": kept}})
            continue
        for key in ("conditionalOperations", "save"):
            if key in it:
                it[key]["commands"], t = dedup_paints(it[key]["commands"], dict(state))
                for slot in t:                   # the branch may not run: forget what it set
                    state.pop(slot, None)
                touched |= t
                break
        out.append(it)
    return out, touched


def text(s, x, y, size, color, pan=0):
    # pan -1: x is the left edge, 0: the centre, 1: the right edge.
    return [paint({"color": color}, {"style": "fill"}, {"textSize": size}),
            {"drawTextAnchored": {"text": s, "x": x, "y": y, "panX": pan, "panY": 0}}]


def when(cond, v1, v2, *cmds):
    return {"conditionalOperations": {"condition": cond, "v1": v1, "v2": v2,
                                      "commands": [c for group in cmds for c in group]}}


def fit(dw, dh, cmds):
    """Draw a layout designed at dw x dh dp, scaled to fit the window and centred."""
    k = f"min(windowWidth / {dw}, windowHeight / {dh})"
    return {"save": {"commands": [
        {"translate": {"dx": f"(windowWidth - {dw} * {k}) / 2",
                       "dy": f"(windowHeight - {dh} * {k}) / 2"}},
        {"scale": {"sx": k, "sy": k}}] + cmds}}


# Brand palette (brand/BRAND.md). The mark is a bento box whose four compartments hold the
# accent tiles ("UI composition" made literal).
MAUVE, BLUE, GREEN, ORANGE = "#8B5CF6", "#3B82F6", "#22C55E", "#F59E0B"
DARK = {"BG": "#0F172A", "BOX": "#FFF7ED", "INK": "#FFFFFF", "DIM": "#94A3B8",
        "BOARD": "#64748B", "TILES": [MAUVE, BLUE, GREEN, ORANGE]}
# E-ink is black and white; a charcoal box with white compartments keeps the grid crisp where
# the accent colours would only dither to similar greys.
PAPER = {"BG": "#FFFFFF", "BOX": "#0F172A", "INK": "#0F172A", "DIM": "#555555",
         "BOARD": "#777777", "TILES": ["#FFFFFF"] * 4}


def mark(P):
    """The bento-box mark in the 360 x 360 field, centred on (180, 150)."""
    box = {"left": 105, "top": 75, "right": 255, "bottom": 225, "rx": 26, "ry": 26}
    cmds = [paint({"color": P["BOX"]}, {"style": "fill"}), {"drawRoundRect": box}]
    for (x, y), color in zip(((123, 93), (186, 93), (123, 156), (186, 156)), P["TILES"]):
        cmds += [paint({"color": color}, {"style": "fill"}),
                 {"drawRoundRect": {"left": x, "top": y, "right": x + 51, "bottom": y + 51,
                                    "rx": 9, "ry": 9}}]
    return cmds


def splash(P):
    cmds = mark(P)
    cmds += text("Kobento", 180, 268, 40, P["INK"])
    cmds += text("UI composition, anywhere", 180, 298, 15, P["DIM"])
    cmds += text("@system.board", 180, 332, 13, P["BOARD"])
    return cmds


def build():
    # One document for every shape of screen: the mark is designed in a 360 square and scaled
    # to fit the window; the board picks the paper palette on e-ink (system.screen_eink).
    edge = lambda color: [paint({"color": color}, {"style": "fill"}),
                          {"drawRect": {"left": 0, "top": 0, "right": "windowWidth",
                                        "bottom": "windowHeight"}}]
    cmds = [when("eq", "@system.screen_eink", 1, edge(PAPER["BG"]) + [fit(W, H, splash(PAPER))]),
            when("ne", "@system.screen_eink", 1, edge(DARK["BG"]) + [fit(W, H, splash(DARK))])]
    cmds, _ = dedup_paints(cmds)

    # What the player fills in. A named variable keeps this default until it is set.
    named = [
        {"name": "system.board", "type": "string", "value": "", "export": True},
        {"name": "system.screen_eink", "value": 0, "export": True},
    ]
    return {
        "header": {"apiLevel": 7, "width": W, "height": H, "profiles": 513,
                   "contentDescription": "Kobento start-up screen"},
        "root": [
            {"global": [{"resources": {"variables": named}}]},
            {"box": {"modifiers": ["fillMaxSize"], "children": [
                {"canvas": {"modifiers": ["fillMaxSize"], "commands": cmds}}]}},
        ],
    }


if __name__ == "__main__":
    src = HERE / "splash.json"
    src.write_text(json.dumps(build(), indent=1) + "\n")
    subprocess.run(["node", str(HERE / "compile.mjs"), str(src), str(OUT_RC)], check=True)
