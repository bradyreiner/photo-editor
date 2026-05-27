# Photo Tool Polish Audit

This document is the execution plan for making the existing photo-editing tools feel professional, intuitive, fast, and trustworthy. It intentionally avoids broad feature expansion. The goal is to perfect the tools already present or clearly underway.

## Product Principle

The app should not win by having the longest tool list. It should win because each tool is:

- easy to find
- obvious when active
- precise when needed
- fast enough to feel direct
- accurate between preview and export
- reversible through undo, reset, and history
- usable with keyboard, mouse, trackpad, and tablet
- integrated with masks, layers, presets, recipes, and export

Every tool should feel like it belongs to one coherent editor, not like a separate experiment.

## Tool Quality Bar

Use this checklist for every tool before calling it finished.

- [ ] Entry point is obvious in the workspace.
- [ ] Active state is visually unmistakable.
- [ ] Only relevant controls are shown while the tool is active.
- [ ] On-canvas handles exist when direct manipulation is expected.
- [ ] Slider/value labels match the real render direction.
- [ ] Reset restores the correct default without side effects.
- [ ] Undo groups continuous drags or brush strokes into useful history steps.
- [ ] Before/after works for the tool's result.
- [ ] Preview updates quickly enough to guide the user.
- [ ] Settled preview matches export within the intended tolerance.
- [ ] Tool works correctly with masks/layers/presets where applicable.
- [ ] Tool communicates why an action is unavailable.
- [ ] Keyboard shortcuts exist for the core workflow.
- [ ] Tests or visual proof cover the highest-risk behavior.

## Priority Order

### 1. Basic Edit Flow

These tools form the first impression. They must feel calm, predictable, and fast before deeper tools matter.

- crop and transform
- light controls
- color controls
- tone curve
- presets
- before/after
- reset and undo

### 2. Local Editing Flow

These tools are powerful but easy to make confusing. They need strong active states, clear stack behavior, and live feedback.

- mask stack
- brush masks
- linear/radial masks
- luminance/color/depth/alpha masks
- smart/object/subject/sky/background/people selections if available locally
- mask refine controls
- overlay modes

### 3. Retouch Flow

Retouch should feel like its own workflow, not like hidden mask settings.

- clone
- heal
- patch
- remove/inpaint fallback
- dust
- red-eye
- skin cleanup
- frequency smoothing
- retouch stroke management

### 4. Layer Flow

Layers should explain the image stack. The user should know what each layer affects and why.

- adjustment layers
- pixel/fill/text/source layers
- layer masks
- clipping masks
- groups
- opacity
- blend modes
- transforms
- layer visibility and isolation

### 5. Proof And Delivery Flow

These features make the editor trustworthy for professional output.

- histogram/scopes
- clipping overlay
- gamut overlay
- soft proofing
- preview/export parity
- export preflight
- pixel inspection

## Audit Matrix

Fill this table one tool at a time. Do not mark a tool complete until the problems column is empty or deliberately accepted.

| Tool | Findable | Active State | Direct Control | Fast | Accurate | Undo/Reset | Tests | Problems |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Crop | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Transform | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Exposure | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Temperature/Tint | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Curves | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Presets | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Brush Mask | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Linear Mask | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Radial Mask | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Range Masks | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Retouch Clone | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Retouch Heal | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Retouch Remove | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Layers | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| Export | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |

## Tool Pass Template

For each tool pass, write the result in this format.

### Tool Name

User expectation:

- What a photographer expects the tool to do.
- What should happen on click, drag, reset, undo, and export.

Current behavior:

- Where the tool lives.
- How it is activated.
- What controls appear.
- What render path it uses.

Friction:

- Findability issues.
- Confusing labels or direction.
- Slow preview paths.
- Missing direct manipulation.
- Missing proof/test coverage.

Fixes:

- UI changes.
- Controller/state changes.
- Render/performance changes.
- Tests and proof.

Acceptance:

- What must be true before this tool is considered polished.

## Specific Polish Targets

### Crop And Transform

- [x] Crop mode has a clear active border and tool header.
- [x] Aspect ratio presets are discoverable and editable.
- [x] On-canvas handles are large enough and do not fight pan/zoom.
- [x] Straighten has a direct horizon drag interaction.
- [x] Grid overlays are available but not visually noisy.
- [x] Reset crop and reset transform are separate if they affect different state.
- [x] Export crop result matches preview crop bounds exactly.

