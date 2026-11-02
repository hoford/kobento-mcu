# RcVG study: what ThorVG would need to replace Skia as the RemoteCompose renderer

Date: 2026-10-24. The original study was read-only. Implementation of the M0 harness and plan items 1–2 began the same day on branch `rcvg`; see **§7 Implementation log** at the end.

## 1. Summary

- **ThorVG already covers most of what RC documents draw.** That includes the geometry primitives, paths, stroke caps, joins, miters and dashes, trim paths, linear and radial gradients with pad, repeat and reflect, clips, alpha layers, Gaussian blur, the advanced separable and non-separable blend modes, PNG/JPEG images, TTF text with outline, italic and spacing, and offscreen canvases. I measured the features of 1,311 corpus documents by rendering each one through the real rccore + kobentovg stack (section 2). About 95% of documents use only features that ThorVG handles natively or that kobentovg emulates well.
- **The gaps that matter are small and concentrated.**
  - **Sweep gradients** (1.4% of docs: gauges and clock faces) are drawn as one flat colour.
  - **Glyph fallback** is missing: 1.3% of docs contain symbols or emoji that Roboto lacks, and these draw nothing.
  - **GPOS kerning** is missing. ThorVG reads only the legacy `kern` table, and the bundled Roboto has GPOS only. This affects every text document (62%) relative to Android: widths and centring are off slightly.
  - **Intermediate font weights** (300/500/600) collapse to regular or bold (1.2% of docs).
  - **Hairline strokes** (0.2%) are drawn as 1 *unit* rather than 1 *pixel*. That is a gross error under large viewport scales.
  - The rest each touch 0 to 0.3% of the corpus: text on path, bitmap shaders, Porter-Duff draw modes, inverse fills, extra path effects, 2D meshes, path booleans, perspective.
- **ThorVG upstream has already closed the biggest gap.** ThorVG main (1.2.0-dev, commit 22d2e24, 2026-10-23) adds `tvg::ConicGradient`, which is a sweep gradient. The vendored copy is v1.0.7; the latest tag is v1.1.3. Upgrading or backporting fixes sweep for about +2.6 KB (-Os), measured.
- **Size today (measured, linked, after gc-sections).** ThorVG in the STM32U5 player is **202 KB** (Thumb-2 -O3), of which about 72 KB is the PNG and JPEG decoders. It is 172 KB on the Tab5 (RISC-V -O2) and 173 KB on the ESP32-S3 (Xtensa -O2). kobentovg adds 32 to 36 KB.
- **Comparable Skia subset (measured on arm64, pre-link) is 3.5 MB**, or 4.7 MB with HarfBuzz shaping. That is about 2.5 to 3.4 MB on Thumb-2, **12 to 17 times ThorVG**. The whole STM32 firmware image is 1.92 MB.
- **RcVG estimate.**

  | Scope | ThorVG part | Backend part | Code total | Engineering |
  |---|---|---|---|---|
  | Top-10 plan | ~2,500 new LOC, +25–38 KB | ~550 LOC in the backend, +14 KB | **+40–52 KB** | ~28 engineer-days, ~37 with harness and buffer |
  | Fuller "everything RC can express" | another ~3,000 LOC, +28–40 KB | | | |

  Optional font data adds 25 to 300 KB. Either way RcVG stays at about **0.3 MB, against roughly 2.5–3.4 MB for Skia**.

### Top 10 by bang for the buck

| Rank | Item | RC importance | New LOC (ThorVG + kobentovg) | Est. bytes (Cortex-M, -Os…-O2) | Days |
|---|---|---|---|---|---|
| 1 | Sweep gradient via upstream `ConicGradient` (upgrade or backport) | High | 280 + 15 (0 if rebasing on main) | +3.5–4.7 KB (measured SwFill delta) | 1.5 |
| 2 | Backend quick wins: device-space hairlines, `Picture::filter(Nearest)`, tight text bounds, inverse fill and clip-difference via InvAlpha mask, UTF-16 bounds fix | Medium | 0 + 130 | +3.3 KB | 1.5 |
| 3 | Intermediate font weights (300/500/600 static faces + nearest-weight matching) | Medium-Low | 0 + 30 | +0.8 KB code, +120–160 KB font data | 1 |
| 4 | Glyph fallback chain (symbols font, optional mono emoji) | Medium | 150 + 40 | +2.4–3.0 KB code, +25–40 KB data (+50–100 KB emoji) | 3 |
| 5 | GPOS pair kerning (PairPos fmt 1/2, Extension) in the sfnt loader | Medium-High | 420 + 20 | +4.3–6.0 KB | 4 |
| 6 | Porter-Duff blend methods for drawing | Low | 170 + 15 | +2.6–3.4 KB | 2 |
| 7 | PathMeasure + glyph-by-glyph text on path | Low-Medium | 200 + 90 | +4.1–4.9 KB | 3 |
| 8 | Path effects: discrete, path-dash, sum, compose (geometric) | Low | 380 + 40 | +4.4–5.9 KB | 3 |
| 9 | Image pattern fill (bitmap shader, tile modes, nearest/bilinear) | Low-Medium | 550 + 40 | +7.3–11.8 KB | 5 |
| 10 | 2D vertex meshes (UV + per-vertex colour) on the existing texmap rasterizer | Low | 380 + 130 | +6.7–8.3 KB | 4 |
| | **Total** | | **~2,530 + 550 ≈ 3,080** | **+39–53 KB** | **28** (+3 harness, +20% buffer → ~37) |

## 2. Method

**Sources read**

> Paths without a leading `~` or `scratchpad/` are relative to this repo (kobento-mcu). The
> rest — `~/code/androidx`, `~/Desktop/.../skia`, `remotecompose-experiments/…`,
> `RemoteCompose-Examples`, `corpus.jsonl`, `scratchpad/…` — were the author's local checkouts
> and working files when the study was run (2026-10-24); they are not part of this repo and are
> recorded here only for provenance.

| Component | What I read |
|---|---|
| Backend | `components/kobentovg/src/ThorvgPaintContext.cpp` (all 1,348 lines) and its header |
| Engine interface | `remotecompose-experiments/players/cpp/lib/rccore/include/rccore/PaintContext.h`, `PaintBundle.h` |
| Skia backend | `players/cpp/lib/rcskia/src/SkiaPaintContext.cpp` (1,959 lines) |
| Java reference | `~/code/androidx/compose/remote/remote-core/.../PaintContext.java` and `remote-player-core/.../AndroidPaintContext.java` (androidx commit 9a589fa6, 2026-10-16) |
| Repo docs | `AGENTS.md` (section "The ThorVG backend against Skia") and `third_party/README.md` |
| ThorVG | Vendored v1.0.7 in `third_party/thorvg` (one local patch, in `SfntLoader::metrics`). Upstream main was shallow-cloned into `scratchpad/upstream/thorvg-main`. |
| Skia | Local checkout `~/Desktop/antigravity/rcSkia/skia` (commit 8328d3b2e4, 2025-11-28) and the built `remotecompose-experiments/players/cpp/build/_deps/skia-src/out/Release-arm64/libskia.a` |

**Feature frequency (measured, not inferred)**

