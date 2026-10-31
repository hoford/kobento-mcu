#!/usr/bin/env python3
"""Convert a (simple, flat) SVG logo into a RemoteCompose document (JSON for json2rc).

Supports the subset the Kobento logos use: <rect> (with rx/ry), <circle>, <polygon>,
<path> (M/L/H/V/Q/C/Z, abs+rel), element/group transforms (translate/rotate/scale/
skewX/skewY/matrix), and fill/stroke/stroke-width. Every shape's transforms are baked
into absolute coordinates and emitted as drawPath (RC has no skew matrix), so the result
is a plain list of paint+drawPath pairs. A full-canvas background shape becomes the RC
canvas fill.

  svg2rc.py in.svg out.json CANVAS_W CANVAS_H [margin]
"""
import math, re, sys, json
import xml.etree.ElementTree as ET

# ---------- affine 2x3 matrices: (a,b,c,d,e,f) => x'=a*x+c*y+e, y'=b*x+d*y+f ----------
I = (1, 0, 0, 1, 0, 0)
def mul(m, n):  # m after n
    a, b, c, d, e, f = m; A, B, C, D, E, F = n
    return (a*A + c*B, b*A + d*B, a*C + c*D, b*C + d*D, a*E + c*F + e, b*E + d*F + f)
def ap(m, x, y):
    a, b, c, d, e, f = m
    return (a*x + c*y + e, b*x + d*y + f)

def parse_transform(s):
    m = I
    if not s:
        return m
    for name, args in re.findall(r"(\w+)\s*\(([^)]*)\)", s):
        v = [float(t) for t in re.split(r"[\s,]+", args.strip()) if t]
        if name == "translate":
            tx = v[0]; ty = v[1] if len(v) > 1 else 0; t = (1, 0, 0, 1, tx, ty)
        elif name == "scale":
            sx = v[0]; sy = v[1] if len(v) > 1 else sx; t = (sx, 0, 0, sy, 0, 0)
        elif name == "rotate":
            a = math.radians(v[0]); ca, sa = math.cos(a), math.sin(a)
            t = (ca, sa, -sa, ca, 0, 0)
            if len(v) == 3:
                t = mul((1, 0, 0, 1, v[1], v[2]), mul(t, (1, 0, 0, 1, -v[1], -v[2])))
        elif name == "skewX":
            t = (1, 0, math.tan(math.radians(v[0])), 1, 0, 0)
        elif name == "skewY":
            t = (1, math.tan(math.radians(v[0])), 0, 1, 0, 0)
        elif name == "matrix":
            t = tuple(v)
        else:
            t = I
        m = mul(m, t)
    return m

# ---------- shape -> list of absolute output segments (after matrix) ----------
K = 0.5522847498  # circle bezier kappa
def seg_pts(segs):
    for s in segs:
        for i in range(1, len(s), 2):
            yield s[i], s[i+1]

def circle_segs(cx, cy, r, m):
    p = [(cx + r, cy), (cx, cy - r), (cx - r, cy), (cx, cy + r)]  # R,T,L,B
    o = r * K
    cubics = [  # from, c1, c2, to  (clockwise R->T->L->B->R)
        ((cx+r, cy), (cx+r, cy-o), (cx+o, cy-r), (cx, cy-r)),
        ((cx, cy-r), (cx-o, cy-r), (cx-r, cy-o), (cx-r, cy)),
        ((cx-r, cy), (cx-r, cy+o), (cx-o, cy+r), (cx, cy+r)),
        ((cx, cy+r), (cx+o, cy+r), (cx+r, cy+o), (cx+r, cy)),
    ]
    out = [("M",) + ap(m, *cubics[0][0])]
    for _f, c1, c2, to in cubics:
        out.append(("C",) + ap(m, *c1) + ap(m, *c2) + ap(m, *to))
    out.append(("Z",))
    return out