Status: Crop handles now use larger invisible hit targets while keeping compact visual knobs. Desktop smoke coverage proves a near-corner grab selects the handle and a center grab still moves the crop instead of treating the full crop as a handle.
Status: Crop mode now draws a non-interactive active border around the painted image area and the HUD header names the current crop submode. Desktop smoke coverage proves the border is visible and the header changes for Box, Transform, and Horizon.
Status: Crop guide overlays now expose all six choices as testable controls and use lower named opacity caps for grid/spiral overlays. Desktop smoke coverage proves each guide mode binds to the on-canvas overlay and the guide opacity stays below the noise threshold.

### Light And Color Controls

- [x] Control order matches photographer expectation: WB, exposure, tone, color, detail.
- [x] Every signed control has correct visual direction and value text.
- [x] Double-click reset works everywhere.
- [x] Dragging many sliders does not start competing full-resolution renders.
- [x] Changed controls are visually marked.
- [x] Panel reset only resets the current group.
- [x] Numerical input is supported where precision matters.
- [x] Preview/export parity tests cover high-risk controls.

Status: Studio `AdjustSlider` now handles reset requests at the shared wrapper level, so every adjustment slider gets double-click reset behavior by default. Desktop smoke coverage proves reset restores both a zero-default exposure slider and a nonzero-default channel mixer slider and clears their modified markers.
Status: The Studio Adjust stack now starts with White Balance, Light/Exposure, Tone Curve, Color, then Presence, with RAW moved below the primary creative controls. Desktop smoke coverage proves the visible primary group order.
Status: Shared signed value text now includes explicit units (`+40%`, `-40%`, `+0.85 EV`) and centered signed sliders fill away from neutral in the correct direction. Desktop smoke coverage proves positive values fill right, negative values fill left, and tint keeps directional track colors.
Status: Precision value fields are now testable on shared sliders. Desktop smoke coverage proves typed exposure input accepts EV text (`+1.37 EV`) and white-balance input accepts Kelvin text (`6500K`) through the same slider value-field path users edit.
Status: Changed adjustment sliders expose a visible modified marker tied to the shared `modified` state. Desktop smoke coverage proves the exposure marker appears after a precise edit and clears when the slider resets to default.
Status: Rapid adjustment changes now have explicit controller coverage. The preview scheduler coalesces a burst across Light, Color, Presence, and White Balance controls into draft interactive work, keeps full-resolution rendering deferred, and proves the pending full render uses the latest adjustment state rather than stale intermediate slider values.
Status: Render parity coverage now names high-risk Light and Color recipes directly: white balance/tone, tone curve, HSL/selective color, RGB channel mixer, black-and-white mixer, color replacement/harmony, and presence/grading. Each recipe must visibly affect the source and match between preview and full-resolution export rendering within tolerance.

### Tone Curve

- [x] Curve points are easy to select, drag, delete, and reset.
- [x] Channel mode is obvious.
- [x] Histogram backdrop does not obscure point editing.
- [x] Curve presets are previewable and reversible.
- [x] Point curve and parametric curve interaction is understandable.
- [x] Tests cover neutral preservation, per-channel behavior, and endpoint behavior.

Status: Tone Curve now exposes the shared editor as a testable Studio control with visible channel/mode and point-count badges, explicit selected-point delete, and active-curve reset. Desktop smoke coverage proves channel mode is visible, selected interior points can be deleted through the control path, and reset restores the active curve.
Status: Tone Curve histogram opacity is now capped through named editor properties instead of hidden canvas literals. Desktop smoke coverage proves primary and RGB overlay opacity stays below the point-editing noise threshold.
Status: Curve presets now expose non-mutating preview point data from the controller, and the shared editor can preview a preset curve on hover without applying it. Controller and desktop smoke coverage prove previewing and clearing the preview leaves the active recipe unchanged.
Status: The controller now exposes the active curve mode (`Neutral`, `Parametric`, or `Point`) and the shared editor includes it in the channel badge. Desktop smoke and controller coverage prove RGB sliders report Parametric mode, drawing/applying a point curve switches to Point mode, and RGB parametric controls clear when point curves take over.
Status: Curve math coverage now includes identity point-curve neutral preservation, endpoint black/white point behavior, red-channel isolation, and luma curve chroma-ratio preservation.

