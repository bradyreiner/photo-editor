# Timeline surface tracking validation

## Real-footage fixture

A compact real-footage fixture is preserved in [`evidence/surface-tracking-2026-10-08`](evidence/surface-tracking-2026-10-08/README.md). It uses a 0.625-second, 8 fps excerpt from Georg Buzin's Wikimedia Commons motion-parallax video, licensed CC BY-SA 4.0. Five 960×540 PNG frames show a printed information board moving rapidly across the image; the last two frames are partly or almost fully out of view. The seed rectangle is normalized and the original source timestamps are recorded separately.

Three frames have manually estimated four-corner references (roughly ±6 px); the final two carry explicit out-of-view notes. This is useful for exercising a seeded perspective warp, fast image motion, and loss handling, but it is not a curated benchmark and cannot substantiate accuracy on broad real footage. The source's low light, blur, short duration, and rapid target exit are also limitations. Do not count the annotated estimates as exact benchmark truth.

## Clear real planar pass

[`evidence/surface-tracking-2026-10-08/clear-real`](evidence/surface-tracking-2026-10-08/clear-real/README.md) adds a separate ten-frame excerpt from a licensed Wikimedia Commons video of a Galton board. It contains a real camera move over one visible rigid plane. The whole-board seed failed; a smaller seed in the textured upper bead reservoir completed all ten frames. Against manually marked board-plane references on the two non-initial scored frames, corner RMSE was 4.81 px and the maximum error was 6.75 px, below the 10 px smoke threshold with about ±5 px manual-reference uncertainty.

This is evidence for a narrow short-pass use case only. The same upper-patch seed drifted beyond 10 px over a longer excerpt, and glare, the moving bead pattern, the small target region, and the 0.45-second duration limit the result. Keep the earlier Heidelberg clip as a separate blur, large-motion, and loss stress fixture; its failing result is not qualified as a clear real-footage pass. Neither clip supports a broad tracker-accuracy claim.

## Core acceptance recommendations

- Initialize track points only inside the seed polygon and any user exclusions. Spread points across the patch; reject low-texture layouts that cannot support a stable surface fit.
- Track points with pyramidal Lucas–Kanade optical flow; validate forward/backward consistency and patch support before fitting the per-frame homography. Lucas and Kanade's original registration method is described in the [IJCAI paper](https://idl.uw.edu/living-papers-paper/lucas-kanade/).
- Fit a homography robustly with RANSAC, then refit from inliers. OpenCV's official [homography tutorial](https://github.com/opencv/opencv/blob/5.x/doc/tutorials/features/feature_homography/feature_homography.markdown) explains this outlier-resistant correspondence fit; the original RANSAC method is [Fischler and Bolles, 1981](https://doi.org/10.1145/358669.358692).
- Gate each update on inlier count and ratio, spatial spread/coverage, reprojection residual, finite coefficients, a convex non-self-intersecting quad, and plausible area/edge changes. Do not update the reference template from a rejected frame; surface loss should be explicit and recoverable through user correction/reseed.
- For deterministic acceptance on the bundled clip, score manual-reference corner RMSE and maximum error on visible frames 0–2 with a 10 px threshold, require finite convex quadrilaterals, and require an explicit out-of-view/lost state by frame 4. Separately verify seed serialization and source-time alignment. This very short fixture only supports a smoke/stress gate; add independently annotated longer footage before making robustness claims.

## Implemented evidence

The current 2D implementation is separately identified from the earlier COLMAP camera solve. In the known static near-plane fixture with background parallax, all 16 frames tracked: independent corner RMSE **0.995 px**, maximum **3.367 px**, and minimum support confidence **0.349**. The measurement uses the generator's known camera/plane geometry, rather than the tracker's own fit residual. [`measure_surface.py`](evidence/surface-tracking-2026-10-08/measure_surface.py) and its JSON receipt are preserved.

The Galton-board worker result was rerun and independently recomputed from the manual board-outline references: **4.808 px RMSE**, **6.752 px maximum**, ten tracked frames. See [`measure_real.py`](evidence/surface-tracking-2026-10-08/measure_real.py). Reference uncertainty is approximately 5 px; the successful excerpt spans only 0.45 seconds. The longer excerpt's drift prevents a general real-footage accuracy claim.

The Heidelberg blur/large-motion requests fail without replacing the previous track. At denser sampling, a loose seed originally produced low-confidence sheared geometry with roughly 40–70 px corner error despite a small feature residual. The 0.30 support gate now rejects that result. `real-visible-result.json` and `real-stress-result.json` retain the failing outcomes. The added 24 fps visible frames have their own first-frame seed and must not be scored against the separate 8 fps manual-corner file. These are FFmpeg-resampled analysis times; no source-PTS precision qualification is claimed for the variable sampling cadence.

The controller workflow performs actual video decode and surface analysis, attachment, placement undo/redo, detach undo, save/reopen, and FFmpeg export. The editable timeline and exported clip are preserved. Magenta title pixels move from x≈303 to x≈117 across the synthetic shot; decoded endpoint PNGs are included. This is an attachment/export smoke check, not a perceptual compositing or occlusion qualification.

Focused runtime, panel, controller, scene-runtime compatibility, motion-runtime, SAM2 protocol, matte, frame scheduling, and playback-controller checks passed **9/9**. The core tests cover perspective/outlier accuracy, exclusions, correction reset, texture loss, cancellation, malformed protocol, weak confidence, and invalid quadrilaterals. The UI test is component interaction evidence; native pointer checks are recorded separately below.

Native fixtures run with a test-only normal-pressure override: automatic memory pressure can intentionally defer metadata/poster hydration on this development machine. That override is not used by the product. Native validation does not qualify resource-pressure recovery, 4K throughput, audible audio, or the broader Timeline baseline. Existing unrelated mask-overlay null-size warnings were observed.

The full app build required one unrelated proof-helper correction: a QVariantMap report is now explicitly converted to QJsonObject when inserted into JSON evidence (`tests/agent_full_workflow_runtime_proof.h`). The surrounding proof behavior was preserved.

The [native Cocoa workflow receipt](evidence/surface-tracking-2026-10-08/native-ui/surface-workflow.json) records a passing pointer-driven exclusion/retrack, four-corner correction/retrack, and title attachment workflow. Paused exact seek and a forward-playback sample at one second were checked against decoded image pixels; a trimmed source was also checked for the correct cache origin. [Correction](evidence/surface-tracking-2026-10-08/native-ui/surface-2d-corner-correction-1440x900.png) and [attached exact-seek](evidence/surface-tracking-2026-10-08/native-ui/surface-2d-exact-seek-1440x900.png) screenshots were visually inspected. This proves the bounded workflow, not sustained playback performance or general visual quality on real shots.

The final build also required a one-line fix in a concurrently changed text-rendering optimization: an undeclared boolean helper was replaced by the equivalent QVariant conversion with the same enabled-by-default behavior (`desktop_pipeline_state_compiler.cpp`). The surrounding optimization was preserved.

The final `photo_editor_app` build passed. An isolated launch of that freshly built app reached the packaged Timeline route in 42 ms with no QML load errors and exited successfully. Its intentionally closed catalog endpoint produced expected helper timeouts; this launch is a startup check, not catalog qualification. The [final receipt](evidence/surface-tracking-2026-10-08/validation-receipt.json) records artifact hashes and bounded claims; focused-test and packaged-launch logs are preserved alongside it.