def rrect_segs(x, y, w, h, rx, ry, m):
    rx = min(rx, w/2); ry = min(ry, h/2)
    x2, y2 = x + w, y + h
    ox, oy = rx * K, ry * K
    out = [("M",) + ap(m, x + rx, y)]
    out.append(("L",) + ap(m, x2 - rx, y))
    out.append(("C",) + ap(m, x2 - rx + ox, y) + ap(m, x2, y + ry - oy) + ap(m, x2, y + ry))
    out.append(("L",) + ap(m, x2, y2 - ry))
    out.append(("C",) + ap(m, x2, y2 - ry + oy) + ap(m, x2 - rx + ox, y2) + ap(m, x2 - rx, y2))
    out.append(("L",) + ap(m, x + rx, y2))
    out.append(("C",) + ap(m, x + rx - ox, y2) + ap(m, x, y2 - ry + oy) + ap(m, x, y2 - ry))
    out.append(("L",) + ap(m, x, y + ry))
    out.append(("C",) + ap(m, x, y + ry - oy) + ap(m, x + rx - ox, y) + ap(m, x + rx, y))
    out.append(("Z",))
    return out

def poly_segs(pts, m, close=True):
    out = [("M",) + ap(m, *pts[0])]
    for p in pts[1:]:
        out.append(("L",) + ap(m, *p))
    if close:
        out.append(("Z",))
    return out

_num = re.compile(r"[-+]?(?:\d*\.\d+|\d+\.?)(?:[eE][-+]?\d+)?")
def parse_path(d, m):
    toks = re.findall(r"[MmLlHhVvCcQqZz]|" + _num.pattern, d)
    i = 0; cx = cy = sx = sy = 0.0; out = []; cmd = None
    def num():
        nonlocal i; v = float(toks[i]); i += 1; return v
    while i < len(toks):
        t = toks[i]
        if re.match(r"[A-Za-z]", t):
            cmd = t; i += 1
        rel = cmd.islower(); C = cmd.upper()
        if C == "M":
            x = num(); y = num()
            if rel: x += cx; y += cy
            cx, cy = x, y; sx, sy = x, y
            out.append(("M",) + ap(m, x, y)); cmd = "l" if rel else "L"
        elif C == "L":
            x = num(); y = num()
            if rel: x += cx; y += cy
            cx, cy = x, y; out.append(("L",) + ap(m, x, y))
        elif C == "H":
            x = num();  x = x + cx if rel else x; cx = x; out.append(("L",) + ap(m, cx, cy))
        elif C == "V":
            y = num();  y = y + cy if rel else y; cy = y; out.append(("L",) + ap(m, cx, cy))
        elif C == "C":
            x1 = num(); y1 = num(); x2 = num(); y2 = num(); x = num(); y = num()
            if rel: x1+=cx;y1+=cy;x2+=cx;y2+=cy;x+=cx;y+=cy
            out.append(("C",) + ap(m,x1,y1) + ap(m,x2,y2) + ap(m,x,y)); cx,cy = x,y
        elif C == "Q":
            qx = num(); qy = num(); x = num(); y = num()
            if rel: qx+=cx;qy+=cy;x+=cx;y+=cy
            c1 = (cx + 2/3*(qx-cx), cy + 2/3*(qy-cy))
            c2 = (x + 2/3*(qx-x), y + 2/3*(qy-y))
            out.append(("C",) + ap(m,*c1) + ap(m,*c2) + ap(m,x,y)); cx,cy = x,y
        elif C == "Z":
            out.append(("Z",)); cx, cy = sx, sy
        else:
            i += 1
    return out

def d_string(segs):
    def f(v): return f"{v:.2f}".rstrip("0").rstrip(".")
    parts = []
    for s in segs:
        parts.append(s[0] + " " + " ".join(f(v) for v in s[1:]))
    return " ".join(parts).replace("Z ", "Z").strip()

# ---------- SVG walk ----------
def strip(tag): return tag.split("}")[-1]
def get_shape_segs(el, m):
    t = strip(el.tag)
    if t == "rect":
        x = float(el.get("x", 0)); y = float(el.get("y", 0))
        w = float(el.get("width")); h = float(el.get("height"))
        rx = float(el.get("rx", el.get("ry", 0))); ry = float(el.get("ry", el.get("rx", 0)))
        return rrect_segs(x, y, w, h, rx, ry, m)
    if t == "circle":
        return circle_segs(float(el.get("cx")), float(el.get("cy")), float(el.get("r")), m)
    if t == "ellipse":
        cx = float(el.get("cx")); cy = float(el.get("cy"))
        rx = float(el.get("rx")); ry = float(el.get("ry"))
        return rrect_segs(cx-rx, cy-ry, 2*rx, 2*ry, rx, ry, m)  # ok: full-round rect == ellipse
    if t == "polygon" or t == "polyline":
        pts = [float(v) for v in re.split(r"[\s,]+", el.get("points").strip()) if v]
        pts = list(zip(pts[0::2], pts[1::2]))
        return poly_segs(pts, m, close=(t == "polygon"))
    if t == "path":
        return parse_path(el.get("d"), m)
    return None