### Presets

- [x] Presets show what settings they change.
- [x] Preset intensity behavior is clear and predictable.
- [x] Applying a preset can target all settings or selected groups.
- [x] Saved presets can be renamed, deleted, and organized.
- [x] Preset hover/preview does not permanently mutate the recipe.
- [x] Preset application is recorded clearly in history.

Status: Presets now expose changed-control chips through `presetChangedControls(...)`, scoped by the active adjustment groups and current intensity. Controller coverage proves Landscape Pop reports Texture, Clarity, and Vibrance before apply, and reports no changed controls at zero intensity.
Status: Preset application now has one shared preview/apply recipe path for intensity and group scoping. Batch application in Edit View uses `presetPreviewRecipeJson(...)`, so the recipe applied to selected assets matches the user-selected intensity and active groups instead of the raw preset JSON.
Status: Presets can be applied to all declared settings or selected groups. Controller coverage proves detail-only application updates Texture/Dehaze while leaving excluded color controls untouched, and RAW-only application restores RAW decode controls without touching tone controls.
Status: User preset management now supports rename, delete, custom pack save, and pack move from the controller, with the Edit View save row exposing both preset name and pack fields. Controller coverage proves rename preserves favorites, rejects built-in and duplicate destinations, moves presets between packs/categories, keeps search working by pack name, and deletes the organized preset afterward.
Status: Preset preview JSON is non-mutating. Controller coverage proves previewing Landscape Pop at 50% intensity for the detail group produces the expected Texture/Clarity recipe while leaving the current controller state unchanged.
Status: Applying a preset records a useful undo/status event. Controller coverage proves applying Landscape Pop creates undo history and reports `Applied preset 'Landscape Pop'` in status.

### Mask Stack

- [x] Creating a mask immediately shows the mask in the stack and on canvas.
- [x] Active mask is unmistakable.
- [x] Add, subtract, intersect, and invert are clear controls.
- [x] Overlay mode is easy to switch: red overlay, matte, marching ants, edge.
- [x] Mask enable/disable and solo are available.
- [x] Mask thumbnails reflect the real matte.
- [x] Mask settings are grouped separately from local color adjustments.
- [x] Selection refine controls explain their effect visually.
- [x] Reusable mask outputs are visible as dependencies.

Status: The Studio mask toolbar now shows a concise active-mask state summary with source type, combine operation, and normal/inverted state. Desktop smoke coverage proves creating a Linear mask with Intersect immediately shows `Linear source / Intersect / Normal`, and toggling inversion updates the same status to `Inverted`.
Status: Studio now shows a compact `MASK STACK` strip immediately after mask creation, with the active mask set name and source count, and the edit viewport mounts a mask canvas overlay instead of the previous generic tint. Desktop smoke coverage proves creating a Linear mask immediately shows the stack chip and an on-canvas overlay while hiding the placeholder tint.
Status: The detailed Mask inspector now uses the same normalized source/operation/state language in the active mask card and source rows. This makes source rows distinguish Normal/Inverted, Off, Solo, and Locked without relying only on borders or button state.
Status: Add, Subtract, Intersect, Exclude, Replace, Union, and Invert controls now have testable object names and explicit tooltips in Mask Properties. Existing smoke coverage already proves the shared creation toolbar applies Intersect and Subtract to newly created linear and brush masks.
Status: Overlay mode switching already exposes Overlay, Matte, On Light, On Dark, Outline, Edge, and Affected buttons in Mask inspector, with controller coverage proving preview sources change across matte/outline/edge modes.
Status: Enable/disable and solo are available per mask source and at the active-mask property level; source-row summaries now reflect Off and Solo state directly.
Status: Mask sets and individual sources now expose compact `thumbnailSource` PNG mattes generated from the same local-mask evaluation path as active previews, and the Mask Sets list renders those thumbnails instead of only colored dots. Controller coverage proves set/source thumbnails are real PNG data URLs, update when a smart-selection matte changes, and exist for grouped refine brush sources.
Status: Reusable mask outputs and source dependencies are now visible in the Mask Sets model and UI. Sources expose `dependencyLabel` for reusable outputs and parent dependencies, the inspector lists reusable outputs, and controller coverage proves a dependent radial source reports `Depends on Reusable Base` while the base source reports `Reusable output: base-alpha`.
Status: Mask inspector sections are explicitly split into Mask Properties, Selection, Edge, Adjust, Shape, Brush, AI Refine, and Range Refine, with stable object names on the core sections. The QML build proves the grouped inspector compiles, and the existing controller coverage proves selection/edge/range controls write the intended mask state separately from local color adjustments.
Status: Selection refine now includes a compact visual effect meter above the Edge sliders. It reports neutral vs active refine state, shows Normal/Inverted state, and draws per-effect bars for Expand/Contract, Feather, Smooth, Fine Edge, Edge Contrast, and Spill cleanup; focused build coverage proves the refined UI compiles, while controller/math coverage proves those controls affect matte evaluation.

