# Viewer overlays, scopes, and editing consistency

Approved scope: restore visible image overlays; make scopes live with functional
sampling, high-precision ranges, native density drawing, useful inspection and
comparison controls; unify panel/slider behavior; qualify editing tool workflows.

Preserve existing panels, photographic color-domain ownership, saved recipe
semantics, and unrelated working-tree changes. Do not close the user's editor.

## Verification and performance gates

Baseline: histogram service/coordinator, exposure slider interaction, inspector
style and modular panel tests passed 5/5. Logs are in
`/private/tmp/viewer-tools-baseline-{build,tests}.log`.

Scope computation loop: identical 512x384 byte and half-float synthetic images,
three warmups and twenty samples. At most three candidates. Accept a compute
optimization only with at least 10% median improvement on half-float, no more
than 5% median regression on byte input, and unchanged default numerical output.
Live scope age and displayed refresh targets require a separate foreground
interaction measurement; CPU compute timing cannot establish them.

## Implemented

- Visible viewer Overlays control and Ctrl+Alt+J clipping shortcut; controls stay
  available while using selection tools. Thresholds, peaking sensitivity/color
  and a false-color legend are available without developer mode. Technical
  provenance remains disclosed separately.
- Fixed a native inspection lifecycle failure: partial native publications can
  lack a complete inspection surface, and viewport publications could cancel
  queued overlays after a CPU fallback arrived. Explicit inspection now requests
  a readable display when needed and restarts current overlay work after render
  activity settles. Native proof checks threshold changes against actual warning
  pixels, including resetting to the original pixels.
- Functional Fast/Balanced/Precise sample budgets, bounded live-drag sampling,
  higher Precise density resolution, configuration/generation rejection, range
  modes (0–100%, −25–400%, 0–1600%), nonfinite-sample rejection, and visible-area
  analysis. Sampling quality participates in render cache identity.
  Live scope publication follows the currently displayed generation while newer
  input is pending, remains monotonic, and invalidates on asset changes. Loading
  another photo clears the old scope snapshot rather than relabeling it.
- Native scene-graph density/vector plots shared by Edit and Timeline, with a
  native raster fallback for Qt's software renderer. Added density gain, held
  reference comparison, freeze/resume, waveform probing and reference guides.
  Domain/range/region changes do not silently reuse an incompatible reference.
- Timeline's live capture interval is 67 ms, with worker backpressure; this is a
  maximum request rate, not a claim of achieved playback/scope frame rate.
- Shared inspector/Scene slider behavior: fine dragging, keyboard increments,
  Escape rollback, unit-aware numeric entry, reset and balanced edit
  transactions. Escape after a committed numeric edit still reaches the owning
  effect preview. Vertical handles reserve a stable size.
- Responsive secondary-pane collapse, compact scope selection when a quad would
  crowd enlarged controls, shared Scene insets, and removal of vector-control
  implicit-size feedback loops. Existing inspector groups remain in place.

The paint-preset round-trip fixture now explicitly creates a feathered edge;
its previous zero-feather pixel-aligned rectangle could not guarantee the
partial coverage that its assertion required. Exact saved-alpha equality is
still checked. The retouch harness was refreshed for the current controller
alias and component capability check.

## Evidence

Final rebuilt verification: **22/22 focused tests passed in 195.40 s** in
`/private/tmp/viewer-tools-qualified-tests.log`. This includes native and software
plot pixels, the real viewer workflow, continuous scopes, sampling/format
correctness, slider/panel geometry, selection/mask, professional tools,
vector/SVG controls, retouch, typography, effects and paint/preset workflows.
The logs, five native captures, software captures and scoped source hashes are
archived in `build/viewer-edit-tools-2026-11-14/`.

`/private/tmp/viewer-tools-final-regressions.log`: 11/11 focused scope, coordinator,
real-viewer pointer, input, slider, panel, and Timeline checks passed in 42.82 s.
Other focused runs cover effects (including exact Escape rollback and one-step
Apply undo), typography, paint/preset persistence, selection-to-layer masks,
global slider/mask fidelity, professional tool workflows, vector mask/SVG
controls, and retouch quality/cancel routing.

The continuous-input controller proof records eight distinct scope-frame
publications during 30 edits over 1.031 s, before ending the gesture. It also
checks a photo switch while Precise scope work is pending. This is a synthetic
controller publication measurement, not displayed native frame-rate proof.

Native Metal density pixels/channel toggles/reference comparison and native
retouch pointer checks pass. The real Main.qml foreground Cocoa/Metal viewer
proof also passes, with its existing editor left open. It verifies visible
overlays at 1280/900 widths, clipping output and configurable threshold pixels,
native scopes, Freeze reaching the controller, 150% interface scaling, and
scrollable enlarged scope settings.
Screenshots are in `/private/tmp/viewer-tools-native-proof/`; software-renderer
proof is in `/private/tmp/viewer-tools-proof/`. The standalone scope software
fallback has a registered pixel test too.

## Performance result and remaining qualification

The fixed 512×384 compute loop did not clear the optimization gate: original
medians were 39.10 ms byte / 39.40 ms half-float; the final comparable run was
43.08 / 42.29 ms. No algorithm-level speedup is claimed. Retained scanline work
supports extended float values and explicit nonfinite handling; the feature
reduces interactive work through actual sampling budgets, rather than counting
this numerical work as an accepted optimization.

The budget experiment uses the same 1024×768 image with each policy. It confirms
786,432 samples at the prior budget and Precise, 196,608 at Balanced, and 49,152
at Fast/live. Its illustrative half-float medians were 140.83 / 42.52 / 17.36 /
167.01 / 17.26 ms for prior/Balanced/Fast/Precise/live respectively. This run
overlapped a native UI proof, so those timings are diagnostic rather than a
guarded performance claim. Artifact: `/private/tmp/viewer-scope-sampling-budgets.txt`.

Sustained displayed scope age/20–30 Hz, large RAW/HDR photographic review,
Timeline playback latency, and full release qualification remain open. The
general desktop source-contract run still stops at an existing mask-preview
source-string expectation; it is not reported as a full-suite pass. A separate
Library delete-dialog implicit-height warning also remains outside this Edit
change. No active editor was closed or relaunched.
