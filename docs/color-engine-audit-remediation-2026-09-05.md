# Color-engine audit remediation — 2026-09-05

Status: verified rendering defects repaired; **not certified state of the art or universally realtime**. This report covers the existing dirty working tree at HEAD `13dcbbf794a0c5800f66124dd2e56d1069e05852`, not just that commit. Existing unrelated changes were preserved. No camera-profile, reference-corpus, or human-approval gate was waived.

## Repairs

- Preserve negative and over-range RGB at the tagged-raster/working-image boundary. A real Display P3 JPEG now retains its negative green/blue components when transformed into linear sRGB. Failed Qt conversions return failure instead of relabeling unconverted pixels.
- Repair Metal shader compilation: signed cube-root helper and correct address spaces, in both source and embedded shader copies.
- Remove four misplaced shader-only fields from `LookStageUniforms`. Metal validation previously found a 524-byte shader argument bound to a 508-byte CPU structure; this caused an apparent GPU hang.
- Size inactive layer buffers to their actual element types, not one byte. Simple fill-only layer stacks now pass Metal argument validation.
- Match CPU adjustment-layer ordering: apply the layer LUT before lens correction, then geometry; extract spatial luminance before applying layer dehaze. Preserve the downstream detail/compositing sequence.
- Use standards-preserving Metal math. Evaluate OCIO linear LUT interpolation explicitly in float rather than relying on hardware-filter precision. Nearest/tetrahedral generated lookups remain unchanged; unrecognized generated linear lookup forms fail closed. Added nontrivial 1D and 3D CLF regression fixtures, including out-of-domain inputs.
- Repair asynchronous native texture publication: shared events must not be rejected because their `device` is nil; deliver completion through Qt's event queue while retaining lifetime, generation, and stale-publication guards.
- Keep `GpuOnly` truthful: unsupported local-mask rendering fails explicitly; `Auto` can use the correct CPU fallback.
- Allow working-stage GPU fusion with visible scopes. Scopes can materialize the completed working surface; zero-readback display publication still requires separate eligibility and hidden scopes.
- Fix the missing Metal texture-factory compile definition in the retained-output test. Shader initialization failures no longer count as hardware skips. GPU tests now enable Metal API validation and have explicit timeouts.

