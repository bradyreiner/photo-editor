# Editor Completion Remediation Breakdown

This is the implementation checklist for the remaining editor-quality work. The earlier color-tools checklist covered narrow render math and wiring. This document covers the broader product standard: the tools must be understandable, visibly responsive, non-destructive, performant, and polished enough that a photographer can trust them on RAW files.

## Completion Standard

- [x] Every visible tool has a clear purpose, a visible effect, predictable reset/default behavior, and a test that proves it changes the render when it should.
  - Status: Completed. The phase checklist below now covers the visible Adjust, Presets, Color, Tone Curve/Input Levels, Layers/Text, Masks/Brush/Object selection, Detail, histogram/scopes, thumbnails, and export surfaces with explicit acceptance criteria and verification. Focused verification passed in `build/dev-macos-debug` with `desktop_shell_smoke_test`, `editor_preview_controller_test`, `editor_preview_math_test`, `render_parity_test`, `color_tool_invariant_test`, `color_science_sweep_test`, `edit_recipe_state_test`, `render_golden_test`, `catalog_asset_model_test`, `export_job_controller_test`, `raw_corpus_test`, and `color_pipeline_test`.
- [x] The main Adjust surface only contains core editing tools. Experimental or technical controls are either redesigned or moved behind a Lab/Advanced affordance.
  - Status: Completed. Presets are separated into their own tab, technical color controls are consolidated into clearer Color Grading/Color Mix/Lab affordances, Levels is treated as optional input-level control beside the Tone Curve, and the smoke suite verifies the key editor states are visible and non-overlapping at desktop and compact sizes.
- [x] Preview, histogram, scopes, thumbnails, export, and preset thumbnails agree on the same edit recipe and color pipeline.
  - Status: Completed. Recipe serialization now preserves `photoLayers` binary payloads by base64-encoding `maskData`/`pixelData` and decoding them on parse, so saved recipes, previews, thumbnails, presets, and exports reconstruct the same layer stack. Verification passed with recipe-state round-trip, render parity/golden, catalog thumbnail, export, preview/controller, and color-pipeline tests.
- [x] Painting, masking, object selection, and layer edits give immediate overlay feedback without forcing a full-frame rerender on every pointer move.
  - Status: Completed. Brush/object/mask work is covered by the overlay and dirty-tile budget work below, including brush visual proof, live overlay behavior, and performance checks for dirty-region rendering rather than full-frame rerender per pointer move.
- [x] The app preserves gradients and shadow detail through a float/16-bit working path until the final display conversion.
  - Status: Completed. The color pipeline, tone mapping, histogram precision, render parity, golden render, raw corpus, color invariant, and real-image QA checks below now cover gradient preservation, shadow recovery, highlight rolloff, and final display conversion behavior on both synthetic and licensed real-image inputs.

## Phase 0: Color Pipeline Correctness

- [x] **Split toning hue/value mismatch**
  - Problem: The UI and engine are still not speaking one value language. The screenshot shows impossible hue display like `-22879°`, while other split-toning controls use wheel values that look like signed percent. The engine has both normalized hue code paths and signed-percent hue code paths.
  - Goal: Split toning uses one representation end to end: hue as normalized `0..1`, saturation/luminance as signed or unsigned percent with documented ranges. The UI displays `0..359°`, never huge negative values.
  - Complete it by: auditing `EditView.qml`, `ColorWheelControl.qml`, `EditorPreviewController`, CPU color grading, and Metal color grading; removing signed-percent hue conversions for grading; adding a conversion helper used everywhere; adding tests for red, yellow, cyan, blue, and magenta grade directions on CPU/GPU.
  - Acceptance: dragging the wheel to yellow makes yellow shadow tint; dragging the slider to `240°` makes blue tint; the numeric field and wheel marker always agree after save/load, preset apply, undo, and app restart.
  - Status: Completed. Grading hue now uses normalized `0..1` values in the wheel, sliders, controller setters, CPU render paths, and Metal render path. Focused verification passed with `color_tool_invariant_test`, `color_science_sweep_test`, `desktop_shell_smoke_test`, `editor_preview_math_test`, `editor_preview_controller_test`, and `render_parity_test` in `build/dev-macos-debug`.

- [x] **Split toning controls are too small and unclear**
  - Problem: The color wheels and sliders are compact, hard to target, and split between wheel cards and slider rows. The selected range is unclear.
  - Goal: A single Color Grading panel with large wheels, large sliders, range tabs, and an obvious global amount/balance control.
  - Complete it by: replacing the current compact `ColorWheelControl` layout with a larger grading component; use Shadows/Midtones/Highlights segmented tabs; show one large wheel plus Hue/Saturation/Luminance sliders; add global `Amount` and `Balance`; make sliders at least 32-40 px high with larger handles; support direct numeric degree entry.
  - Acceptance: every grading control is usable on a trackpad, values are readable, wheel/sliders stay synchronized, and reset behavior is obvious.
  - Status: Completed. The Studio inspector now uses a large focused grading wheel, three range tabs, compact preview wheels, 40 px Hue/Saturation/Luminance sliders, and real `Amount`/`Balance` controls wired through controller state, recipe JSON, CPU render, Metal render, preset diffing, preset intensity, and thumbnail previews. Focused verification passed with `desktop_shell_smoke_test`, `editor_preview_math_test`, `editor_preview_controller_test`, `edit_recipe_state_test`, `render_parity_test`, `color_tool_invariant_test`, and `color_science_sweep_test` in `build/dev-macos-debug`.

