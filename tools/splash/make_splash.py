#!/usr/bin/env python3
"""Builds the start-up screen: "Muse Gadget + Remote Compose" on a Muse player, "Remote
Compose" alone on the RC player (system.muse = 0).

  python3 tools/splash/make_splash.py      # -> splash.json here, muse_splash.rc in the docs

The Muse mark is one thick stroke traced from the logo; the origami crane is the polygons of
Wikimedia's "Origami - Crane.svg" (crane.svg), each facet shaded from the mean of its
gradient. Four layouts, chosen by the window's aspect ratio and scaled to fit it: portrait
(360 x 640 dp, the Tab5; in a white paper palette when the board sets system.screen_eink, the
PaperS3), landscape (320 x 240 dp, the Core2), square and round (240 x 240 dp; round when the
board sets system.screen_round).

Live device state, filled in by the player (docs/RC_SYSTEM_VARIABLES.md):
  system variables  batteryLevel, powerState, memoryFree, networkSignal (proposed ids 38..41)
  named variables   system.muse (1: the Muse player; 0: Remote Compose alone),
                    system.wifi_ssid, system.banner_title / _detail / _color, muse.link,
                    muse.agent; declaring system.banner_title makes the player hand its status
                    banner to this document instead of drawing it on top
  host actions      a tap on the left of the top bar (Wi-Fi) sends hostAction(0,
                    "doc.show=wifi_settings"), on the right (battery) "system.backlight=cycle"
"""
import json
import pathlib
import re
import subprocess

