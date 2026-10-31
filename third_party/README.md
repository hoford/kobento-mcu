# Vendored third-party code

## thorvg (v1.0.7, MIT)
Upstream: https://github.com/thorvg/thorvg tag v1.0.7. Local patches:
- `src/loaders/sfnt/tvgSfntLoader.cpp` `SfntLoader::metrics()`: querying the metrics of an
  outline-less glyph (a space) cached an infinite bbox, after which any text containing
  that glyph rendered nothing. Fixed by skipping the bbox computation for empty paths.
  Repro: `Text::metrics(" ", gm)` then render `" 3"` -> 0 pixels. Worth reporting upstream.
