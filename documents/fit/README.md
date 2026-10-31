# Fixed-size documents, shown whole

Every `.rc` here is built into the players with `fit` on: it is laid out at its own size
(its header's width and height; 600 x 600 when the header has none) and scaled to fit the screen, centred on black, instead of
being laid out at the screen's size. Use it for documents designed for one size, which
would otherwise be cut off on a smaller or differently shaped screen.

| File | Source (downloaded 2026-10-19) | Size |
|---|---|---|
| `sakura_season.rc` | `docs/render/01-sakura-season/doc.rc` | 360 x 360 |
| `friendly_aliens_ufo.rc` | `docs/render/22-friendly-aliens-ufo/doc.rc` | 360 x 360 |
| `under_the_sea.rc` | `docs/render/23-under-the-sea/doc.rc` | 360 x 360 |
| `jancy_clock.rc` | `docs/androidx-demos/clock-demo2-jancy-clock2/doc.rc` | none (600 x 600) |
| `gmt_watch.rc` | `docs/androidx-demos/experimental-gmt/doc.rc` | none (600 x 600) |
| `solar_gmt_watch.rc` | `docs/androidx-demos/experimental-solar-gmt/doc.rc` | none (600 x 600) |
| `maze.rc` | `docs/androidx-demos/maze2/doc.rc` | 500 x 500 |
| `maze_simple.rc` | `docs/androidx-demos/maze/doc.rc` (coarse grid) | 500 x 500 |

Paths are under `https://hoford.github.io/RemoteCompose-Examples/`. A page's
`doc.html?id=<id>` has its file at `docs/<id>/doc.rc` (the catalog, `catalog/corpus.jsonl`,
lists every id).