1. The catalog's `apis` field is too coarse (for example `paint`, `text`). So I built `scratchpad/rcstats`: a subclass of `ThorvgPaintContext` that records every PaintContext call and decodes every PaintBundle tag. That covers gradient type and tile mode, blend mode, colour filter, path effect type, typeface weight and type, font axes, texture, shader, stroke cap, join and hairline, inverse winding, non-uniform or mirrored scales, and every non-ASCII codepoint drawn.
2. It was built in the scratchpad against the same rccore, kobentovg and ThorVG sources. I ran it over all 1,311 documents in `corpus.jsonl` (downloaded) using the local `~/code/github/RemoteCompose-Examples` `.rc` files: one 400x400 layout, light theme, clock fixed at 1.5 s, two frames. 1,309 rendered and 2 crashed.
3. I ran the same tool over the 109 device documents in `documents`.
4. Missing glyphs were found by parsing Roboto-Regular's `cmap` (formats 4 and 12) and checking every drawn codepoint against it.
5. Raw results: `scratchpad/feature_usage.json`, `feature_counts.txt`, `esp_feature_usage.json`.
6. **Caveat: these are lower bounds.** Features behind conditionals, touch, later animation times or other themes are undercounted.

**Sizes**

- **Linked size per object:** I parsed the GNU ld maps with `scratchpad/scripts/mapsize.py` (sums `.text`, `.rodata`, `.data` and `.literal` per input object, skipping debug sections). Maps used: `stm32/build/rcplayer.map` (Cortex-M33, -O3) and `esp32/firmware/rcplayer/build/{m5stack_tab5,waveshare_s3_lcd128}/rcplayer.map` (-O2).
- **Bytes-per-LOC calibration:** I compiled ThorVG SW-engine files from both v1.0.7 and main with `arm-none-eabi-g++` 15.2 (`-mcpu=cortex-m33 -mthumb -mfpu=fpv5-sp-d16 -mfloat-abi=hard -ffunction-sections`) at -Os and -O2, then ran `arm-none-eabi-size`.
- **LOC** means non-blank, non-comment lines.

### Bytes-per-LOC calibration (Cortex-M33, GCC 15.2)

| ThorVG file (v1.0.7) | LOC | -Os bytes | -O2 bytes | B/LOC -Os | B/LOC -O2 |
|---|---|---|---|---|---|
| tvgSwFill.cpp (gradients) | 596 | 6,878 | 11,744 | 11.5 | 19.7 |
| tvgSwRaster.cpp (+ Texmap/C headers, ~1,909) | 1,909 | 17,062 | 25,124 | 8.9 | 13.2 |
| tvgSwStroke.cpp | 592 | 5,356 | 8,412 | 9.0 | 14.2 |
| tvgSwShape.cpp | 455 | 3,534 | 5,220 | 7.8 | 11.5 |
| tvgSwRle.cpp | 571 | 3,558 | 4,356 | 6.2 | 7.6 |
| tvgSwPostEffect.cpp | 458 | 3,774 | 5,344 | 8.2 | 11.7 |
| tvgSwRenderer.cpp | 701 | 4,670 | 5,468 | 6.7 | 7.8 |
| tvgSwBlendOp.cpp | 206 | 2,682 | 3,572 | 13.0 | 17.3 |
| **Real feature delta:** tvgSwFill v1.0.7 → main (adds ConicGradient and more, +256 raw lines) | ~190 | +2,600 | +3,784 | ~13.7 | ~19.9 |
| kobentovg (STL-heavy backend, linked STM32 -O3 / Tab5 -O2) | 1,369 | — | 36,395 / 31,605 | — | 23–27 |

**Rules used below:**

- **ThorVG engine code:** 9 B/LOC at -Os and 13 B/LOC at -O2.
- **Pixel-loop code** (fills, blenders): 11.5 to 20 B/LOC.
- **kobentovg-style code:** 25 B/LOC.
- **Uncertainty:** each LOC guess is ±40%, and each B/LOC is ±30%.
- **Instruction sets:** Xtensa (S3) and RISC-V (P4) came out within ±20% of Thumb-2 in the maps. For example, SwFill is 12.3 KB on STM32 -O3, 13.2 KB on Tab5 and 12.0 KB on S3.

## 3. RC feature inventory

Legend:

- **Usage %** is the share of 1,311 corpus docs (from my measurement) that exercised the feature in one frame. Where a figure is an opcode count from `catalog/by-op.json`, it is marked "op".
- **ThorVG status:** N = native, P = partial, X = absent.
- **kobentovg handling:** Nat = native, Emu = emulated, Apx = approximated, Ign = ignored.
- **Importance** combines frequency with how visible the error is when the feature is wrong.

### 3.1 Geometry and paths

| Feature | Usage % | ThorVG 1.0.7 status (API) | kobentovg handling | Skia implementation (LOC; arm64 obj) | Est. RcVG new LOC | Est. bytes | Importance | Effort |
|---|---|---|---|---|---|---|---|---|
| drawRect / RoundRect | 88.6 / 29.1 | N `Shape::appendRect(x,y,w,h,rx,ry)` | Nat | SkScan_Path/AAAPath 2,106 | 0 | 0 | High (done) | — |
| drawCircle / Oval | 43.9 / 11.1 | N `appendCircle` | Nat | SkRRect, SkScan | 0 | 0 | High (done) | — |
| drawLine | 39.3 | N (moveTo/lineTo, stroke forced) | Nat | SkScan_Hairline/Antihair 1,404 | 0 | 0 | High (done) | — |
| drawArc / drawSector | 8.4 / 1.1 | P (no arc API) | Emu: ≤90° cubic segments (`appendArc`) | SkPath::arcTo, SkGeometry 2,854 | 0 | 0 | Medium (done) | — |
| drawPath (cubic/quad/close) | 18.0 | N | Nat | SkPath/SkPathBuilder | 0 | 0 | High (done) | — |
| Conic segments (weighted quads) | rare | X | Apx: rational quad → one cubic (`k=4/3·w/(1+w)`); close for arcs | SkConic in SkGeometry | 0 (optionally ~40 for split-and-approximate) | ~0.5 KB | Low | 0.5 d |
| drawPath(start,end) trim | 0 measured | N `Shape::trimpath` | Nat | SkContourMeasure::getSegment 676 | 0 | 0 | Low (done) | — |
| Even-odd fill | 0.1 | N `FillRule::EvenOdd` | Nat | SkScan_Path | 0 | 0 | Low (done) | — |
| Inverse fills (winding 2/3) | 0.1 | X | Apx: treated as EvenOdd or NonZero (wrong area) | SkScan_Path inverse spans | rc 25 (InvAlpha mask over a clip-sized rect); or TVG ~80 in tvgSwRle (complement spans) | 0.6 KB / 0.9 KB | Low | 0.5 d |
| Path tween (drawTweenPath, PATH_TWEEN) | 0.5 | X | Emu: point lerp, same as `SkPath::interpolate` precondition | SkPath::interpolate | 0 | 0 | Low (done) | — |
| Path combine (PATH_COMBINE, union/intersect/diff/xor/rdiff) | 0 (op uncovered) | X | Ign (`combinePath` not overridden) | **pathops 16,000 LOC (cpp), 188 KB arm64** | Custom Vatti/Martinez on flattened polylines ~1,200; or Clipper2 (BSL-1.0, ~5k LOC) | 11–16 KB custom; ~40–60 KB Clipper2 | Low now | 10–15 d |
| 2D meshes (ADD/DRAW_MESH_2D, MATRIX_FROM_MESH_2D) | 0 (op uncovered) | P: internal triangle texmap (`tvgSwRasterTexmap.h`, 410 LOC); I believe the 0.x `Picture::mesh()` was dropped by 1.0 (not verified) | Ign | SkVertices + SkDraw_vertices 568 + raster pipeline | 380 + rc 130 | 6.7–8.3 KB | Low | 4 d |

### 3.2 Strokes and path effects

