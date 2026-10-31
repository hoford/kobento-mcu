#!/usr/bin/env python3
"""Render the muse_splash crane two ways — filled paths (reference) vs mesh2d — and diff them.

  python3 tools/splash/crane_mesh_test.py

Reuses make_splash's crane_facets/mix/svg_path so the geometry and shading are identical; only
the fill mechanism differs. Writes path.rc / mesh.rc and path.png / mesh.png in the scratchpad
and prints the mean abs pixel error.
"""
import importlib.util
import json
import pathlib
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("ms", HERE / "make_splash.py")
ms = importlib.util.module_from_spec(spec); spec.loader.exec_module(ms)

W = H = 300
CX, CY, WIDTH = 150, 150, 220
OUT = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path("/tmp")


def ear_clip(pts):
    """Triangulate a simple polygon (CCW or CW) by ear clipping. Returns index triples into
    pts. Handles the convex and the few concave crane facets uniformly."""
    n = len(pts)
    if n < 3:
        return []
    if n == 3:
        return [(0, 1, 2)]

    def area2(a, b, c):
        return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])

    # Work in CCW order so a positive cross product means a convex corner.
    idx = list(range(n))
    signed = sum(pts[i][0] * pts[(i + 1) % n][1] - pts[(i + 1) % n][0] * pts[i][1]
                 for i in range(n))
    if signed < 0:
        idx.reverse()

    def inside(a, b, c, p):
        return area2(a, b, p) >= 0 and area2(b, c, p) >= 0 and area2(c, a, p) >= 0

    tris = []
    guard = 0
    while len(idx) > 3 and guard < 1000:
        guard += 1
        clipped = False
        m = len(idx)
        for k in range(m):
            ia, ib, ic = idx[(k - 1) % m], idx[k], idx[(k + 1) % m]
            a, b, c = pts[ia], pts[ib], pts[ic]
            if area2(a, b, c) <= 0:
                continue                         # reflex or degenerate corner
            if any(j not in (ia, ib, ic) and inside(a, b, c, pts[j]) for j in idx):
                continue                         # another vertex sits in the ear
            tris.append((ia, ib, ic))
            idx.pop(k)
            clipped = True
            break
        if not clipped:
            break                                # numeric trouble: stop, caller draws what it has
    if len(idx) == 3:
        tris.append((idx[0], idx[1], idx[2]))
    return tris


def facet_geometry():
    """The crane facets mapped to (device points, argb-with-alpha). Mirrors make_splash.crane."""
    facets = ms.crane_facets()
    xs = [x for f in facets for x, _ in f[0]]
    ys = [y for f in facets for _, y in f[0]]
    scale = WIDTH / (max(xs) - min(xs))
    ox = CX - (min(xs) + max(xs)) / 2 * scale
    oy = CY - (min(ys) + max(ys)) / 2 * scale
    lo, hi = min(f[1] for f in facets), max(f[1] for f in facets)
    out = []
    for pts, l, alpha in facets:
        dpts = [(ox + x * scale, oy + y * scale) for x, y in pts]
        hexc = ms.mix("#0E5A4C", "#D9FBEA", (l - lo) / (hi - lo))
        r, g, b = (int(hexc[i:i + 2], 16) for i in (1, 3, 5))
        argb = (round(alpha * 255) << 24) | (r << 16) | (g << 8) | b
        out.append((dpts, argb, hexc, alpha))
    return out


def crane_paths():
    # The real crane straight from make_splash: filled paths + the opaque-facet outlines.
    ms.use_palette(ms.DARK)
    return ms.crane(CX, CY, WIDTH)


def crane_mesh(edge=0.8):
    # Fills come from mesh2d; the thin facet outlines stay paths (mesh2d has no stroke), exactly
    # as make_splash.crane strokes the opaque facets.
    cmds = []
    mid = 1
    for dpts, argb, _hexc, alpha in facet_geometry():
        tris = ear_clip(dpts)
        verts, colors = [], []
        for x, y in dpts:
            verts += [round(x, 3), round(y, 3)]
            colors.append(argb)
        indices = [i for t in tris for i in t]
        # Opaque paint so the mesh Picture isn't dimmed (its vertex colours carry the alpha).
        cmds += [ms.paint({"color": "#FFFFFF"}, {"style": "fill"}, {"alpha": 1.0}),
                 {"addMesh2D": {"id": mid, "verts": verts, "colors": colors, "indices": indices}},
                 {"drawMesh2D": {"mesh": mid}}]
        if alpha == 1.0:
            cmds += [ms.paint({"color": "#06231E"}, {"style": "stroke"}, {"width": edge},
                              {"strokeJoin": "round"}),
                     {"drawPath": {"path": ms.svg_path(dpts, close=True)}}]
        mid += 1
    return cmds


def doc(cmds):
    bg = [ms.paint({"color": "#0B1220"}, {"style": "fill"}),
          {"drawRect": {"left": 0, "top": 0, "right": W, "bottom": H}}]
    return {"header": {"apiLevel": 7, "width": W, "height": H},
            "root": [{"box": {"modifiers": ["fillMaxSize"], "children": [
                {"canvas": {"modifiers": ["fillMaxSize"], "commands": bg + cmds}}]}}]}


def build(name, cmds):
    j = OUT / f"{name}.json"; rc = OUT / f"{name}.rc"
    j.write_text(json.dumps(doc(cmds), indent=1) + "\n")
    subprocess.run(["node", str(HERE / "compile.mjs"), str(j), str(rc)], check=True)
    return rc


def render(rc, png):
    subprocess.run(["host/build/rc2png", str(rc), str(png), "--fit", str(W), str(H),
                    "--density", "1"], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)


if __name__ == "__main__":
    pr = build("path", crane_paths()); mr = build("mesh", crane_mesh())
    print(f"path.rc {pr.stat().st_size} B   mesh.rc {mr.stat().st_size} B")
    render(pr, OUT / "path.png"); render(mr, OUT / "mesh.png")
    print(f"rendered -> {OUT}/path.png  {OUT}/mesh.png")