- [x] **Vibrance and split toning ordering is confusing**
  - Problem: Vibrance currently acts before color grading, so it changes existing image chroma but not the artificial chroma introduced by split toning. That is technically defensible but feels broken when the final image becomes heavily tinted and Vibrance appears unrelated.
  - Goal: Users can control final color intensity without knowing pipeline order.
  - Complete it by: keeping Vibrance as source-color protection if desired, but adding explicit Color Grading `Amount`; making global Saturation affect final chroma consistently; documenting pipeline order in code/tests, not visible app text; adding a post-grade color-intensity test.
  - Acceptance: Saturation visibly affects the final graded image; Vibrance visibly affects natural low-saturation image color; Color Grading Amount fades split toning from 0-100%.
  - Status: Completed. Vibrance now remains a protected source-color adjustment before hue-domain tools, while global Saturation is applied after color grading so it controls the final chroma introduced by split toning. The ordering is documented in the CPU and Metal render paths, and `editor_preview_math_test` now verifies grading amount fade-out, post-grade desaturation/saturation, and vibrance on low-saturation source color. Focused verification passed with `editor_preview_math_test`, `render_parity_test`, `color_tool_invariant_test`, and `color_science_sweep_test` in `build/dev-macos-debug`.

- [x] **Histogram and scopes are based on 8-bit display data**
  - Problem: `compute_histogram_data` reads `QRgb` values from a `QImage`, uses 256 bins, and scopes use 128 columns x 64 levels. This explains jagged histograms and why edits look like they lose precision when the preview path has already converted to 8-bit.
  - Goal: Histograms/scopes are computed from the high-precision rendered preview buffer where possible, then downsampled for display with smoothing.
  - Complete it by: exposing histogram input from the float working image or `RGBA16FPx4` render, using 1024 or 4096 internal bins, smoothing for display, preserving raw counts separately, and only converting to 256 display points at the QML boundary.
  - Acceptance: smooth gradients produce smooth histogram movement; strong tone/color edits do not create false combing from quantization; histogram and scopes match export previews.
  - Status: Completed. The histogram/scope sampler reads normalized values from 16-bit/float `QImage` formats, accumulates 4096 internal bins, derives 256 public count buckets, and smooths display values. Adjusted preview renders now publish a dedicated `RGBA16FPx4` histogram image from the working buffer before display quantization; the old display-image path is retained only as a fallback for cache-only results. `editor_preview_controller_test` verifies sub-8-bit scope sampling, rendered-preview scope parity, high-precision adjusted-render histogram input, immediate low-res refresh, and stale-generation rejection. Focused verification passed with `editor_preview_controller_test` and `desktop_shell_smoke_test` in `build/dev-macos-debug`.

- [x] **Posterization and gradient loss**
  - Problem: The working engine uses float pixels in places, but several preview/upload paths convert to `ARGB32`/`BGRA8Unorm`. The Metal preview texture is `MTLPixelFormatBGRA8Unorm`, and many helper paths convert images to 8-bit before display/scopes.
  - Goal: Editing stays float/16-bit until the final display texture, with dithering or 16-bit/float texture upload where supported.
  - Complete it by: auditing all `QImage::Format_ARGB32` conversions in preview/render paths; keeping preview renders as `RGBA16FPx4` or float buffers internally; switching native preview textures to `RGBA16Float`/`BGRA10_XR` where feasible; adding final display dithering when falling back to 8-bit; adding gradient torture tests.
  - Acceptance: a smooth sky/shadow ramp remains visually smooth after exposure, shadows, curves, split toning, vibrance, and export.
  - Status: Completed. Adjusted preview renders now keep a dedicated `RGBA16FPx4` high-precision image for histogram/scope analysis, and the native Metal preview texture uploads `RGBA16FPx4` images as `MTLPixelFormatRGBA16Float` with 8 bytes per pixel instead of forcing the display path through BGRA8. Normal 8-bit sources still use the existing `BGRA8Unorm` path, and region/patch uploads preserve the active texture format. Focused verification passed with `cmake --build --preset build-dev-macos-debug`, `editor_preview_controller_test`, `desktop_shell_smoke_test`, `editor_preview_math_test`, `color_pipeline_test`, `color_tool_invariant_test`, `render_parity_test`, and `git diff --check` in `build/dev-macos-debug`.

- [x] **Histogram refresh feels slow**
  - Problem: histogram computation is async and queues pending images while a watcher is running, so the UI can lag behind rapid slider changes.
  - Goal: histogram updates feel live while dragging and settle to exact values after the final render.
  - Complete it by: computing a fast low-res histogram immediately from the visible tile/preview mip, replacing it with an exact histogram after render settle, cancelling stale jobs by generation, and showing stale state only subtly.
  - Acceptance: histogram moves within one frame or two during slider drag and never displays an old result after the preview has settled.
  - Status: Completed. `startHistogramUpdate` now publishes an immediate generation-guarded low-resolution histogram/scope preview from a 192 px scaled image, then replaces it with exact async data after settle. Stale generations are rejected in both the immediate preview and settled async paths. Focused verification passed with `editor_preview_controller_test` and `desktop_shell_smoke_test` in `build/dev-macos-debug`.

## Phase 1: Tone Tools

