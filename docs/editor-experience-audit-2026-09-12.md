# Editor experience: six reported problems and 25 additional improvements

Date: September 12, 2026. Source baseline: `13dcbbf` plus the existing, extensively modified working tree.

The goal is to make editing feel immediate, photographic, precise, and easy to resume. The first priority is the six problems reported by the user: edit delay, shadows/highlights, curves, split toning, returning to masks, and mask refinement. The revised next 25 items focus on concrete tool interactions, history behavior, and configurable export. Existing capabilities are called out so they are not counted as new work.

This is an investigation and implementation specification. **The original investigation did not claim application fixes; subsequent implementation evidence is linked below.** Source findings were checked against the current files; older benchmark results are explicitly attributed to their reports. Native UI inspection was attempted but the Computer Use runtime failed to load `@oai/sky`. Consequently, the user's exact mask-click sequence, current rendered appearance, and current end-to-end latency have not been reproduced in this audit. Existing test names below identify coverage to extend and run, not tests passed during this audit.

This is a supporting specification under the [photographer workflow owner](user-experience-improvement-checklist.md). Implementation status remains in that owner and the relevant [color owner](color-engine-and-app-foundations-completion-strategy.md), [mask/layer owner](mask-layer-selection-checklist.md), [performance owner](performance-measurement-expansion-checklist.md). Numbers here identify these 31 acceptance specifications; they do not create a competing completion board. Previously checked component work must be reopened in its owner when a current reproduction contradicts it.

## Implementation evidence added September 12

Specification 6 has a [refinement toolbar layout correction](evidence/editor-experience-2026-09-12/mask-refinement-layout.md) with native visual review. The focused editing surface and usability gate remain open.

Specification 5 has [stable-component reselection progress](evidence/editor-experience-2026-09-12/mask-reselection.md). Controller/session identity handling and asset isolation are implemented. Native pointer tests cover Base Image and owning-layer click-away, multiple mask kinds, hidden/locked masks, component reorder/undo/redo, and same-session asset return with colliding mask IDs. Unified target ownership, the remaining interaction matrix, cross-process restoration, and presented-frame latency remain open.

Specifications 3 and 12 have partial implementation and automated evidence in the
[editor precision packet](evidence/editor-experience-2026-09-12/README.md), including
native component screenshots, pointer/keyboard checks, controller undo/redo checks,
and source/artifact hashes. Both gates remain open pending the broader Studio,
rendering, and photographic evidence specified below. Original investigation
findings describe the baseline; the linked packet records subsequent changes.

Specification 7 now has [extended-exposure implementation evidence](evidence/editor-experience-2026-09-12/exposure.md): ±5 EV controls and setters, central precision, actual numeric entry, disk recipe round-trip, CPU export-render equality and scene-stage 2^EV verification. Its full Studio and real-photo gate remains open.

Specification 11 now has [section-comparison implementation evidence](evidence/editor-experience-2026-09-12/section-comparison.md): explicit preview-only scopes, stable layer targeting, click/hold restoration, native component checks and controller/CPU render isolation. Full Studio and presented-frame proof remain open.

## Evidence and priority rules

- **Source finding:** behavior or a structural gap visible in the inspected implementation. It can explain a symptom without proving it was the user's runtime cause.
- **Recorded evidence:** measurements or verification in an existing repository report. These are historical observations, not fresh benchmarks.
- **Concrete change:** an identified limitation, a source-level behavior mismatch, or a specified new interaction. Existing features are acknowledged explicitly; merely proving that an existing feature works is not counted among the 25 additions.
- **P0:** core editing is blocked, the wrong target can be changed, or feedback cannot be trusted. **P1:** substantial editing quality or workflow friction. **P2:** subsequent refinement.

All 31 completion gates are unverified in this audit. A gate needs the specified behavior, focused automated evidence, and visible proof where applicable. A source-string contract, passing compilation, or a synthetic golden alone cannot close a photographic or interaction requirement.

## 1. Applying edits takes a second or two

**Priority / owner:** P0; performance with color/render foundations. **Evidence:** source findings plus recorded latency failures.

The current controller deliberately delays full work by `700 ms`, or `1800 ms` for some expensive states. Interactive debounce is already zero. Reducing a slider timer alone therefore cannot explain or fix the entire delay. `persistCurrentState()` calls `checkpointRecipeSaves()` before starting the 350 ms persistence timer; the journal write is synchronous at that call site. Mask-heavy recipe preparation, publication, and persistence all need to be separated in a trace.

The [September 10 UI/tone report](ui-advanced-tone-performance-2026-09-10.md) records 585–966 ms to the first frame after publication in its after runs, 490–525 ms maximum GUI heartbeat gaps, and zero native publications during its coalesced curve burst. However, current `flushEditTransactionInteractivePreview()` already has an in-flight guard intended to prevent repeated cancellation from starving a drag. Verify that guard in the actual slider, wheel, and layer paths before proposing it again. The [draft-render report](render-drafts-library-performance-2026-09-10.md) also records 196/411 ms first draft renders, still above its 100 ms budget.

**Fix approach:** instrument input → setter/recipe snapshot → queue wait → render → texture publication → presented frame, using the same asset/revision/gesture ID. Keep one useful interactive frame in flight and retain the newest pending state; reject out-of-order results. Reuse decoded input, compiled stages, immutable mask payloads, and unaffected tiles. Move expensive save preparation off the GUI thread while preserving ordered durable acknowledgements and crash recovery. Dispatch settled visible-region work promptly on release; let background full-image work finish separately. Preserve the existing 1024-pixel draft floor and accurate final detail.

**Completion criteria:**

- Under the common performance protocol below, input-to-first-visible edited pixels is p95 ≤100 ms, a continuous two-second drag has no rendered-feedback gap >150 ms after its first result, and GUI stalls remain ≤50 ms. Measure the presented image, not merely the moved slider or an overlay.
- On release, the latest accurate visible region appears at p95 ≤500 ms; full-image completion is p95 ≤2 s on the declared reference workload. Separately report heavier cases and cold RAW loading.
- Global exposure, shadows, highlights, curve dragging, grading, local exposure, and mask feather each meet the gate; asset switching and an export in the background do not publish an obsolete result.
- No lowered-resolution final preview, missing mask during a color edit, dropped last input, duplicate history step, or loss of an edit acknowledged as protected. Extend `editor_preview_feedback_test`, `render_scheduler_test`, and `global_slider_mask_fidelity_test`, then capture native interaction traces.

**Code:** [controller](../apps/desktop/src/editor_preview_controller.cpp), especially `flushEditTransactionInteractivePreview`, `performRenderPreview`, `persistCurrentState`, and `checkpointRecipeSaves`; [scheduler](../apps/desktop/src/render_scheduler.h); [render policy](../apps/desktop/src/edit_render_policy.h).

