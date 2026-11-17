# Native review excerpt verification

## Renderer change

The ordinary H.264 excerpt path now reuses the bounded native compositor windows at full sequence dimensions and keeps keyframes on the global sequence clock. It renders one continuous ranged audio stem, adaptively splits any video window whose FFmpeg graph exceeds 256 inputs, and keeps the existing full-quality encoder settings. Each encoded segment receives a one-frame clone guard for concat timing; the guard frames are removed after concat and the CFR timestamps are rebuilt. The deferred spatial-grade path still uses `renderTimelineWithFrameStreamGrades`.

## Focused checks

- `timeline_bounded_h264_export_workflow` passed. This includes the existing bounded-export checks and two pixel-exact guard tests: eight one-frame color segments and a mixed 1+16-frame case beginning at global frame 43. Both outputs matched the individually decoded expected frames byte-for-byte after guard removal. The existing subframe fixture also retained 306 frames, global-time seam frames, continuous AAC, and released its procedural cache leases.
- `timeline_review_excerpt_export_workflow` passed on a dense generated 4K sequence. The first 16-frame graph required 304 inputs and was split at global frame 38 under the 256-input cap. It produced exactly 60 frames for global range `[30,90)`.
- `timeline_selection_refresh_coalescing` passed.
- `timeline_spatial_source_grade_workflow` rendered and validated its nonzero-start excerpt for global range `[24,48)`: H.264, 96×96, 24 decoded frames at 24 fps, with a one-second AAC stream. The enclosing workflow then exited 1 on its full-delivery-versus-preview pixel-parity assertion; that result is retained below and is not counted as a passing spatial parity check.

## Dense 4K excerpt evidence

Artifact: [review-excerpt-4k-b8463681-f658-47e3-8edf-1d7fc0c024ef.mp4](review-excerpt-4k-b8463681-f658-47e3-8edf-1d7fc0c024ef.mp4)

- [Controller verification summary](review-excerpt-4k-b8463681-f658-47e3-8edf-1d7fc0c024ef-verification.json)
- [ffprobe streams and container metadata](review-excerpt-4k-b8463681-f658-47e3-8edf-1d7fc0c024ef-ffprobe.json)
- [Independent decoded-frame inspection](review-excerpt-4k-b8463681-f658-47e3-8edf-1d7fc0c024ef-independent-inspection.json)

The retained file is 3840×2160 H.264 at 30 fps, exactly 60 video frames / 2.000 s, plus stereo 48 kHz AAC from 0.000 to 2.000 s. The mixed audio measured −24.1 dB mean volume. A separate 64×36 RGB decode found all 59 adjacent frame pairs different, minimum adjacent mean RGB delta 1.227, and minimum frame mean 12.937. Chunk-boundary mean RGB deltas at frames 8, 16, 32, and 48 were 2.003, 2.251, 1.332, and 1.326.

Earlier successful proof retained from the same renderer: [review-excerpt-4k-d861d008-13f1-4ba2-bc37-8bd8d465ee2b.mp4](review-excerpt-4k-d861d008-13f1-4ba2-bc37-8bd8d465ee2b.mp4), with its [verification summary](review-excerpt-4k-d861d008-13f1-4ba2-bc37-8bd8d465ee2b-verification.json) and [independent inspection](review-excerpt-4k-d861d008-13f1-4ba2-bc37-8bd8d465ee2b-independent-inspection.json).

## Deferred spatial excerpt evidence

Artifact: [spatial-review-excerpt-e40c3b1b-f0d9-40a5-92b9-73fddef17127.mp4](spatial-review-excerpt-e40c3b1b-f0d9-40a5-92b9-73fddef17127.mp4)

- [Controller verification summary](spatial-review-excerpt-e40c3b1b-f0d9-40a5-92b9-73fddef17127-verification.json)
- [ffprobe streams and container metadata](spatial-review-excerpt-e40c3b1b-f0d9-40a5-92b9-73fddef17127-ffprobe.json)

The excerpt is 96×96 H.264 at 24 fps, exactly 24 frames / 1.000 s, with AAC audio at the same start and duration. This confirms range routing and output shape; it does not establish full spatial-grade pixel parity.

## Spatial full-delivery parity finding

The full export and preview comparison reported matching alpha and geometry but a large RGB difference. At the two-keyframe transform midpoint (1 s; `frameScale=1.20`, `frameOffsetX=0`), expected alpha mean was 0.586948 and decoded ProRes alpha mean was 0.587188. The alpha-plane RMS difference was about 0.00035, with the best translation at `(0,0)`. At maximum-error pixel `(33,15)`, expected RGBA was `(0.8482, 0.2594, 0, 0.6000)` while decoded ProRes was `(0.1877, 0.0582, 0, 0.6001)`.

The ProRes stream is `yuva444p12le` tagged BT.709 primaries, transfer, and matrix; `color_range` is unknown. Forcing full-range input during decode changed the sample only to approximately `(0.2258, 0.1120, 0.0580)`, so range metadata alone does not account for the discrepancy. A controlled RGBA64-to-ProRes4444-to-RGBA64 check round-tripped `(0.8482, 0.2594, 0, 0.6)` as `(0.8494, 0.2594, 0.0010, 0.6001)`. The H.264 excerpt from the same 1–2 s source range was substantially closer to its expected black composite (mean RGB absolute error 0.0241 versus 0.1395 for the full ProRes output), which points toward a full alpha-preserving composition/frame-data difference; the exact stage is not proven.

Skipping only the excerpt call in a temporary test-only diagnostic produced the same full-delivery parity metrics. The skip gate was removed. This isolates the failure from the excerpt call but does not prove whether it predates the current source state. The failing report and pixel captures are retained under `build/app-audit-fixes-2026-10-25/video/spatial-source-grade/` (`timeline-spatial-source-grade.json`, `spatial-preview.png`, `spatial-expected.png`, `spatial-delivery-decoded.png`, and `spatial-delivery.mov`).

## Build and source state

The focused Debug test translation unit compiled and linked after the missing Debug engine archive was rebuilt with `ninja -C build/dev-macos-debug engine/libphoto_engine.a -j 2`. The test compile emitted the existing unused `kRangeStart` warning in `timeline_authored_project_render_probe.inc`; `git diff --check` was clean. Review-only graph retention/logging code was removed before the successful dense export. No application source changed after the parent build was released.

Implementation source hashes at verification time:

- `timeline_document_controller.cpp`: `993b590a309066f99d1c361df868b52c77962553d02fead924c70e10607f8827`
- `timeline_document_controller.h`: `912e7a73548f773f40f279b3071ccc2c0d512600a410df3dfc2ce0a7cc562f97`
- `timeline_bounded_h264_export.cpp`: `4ce133a5616477ab15ba764fd71d583d932f671c6f88e5be29e22dad22ae0f98`
- `timeline_bounded_h264_export_workflow.inc`: `35e0f80f7892a562cc94922e1b428393d79771ccb63681f42287e7e0b9d1f935`
- `timeline_spatial_source_grade_workflow.inc`: `360340fefbc522c2f0544dea5e51f248fbb23f67d12784a716c9e423693552e4`