- [x] **Exposure/shadows/highlights still do not feel like RAW recovery**
  - Problem: Brightening a RAW-like dark image should reveal detail and become obviously brighter. If the image stays dark or turns ugly, either the RAW normalization, tone mapping, preview output, or slider response curve is wrong.
  - Goal: Exposure is true stop gain; Shadows opens dark regions without gray haze; Highlights rolls off bright regions without crunchy edges.
  - Complete it by: validating RAW black/white normalization before tone; measuring luminance deltas on real dark RAW fixtures; tuning shadows/highlights with luminance masks and local contrast preservation; ensuring preview/export parity.
  - Acceptance: `+2 EV` visibly doubles scene-linear exposure before tone mapping, shadow recovery reveals texture in dark ranges, and highlight recovery has smooth rolloff.
  - Status: Completed. Exposure is applied as scene-linear stop gain before tone mapping, RAW decode has shadow boost/highlight recovery controls, highlight reconstruction modes are serialized and tested, and shadows/highlights are signed luminance-region controls with smooth rolloff and monotonic neutral-ramp behavior. Focused verification passed with `editor_preview_math_test`, `color_pipeline_test`, `color_tool_invariant_test`, and `render_parity_test` in `build/dev-macos-debug`, including positive exposure brightening, highlight shoulder protection, signed shadow/highlight response, highlight reconstruction, over-range chromatic highlight handling, and CPU/Metal parity.

- [x] **Contrast looks ugly**
  - Problem: Contrast can crush endpoints or make midtones brittle if it is applied as a broad curve without scene-aware protection.
  - Goal: Contrast adds midtone punch while preserving black/white endpoints and color ratios.
  - Complete it by: enforcing endpoint-preserving S-curve behavior, adding per-luminance-region tests, and comparing with real landscape/portrait/night fixtures.
  - Acceptance: neutral ramps stay monotonic, endpoints remain controlled, and real images gain punch without hard clipping.
  - Status: Completed. Contrast now uses a photographic tone response focused on midtones with endpoint protection and chroma-preserving luminance remapping instead of a brittle broad RGB curve. Focused verification passed with `editor_preview_math_test`, `color_tool_invariant_test`, and `render_parity_test` in `build/dev-macos-debug`, including positive/negative contrast behavior, monotonic neutral ramps, midtone-focused contrast deltas, finite alpha-safe output, and CPU/Metal parity.

- [x] **Whites and Blacks do not feel functional**
  - Problem: Whites/Blacks are expected to move endpoint placement. If they look like weak tone nudges, the user cannot set black/white points.
  - Goal: Whites controls upper endpoint energy; Blacks controls lower endpoint energy; both are visible and histogram-correlated.
  - Complete it by: making endpoint controls explicit in tone math and UI labels; showing clipping indicators while dragging; adding histogram movement tests.
  - Acceptance: Blacks moves the left histogram mass and deep shadows; Whites moves the right histogram mass and bright values.
  - Status: Completed. Whites and Blacks are explicit endpoint-energy controls in the tone anchor math, with positive Blacks lifting dark tones, negative Blacks deepening them, and positive Whites brightening highlight values while preserving highlight chroma. Focused verification passed with `editor_preview_math_test`, `color_tool_invariant_test`, `render_parity_test`, `editor_preview_controller_test`, and `desktop_shell_smoke_test` in `build/dev-macos-debug`, including signed endpoint response, neutral-ramp monotonicity, levels/whites/blacks cases, histogram refresh after edits, and clipping overlay controls in the edit workspace.

- [x] **Levels vs Tone Curve product decision**
  - Problem: Levels are currently shown as three basic sliders, but Tone Curve can already express richer mappings. The current Levels UI looks redundant and less useful.
  - Goal: Levels should either be a focused input-level utility or hidden from the main Adjust flow.
  - Complete it by: demoting Levels into Tone Curve as `Input Levels`; adding a small three-handle histogram strip for black/gamma/white; keeping it collapsed by default; removing standalone Levels sliders from the core panel.
  - Acceptance: normal users see Tone Curve, not a confusing extra Levels section; advanced users can still set input black/gamma/white precisely.
  - Status: Completed. Levels no longer appears as a standalone Light panel; Tone Curve now owns a collapsed `Input Levels` utility with a luminance histogram strip, black/gamma/white markers, and larger precise Black Input/Gamma/White Input sliders. Focused verification passed with `desktop_shell_smoke_test` in `build/dev-macos-debug`.

- [x] **Advanced Tone is unclear**
  - Problem: controls like local tone, filmic toe/shoulder, adaptive contrast, zones, and levels are mixed together without a mental model.
  - Goal: Advanced Tone is either renamed into user-facing concepts or moved into Lab.
  - Complete it by: grouping as `Highlight Roll-off`, `Shadow Fade`, `Local Contrast`, and `Zone Mixer`; adding reset per group; hiding technical controls by default.
  - Acceptance: the default Adjust panel is shorter and every visible tone control has a clear photographic outcome.
  - Status: Completed. The old `Advanced Tone` label is gone from Studio Adjust; the collapsed subgroup is now `Roll-off & Detail` with Highlight Detail, Highlight Roll-off, Shadow Fade, and Local Contrast, `Zone Mapping` is now `Zone Mixer`, and Roll-off/Detail, Zone Mixer, and Input Levels each expose a focused reset control. Focused verification passed with `desktop_shell_smoke_test` in `build/dev-macos-debug`.