### Brush Masks

- [x] Brush cursor shows true size, hardness, and mode.
- [x] Brush HUD shows size, flow, feather/hardness, erase/add/subtract.
- [x] Keyboard shortcuts adjust size and feather without leaving the canvas.
- [x] Tablet pressure is visible and testable.
- [x] Brush strokes have immediate overlay feedback.
- [x] Undo removes the last stroke, not the whole mask unless expected.
- [x] Stroke smoothing does not lag behind the cursor.

Status: The Studio edit viewport now exposes a real brush cursor ring with testable diameter, hardness diameter, active Add/Erase mode, and live pressure readout. The HUD reports Size, Flow, Hardness, and Pressure from the same controller state, and desktop smoke coverage proves the HUD, cursor, mode, and pressure text update while painting.
Status: Brush shortcuts are now discoverable/testable on the Studio workspace for Add, Erase, size down/up, and hardness down/up. Desktop smoke coverage proves the shortcut objects exist beside the HUD controls, and the adjustment functions still update the controller values without leaving the canvas.
Status: Brush strokes now commit as explicit undo transactions. The controller captures the exact pre-stroke edit state at stroke start, records the active mask index in in-memory history state, and pushes that state only when the stroke changes the mask. Controller coverage proves undo removes the last completed stroke samples without removing the active brush mask.
Status: Immediate brush feedback is covered by the live stroke path: active brush strokes expose interpolated live samples before commit, build a live raster preview source, and clear the transient live buffer after commit. Existing controller coverage proves long drags create continuous mattes and separated strokes do not bridge through gaps.
Status: Brush smoothing now dampens intermediate stroke samples without moving the live endpoint away from the cursor. Controller coverage proves a fully smoothed stroke still exposes and commits the exact current cursor point while keeping earlier samples damped.

### Retouch

- [x] Retouch tools have distinct modes and names, not buried generic mask state.
- [x] Clone source pin is visible and movable.
- [x] Heal/clone/remove communicate whether a source is required.
- [x] Retouch strokes are listed and individually removable.
- [x] Retouch before/after toggle isolates retouch only.
- [x] Dust and red-eye tools expose a review workflow before committing.
- [x] Local algorithm fallback is clear when advanced inpaint is unavailable.

Status: The Studio retouch HUD now includes a mode-specific status label (`Remove`, `Heal`, `Patch`, `Clone Stamp`, `Dust Cleanup`, `Red Eye`, `Skin Cleanup`, or `Frequency Smoothing`) plus the current requirement and Before/After state. Desktop smoke coverage proves Heal reports `Paint area`, Remove reports `Remove`, and Clone Stamp reports `Pick source` before a source is selected.
Status: Clone Stamp now has a primary Studio viewport source pin once a source is picked. The pin reports `Source set`, displays on the canvas independently of the older mask overlay path, and exposes a move handler; desktop smoke coverage proves moving the pin updates the active clone source coordinates in the controller.
Status: Retouch operations are listed separately from selection masks in the Retouch inspector stack with Select, Apply, Cancel, Rename, Off/On, Up, Down, and Delete actions. Existing controller coverage proves retouch entries can be renamed, disabled, reordered, deleted, canceled while pending, and applied without collapsing the whole retouch stack into generic mask state.
Status: Retouch before/after is available in both the Studio HUD and Retouch inspector. Controller coverage proves Before disables retouch masks only for the preview render request without mutating the committed masks, and After restores retouch masks in preview state.
Status: Dust removal, reusable sensor dust maps, and red-eye removal now stage detected retouch masks for review instead of committing immediately. Controller coverage proves each generated mask exposes a preview matte and remains uncommitted until Apply, and proves staged dust/red-eye edits can be discarded before commit.
Status: Local inpaint fallback is explicit in the retouch stack audit text as `Local fallback`, and controller coverage proves the CPU fallback writes deterministic patch data, model name, prompt, seed, blend amount, candidate count, source mask, and diagnostics when advanced inpaint is unavailable.

