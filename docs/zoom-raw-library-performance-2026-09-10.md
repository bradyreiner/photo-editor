# Cached zoom, RAW reuse, and incremental Library updates — September 10, 2026

This pass targets cached viewport latency, repeated RAW decoding, and Library
metadata updates. The acceptance target is at least 20% less work/time on each
focused case, with unchanged pixels and bounded retained caches. Existing dirty
workspace changes were preserved. Artifacts and starting-source snapshots are
in `/private/tmp/framehouse-zoom-decode-library/`.

## Changes

Large immutable mask/layer identity entries now reuse their canonical digest.
The shared cache checks QVariant-map equality, including binary and brush
payloads, and retains at most four entries with a 32 MiB serialized-payload
budget. This avoids repeated JSON serialization and SHA256 hashing during
native viewport coverage checks. A macOS sample of the original benchmark
showed those operations on the GUI thread.

The shared render-start path rechecks pending viewport coverage. If the full
render published current pixels while a viewport request waited, the request
finishes without starting another worker. The native regression explicitly
checks this completion-handoff case.

LibRaw retains packed 16-bit decoded samples before float conversion and
resizing. Keys include source path/revision, RAW settings, output color contract,
and the actual half-size demosaic mode. Full-size samples never substitute for
half-size samples or vice versa. Metadata accompanies the retained samples.
The LRU admits at most two entries and 192 MiB of packed samples, bypasses
oversized entries, and releases decoder buffers before float allocation.

Library delegates read a single lightweight catalog projection and refresh only
when their model row changes. Model resets, membership changes, width, and
thumbnail aspect changes still rebuild geometry. Metadata-only updates do not.
Bindings retain dependencies on delegate identity/index and selection signals so
filtering, row reuse and selection remain current.

## Measurements and proof

| Focused case | Before/cold | After/reused | Evidence |
| --- | ---: | ---: | --- |
| Cached viewport patch, median of 3 | 433 ms | 312 ms | 27.9% lower; all patches exactly match full native pixels |
| 30 Library metadata notifications, 2,000 assets | 683 ms | 1 ms | Zero geometry rebuilds; 24 delegates at both scroll ends |
| Compatible 1600 px RAW decode | 1595 ms | 39 ms | Exact QImage equality against independent cold decode |
| Repeated full RAW decode | 3782 ms | 209 ms | Exact QImage equality; packed cache 181,623,600 bytes |

The first 1024 px request after a full-only cache remains a miss, as required by
the differing demosaic modes. It measured 972 ms versus 1002 ms cold; no reuse
claim is made for incompatible modes. These are focused fixture measurements,
not universal camera/edit latency guarantees. Library metadata timing excludes
catalog persistence and source decoding; its before and initial after layout
runs overlapped a small test-target rebuild, so population timings are not used
as acceptance statistics.

Native curve workflow: 1024x683 draft, exact same-resolution reference equality,
full-preview/export maximum channel difference 1/255. Before/final draft,
matched-reference and full PNG files are byte-identical. Cached viewport patches
also compare against the full native preview. The first final draft measured
207 ms, still above the 100 ms interaction budget; full edit restore was 6126 ms.
The original observed 15-second viewport queue/10-second cache-hit event was not
reproduced in isolation, so this pass does not claim every cause of that event is
resolved. The duplicate-viewport handoff is now guarded and tested.

The final native driver peak RSS was 5,834,309,632 bytes while retaining reference
images and exporting. No matched baseline peak was captured, so this is not a
claim of reduced application RSS. Cache admission limits are separately checked.

Four focused tests pass: RAW decoder, gallery projection, recipe identity, and
render parity (4.19 s). RAW tests cover settings invalidation and existing
camera/scene-linear, metadata, resizing, cancellation, and concurrent decode
contracts. Gallery projection checks cover filtered ordering and invalid indices.
Native Library proof checks actual rating/pick publication and screenshots;
final filter/selection and Release app build results are recorded below.

The initial new viewport test inherited an interactive-overlay flag after
setting the viewport. The harness now explicitly constructs a full viewport
request; the corrected proof passes without changing rendering semantics.

The previously reported broader catalog import-test timeout was not rerun or
claimed resolved in this pass. GPU camera-characterization fallback remains.

Final Library verification also passes with square thumbnails: rating filters
reduce the model to the rated asset, clearing the filter restores all 2,000
assets with the rating intact, and moving selection clears the previous tile's
highlight. Metadata updates remain 1 ms with zero geometry rebuilds. Native
screenshots at both ends and with rating/pick badges were inspected. This run
was for behavior/visual validation while the Release app compiled, not a new
layout timing claim.

The Release app builds without new warnings. The native launcher gracefully
replaced the previous instance; PID 96413 was verified running the updated
Release binary. The local recovery journal was backed up with zero pending or
active saves. No QML ReferenceError, TypeError or binding-loop message appeared
in the inspected startup log. Artifacts: `build-app.log`, `relaunch.log`,
`focused-tests.log`, `before.log`, `final.log`, `raw-benchmark.log`,
`library-after/`, and `library-final-square/`.