| Feature | Usage % | ThorVG status | kobentovg | Skia (LOC) | RcVG LOC | Bytes | Imp. | Effort |
|---|---|---|---|---|---|---|---|---|
| Stroke style / width | 37.6 | N `strokeWidth` | Nat | SkStroke 1,564 | 0 | 0 | High (done) | — |
| Caps: round / square | 22.1 / 0.1 | N `strokeCap` | Nat | SkStroke | 0 | 0 | High (done) | — |
| Joins: round / bevel, miter limit | 1.8 / –, 0 | N `strokeJoin`, `strokeMiterlimit` | Nat | SkStrokerPriv | 0 | 0 | Medium (done) | — |
| Fill-and-stroke | 0.3 | N (fill + stroke on one Shape) | Nat | SkPaint::kStrokeAndFill | 0 | 0 | Low (done) | — |
| Hairline (width 0 = 1 device px at any CTM) | 0.2 (viewport-chart/flip docs) | X (width is in local units) | Apx: width 1 *unit*, so a 50x viewport scale draws a 50 px line | SkScan_Hairline/Antihair 1,404 | rc ~40 (pre-transform the path to device space, stroke 1 px under identity) or TVG ~30 "non-scaling stroke" | 1.0 KB | Medium (rare, gross error) | 0.5 d |
| Dash (`PATH_EFFECT` type 1) | 0.2 | N `strokeDash(pattern,cnt,offset)` | Nat | SkDashPathEffect + SkDashPath 678 | 0 | 0 | Low (done; check zero-length dash dots with round caps) | — |
| Discrete (type 2), PathDash (3), Sum (4), Compose (5) | 0.15 (paths-demos ×2) | X | Ign (dash cleared, others dropped) | SkDiscretePathEffect 142, Sk1DPathEffect 212, SkPathEffect sum/compose 161, SkDashPath 678 | 380 (path→path transforms incl. a geometric dasher for compose; Skia's LCG for discrete parity) + rc 40 | 4.4–5.9 KB | Low | 3 d |
| Corner path effect | not in RC wire format (`PaintPathEffects` ids 1–5) | X | n/a | SkCornerPathEffect 146 | 0 | 0 | — | — |

### 3.3 Paint, shaders, colour

| Feature | Usage % | ThorVG status | kobentovg | Skia (LOC; arm64) | RcVG LOC | Bytes | Imp. | Effort |
|---|---|---|---|---|---|---|---|---|
| Solid colour + paint alpha | 29.7 alpha, 8.8 translucent | N `fill(r,g,b,a)`, `opacity` | Nat | — | 0 | 0 | High (done) | — |
| Linear gradient (+ repeat/mirror) | 2.4 (+0.2/0.2) | N `LinearGradient`, `FillSpread::Repeat/Reflect` | Nat (stops clamped to [0,1] like Skia) | SkLinearGradient + SkGradientBaseShader 961 | 0 | 0 | Medium (done) | — |
| Radial gradient | 0.5 | N `RadialGradient` | Nat | SkRadialGradient | 0 | 0 | Low (done) | — |
| **Sweep gradient** | **1.4** (pressure gauge, clock faces, rings) | **X in 1.0.7; N in main as `ConicGradient` (experimental)** | **Apx: flat middle-stop colour** | SkSweepGradient 140 + base 961; 2.5 KB + 15.4 KB arm64 | 280 backport (0 if upgrading) + rc 15 | **+2.6 KB -Os / +3.8 KB -O2 measured** | **High** | 1.5 d |
| Gradient local matrix (SHADER_MATRIX) | 0 measured | N `Fill::transform` | Nat | SkLocalMatrixShader | 0 | 0 | Low (done) | — |
| Decal tile mode | blur layers only (engine does not route it) | X | n/a | SkTileMode::kDecal | ~20 in SwFill | 0.3 KB | Low | 0.5 d |
| Bitmap shader (TEXTURE, tile X/Y, filter) | 0.3 (texture demos) | X (no pattern fill) | Ign (`i += 3`) | SkImageShader 703 + SkBitmapProcState 1,160; 10.7 KB arm64 | 550 + rc 40 | 7.3–11.8 KB | Low-Medium | 5 d |
| Runtime shaders (AGSL) | 0.8 | — | Ign | **out of scope** | — | — | — | — |
| Colour filter (blend-mode colour filter) | 0 in corpus | X as a paint property (SceneEffect Fill/Tint only) | Emu: exact on solid colours and per-image filtered copies; approximate on gradients (stops filtered, then interpolated) | SkBlendModeColorFilter 100 | 0 (optionally a TVG span-level colour filter ~150) | — | Low (done) | — |
| Advanced blend modes (multiply … luminosity, plus) | 0 in corpus | N `Paint::blend(BlendMethod)` | Nat | SkBlendMode 295 + raster pipeline stages | 0 | 0 | Low (done) | — |
| Porter-Duff draw modes (Clear, Src, SrcIn/Out/Atop, Dst*, Xor) | 0.1 (SrcOut in bitmap-drawing demo) | X (BlendMethod has no PD modes) | Apx: drawn SrcOver | SkBlendMode + `SkRasterPipeline_opts.h` (5,443 LOC shared) | 170 + rc 15 | 2.6–3.4 KB | Low | 2 d |
| Anti-alias off | 0 | X (always AA) | Ign | SkScan non-AA paths | ~40 in SwRle (threshold coverage) | 0.4 KB | Low | 0.5 d |

### 3.4 Text

| Feature | Usage % | ThorVG status | kobentovg | Skia/Android | RcVG LOC | Bytes | Imp. | Effort |
|---|---|---|---|---|---|---|---|---|
| drawTextAnchored / drawTextRun / CORE_TEXT layout (via measurement) | 54.1 / 14.8 / 12.7 (op) | N `Text` (TTF via sfnt loader) | Nat, with workarounds: ×0.75 size (pt@96 dpi), top-left box anchoring, gradient text via an alpha mask (text fill is in font units) | SkFont/SkTextBlob; Android Minikin + HarfBuzz | 0 (API polish upstream: px sizes, baseline anchor, Fill in user space) | 0 | High (done) | — |
| **Kerning (GPOS PairPos)** | **all text docs (~62%) vs Android** | P: legacy `kern` table only (`tvgTtfReader.cpp:283`); Roboto ships GPOS, no `kern` | Ign | Android: HarfBuzz GPOS. rcskia `MakeFromString`: **no kerning either** | 420 + rc 20 | 4.3–6.0 KB | Medium-High | 4 d |
| Font weight 700 / 600 / 500 / 300; italic | 6.4 / 0.7 / 0.3 / 0.2; 0.2 | P: one face per `font()` name; synthetic italic `italic(shear)` | Apx: ≥600 → Roboto-Bold, otherwise Regular; italic sheared | SkFontMgr style match | rc 30 + 2–3 static faces (data) | +0.8 KB code, +120–160 KB data | Medium-Low | 1 d |
| Variable-font axes (FONT_AXIS) | 0.1 | X (no fvar/gvar) | Ign (Skia backend ignores it too) | FreeType ttgxvar.c 4,895 | ~900 | 8–12 KB | Low | 8 d |
| **Glyph fallback (symbols, emoji)** | **1.3** (⚡ ♥ ▲ ✓ → ❄ ☀ ⏮⏸⏭ 🔥 📈 …); 0 of the 109 device docs | X (no fallback list, no has-glyph query) | Ign (missing glyphs render nothing) | Android font fallback chain | 150 + rc 40 | 2.4–3.0 KB + 25–40 KB symbols subset (+50–100 KB mono emoji) | Medium | 3 d |
| Colour emoji (COLR/CBDT) | subset of the above | X | — | FreeType ttcolr 1,923 / ttsbit 1,722 | COLRv0 ~250 | 2.3–3.3 KB + data | Low | 3 d |
| Text stroke (outline) | 1.4 | N `Text::outline(w,r,g,b)` (no alpha or gradient on the outline) | Nat | SkPaint stroke on glyph paths | ~20 | 0.3 KB | Low | — |
| Letter spacing (TEXT_STYLE) | engine skips it (`LayoutOperations.h:292`) | N `Text::spacing(letter,line)` | n/a | Paint.setLetterSpacing | rc 10 once the engine passes it | — | Low | — |
| Text on path | 0.2 (clock docs) | X | Apx: drawn straight at the first moveTo | SkContourMeasure 676 + RSXform blob | 200 (PathMeasure) + rc 90 | 4.1–4.9 KB | Low-Medium | 3 d |
| Text on circle (DRAW_TEXT_ON_CIRCLE) | 0 (op uncovered) | X | n/a: engine stub (`StubOperations.h:78`) | Android: Path.addArc + drawTextOnPath | rc 40 on PathMeasure (+ engine work) | 1 KB | Low | 0.5 d |
| Tight text bounds (getTextBounds, TEXT_MEASURE) | 0.1 (op) | N `GlyphMetrics.min/max` | Apx: font ascent/descent; indexes the UTF-8 string with UTF-16 indices (bug) | Paint.getTextBounds = ink bounds | rc 30 | 0.8 KB | Low | in item 2 |
| RTL / bidi / complex scripts | 0 (no Arabic/Indic/CJK found) | X | Ign (`rtl` flag dropped) | HarfBuzz 154k LOC, 1.2 MB arm64 (shaping only) | out of MCU scope | — | Low | — |
| Bitmap fonts (DATA_BITMAP_FONT) | 0 (op uncovered) | n/a (engine draws via bitmaps) | — | — | 0 | 0 | Low | — |

### 3.5 Clips, layers, transforms, images

| Feature | Usage % | ThorVG status | kobentovg | Skia | RcVG LOC | Bytes | Imp. | Effort |
|---|---|---|---|---|---|---|---|---|
| clipRect | 97.6 (layout) | N `Paint::clip(Shape*)` in a nested `Scene` | Nat | SkRasterClip/AAClip | 0 | 0 | High (done) | — |
| roundedClipRect / clipPath | 2.9 / 0.8 | N | Emu (per-corner cubic path) / Nat | SkRRect / SkAAClip | 0 | 0 | Medium (done) | — |
| clipPath regionOp ≠ intersect | 0 | P (`MaskMethod::InvAlpha` possible) | Ign (op unused) | SkClipOp::kDifference | rc 15 | 0.4 KB | Low | in item 2 |
| saveLayerAlpha | 0.2 | N `Scene::opacity` | Nat | saveLayerAlpha | 0 | 0 | Medium (done) | — |
| Blur layer (graphicsLayer blur) | 0 | N `SceneEffect::GaussianBlur` | Nat (sigma × CTM scale) | SkBlurImageFilter + SkBlurEngine 1,267 | 0 | 0 | Low (done; check radius-vs-sigma against Android) | — |
| Elevation shadows (ambient/spot) | 0 (engine does not route graphicsLayer shadow attrs) | P `SceneEffect::DropShadow` (approximation) | n/a | SkShadowUtils + SkShadowTessellator 1,683 | rc 60 approximation | 1.5 KB | Low | 1 d |
| drawToBitmap | 0.2 | N (second `SwCanvas`) | Emu | SkSurface | 0 | 0 | Low (done) | — |
| translate / rotate / scale / skew | 99 / 13 / 7.5 / 0 | N (3x3 affine) | Nat | SkMatrix | 0 | 0 | High (done) | — |
| Perspective (graphicsLayer rotationX/Y, cameraDistance) | 0 (engine does not implement it) | X (SW engine ignores e31/e32: no reference in `cpu_engine/`) | n/a | SkM44 + SkMatrix 1,619 + pipeline perspective stages | ~400 (flatten, then project outlines; perspective-correct texmap) | 4–6 KB | Low now | 5 d |
| drawBitmap / drawBitmapInt (src rect) | 0.5 | N `Picture::load(raw)`, `size()` | Nat / Emu (clip + scale) | SkCanvas::drawImageRect | 0 | 0 | Low (done) | — |
| Bitmap filter quality (nearest) | 0.1 | **N `Picture::filter(FilterMethod::Nearest)`** | Ign (tags 10/17 skipped) | SkSamplingOptions | rc 5 | 0.1 KB | Low | in item 2 |
| PNG / JPEG decode | images 0.4 | N (lodepng, jpgd; ~70 KB linked) | Nat | SkCodec + libpng/zlib 295 KB arm64 | 0 | 0 | Low (done) | — |
| WebP / GIF / animated | ~0 | WebP loader exists (not compiled in); no GIF | Ign | SkCodec animated | — | — | Low | — |
| 3D meshes (Paint3D) | 0.1 | n/a: rccore software 3D, blitted as an image | Emu | — | out of scope | — | — | — |
| Antialiasing exactness | all | ThorVG: exact-area coverage RLE (ftgrays-derived, `tvgSwRle.cpp`) | — | Skia: analytic AA (AAA) / supersampling | not pursued (sub-LSB to a few LSB edge differences; bit-parity is not a realistic goal) | — | — | — |

### 3.6 Device-document check

The same tool run over `documents` (109 docs) found:

- **Used:** sweep 2, linear gradient 2, no missing glyphs, clipPath 1, no blend or colour-filter modes, no textures.
- **Interpretation:** the device documents were authored around the current gaps, so they understate demand.

## 4. Size totals

| Item | Source LOC | Size |
|---|---|---|
| ThorVG v1.0.7 subset compiled by the players (cpu_engine 6,386; renderer core 2,319 cpp + 2,468 hdr; common ~815 hdr + cpp; sfnt 1,495; png 2,144; jpg 2,017) | **~17.6k** | **202 KB** linked STM32U5 (Thumb-2 -O3; code 197 KB). Without PNG/JPEG decoders ~130 KB. **172 KB** Tab5 (RISC-V -O2), **173 KB** ESP32-S3 (Xtensa -O2). arm64 -O3 pre-link 278 KB. |
| Biggest ThorVG objects (STM32, linked) | | Jpgd 48.9 KB, SwRaster 30.4, LodePng 21.3, SwFill 12.3, SwStroke 9.4, Paint 9.1, SfntLoader 6.9, SwShape 6.7, SwRenderer 6.4, SwPostEffect 6.2, Render 6.0, SwRle 5.3, SwBlendOp 4.8 |
| kobentovg (backend) | 1,369 | 36 KB STM32, 32 KB Tab5, 34 KB S3 |
| rccore (engine, for context) | — | 403 KB STM32 |
| Whole STM32U5 firmware image | — | 1.92 MB (4 MB flash) |
| **Skia, comparable CPU subset** (Release arm64 `libskia.a`, pre-link): core 1.29 MB + other libskia (effects, shaders, utils) 0.36 + pathops 0.19 + SkSL non-GPU 0.72 + FreeType 0.56 + FT port 0.07 + PNG/zlib 0.30 | src/core 77.5k, effects 5.6k, shaders 3.6k, opts 6.5k, pathops 16k, plus FreeType/HarfBuzz | **3.49 MB arm64** (4.72 MB with HarfBuzz shaping, 1.23 MB; hb-subset's 1.43 MB excluded). Scaled by ThorVG's measured arm64-pre-link → Thumb-linked ratio (0.73): **≈2.5 MB, or 3.4 MB with HarfBuzz**. |
| **RcVG, top 10** | ThorVG +2,530, kobentovg +550 | ThorVG 202 → **227–240 KB**; kobentovg 36 → ~50 KB; plus optional font data 25–300 KB |
| **RcVG, fuller scope** (adds perspective, custom path booleans, variable fonts, COLRv0 emoji, GSUB ligatures, AA-off, shadows, text on circle) | ThorVG +~5,600 total | ThorVG ≈ **255–280 KB**; renderer + backend ≈ 310–330 KB |

Skia caveats:

- Some of the Skia figure is code that gc-sections would drop. The 0.73 ratio already folds in ThorVG's own gc effect, but Skia's dead-code share may differ.
- Recent Skia makes SkSL hard to remove from a CPU build (I did not verify that a no-SkSL build is possible).

Even the lower bound (2.8 MB arm64 without SkSL or HarfBuzz, about 2 MB Thumb) is larger than the entire current STM32 firmware.

## 5. Top-10 build plan

**Ranking rule:** importance (High 3, Medium 2, Low 1, weighted by frequency and how visible the error is) divided by engineer-days. Ties go to items that unblock later ones; for example PathMeasure comes before path effects.

### Milestone 0: parity harness (3 days, before any feature)

- **Reference renderer.** `tools/sweep_skia.sh` expects `host/skia-ref/tools/rc2image/rc2image`, which is not present locally. `RemoteCompose-Examples/tools/compare_players.py` points at the old `~/code/github/rcExperiments` path. Rebuild rc2image from `remotecompose-experiments/players/cpp` (the Skia build already exists in `build/_deps/skia-src`).
- **Per-feature document lists.** Generate them from `scratchpad/feature_usage.json`. For example, sweep gives 18 docs: `advanced-samples/pressure-gauge`, `androidx-demos/clock-demo2-jclock2`, and so on.
- **Run per feature.** Run `sweep_skia.sh` plus `compare.py` (mean absolute error) over each list before and after each change. Also run a full-corpus regression with `rc2png --time` to catch collateral changes.
- **Android ground truth.** Where Skia and Android differ (kerning, fallback fonts, blur radius), capture Android reference PNGs once; the catalog has no reference renders.
- **Unit images.** Extend `host/paintcheck` with pixel checks per feature: sweep seam and angle direction, PD mode truth tables, hairline width at scale 1/10/100, kerning pairs ("AV", "To", "Wa").

### Plan items

| # | Feature | Approach (files to touch) | Skia code to study (BSD-3) | Tests | Days | Upstream? |
|---|---|---|---|---|---|---|
| 1 | **Sweep gradient** | Prefer moving the vendored ThorVG 1.0.7 → 1.1.3 or main, which brings `ConicGradient` (`inc/thorvg.h`, `tvgFill.h/.cpp`, `cpu_engine/tvgSwFill.cpp` `_prepareConic` / `_fillConic`, ~200 lines). Otherwise backport those pieces (~280 LOC). In kobentovg `makeFill()`, add `SWEEP → ConicGradient::gen()->conic(cx,cy,0)` and delete the middle-stop fallback in `styled()`. Re-verify the vendored `SfntLoader::metrics` patch and the 0.75 text-size quirk after the upgrade. | `SkSweepGradient.cpp` (t = atan2 / 2π, start at +x, clockwise in y-down), `SkGradientBaseShader.cpp` (stop clamping) | 18 sweep docs vs Skia; seam AA (ThorVG anti-aliases the seam, Skia does not: expect a 1 px difference); stops outside [0,1] | 1.5 | Already upstream (experimental API: pin a commit) |
| 2 | **Backend quick wins** | kobentovg only. (a) Hairline: when width==0, transform path points by `mMatrix`, push with identity, stroke 1 px. (b) `FILTER_BITMAP` / `IMAGE_FILTER_QUALITY` → `Picture::filter(Nearest)`. (c) `getTextBounds` from `GlyphMetrics.min/max` with UTF-16→UTF-8 index conversion. (d) Inverse fills and clip difference: full-clip rect `mask(path, MaskMethod::InvAlpha)`. | `SkScan_Antihair.cpp`, `SkFont::measureText` bounds | viewport-* docs, winding demo, TEXT_MEASURE doc; full-corpus no-regression | 1.5 | No (backend) |
| 3 | **Font weights** | Weight → nearest registered face (CSS matching rules) in `fontName()`; register Light/Medium (and SemiBold) Latin subsets; the host and boards choose which faces to ship. | `SkFontStyleSet::matchStyle` / CSS font matching | weight300/500/600 docs vs Android | 1 | No |
| 4 | **Glyph fallback** | ThorVG: `Text::font()` accepts an ordered fallback list (or adds `Text::hasGlyph`). In `loaders/sfnt/tvgSfntLoader.cpp`, resolve each codepoint to the first face whose cmap has it, and keep per-glyph face plus scale for mixed ascent. kobentovg: fallback list `[Roboto, NotoSansSymbols2-subset, (NotoEmoji-mono-subset)]`. | Android `FontFallback` / Minikin (concept); `SkFontMgr::matchFamilyStyleCharacter` | the 17 missing-glyph docs (§3.4); emoji in render/dashboard-card, music-player | 3 | Yes (generic need; Lottie text benefits) |
| 5 | **GPOS kerning** | New `loaders/sfnt/tvgOtLayout.cpp`: parse GPOS ScriptList/FeatureList ('kern', DFLT/latn) → LookupList type 2 (PairPos fmt 1 glyph pairs, fmt 2 class pairs) and type 9 extensions; Coverage fmt 1/2, ClassDef fmt 1/2, ValueRecord XAdvance (scaled). Lazy, in place over font data (no RAM tables), binary search per pair. Hook it where `tvgTtfReader` applies `kern` today. Expose it through `Text::metrics(ch, gm, &next)` so kobentovg `advanceOf` agrees with drawing. | HarfBuzz `OT/Layout/GPOS/PairPosFormat1.hh` (271), `PairPosFormat2.hh` (413), `PairSet.hh`, `ValueFormat.hh` (MIT) | kerning-pair unit images; layout docs (alignment-and-justification) vs Android; decide whether rcskia should kern too (it does not) | 4 | Yes |
| 6 | **Porter-Duff draw modes** | `inc/thorvg.h` BlendMethod += Clear, Src, SrcIn, SrcOut, SrcAtop, DstOver, DstIn, DstOut, DstAtop, Xor. `cpu_engine/tvgSwBlendOp.cpp`: premultiplied per-pixel ops. `tvgSwRenderer.cpp::blend()`: dispatch. Check the coverage lerp in `_rasterBlending*` so modes only affect covered pixels (Skia's bounded semantics). kobentovg `toBlend()`. | `SkBlendMode.cpp`, `SkBlendModePriv.h`, PD stages in `SkRasterPipeline_opts.h` | 12-mode truth-table grid in paintcheck; bit-draw2 doc | 2 | Maybe (Canvas2D-style composite ops; ThorVG dropped its 0.x composite modes) |
| 7 | **PathMeasure + text on path** | New `common/tvgPathMeasure.{h,cpp}` on `tvgMath` `Bezier::length / at / split`: contour lengths, `posTan(d)`, `segment(d0,d1)`. kobentovg `drawTextOnPath`: per glyph, place at `d + adv/2`, rotate to the tangent, emit one `Text` per glyph (or upstream a `Text::path(Shape*)` layout). Also prepares MATRIX_FROM_PATH and DRAW_TEXT_ON_CIRCLE once rccore adds the hooks (engine owners). | `SkContourMeasure.cpp` (676 with SkPathMeasure); RSXform placement in `SkiaPaintContext.cpp:671-735` | androidx-demos/clock, androidx-kotlin/01dslclock vs Skia | 3 | PathMeasure API: maybe; text on path: probably (Lottie text path) |
| 8 | **Path effects** | `rcvg/PathEffect.cpp` (outside the raster engine): discrete (Skia algorithm and LCG seed, for parity), path-dash (translate/rotate/morph styles, via PathMeasure), sum (draw twice), compose (outer∘inner; needs a geometric dash, using PathMeasure segment). The output is a new `tvg::Shape` path. | `SkDiscretePathEffect.cpp` 142, `Sk1DPathEffect.cpp` 212, `SkPathEffect.cpp` 161, `SkDashPath.cpp` | paths-demos (2 docs) vs Skia | 3 | No (Android-specific semantics) |
| 9 | **Image pattern fill** | New `tvg::ImageFill : Fill` (raw ARGB + w/h + tileX/tileY {clamp, repeat, mirror, decal} + filter + transform). SW: extend `SwFill` with an image variant in `tvgSwFill.cpp` (inverse-mapped span sampler; reuse the bilinear helpers in `tvgSwImage/Raster`); add raster span variants beside `_rasterLinearGradient*` in `tvgSwRaster.cpp`. kobentovg TEXTURE tag → `ImageFill`. | `SkImageShader.cpp` 703, `SkBitmapProcState*.cpp` 1,160 | texture-demo-* (4 docs); tile-mode grid in paintcheck | 5 | Yes (SVG `<pattern>` is unsupported in ThorVG today) |
| 10 | **2D meshes** | Public `tvg::Mesh` (or `Picture::mesh(vertices, uv, colors, indices)`) feeding `tvgSwRasterTexmap.h` triangles; add per-vertex colour (barycentric) and untextured solid mode; blend modes via the existing blender. kobentovg `setMesh / drawMesh / matrixFromMesh` (matrixFromMesh is pure math in the backend). | `SkVertices.cpp`, `SkDraw_vertices.cpp` (568) | synthesize test docs (no corpus doc uses meshes); compare with rcskia `drawVertices` | 4 | Maybe (it existed in 0.x, I believe) |

### Milestones

| Milestone | Contents | Cumulative effort |
|---|---|---|
| M0 | Parity harness | ~3 d |
| M1 "visible wins" | Items 1–3 | ~7 d. Fixes every gauge/clock sweep and all hairline/viewport docs; no new ThorVG code if upgrading |
| M2 "text parity" | Items 4–5 | ~14 d. Fallback glyphs and Android-matching widths; re-run layout-heavy docs |
| M3 "paint completeness" | Items 6, 9 | ~21 d |
| M4 "path toolkit" | Items 7, 8, 10 | ~31 d; plus a 20% buffer for upstream review and board testing (Tab5 PSRAM performance, S3 2 MB PSRAM) → **~37 d** |
| Later, if the corpus grows | Path booleans (10–15 d), perspective (5 d, after rccore graphicsLayer support), variable fonts (8 d), COLRv0 (3 d), shadows (1 d), AA-off (0.5 d) | |

### Upstream vs fork

**Upstream into ThorVG (MIT):**

- ConicGradient (already there).
- GPOS kerning.
- Font fallback.
- Image pattern fill.
- Non-scaling (hairline) stroke, if done in ThorVG rather than the backend.
- PathMeasure.
- Possibly Porter-Duff modes and meshes (both were removed or never present; expect discussion).

**Keep in RcVG** (a thin `rcvg` library beside ThorVG plus the kobentovg backend):

- Android/Skia-specific semantics: path effects with Skia's LCG, colour-filter emulation, RC text anchoring, the weight-matching table, RC font data.

Track ThorVG main with a small patch queue rather than a hard fork. Skia code is BSD-3 and HarfBuzz is MIT, so porting algorithms is license-compatible. Keep the copyright notices in files that are direct ports.

## 6. Risks and open questions

1. **ThorVG upgrade risk.** 1.0.7 → main touches text sizing, the vendored metrics patch and SwRaster (1,813 → 1,437 lines; it was restructured into `tvgSwRasterCore`/`Simd`). `ConicGradient` is marked experimental. Mitigation: pin a commit and run the M0 sweep over the whole corpus before and after.
2. **Which reference counts.**
   - **Kerning:** Android (HarfBuzz) kerns; rcskia does not.
   - **Fallback fonts:** these differ per platform.
   - **Blur:** Android takes a radius; rccore passes a value used as sigma. I did not verify which is right.

   Decide whether Android or rcskia is ground truth per feature, or the compare tooling will penalise correct fixes.
3. **Frequency is a lower bound.** One frame, one size, one theme. Interactive or conditional content (for example graphicsLayer blur at 0 docs here, and 2 docs carry MODIFIER_GRAPHICS_LAYER) may use more. The corpus is also authored by people who know the current players' limits; the device documents show this clearly (no missing glyphs at all).
4. **Engine-side blockers (not RcVG).** These need rccore work by its owners before RcVG support matters: MATRIX_FROM_PATH (stub), DRAW_TEXT_ON_CIRCLE (stub), graphicsLayer rotationX/Y, cameraDistance and elevation (not routed), TEXT_STYLE letter spacing (skipped), PATH_COMBINE (no backend method in C++ beyond a no-op default).
5. **MCU performance.**
   - Pattern fills and per-glyph text-on-path paints add per-pixel and per-object cost on PSRAM-bound targets (Tab5 is about 144 MB/s).
   - GPOS lookups add per-pair binary searches; cache per string in kobentovg's width cache, which already exists.
6. **Flash budget for fonts.** Each extra Latin face is about 50–80 KB; a symbol subset is about 25–40 KB; monochrome emoji subsets are about 50–100 KB; colour emoji is impractical on MCUs. These numbers are my estimates from Roboto's table sizes (`glyf` 197 KB of 349 KB), not measured subsets.
7. **Estimate uncertainty.** LOC estimates are ±40% and bytes/LOC ±30%. The only feature-level measurement is the SwFill delta for ConicGradient (+2.6 KB -Os / +3.8 KB -O2), which also contains unrelated main-branch changes to that file, so it is an upper bound. Path booleans are the least certain: 10–15 days and 11–60 KB depending on the algorithm or library.
8. **Bit-exact parity with Skia is not a goal.** The AA algorithms differ (ThorVG exact-area RLE vs Skia analytic AA), so measure success by `compare.py` mean error per feature, not by zero difference.
9. **Corpus crashes.** Two corpus documents crashed the stack with exit code 0 and no output (logged as `ERR:crash0` in `feature_usage.json`). This is worth a look by the rccore owners.

## Appendix: corpus feature counts (1,311 docs, one frame)

From `scratchpad/feature_counts.txt`:

| Feature | Docs | Share |
|---|---|---|
| matrixTranslate | 1299 | 99.1% |
| clipRect | 1279 | 97.6% |
| drawRect | 1162 | 88.6% |
| drawTextAnchored | 709 | 54.1% |
| drawCircle | 576 | 43.9% |
| drawLine | 515 | 39.3% |
| style:stroke | 493 | 37.6% |
| paint:alpha | 390 | 29.7% |
| drawRoundRect | 381 | 29.1% |
| capRound | 290 | 22.1% |
| drawPath | 236 | 18.0% |
| non-ASCII text | 232 | 17.7% |
| drawTextRun | 194 | 14.8% |
| matrixRotate | 171 | 13.0% |
| drawOval | 145 | 11.1% |
| translucent colour | 116 | 8.8% |
| drawArc | 110 | 8.4% |
| matrixScale | 98 | 7.5% |
| weight 700 | 84 | 6.4% |
| non-uniform scale | 53 | 4.0% |
| roundedClipRect | 38 | 2.9% |
| linear gradient | 31 | 2.4% |
| joinRound | 23 | 1.8% |
| text stroke | 18 | 1.4% |
| **sweep gradient** | **18** | **1.4%** |
| missing glyphs | 17 | 1.3% |
| drawSector | 15 | 1.1% |
| AGSL shader | 11 | 0.8% |
| clipPath | 10 | 0.8% |
| weight 600 | 9 | 0.7% |
| radial gradient | 7 | 0.5% |
| drawBitmap | 7 | 0.5% |
| drawTweenPath | 6 | 0.5% |
| bitmap texture | 4 | 0.3% |
| fill-and-stroke | 4 | 0.3% |
| weight 500 | 4 | 0.3% |
| dash | 3 | 0.2% |
| hairline | 3 | 0.2% |
| repeat tile | 3 | 0.2% |
| weight 300 | 3 | 0.2% |
| italic | 3 | 0.2% |
| typeface by name | 3 | 0.2% |
| mirror tile | 2 | 0.2% |
| path effects 2/3/4 | 2 | 0.2% |
| drawTextOnPath | 2 | 0.2% |
| drawToBitmap | 2 | 0.2% |
| saveLayerAlpha | 2 | 0.2% |
| mirror matrix | 2 | 0.2% |
| Porter-Duff SrcOut | 1 | 0.1% |
| inverse fill | 1 | 0.1% |
| even-odd | 1 | 0.1% |
| font axis | 1 | 0.1% |
| filterBitmap | 1 | 0.1% |
| capSquare | 1 | 0.1% |

**Not seen at all:** colour filters, advanced blend modes, blur layers, meshes, path combine, anti-alias off, skew, clip difference.

## 7. Implementation log

Started 2026-10-24 on branch `rcvg`. The gold standard is RemoteCompose on an Android
device; where both agree, Skia (`rc2image`) is the stand-in reference and is exact for the
sweep gradient (both are `SkSweepGradient`).

### M0: parity harness (done)

- **Skia reference renderer.** Built `host/skia-ref/tools/rc2image/rc2image` inside this
  worktree by compiling `../remotecompose-experiments/players/cpp/tools/rc2image/main.cpp`
  against that tree's already-built static libraries (`librcskia.a`, `librccore.a`,
  `libskia.a`, `libminiz.a`) plus the macOS frameworks. Nothing was written into the sibling
  repo. CLI matches `tools/sweep_skia.sh` (`--fit W H --time ms --seed N`).
- **Comparison harness.** `tools/sweep_skia.sh` + `tools/compare.py` (mean absolute error)
  work end to end.
- **Per-feature doc lists.** Detected with the real engine: `RC_TVG_TRACE=1 rc2png` prints
  `gradient type=N` (2 = sweep) and `styled … stroke=1 sw=0` (hairline). Sweep scan found the
  same 18 corpus docs the study did; hairline scan found 11.

### Item 1: sweep gradient via ConicGradient (done)

Backported `tvg::ConicGradient` from ThorVG v1.1.3 (stable tag, not experimental main) into
the vendored 1.0.7, keeping 1.0.7's fill-dispatch shape rather than taking 1.1.3's rewritten
raster core:

- `inc/thorvg.h`: `Type::ConicGradient` + the `ConicGradient` class.
- `src/renderer/tvgFill.{h,cpp}`: `ConicGradientImpl`, `CONIC` macros, `ConicGradient`
  methods, `duplicate()` dispatch.
- `src/renderer/cpu_engine/tvgSwCommon.h`: `SwFill::SwConic` (union member, no size growth)
  + five `fillConic` overloads.
- `src/renderer/cpu_engine/tvgSwFill.cpp`: `_prepareConic`, `_conicPos`
  (`t = atan2(ry,rx)/2π + offset`, wrapped to [0,1); hard seam, matching Skia — no seam AA),
  five `fillConic` span functions mirroring `fillRadial`, and the `fillGenColorTable` branch.
  Guarded `_estimateAAMargin` so a conic fill is not mis-cast to linear.
- `src/renderer/cpu_engine/tvgSwRaster.cpp`: `FillConic` functor,
  `_rasterConicGradientRect/Rle`, and three dispatch branches.
- `src/renderer/tvgText.h`: a conic branch so gradient-text scaling does not mis-cast a conic
  fill to radial (that path writes past the object = heap corruption).
- Backend `components/kobentovg/src/ThorvgPaintContext.cpp`: `makeFill()` maps
  `SWEEP_GRADIENT → ConicGradient::conic(cx, cy, 0)`; the flat middle-stop fallback is gone;
  `styled()` and `drawText()` treat sweep as a gradient.

**Result (Skia reference, 400×400, mean abs err).** Every one of the 18 sweep docs improved;
residuals are now non-sweep (clock-hand AA, the ~1 px hard-seam difference, text kerning):

| doc | before | after |
|---|---|---|
| androidx-demos/procedure-gradient4 | 75.94 | 3.28 |
| androidx-demos/procedure-text-path-effects | 46.90 | 2.22 |
| androidx-demos/count-down | 20.17 | 1.58 |
| androidx-demos/fancy-clock2 | 19.00 | 2.40 |
| androidx-demos/experimental-solar-gmt | 15.52 | 2.44 |
| render/p02-new-draw | 11.74 | 0.15 |
| render/15-paint | 6.60 | 0.33 |
| (18 docs total; all now < 3.4) | | |

**Regression.** 65 non-sweep corpus docs are byte-identical to the pre-change build (the new
code is reached only for `Type::ConicGradient`).

**Size (measured, arm64 `-O3`, `__text` per object; device numbers will differ).**

| object | before | after | delta |
|---|---|---|---|
| tvgSwFill.cpp.o | 12,508 | 14,968 | +2,460 |
| tvgSwRaster.cpp.o | 40,484 | 43,500 | +3,016 |
| tvgFill.cpp.o | 1,941 | 2,599 | +658 |
| **code total** | | | **+6,134 B** |
| whole `libthorvg.a` | 515,208 | 526,056 | +10,848 B |

Scaling by the study's measured arm64→Thumb-linked ratio (0.73) projects ≈ **+4.5 KB** on a
Cortex-M, inside the study's +2.6–4.7 KB estimate; the backport omits 1.1.3's seam-AA, so the
on-device figure should sit near the low end. Not yet cross-compiled for the ESP targets (no
flashing; a device build would confirm).

### Item 2: backend quick wins (3 of 5 done; `kobentovg` only)

- **(a) Hairline (width 0).** `styled()` now sets width `1/scale(CTM)` (scale =
  `sqrt|det(CTM)|`) so a hairline lands at ≈1 device px after the push transform. Exact for
  uniform scale (the gross-error viewport case), approximate under shear. Small wins where
  scale ≠ 1 (anchored-text 19.3→18.9, thumb-wheel1 2.1→1.9, gen-gauge 0.18→0.14); neutral
  elsewhere. A true device-space hairline (baking the CTM into geometry) was not pursued.
- **(b) Bitmap filter.** `FILTER_BITMAP`/`IMAGE_FILTER_QUALITY` upper == 0 now selects
  `Picture::filter(FilterMethod::Nearest)` (verified against androidx
  `PaintBundle.setFilterBitmap(upper != 0)`); otherwise ThorVG's default bilinear.
- **(c) getTextBounds UTF-16 fix.** `start`/`end` are UTF-16 code units (Java String indices)
  but the stored text is UTF-8; added `utf16IndexToByte()` so a non-ASCII prefix no longer
  shifts every measurement. Vertical bounds are still font ascent/descent, not tight ink
  bounds (would need ThorVG `GlyphMetrics`).
- **(d) Inverse fills + clip difference — deferred.** The InvAlpha-over-clip-rect approach
  needs the current clip bounds, which the backend does not track (clips are ThorVG `Scene`
  clips with no retrievable rectangle). It affects 1 corpus doc (inverse fill) and 0 (clip
  difference), so it is not a "quick" win here; it needs clip-bounds plumbing first.

**Not yet pushed; commit only on the owner's request.** Device (ESP-IDF) compile of the
changed `thorvg`/`kobentovg` components is still to be confirmed (host build is clean).

### Item 3: text on a path (done)

`drawTextOnPath` was a stub that drew the whole string at the path's first `moveTo`. It now
places each glyph along the path: the stored path is flattened with RC's own path measure
(`rccore::mesh2d::flattenPath` + `samplePolyline`), and each glyph is centred on its
arc-length midpoint, rotated to the tangent, and offset perpendicular by `vOffset` — the same
placement as the Skia reference's per-glyph `SkRSXform`. Gradient-on-path falls back to the
solid colour; kerning is not applied (the whole backend measures glyph-by-glyph).

Verified on the only two corpus docs that use it (`androidx-demos/clock`,
`androidx-kotlin/01dslclock`): "Wed 0" / "Wed : 0" curve along the bottom arc and match Skia
on the glyphs. (Both docs also show a pre-existing, unrelated background-colour mismatch —
the scalloped face is pale in ThorVG, dark in Skia — not touched by this change.)

### Item 4: 2D vertex meshes (done)

`setMesh` / `drawMesh` / `matrixFromMesh` (opcodes 104/105/106) were no-ops. The engine
(`AddMesh2D`) expands every layout/expression/spline mesh into a user-space triangle list;
the backend now transforms those verts through the CTM into device space, rasterizes the
triangles with `rccore::d3::fillTriangleGouraud` / `fillTriangleTextured` (the fixed-point
rasterizer the software-3D path already uses) into a device-**bbox**-sized `ARGB8888S` buffer
(bbox, not full-screen, to bound PSRAM), then pushes that buffer as a `Picture` so the current
clip and paint order both apply. Painter's order comes from a strictly-decreasing per-triangle
z against the rasterizer's `>` depth test. `matrixFromMesh` reuses `mesh2d::sampleFrame` +
`buildMatrix`. ThorVG has no `drawVertices` (what the Skia backend uses), so this is the
device-buffer approach instead.

No corpus document uses 2D meshes (the op landed in the engine 2026-09; json2rc cannot emit
them), so correctness was checked with a standalone harness: a Gouraud quad, a 25° device-space
rotation, a clip, and a translucent SrcOver quad all render correctly. The textured-mesh path
is implemented (modulate, with the rasterizer's GL v-flip cancelled to match RC's top-left uv)
but not visually verified.

**Size (Items 3 + 4, `kobentovg` only; `libthorvg.a` unchanged).** Backend `ThorvgPaintContext`
`__text` 59,320 → 76,752 B (**+17,432 B** arm64 `-O3`, ≈ **+12.7 KB** Thumb). Both features are
pure backend code over rccore's rasterizer and math, so ThorVG itself does not grow.

### Item 5: ThorVG audit — compaction, dead code, perf

The build is already tightly scoped (CPU engine + raw/png/jpg/sfnt loaders; no SVG, Lottie,
WebP, GIF, GL/WG; LodePng is decode-only; SIMD headers are `#ifdef`-guarded to the scalar
path). The device links with `-ffunction-sections -Wl,--gc-sections`, so anything *unreferenced*
is already stripped — e.g. `tvgOtfReader` (CFF, 7.8 KB) is behind `THORVG_OTF_LOADER_SUPPORT`,
never defined, so it costs 0 in the firmware; likewise Saver/Accessor/Animation/file-path
loaders when unreferenced. The real prizes are code *referenced* through a switch or function
pointer yet never exercised by RC.

**Applied — post-effect Fill / Tint / Tritone removed.** RC only ever creates `GaussianBlur`
and `DropShadow`, but `SwRenderer::prepare`/`render` dispatch all five `SceneEffect`s through a
static switch, so `effectFill`/`effectTint`/`effectTritone` (+ their `*Update`, + the
tritone-only `_trintone`) were reachable dead code. Removed from `tvgSwPostEffect.cpp`, the two
switches in `tvgSwRenderer.cpp`, and the decls in `tvgSwCommon.h`. Measured: `tvgSwPostEffect`
`__text` 9,860 → 5,992 (−3,868 B), `tvgSwRenderer` 8,132 → 7,868 (−264 B) = **−4,132 B** arm64
(≈ −3.0 KB Thumb); `libthorvg.a` 526,056 → 520,360. GaussianBlur confirmed still correct
(standalone blur test: a sharp square's edge spreads to grey outside it).

### On-target size (measured, Tab5 / ESP32-P4, RISC-V `-O2`, post-`--gc-sections`)

Built `firmware/rcplayer` for `m5stack_tab5` (the Muse build needs `sdkconfig.secrets`, absent
here; rcplayer links the identical `thorvg`/`kobentovg` components, so it is an equivalent proxy
and needs no secrets). Compared the current tree to a clean `adbaae8` baseline (stock ThorVG
1.0.7, no RcVG work) via `idf.py size-components`; the working tree was set aside with a
temporary WIP commit and fully restored after. Flash `.text`:

| Component | Baseline | With RcVG (all of Items 1,3,4,5) | Δ `.text` | Δ total |
|---|---|---|---|---|
| `libthorvg.a` | 168,760 | 171,328 | **+2,568** | +3,358 |
| `libkobentovg.a` | 31,258 | 42,098 | **+10,840** | +11,863 |
| **Combined** | 200,018 | 213,426 | **+13,408 B (+13.1 KB)** | +15,221 B |

So every RcVG feature so far — conic/sweep gradient, text on a path, and 2D meshes — costs
**+13.1 KB of flash code** on the Tab5, ~0.37 % of the 3.6 MB app. The `libthorvg` figure is
already *net* of the Fill/Tint/Tritone removal (≈ −3 KB on RISC-V): the backported
ConicGradient adds ~+5.6 KB to the core, the cut gives ~3 KB back, leaving +2.6 KB. The backend
(+10.8 KB) is text-on-path + 2D meshes + the sweep wiring. Nothing was flashed.

**Not applied — owner's call (risk/tradeoff):**
- **JPEG progressive decode** (`tvgJpgd.cpp`, ~5–6 KB): reachable via `init_progressive()`.
  Removable only if RC's images are known baseline-only — progressive JPEGs are common, and
  dropping the path silently fails them. Best gated behind a build flag, not hard-removed.
- **De-virtualize the ARGB raster path** (perf, ~0 size): the hot span/pixel loops indirect
  through `surface->blender`/`join`/`alphas[]` though RC uses exactly one surface format
  (ARGB8888 premultiplied). Inlining the ARGB joins removes an indirect call per pixel on
  cores with no branch predictor (Xtensa/RISC-V). Medium effort; measure first.
- **`-O3` / `-funroll-loops` on the hot SW files** (`tvgSwRaster`/`tvgSwFill`/`tvgSwRle`):
  likely faster span loops for little flash; worth an A/B. (`-ffast-math` would change gradient
  rounding — only with accepted visual diffs.)
- **Texmap** (`tvgSwRasterTexmap.h`, ~20 KB): reachable whenever an image is rotated/sheared
  or RLE-clipped, so **not** safely removable unless RC provably never does either.
- **ABGR colour-space paths**: < 1 KB and already abstracted behind `surface->join`; not worth
  removing.
