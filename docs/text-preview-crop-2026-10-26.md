# Text, live preview, and crop repairs

This work targets three reported workflows: styling different words in one text
box, seeing edits while moving a slider, and cropping/leveling a photograph.
Existing unrelated checkout changes are retained.

The follow-up report adds unlocked base-image reordering and shared brush/eraser
edge correctness and latency. The supplied screenshot shows a pale rim around
an erased transparent region. The user confirmed that it remains after release;
its cause must be established from settled pixel comparisons, not inferred from
cursor appearance alone.

## Verification contract

Text formatting must survive recipe serialization, content edits, undo/redo,
and rendering. Selecting words must let the user change their font, fill, and
outline without changing the rest of the layer. With no selection, the existing
whole-layer controls remain available.

Live-preview changes use the same fixture and benchmark before and after each
candidate. The bounded loop permits at most three candidates, with a 15% target
latency improvement and a 5% protected regression cap. Color fidelity, final
resolution, source isolation, and grouped undo are correctness gates. Missing
native presentation or host metrics must be reported as an evidence limitation.

Crop must expose aspect, automatic leveling, manual horizon, reset, and exit in
one coherent workflow. A horizon correction must respect image aspect and be
undoable. Photos without a reliable horizon must retain their existing rotation.

## Manual review

1. Open a disposable photograph and create a text box with several words.
   Select one word, change its font, fill, and outline, then repeat with another
   word. Edit the copy, undo/redo, save/reopen, and export. Inspect both the live
   canvas and exported image.
2. Hold an Exposure slider drag for several seconds. Confirm the photograph
   changes during movement and becomes sharp after release. Repeat with a text
   layer and inspect undo/redo.
3. Open Crop on a tilted landscape. Try automatic leveling, undo it, then draw
   a manual horizon. Change aspect, move/resize the crop, reset, and finish.
   Repeat on a portrait and an image without a clear horizon.
4. Unlock the base image, change its position relative to an obvious colored
   layer, undo/redo, save/reopen, and export. Confirm layer order matches pixels
   and the source is not composited twice.
5. Paint the same stroke with Selection, Paint, and Eraser, including partially
   transparent edges over dark and bright pixels. Check cursor/coverage geometry,
   live and settled output, and undo. Inspect eraser edges on both light and dark
   backgrounds, and compare exported alpha with the preview.

## Results

The focused `photo_auto_horizon_test` passes 15 known-angle cases across
landscape, portrait, and downsampled images. It also refuses featureless, tiny,
ambiguous, partial-width, and steep-line fixtures. These generated fixtures
establish geometry and refusal behavior; photographic quality and the integrated
Crop workflow require the separate native check.

The freshly rebuilt preview-feedback baseline passes before scheduling changes.
An additional ten-case comparison against the initially unchanged text renderer
passes with identical typography pixels. The final changes preserve existing
rendering for styles without runs. This document does not claim universal realtime
performance or broad photographic qualification.

The persistent bright eraser edge has a concrete display-boundary defect:
straight-alpha ARGB32 and half-float images, and straight native GPU surfaces,
were passed to Qt Quick's premultiplied texture material. The display adapter
now converts CPU uploads and uses a GPU conversion pass for native surfaces;
the engine/export source remains straight-alpha. Physical Metal checks cover
translucent image uploads, regional patches, already-completed and deferred native
producers through asynchronous publication, and the existing color parity suite. The pure display-alpha
check also covers light/dark backgrounds and preserves input images. A Qt integer
premultiplied-to-half-float conversion path lost alpha on this host; the adapter
unassociates in source precision before that conversion, with a regression check.

The production Paint/Eraser stroke now uses the same physical circular falloff
and centered one-pixel hard-edge antialias band as the shared brush coverage
math. The focused edge test checks fractional centers, small/large hard and soft
tips, committed recipe pixels, and the independent selection raster. Selection's
existing minimum normalized radius remains 0.01; its effective radius is checked
independently for smaller requests, with direct cross-tool equality whenever
the effective radii match. The hard-edge boundary check explicitly requires
opaque, half-alpha, and clear samples at radii 19, 20, and 21 for a 20 px tip.

The initial slider diagnostic baseline rendered drafts in approximately 17–23 ms,
but published no native or overlay frames during the held input. Three runs'
first native frames arrived at 3066, 3237, and 3143 ms (median 3143 ms), after
input ended. These baseline runs correctly failed the required live-feedback
gate. Those initial runs lacked independent foreground/occlusion samples and
are diagnostic only; they cannot support a percentage improvement claim.
The candidate extends the existing same-transaction publication guard to
global tone/color drafts, with asset/path/mask/source/transaction isolation.
It also accepts already-completed native producers that legitimately have no
shared ready event. Previously these valid surfaces were rejected.