## Phase 2: Presets

- [x] **Presets are in the wrong place**
  - Problem: Presets are embedded at the top of Adjust, pushing actual editing controls down and making the inspector feel cluttered.
  - Goal: Presets have their own inspector tab.
  - Complete it by: adding a `Presets` tab next to Adjust/Layers/Scopes/Detail/Info; moving the preset browser, search, intensity, apply, save, import/export, favorites, and pack management there; keeping only a small optional active-preset indicator in Adjust.
  - Acceptance: Adjust starts with editing controls; Presets opens a dedicated browser.
  - Status: Completed. The inspector now has a dedicated `Presets` tab, `presetLibraryInspectorGroup` is only visible in that tab, and Adjust filtering no longer includes presets as an adjustment group. Focused verification passed with `desktop_shell_smoke_test` in `build/dev-macos-debug`.

- [x] **Preset UI looks unfinished**
  - Problem: cards are cramped, thumbnails/gradients do not communicate the look, and management controls are dense.
  - Goal: Presets feel like a real library of Virel and custom looks.
  - Complete it by: building larger cards with real rendered thumbnails, category filters, pack sections, favorite state, preview-on-hover/press, one Apply button, and secondary management actions in a menu.
  - Acceptance: built-in Virel presets and user presets are searchable, previewable, saveable, renameable, movable, deletable, importable, and exportable without crowding the editor.
  - Status: Completed. The Presets tab now has category filters, pack filtering, favorites filtering, grouped pack headers, larger rendered-thumbnail cards, hover/press preview, one primary Apply action, and a management popup for rename/move/delete/import/export actions. Focused verification passed with `desktop_shell_smoke_test` in `build/dev-macos-debug`.

- [x] **Preset quality and coverage**
  - Problem: presets cannot be trusted if they are not tuned against real images and if their thumbnails do not match their applied result.
  - Goal: Virel presets are useful starting points for portrait, landscape, monochrome, night, drone, RAW recovery, and high-dynamic-range scenes.
  - Complete it by: retuning built-ins after tone/color fixes, rendering preset thumbnails from the same pipeline, adding preset golden tests, and storing user preset packs outside volatile cache.
  - Acceptance: applying a preset matches its thumbnail and can be undone, faded by intensity, saved as custom, synced/imported, and batch-applied.
  - Status: Completed. Built-in Virel presets now include portrait, landscape, monochrome, night, drone, RAW recovery, and HDR looks; preset thumbnails render through the same color pipeline against a fixture scene instead of a synthetic ramp; controller tests verify thumbnail/render agreement, category/pack coverage, favorites, undo, intensity fade, partial groups, safety warnings, save/apply behavior, and sync/import/export paths. User presets persist to `AppBootstrap::appDataLocation()/editor-presets.json` rather than cache. Focused verification passed with `editor_preview_controller_test` and `render_golden_test` in `build/dev-macos-debug`.

## Phase 3: Color Tools Scope Reduction

- [x] **Color Mix is unclear**
  - Problem: HSL/HSV/LCH/OKLab modes plus CMYK-style sliders make the tool look powerful but hard to predict.
  - Goal: The default Color Mixer is simple: channel swatches with Hue, Saturation, Luminance.
  - Complete it by: making HSL/OKLab a hidden advanced choice, showing affected hue range on hover/drag, adding per-channel reset, and removing CMYK sliders from the default view.
  - Acceptance: selecting Cyan and moving Saturation changes cyan things, with visible affected range and no mystery model switching.
  - Status: Completed. The default Color Mix now presents channel swatches plus Hue/Saturation/Luminance controls, hides HSL/HSV/LCH/OKLab model switching and CMYK-style sliders unless Lab is enabled, exposes an active-channel summary and per-channel reset, and shows an affected hue-range strip for the selected channel. Focused verification passed with `desktop_shell_smoke_test` in `build/dev-macos-debug`.

- [x] **Channel Mixer makes zero sense in normal editing**
  - Problem: A 3x3 matrix is an advanced technical tool, not a normal color-editing control.
  - Goal: Hide it by default or make it a proper Lab utility.
  - Complete it by: moving Channel Mixer to Lab; showing output rows (`Red output`, `Green output`, `Blue output`), source sliders, row sum warnings, monochrome mode, identity reset, and useful presets.
  - Acceptance: it never appears in the normal Adjust flow and advanced users can understand the matrix.
  - Status: Completed. Channel Mixer is hidden behind the Advanced Color `Lab` affordance, keeps explicit Red/Green/Blue output rows with source sliders, exposes Identity/Mono/Swap RB presets plus per-row reset controls, and now shows per-row sum feedback with warning coloring when a row drifts from neutral. Focused verification passed with `desktop_shell_smoke_test` in `build/dev-macos-debug`.

- [x] **Color Replace controls are not usable enough**
  - Problem: source hue, target hue, range, and strength sliders are too abstract without direct picking and mask feedback.
  - Goal: Color Replace works like pick source, choose replacement, refine mask.
  - Complete it by: adding source/target swatches, eyedroppers, affected-mask preview, range/softness handles, preserve luminance/saturation toggles, and local before/after sampling.
  - Acceptance: users can click a color in the photo, see what is selected, pick a new color, and refine the result without guessing hue degrees.
  - Status: Completed. Color Replace now exposes source and target swatches, source/target/skin eyedroppers, a mask preview toggle, range and softness refinement, strength control, preserve luminance/saturation toggles, skin protection, Auto Skin, and Reset Skin. Focused verification passed with `desktop_shell_smoke_test` in `build/dev-macos-debug`.

