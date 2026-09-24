# Local color edits: native delivery and final-quality routing

## Fixed defects

Two distinct defects were exposed by a real-controller local-temperature probe:

1. Native-only preview results used full-source output dimensions in three
   publication branches. A 512x379 surface from a 4208x3120 DNG was reported as
   4208x3120. Native upload/adoption checks rejected the mismatched extent, and
   no new native revision appeared. These branches now use the existing
   preview-size/crop calculation, including max_edge. Edge-zero full-resolution
   requests retain the full coordinate space.
2. Local color setters submitted mask_rasterization / mask_preview_change.
   That path intentionally omits local adjustments and layers for geometry
   feedback and suppressed full-resolution scheduling. Merely repairing texture
   delivery therefore produced a fast but incomplete image.

Fourteen local color operations (exposure, contrast, highlights, shadows, whites,
blacks, temperature, tint, vibrance, saturation, hue/saturation shift and
colorization hue/strength) now submit local_adjustments / edit_change with
full-resolution scheduling. Their draft retains local masks/adjustments and
retouch rather than clearing them. Existing high-frequency-detail reductions
remain; settled/full work restores the exact pipeline. Other geometry/brush/
metadata operation routing is unchanged, not certified complete by this change.

No source resolution, mask precision, output transform or RAW decode math was
reduced to improve the result. No catalog/model data or originals were modified.

## Native controller/pixel proof

interaction_workflow_benchmark_driver now has --local-temperature. It requires
a real native texture, uses isolated data/cache/preferences and a closed catalog
port, loads the photographic source with a deterministic half-image mask, and
drives the same coalesced edit lifecycle and mask-temperature setter used by QML.

Setter admission, first native handoff and completion of queued full work are
separate events. After timing, a bounded Metal blit reads back the actual
published texture. Consecutive final images must differ, the protected rightmost
quarter must be identical, and the full generation must match current state.
The final probe also requires native dimensions to match full source/crop size.
Optional --local-crop and --local-output provide cropped coverage and new
temporary PNG proofs. This is not an exposed-window input-to-photon benchmark.

Evidence under /private/tmp/local-mask-controller-proof:

- 20260908T064144.954021Z-perf-interaction-workflow-a81ebfe0: real RAW failure
  before the extent repair; render completed without native handoff.
- 20260908T064534.256448Z-perf-interaction-workflow-0430260b: extent-only repair
  delivered 512x379 drafts, but fullGeneration stayed zero. Its 14.5ms median
  handoff is explicitly rejected as evidence of correct local editing.
- 20260908T065710.659568Z-perf-interaction-workflow-fbe60d64: corrected routing,
  three real RAW edits, full 4208x3120 native images, distinct hashes and zero
  protected-region differences.
- 20260908T065822.136162Z-perf-interaction-workflow-d0790b6d: matching real JPEG
  workflow and native-pixel checks pass.
- 20260908T081529.400032Z-perf-interaction-workflow-44be0c5a: three real cropped
  RAW edits pass the final strict-size/generation/pixel checks. Settled output
  is 3366x2496 for the 80-percent crop; protected pixels remain unchanged.

Those RAW/JPEG runs overlapped unrelated compilation and are functional evidence,
not accepted latency comparisons. Native RAW PNGs in
/private/tmp/local-mask-native-raw-frames and the final JPEG native PNG in
/private/tmp/local-mask-native-jpeg-frames were visually inspected. Existing
visible RAW noise is not fixed or explained by this work.

## Regression and app checks

All nine focused CTests passed in 12.38 seconds: compiled local adjustment,
luminance projection, mask coverage cache, edit render policy, preview math,
uncropped/cropped native local interaction, and legacy/ACES2 preview references.
Log: /private/tmp/local-color-final-regressions.log.

The broad editor_preview_controller_test was rebuilt, and its older local-color
contract was updated to require local_adjustments with deferred full work.
However, execution fails earlier at the retry fixture's missing last-good
image-provider preview (line 3961), both normally and with Metal preview disabled.
The updated downstream assertions therefore have not executed in that suite.
No retry or global-slider assertions were weakened. Logs:
/private/tmp/local-color-controller-contract-test.log and
/private/tmp/local-color-controller-contract-cpu-test.log. This failure needs a
separate isolation; the broad suite is not claimed green.

The rebuilt development app completed its isolated RAW-base/JPEG-placed-layer
smoke test with receipt ok=true, QML placement, published preview, color-managed
source thumbnail, undo/redo and PNG export. Receipt and visually inspected
startup/export PNGs: /private/tmp/local-color-app-proof-oLPWsG. The screenshot
contains the expected catalog-recovery banner because this proof deliberately
uses a closed catalog port; it does not test the user's catalog. The private
runtime started, but installedModels is empty. No model quality claim follows.
This app smoke does not substitute for direct mask-slider input-to-photon proof.

## Rejected CPU optimization

The performance-loop skill governed a separate bounded attempt: at most three
candidates, >=15% warm-median improvement, exact ordered image hashes, p95 within
5%, first-mask time within 20%, and peak resident memory within 10%.

A fresh full-resolution CPU baseline followed by a separate sampled run found
repeated full RGB-to-XYZ transforms used only for luminance. A Y-row-only
candidate passed 500,048 exact projection comparisons, 1,135,872 compiled local
adjustment comparisons and all 24 image hashes, but did not improve performance:

| Metric | Baseline | Rejected candidate |
| --- | ---: | ---: |
| Warm median ms | 657 | 672 |
| Warm p95 ms | 762 | 767 |
| First mask ms | 1399 | 1338 |
| Peak resident bytes | 2606956544 | 2606350336 |

The candidate was reverted with a scoped patch. No luminance engine optimization
remains. The projection test remains as a numerical regression. The skill stopped
this micro-optimization path after one failed attempt; the native routing defects
above are correctness repairs, not a relaxed performance-gate acceptance.

Artifacts:
/private/tmp/raw-local-next-baseline-0yIk2M,
/private/tmp/raw-local-next-profile-Evbdzl,
/private/tmp/raw-luminance-candidate-oNgZyU,
/private/tmp/raw-luminance-candidate-comparison.json.

## Remaining work

Measure and accelerate the correctly masked draft/settled path in foreground.
Do not compare it with omitted-effect drafts or advertise CPU-only worker timing
as UI frame rate. Complete geometry/refinement release behavior, wider camera/
working-space/colorimetric evidence, full professional tools and the other
active-goal areas remain open.
