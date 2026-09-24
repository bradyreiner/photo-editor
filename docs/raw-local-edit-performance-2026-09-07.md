# RAW local-edit performance loop

Status: attempt 1 rejected and reverted; attempt 2 passes the preview speed,
pixel and memory gates. Mask-set adjustment setup is now compiled once per
render. This is a scoped CPU local-edit speedup, not realtime certification.

## Scope and gates

Use the actual `render_preview_request_for_test` entry point with the real
HDR+ Nexus 6 DNG already used in `raw-neutral-preview-2026-09-07.md`.
The performance-loop skill governs acceptance: at most three targeted attempts,
at least 15% lower median real local-temperature render time, no failed pixel
checks, no more than 5% higher median comparison latency, and no more than 10%
higher peak resident memory. Two attempts have been measured: one reverted, one
accepted. Do not repeat the rejected attempt or stack unrelated optimizations.

These runs use ACES 2, 1600x1186, an in-memory CPU image, isolated app/cache data,
and `/usr/bin/time -l` for host memory. They are worker measurements, not native
texture or input-to-photon latency. Requiring a CPU image deliberately excludes
the native working-surface fusion route. No conclusion about that route follows
from these CPU timings. The source file is never modified.

## Measurement changes

`legacy_preview_locality_driver` now retains the existing decode, linearization,
pipeline, output, histogram and detail timing fields in each proof receipt.
The initial timing probe is:
`/private/tmp/framehouse-raw-timing-baseline-yocxZe/receipt.json`, with memory in
`/private/tmp/raw-timing-baseline.log`.

Its cold neutral render took 371 ms: 65 ms decode, 34 ms pipeline and 234 ms
output. Warm masked work took 94 ms: 0 ms decode, 65 ms pipeline and 22 ms output.
The cold output setup and warm working pipeline are separate costs. Timings can
be nested; these fields are not all additive exclusive stages.

The driver now also performs a real +12 temperature adjustment through a 16x16
half-image segmentation mask, not just a zero-alpha control. It requires visible
image change and exact preservation of every pixel in the rightmost quarter,
outside the mask's bilinear transition. It writes `local-temperature.png` and a
`localTemperature` receipt section. This is a deterministic local-edit test, not
semantic segmentation or photographic quality certification.

## Attempt 1: scalar mask-set refinement object

Hypothesis: constructing a complete `LocalMask` per pixel just to supply seven
scalar refinement settings contributes materially to the warm working cost.
The candidate used a compact scalar object and shared the unchanged refinement
arithmetic with the source-mask path. No renderer, precision, RAW decode,
sampling, resolution or local-edit semantics were changed.

| Metric | Baseline samples | Candidate samples | Decision |
| --- | --- | --- | --- |
| Local temperature render ms | 155, 161, 175 | 144, 154, 160 | Median 4.35% faster; misses 15% gate |
| Local temperature pipeline ms | 129, 137, 152 | 121, 129, 137 | Diagnostic only |
| Transparent-mask render ms | 89, 104, 97 | 86, 85, 92 | Diagnostic only |
| Ordinary comparison render ms | 120, 123, 116 | 113, 115, 123 | Regression guard passes |
| Maximum RSS bytes across runs | 1,197,391,872 | 1,197,244,416 | Memory guard passes |

Baseline directories under `/private/tmp`:
`framehouse-local-mask-baseline-bhmWKr`,
`framehouse-local-mask-baseline-1ECLTF`,
`framehouse-local-mask-baseline-rXZv0I`.
Candidate directories:
`framehouse-local-mask-candidate-Cj4hir`,
`framehouse-local-mask-candidate-TbEa7K`,
`framehouse-local-mask-candidate-yFgCRZ`.
Each directory has `receipt.json`; each sibling `<directory>.log` contains
the memory measurement. An initial command was rejected by the driver's
empty-output-directory guard because its log was inside that directory;
`framehouse-local-mask-baseline-sWZAD0` is not a benchmark sample.

All six real-image local-edit PNGs have SHA-256
`466108101c3f0e14336f4c7d41f566c1ad18be654abf3227b964d3796c569c89`.
All comparison, transparent-mask, protected-region and disk-cache gates pass.
The baseline local-temperature output was visually inspected. Its visible RAW
noise remains unresolved.

Result: candidate reverted with a scoped reverse patch. The median gain was
small relative to run variability and below the acceptance gate. No engine
optimization from this attempt remains. The added timing/protected-region
coverage remains, along with 48 combinations in `editor_preview_math_test`
checking source-versus-set scalar refinement order for inversion, expansion,
smoothing, fine edges, edge contrast, density and opacity.

## Final verification after reverting

The app and focused targets rebuilt successfully; the selected build log has no
warnings or errors: `/private/tmp/raw-mask-final-restored-build.log`.
Seven tests pass in 5.42 seconds in `/private/tmp/raw-local-edit-final-tests.log`:
color pipeline, canonical native working fusion, preview math, preview history,
preview feedback and both portable reference workflows. The 48 new refinement
combinations pass on both the candidate and restored implementation.

All expanded real-file checks pass after restoration, including the actual local
temperature edit and untouched-region guard:

| Input/model | Final proof directory under `/private/tmp` |
| --- | --- |
| DNG / legacy | `framehouse-local-edit-final-CdcyIS` |
| DNG / ACES 2 | `framehouse-local-edit-final-4b2OaD` |
| JPG / legacy | `framehouse-local-edit-final-RVIBSk` |
| JPG / ACES 2 | `framehouse-local-edit-final-xtTZGo` |