def main():
    svg_path, out_json, CW, CH = sys.argv[1], sys.argv[2], float(sys.argv[3]), float(sys.argv[4])
    margin = float(sys.argv[5]) if len(sys.argv) > 5 else 0.06 * min(CW, CH)
    root = ET.parse(svg_path).getroot()
    vb = root.get("viewBox")
    vbw, vbh = (float(vb.split()[2]), float(vb.split()[3])) if vb else (CW, CH)

    shapes = []  # (segs, fill, stroke, stroke_w)
    def walk(el, m):
        m = mul(m, parse_transform(el.get("transform")))
        t = strip(el.tag)
        if t in ("rect","circle","ellipse","polygon","polyline","path"):
            segs = get_shape_segs(el, m)
            if segs:
                fill = el.get("fill", "#000000")
                stroke = el.get("stroke", "none")
                sw = float(el.get("stroke-width", 0))
                shapes.append((segs, fill, stroke, sw, m))
        for ch in el:
            walk(ch, m)
    walk(root, I)

    # detect background: a shape covering ~the whole viewBox
    bg = "#FFFFFF"; fg = []
    for sh in shapes:
        xs = [p[0] for p in seg_pts(sh[0])]; ys = [p[1] for p in seg_pts(sh[0])]
        if not xs: continue
        w = max(xs)-min(xs); h = max(ys)-min(ys)
        if w >= 0.95*vbw and h >= 0.95*vbh and sh[1] != "none" and len(fg)==0:
            bg = sh[1]
        else:
            fg.append(sh)

    # fit all foreground into the canvas (uniform scale, centered)
    xs = [p[0] for sh in fg for p in seg_pts(sh[0])]
    ys = [p[1] for sh in fg for p in seg_pts(sh[0])]
    bx0, by0, bx1, by1 = min(xs), min(ys), max(xs), max(ys)
    bw, bh = bx1-bx0, by1-by0
    s = min((CW-2*margin)/bw, (CH-2*margin)/bh)
    tx = (CW - s*bw)/2 - s*bx0
    ty = (CH - s*bh)/2 - s*by0
    fit = (s, 0, 0, s, tx, ty)

    def refit(segs):
        return [(seg[0],) + tuple(v for i in range(1, len(seg), 2) for v in ap(fit, seg[i], seg[i+1]))
                for seg in segs]

    cmds = [{"paint": {"ops": [{"shader": 0}, {"color": bg}, {"style": "fill"}, {"alpha": 1.0}]}},
            {"drawRect": {"left": 0, "top": 0, "right": CW, "bottom": CH}}]
    for segs, fill, stroke, sw, _m in fg:
        d = d_string(refit(segs))
        if fill and fill != "none":
            cmds.append({"paint": {"ops": [{"shader":0},{"color": fill},{"style":"fill"},{"alpha":1.0}]}})
            cmds.append({"drawPath": {"path": d}})
        if stroke and stroke != "none":
            cmds.append({"paint": {"ops": [{"shader":0},{"color": stroke},{"style":"stroke"},
                                           {"width": round(sw*s,2)},{"strokeJoin":"round"},
                                           {"strokeCap":"round"},{"alpha":1.0}]}})
            cmds.append({"drawPath": {"path": d}})

    doc = {"header": {"apiLevel": 7, "width": int(CW), "height": int(CH)},
           "root": [{"box": {"modifiers": ["fillMaxSize"], "children": [
               {"canvas": {"modifiers": ["fillMaxSize"], "commands": cmds}}]}}]}
    json.dump(doc, open(out_json, "w"), indent=1)
    print(f"wrote {out_json}: {len(fg)} shapes, bg {bg}, {int(CW)}x{int(CH)}")

if __name__ == "__main__":
    main()
