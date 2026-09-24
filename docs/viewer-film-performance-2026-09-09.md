# Viewer film-edit performance loop

User priority: realtime interaction and film emulation in the Gaussian viewer,
not page switching. The current turn targets repeated film/tone edits at fixed
camera and lens settings. Full camera-motion throughput, optical convergence,
film calibration and viewer UX remain separate open gates.

Using the bounded photo-editor-perf-loop procedure, with at most three attempts.
Target: at least 50% lower p95 native renderer latency across twelve fixed-camera
film edits. Guards: exact output pixel hashes, focused native tests, actual viewer
control proof, no source-image mutation, at most one retained source texture
(256 MiB at 4096 square), and no more than 10% cold-render regression in a clean
comparison. Active training contention means current wall-clock results are
diagnostic stress measurements, not standalone realtime certification.

Benchmark entrypoint: `gaussian_scene_render_test --film-perf PLY CAMERAS RECEIPT`.
It uses the existing trained 753,038-splat scene, 800-pixel output, a 16-sample
seven-blade lens and twelve grain/halation/exposure edits. GPU texture readback
is outside the measured render call and is used only to hash the actual pixels.

Baseline/candidate root: `/private/tmp/framehouse-viewer-film-perf-v7PTZT`.

Attempt 1: retained resolved-source reuse clears pixel parity for twelve film
edits; initial contended renderer p95 drops from 7311.10 to 8.24 ms. Actual SUV
viewer film controls render in 4.08–9.67 ms with 24–33 ms to two post-publication
frame swaps. This is not measured input-to-photon. The viewer test exposes only
11 published motion drafts in two seconds and 1525 ms settling. Its final
render spends 96.38 ms in CPU sort/setup. Camera motion is not realtime yet.

Attempt 2 targets static-scene CPU depth setup/sorting without changing point
count, ordering precision or image resolution. Use `--motion-perf` on the actual
SUV at 720 edge and thirty fixed poses, each hashed. Gate: at least 25% lower
median CPU setup with exact image hashes; protected end-to-end p95 and cold
latency cap 10% in comparable runs. GPU contention remains a timing limitation.

## Kept changes and final evidence

Both attempts are kept as development improvements, not clean-hardware realtime
certification. No third attempt was made. The production app bundle was not
replaced or restarted; the native test targets exercised the changed code.

- Resolved-source reuse retains the completed pre-film GPU texture, so fixed-camera
  grain, halation, exposure and look changes skip depth sorting and aperture
  rasterization. Film sliders no longer enter camera-navigation draft mode.
  Camera, intrinsics, lens, resolution, scene identity and scene time invalidate
  reuse; canceled/interactive renders cannot replace the completed source.
  There is one retained texture, capped at 256 MiB, not an edit-history cache.
- The depth-sort hot loop now accesses point and radix buffers directly and
  avoids per-point vector temporaries. All points and full 32-bit depth precision
  remain; there is no LOD, quantization or resolution reduction in this change.

| Check | Before | After |
| --- | --- | --- |
| Fixed-camera film renderer p95, initial comparison | 7311.10 ms | 8.24 ms |
| SUV motion median CPU sort/setup | 88.59 ms | 37.69 ms |
| SUV motion renderer p95 | 330.94 ms | 138.49 ms |
| SUV motion cold render | 473.57 ms | 139.29 ms |
| Actual viewer published motion drafts in two seconds | 11 | 20 |
| Actual viewer time to settled photo after motion | 1525 ms | 1422 ms |

The twelve film-edit images and thirty fixed motion-pose images match their
respective baseline pixel hashes exactly. A final film rerun also matches all
twelve original hashes, with 1.45 ms median and 21.87 ms p95. Timing variation
reflects concurrent GPU workloads; these runs do not establish repeatable
standalone speed ratios or clear the clean-machine regression gate.

Actual `Main.qml` SUV proof is foreground-eligible and passes. Film controls
publish a resolved image and reach two subsequent frame swaps in 17–33 ms
(renderer 5.51–15.20 ms), without entering draft mode or rerendering the lens.
The source texture uses 40,348,000 bytes at 1375 × 1834. This measures the native
UI publication/frame-swap path, not input-to-photon latency. Screenshots were
visually inspected. Final receipt: `suv-ui-final/receipt.json` under the artifact
root above; screenshots are `film-controls.png` and `resolved-after-motion.png`.

Six focused tests pass in 4.17 seconds: scene rendering, spacetime rendering,
film export, saved views, camera paths and viewport handoff. New checks cover
cache invalidation, independent fresh-render parity, draft/source isolation and
preservation of settled resolution during film edits. `git diff --check` passes.

## Remaining gates

Camera movement is still about 10 published drafts/second in this contended
SUV run, not realtime. Settling still takes about 1.42 seconds. GPU rasterization
and aperture integration are the next viewer bottlenecks; progressive refinement
and clean-hardware frame-time/memory measurements remain necessary. Another
unrelated Brush training job was observed and left untouched. The bounded source
cache is tested, but this is not a full memory-pressure validation.

Grain/halation remain creative effects and bokeh remains generic thin-lens
geometry, not calibrated film-stock or measured lens emulation. Reconstruction
quality is a separate limitation; see `suv-capture-2026-09-09.md`.
