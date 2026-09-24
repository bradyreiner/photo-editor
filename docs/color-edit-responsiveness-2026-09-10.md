# Color edits, white-balance correctness, and resident zoom

The investigation reproduced slow native color feedback on a 4208 x 3120 JPEG,
and a separate temperature preview/export mismatch on a synthetic color fixture.
The work preserves the existing dirty checkout and does not migrate catalog data.

## Correctness defect

Global temperature and tint are serialized in `recipe.look`, but the engine
applies their white-balance matrix in CameraCharacterization. That stage's cache
identity omitted both controls, so editing either could reuse old characterized
pixels even though the downstream recipe changed. The focused compiler regression
fails before the fix. The native temperature regression reproduces a maximum
preview/export difference of 16/255, including with optional Metal output disabled.

CameraCharacterization v5 now includes both controls and retains Decode and
NormalizeRaw reuse. Its downstream identities change through the existing
upstream dependency chain. Saved previews use a new cache namespace to regenerate
older incorrect images; originals and edit recipes are unchanged.

## Performance changes

- Zoom/pan inside current full-quality native coverage reuses the resident pixels.
  The old controller scheduled 30 unnecessary visible-tile renders for 30 moves.
  Coverage remains recipe-specific; partial textures still require uncovered tiles.
- Lossless PNG cache writes use Qt compression 12 (zlib level 1), preserving pixels
  and ICC profiles. On the photographic fixture, median encoding changes from
  1254.59 to 681.80 ms (45.65% faster), with file size increasing from 15,384,067 to
  15,981,798 bytes (3.89%). Decode median changes from 116.91 to 118.67 ms (+1.50%).
- The third experiment enabled the existing GPU output table for full previews.
  Although parity passed, measured output-stage time remained about 1.37 seconds.
  It did not clear the 15% performance gate and was reverted. Final-quality
  preview/export execution policy remains unchanged.

## Measurement protocol

At most three optimization candidates: resident zoom, lossless cache encoding,
and final output acceleration. Target at least 15% improvement, related timing
regression cap 20%, retained-cache memory cap 10%, and unchanged existing color
parity limits. Correctness repairs are separate from the optimization attempt limit.

Visible-window runs use `actual_app_workflow_benchmark_driver
--color-interaction-proof --iterations 3 --timeout-ms 60000`, with optional
`--color-control curve`, host Cocoa/Metal, scopes enabled, and the same photographic
JPEG. The actual QML layout selects a 4432-pixel target; this is reported rather
than presenting it as a 2560-pixel test. The baseline curve first-publication median
is 296 ms and full-render-idle median is 3923 ms. Exposure is 276 / 3774 ms.
Each run also exercises a 30-update nominal 60 Hz burst. These are publication and
QML frame timings, not measured display photons or broad p95 acceptance.

Artifacts are under `/private/tmp/color-edits-*`: `curve-before`,
`exposure-before`, their logs/captures, `codec-before.json`, `codec-fast.json`,
`zoom-before.log`, `wb-before.log`, and native parity logs/images. Timing runs
must be serial and exclude builds and other test renders. The first candidate
zoom smoke overlapped compilation and is not accepted as timing evidence.

## Validation status

All 13 focused checks pass after the white-balance fix: recipe dependency
identities, render executor CPU/GPU output and proof surfaces, native working
fusion, Metal ACES output parity, color tool invariants, lossless cache codec,
preview feedback, coalesced undo, resident zoom, and native curve/saturation/
temperature/tint preview-export comparisons. Each synthetic native control has
zero draft/reference channel difference and at most 1/255 final/export difference.
The real RAW curve check has at most 1/255 for both comparisons; the real RAW
temperature check has zero draft/reference difference and 1/255 final/export difference. These limits
remain below the existing 2/255 gate. See `final-validation.log` and
`final-test-details.log` under the artifact prefix. The original broad pixel-zoom test separately stops at its existing
pixel-brush outline simplification assertion; the new resident-zoom regression
runs independently and does not suppress that failure.

## Final retained results

The Release app and focused targets build successfully. The unsuccessful GPU
experiment is absent from the final source and binaries. Final timing runs use
`curve-final` and `exposure-final`, after all compilation and regression work
finished. The intermediate `*-after` runs temporarily paused our one remaining
compiler, resumed it automatically, and are not the final retained-code evidence.

| Median measurement | Before | Final | Change |
| --- | ---: | ---: | ---: |
| Curve to all render work idle | 3923 ms | 3059 ms | 22.02% faster |
| Exposure to all render work idle | 3774 ms | 2977 ms | 21.12% faster |
| Curve first native publication | 296 ms | 281 ms | 5.07% faster |
| Exposure first native publication | 276 ms | 279 ms | 1.09% slower |
| Curve burst through final frame | 4729 ms | 3822 ms | 19.18% faster |
| Exposure burst through final frame | 4633 ms | 3715 ms | 19.81% faster |

Both full-quality latency gates pass. Maximum reported preview-cache bytes are
2.53% lower for curves and unchanged for exposure. This guards the reported caches,
not total process RSS: baseline process RSS was not recorded, so no total-process
memory improvement is claimed. `color-edits-final-comparison.json` contains the
measurements and gate booleans. Final app captures were visually inspected.

Resident zoom schedules zero additional renders for thirty moves and leaves the
native revision and full-quality coverage intact. Its final isolated admission
is 17.70 ms total; the before run's timing overlapped compilation, so only the
30-to-zero scheduling comparison is treated as controlled regression evidence.

The real 4208 x 3120 JPEG switching test passes all twelve cached pixel hashes,
latest-selection-only publication, and asynchronous admission checks. Median
controller admission is 0.88 ms; preview-ready is 121.54 ms. There is no matched
before measurement for this test, so this is a correctness/latency observation,
not a measured photo-switching speedup. Receipt: `color-edits-warm-jpeg-final.json`.

Final retained regression run: 13/14 pass. The additional synthetic warm-load
recovery case fails `recoveryPassed`, while pixels, asynchronous dispatch, and
latest-selection protection all pass. This evicted-cache recovery failure remains
unresolved; it is not hidden by the successful real-photo switching test, which
does not exercise recovery. Logs: `color-edits-retained-validation.log` and the
copied `color-edits-retained-test-details.log`. The earlier broad pixel-zoom test
also remains blocked at its pixel-brush outline assertion.

Live dragging still needs work: final first feedback is about 280 ms, and the
30-update bursts publish no new native color image during the input interval.
The input handler stays responsive, but render cancellation/coalescing and the
roughly 1.37-second full-resolution output stage remain bottlenecks. This pass
accepts lossless cache encoding and resident coverage reuse, fixes the verified
white-balance dependency defect, and stops the bounded optimization loop.
