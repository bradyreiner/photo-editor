# Realtime color preview follow-up — 2026-09-05

This follows `color-engine-audit-remediation-2026-09-05.md`. Scope: host-Metal preview performance, native texture lifetime, and visible JPG/RAW editing proof. It does not close the independent camera-reference, calibrated display/HDR, human-review, or other-platform gates.

Artifacts: `/private/tmp/photo-editor-realtime-pass-G228dX`. Runs use the existing dirty working tree; unrelated edits were preserved. The bounded performance loop permits at most three optimization attempts, without relaxing color precision. Targets: first feedback median <16 ms / p95 <33 ms; 2560-pixel settled preview median <150 ms / p95 <250 ms; retained-cache memory regression <=5%. These are workload gates, not a universal realtime claim.

## Changes

1. Add an explicit native texture lease from the preview backend through the controller/item to the scene-graph node. The node keeps the underlying Metal resource alive until its Qt wrapper is destroyed or replaced. Qt's native wrapper does not own the supplied texture; see [Qt's native Metal texture contract](https://doc.qt.io/qt-6.5/qnativeinterface-qsgmetaltexture.html). A focused test verifies the native retain count increases, retains the lease across backend clear/destruction, and checks its final release. Retention uses explicit `CFRetain`/`CFRelease`, not a bridge cast that becomes a no-op without ARC. The packaged app now compiles this backend with ARC, matching the test targets and the backend's strong-resource ownership assumptions.
2. Add a default-off `ColorPlanExecutionOptions::allow_metal_photographic_output`. Desktop in-memory previews opt in unless Metal preview is disabled. The existing Metal ACES output implementation returns validated CPU-readable pixels, so scopes, proofing, device transforms, and publication retain their canonical order. CPU reference/export behavior and LUT quality are unchanged. GPU failures preserve the input for CPU fallback; execution/cache identities and telemetry distinguish the accelerated path.
3. Extend the existing actual-app workflow driver with `--color-interaction-proof`, `--scopes-visible`, exposure publication/frame timing, a 30-update coalesced edit, and before/after window captures. Waits use a normal nested Qt event loop. Completion requires render work to finish and the native surface to reach the viewport-required resolution; a draft no longer counts as settled. Test callbacks disconnect before their captured state goes out of scope.
4. Cache the current source's dimensions in the controller, keyed by absolute path, file size, and modification time. This is a single bounded entry; invalid/missing files are not permanently cached. A host main-thread sample (`app-main-thread.sample.txt`) showed repeated `cropRectWidth/Height/X/Y` and source-size property reads entering `LibRaw::identify` and TIFF parsing during edits. Tests exercise 400 cached geometry reads with no additional decoder probes, file replacement, and missing/restored sources. This is the second performance optimization; no shader precision or color quality was reduced.

## Same-workload controller benchmark

Release build; native Metal texture backend; ten exposure edits after resident-source/color warmup; no overlapping builds or rendering workloads. RAW: 4208×3120 Nexus 6 DNG, scopes enabled. JPG: accompanying real reference JPG, scopes disabled. These are distinct workloads, not a codec comparison.

| Workload / metric | Fresh baseline | After output-stage acceleration |
| --- | ---: | ---: |
| RAW first feedback median / p95 | 14 / 15.55 ms | 15 / 18.2 ms |
| RAW settled 2560 median / p95 | 170 / 175.55 ms | 144.5 / 165.55 ms |
| RAW retained bytes, both cycles | 1,587,414,560 | 1,587,414,560 |

RAW settled median improved 15%, while first feedback remains within its absolute latency gates. Retained bytes are cache accounting, **not whole-process RSS**. This first optimization passes the stated controller-level workload gates. The older JPG first-feedback miss did not reproduce in the fresh baseline; no invented JPG speedup is claimed.

After the dimension-cache fix, isolated `final-controller-raw` measured **12 / 14 ms** feedback and **138 / 165.6 ms** settled output. `final-controller-jpeg` measured **13 / 13.55 ms** feedback and **44 / 44.55 ms** settled output. Retained bytes remained 1,587,414,560 (RAW) and 1,086,238,752 (JPG), identical in both cycles and to their respective baselines. Both workloads pass the stated controller gates. The RAW settled median is 19% below the fresh baseline; JPG settled time is effectively unchanged.

Evidence: `before-raw`, `before-jpeg`, `after-output-raw` directories and corresponding `.log` files. The driver is `interaction_workflow_benchmark_driver`, with `PHOTO_EDITOR_BENCHMARK_NATIVE_PREVIEW=1`, explicit `PHOTO_EDITOR_BENCHMARK_SCOPES_VISIBLE`, `--iterations 10`, and `--timeout-ms 60000`.

## Visible editor proof and important limits

The existing `actual_app_workflow_benchmark_driver` instantiates desktop controllers and loads the repository's real `Main.qml` into a Cocoa/Metal window. It uses temporary catalog fixtures and runtime directories. It loads QML from the filesystem, not the packaged app's ahead-of-time QML module; its timings must not be advertised as packaged-release display certification. Computer Use could not load its runtime, so this proof uses the app's test harness rather than manual pointer input.

`frameSwapped` after publication measures a completed QML frame, not physical scanout or an optical input-to-photon measurement. A frame already in flight may finish after publication; the separate settled frame barrier and final image-size checks provide the completion evidence.

Before the dimension-cache fix, RAW run `app-timing-raw` completed five isolated edits and a coalesced 30-update burst, reaching the native 4208×3120 surface. Isolated feedback took approximately 80 ms to publish, and full-resolution completion took roughly two seconds. Burst setters took at most 1 ms, but dispatching 30 updates took 2,071 ms; final render/frame completion took 4,512 ms from burst start. That baseline is **not a 60 Hz editing pass**. Full-resolution processing remains a separate CPU-quality path.

Final five-edit window runs (`final-app-raw`, `final-app-jpeg`) both completed successfully at native **4208×3120**, including the final +0.76 EV burst result. Before/after captures were visually inspected: the final image is present and sharp, with no draft/refining badge. RAW and the camera-processed reference JPG are not expected to look identical and are not independent color-accuracy references.

| Visible-window measurement | RAW before dimension cache | RAW after | JPG after |
| --- | ---: | ---: | ---: |
| First publication median / p95 | 80 / 86.2 ms | 51 / 55.2 ms | 67 / 88.8 ms |
| First frame after publication median / p95 | 88 / 88.8 ms | 56 / 59.4 ms | 80 / 91.4 ms |
| Full-resolution render/frame completion median / p95 | 2,161 / 2,286.4 ms | 2,089 / 2,122.2 ms | 2,120 / 2,151.6 ms |
| 30-update input duration | 2,071 ms | 1,420 ms | 950 ms |
| Frame swaps during input | Not separately recorded | 30 | 59 |
| Native color publications during input | Not separately recorded | 7 | 26 |
| Burst start through final full-resolution frame | 4,512 ms | 3,728 ms | 2,934 ms |

The RAW publication median improved 36%, and burst input duration improved 31%. Both burst setter maxima were 1 ms. Nevertheless, **neither visible-window workload passes the strict realtime latency gate**, and frame swaps alone do not mean every color edit was displayed. The renderer coalesces superseded edits.

The follow-up sample `app-main-thread-after-cache.sample.txt` identifies remaining main-thread work in Canvas painting/polish and QML binding evaluation; it also catches ICC diagnostics reading RAW metadata. That sample is diagnostic, not an uncontaminated timing benchmark. This pass stops after two retained optimizations: the next change requires component-level paint/invalidation and packaged-QML profiling rather than a speculative engine patch.

An early exploratory window run exited with signal 11. It did not reproduce under the debugger, which produced no crash stack. The native-resource ownership gap was independently identified and repaired, but is not presented as a confirmed explanation of that crash. Earlier exploratory captures/timings ending on a 256-pixel draft are rejected as settled/full-resolution proof.

## Verification

- Executor test: optional acceleration preserves all four CPU-readable output/proof/device/publish surfaces, proof gamut masks, output parity, separate cache entries, and truthful Metal dispatch when hardware is available.
- Metal ACES parity and native lease tests passed with `MTL_DEBUG_LAYER=1`.
- All 19 focused regression tests passed with Metal validation: color pipeline/property/reference/tool invariants; three Metal parity tests; render parity/executor; native working fusion; raster conversion; preview publication; three scope tests; feedback, coalesced undo, and pixel-zoom/source-size behavior. Evidence: `final-tests.xml` and `final-tests.log`.
- The complete Release `photo_editor_app` target built without new warnings/errors (`build-verified.log`); the production texture backend's generated compile command includes `-fobjc-arc`.
- The final window runs show `metal-canonical-cpu-readable`, one output LUT dispatch, and nonzero readback bytes on scoped draft previews. Publication telemetry is preserved through the CPU-readable image packaging path, not only the zero-readback native path.
- Final telemetry build: `build-final-telemetry.log`; no warnings/errors. Final regression/2560-pixel telemetry rerun: `final-confirm-tests.xml`, `final-confirm-tests.log`, and `final-confirm-controller-raw` (recorded separately from the earlier measured comparison).
- The final regression rerun again passed all 19 tests. The final RAW confirmation measured 12 / 13.65 ms feedback and 148.5 / 157 ms settled output, with unchanged retained bytes. Every inspected 2560×1898 settled row reports `metal-canonical-cpu-readable`, one output dispatch, and 77,742,080 readback bytes. The repeated median range (138–148.5 ms) remains inside the <150 ms gate, but leaves little margin on this host.

## Remaining work

Do not promote the engine to universally realtime or state of the art on these results. Next: identify individual Canvas repaint costs, avoid unnecessary offscreen/unchanged-content painting without stale controls or scopes, and measure equivalent packaged-build behavior. Then address the viewport/full-resolution scheduling boundary and CPU-quality refinement with unchanged color tolerances and measured memory limits. Mixed/local effects, independent real-camera color references, preview/export numerical comparisons, calibrated display/human validation, and other hardware tiers remain separate acceptance gates.