The corrected Cocoa/Metal proof has two valid before and two valid after
Exposure-drag runs. All four presented native frames during the 30 held updates;
all 175 before and 195 after AppKit/CGWindow foreground samples qualified, with
zero unqualified samples. A third before attempt failed foreground qualification
before held input and contributes no timing result. The matching runs and their
source hashes are recorded in the
[slider A/B receipt](evidence/text-preview-crop-2026-10-26/slider-ab-receipt.json).

The native `desktop_shell_photo_tools_test` passes with the Crop workspace and
native pointer input. Its Auto Level checks use a generated 800×400 tilted
horizon fixture, refuse an ambiguous no-horizon photo without changing its
rotation, verify repeat idempotence and one-step undo, and cancel an in-flight
result when Crop closes. The workflow also checks crop geometry, a manual
rectangular horizon and undo, inpaint history, and all three Compare modes. The
[Auto Level capture](evidence/text-preview-crop-2026-10-26/crop-auto-level-native.png)
and [run receipt](evidence/text-preview-crop-2026-10-26/crop-workflow-receipt.json)
are included here. This proves UI and known-geometry behavior; the generated
horizon does not establish broad photographic detection quality.

Text dragging, corner resize, edge resize, and Selection brush have separate
held-pixel checks. Text probe screenshots verify rendered glyphs/reflow with
guides hidden and exact undo/redo; box movement alone is not the gate. An initial
corner run was excluded until its overlapping window was identified.

The small overlap was the documented macOS system cursor plane. The sampler now
records it separately; every other overlapping window still fails qualification.
Application activation also waits, without retrying activation, for up to 1.5 s
for foreground transitions to settle. Lock-screen and interrupted-focus runs are
excluded. A temporary `caffeinate -d -w <test-pid>` assertion prevents display
sleep only for each disposable benchmark process.

## Accepted regional paint candidate

The regional render plan omitted the full preview's display ICC asset/target.
Its patch therefore used a different output path and the encoding guard correctly
fell back to a complete preview. The patch plan now inherits or derives the same
profile and target; the encoding guard remains in place.

Matched Cocoa/Metal runs used the same 6000×4000 photographic fixture, the
96 px solid-brush default, five trials per mode, and actual changed midpoint
pixels while held. All before/after window samples qualified, and all five
strokes in each run changed the expected pixels. An earlier Base Image Eraser
baseline is retained as a diagnostic but excluded because its initial window
sample was not qualified; the table uses a new five-trial baseline against the
same frozen before-candidate binary and fixture.

| Operation | Median visible response before | After | Improvement |
| --- | ---: | ---: | ---: |
| Base Image Eraser | 90.3 ms | 30.1 ms | 66.7% |
| Paint-layer Eraser | 84.4 ms | 27.6 ms | 67.2% |
| Paint | 76.9 ms | 31.0 ms | 59.6% |

All fifteen after-candidate drafts used `paint-region`, uploading 11,934–21,658
pixels instead of 698,368. Paint's median commit time changed by 0.26%; the
Base Image Eraser changed by +1.05%, and the Paint-layer Eraser median improved
by 9.3%; all are within the 5% protected regression cap. Peak RSS fell in all
three modes. The regional patch inherits the full preview's display profile and
target, and pixel parity checks retain the encoding guard. These latency results
apply to the measured fixture and brush, not to a universal frame-rate guarantee.
The
[comparison summary](evidence/text-preview-crop-2026-10-26/paint-eraser-comparison.json)
and [qualification receipt](evidence/text-preview-crop-2026-10-26/brush-regional-receipt.json)
record the accepted comparisons, qualified Base Image Eraser rerun, and excluded
diagnostic baseline.

## Text and slider performance decisions

The text profile found repeated action-menu delegate creation on the GUI thread
during geometry edits. Layer action menus now request current items when opened;
closed menus retain a stable model. With an identical binary, source and text
fixture, median move-dispatch duration fell from 44.7 to 15.7 ms for text alone,
and 60.4 to 13.6 ms with a Paint layer. Sixty input moves fell from 2.8–3.7 s to
approximately 1 s. Text-only corner/side dispatch medians also fell from about
38 ms to 16 ms. Exact undo/redo and held glyph/reflow checks passed. One mixed
side-resize sample lost foreground and is excluded. This improves GUI input
handling, not the remaining styled-text image-render cadence.

The second text candidate (native working fusion) was rejected because the
display-referred text path deliberately prevents that fusion to preserve glyph
colors. The third candidate accelerated output conversions around the existing
display-linear glyph compositor. Its color/alpha/ICC checks passed on eligible
fixtures, but the real mixed-style native fixture includes underline and an
outline and stays on the established working-scene text path. Both frozen
control and candidate presented nine native revisions during approximately
1.03 seconds of movement, with median output work of 47 and 48 ms. The first
native update was 121 ms in both. Their source, typography, window, viewport,
and renderer fixture hashes match; foreground and exact undo/redo checks passed.
The candidate did not exercise its intended accelerated path and provides no
improvement for the requested workflow. It and its experimental API/test were
removed; no styling or fidelity policy was relaxed to force eligibility.
[Rejected-candidate receipt](evidence/text-preview-crop-2026-10-26/text-candidate3-rejected.json).