- [x] **Skin Tone is not a standalone usable tool**
  - Problem: a line hue/range/strength panel is too abstract and produces bad results when users do not understand the mask.
  - Goal: Skin protection is part of Color Replace/Color Grading and is automatic, inspectable, and optional.
  - Complete it by: moving skin controls behind `Protect Skin`; adding auto skin range, sample skin eyedropper, protected-mask preview, and a clear reset.
  - Acceptance: skin protection prevents unwanted hue shifts on portraits without appearing as a confusing separate editing panel.
  - Status: Completed. Skin protection is integrated into Color Replace behind `Protect Skin`, with source/target/skin eyedroppers, affected-mask preview, Auto Skin, protected hue/range/strength controls, and a new Reset Skin action. The older `Skin Protection Lab` panel remains hidden behind Lab instead of appearing in normal editing. Focused verification passed with `desktop_shell_smoke_test` in `build/dev-macos-debug`.

- [x] **Harmony is not productized**
  - Problem: hue-pull toward palette anchors is an experimental color transform without enough UI explanation or preview.
  - Goal: Either remove it from main UI or make it a creative Lab workflow.
  - Complete it by: moving to Lab, showing palette anchors, affected mask/range, named palette presets, and global amount.
  - Acceptance: normal users do not see Harmony; Lab users can understand and preview it.
  - Status: Completed. Harmony is hidden from normal editing behind the Advanced Color `Lab` affordance, exposes named Warm/Comp/Triad palette presets, palette anchor swatches, an affected-range strip, Base Hue, and a global `Amount` slider. Focused verification passed with `desktop_shell_smoke_test`, `editor_preview_math_test`, `render_parity_test`, and `color_tool_invariant_test` in `build/dev-macos-debug`.

## Phase 4: Details, Text, Layers

- [x] **Detail tab does not feel functional**
  - Problem: Detail controls exist, but users cannot see what is being sharpened/noise-reduced and cannot evaluate at 1:1.
  - Goal: Detail is a complete sharpening/noise panel with correct preview conditions.
  - Complete it by: adding enforced 1:1 detail preview, sharpen amount/radius/detail/masking controls, luminance/color noise reduction, masking overlay, and before/after tile preview.
  - Acceptance: changing sharpening or NR has an obvious effect at 1:1 and exports match preview.
  - Status: Completed. Detail now has an explicit 1:1 inspection workflow on the edit viewport, loupe and edge-mask/peaking controls, per-section before/after compare, grouped Sharpening and Noise Reduction controls, and real Amount/Radius/Detail/Masking state wired through QML, controller properties, recipe JSON, preset interpolation, preview/export render mapping, CPU detail math, and Metal fallback. Defaults preserve previous preset output hashes; non-default Radius/Detail uses the CPU detail pass for parity. Focused verification passed with `cmake --build --preset build-dev-macos-debug`, `desktop_shell_smoke_test`, `edit_recipe_state_test`, `editor_preview_controller_test`, `editor_preview_math_test`, `color_pipeline_test`, `color_tool_invariant_test`, and `render_parity_test` in `build/dev-macos-debug`.

- [x] **Text tab/workflow is incomplete**
  - Problem: Text appears to exist in carousel/layer paths, but the Edit workspace does not expose a complete text creation/editing workflow.
  - Goal: Text layers work like real layers: add text, edit content, font, size, color, alignment, transform, opacity, blend, mask, reorder, delete.
  - Complete it by: integrating text creation into Layers; adding canvas handles and inspector controls; persisting text layers in recipe/project state; rendering through preview/export.
  - Acceptance: a text layer can be created in Edit, edited later, exported, undone/redone, hidden, reordered, and transformed.
  - Status: Completed. Text creation is integrated into Layers, and each text layer row now exposes editable content, font family, size, color, bold weight, alignment, text box width/height, opacity, blend mode, mask, visibility, lock, reorder, duplicate/delete, and transform affordances. Text metadata is normalized and persisted in the photo layer model, preview/export rasterization now honors text alignment, and controller edits use the normal layer stack path for undo/redo and render invalidation. Focused verification passed with `desktop_shell_smoke_test`, `editor_preview_controller_test`, `editor_preview_math_test`, and `render_parity_test` in `build/dev-macos-debug`.

- [x] **Layers are not Photoshop-complete enough**
  - Problem: the Layers tab does not provide the core operations users expect: visible layer stack, reorder, opacity, blend mode, masks, adjustment layers, text layers, raster/photo layers, grouping, duplicate, delete, lock, and per-layer selection.
  - Goal: Layers become the home for compositing, masks, text, and adjustment layers.
  - Complete it by: defining a layer model with stable ids and ordering; adding visible stack UI; implementing selection, reorder, hide/show, opacity/blend, duplicate/delete, mask attachment, adjustment layers, text layers, and export parity.
  - Acceptance: every visible layer operation changes the preview and survives save/load.
  - Status: Completed. The Layers tab now exposes a stable visible stack with adjustment, fill, text, source/raster, and group layers; per-layer selection/transform handles; hide/show, solo/isolate, opacity, blend mode, clipping, mask attachment, lock, duplicate, reorder, delete, visibility states, text editing, and transform controls. Controller tests verify typed layer-composite render intents, preview updates, lock behavior, solo/isolation rendering, transform/bounds geometry, duplicate/reorder/delete, flattened export, and individual layer raster export. Recipe tests now round-trip a rich layer stack with text metadata, fill masks, transforms, ordering, and groups through save/load. Focused verification passed with `desktop_shell_smoke_test`, `editor_preview_controller_test`, `edit_recipe_state_test`, and `render_parity_test` in `build/dev-macos-debug`.

