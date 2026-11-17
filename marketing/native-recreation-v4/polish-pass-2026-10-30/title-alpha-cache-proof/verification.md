# Sparse title alpha cache proof

The focused cache benchmark used the current P14 title PNG cache at
`/private/tmp/looksy-dense-timeline-v7iaxqgh/cache/timeline-titles` (318 PNGs,
3,313,387 bytes total; largest file 42,403 bytes). The benchmark selected the
first 22 canonical paths and simulated five independent four-frame windows,
matching the repeated per-window alpha-support work seen in the P14 sample.

`benchmark-p14.txt` records the measured comparison:

- Existing per-window decode and alpha scan: 110 decodes in 583 ms.
- Content-identity cache: 22 decodes, 88 hits, 131 ms including a full file
  read and SHA-256 calculation on every lookup.
- The cache-only repeat stage was 4.45x faster in this run; this does not claim
  the full export is 4.45x faster.

The Release helper test covers a 3840×2160 PNG, changed content at the same
canonical path with unchanged size and modification time, invalid and oversized
inputs, eviction after 513 paths, and twelve simultaneous lookups sharing one
decode. The production filter workflow test obtains alpha bounds from the disk
cache and verifies the same ROI graph, ROI dimensions and overlay expressions
as direct in-memory bounds, followed by an exact 15-frame FFmpeg framemd5/PTS
comparison.

Focused check:

```text
ctest --test-dir build/release-macos --output-on-failure -R '^(timeline_sparse_title_roi_test|timeline_sparse_title_filter_workflow_test)$'
2/2 passed
```

The helper stores only the canonical path, SHA-256, image dimensions and alpha
bounds. It re-reads and hashes source bytes for each lookup, decodes from those
same bytes on a miss, caps retained metadata to 512 paths, limits concurrent
file/decode requests to two, and falls back to the full-canvas path for failed,
oversized or above-4K-area images.
