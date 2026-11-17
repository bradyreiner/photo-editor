# Brush responsiveness repair

The paint tool now yields between dabs and considers its existing regional live preview before escalating to a full-resolution composite. Long pointer movements no longer have to finish rasterizing before publishing intermediate paint. Committed strokes and rejected regional requests retain native-quality compositing.

The rasterizer also avoids radial-distance and material work outside a round dab's support and inside its solid core. Forty-eight raster hash comparisons against the preserved original implementation remained byte-identical. The focused optimized raster benchmark improved 36–67% across 96, 512, and 1024 px brushes at three hardness settings.

## Native evidence

Artifacts: `build/brush-improvement-2026-10-29/`. `comparison.json` records workload, binary/source hashes, results, and limitations. The comparison uses a 3000 × 2000 synthetic PNG, a 512 px brush, three strokes, the Debug build, the native Metal texture, and actual changed midpoint pixels captured from the exposed Cocoa window. Both baseline and candidate passed the foreground/key-window/occlusion and visible-pixel checks.

| Metric | Before median | After median |
| --- | ---: | ---: |
| Actual paint visible at stroke midpoint | 2150 ms | 123 ms |
| Completed stroke presented | 2151 ms | 314 ms |
| Worker commit completion | 33.3 ms | 30.3 ms |

Visible midpoint delay improved 94.3%. Measured peak RSS was 2.47 GB before and 1.91 GB after; this short run does not establish long-session memory behavior.

The performance loop accepted two optimization attempts within a three-attempt ceiling. Its gate was at least 20% improvement, with no protected metric regression above 10% or RSS growth above 5%.

## Regression coverage

Eight focused CTest cases passed in the isolated source snapshot: paint responsiveness, tile commit, resampling, coordinate transforms, stroke sampling, brush fidelity, pixel-paint workflow, and edge parity. Coverage includes pressure, selection clipping, all five materials, erasing, held draft immutability, cancellation, undo, recipe reopening, and exports. A new render-routing regression checks that a reduced live preview uses a partial update while the settled request still performs native-quality compositing.

The additional Cocoa run of `photo_pixel_paint_test --require-native-device-icc` passed with the system monitor profile. Its regional/full reference comparisons reported zero maximum channel error and zero error outside the changed region. Log: `native-icc-ctest.log`.

## Scope of qualification

Concurrent changes to brush structures invalidated one shared-build run. Final native results and the eight-test pass therefore use the source snapshot under `source/` and its dedicated `frozen-build/`, with hashes in `comparison.json`. Subsequent changes in the shared checkout are outside that qualification.

These results establish a focused responsiveness improvement. They do not establish sustained tablet input, physical-pointer smoothness, every RAW/adjustment fallback, or packaged full-app qualification. Large individual dabs can exceed the worker's 8 ms target because it yields between dabs. Native quality, history, and recovery checkpoints remain intact.