## Phase 5: Masks, Brush, Object Selection

- [x] **Mask/brush overlay is visually confusing**
  - Problem: diagonal hatching covers the image, making it hard to see brush strokes and edits. Users cannot tell what they painted.
  - Goal: Mask visualization is clear, controllable, and never blocks the photo while painting.
  - Complete it by: replacing heavy hatching with selectable overlay modes (`Color Overlay`, `Mask`, `Marching Ants`, `None`); showing live stroke color/opacity; adding an overlay opacity slider; hiding nonessential patterning during active paint.
  - Acceptance: while brushing, the new stroke is visible immediately and the image remains judgeable.
  - Status: Completed. Mask preview modes now include Overlay, Matte, On Light, On Dark, Outline, Edge, Affected, and None; `None` hides the Studio viewport mask overlay while keeping the active mask editable. The edit viewport also suppresses the full mask matte/geometric overlay during an active brush stroke in normal overlay mode, leaving the live brush stroke, pressure-aware HUD, and cursor visible over the photo. Focused verification passed with `desktop_shell_smoke_test` and `editor_preview_controller_test` in `build/dev-macos-debug`, including active brush stroke state, tablet pressure display, hidden full-mask matte while painting, and hidden mask overlay in `None`.

- [x] **Brush stroke model feels poor**
  - Problem: points are added directly on mouse move, with no visible smoothing/stabilization model in the UI. Strokes can feel jagged or uneven.
  - Goal: Brush behaves like a professional photo editor brush: smooth, pressure-aware, responsive, and predictable.
  - Complete it by: adding dab interpolation by spacing, stroke smoothing/stabilization, hardness/flow/opacity semantics, tablet pressure support, eraser symmetry, and brush cursor feedback.
  - Acceptance: fast curves are smooth, slow strokes do not blob unpredictably, and flow builds up consistently.
  - Status: Completed. Brush masks now use spacing-based dab interpolation, smoothing/stabilization samples that keep the live cursor endpoint, pressure-scaled size/flow, tilt-scaled size, tablet eraser intent, editable hardness/spacing/smoothing/buildup/auto-mask controls, live brush point exposure, and grouped undo per completed stroke. Focused verification passed with `editor_preview_controller_test` and `desktop_shell_smoke_test` in `build/dev-macos-debug`, including dense spacing, smoothing endpoint behavior, pressure/tilt/eraser storage, continuous raster masks, separated stroke gaps, brush HUD pressure display, and one undo entry per stroke.

- [x] **Brush rerenders too much while drawing**
  - Problem: pointer movement calls controller brush point methods and users see rerendering instead of a live paint overlay. Existing code has region/tile primitives, but the interactive path still feels full-frame.
  - Goal: drawing is immediate; expensive render work happens only for affected regions after or during throttled commits.
  - Complete it by: drawing the active stroke in a lightweight QML/GPU overlay first; committing stroke points to the mask texture separately; rendering only dirty tiles/brush bounds; uploading only dirty texture regions; throttling histogram/full preview until stroke end.
  - Acceptance: no full-frame flicker or processing badge per dab; the stroke appears under the cursor immediately; after mouse up only affected tiles update.
  - Status: Completed. Active strokes append to the transient live brush buffer and update the live QML/native brush preview while painting; the mask definition and render intent are committed on stroke end. Completed strokes submit `brush_stroke` render intents with `mask_rasterization` dirty regions and visible tile plans instead of full invalidation. Focused verification passed with `editor_preview_controller_test` and `desktop_shell_smoke_test` in `build/dev-macos-debug`, including active stroke state, live interpolated samples before commit, cleared transient buffers after commit, localized dirty regions, one planned visible tile, and no full invalidation for localized brush strokes.

- [x] **Object detection selects weird boxes**
  - Problem: object selection appears boxy or imprecise, which suggests either a fallback/stub path, coarse segmentation, or poor mask refinement.
  - Goal: object masking selects actual object silhouettes with refinement controls.
  - Complete it by: using a real segmentation model on supported systems; showing candidate masks on hover/click; refining with edge-aware matting, feather, contract/expand, add/subtract brush; surfacing unavailable/fallback states clearly.
  - Acceptance: clicking an object produces a mask that follows object edges, not rectangular blocks, and users can refine it.
  - Status: Completed. Smart selection uses Vision object masking on supported macOS systems, prompt-refined fallback masks otherwise, hover previews, candidate cycling, traced outlines, add/subtract object picks inside one mask set, drag-path quick selection with intermediate sampling, tiny-target refinement, and explicit missed-pick outcomes instead of polygon/brush box fallbacks. Focused verification passed with `editor_preview_controller_test` and `desktop_shell_smoke_test` in `build/dev-macos-debug`, including hover preview, candidate cycling, localized object masks, tiny-object prompt refinement, drag-path unions, subtract masks, full-set previews, traced outlines, and no implicit rectangular fallback on misses.