## 2. Shadows and highlights do not behave as expected

**Priority / owner:** P1; color. **Evidence:** source finding; appearance requires real-photo confirmation.

The non-legacy tone function is a small, bounded log-luminance adjustment. It applies `0.54 × response(control) × bell_weight`, with shadows centered at −1.85 stops from 18% gray and highlights at +1.95 stops. The bell has finite support. Calculating directly from those constants, shadows affect approximately linear luminance `0.01385–0.18`; highlights affect `0.19292–2.50719`. Outside those bands their contribution is zero. The maximum change at either band's center is only ±0.54 stop, or roughly ×1.454 / ×0.688. These are isolated stage calculations, not whole-image measurements.

This can leave very deep recoverable shadows and bright scene values unchanged, even at the slider limit. The older anchor model is a different path, so the photo's saved rendering model also matters. The old [color checklist](color-tools-remediation-checklist.md) describes an anchor retune as complete; that description does not establish the behavior of today's non-legacy path.

**Fix approach:** define the intended photographic response first: Shadows opens/closes dark detail while protecting the black endpoint and middle gray; Highlights compresses/expands bright detail while preserving ordering, hue, and headroom. Calibrate broader, smoothly tapered luminance response against real backlit, night, and bright-sky photos. Keep Blacks/Whites as distinct endpoint controls. Test combinations after exposure and through the selected output transform. Separate rendering existing highlight information from RAW reconstruction; lowering a flat clipped patch cannot recreate lost texture. Version any changed rendering semantics so reopening an existing photo does not silently change its look.

**Completion criteria:**

- A measured ramp from −10 to +8 stops remains finite, ordered, continuous, and neutral at zero for each control and their combinations; signed channel/headroom handling remains covered.
- An approved input/output response table covers slider positions −100, −50, −25, 0, +25, +50, +100 and specifies the intended affected regions before tuning. Increasing a slider cannot reverse its intended luminance direction at a fixed input sample.
- On the real-photo corpus, +Shadows reveals existing dark detail without flattening unaffected midtones; −Highlights separates existing bright texture without gray skies, hue breaks, or edge halos. Two experienced photographers independently accept every designated stress image, with disagreements resolved and recorded.
- Run global and masked-local variants through CPU, Metal, draft, settled preview, export, undo, and reopen. Extend [tone invariants](../tests/color_tool_invariant_test.cpp) and [render parity](../tests/render_parity_test.cpp); apply the common numerical parity gates below.

**Code:** [tone runtime](../engine/src/pipeline/color_tone_runtime.cpp), `apply_monotone_log_luminance_tone` and legacy dispatch; [weight math](../engine/src/pipeline/color_math_runtime.cpp); [recipe adapter](../apps/desktop/src/edit_recipe_v13_adapter.cpp).

## 3. Curves are too sensitive

**Priority / owner:** P1; workflow and color. **Evidence:** source finding with a recent partial fix already present.

The [September 10 curve correction](tone-curve-correction-2026-09-10.md) fixed selection jumps, preserved the initial grab offset, inserted new handles on the curve, and preserved extended highlights. Those changes are present. The remaining drag still uses direct pointer displacement divided by graph dimensions, with no fine-adjust modifier. Compact mode has a smaller control and hides the numeric In/Out row. For illustration, a five-pixel vertical move in a 100-pixel plotting area changes normalized output by 0.05—about 13 displayed 8-bit levels. That geometric example is not a measured current window size. Preview delay can amplify overshooting.

**Fix approach:** retain the grab-offset correction; add a discoverable fine mode with 0.1× displacement, precise keyboard nudging, axis locking, and an expanded graph accessible from compact mode. Keep numeric entry available through a focused popover or expansion. Preserve exact neutral behavior and evaluate interpolation against the renderer rather than tuning only the drawn line. Fine mode must remain anchored when the modifier is pressed or released mid-drag.

**Completion criteria:**

- Selecting any handle causes zero recipe change. Inserting a point on the displayed curve stays within 1/255 of the previous SDR curve response; endpoints and extended highlights retain their established contracts.
- A fixed ten-pixel move in fine mode produces one tenth of normal coordinate movement within rounding tolerance, with no jump when modifiers change. Arrow nudge is 1/255; fine nudge is 0.1/255 in normalized coordinates.
- Normal and compact layouts support accurate numeric placement, point deletion, reset, and keyboard focus. One completed drag is one undo operation; cancel restores its starting state.
- A user can raise a selected output value by three displayed levels without overshooting on five repeated attempts, with live feedback meeting item 1. Extend [native curve interaction tests](../tests/tone_curve_interaction_test.cpp) and [headroom tests](../tests/color_curve_headroom_test.cpp), and record the actual panel at both sizes.

**Code:** [ToneCurveEditor](../apps/desktop/qml/components/ToneCurveEditor.qml), `normalizeForGraph`, `updatePoint`, and the interaction `MouseArea`; [curve evaluator](../engine/src/pipeline/color_curve_evaluator.cpp).

## 4. Split toning needs a coherent color and interaction model

**Priority / owner:** P1; color and workflow. **Evidence:** multiple source findings.