### Layers

- [x] Layer stack has thumbnails, visibility, opacity, blend mode, and active state.
- [x] Layer masks are visible as masks, not hidden layer properties.
- [x] Clipping masks show dependency on the layer below.
- [x] Blend mode changes preview instantly at draft quality.
- [x] Layer transform handles are direct and precise.
- [x] Solo/isolate current layer is available.
- [x] Layer comps or visibility states are considered after the core stack is stable.

Status: Layer rows now expose a real thumbnail cell, visible mask badge, opacity percent, opacity step controls, blend-mode cycle, clipping-mask toggle, layer-mask toggle, visibility, and row isolate state. Controller coverage proves opacity, blend mode, clipping, and layer masks normalize into row metadata, stay blocked on locked layers, and carry into draft render requests. Desktop smoke coverage proves the Studio layer row reflects live opacity, Multiply blend mode, clipping, mask toggle, mask badge, and isolate/clear-isolate state.
Status: Photo layer stack changes now render through the draft preview path (`photo_layer_change`) instead of scheduling a full-resolution render; controller coverage proves layer opacity/blend changes do not arm the full render timer or pending full render request.
Status: The active photo layer now has a direct canvas transform overlay with a selectable layer target, move surface, corner scale handles, rotate handle, and live X/Y/scale/rotation readout. Desktop smoke coverage proves the Studio layer row exposes transform targeting, the overlay/handles are visible for an editable text layer, and a viewport move updates exact layer transform metadata through `setPhotoLayerTransform`.
Status: Layer visibility states now exist as a lightweight layer-comp foundation: the controller can save the current enabled/solo state for each layer by stable layer id, restore it later without touching locked layers, and delete saved states. Studio exposes Save/Apply/Delete visibility-state controls in the layer panel, and controller plus desktop smoke coverage prove saved states summarize visible layers, restore visibility/isolation, and return the stack to normal editing.

### Scopes And Proof

- [x] Histogram/scopes update without blocking the UI thread.
- [x] Clipping and gamut overlays clearly indicate what target they refer to.
- [x] Soft proof profile is visible near export settings.
- [x] Pixel inspector can read before/after values.
- [x] Preview/export diff exists for proofing difficult recipes.
- [x] Export preflight catches profile, gamut, clipping, size, and missing-file issues.

Status: Histogram and scope computation now exposes explicit async activity state while work is running through the existing `QFutureWatcher` path. Controller coverage proves histogram/scope updates queue without blocking the caller, publish histogram/waveform/vectorscope data after completion, and clear the active state when idle. The Studio scopes inspector also surfaces the background update state, with desktop smoke coverage proving the status label is present.
Status: Clipping and gamut overlays now carry explicit target labels in the edit viewport: clipping identifies preview clipping with blue shadow/red highlight semantics, and gamut identifies the active output color space. Desktop smoke coverage proves both target labels are present and readable when the overlays are enabled.
Status: The edit viewport now drives a non-render-blocking pixel inspector from normalized image coordinates. The HUD follows the cursor and reports sampled pixel coordinates, After RGB/hex, Before RGB/hex when compare/reference data is available, and RGB/luminance deltas. Controller coverage proves sampled before/after preview RGB values and deltas come from the cached rendered previews, while desktop smoke coverage proves the Studio HUD labels are present in the edit workspace.
Status: Export now has a first-class preflight result exposed by the export controller and surfaced in the Export view before queueing. It reports blocking missing source/destination errors, target format/profile, soft proof profile, estimated output size, web/profile mismatches, gamut-mapping risk, preview clipping, and RAW clipping. Export controller coverage proves profile, soft-proof, clipping, size, missing-source, and missing-destination checks are produced before an export job is queued.
Status: The Export view now shows the active soft proof profile directly in the preflight panel beside the output format/profile/size summary, so proofing context is visible before queueing the job. Export controller coverage proves the preflight result carries the soft proof profile and flags a mismatch from the export output profile.
Status: Rendered photo exports now compare the exact in-memory rendered image against the file reloaded from disk and persist a `previewExportDiff` proof with dimensions, max/mean channel delta, preset tolerance, pass/fail state, and status. Export controller coverage proves a difficult crop-bounds recipe publishes the diff, persists it in job metadata, and verifies the written lossless export stays within a one-channel tolerance of the preview render.