These final functional runs overlap the focused test run and are not substitute
performance samples. The separately recorded broader source-contract failure
remains outside this selected test set; no full-suite pass is claimed.

## Attempt 2: compile local adjustment setup once per mask/render

The equivalent-global branch of `apply_local_adjustment_mask` rebuilt its
`ColorAdjustmentState` and input-stage parameters for every affected pixel.
`CompiledLocalAdjustment` now prepares those immutable values in the mask-set
descriptor before workers start. It has render-local ownership, no global cache
or pointer into mutable UI state, and is compiled in the same working context
used by the pixel evaluator. Mask composition, alpha weighting, arithmetic and
output transforms are unchanged. Specialized local hue/colorization and the
existing single-brush exposure route retain their original paths. Legacy
ungrouped engine mask callers without a descriptor retain the original evaluator.
The desktop compiler supplies mask sets for normal photographic selection edits.

The refreshed, isolated three-run baseline and candidate used exactly the same
1600x1186 ACES 2 DNG scenario and process-level memory measurement:

| Metric | Refreshed baseline | Candidate | Gate |
| --- | --- | --- | --- |
| Local temperature render ms | 150, 152, 156 | 95, 98, 103 | Median 35.53% faster; pass |
| Local temperature pipeline ms | 128, 127, 131 | 72, 76, 78 | Median 40.63% faster |
| Ordinary comparison render ms | 116, 142, 124 | 124, 114, 118 | No median regression; pass |
| Maximum RSS bytes across runs | 1,197,326,336 | 1,196,736,512 | No growth; pass |

Baseline directories under `/private/tmp`:
`framehouse-local-setup-baseline-ipyJSh`,
`framehouse-local-setup-baseline-UNxDzr`,
`framehouse-local-setup-baseline-anYDo7`.
Candidate directories:
`framehouse-local-setup-candidate-W9i6wt`,
`framehouse-local-setup-candidate-gET4tW`,
`framehouse-local-setup-candidate-xZh9bN`.
Receipts and sibling `.log` files have the same layout as attempt 1. Candidate
local-temperature PNGs retain the baseline SHA-256 recorded above.

`compiled_local_adjustment_test` compares the new path directly with the retained
per-pixel evaluator: **33,600 bit-exact comparisons**, covering Linear sRGB,
Display P3, ProPhoto RGB and Rec.2020, legacy and ACES 2 rendering, Bradford/CAT16,
two headroom settings, ten individual positive/negative controls, mixed controls,
specialized-color fallback, six mask weights and seven input pixels including
negative/extended RGB and nonopaque alpha. This proves setup equivalence, not
independent colorimetric accuracy of both evaluators.

Eight rebuilt focused tests pass in 7.58 seconds:
`/private/tmp/local-setup-final-tests.log`. The app and selected targets build;
no compiler warnings/errors appear in the selected logs. Final real-image
preview runs pass all comparison, disk-cache, transparent-mask and actual-local
protected-region gates:

| Input/model | Final directory under `/private/tmp` | Local-edit PNG SHA-256 |
| --- | --- | --- |
| DNG / legacy | `framehouse-local-setup-final-v4g9yR` | `cb46f4c5511db290a02a42f76a348863c3e3dba1f36b4713b2e35f229b0d7bae` |
| DNG / ACES 2 | `framehouse-local-setup-final-JyiZaG` | `466108101c3f0e14336f4c7d41f566c1ad18be654abf3227b964d3796c569c89` |
| JPG / legacy | `framehouse-local-setup-final-9GOssD` | `e653d9247074ceabd8ecdfca747298b4315cd491774a0bd671d1555c0e4e95f0` |
| JPG / ACES 2 | `framehouse-local-setup-final-zc0Wll` | `54118a0fc623e35552e847a4b6e14131cdcfcdf2fff69a0212261435d01f6397` |

Each PNG is byte-identical to its corresponding pre-optimization final proof.
The final legacy RAW local-edit image was visually inspected; noise remains.
Final functional runs overlapping CTests are not performance samples.

The two broader rebuilt engine checks, `render_parity_test` and
`render_golden_test`, also pass in 4.11 seconds:
`/private/tmp/local-setup-render-regression-tests.log`. That is ten passing
focused tests across two runs, not a claim that the entire repository suite is
green. The separately recorded source-contract failure remains outside this set.

Full-resolution ACES 2 checks also pass at 4208x3120:
`/private/tmp/framehouse-local-setup-full-igkURJ/receipt.json` (DNG) and
`/private/tmp/framehouse-local-setup-full-CuLwmS/receipt.json` (JPG). All
transparent-mask/reference invariants, missing-reference and disk-cache gates,
plus the actual local-temperature untouched-region check pass. Local-edit worker
times were 734 ms RAW and 718 ms JPG, with zero decoded-cache miss time. These
single functional runs have no paired full-resolution performance baseline and
must not be advertised as a full-resolution speedup or realtime result.

## Remaining work

The median 98 ms worker render still misses the 80 ms composited-mask draft
budget; it also does not measure input-to-photon latency. Native RAW acceleration,
full-resolution latency, loading, embedded inference, Gaussian 4D/film and
complete editing tools remain open in `active-app-completion-goal.md`.