The three-candidate text loop stops here. **Fully styled text dragging remains a
known realtime gap.** Corner and edge resize show held glyph reflow and grouped
undo, but these results do not establish 60 fps. The next work requires a broader
color-preserving output path for styled working-scene text.

The native typography workflow passes selected-word font/fill/outline isolation,
whole-layer scope switching, multiline copy, Bold/Italic/Underline, keyboard font
search, supported densities, floating/docked controls, and point and paragraph
box resize. Screenshot review caught and corrected a missing width constraint
in the perspective controls. Base Image unlock now creates a persisted normal
stack row; reordering, hiding, undo/reopen/export, and source composition tests
pass. The group-drop affordance matches the controller's restriction on grouping
Base Image.

The third slider scheduling candidate (completion handoff without the timer
wait) was reverted. Across two valid before and two valid after runs, the median
of run-level mean held native frame intervals improved from 47.143 to 44.243 ms
(6.15%), below the 15% target. Guard metrics regressed: median first native
frame latency went from 136.5 to 177.5 ms (30.0% slower), and median held GUI
heartbeat maximum gap went from 33.5 to 70 ms; one candidate run reached 117 ms.
Setter latency remained 1 ms and median RSS fell 3.2%, but those gains do not
offset the presentation regressions. The final code retains the native producer
and source/transaction identity correctness fixes and restores the original
16 ms coalescing policy. The bounded slider loop stops after candidate three;
no 60 fps claim is made.

## Selection brush live feedback

One qualified Selection brush Add gesture used the native visible window on the
mountain fixture. AppKit/CGWindow qualification held across all nine recorded
samples (six during the stroke), with no unqualified samples. The first QML frame
after a brush point arrived at 63 ms, the high-fidelity raster completion was
observed at 62 ms, and release-to-visible commit took 3 ms. The
[qualification receipt](evidence/text-preview-crop-2026-10-26/selection-brush-receipt.json)
records the matching binary and fixture hashes and points to the
original screenshot. This is one visible Selection-brush sample; it does not
establish repeated frame cadence or an Eraser result.

## Large-brush CPU raster result

A direct CPU `PhotoPixelStroke` comparison for a 1024 px solid white Base Image
Eraser coverage stroke on a 6000×4000 surface ran five alternating control and
candidate pairs. The candidate skips later dabs at pixels whose accumulated
coverage is already exactly one. Median raster-plus-finish time fell from
161.18 ms to 86.95 ms (46.1%); median tile commit time changed from 133.43 ms to
134.00 ms (+0.4%). All five pairs produced bit-identical raw float draft tiles
and committed paint payloads across 252 touched tiles. Midstroke changes to
opacity, color alpha, selection, transform, erase mode, and material fall back
to the reference path in the parity probe. The probe does not rasterize the full
24 MP canvas, so this is a CPU worker result rather than a visible-frame result.

A separate pre-candidate native Base Image Eraser run had qualified pre-run and
post-run AppKit/CGWindow samples, three visible-pixel checks, and a 191.46 ms
median visible midpoint response. That diagnostic predates the shared edge correction and this
coverage shortcut. A Paint 1024 attempt is excluded because its foreground
sample showed Finder above the app and failed qualification. The user requested
`SKIPVISIBLE`, so candidate-after visible latency remains unmeasured. The
[large-brush receipt](evidence/text-preview-crop-2026-10-26/large-brush-cpu-2026-10-26/receipt.json)
and its source, hashes, probe logs, and qualified/excluded run records are
included with the evidence.

## Delivery checks

The final changes are checked by 19 focused regressions. The main run passed 18;
an older hard-edge assertion still described the previous inward antialias band.
After pinning its sample coordinates to the centered band, the default paint
suite and new edge suite both pass. The new suite also compares overlapping
paint/eraser strokes with an all-white-selection control, including style changes
that must disable the saturated-pixel shortcut. The full and focused rerun logs
are recorded in the delivery receipt.

The Release `photo_editor_app` build completes successfully. The app binary,
production/test source hashes, aggregate passing results, and limits are in the
[delivery receipt](evidence/text-preview-crop-2026-10-26/final-delivery-receipt.json).
Built app: `build/release-macos/apps/desktop/Looksy.app` (executable SHA-256
`c5ac56e80d364b2f1670ebfb4c963ca392bc98c86180fcac33ebebe467af7f0c`).

The app build also required small compatibility fixes in concurrent Timeline
work: missing clip-level matte/blend declarations, a loader lambda capture, a
QVariant default-value call, and the corresponding test-runtime source list. The
existing Timeline work was retained. These are build integration fixes, not a
claim that the broader Timeline changes have completed workflow validation.

At the user's request, final visible Mac checks were skipped. Earlier native
Crop, typography, slider, and brush evidence is scoped to its recorded sources
and fixtures. The final large-brush result is CPU-only; fully styled text remains
the realtime gap described above.