Apple's installed `MTLEvent.h` documents that shared events have a nil device. OCIO's upstream [GPU filtering precision discussion](https://github.com/AcademySoftwareFoundation/OpenColorIO/issues/1763) is consistent with the interpolation error measured here; the measured CPU/GPU comparisons, not the discussion alone, establish this repair.

## Correctness evidence

Artifacts: `/private/tmp/photo-editor-color-fix-20260905/`.

- 30 distinct focused tests passed across `regression.xml`, `raw-contracts.xml`, and `scopes-regression.xml`. They cover CPU pipeline/property/reference/CMM/chart/tool tests; RAW/DCP contracts; ICC and creative LUTs; official ACES/OCIO reference; Qt conversion; preview/publication; scope behavior; and CPU/GPU render parity. GPU tests ran on the host GPU, not a no-device sandbox skip.
- The complete `photo_editor_app` Release target built successfully after the repairs (`build-app.log`). No new warning or error was reported in that build log. This is a build check, not a physical display-presentation measurement.
- Retained combined-edit maximum RGB differences from the unfused CPU reference: linear sRGB `0.000473`, P3 `0.001101`, ProPhoto `0.000609`, Rec.2020 `0.000513`, all below the unchanged `0.004` RGB gate. Point edits, geometry, and global detail separately differ by approximately `0.000004`. Layer prefixes and effect-order variants are also checked.
- The compound resampling/alpha gate is now explicitly four float32 machine epsilons (`0.000000477`). The old decimal `0.0000001` gate rejected a measured two-ULP difference (`0.000000119`) after geometry and compositing. RGB tolerances were not relaxed. The new staged comparisons explicitly reject nonfinite RGB/alpha.
- The OCIO cube maximum channel error fell from approximately `0.001823` to `0.00000012` before enabling safe shader math; final parity gates and the additional CLF fixtures pass with safe math enabled.
- Raster tests exercise eight source profiles against four selectable linear working spaces, source-linear round trips, transparency, signed/HDR values, and an actual tagged P3 JPEG.
- Three actual JPEG controller edit sweeps completed. Six DNG captures from three phone camera models each rendered through shipping camera-linear and portable characterized LibRaw paths: 12 finite outputs. Strict validation reports `valid-non-promotable-real-raw-render-set`. See `raw-renders/real-raw-render-set.json` and `raw-validation.json`.
- Downscaled regenerated RAW portrait and sunset images were visually inspected for gross corruption. These are not calibrated reference comparisons or photographer approval.

## Performance evidence and boundary

The bounded performance loop used a resident 4208×3120 Nexus 6 DNG, ten exposure changes, a 2560-pixel settled preview, explicit warmup, Release build, and host GPU access. No build or other rendering workload overlapped the reported measurements. The same simulated-texture benchmark route was retained for the before/after comparison.

| Measurement | Before | After correctness repairs | After scopes eligibility repair |
| --- | ---: | ---: | ---: |
| First feedback median / p95 | 14 / 22.1 ms | 14 / 15.55 ms | 13 / 15 ms |
| Settled preview median / p95 | 260 / 474.65 ms | 227 / 233.1 ms | 170 / 176.1 ms |
| Retained bytes, both memory cycles | 1,885,152,800 | 1,583,543,840 | 1,587,414,560 |

Settled median improved 35%; p95 improved 63%; retained bytes decreased 16%. The final run records Metal working-stage execution without a fallback reason. First feedback meets the `<16 ms` median / `<33 ms` p95 gates. Settled p95 meets `<250 ms`; **settled median still misses `<150 ms`**.

The native-texture benchmark can now be selected with `PHOTO_EDITOR_BENCHMARK_NATIVE_PREVIEW=1`; it fails explicitly if no usable GPU texture exists. `PHOTO_EDITOR_BENCHMARK_SCOPES_VISIBLE=0/1` selects an explicit scopes scenario. These are benchmark-only controls; normal app defaults are unchanged. A native-texture run before the scopes eligibility repair measured 16/18.65 ms feedback and 232.5/241 ms settled output, still using CPU working edits because scopes were visible. It is not presented as a GPU speedup.

A separate, isolated **native Metal / scopes hidden** run after the repairs measured **12/12 ms first feedback and 49.5/63.75 ms settled output**, with both memory cycles at 1,194,833,440 retained bytes. It passes the stated latency gates for that workload. Evidence: `native-raw-isolated/desktop.jsonl` and `native-raw-isolated-console.log`. This is a different scenario from the before/after table, not a 260-to-49.5 ms same-workload comparison. The earlier exploratory `native-raw-scopes-hidden` run overlapped the final benchmark build and is excluded from performance claims.

The matching **real JPEG / native Metal / scopes hidden** exposure workflow completed ten iterations at **33/34.55 ms first feedback and 104.5/132.1 ms settled output**. Both memory cycles were 1,555,044,384 retained bytes. Settled latency passes; first feedback misses both gates. Evidence: `native-jpeg-isolated/desktop.jsonl` and its console log. The benchmark skips the RAW-specific pyramid warmup for non-RAW files, but retains normal preview and color-transform warmup. JPEG and RAW input dimensions/content differ, so these are separate workload results, not a codec speed comparison.

The correctness repairs and the scoped GPU eligibility repair are retained. The full performance gate is not closed. Further optimizing scope/output materialization is a distinct follow-up, not grounds for loosening color precision or presenting unlike benchmark scenarios as a speedup.

## Still required for a defensible top-tier claim

- Broader real camera/container/sensor coverage and independently measured references, including licensed camera profiles and recorded human review.
- Extreme and mixed recipes across every supported input/output encoding, masks, retouch, ICC proof/device targets, and HDR/print outputs. Four working spaces do not mean every output encoding was exhaustively certified.
- Further settled-preview optimization; supported local masks/retouch and image-dependent effects can still use CPU fallback.
- Physical screen-presentation/frame-time measurements, calibrated monitor/HDR evaluation, and non-Mac GPU/hardware coverage. Controller publication latency is not physical display latency.

The existing promotion strategy remains authoritative: `docs/color-engine-and-app-foundations-completion-strategy.md`. No synthetic or self-referential result was substituted for an external acceptance gate.