HERE = pathlib.Path(__file__).resolve().parent
OUT_RC = HERE.parent.parent / "documents/muse_splash.rc"
W, H = 360, 640


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
    # reference like "$colors..." or an expression) is not statically known: return None.
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
    carries over between draws (a PAINT_VALUES bundle is a delta, not a reset), yet the
    generator's paint() helper re-emits a full block before every draw.

    The engine (ThorvgPaintContext::applyPaint) folds COLOR and ALPHA into one register,
    mPaint.color: COLOR writes all four bytes (its own alpha included), ALPHA overwrites only
    the top byte. So they share the "color" slot here — the subtlety that makes a naive
    per-key dedup wrong (dropping an alpha after a colour would leave the colour's own alpha in
    force). SHADER(0) and the gradient share the "shader" slot. A colour or alpha that is an
    expression or colour-id (not a literal) can't be resolved, so it is never dropped and
    leaves the slot unknown.

    State is tracked only along the deterministic path. A conditionalOperations block may or
    may not run, so after one we forget every slot it could have written; a later draw then
    re-asserts what it needs. save{} (the fit wrapper) is treated the same way. The result is
    pixel-identical whichever branches run. Returns the slots this list could write."""
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


_MISS = object()


def text(s, x, y, size, color, pan=0):
    # pan -1: x is the left edge, 0: the centre, 1: the right edge.
    return [paint({"color": color}, {"style": "fill"}, {"textSize": size}),
            {"drawTextAnchored": {"text": s, "x": x, "y": y, "panX": pan, "panY": 0}}]


def when(cond, v1, v2, *cmds):
    return {"conditionalOperations": {"condition": cond, "v1": v1, "v2": v2,
                                      "commands": [c for group in cmds for c in group]}}


def number(expr, whole, decimal):
    return {"type": "textFromFloat", "value": expr, "whole": whole, "decimal": decimal}


INK, DIM = "#E6EDF7", "#8FA0BE"
GREEN, AMBER, RED = "#4CD964", "#FFC107", "#FF453A"
# The rest of the palette: dark (the default) or paper (e-ink: white, dark ink, no glows).
DARK = {"INK": INK, "DIM": DIM, "SKY": ["#FF070B18", "#FF0C1A2C"], "GLOW": True,
        "CARD": "#141C2A", "CARD_EDGE": "#26324A", "BOLT_EDGE": "#0B1220",
        "TITLE": "#FFFFFF", "COMPOSE": "#8EF0C4", "BOARD": "#7D8AA5",
        "PLUS_LINE": "#5A6886", "PLUS": "#9FB0D0"}
PAPER = {"INK": "#111111", "DIM": "#555555", "SKY": None, "GLOW": False,
         "CARD": "#FFFFFF", "CARD_EDGE": "#222222", "BOLT_EDGE": "#FFFFFF",
         "TITLE": "#111111", "COMPOSE": "#0B6B53", "BOARD": "#555555",
         "PLUS_LINE": "#9A9A9A", "PLUS": "#444444"}
P = DARK


def use_palette(pal):
    global P, INK, DIM
    P, INK, DIM = pal, pal["INK"], pal["DIM"]


def status_bar(width, k=1.0, compact=False):
    """Wi-Fi on the left, free memory in the middle, battery on the right. Laid out for a
    360 dp wide portrait screen and scaled by k (the right side stays anchored to `width`)."""
    R = lambda v: round(v * k, 2)                      # a length or a left-anchored x
    RX = lambda v: round(width - (360 - v) * k, 2)     # an x anchored to the right edge
    cmds = []
    # Four bars, each lit above its share of networkSignal (0 = offline).
    for i, (h, t) in enumerate(((6, 0), (10, 30), (14, 55), (18, 80))):
        x = 20 + i * 7
        cmds += [paint({"color": INK}, {"style": "fill"},
                       # clamp(min, max, value): value last.
                       {"alpha": f"0.25 + 0.75 * clamp(0, 1, (networkSignal - {t}) * 1000)"}),
                 {"drawRoundRect": {"left": R(x), "top": R(38 - h), "right": R(x + 4),
                                    "bottom": R(38), "rx": R(1), "ry": R(1)}}]
    if not compact:
        cmds.append(when("gt", "networkSignal", 0, text("@system.wifi_ssid", R(54), R(32), R(13), INK, -1)))
        cmds.append(when("le", "networkSignal", 0, text("No Wi-Fi", R(54), R(32), R(13), DIM, -1)))
        cmds += text(number("memoryFree", 3, 1), width / 2, R(32), R(12), DIM, 1)
        cmds += text(" MB free", width / 2, R(32), R(12), DIM, -1)

    # Battery: outline and nub, a fill to the level (red at 20 % and below), the percentage.
    batt = []
    batt += [paint({"color": INK}, {"style": "stroke"}, {"width": R(1.5)}),
             {"drawRoundRect": {"left": RX(306), "top": R(21), "right": RX(334), "bottom": R(37),
                                "rx": R(3), "ry": R(3)}},
             paint({"color": INK}, {"style": "fill"}),
             {"drawRoundRect": {"left": RX(335), "top": R(26), "right": RX(338), "bottom": R(32),
                                "rx": R(1), "ry": R(1)}}]
    right = f"{RX(308.5)} + {R(23)} * batteryLevel / 100"
    for cond, color in (("gt", GREEN), ("le", RED)):
        batt.append(when(cond, "batteryLevel", 20,
                         [paint({"color": color}, {"style": "fill"}),
                          {"drawRect": {"left": RX(308.5), "top": R(23.5), "right": right,
                                        "bottom": R(34.5)}}]))
    batt += text(number("batteryLevel", 3, 0), RX(291), R(32), R(13), INK, 1)
    batt += text("%", RX(292), R(32), R(13), INK, -1)
    cmds.append(when("ge", "batteryLevel", 0, batt))
    # Charging: a bolt over the battery.
    bolt = [(322, 19), (313, 30), (319, 30), (316, 39), (326, 27), (320, 27)]
    bolt = svg_path([(RX(x), R(y)) for x, y in bolt], close=True)
    cmds.append(when("eq", "powerState", 1,
                     [paint({"color": P["BOLT_EDGE"]}, {"style": "stroke"}, {"width": R(2.5)},
                            {"strokeJoin": "round"}),
                      {"drawPath": {"path": bolt}},
                      paint({"color": AMBER}, {"style": "fill"}),
                      {"drawPath": {"path": bolt}}]))
    # No battery (or not readable): on external power.
    cmds.append(when("lt", "batteryLevel", 0, text("USB power", RX(338), R(32), R(13), INK, 1)))
    return cmds


def status_card(left, top, right, bottom, k=1.0):
    """The connection state: the player's banner text, or Muse's agent once connected."""
    R = lambda v: round(v * k, 2)
    mid = (top + bottom) / 2
    box = {"left": left, "top": top, "right": right, "bottom": bottom, "rx": R(14), "ry": R(14)}
    cmds = [paint({"color": P["CARD"]}, {"style": "fill"}, {"alpha": 0.92}),
            {"drawRoundRect": box},
            paint({"color": P["CARD_EDGE"]}, {"style": "stroke"}, {"width": 1}),
            {"drawRoundRect": box}]
    cx, tx = left + R(22), left + R(40)
    dot = lambda c: [paint({"color": c}, {"style": "fill"}),
                     {"drawCircle": {"cx": cx, "cy": mid, "radius": R(6)}}]
    cmds.append(when("eq", "@muse.link", 2, dot(GREEN)))
    cmds.append(when("eq", "@muse.link", 1, dot(AMBER)))
    cmds.append(when("eq", "@muse.link", 0, [paint({"color": "$colors.system.banner_color"},
                                                    {"style": "fill"}),
                                              {"drawCircle": {"cx": cx, "cy": mid, "radius": R(6)}}]))
    cmds.append(when("lt", "@muse.link", 0, dot(RED)))
    cmds.append(when("eq", "@muse.link", 2,
                     text("Connected to Muse", tx, mid - R(5), R(15), INK, -1),
                     text("@muse.agent", tx, mid + R(14), R(12), DIM, -1)))
    cmds.append(when("lt", "@muse.link", 2,
                     text("@system.banner_title", tx, mid - R(5), R(15), INK, -1),
                     text("@system.banner_detail", tx, mid + R(14), R(12), DIM, -1)))
    return cmds


def link_dot(cx, cy, r):
    """Muse's link state as a dot: green connected, amber connecting, the banner's colour
    while pairing, red on error."""
    dot = lambda c: [paint({"color": c}, {"style": "fill"}),
                     {"drawCircle": {"cx": cx, "cy": cy, "radius": r}}]
    return [when("eq", "@muse.link", 2, dot(GREEN)),
            when("eq", "@muse.link", 1, dot(AMBER)),
            when("eq", "@muse.link", 0, [paint({"color": "$colors.system.banner_color"},
                                               {"style": "fill"}),
                                         {"drawCircle": {"cx": cx, "cy": cy, "radius": r}}]),
            when("lt", "@muse.link", 0, dot(RED))]


def status_title(x, y, size, pan):
    return [when("eq", "@muse.link", 2, text("Connected to Muse", x, y, size, INK, pan)),
            when("lt", "@muse.link", 2, text("@system.banner_title", x, y, size, INK, pan))]


def mix(a, b, t):
    ca = [int(a[i:i + 2], 16) for i in (1, 3, 5)]
    cb = [int(b[i:i + 2], 16) for i in (1, 3, 5)]
    return "#" + "".join(f"{round(x + (y - x) * t):02X}" for x, y in zip(ca, cb))


def svg_path(pts, close=False):
    d = "M" + " L".join(f"{x:.1f},{y:.1f}" for x, y in pts)
    return d + " Z" if close else d


# ── Muse mark: the stroke's centre line in the logo's 800 x 600 artwork ──
MUSE = [(186, 272), (284, 163), (245, 432), (410, 188), (398, 432), (530, 238), (527, 434),
        (620, 327)]
MUSE_STROKE = 62
MUSE_CENTER = (403, 298)      # middle of the mark, stroke included


def muse_path(cx, cy, scale):
    pts = [(cx + (x - MUSE_CENTER[0]) * scale, cy + (y - MUSE_CENTER[1]) * scale) for x, y in MUSE]
    return svg_path(pts), MUSE_STROKE * scale


# ── Crane: filled polygons from crane.svg, in drawing order ──
def crane_facets():
    svg = (HERE / "crane.svg").read_text()
    grads = {}
    for m in re.finditer(r'<linearGradient\b(.*?)(/>|>(.*?)</linearGradient>)', svg, re.S):
        gid = re.search(r'id="([^"]+)"', m.group(1)).group(1)
        href = re.search(r'xlink:href="#([^"]+)"', m.group(1))
        stops = [int(c, 16) for c in re.findall(r'stop-color:#([0-9a-fA-F]{2})', m.group(3) or "")]
        grads[gid] = (href.group(1) if href else None, stops)

    def shade(gid):
        while gid in grads and not grads[gid][1] and grads[gid][0]:
            gid = grads[gid][0]
        stops = grads[gid][1]
        return sum(stops) / len(stops) / 255.0

    facets = []
    # Each facet sits in its own <g transform="translate(...)" style="opacity:..">.
    for g in re.finditer(r'<g\b(.*?)>(.*?)</g>', svg, re.S):
        tr = re.search(r'translate\(([-\d.]+),([-\d.]+)\)', g.group(1))
        dx, dy = (float(tr.group(1)), float(tr.group(2))) if tr else (0.0, 0.0)
        gop = re.search(r'opacity:([\d.]+)', g.group(1))
        for p in re.finditer(r'<path\b(.*?)/>', g.group(2), re.S):
            style = re.search(r'style="([^"]*)"', p.group(1)).group(1)
            fill = re.search(r'fill:url\(#([^)]+)\)', style)
            if not fill:
                continue        # the fold line, a stroke only
            d = re.search(r'\sd="([^"]*)"', p.group(1)).group(1)
            nums = [float(v) for v in re.findall(r'[-\d.]+', d)]
            pts = [(nums[i] + dx, nums[i + 1] + dy) for i in range(0, len(nums), 2)]
            if pts[0] == pts[-1]:
                pts.pop()
            facets.append((pts, shade(fill.group(1)), float(gop.group(1)) if gop else 1.0))
    return facets


def backdrop(w, h, glows):
    if not P["SKY"]:
        # Paper: plain white. It covers the whole window, as the player clears to white anyway.
        return [paint({"color": "#FFFFFF"}, {"style": "fill"}),
                {"drawRect": {"left": -w, "top": -h, "right": 2 * w, "bottom": 2 * h}}]
    cmds = [paint({"style": "fill"}, {"linearGradient": {
        "x1": 0, "y1": 0, "x2": 0, "y2": h, "colors": P["SKY"], "tileMode": 0}}),
        {"drawRect": {"left": 0, "top": 0, "right": w, "bottom": h}}]
    for color, cx, cy, r0, step in glows:
        for k in range(6):
            cmds += [paint({"color": color}, {"style": "fill"}, {"alpha": 0.025}),
                     {"drawCircle": {"cx": cx, "cy": cy, "radius": r0 - k * step}}]
    return cmds


def muse_mark(cx, cy, scale):
    """The Muse mark: round caps and joins, blue gradient corner to corner like the logo."""
    path, width = muse_path(cx, cy, scale)
    half = 237 * scale   # the mark's half-width; the gradient spans its box
    return [paint({"style": "stroke"}, {"width": round(width, 2)}, {"strokeCap": "round"},
                  {"strokeJoin": "round"},
                  {"linearGradient": {"x1": cx - half, "y1": cy - 0.63 * half, "x2": cx + half,
                                      "y2": cy + 0.63 * half,
                                      "colors": ["#FF0042D6", "#FF0A84FF"], "tileMode": 0}}),
            {"drawPath": {"path": path}}]


def crane(cx, cy, width, edge=0.8):
    """The crane, paper turned Compose green: the darkest facet deep teal, the lightest mint."""
    facets = crane_facets()
    xs = [x for f in facets for x, _ in f[0]]
    ys = [y for f in facets for _, y in f[0]]
    scale = width / (max(xs) - min(xs))
    ox = cx - (min(xs) + max(xs)) / 2 * scale
    oy = cy - (min(ys) + max(ys)) / 2 * scale
    lo, hi = min(f[1] for f in facets), max(f[1] for f in facets)
    cmds = []
    for pts, l, alpha in facets:
        path = svg_path([(ox + x * scale, oy + y * scale) for x, y in pts], close=True)
        color = mix("#0E5A4C", "#D9FBEA", (l - lo) / (hi - lo))
        cmds += [paint({"color": color}, {"style": "fill"}, {"alpha": alpha}),
                 {"drawPath": {"path": path}}]
        if alpha == 1.0:
            cmds += [paint({"color": "#06231E"}, {"style": "stroke"}, {"width": edge},
                           {"strokeJoin": "round"}),
                     {"drawPath": {"path": path}}]
    return cmds


def plus(cx, cy, arm, gap, line, stroke):
    """A "+" between two hairlines."""
    return [paint({"color": P["PLUS_LINE"]}, {"style": "stroke"}, {"width": 1}),
            {"drawLine": {"x1": cx - gap - line, "y1": cy, "x2": cx - gap, "y2": cy}},
            {"drawLine": {"x1": cx + gap, "y1": cy, "x2": cx + gap + line, "y2": cy}},
            paint({"color": P["PLUS"]}, {"style": "stroke"}, {"width": stroke},
                  {"strokeCap": "round"}),
            {"drawLine": {"x1": cx, "y1": cy - arm, "x2": cx, "y2": cy + arm}},
            {"drawLine": {"x1": cx - arm, "y1": cy, "x2": cx + arm, "y2": cy}}]


def portrait(pal=DARK, muse=True):
    """360 x 640 dp (the Tab5, and the PaperS3 in the paper palette): the marks stacked, the
    status card at the bottom. Without Muse, the crane alone, larger."""
    use_palette(pal)
    w, h = 360, 640
    if muse:
        cmds = backdrop(w, h, (("#1E6BFF", 180, 118, 124, 16), ("#3DDC84", 180, 392, 136, 16)))
        cmds += muse_mark(180, 118, 0.38)
        cmds += text("Muse Gadget", 180, 236, 34, P["TITLE"])
        cmds += plus(180, 282, 12, 30, 80, 3)
        cmds += crane(180, 392, 236)
        cmds += text("Remote Compose", 180, 522, 34, P["COMPOSE"])
    else:
        cmds = backdrop(w, h, (("#3DDC84", 180, 290, 170, 18),))
        cmds += crane(180, 290, 300)
        cmds += text("Remote Compose", 180, 500, 38, P["COMPOSE"])
    cmds += text("@system.board", 180, 556, 14, P["BOARD"])
    cmds += status_bar(w)
    cmds += status_card(16, 576, 344, 628)
    use_palette(DARK)
    return cmds


def landscape(muse=True):
    """320 x 240 dp (the Core2): the marks side by side, the status card at the bottom.
    Without Muse, the crane alone in the middle."""
    w, h = 320, 240
    if muse:
        cmds = backdrop(w, h, (("#1E6BFF", 80, 92, 70, 9), ("#3DDC84", 240, 92, 74, 9)))
        cmds += muse_mark(80, 90, 0.2)
        cmds += plus(160, 90, 7, 0, 0, 2.5)
        cmds += crane(240, 92, 126, edge=0.6)
        cmds += text("Muse Gadget", 80, 152, 17, "#FFFFFF")
        cmds += text("Remote Compose", 240, 152, 17, "#8EF0C4")
    else:
        cmds = backdrop(w, h, (("#3DDC84", 160, 88, 92, 10),))
        cmds += crane(160, 88, 150, edge=0.6)
        cmds += text("Remote Compose", 160, 158, 22, "#8EF0C4")
    cmds += text("@system.board", 160, 178, 11, "#7D8AA5")
    cmds += status_bar(w, 0.78)
    cmds += status_card(8, 192, 312, 232, 0.78)
    return cmds


def square(muse=True):
    """240 x 240 dp: marks side by side, the names stacked, a one-line status card. Without
    Muse, the crane alone."""
    w, h = 240, 240
    if muse:
        cmds = backdrop(w, h, (("#1E6BFF", 66, 96, 58, 8), ("#3DDC84", 172, 96, 62, 8)))
        cmds += muse_mark(66, 94, 0.15)
        cmds += plus(120, 94, 6, 0, 0, 2)
        cmds += crane(172, 96, 96, edge=0.5)
        cmds += text("Muse Gadget", 120, 152, 17, "#FFFFFF")
        cmds += text("Remote Compose", 120, 173, 17, "#8EF0C4")
    else:
        cmds = backdrop(w, h, (("#3DDC84", 120, 96, 70, 9),))
        cmds += crane(120, 96, 124, edge=0.5)
        cmds += text("Remote Compose", 120, 168, 20, "#8EF0C4")
    cmds += status_bar(w, 0.66, compact=True)
    box = {"left": 8, "top": 190, "right": 232, "bottom": 230, "rx": 10, "ry": 10}
    cmds += [paint({"color": "#141C2A"}, {"style": "fill"}, {"alpha": 0.92}),
             {"drawRoundRect": box},
             paint({"color": "#26324A"}, {"style": "stroke"}, {"width": 1}),
             {"drawRoundRect": box}]
    cmds += link_dot(24, 210, 5)
    cmds += status_title(36, 214, 12, -1)
    return cmds


def round_(muse=True):
    """240 x 240 dp, round: everything inside the circle, a ring around the edge. Without
    Muse, the crane alone and a ring in Compose greens."""
    w, h, c = 240, 240, 120
    cmds = backdrop(w, h, (("#1E6BFF", 82, 104, 50, 7), ("#3DDC84", 160, 104, 54, 7)) if muse
                    else (("#3DDC84", 120, 102, 62, 8),))
    # The rim: a thin ring shading from Muse blue (left) to Compose green (right) and back,
    # as arc segments (a sweep gradient on a stroke isn't drawn by every player).
    n = 24
    for i in range(n):
        t = abs(((i + 0.5) / n * 2) - 1)                 # 1 at the left, 0 at the right
        cmds += [paint({"color": mix("#3DDC84", "#0A84FF" if muse else "#0E5A4C", t)},
                       {"style": "stroke"}, {"width": 3}),
                 {"drawArc": {"left": c - 116, "top": c - 116, "right": c + 116, "bottom": c + 116,
                              "startAngle": i * 360 / n, "sweepAngle": 360 / n + 0.6}}]
    # Top: Wi-Fi bars and the battery, centred.
    for i, (hh, t) in enumerate(((4, 0), (7, 30), (10, 55), (13, 80))):
        x = 92 + i * 5
        cmds += [paint({"color": INK}, {"style": "fill"},
                       {"alpha": f"0.25 + 0.75 * clamp(0, 1, (networkSignal - {t}) * 1000)"}),
                 {"drawRoundRect": {"left": x, "top": 40 - hh, "right": x + 3, "bottom": 40,
                                    "rx": 1, "ry": 1}}]
    batt = text(number("batteryLevel", 3, 0), 140, 40, 12, INK, 1) + text("%", 141, 40, 12, INK, -1)
    cmds.append(when("ge", "batteryLevel", 0, batt))
    bolt = svg_path([(158, 28), (152, 36), (156, 36), (154, 43), (161, 34), (157, 34)], close=True)
    cmds.append(when("eq", "powerState", 1, [paint({"color": AMBER}, {"style": "fill"}),
                                            {"drawPath": {"path": bolt}}]))
    if muse:
        cmds += muse_mark(76, 102, 0.13)
        cmds += plus(120, 102, 5, 0, 0, 2)
        cmds += crane(166, 104, 80, edge=0.5)
        cmds += text("Muse Gadget", c, 150, 15, "#FFFFFF")
        cmds += text("Remote Compose", c, 169, 15, "#8EF0C4")
    else:
        cmds += crane(c, 100, 104, edge=0.5)
        cmds += text("Remote Compose", c, 164, 17, "#8EF0C4")
    cmds += status_title(c, 194, 11, 0)
    cmds += link_dot(c, 210, 4)
    return cmds


def fit(dw, dh, cmds):
    """Draw a layout designed at dw x dh dp, scaled to fit the window and centred."""
    k = f"min(windowWidth / {dw}, windowHeight / {dh})"
    return {"save": {"commands": [
        {"translate": {"dx": f"(windowWidth - {dw} * {k}) / 2",
                       "dy": f"(windowHeight - {dh} * {k}) / 2"}},
        {"scale": {"sx": k, "sy": k}}] + cmds}}


def build():
    # One document for every shape of screen: the layout follows the window's aspect ratio,
    # and the board says whether a square panel is round (system.screen_round) and whether
    # the panel is e-ink (system.screen_eink: the paper palette).
    # And whether it runs Muse (system.muse): without it, Remote Compose alone.
    ratio = "windowWidth / windowHeight"

    def layouts(muse):
        tall = [when("eq", "@system.screen_eink", 1, [fit(360, 640, portrait(PAPER, muse))]),
                when("ne", "@system.screen_eink", 1, [fit(360, 640, portrait(DARK, muse))])]
        squareish = [when("eq", "@system.screen_round", 1, [fit(240, 240, round_(muse))]),
                     when("ne", "@system.screen_round", 1, [fit(240, 240, square(muse))])]
        return [when("lt", ratio, 0.8, tall),
                when("gt", ratio, 1.25, [fit(320, 240, landscape(muse))]),
                when("ge", ratio, 0.8, [when("le", ratio, 1.25, squareish)])]

    # The layouts keep their own aspect ratio; the window around them gets the backdrop's
    # colour (white on e-ink), so a taller or wider screen shows no bands.
    edge = lambda color: [paint({"color": color}, {"style": "fill"}),
                          {"drawRect": {"left": 0, "top": 0, "right": "windowWidth",
                                        "bottom": "windowHeight"}}]
    cmds = [when("eq", "@system.screen_eink", 1, edge("#FFFFFF")),
            when("ne", "@system.screen_eink", 1, edge(DARK["SKY"][0])),
            when("eq", "@system.muse", 1, layouts(True)),
            when("ne", "@system.muse", 1, layouts(False))]
    # Strip paint ops that only restate the current paint state (the helper re-emits a full
    # block per draw; the context carries it over). Pixel-identical; see dedup_paints.
    cmds, _ = dedup_paints(cmds)

    # What the player fills in. A named variable keeps this default until it is set.
    named = [
        {"name": "muse.link", "value": 1, "export": True},
        {"name": "muse.agent", "type": "string", "value": "", "export": True},
        {"name": "system.wifi_ssid", "type": "string", "value": "", "export": True},
        {"name": "system.banner_title", "type": "string", "value": "Starting", "export": True},
        {"name": "system.banner_detail", "type": "string", "value": "", "export": True},
        {"name": "system.board", "type": "string", "value": "", "export": True},
        {"name": "system.screen_round", "value": 0, "export": True},
        {"name": "system.screen_eink", "value": 0, "export": True},
        {"name": "system.muse", "value": 1, "export": True},
    ]
    backlight = {"type": "hostAction", "actionId": 0, "metadata": "system.backlight=cycle"}
    wifi = {"type": "hostAction", "actionId": 0, "metadata": "doc.show=wifi_settings"}
    return {
        "header": {"apiLevel": 7, "width": W, "height": H, "profiles": 513,
                   "contentDescription": "Remote Compose start-up screen (with Muse Gadget on a Muse player)"},
        "root": [
            {"global": [{"resources": {"colors": [{"system.banner_color": "#FF9E9E9E"}],
                                       "variables": named}}]},
            {"box": {"modifiers": ["fillMaxSize"], "children": [
                {"canvas": {"modifiers": ["fillMaxSize"], "commands": cmds}},
                # The top bar is two buttons: Wi-Fi on the left opens the network list, the
                # battery side cycles the backlight.
                {"row": {"modifiers": ["fillMaxWidth", {"height": 44}], "children": [
                    {"box": {"modifiers": [{"horizontalWeight": 1}, "fillMaxHeight",
                                           {"onClick": wifi}], "children": []}},
                    {"box": {"modifiers": [{"horizontalWeight": 1}, "fillMaxHeight",
                                           {"onClick": backlight}], "children": []}}]}},
            ]}},
        ],
    }


if __name__ == "__main__":
    src = HERE / "splash.json"
    src.write_text(json.dumps(build(), indent=1) + "\n")
    subprocess.run(["node", str(HERE / "compile.mjs"), str(src), str(OUT_RC)], check=True)