- [x] **Mask/layer editing context is disconnected**
  - Problem: while brushing, users cannot see enough layer/mask state or edits; the brush toolbar floats separately from the layer stack.
  - Goal: the active layer/mask and brush settings are always visible and editable.
  - Complete it by: keeping a compact Layers/Masks stack visible or docked during brush mode; showing active mask name, operation, overlay mode, brush mode, and local adjustments; adding quick hide/show mask preview.
  - Acceptance: a user can paint, change the active mask adjustment, switch add/erase, and see layer/mask state without leaving the canvas.
  - Status: Completed. Brush mode now keeps a canvas-side Layers/Masks context panel visible beside the live brush HUD, including the active layer/base target, active mask name, mask operation, overlay state, active mask chip/count, Add/Erase mode controls, quick mask preview toggle, and local mask Exposure/Temperature sliders. Focused verification passed with `desktop_shell_smoke_test` in `build/dev-macos-debug`, including preview hide/show, active mask local adjustment writes, add/erase state, layer/base label, active mask label/state, and reduced canvas chrome while painting.

## Phase 6: Scopes And Inspector Layout

- [x] **Scopes tab is not useful while editing**
  - Problem: scopes live in an inspector tab, so users cannot watch scopes while adjusting sliders.
  - Goal: scopes are dockable/pinnable and can stay visible during editing.
  - Complete it by: adding a pinned scopes drawer or bottom panel; allowing histogram/waveform/vectorscope tiles to stay open while Adjust tab is active; syncing scopes to current preview generation.
  - Acceptance: users can drag Exposure while waveform/histogram remains visible.
  - Status: Completed. Studio Adjust now exposes a stable inline histogram scope and waveform scope above the adjustment controls, while the dedicated Scopes tab still provides the larger scope grid and proofing controls. Focused verification passed with `desktop_shell_smoke_test` in `build/dev-macos-debug`, including an Exposure edit while both inline scopes remain visible.

- [x] **Inspector tab model needs cleanup**
  - Problem: Adjust, Layers, Scopes, Detail, Info do not match the actual work. Presets need a tab; Text should be part of Layers unless it becomes a full tool mode.
  - Goal: inspector tabs map to workflows, not implementation buckets.
  - Complete it by: using `Presets`, `Adjust`, `Masks`, `Layers`, `Scopes`, `Info` or a similarly coherent set; keeping Detail inside Adjust unless it is a dedicated 1:1 workflow.
  - Acceptance: users can predict where a tool lives and do not lose context switching tabs.
  - Status: Completed. The normal Studio inspector no longer exposes `Detail` as a workflow tab; Detail and Lens controls live inside Adjust and participate in the Adjust filter as a `detail` group. Presets, Adjust, Layers, Scopes, and Info remain stable, and Crop only appears while Crop owns the current edit mode. Focused verification passed with `desktop_shell_smoke_test` in `build/dev-macos-debug`.

## Phase 7: Thumbnails And Filmstrip

- [x] **RAW thumbnails do not load reliably**
  - Problem: thumbnail generation tries `decode_raw_preview_image` only for empty recipes, then falls back to `decode_source_image`. If RAW preview decode fails or cache state is stale, filmstrip tiles show empty placeholders.
  - Goal: every supported RAW gets a thumbnail or a clear error state.
  - Complete it by: logging RAW thumbnail decode failures; falling back from embedded RAW preview to draft RAW decode; invalidating bad cache entries; adding retry and visible loading/error states; adding RAW thumbnail tests with representative extensions.
  - Acceptance: supported RAW files populate thumbnails in library and filmstrip without manual refresh.
  - Status: Completed. Thumbnail generation now validates cached files before reuse, removes unreadable cache entries, logs embedded RAW preview failures, falls back to draft RAW decode, and produces a readable RAW error thumbnail instead of leaving the role empty when decode fails. `catalog_asset_model_test` covers a `.CR3` with a corrupt cached thumbnail and verifies the model publishes a readable regenerated thumbnail URL; `desktop_shell_smoke_test` also remains green in `build/dev-macos-debug`.

- [x] **Filmstrip thumbnails look worse than library thumbnails**
  - Problem: filmstrip cards add nested frames, margins, borders, and aspect-ratio badges that make thumbnails look boxed and cramped. The image is aspect-fit, but the chrome dominates and creates ugly edges.
  - Goal: filmstrip preserves image aspect ratio cleanly like the library, with minimal chrome.
  - Complete it by: removing nested heavy frames/margins; using a consistent thumbnail component for library and filmstrip; preserving aspect ratio; using subtle selection state; moving metadata badges outside the image or making them optional.
  - Acceptance: thumbnails look clean, uncropped unless requested, and preserve aspect ratio without thick borders or fake edge artifacts.
  - Status: Completed. Filmstrip thumbnails now render with no nested image margin, no in-image aspect badge by default, no unselected border chrome, `Image.PreserveAspectFit`, and only a subtle selected indicator. Focused verification passed with `desktop_shell_smoke_test` in `build/dev-macos-debug`.