## UX Rules For The Editor

- Do not show every control all the time.
- Do not bury the active tool state in tiny text.
- Do not make users guess whether they are editing global image state, a mask, or a layer.
- Do not let background full-resolution renders make live tools feel broken.
- Do not make powerful tools depend on hidden keyboard knowledge.
- Prefer direct manipulation on the image when the tool has geometry.
- Prefer compact, contextual inspectors over long walls of controls.
- Prefer one unified edit stack mental model over separate hidden systems.

## First Three Execution Passes

### Pass 1: Basic Edit Tool Trust

- [x] Audit all light/color slider directions, labels, ranges, reset values, and typed inputs.
- [x] Fix any control whose display value does not match render behavior.
- [x] Add tests for signed direction on high-risk color and tone controls.
- [x] Ensure slider drag schedules live preview without immediate expensive full render contention.

Status: The shared slider contract now covers signed direction, neutral/default positions, modified markers, typed raw values, typed Kelvin values, typed degree values for normalized hue controls, and typed percent values for normalized radius/position controls. Studio smoke coverage proves high-risk light/color sliders expose the expected ranges/defaults, signed adjustments fill in the correct direction from neutral, double-click reset restores declared defaults, hue fields accept displayed degree units, and normalized percent fields accept displayed percent units instead of raw 0..1 values.
- [x] Make changed controls visually obvious.

Current status as of 2026-05-24:

- Temperature display and typed Kelvin input now move in the same warm/cool direction as the render engine.
- High-risk signed controls have numeric tests for temperature, tint, highlights, and shadows.
- Rapid slider edits defer the full-resolution render until interactive preview work settles.
- The shared inspector slider now marks modified controls with accent color, a changed-state dot, and a filled value rail.
- Specialized color/detail/transform controls now use explicit slider handlers, correct identity defaults, and testable QML object names for high-risk controls.
- Channel mixer identity channels, color replacement defaults, skin-tone defaults, color-grading hue range, crop zoom, transform scale, liquify radius, and mask opacity/density/inpaint blend no longer appear modified at their neutral state.
- Per-panel reset behavior is now available from the active Studio inspector headers for White Balance, Light, Color, Presence, Detail, Lens, and RAW, with controller-level group reset behavior that preserves unrelated controls.
- Remaining audit work: broader per-tool evidence for the unchecked specific polish targets.

### Pass 2: Crop And Mask Usability

- [x] Make crop/transform active states and handles clear.
- [x] Simplify mask creation into obvious choices.
- [x] Make mask stack active/solo/disable/overlay controls clear.
- [x] Add brush HUD and reliable shortcut behavior.
- [x] Ensure mask previews update immediately at draft quality.

Current status as of 2026-05-24:

- Crop now has an explicit active HUD with Crop Box, Transform, and Done controls.
- Crop Box and Transform are separate submodes, so the transform handles are reachable instead of being hidden behind crop-box editing state.
- Desktop smoke coverage proves the QML crop HUD, crop overlay, transform overlay, and crop submode switching stay wired correctly.
- The crop inspector now exposes typed percent controls for Crop X, Crop Y, Crop W, and Crop H, plus separate Reset Crop and Reset Transform commands.
- Controller coverage proves Reset Crop preserves transform, perspective, liquify, and crop-fill state, while Reset Transform preserves explicit crop geometry and reports no-op state correctly.
- Studio now exposes the full crop aspect preset list through a combo box, plus Add and Remove controls for custom ratios backed by the controller's persisted custom preset API.
- Desktop smoke coverage proves a custom aspect ratio can be added, selected, removed, and returned to Original from the Studio crop inspector.
- Crop now has a Horizon submode with a dedicated on-canvas guide overlay that commits dragged endpoints through the same straighten-from-horizon controller path.
- Desktop smoke coverage proves Horizon mode activates a direct guide surface and that normalized guide endpoints update the rotation correction.
- Export controller coverage now uses a unique per-pixel source to prove the exported crop has the same bounds and pixels as the preview crop render.
- Mask creation now separates source choice from combine mode: Object, Brush, Lasso, Linear, and Radial are distinct from Add, Subtract, and Intersect.
- New mask sources inherit the selected combine mode, Intersect is no longer a dead control, and the creation toolbar forces the overlay preview on so the result is visible immediately.
- Desktop smoke coverage proves Intersect linear-mask creation and Subtract brush creation are wired through the QML toolbar.
- Mask preview labels now use explicit terms: Overlay, Matte, On Light, On Dark, Outline, Edge, and Affected.
- Mask source rows now expose On/Off, Solo, and Lock/Locked as direct controls instead of relying on abbreviated state letters.
- Full debug build and the focused desktop/editor suite pass with the latest crop, mask creation, mask stack, and render scheduling changes.
- Brush masking now has an on-canvas HUD with explicit Add, Erase, Size, Flow, Hardness, and Done controls.
- Studio-level brush shortcuts now route B to Add, E to Erase, and [ / ] to brush size while preserving the same QML functions used by the HUD.
- Desktop smoke coverage proves the brush HUD appears for an active brush, erase/add mode switches are reflected in the UI, and size/flow/hardness controls update the active brush.
- Mask stack edits now force a draft interactive render first and defer the settled full render behind it, so mask geometry/stack changes are not blocked by the high-quality render path.
- Controller coverage proves a new mask exposes its matte preview immediately, marks the next interactive render as draft quality, and queues the full render as pending instead of starting the full timer right away.

### Pass 3: Retouch And Layer Coherence

- [x] Split retouch modes into clear workflows.
- [x] Add retouch stroke audit/list behavior.
- [x] Make layer stack thumbnails and dependencies easier to read.
- [x] Add layer isolate/solo workflow.
- [x] Add preview/export proof for layers plus masks.

Current status as of 2026-05-24:

- Studio now exposes retouch as its own workflow with a dedicated on-canvas HUD instead of relying on hidden mask state.
- The retouch HUD gives direct entry points for Remove, Heal, Patch, Clone, Dust, Red Eye, Skin, and Smooth, plus Apply, Regenerate, Before/After, and Done controls.
- Desktop smoke coverage proves Studio can start Heal and Remove retouch modes, shows the dedicated HUD, and reflects the active retouch mode in the on-canvas controls.
- The retouch stack now exposes audit labels, mode labels, staged/applied status, stroke counts, candidate counts, and fallback markers from the controller model.
- The retouch inspector list now uses a two-line audit card with direct Select, Apply, Cancel, Rename, On/Off, Up, Down, and Delete controls.
- Controller coverage proves retouch rows expose audit metadata and still support renaming, disabling, reordering, and deleting operations.
- Layer rows now expose a thumbnail well, readable kind/status/opacity/blend metadata, dependency text for clipping/groups/masks, and direct On/Off, Solo, Lock, Up, and Down controls.
- The controller layer model now normalizes `flatIndex`, `kindLabel`, `opacityPercent`, `layerAuditLabel`, and `dependencyLabel` so QML does not have to infer stack meaning from raw layer state.
- Controller coverage proves layer rows expose readable visible/blend metadata and that the same audit label updates for hidden and solo states.
- The active Studio inspector now exposes the layer stack beside the main adjustment workflow, with Add Current, Fill, Text, isolate, clear isolation, readable row metadata, and direct row controls.
- Layer isolation now has explicit controller operations: `isolatePhotoLayer()` forces exactly one unlocked layer into solo state, and `clearPhotoLayerSolo()` returns the full stack to normal visibility.
- Desktop shell smoke coverage proves the live Studio UI exposes isolate and clear-isolate controls with the correct inactive/active state, while controller coverage proves exact solo state transitions.
- Render parity coverage now exercises a deterministic recipe with both a selection mask and a masked fill layer, proves the preview render matches the full-resolution export render within tolerance, and verifies the encoded export artifact preserves the same pixels.

## Definition Of Done

A tool is polished when a new user can find it, a serious editor can control it precisely, the preview feels responsive, the exported result matches the preview, and the implementation has enough tests or visual proof to prevent regressions.
