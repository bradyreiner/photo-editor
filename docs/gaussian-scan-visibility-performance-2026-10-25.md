# Native Gaussian visibility performance — 2026-10-25

Native Metal acceleration structures reduce the retained dense scan/environment
recipe from **50,539.27 ms to 365.41 ms** per warm frame on this M4 Pro / 48 GB
host. Resolution, Gaussian data, sample counts, camera poses, lighting and
opacity conventions are unchanged. Two of three comparison images are exactly
equal; the third differs by one 8-bit value in one channel of one pixel.

The actual dense SceneWorkspace workflow also passes with its original
**20,000 ms per-action limit**, including pointer toggles, undo/redo, effect
retention, frozen photo/film exports, a concurrent lighting edit, reopening,
shot recall and compact-window toggles. Its retained final 2048×1364 frame takes
375.68 ms. This closes the previously failed dense-environment action gate on
this host; it does not establish sustained input-to-photon performance or
complete reference-video parity.

## Controlled loop

The baseline uses the exact final camera and composition retained from the
previous native workflow, including the 1,244,819-point Bonsai artifact,
2048×1364 pixels, scan shadows, 16 light samples and captured environment
illumination. Three full frames use yaw offsets 0, .01 and .02 degrees. The
first includes renderer/index setup; the latter two force a new shading result
and define the reported warm median. These are two measured warm poses, not a
latency percentile or a clean-machine benchmark.

Before changing production code, the recorded gates were: at least 20% lower
warm render time; maximum RGB difference 1/255; at most 10% regression in cold
full-frame rendering, peak process RSS and direct-light median; at most three
optimization attempts. Plan: `/private/tmp/looksy-visibility-perf-plan-v1.json`.

Separate CPU diagnostics found 721 mean node visits and 748 mean particle tests
per sampled visibility ray, with only 35 contributing particles. They describe
the software tree at a sampled receiver point, not GPU instruction counts or
the native acceleration structure. No instrumentation is added to the timed
GPU shader.

1. A bounded surface-area split experiment improved warm rendering by 12.64%,
   with maximum RGB difference 1. It missed the 20% gate and was reverted.
2. Native Metal traversal passes the performance, image and memory gates and
   is retained. The software implementation remains available for unsupported
   devices and explicit fallback qualification.

The initial direct-light baseline overlapped compilation. It is excluded and
replaced by the unloaded `direct-baseline-v2` run; it supports no performance
claim here. All accepted before/after timings run without a concurrent build.

## Result

| Metric | Software baseline | Native Metal | Change |
| --- | ---: | ---: | ---: |
| Warm full-frame median | 50,539.27 ms | 365.41 ms | 99.28% lower |
| Cold renderer/index frame | 53,728.29 ms | 1,088.71 ms | 97.97% lower |
| Direct-light median, 800×533 | 65.94 ms | 44.96 ms | 31.81% lower |
| Peak process RSS | 1,045,168,128 B | 1,083,916,288 B | 3.71% higher |
| Peak platform memory footprint | 1,522,812,080 B | 2,019,247,424 B | 32.60% higher |
| Retained visibility index | 92,309,808 B | 173,052,096 B | 80,742,288 B higher |

The larger retained index is reported separately from total process RSS.
The broader platform memory-footprint counter also grows 32.60%; the predefined
RSS guard passes, but native acceleration has this additional memory cost.
That platform peak is not a steady-state index or a dedicated GPU-memory
measurement. Native
construction avoids the temporary software tree and uses the driver-managed
structure; retained-index memory is not identical to peak construction memory.
The native warm frames take about 300 ms GPU time and 175 lighting submissions,
with a maximum measured submission of 6.38 ms. No guaranteed GPU submission
latency is inferred from the adaptive scheduler's 8 ms target.

Comparison: `/private/tmp/looksy-visibility-perf-native-compare-v3.json`.
Baseline/candidate frames and complete metrics:
`/private/tmp/looksy-visibility-perf-baseline-v1/receipt.json`,
`/private/tmp/looksy-visibility-perf-native-v3/receipt.json`.
The adjacent `*-time.log` files retain host peak RSS. Direct-light guard:
`/private/tmp/looksy-visibility-direct-software-v3/receipt.json`,
`/private/tmp/looksy-visibility-direct-native-v3/receipt.json`.

## Implementation and fidelity