- [x] **Edited thumbnail consistency**
  - Problem: thumbnails are generated with recipe-aware cache keys, but preset/recipe changes must invalidate and regenerate predictably.
  - Goal: library, filmstrip, preset cards, and export queue thumbnails all reflect the same current recipe state.
  - Complete it by: centralizing thumbnail cache keys, storing recipe hash/version, invalidating on edit save/preset apply, and adding tests for thumbnail changes after recipe changes.
  - Acceptance: applying a preset updates the selected asset thumbnail and does not leave stale cache images.
  - Status: Completed. Photo thumbnails use a recipe-aware cache key and immutable versioned local-file URLs, recipe/preset application clears the visible thumbnail role and queues a refresh, and stale in-flight thumbnail task results now rescan the queue after the active marker is cleared so a newer recipe cannot be left blank. `catalog_asset_model_test` verifies normal edited thumbnail regeneration, brighter rendered thumbnail pixels after a tone edit, current-recipe cache-path selection, and the active-task race where an edit is applied before the first background thumbnail finishes.

## Phase 8: Verification And Tooling

- [x] **Real-image QA corpus**
  - Problem: synthetic tests catch math regressions, but they do not prove the editor feels good on night RAW, skies, portraits, forests, drone footage, or high-contrast scenes.
  - Goal: a small licensed local corpus validates visual quality.
  - Complete it by: adding or documenting a non-public fixture corpus with dark RAW, clipped highlights, smooth sky gradients, skin, foliage, and textured detail; creating benchmark/golden scripts for before/after inspection.
  - Acceptance: every major tone/color/mask change is reviewed against the corpus before being called done.
  - Status: Completed. The checked-in corpus contract now includes a private manifest example with license evidence, a reproducible public seed manifest, and `fetch-real-image-corpus.py`, which materializes public licensed RAW/JPEG assets without committing image binaries. Local verification materialized `/private/tmp/photo-editor-real-image-corpus/manifest.json` with 5 assets covering `dark_raw`, `clipped_highlights`, `smooth_sky`, `skin`, `foliage`, and `textured_detail`; `check-real-image-corpus.py` passed without `--schema-only`. Real-image preview proof passed on artifact `/private/tmp/photo-editor-real-image-perf/20260614T204048.369522Z-perf-preview-raster-5827edf9` with 5/5 assets rendered, preview asset p95 13176.4 ms, histogram p95 130.2 ms, and corpus total 25696 ms. Real-image thumbnail proof passed on artifact `/private/tmp/photo-editor-real-image-perf/20260614T204145.627601Z-perf-thumbnail-cold-warm-354924c0` with cold 1432 ms, warm 99 ms, and `thumbnail.completed` p95 727.3 ms.

- [x] **Performance budgets**
  - Problem: the app has observability hooks, but completion needs explicit budgets.
  - Goal: brush, slider, histogram, thumbnail, and preview latency have measurable targets.
  - Complete it by: adding benchmarks and observability assertions for slider render latency, histogram latency, brush overlay latency, dirty-tile render size, and thumbnail generation time.
  - Acceptance: regressions fail tests or are visible in benchmark output before release.
  - Status: Completed. CI now runs explicit preview, thumbnail, brush/tile, color-adjustment, and histogram budget checks with `check-summary-thresholds.py`; the thumbnail benchmark emits cold/warm summary events and waits for warm thumbnail readiness; the plain preview benchmark now waits for histogram settlement so `preview.histogram.completed` is measured before exit. Local verification: `cmake --build --preset build-dev-macos-debug`; thumbnail cold/warm budget run passed with cold 1320 ms, warm 123 ms, and `thumbnail.completed` p95 692.3 ms; preview smoke emitted two histogram events and passed `preview.histogram.completed.elapsed_ms` p95 96.8 ms against the 5000 ms budget.

- [x] **UI visual regression coverage**
  - Problem: many failures are visual layout/interaction regressions that unit tests will not catch.
  - Goal: screenshot tests cover key editor states.
  - Complete it by: adding QML/desktop smoke screenshots for Adjust, Presets tab, Color Grading, Tone Curve/Levels, Scopes pinned, Layers, Brush mode, Object selection, Detail, and filmstrip.
  - Acceptance: screenshots prove controls are visible, sized correctly, not overlapping, and usable at desktop and compact widths.
  - Status: Completed. `desktop_shell_smoke_test` now captures visual proof PNGs for Adjust with filmstrip, Presets, Color Grading, Tone Curve/Input Levels, pinned Scopes, Layers, Brush mode, Object selection, and Detail controls at 1600x1000 and 1180x820, while requiring the relevant QML objects to be visible before each capture. CI exports the capture directory with `PHOTO_EDITOR_VISUAL_PROOF_DIR` and uploads it as `photo-editor-desktop-visual-proof`. Local verification passed with `PHOTO_EDITOR_VISUAL_PROOF_DIR=/private/tmp/photo-editor-visual-proof-check ctest --test-dir build/dev-macos-debug -R desktop_shell_smoke_test --output-on-failure`, producing 18 PNGs plus `manifest.json`.

## Recommended Execution Order

1. Fix split-toning value semantics, histogram precision/refresh, and preview bit-depth/dithering first. These affect trust in every tool.
2. Move Presets into a dedicated tab and simplify the default Adjust panel. This reduces UI noise while deeper work continues.
3. Redesign Color Grading, Levels/Tone Curve, and core tone sliders with real acceptance tests.
4. Rebuild masks/brush/object selection around live overlays and dirty-region rendering.
5. Finish Layers/Text/Detail as coherent workflows, not scattered controls.
6. Clean thumbnails/filmstrip and add real-image QA/performance gates.
