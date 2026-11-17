# Generator PNG compression audit — 2026-10-31

## Decision
Keep the current lossless PNG compression setting at 11. On the two real 1920×1080 generator frames checked, lowering it to 0 or 1 cut isolated writer time, but produced 8.3 MB per frame. The tested Desktop-light frame grew 5.17×; the sparse vector frame grew 176×. That storage cost is a poor fit for the shared 2 GiB generator-frame cache. Level 0 and 1 produced the same byte count, so 0 offered no measured advantage over 1.

This is a narrow two-frame, one-run-per-level comparison. It does not establish an end-to-end native export speedup, nor does it justify changing a per-kind policy. No application source, native project, cache files, or worker state were modified.

## Method and observations

The private Qt console utility used the installed Qt 6.10.2 `QImageReader`/`QImageWriter` on one cached Desktop-light Metal frame and one cached vector-path CPU frame. It wrote PNG at settings 11 (current), 1, and 0, decoded each output back to `Format_RGBA8888`, and SHA-256 hashed each row’s pixel bytes. All six decoded hashes exactly matched their respective source frame hashes.

| Cached generator frame | Setting | PNG write time | Output bytes | RGBA exact |
|---|---:|---:|---:|---|
| Desktop-light, 1920×1080 | 11 | 56.7 ms | 1,605,790 | yes |
| Desktop-light, 1920×1080 | 1 | 27.9 ms | 8,308,808 | yes |
| Desktop-light, 1920×1080 | 0 | 27.8 ms | 8,308,808 | yes |
| Vector path, 1920×1080 | 11 | 29.1 ms | 47,147 | yes |
| Vector path, 1920×1080 | 1 | 27.9 ms | 8,308,808 | yes |
| Vector path, 1920×1080 | 0 | 26.9 ms | 8,308,808 | yes |

The application currently calls `QImageWriter::setCompression(11)` at `apps/desktop/src/timeline_native_generator_frame_cache.cpp:1356` (constant at line 45). The installed Qt 6.10.2 header exposes `setCompression(int)` but does not include the PNG plugin implementation or a local format-specific range description. Therefore the report records the observed behavior of the installed Qt runtime rather than asserting an undocumented numerical mapping. Empirically, 0 and 1 yielded the same near-raw-size PNG for both content types while 11 compressed these frames substantially.

The active runtime log provides context for the cost: the 6496–6512 export window reports generator 20,706 ms, graph 28 ms, final video encode 11,953 ms. Nearby 16-frame generator sequences record `pngEncodeUs` of 5,007,687 and 6,654,537 µs. The isolated utility’s timings ran during the larger render, so they are only comparative samples, not a clean idle-host benchmark. The native log also shows high run-to-run variation; lowering compression cannot be assumed to reduce full-export elapsed time by the isolated per-frame delta.

## Evidence

- `results.json` records source PNG file hashes, manifest backends, output sizes, elapsed times, decoded pixel hashes, and native log line references.
- `bench.cpp`, `bench.pro`, `desktop-light.txt`, and `vector-path.txt` preserve the small private benchmark.
- The benchmark binary was built against installed Qt 6.10.2 using qmake6 and clang; it ran only against cached PNG copies and deleted each temporary encoded output after decode/hash verification.
