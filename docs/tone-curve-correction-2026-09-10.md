# Tone curve interaction and highlight preservation

Selecting a curve handle previously moved it immediately to the cursor anywhere
inside its 28–36 pixel hit area. The editor now preserves the initial grab offset:
a click selects, and a five-pixel drag moves the handle five pixels. New points
start on the existing curve at the chosen input position, instead of jumping to
the clicked output level. Endpoint editing remains available.

Point curves previously clamped scene-linear channels to 0–1 during the sRGB
transfer. Activating even a straight curve could flatten distinct bright values
to the same value. The SDR portion of the curve keeps its existing response;
above 1, the renderer scales the original value by the linear white-endpoint
output. Below 0, it preserves signed values with the black-endpoint offset.
These extensions are continuous at the endpoints. The CPU evaluator, compiled
LUT sampler, native Metal shader, and retained adjustment-layer shader agree.
The compatibility tone path also retains scene headroom before the point curve.

Curve and layer render-stage identities and the desktop preview-cache version
were advanced so previously clipped previews are regenerated. Existing curves
can therefore recover highlight detail that the old rendering discarded.

The earlier diagnosis of a slider-to-point domain switch was too broad: the
desktop V13 adapter already converts parametric sliders to the same canonical
point representation. That mapping and the sliders' behavior remain intact.

## Verification

- `color_curve_headroom_test`: neutral curves preserve SDR, signed channels and
  extended highlights; small edits retain highlight separation; the white
  endpoint is continuous; adjustment-layer identity curves retain highlights.
- `tone_curve_interaction_test`: native mouse selection, grab offsets and
  insertion pass in normal and compact controls. Screenshots were inspected.
- `metal_aces2_output_lut_parity_test`: additional GPU identity cases preserve
  highlights up to 8 in RGB, Luma, each individual channel and adjustment layers.
- Canonical working fusion, color-tool invariants, V13 recipes, the desktop
  adapter and native curve preview/export tests pass. Eight focused tests pass
  in total (seven in the grouped CTest run, plus the headroom executable).
- The real `hdrplus-nexus6-skin-0218.dng` fixture has at most 1/255 channel
  difference for both draft/reference and final/export comparisons.
- Full-app JPEG before/after captures were inspected. These capture runs
  overlapped compilation and are visual evidence, not performance measurements.

Evidence: `/private/tmp/curve-fix-validation.log`, `curve-fix-raw-parity.log`,
`curve-fix-ui.log`, `curve-fix-ui/`, and `curve-fix-photo/`. Release and normal
development app builds contain the fix. The already-running app is not restarted
by this work; reopening the rebuilt app loads the changes.

This fixes curve interaction and range handling. The delayed live-preview
updates and evicted-cache recovery issue recorded in the earlier responsiveness
report remain separate outstanding work.
