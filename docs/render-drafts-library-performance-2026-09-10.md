# Higher-resolution drafts and Library rendering — September 10, 2026

RAW color drafts previously used a 256-pixel long edge; expensive draft paths
used 512 pixels. Both now use 1024 pixels. The existing 1600-pixel path for
simpler previews remains. Decode prewarming follows the new draft size;
RAW characterization, color transforms and final resolution are unchanged.

Standard photo-layer adjustments now prepare their input transform and settings
once per layer. The existing scalar fallback still handles local hue/colorize
semantics. The production callback is immutable and explicitly opts into worker
execution, allowing independent adjustment rows and pre-spatial preparation to
run through the existing scheduler. Custom callbacks retain serial execution
unless explicitly declared thread-safe. Neutral layers, clipping dependencies,
curve preparation and bounded selection-mask caching retain their prior behavior.

The Studio Library preserves its justified row geometry but instantiates only
rows near the viewport. Exact row offsets preserve full scroll extent, binary
search replaces the linear visibility scan, and aspect updates preserve the row
being viewed. Gallery layout reads a small projection of visible catalog rows;
it no longer canonicalizes edit recipes or loads full metadata for every tile.
The catalog's order, filters, asset identities and selection behavior remain the
source of truth.

## Validation

The native Library proof uses 2,000 isolated fixture items, checks bounded tile
counts at both ends of the scroll range, checks selection after returning to the
start, publishes current thumbnails and waits for image readiness before taking
screenshots. Its population timing excludes explicit fixture thumbnail
publication and therefore measures grid layout, not cold photo decoding.

The original grid instantiated all 2,000 tiles and took 1,935 ms to populate.
The final run instantiated 24 tiles and populated in 59 ms (96.95% less time).
Jumping to the end took 3 ms instead of 7 ms. The timing run had no concurrent
build. Screenshots show current thumbnails at both ends, and a second square
image fixture verifies layout updates when loaded proportions change. These
are isolated layout measurements, not cold thumbnail decode or disk throughput.

Scalar/optimized rendering tests cover mixed light/color adjustments, local hue
and colorize fallbacks, clipping, curve channels, transformed pixels, texture and
clarity, odd row sizes, signed/HDR values and alpha. Native RAW tests enforce a
1024-pixel draft floor and compare each draft to its same-resolution reference
and the full preview to an actual flattened export for exposure, curves,
temperature, tint and saturation.

Private artifacts live in `/private/tmp/framehouse-render-library-next/`.
The original renderer is archived as `baseline-render-driver`; source snapshots
preserve the starting versions in the same directory. The Library proof runs
inside the actual QML shell with an isolated catalog and synthetic image fixture.
Its deliberately unavailable catalog helper does not access the user's catalog.

## Rendering measurements and final checks

At 512 × 341, the same-process scalar/production comparison for a layer with
exposure, temperature, contrast and saturation changes drops from 114.841 to
5.077 ms (95.58%). Three alternating samples per path compare bit-identical
RGBA. Neutral and curve layer optimizations retain their existing results.

Five native RAW controls (curves, exposure, temperature, tint and saturation)
all produce 1024 × 683 drafts with a 0/255 same-resolution reference delta;
full preview/export error stays 1/255. Full PNGs for the curve recipe and the
mixed light/color layer recipe are byte-identical to the archived renderer.
The latter full edit drops from 43,043 to 10,087 ms, but that single diagnostic
pair overlapped compilation and is not a performance acceptance statistic.

A further scheduling fix applies interactive priority at the shared render
entry point. Stale/cancelled completion handoffs previously bypassed the timer
entry check and could start full rendering alongside the first draft. Final
curve and exposure runs explicitly require draft publication before full work,
reach current full quality and preserve pixel parity. Their first draft render
times are 196 and 411 ms: the 100 ms budget is still unmet on this heavy RAW
recipe, and no universal per-tool latency claim is made. Larger drafts trade
more pixel work for visible detail; the priority fix does not lower resolution.

Seven focused regressions pass in 8.92 seconds: color pipeline, raster cache,
layer scalar parity, compiled local adjustments, gallery projection, native
render parity and layer LUT parity. The broader `catalog_asset_model_test`
passes its gallery/culling checks but fails later at `import should eventually
finish`; that import timeout remains unresolved. A separate registered
`catalog_gallery_projection_test` allows the new projection/filter contract to
run independently without hiding the broader failure.

The Release app builds without new compiler warnings. Final native artifacts:
`library-after/`, `library-square/`, `layer-benchmark.log`, `render-results.json`,
`priority-results.json`, `tests-focused-final.log`, and the original failing
`tests-final.log`. Source-based QML shell compilation time is excluded from the
Library population statistic.

The tested Release app was relaunched and confirmed running. Startup emitted no
QML ReferenceError, TypeError or binding-loop messages in the inspected log.
The recipe outbox was backed up before relaunch with zero pending/active saves.