The native path builds conservative bounds from the same original Gaussian
scale/rotation factors, conditional temporal factors, mean and opacity. It
uses a custom bounding-box intersection function to evaluate the same maximum
response on the finite light segment. The ray payload accumulates the same
product transmittance, cutoff, selected alpha cap and termination threshold.
The geometry explicitly disables duplicate intersection-function invocation;
counting a particle twice would change opacity. Captured appearance and the
primary rasterizer are unchanged.

The custom intersection, payload and linked-table mechanism follows
[Apple's Metal ray-tracing API](https://developer.apple.com/videos/play/wwdc2020/10012/).
The backend is selected when the device supports ray tracing and function
pointers. `LOOKSY_GAUSSIAN_SCAN_SOFTWARE=1` explicitly selects the retained
software path for diagnostics. `scanShadowBackend` identifies the executed
path in frame metrics. The scene format and normal user controls do not change.

Native buffers and acceleration structures are cached with the immutable scene
and selected temporal instant. They are replaced on scene/time changes.
Indirect particle buffers are explicitly declared as read resources, and
disabled/empty visibility binds valid dummy resources. Builds complete before
publication. Acceleration geometry, particle buffers and the time key commit
together after the cancellation check; a cancelled rebuild preserves the
previous complete cache. Cancellation cannot publish an incomplete frame.

**23/23 focused tests pass** in 108.13 seconds:
`/private/tmp/looksy-visibility-native-ctest-v2.log`.
This includes native and forced-software opacity, overlap, anisotropy,
thin-factor, finite light-distance, time, alpha-cap, environment, soft-source,
exact tiling and observed-batch cancellation checks, a cancelled time rebuild followed by an exact revisit of the previous time, plus the existing UI,
material, persistence and export tests. Native/software oracle receipts:
`/private/tmp/looksy-visibility-native-oracle-v1/receipt.json`,
`/private/tmp/looksy-visibility-software-oracle-v1/receipt.json`.
The dense native action proof is
`/private/tmp/looksy-visibility-native-public-v2/receipt.json`; its wait limit
remains 20,000 ms. Captures were visually inspected.

The final packaged Main application also passes the scoped environment/scene
proof at 2700×1799, with native scan visibility, an exposed window and no preview
readback. Its cold render call is 1393.84 ms. Receipt/capture:
`/private/tmp/looksy-visibility-native-main-v2/receipt.json`,
`environment-lighting.png`. The window is inactive; this is not foreground
sustained-interaction or catalog/agent qualification.

The final native exporter produces a twelve-frame lossless master from the
frozen dense film recipe. Independent decode/native replay verifies all frames
and integrity hashes with maximum channel error zero. The earlier retained
bright-source master also replays all twelve frames exactly. Receipts:
`/private/tmp/looksy-visibility-native-sequence-v2/camera-path.json`,
`/private/tmp/looksy-visibility-native-sequence-replay-v2/receipt.json`,
`/private/tmp/looksy-visibility-native-legacy-replay-v2/receipt.json`.
These are 256×170 checks; they do not extend the earlier 8K qualification to
shadow-enabled rendering.

`/private/tmp/looksy-visibility-native-source-binary-v1.json` pins 105 relevant
source files, sixteen binaries and the qualification receipts. It records the
accepted second attempt, the passed dense action gate and the remaining full
video/interaction qualifications. The benchmark entry point is
`gaussian_scan_shadows_test --visibility-perf PROJECT RETAINED_UI_RECEIPT NEW_OUTPUT_DIRECTORY`;
the output directory must be new and absolute. The retained input project is
`/private/tmp/looksy-scan-shadows-public-v4/project`, and its input receipt is
`/private/tmp/looksy-scan-shadows-public-v4/receipt.json`. Source/composition
identity is compared explicitly in the paired result.

## Remaining qualification

These measurements cover this retained scene on one M4 Pro. Other devices,
larger scene/object combinations and sustained native input-to-photon behavior
remain separate gates. The software fallback retains its earlier performance
limits. Appearance-opacity visibility does not recover measured extinction,
spatial probe radiance, inverse capture lighting or multibounce transport.
Representative source reconstruction, original-rasterizer convention auditing,
shadowed volumes and matched photographic comparisons remain open in the
[reference parity report](gaussian-reference-parity-2026-10-24.md).
