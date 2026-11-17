# Static title PNG loop benchmark (P8 fixture)

The preserved P8 sparse-title fixture has a contiguous 30-frame interval at global frames 1424–1453 (47.467–48.433 s) where 108 of the 112 title PNG inputs are eligible for a decode-once loop: each stays active with fixed identity scale and offset, constant clip and track opacity, no unsupported geometry, and an already alpha-cropped sparse PNG source.

The baseline repeated the PNG demux loop at 30 fps. The candidate decoded each eligible PNG once, passed the decoded ROI through the fixed identity scale/crop stage, then used `loop=29:size=1:start=0` with a rational 1/30 timebase and `setpts=N`. Both produced exactly 30 RGBA frames, and every `framemd5` record (including PTS) matched.

| Path | Wall time | Peak RSS | Frames |
|---|---:|---:|---:|
| Repeated PNG demux loop | 1.034 s | 427,032,576 B | 30 |
| Decode once, fixed ROI, then loop | 0.432 s | 156,254,208 B | 30 |

That is a 58% lower wall time and 63% lower peak RSS for this title-only graph. The proof uses the saved P8 project and title PNG fixture, not a full native render; it has no background video or audio. It supports a guarded static-title optimization but does not establish full-film export time. Exact inputs, clip IDs, PTS/hash evidence, and FFmpeg stderr paths are in [the JSON report](static-title-loop-benchmark-p8.json); raw outputs are retained under `/private/tmp/looksy-static-title-loop-bench-108/`.