The hue-scale bug described in the old checklist is already fixed: the engine wraps hue as `0..1`. A different mismatch remains: `ColorWheelControl` paints with `Qt.hsla`, while the grading runtime uses the same normalized angle directly as the direction of an OKLab a/b offset. HSL hue and perceptual a/b hue are different coordinates, so the displayed swatch is not a reliable prediction of the grade. This inference follows from the inspected code and the definitions in [Qt's HSL API](https://doc.qt.io/qt-6/qml-qtqml-qt.html#hsla-method) and the [Oklab author's color-space description](https://bottosson.github.io/posts/oklab/).

Additional issues: the right-button fine adjustment linearly interpolates `nextHue - hue`, which takes the long route across the 0/1 seam; wheel and inner-rail edits emit value changes without a component-level gesture start/finish contract; and the compiled/scalar grade uses `abs(saturation)`, so negative saved values do not mean desaturation. Current ring UI saturation is nonnegative, making the last issue primarily a recipe/import/agent semantic concern. Tonal selection uses fixed lightness bands with Balance but no explicit blend-width parameter.

**Fix approach:** choose one documented hue contract and use it in wheel drawing, swatches, grade evaluation, and every backend. Prefer generating the swatch from the actual grade transform on a neutral reference patch; if changing stored hue semantics, version/migrate explicitly. Use shortest-angle interpolation across the seam. Give each wheel/rail/macro gesture one transaction and a numeric fine-entry route. Keep hue selection meaningful at zero saturation without silently adding color. Define Balance direction visibly; add a simple Blending control only with real, versioned overlap behavior. Calibrate low-strength response for subtle photographic grades.

**Completion criteria:**

- Twelve evenly spaced hue selections on neutral patches produce swatches matching the actual transformed preview within ΔE00 ≤2 under the same SDR reference profile; all twelve also pass subjective hue identification. Do not use that tolerance to judge unrelated output profiles or scene patches.
- Fine dragging from 359° to 1° travels through 0°, with no excursion around the wheel. Mouse, numeric, keyboard, and imported recipes resolve to the same hue/saturation semantics.
- Zero amount and zero saturation/luminance are exact neutral operations; Balance moves influence in its labeled direction; any new Blending control changes overlap smoothly without a neutral brightness bump.
- Low-strength cool shadows/warm highlights pass skin, neutral clothing, deep-black, and bright-sky reviews without unwanted cast or abrupt tonal boundaries. Global/local and CPU/Metal/export match the common parity gates.
- A wheel drag creates one undo step and meets item 1. Presets and existing saved recipes retain their intended appearance or require an explicit, reversible rendering upgrade.

**Code:** [wheel](../apps/desktop/qml/components/ColorWheelControl.qml), especially `updateWheel`; [scalar grading](../engine/src/pipeline/color_look_runtime.cpp); [compiled grading](../engine/src/pipeline/color_look_grading_program.cpp); [Studio](../apps/desktop/qml/workspaces/StudioWorkspace.qml), `setSelectedToneWheelHue` and `splitToningFocusedWheel`.

## 5. A mask cannot reliably be selected again after clicking away

**Priority / owner:** P0; masks/layers. **Evidence:** user-reported failure; source-level risks located, exact runtime cause unconfirmed.

Mask child thumbnails already have click handlers. The problem is not simply an absent `onClicked`. `selectMaskSetAndLayer()` chooses the first source's flat index, writes the active mask, optionally selects an owning layer, then writes a separate selected-edit-target object and changes tools. Layer selection overwrites target scope; tool changes can call `deselectActiveMask()`. Several related states coexist: active mask index, selected mask-set index, active layer index, selected target, inspector tab, and overlay visibility. A combined mask also reopens at its first source rather than necessarily the last edited component.

The layer's fixed action zone deliberately hides its mask-state pill and has no mask-select signal. Mask editing depends on the child rows being visible and receiving the click. The active [mask checklist](mask-layer-selection-checklist.md) marks this capability complete, but its documented click-routing proof does not settle the reported click-away/reselect sequence.

**Fix approach:** first reproduce create → edit → click image/layer/other tool → click the original mask thumbnail. Trace hit testing and all target-state writes. Centralize selection in one controller operation using stable layer, mask-set, and component IDs; derive UI indexes from that state. Make every displayed mask thumbnail a reliable selection target, automatically reveal the selected child, preserve the last edited component, and separate selecting a mask from toggling its effect or overlay. Avoid a later tool-change cleanup clearing the newly selected mask.

**Completion criteria:**

- Single-click reselect works in 20 consecutive cycles for brush, gradient, generated, and compound masks after clicking the canvas, another layer, Base Image, Curves, and another asset then returning.
- Repeat with collapsed groups, scrolled rows, renamed/reordered layers, undo/redo, hidden masks, and a saved/reopened recipe. A locked target may be selected and inspected but explains why editing is unavailable.
- Correct mask/component highlight, inspector values, canvas handles, overlay, and brush destination agree within 100 ms on a warm mask. Selection alone changes neither pixels nor history.
- A real native pointer test clicks visible coordinates and then modifies the selected mask, proving the correct stable ID changed. Extend [layer adjustment coverage](../tests/editor_preview_layer_adjustment_test.cpp) and native shell interaction coverage; a direct call to the selection function is insufficient.

**Code:** [Studio](../apps/desktop/qml/workspaces/StudioWorkspace.qml), `selectMaskSetAndLayer`, `setSelectedEditTarget`, `setActivePhotoLayerSelection`, and `clearTransientToolStateFor`; [LayerRowActionZone](../apps/desktop/qml/components/LayerRowActionZone.qml); [mask session](../apps/desktop/src/mask_session.h).

## 6. Masks are hard to edit and refine

**Priority / owner:** P1, immediately after item 5; masks/layers. **Evidence:** user feedback plus existing, dispersed capabilities.

The implementation already includes add/subtract/intersect components, brush refinement, feather, density, smooth, fine edge, expansion, generated-mask cleanup, and several preview modes. Rebuilding those capabilities would miss the usability problem. Their controls are spread across Studio's selected-mask area, the contextual brush surfaces, and `MaskInspectorSections`. The code explicitly distinguishes mask density, mask opacity, brush flow, brush opacity, layer opacity, and overlay opacity—six concepts whose purpose must be apparent in context.

**Fix approach:** make selecting an existing mask open a focused **Edit mask** surface. Keep Add, Subtract, Refine edge, and View directly available, with one or two relevant settings at a time. Use page replacement for deeper refinement, keeping the photo and selected target visible. Show components in an ordered, understandable list with independent enable/disable and rename. Offer on-image gradient/shape handles and a localized refine brush. Make effect strength separate from overlay visibility. Cancel must restore the pre-refinement matte and recipe; Done commits one operation. Unsupported operations need a useful reason at the control.

**Completion criteria:**

- Three users unfamiliar with the implementation independently select an existing mask, add a missed region, subtract spill, refine an edge, adjust softness, and return to color editing within two minutes, without developer help or accidental edits to another target.
- No more than two actions from a selected mask reach any of the four primary tasks. Opening refinement never creates an extra selection or adjustment layer unless explicitly requested.
- Refine a portrait hair edge, a hard building edge, and a low-contrast object at 100% and 200%. Review before/after alpha and the composited photo on light and dark backgrounds; no new fringe, lost intended holes, or global edge movement from a local stroke.
- Cancel, undo/redo, duplicate, reopen, and export preserve component order and the intended matte. Refinement feedback meets item 1; accuracy uses the shared mask-geometry tolerance below.

**Code:** [mask inspector](../apps/desktop/qml/workspaces/MaskInspectorSections.qml), [mask canvas](../apps/desktop/qml/workspaces/MaskCanvasOverlay.qml), [shared overlay](../apps/desktop/qml/workspaces/SharedMaskOverlay.qml), [brush context](../apps/desktop/qml/components/BrushContextPanel.qml), and the controller's active-mask refinement operations.

## The 25 additional improvements: tools, history, and export

Revised after the user's feedback that the first list mostly described capabilities already present. The replacement contains **8 tool/interaction changes, 8 history changes, and 9 export changes**. Each item states what already exists and the concrete difference being proposed. Library, import, agent readiness, general cache work, and broad capability-verification items have been removed from this list.

“New interaction” means the inspected user-facing path lacks that interaction; it does not mean there are no reusable backend helpers. Runtime appearance still needs native verification. Existing group comparison, detail loupe, numeric sliders, crop pixel dimensions, named snapshots, export preflight, retry, and queue controls are explicitly retained rather than counted as additions.

### Tools and editing experience — 8 changes

### 7. Expand exposure beyond the current ±2 EV limit

**Priority / owner:** P1; tools/color. **Type:** verified control limitation.

**Already exists / actual gap:** Exposure is implemented, but both Studio's slider and `EditorPreviewController::setExposure()` clamp to −2…+2. A user cannot make a larger correction through that control, even by typing a value.

**Change:** support at least ±5 EV, with a precise central range, numeric entry, and fine adjustment. Trace recipe normalization and local-layer limits as well as the global setter so the UI cannot advertise values that persistence silently clips.

**Completion criteria:** type and drag to −5, −3, +3, and +5; the same value survives save/reopen, undo/redo, and export. A +3 setting scales scene-linear exposure by 8 before subsequent tone/output processing. Near-zero adjustments retain 0.01 EV precision and do not become harder to set because the range widened.

**Code:** [Studio exposureSlider](../apps/desktop/qml/workspaces/StudioWorkspace.qml), [setExposure](../apps/desktop/src/editor_preview_controller.cpp), [recipe adapter](../apps/desktop/src/edit_recipe_v13_adapter.cpp).

### 8. Give white balance honest temperature semantics

**Priority / owner:** P1; tools/color. **Type:** verified display mapping needing a clearer contract.

**Already exists / actual gap:** The white-balance sampler and temperature/tint controls already exist. Studio's `whiteBalanceKelvin()` maps a signed −100…+100 adjustment to 2000…12000 K, with zero always displayed as 5500 K. That display is not derived from the photo's as-shot illuminant, so it does not itself establish a measured or camera-relative Kelvin value.

**Change:** for RAW, expose as-shot and actual temperature/tint through a defined camera/profile-aware conversion. For rendered files or relative layer corrections, label the value as a relative warming/cooling adjustment unless a meaningful Kelvin contract is available. Preserve existing looks when changing representations.

**Completion criteria:** two RAWs with different as-shot white balances do not misleadingly report identical 5500 K neutrals; As Shot restores each source's intended baseline. Numeric entry, sampler, slider, reset, and reopen agree. Relative JPEG/layer adjustments are clearly labeled. Validate on neutral references under at least three illuminants without claiming that an arbitrary colored surface is neutral.

**Code:** [Studio whiteBalanceKelvin](../apps/desktop/qml/workspaces/StudioWorkspace.qml), [InspectorSlider Kelvin conversion](../apps/desktop/qml/components/InspectorSlider.qml), [input runtime](../engine/src/pipeline/color_input_runtime.cpp).

### 9. Adjust a photographed color by dragging on it

**Priority / owner:** P1; tools. **Type:** new interaction over the existing color mixer.

**Already exists / actual gap:** Color swatches and H/S/L adjustments exist. The inspected Studio/controller surface does not expose an image-drag tool that resolves the clicked color and changes the relevant mixer bands together. Color-range masking and color replacement samplers serve different purposes.

**Change:** add a targeted Hue/Saturation/Luminance tool: click a blue sky or green leaf, show the affected color bands, and drag to adjust them. Freeze the sampled source color for the gesture so changing the output cannot make the tool chase a different hue.

**Completion criteria:** a color between two mixer bands changes those bands with smooth weights; neutral pixels report that there is no reliable hue target. Vertical drag, numeric adjustment, and undo yield the same result. Global and selected-layer routing agree, and cancel leaves the recipe unchanged. Five real-photo tasks require no guessing which swatch owns the sampled color.

**Code:** [Studio Color Mix controls](../apps/desktop/qml/workspaces/StudioWorkspace.qml), [controller sampling APIs](../apps/desktop/src/editor_preview_controller.h).

### 10. Place a curve point from a tone in the image

**Priority / owner:** P1; tools. **Type:** new interaction, separate from item 3's drag sensitivity fix.

**Already exists / actual gap:** Point curves, channels, numeric entry, and the graph histogram already exist. The inspected curve control has no image sampler that marks where a photographed tone lies on the active curve.

**Change:** hover over the photo to locate its input tone on the graph; click to select or insert a point at that tone; optionally drag vertically on the photo to adjust it. Sample in the active curve's input domain, accounting for preceding stages and selected channel.

**Completion criteria:** known gray and color patches locate the expected channel coordinate; clicking without dragging does not visibly change the image. Existing nearby points are selected rather than duplicated. The tool works for RGB, Luma, and individual channels with a visible mode label, supports fine adjustment, and creates one undo step per drag.

**Code:** [ToneCurveEditor](../apps/desktop/qml/components/ToneCurveEditor.qml), [curve evaluator](../engine/src/pipeline/color_curve_evaluator.cpp), [Studio viewport/tool routing](../apps/desktop/qml/workspaces/StudioWorkspace.qml).

### 11. Make “this section only” before/after actually section-specific

**Priority / owner:** P1; tools/experience. **Type:** source-level scope mismatch to reproduce.

**Already exists / actual gap:** Section before/after already exists. `StudioInspectorGroup` promises “this section only,” but White Balance and Light both pass `compareGroup: "tone"`; other separate panels share `color` or `detail`. The UI cannot distinguish panels that submit the same group key.

**Change:** add explicit comparison scopes for White Balance, Basic Tone, Curves, Grading, Color Mix, and other visible sections. Temporarily bypass only that panel on the selected target. Keep whole-image and whole-layer comparison as separately labeled actions.

**Completion criteria:** with white balance, exposure, curves, and grading all active, comparing White Balance leaves the other three effects visible. Comparing Curves preserves exposure. Repeat on an adjustment layer. Press/release and toggle modes restore exactly, create no history entry, and never write the preview bypass into the saved recipe.

**Code:** [StudioInspectorGroup](../apps/desktop/qml/components/StudioInspectorGroup.qml), [Studio compareGroup bindings and toggleCompareEditGroup](../apps/desktop/qml/workspaces/StudioWorkspace.qml).

### 12. Stop two-axis pads from jumping or changing the wrong axis

**Priority / owner:** P1; tools/experience. **Type:** verified interaction behavior.

**Already exists / actual gap:** The two-axis pad already coalesces edits. However, `StudioAdjustmentPad2D` maps the absolute press position straight to both values, then updates both on movement. It has no grab-offset preservation or axis lock; `onCanceled` flushes pending values and finishes rather than restoring the starting state.

**Change:** preserve the handle's grab offset, add a visible axis-lock/fine-adjust affordance, and provide actual cancel rollback. Apply the shared interaction contract to temperature/tint and other pads. Keep existing numeric sliders available.

**Completion criteria:** grabbing within the handle hit area changes neither value; a horizontal locked drag changes only the horizontal parameter; fine mode produces 0.1× normal movement without jumping when activated mid-drag. Escape/cancel restores both starting values, and a completed two-axis drag remains one undo step.

**Code:** [StudioAdjustmentPad2D](../apps/desktop/qml/components/StudioAdjustmentPad2D.qml), [Studio pad bindings](../apps/desktop/qml/workspaces/StudioWorkspace.qml).

### 13. Turn Liquify into a direct on-image editing tool

**Priority / owner:** P2; tools/color. **Type:** substantive new tool behavior over an existing effect.

**Already exists / actual gap:** Liquify exists as Center X, Center Y, Radius, and Strength controls. The inspected controller exposes those four parameters, which describe one localized effect; it does not expose a replayable push-brush stroke sequence or a freeze mask.

**Change:** provide an on-image brush with Push and Restore, adjustable size/strength, and protected regions. Store replayable deformation strokes or a versioned displacement representation. Retain the current radial effect as a compatible older operation.

**Completion criteria:** make three independent local pushes, protect a neighboring edge, restore only part of one change, and undo each stroke. Save/reopen and export preserve the deformation at full resolution; crop/rotation do not move it to another source region. Native feedback follows the brush, and image edges do not expose undefined pixels.

**Code:** [Studio cropLiquifyGroup](../apps/desktop/qml/workspaces/StudioWorkspace.qml), [controller Liquify properties](../apps/desktop/src/editor_preview_controller.h), [recipe state](../apps/desktop/src/edit_recipe_state.h).

### 14. Let the user build a small personal adjustment panel

**Priority / owner:** P2; editing experience. **Type:** new organization behavior.

**Already exists / actual gap:** The inspector can filter adjustment groups and expand sections. The inspected surface does not offer a persistent, user-ordered collection of individual controls, such as Exposure, Shadows, Temperature, and one curve, across those groups.

**Change:** allow pinning existing controls to a compact personal panel, rearranging them, and opening their full section when needed. Keep it to a short user-chosen set; reuse the same controls and target bindings rather than creating a second set of values.

**Completion criteria:** pin four controls from different sections, reorder them, restart, and retain that order. Edits and resets are immediately reflected in both locations; switching layers shows the correct target's values. Unpinning changes no recipe data. The default inspector remains useful without configuration.

**Code:** [Studio adjustment filter and inspector groups](../apps/desktop/qml/workspaces/StudioWorkspace.qml), [InspectorSlider](../apps/desktop/qml/components/InspectorSlider.qml).

### History — 8 changes

### 15. Keep the full edit history when returning to a photo

**Priority / owner:** P1; history. **Type:** verified retention limitation.

**Already exists / actual gap:** Undo/redo and named persistent snapshots exist. The UI explicitly says undo steps last for the session. In addition, `HistoryService::touchAsset()` clears an evicted asset's undo/redo stacks, so session lifetime is not the only limit. Named snapshots are not a substitute for the chronological sequence.

**Change:** persist bounded per-photo edit revisions and restore the timeline lazily. Separate history retention from decoded-image/asset-cache eviction. Show any retention boundary explicitly and keep named versions regardless of that boundary. Reuse existing recipe snapshot/provenance structures where suitable.

**Completion criteria:** make 30 edits, visit enough photos to exceed `kMaxTrackedAssets`, return, restart, and undo/redo the same 30 steps with matching labels and recipes. Define the revision/space limit before implementation; pruning must not silently erase named versions or the current state. A revision is labeled saved only after its durable write succeeds, including snapshot-save failures currently ignored by the controller wrapper.

**Code:** [HistoryService](../apps/desktop/src/history_service.h), [controller history/snapshot persistence](../apps/desktop/src/editor_preview_controller.cpp), [snapshot store](../apps/desktop/src/edit_history_snapshot_store.cpp).

### 16. Preview a history step before restoring it

**Priority / owner:** P1; history/experience. **Type:** verified interaction limitation.

**Already exists / actual gap:** History rows are clickable, but a tap immediately calls `restoreEditHistoryIndex()`. That method walks undo/redo operations until it reaches the target. Exploring an old step therefore changes the current editing position rather than merely previewing it.

**Change:** provide temporary hover/keyboard preview and a clear Restore action. Render the chosen revision directly rather than showing every intermediate undo result. Keep preview separate from durable recipe, export source, and current branch position.

**Completion criteria:** preview five earlier states and exit: current recipe, undo position, redo tail, autosave, and queued export inputs remain unchanged. Restore one state deliberately and obtain the expected edit position. Rapid previews cannot publish late frames; cached preview switches meet p95 ≤100 ms.

**Code:** [Studio editHistoryEntryRow](../apps/desktop/qml/workspaces/StudioWorkspace.qml), [restoreEditHistoryIndex](../apps/desktop/src/editor_preview_controller.cpp).

### 17. Preserve alternate editing directions after undo

**Priority / owner:** P1; history. **Type:** missing user-facing branch workflow over existing provenance fields.

**Already exists / actual gap:** Recipe snapshots already support branch and parent IDs. However, normal new edits clear the redo stack, and the inspected History panel offers a linear list and named snapshots rather than recoverable alternate editing directions.

**Change:** when editing from an earlier step, retain the abandoned tail as an alternate version and expose a simple “Other versions” choice. Reuse the branch metadata; do not replace it with another incompatible history model.

**Completion criteria:** create A→B→C, return to A, then make D. Both A→B→C and A→D remain selectable after relaunch, with correct pixels and labels. Switching versions changes no other photo. Old branches retain source/profile dependencies and respect explicit retention rules without disappearing as ordinary redo cleanup.

**Code:** [recipe history branch metadata](../apps/desktop/src/edit_recipe_history.cpp), [HistoryService](../apps/desktop/src/history_service.h), [controller redo clearing](../apps/desktop/src/editor_preview_controller.cpp).

### 18. Find edits by tool, layer, mask, or time

**Priority / owner:** P2; history/experience. **Type:** new navigation in an existing history panel.

**Already exists / actual gap:** History already displays user labels, detail, scope, current state, batch records, and agent records. The visible panel uses repeaters and does not expose search or tool/target filters.

**Change:** add search and small filters for the selected layer/mask, tool family, and session/date. Virtualize long histories and preserve chronological context around a match. Filter history visibility without altering which state is current.

**Completion criteria:** in 500 revisions, find “Sky mask feather” and all Curves edits without scrolling the whole list. Filtering and clearing filters retain the current state and stable selected revision. Results appear within 100 ms on the reference machine; keyboard navigation can preview or restore the intended match through item 16.

**Code:** [Studio editHistoryPanel](../apps/desktop/qml/workspaces/StudioWorkspace.qml), [controller editHistoryEntries](../apps/desktop/src/editor_preview_controller.cpp).

### 19. Show the complete change behind a history entry

**Priority / owner:** P1; history/experience. **Type:** richer explanation beyond existing summary text.

**Already exists / actual gap:** Each history row already shows a label and a short detail string capped at two lines. It has no expanded structured before→after list for every changed parameter, especially multi-control, reset, preset, or masked edits.

**Change:** expand a selected entry into affected target plus grouped old/new values, with a thumbnail or temporary comparison for geometry/matte changes. Identify added/deleted mask components by name and stable identity; avoid dumping recipe JSON.

**Completion criteria:** a preset changing ten values lists all ten and no unaffected fields; a group reset shows exactly what reset; a mask stroke identifies its mask and changed region. The information is derived from stored revisions, remains correct after rename/reorder, and works after reopening the photo.

**Code:** [Studio editHistoryEntryRow](../apps/desktop/qml/workspaces/StudioWorkspace.qml), [controller history projection](../apps/desktop/src/editor_preview_controller.cpp), [recipe state](../apps/desktop/src/edit_recipe_state.h).

### 20. Rename and organize existing snapshots

**Priority / owner:** P2; history/experience. **Type:** missing management actions, not a request to add snapshots.

**Already exists / actual gap:** Named snapshots already support creation, restoration, and deletion. Their controller API and rows do not offer rename, notes, or user ordering; the stored snapshot record has id, name, creation time, and state.

**Change:** add inline rename, optional short notes, and a stable pin/order preference. Show creation time and a preview without requiring restoration. Keep snapshot identity stable when its display name changes.

**Completion criteria:** rename “Snapshot 1” to “Client approved,” add a note, pin it, and relaunch. The same snapshot ID and recipe remain; comparison references still resolve. Empty/duplicate-name handling is explicit. Editing snapshot metadata does not add a photographic undo step or change the current image.

**Code:** [snapshot model](../apps/desktop/src/edit_history_snapshot_store.h), [snapshot API](../apps/desktop/src/editor_preview_controller.h), [Studio editHistorySnapshotRow](../apps/desktop/qml/workspaces/StudioWorkspace.qml).

### 21. Recover an accidentally deleted snapshot

**Priority / owner:** P1; history/experience. **Type:** verified destructive UI behavior without a restore path.

**Already exists / actual gap:** The snapshot row's delete action directly calls `deleteEditHistorySnapshot()`, which removes the snapshot, updates comparison selection, and saves the remaining list. It does not put that deleted restore point on the photographic undo stack or expose a recently-deleted collection.

**Change:** make deletion recoverable through a small Undo notification and a bounded Recently Deleted list. Keep recovery separate from normal image undo so recovering a named version cannot unexpectedly undo a brush stroke.

**Completion criteria:** delete the active comparison snapshot, undo the deletion, and recover the same ID, name, recipe, and comparison choice. Recovery also works after relaunch during the declared retention period. Only an explicit permanent-delete action or visible retention expiry removes recoverability; deletion never alters the current photo recipe.

**Code:** [deleteEditHistorySnapshot](../apps/desktop/src/editor_preview_controller.cpp), [snapshot store](../apps/desktop/src/edit_history_snapshot_store.cpp), [Studio snapshot delete action](../apps/desktop/qml/workspaces/StudioWorkspace.qml).

### 22. Show which edit version was actually delivered

**Priority / owner:** P1; history/export. **Type:** a link between two existing systems.

**Already exists / actual gap:** Export jobs already capture recipe/settings and have saved queue history. Photo History shows edits and snapshots, but its inspected rows do not expose a delivery milestone linked to the exact exported revision and output file.

**Change:** add non-editing “Exported Web Proof” milestones with output path, time, delivery settings, and exact revision. Allow Compare with current, Reveal, and Export this version again. Distinguish queued, failed, and successfully completed delivery.

**Completion criteria:** export version A, keep editing to B, then use the history milestone to compare/re-export A without restoring over B. Recipe/settings and output digest match the recorded delivery, subject to codec determinism. Failed jobs never appear as successful milestones; missing files are labeled; milestone navigation does not add undo steps.

**Code:** [export controller](../apps/desktop/src/export_job_controller.cpp), [Studio History panel](../apps/desktop/qml/workspaces/StudioWorkspace.qml), [job store](../apps/desktop/src/job_store.cpp).

### Export — 9 changes

The active route is the dedicated [ExportWorkspaceHost](../apps/desktop/qml/workspaces/ExportWorkspaceHost.qml) and [ExportView](../apps/desktop/qml/workspaces/ExportView.qml). It already provides preset selection, color-space override, export crop, destination, preflight, and queue actions. The proposed work below changes what the user can configure and inspect; it does not ask to rebuild those existing features. The older static export cards inside Studio are not used as evidence of active-page behavior.

### 23. Make format, quality, and bit depth independently editable

**Priority / owner:** P1; export. **Type:** verified fixed-preset limitation.

**Already exists / actual gap:** ExportView offers a fixed list of named presets and a read-only summary. Encoding settings are resolved from preset names by `export_image_settings_for_preset()`. A user cannot directly choose, for example, a full-size JPEG at quality 92 through the displayed settings controls.

**Change:** expose format, supported bit depth, and codec quality as real settings; let presets populate them. Persist a typed delivery-settings object in each job rather than inferring behavior from its display name. Keep advanced HDR settings separate from everyday JPEG/TIFF delivery.

**Completion criteria:** export full-size JPEG at 60/85/95 and TIFF at supported 8/16-bit depths; inspect the actual files and match the displayed settings. Renaming a preset never changes encoding. Invalid format/depth combinations cannot be submitted. Existing saved preset-based jobs retain their previous interpretation.

**Code:** [ExportView preset section](../apps/desktop/qml/workspaces/ExportView.qml), [ExportImageSettings](../apps/desktop/src/export_image_settings.h), [export controller API](../apps/desktop/src/export_job_controller.h).

### 24. Add exact resize controls and print dimensions

**Priority / owner:** P1; export. **Type:** verified sizing limitation.

**Already exists / actual gap:** Cropping exists. Resize is tied to named presets: `preset_max_edge()` selects 1440, 2048, 2560, or full size. The active form has no independent width, height, long-edge, or physical-size/resolution fields.

**Change:** support long edge, fit within width×height, and physical dimensions plus PPI; provide a no-upscale option. Keep crop/aspect choice distinct from resampling, and preview each selected photo's resulting dimensions.

**Completion criteria:** export portrait and landscape photos to a 3000×2000 bounding box, a 2048-pixel long edge, and an 8×10-inch/300-PPI delivery with an explicit aspect decision. Pixels and resolution metadata match the selected mode; no-upscale preserves smaller sources. Editing these output settings does not change the photo's crop recipe.

**Code:** [preset_max_edge and export dimension planning](../apps/desktop/src/export_job_controller.cpp), [ExportView export crop section](../apps/desktop/qml/workspaces/ExportView.qml).

### 25. Add a filename builder with a real batch preview

**Priority / owner:** P1; export/experience. **Type:** verified naming limitation.

**Already exists / actual gap:** `plan_export_output()` constructs a sanitized source/display name plus preset suffix and numbers collisions. Safe collision handling already exists; user-chosen naming tokens and numbering are not exposed by that path.

**Change:** allow original name, custom text, capture date, sequence number, and rendition suffix, with a live preview for the selected batch. Expose Keep both and Skip existing first; overwriting needs an explicit, separate choice if supported.

**Completion criteria:** a 20-photo batch previews exactly the names ultimately written, including same-name sources, Unicode names, missing dates, and existing outputs. Numbering start/padding is respected, concurrent queued jobs reserve names safely, and retry does not accidentally create an extra numbered delivery. Preset rename alone does not rename jobs already queued.

**Code:** [plan_export_output and safe_export_token](../apps/desktop/src/export_job_controller.cpp), [ExportView delivery review](../apps/desktop/qml/workspaces/ExportView.qml).

### 26. Let the user choose exported metadata

**Priority / owner:** P1; export. **Type:** verified fixed metadata policy.

**Already exists / actual gap:** Preflight explains the policy, but rendered-photo delivery says camera metadata is not copied; Original Copy preserves the entire original file. That is not an editable privacy/credit policy for an edited export.

**Change:** offer practical metadata choices: copyright/contact only, camera/capture information without location, and all supported metadata. Expose location separately and show a preview of retained fields. Preserve essential color interpretation metadata regardless of a privacy choice.

**Completion criteria:** inspect exported EXIF/XMP/IPTC where supported: each choice keeps/removes exactly its documented fields. GPS is absent when disabled, copyright survives the appropriate choices, normalized orientation remains correct, and embedded color information is retained. Unsupported fields/formats are explained before export, and Original Copy remains clearly a byte-for-byte operation.

**Code:** [export metadataPolicy](../apps/desktop/src/export_job_controller.cpp), [image encoder](../apps/desktop/src/export_image_encoder.h), [ExportView delivery review](../apps/desktop/qml/workspaces/ExportView.qml).

### 27. Add output sharpening after resizing

**Priority / owner:** P1; export/tools. **Type:** explicit missing delivery operation.

**Already exists / actual gap:** Editing sharpening exists. Export planning explicitly reports “No additional output sharpening.” This means there is no separate delivery-stage sharpening choice after downsizing.

**Change:** provide Off plus restrained Screen/Print output sharpening, with a small strength control and a 100% output-size comparison. Apply it after resize and before final encoding, without changing editing sharpening or the saved image recipe.

**Completion criteria:** downsize a fine-detail image and a portrait; Off matches the prior export, enabled settings affect only the exported rendition, and the preview shows the same output pixels. Required review rejects halos, ringing, and amplified skin noise. Full-size and resized exports use an explicit radius/scale policy rather than applying the same source-pixel radius blindly.

**Code:** [sharpeningPolicy](../apps/desktop/src/export_job_controller.cpp), [photo export task](../apps/desktop/src/photo_export_task.h), [image settings](../apps/desktop/src/export_image_settings.h).

### 28. Preview actual compression and target file size

**Priority / owner:** P1; export/experience. **Type:** new encoded-output preview.

**Already exists / actual gap:** ExportView shows a current image and preset/quality summaries. It has no visible round-trip encode/decode preview or target-byte control; a source preview cannot reveal JPEG artifacts at the chosen output dimensions.

**Change:** show an encoded result at fit and 100%, with measured bytes for the tested image, alongside its uncompressed output reference. Offer an optional file-size target that searches supported codec quality without silently resizing unless the user enables that behavior.

**Completion criteria:** previewed pixels come from the same encoded settings used for final delivery; measured single-image bytes match the final encoder result within declared metadata variation. For a 2 MB limit, produce a valid file below the limit or explain why the selected constraints cannot meet it. Label batch estimates as estimates and exclude unsupported/lossless formats from meaningless quality controls.

**Code:** [ExportView current image/configuration](../apps/desktop/qml/workspaces/ExportView.qml), [image encoder](../apps/desktop/src/export_image_encoder.h), [image settings](../apps/desktop/src/export_image_settings.h).

### 29. Export several renditions of the same selection in one action

**Priority / owner:** P2; export/experience. **Type:** new batch-delivery composition.

**Already exists / actual gap:** Batch export exists, but its API takes one preset and destination for the asset list. Multiple different deliveries require separate configuration/queue operations.

**Change:** let the user select two or three output recipes—such as full-resolution TIFF, web JPEG, and small preview—with independent suffixes/subfolders. Show photo count and total output count distinctly, and group the resulting jobs by delivery.

**Completion criteria:** 20 photos × 3 renditions previews and produces exactly 60 outputs with the intended independent settings. Retry one failed rendition without repeating the other 59. All renditions capture the same source edit revision even while the user continues editing. Cancelling a delivery group leaves clearly labeled completed outputs and cancelled work.

**Code:** [queueBatchExport API](../apps/desktop/src/export_job_controller.h), [ExportView queueBatchExports](../apps/desktop/qml/workspaces/ExportView.qml), [export controller](../apps/desktop/src/export_job_controller.cpp).

### 30. Choose the background when exporting transparency to an opaque format

**Priority / owner:** P1; export/experience. **Type:** verified hard-coded output behavior.

**Already exists / actual gap:** Alpha-preserving formats exist, and the settings distinguish preserve versus flatten. `export_alpha_behavior()` explicitly reports `flatten-to-white-opaque` for flattened output; the active export UI does not expose that background as a choice.

**Change:** show transparency against a checkerboard and offer white, black, or a chosen background color when the output requires flattening. Keep source-layer transparency and the editing recipe untouched; preview the actual edge composite.

**Completion criteria:** export a cutout with feathered and semitransparent edges to JPEG against all three background choices; pixels match the declared alpha/compositing calculation with no dark/white fringe. PNG/TIFF preserve alpha when selected. Switching format updates the relevant controls and never silently substitutes a background different from the preview.

**Code:** [export_alpha_behavior](../apps/desktop/src/export_image_settings.h), [image encoder](../apps/desktop/src/export_image_encoder.h), [ExportView](../apps/desktop/qml/workspaces/ExportView.qml).

### 31. Save real reusable delivery presets

**Priority / owner:** P1; export/experience. **Type:** new saved configuration over the fixed preset list.

**Already exists / actual gap:** Built-in export presets and edit-look preset management exist. The active export combo is a fixed model; a named edit look is not a saved output configuration containing quality, resize, naming, metadata, sharpening, and destination rules.

**Change:** let the user save, duplicate, rename, and update a delivery preset from the new export controls. Store typed/versioned settings with a stable ID. Destination inclusion should be optional; built-in presets should be duplicable rather than destructively replaced.

**Completion criteria:** save “Client gallery” with JPEG 90, 2400-pixel long edge, no location metadata, output sharpening, and a naming template. Relaunch and apply it to different photos: every setting returns correctly. Renaming changes no encoding behavior; updating it does not mutate jobs already queued. Import/export of delivery presets, if included, validates unsupported settings and never silently falls back to unrelated built-ins.

**Code:** [ExportView fixed preset model](../apps/desktop/qml/workspaces/ExportView.qml), [preset-name settings resolution](../apps/desktop/src/export_image_settings.h), [export controller](../apps/desktop/src/export_job_controller.cpp).

## Shared completion protocol

### Performance measurements

The numbers above are **proposed acceptance budgets**, not measured results or promised performance on every computer. Declare the reference Apple Silicon Mac, RAM, GPU/CPU, macOS, app build, monitor size/scale/profile, power mode, and source storage before collecting a baseline. Use a supported 32 GB-or-higher reference configuration; report the minimum supported hardware separately rather than averaging the tiers together.

Use 24–45 MP RAW and high-resolution JPEG sources at a 2560×1600-class display, fit and 100% views, with both a simple recipe and a stress recipe containing ten layers and five substantial masks. Include color, curve, spatial-detail, and generated/brush-mask work. Cold decode and warm interaction are different scenarios. Do not shrink the image, disable masks, turn off persistence, or substitute fixture-only rendering to claim an end-to-end pass.

For each interactive control, collect at least 100 gestures across five independent runs, including two-second continuous drags. Record p50, p95, maximum, time to first edited pixels, gaps during drag, release-to-accurate viewport, full-image completion, GUI heartbeat, cancellations, and peak RSS. No compilation should run during timing. Store raw event data and a screen recording; render completion without frame presentation is not the endpoint. Compare against a same-workload baseline and do not hide a failing control in an aggregate average.

### Color and mask evidence

Use at least 12 real photos covering daylight, tungsten, mixed light, skin tones, night shadows/noise, foliage, saturated colors, backlit edges, bright clouds/speculars, and flat/low-contrast subjects. Include at least four RAW captures from three camera families, with recorded profile and decoder metadata, plus JPEG/TIFF sources. License and hash assets and record recipe/output settings. Synthetic ramps and alpha fixtures supplement real photos; they do not replace them. Reuse the stricter corpus requirements of the color owner where applicable.

For same-dimension lossless SDR comparisons using identical rendering/output settings, require max channel difference ≤1/255 between native final preview readback and flattened export, and between draft and its designated same-resolution reference. Compare resized sources consistently so resampling differences are not misdiagnosed. Test floating HDR/signed stages before encoding with an explicit stage-specific absolute/relative error budget agreed before implementation; an 8-bit SDR pass cannot certify HDR. Lossy formats use a declared codec tolerance and separate visual inspection.

Mask reference comparisons use ≤1 source-pixel boundary displacement and ≤1/255 alpha difference away from antialiasing boundaries, plus ≤1 device-pixel overlay alignment. State the reference rasterizer and transform. Fine-edge quality also needs real composited-photo review: a matching implementation and reference can share the same unattractive edge.

For tone and grading appearance, two experienced photographers review labeled tasks but blinded candidate order against the baseline and agreed intent. Record acceptance per image and the reason for rejection. Unresolved objection on a required stress image leaves that gate open; passing automated numbers alone does not establish photographic quality.

### Evidence packet for each item

Record the item number, canonical owner, responsible engineer/reviewer, source revision and dirty-state snapshot, reproduction/fixture, chosen behavior, implementation paths, exact commands/results, numerical artifacts, and screenshots/recordings. State remaining limits and supported hardware/backend scope. Use durable repository-approved artifact storage with hashes; a deleted `/private/tmp` file cannot be the sole completion evidence. Once all gates pass, link the packet from the existing owner before updating its status.

## Recommended delivery order

1. **Make editing usable:** close 1 and 5 first, then 3, 6, 11, and 12. This addresses feedback, returning to masks, precision, truthful comparison, and predictable gestures.
2. **Improve photographic tools:** close 2, 4, 7, and 8; add the on-image tools in 9 and 10. Treat 13 and 14 as subsequent tool expansions, not prerequisites for fixing the original complaints.
3. **Make history useful for real editing sessions:** close 15 and 16, then 17–21. Persistent history is shared infrastructure; filters, differences, snapshot organization, and deletion recovery remain distinct user-facing outcomes.
4. **Make export configurable:** build the typed output-settings foundation in 23, then 24–27 and 30. Add reusable delivery presets in 31, encoded preview in 28, and multiple renditions in 29. Link completed deliveries back to exact edit revisions through 22.

Items that share implementation should be delivered together where practical. Do not create separate infrastructure for every numbered requirement or count an existing feature's regression test as a new improvement.

## Overall acceptance run

Use an isolated catalog with 20 representative real photos already available for editing. Correct exposure and white balance, shape shadows/highlights, make a subtle curve and grade, and refine at least five masks. Click away from every mask and return. Exercise section-only comparison and precise pad gestures. Save several editing directions, search and preview old history, rename a snapshot, recover a deleted snapshot, visit enough photos to trigger cache eviction, and relaunch into the same usable history.

Export the set using a custom delivery preset and multiple renditions, inspect encoded quality at output size, choose metadata/naming/background/sharpening intentionally, and retry one failed output. Use a completed delivery's history milestone to retrieve its exact edit revision while preserving newer work.

Completion requires linked evidence for the six reported problems and all 25 concrete additions. The visible tool changes must be demonstrated in native use, color/mask results must pass real-photo review, and export files and reopened history must match the user's choices. No wrong-target edits, silent loss of retained history, accidental snapshot loss, mislabeled comparison scope, or advertised output setting that the encoder ignores may remain. Hardware or human-review gaps stay explicitly unverified.

## Audit validation and boundaries

The original investigation traced tone, curve, grading, render scheduling, save, and mask-target code. This revision additionally inspected the active History and Export surfaces, history storage/eviction/restore behavior, preset-derived encoding and delivery planning, two-axis pads, exposure bounds, white-balance display mapping, section-comparison scopes, and available tool APIs. Links identify both inspected evidence and follow-up implementation locations; they do not claim a complete audit of every linked file.

The original investigation was document-only work: no application behavior was changed, no new native benchmark was run, and no user photos or catalog records were modified during that investigation. Subsequent implementation is recorded in the evidence section above. The original native UI inspection could not load its runtime. Accordingly, source-level interaction mismatches require native reproduction before a runtime-fix claim; the new interaction proposals have explicit before/after scope and completion gates.

The document remains registered as supporting material in `docs/checklist-index.json`. The previously observed global-index error concerned `docs/color-engine-audit-remediation-2026-09-05.md`; validation of this revision reports current registry results independently. Document checks verify exactly six original areas plus eight tool, eight history, and nine export additions, with resolvable local links and criteria for each.
