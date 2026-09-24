# Photoshop-style editing implementation progress

Objective: implement the entire [Photoshop-style editing plan](photoshop-style-editing-implementation-plan.md). This ledger records partial work; it does not replace or narrow that plan's acceptance requirements.

## 2026-09-12 — Canonical and desktop recipe integration

### Implemented

- Canonical `LayerRecipeV13` now owns typed effect instances. Recipes with effects serialize as `photo-editor-edit-recipe-v13.4`, including output color space; recipes without effects continue to emit their established v13.2/v13.3 bytes. Existing serialized identities are not reinterpreted.
- Canonical validation checks effect envelopes; serialization and layer-stage cache identities include their ordered parameters. Changing a blur invalidates the layer/downstream stages while preserving source-decode identity.
- Render compilation requires explicit executor algorithm admission and available effect mask sets. Unknown active algorithms cannot become renderable merely by being listed as capabilities. Disabled effects do not require unavailable executors/masks. Actual app executors have not opted in yet, preventing silent omission until pixel integration is implemented.
- Desktop layer maps carry the same envelope in `effectStack`, via `photo_layer_effect_codec.h`. The canonical adapter preserves it. Asset-backed canonical reopening checks stored layer effects against canonical effects and rejects mismatches.
- Desktop JSON containing effect stacks now declares **recipeVersion 14**. Ordinary effect-free JSON retains **version 12**. Automatic migration preserves the required version; explicit downgrade of an effect document to version 12 fails. Version 13 remains reserved for the separate canonical format. No database schema or catalog reset was performed.
- Added regression coverage to canonical, adapter, persistence and desktop-state tests. The standalone desktop-state test now links the engine codec it validates.

### Verified

```sh
cmake --build --preset build-ci-headless --target edit_recipe_v13_test photo_effect_gaussian_test photo_effect_codec_test -j 4
cmake --build --preset build-dev-macos-debug --target edit_recipe_state_test edit_recipe_v13_adapter_test edit_recipe_v13_persistence_test -j 4
ctest --preset test-ci-headless -R '^(photo_effect_(gaussian|codec)|edit_recipe_v13)_test$' --output-on-failure
ctest --test-dir build/dev-macos-debug -R '^edit_recipe_(state|v13_adapter|v13_persistence)_test$' --output-on-failure
```

After successful rebuilds, **6/6 tests passed**: three headless suites in 0.32 seconds and three desktop recipe suites in 1.22 seconds. The first desktop build exposed a missing test-target engine dependency; it was corrected before these accepted runs. Test execution against older binaries during that failed-build interval is not accepted evidence. Desktop CMake emitted missing optional Vulkan-header notices; the requested targets built successfully. Focused whitespace checks passed.

These checks cover serialized state, assets, cache identities and render admission. They do **not** prove a visible blur, actual exported pixels, healthy catalog save/relaunch, Metal dispatch, or Windows runtime. The engine-to-layer compositor and UI integrations remain the next work.

## 2026-09-12 — Effect engine and serialization foundations

This earlier checkpoint describes the state before the recipe integration above.

### Implemented

- `photo_effect_types.h`: stable effect IDs, algorithm version, amount, enabled state, input scope, mask reference, Gaussian parameters, and opaque preservation of future effect parameters.
- `photo_effects.h/.cpp`: finite-support separable Gaussian on linear RGBA; premultiplied convolution and mask/amount mixing; transparent, extend and reflect edges; independent X/Y render scale; support bounds; scratch-budget enforcement; cancellation/error without partial publication.
- Ordered transactional effect stacks, unique-ID validation, named mask bindings, accumulated support bounds and active-unknown-effect rejection. Disabled effects do not require missing mask dependencies.
- `photo_effect_codec.h/.cpp`: independent `photo-editor-effects:v1` envelope for subsequent recipe/preset integration. Stable double precision, locale independence, ordered entries, bounded decoding, validation, and exact opaque future-parameter preservation.
- Two focused CMake-registered native suites. Production engine target builds the new code.

### Verified in this worktree

Commands executed successfully:

```sh
cmake --build --preset build-ci-headless --target photo_effect_gaussian_test photo_effect_codec_test -j 4
ctest --preset test-ci-headless -R '^photo_effect_(gaussian|codec)_test$' --output-on-failure
```

Final focused result: **2/2 tests passed**, 0.47 seconds total. No compiler warnings were emitted for the new targets in the successful build output. Whitespace checks passed for the new files and modified build registrations.

`photo_effect_gaussian_test` compares the separable implementation against an independent direct 2D convolution, including masked partial amounts, negative/HDR values and alpha. It also covers constant-image extension/reflection, one-pixel images, hidden RGB in transparent pixels, impulse symmetry/energy, anisotropic support, padded-tile/full-frame equivalence for all three edge modes, exact identity, cancelled entry, invalid input, memory budget rejection, effect order, named masks, duplicate IDs and whole-stack rollback after a later missing dependency.

`photo_effect_codec_test` covers exact double/string round trips, Unicode/quotes/newlines, host locale changes, ordering, opaque unknown effects, disabled unknown rendering bypass versus active rejection, malformed/versioned/oversized envelopes, invalid parameters, duplicate IDs and empty stacks.

### Explicit limitations

- The engine and effect envelope are not connected to desktop recipe adaptation, layer compilation, export handoff, or UI yet. There is no newly available blur control in the app.
- Existing v12 desktop and canonical v13 document formats are unchanged. The independent effect envelope needs a versioned document integration before it can be saved by the application.
- No GPU implementation, real-image app proof, performance benchmark, export/relaunch proof or Windows host run has been completed for these additions.
- The caller must supply padded tiles and crop the output. Tile equivalence is numerically tested, but automatic tiled execution and retained stage caching are still required.
- The current CPU reference is exact finite-support convolution and can be expensive at large radius. Scratch limits reject oversized work rather than exceed the budget; callers still need the tile scheduler integration.
- Both layer-content and composite-below intent can be serialized, but the owning compositor must resolve that scope. The primitive receives already-selected input pixels.
- Text, styles, real groups, shapes, documents, packaging, painting and all later plan phases remain outstanding.

## Next implementation steps

1. Extend CPU layer/composite processing and runtime `PhotoLayer` with typed effects, resolved mask data, radius scale and error propagation. Canonical serialization/desktop versioning are now integrated; executor capability admission must remain disabled until actual pixel processing is connected.
2. Ensure active effects cannot be skipped by LUT/Metal fast paths. Resolve effect mask references and coordinate scaling through the shared pipeline compiler. Relevant paths: `compile_pipeline_photo_layers` in desktop `photo_render_utils.cpp`, `apply_photo_layers` / `apply_standard_photo_layers` in `color_layer_runtime.cpp`, `apply_color_layers_stage` in `color_local_layer_runtime.cpp`, and fill-layer/Metal admission code. Existing layer functions return void, so effect failures need explicit propagation rather than silent continuation.
3. Connect tiled execution and budget accounting, then controller commands, coalesced undo, persistence and the first Gaussian UI. No visible feature is complete until the whole preview/save/relaunch/export path passes.
4. Execute Phase 0 real-app baseline/text-worker checks and finish Phase 1 acceptance alongside these changes. Continue through every subsequent phase of the original plan.

## Full-plan status

Typography foundations: duplicate controller/shared text rasterizers now call
`photo_text_rasterizer.h`. It preserves legacy layout, accepts bounded requested
raster dimensions, and fails explicitly without a GUI font runtime. Desktop output
rejects missing text rasters. GUI tests pass visible glyphs, alignment, empty text,
multiline Unicode, doubled raster bounds, cold background-thread rendering matching
the GUI thread, and visible fallback for a nonexistent font. Installed-font selection
and missing-font warnings are connected to Studio; actual interaction is unverified.
Requested-resolution rasterization still needs compiler/export geometry integration.
Rich text content/versioning, on-canvas editing, style runs and full worker exports
remain unfinished; these foundations do not complete Phase 4.

Box blur now has a typed integer radius, versioned codec, CPU uniform separable
kernel, edge-mode-aware compositor, controller edits, registry/picker entry and
radius inspector. Tests pass impulse support, radius/edge serialization, negative
radius rejection, constant cutout extension, controller undo, and serialized
desktop output under legacy and ACES2 models. The latest rebuilt desktop
color-input suite passed in 2.27 seconds. Large-radius performance, tiled box
reference coverage, real-media and actual UI verification remain open.

Phase 2 has started with `invert_linear_rgb:1`: explicit linear-working-RGB
inversion, amount/mask blending, preserved alpha and unclamped HDR values.
Primitive and compositor tests pass, including mixed Gaussian/invert tile parity.
Desktop creation and generic amount commands, inspector controls and canonical
algorithm admission are connected. Controller undo tests pass. The rebuilt desktop
color-input suite now verifies serialized invert reverses an impulse's contrast
under both legacy and ACES2 output models. Browser/presets and the other essential
filters remain unfinished; no visual, Metal or real-media performance claim follows.

The rebuilt desktop color-input suite passes Gaussian output, active-unknown
rejection and disabled-unknown identity under both `legacy-oklch-v1` and
`aces2-ocio-v1`. A linear selection also compiles into a full-size, varying effect
coverage raster. These tests use synthetic CPU fixtures; they do not establish
hardware acceleration, real-media fidelity or queued-export execution.

Effect masks: selection assignment copies the active selection set under new IDs
in one history operation; clearing a mask detaches coverage and retains the saved
selection. Controller tests pass assignment, copy identity, clear, undo and redo.
The CPU compositor suite now also passes varying mask coverage with partial effect
amount, and rejects missing composite-mask data without publishing pixels. Desktop
selection-raster-to-effect numerical parity and actual mask interaction remain open.

Inspector follow-up: Gaussian edges are editable (transparent/extend/reflect),
effect rows collapse with one editor open, and unavailable effects can be disabled
without changing their opaque parameters. Controller regression coverage passes
edge-mode undo and unknown-effect bypass/preservation. Numeric fields restore saved
values after invalid focus loss and support Escape cancellation. Component qmllint
passes; these interaction changes still await actual UI proof. The unknown-effect
test fixture initially confused adjustment-patch JSON with full-recipe JSON; the
fixture now uses explicit layer state and the accepted rebuilt test passes.

Fixed a demonstrated zero-radius regression: the compositor previously entered
premultiplied effect mixing for an identity Gaussian and changed hidden RGB at
alpha zero. Active-effect detection now bypasses known version-1 zero-sigma
Gaussian instances. The new regression failed before the change and passes after
it. Rebuilt headless Gaussian, layer-alpha and layer-effect suites: 3/3 passed.

Desktop output integration now has focused evidence: rebuilt and passed
`photo_layer_color_input_test`. Its new case saves/reloads an adjustment-layer
Gaussian through desktop JSON, renders a synthetic impulse through
`render_output_image_with_backend` on CPU, and checks that output pixels spread.
It also verifies half-resolution compilation preserves the effect and scales
both blur axes by 0.5. This is synthetic CPU output evidence; it does not prove
queued worker export, real-photo appearance, Metal or actual app interaction.

Full desktop integration build passed: `cmake --build --preset build-dev-macos-debug
--target photo_editor_app -j 4`, including compiled Studio and PhotoEffectStack QML.
Build output is in `/tmp/photo-effects-desktop-build.log` (temporary local evidence).
Effect reordering and duplication also pass the controller history suite, including
payload preservation, new duplicate IDs and undo/redo. Visual verification was
attempted using the computer-use skill, but its bootstrap returned
`Computer Use could not load @oai/sky from the cua_node runtime` before app access.
No screenshot or actual interactive workflow has therefore been verified.

The Studio layer inspector now includes `PhotoEffectStack.qml`: Gaussian creation,
size, amount, bypass, reset and removal. The standalone component passes qmllint;
the full `photo_editor_app_qmllint` target completes with numerous app-wide warnings,
so this is not a warning-clean app claim. The rebuilt
`editor_preview_layer_mask_history_test` passes effect create/edit/remove undo/redo,
stable identity, invalid-input rejection and recipe reload into a fresh controller.
Actual catalog save/relaunch, exported pixels and visual interaction are still open.

Desktop controller commands now expose effect rows, add/update Gaussian blur by
stable effect ID, and remove an effect. They reject locked/incompatible layers,
nonfinite/out-of-range parameters, malformed stacks and edits to unknown operators.
New instances receive UUIDs and the appropriate layer/composite scope; mutations
use the existing layer stack change path. The desktop shell test runtime target
rebuilt successfully, including MOC and the controller. Command behavior, gesture
coalescing and actual UI workflows still require tests; this is compilation evidence.

The new `photo_layer_effect_test` was built with `build-ci-headless` and passed
through `test-ci-headless`. It exercises the production color-layer stage:
an otherwise neutral composite adjustment blurs an HDR/negative-color impulse
with alpha, a one-pixel cutout produces the same positioned halo outside its
original bounds, the retained program rejects the spatial effect, and unknown
algorithms, unresolved masks and incompatible scopes fail without publishing
partial pixels. The standalone Gaussian reference supplies expected pixels;
its separate suite checks the convolution against an independent reference.
This is focused CPU compositor evidence, not desktop or export workflow proof.

Latest focused verification: rebuilt `photo_effect_gaussian_test` and `photo_layer_alpha_test`
with `build-ci-headless`, then ran both through `test-ci-headless`; 2/2 passed.
The Gaussian suite now exercises the automatic tiled executor with two ordered
effects, varying mask coverage, all three edge modes, and transactional rejection
of missing masks and insufficient scratch budgets. Tile mask allocation no longer
creates an additional full temporary coverage buffer. This verifies those numerical
paths; compositor-specific effect output, UI, export/relaunch, performance and
hardware gates remain open.

| Phase | Status |
| --- | --- |
| 0 — Baseline and contracts | Source paths revalidated; runtime baseline and decision receipts incomplete |
| 1 — Effect framework and Gaussian | CPU compositor, persistence and initial stack UI exist; complete preview transactions, native workflow and performance gates remain open |
| 2 — Essential effects/browser | Gaussian, box and invert with basic picker exist; remaining operators, presets, previews and acceleration incomplete |
| 3 — Blur canvas controls | Not implemented |
| 4 — Everyday text tool | Versioned typography, inspector, draft canvas editing, persistence and background file export tested in part; creation gestures, rich ranges, layout fidelity and native proof incomplete |
| 5 — Styles/shapes/layout | Not implemented |
| 6 — Groups/documents | Not implemented |
| 7 — Portable projects | Not implemented |
| 8 — Advanced effects | Not implemented |
| 9 — Painting/paths/deformation | Not implemented |
| 10 — Interchange/professional features | Not implemented |

The goal remains active. None of the release checkpoints has been reached.

### Text output scaling and explicit content failures — 2026-09-12

Current source connects requested-resolution text rasterization to layer compilation,
including independent output-axis scaling and placement. Added explicit content
error propagation for text raster budget, allocation and font-runtime failures;
oversized output no longer uses an artificial 20000-pixel request to trigger an
indirect failure or misreports every failure as a missing font runtime.

Built `photo_layer_color_input_test` and `photo_text_rasterizer_test` with the
`build-dev-macos-debug` preset. CTest passed all four focused suites:
`photo_layer_text_scale_test`, `photo_layer_color_input_test`,
`photo_text_rasterizer_test`, and `photo_text_missing_runtime_test` (3.79 seconds).
The compilation fixture covers reduced, enlarged/nonuniform and oversized text
output. These are offscreen/source-pipeline checks, not actual-app visual proof
or queued-export-worker qualification. Rich canvas text editing and the broader
implementation plan remain incomplete; the goal remains active.

### Versioned typography style contract — 2026-09-12

Added `photo_text_style.h` as the initial opt-in typography contract: font family
and face, fractional document-pixel size, numeric weight, italic, underline,
tracking, proportional line height, baseline offset, and alpha-preserving color.
Its version-1 JSON decoder rejects unsupported versions/fields, missing or
incorrectly typed values, fractional weights, and out-of-range parameters.
Legacy text interpretation remains unchanged. This model is not yet connected
to document persistence, rich text layout, controller commands or visible controls.

Built and passed `photo_text_style_test` using `build-dev-macos-debug` and CTest
(0.23 seconds). The test exercises serialized round-trip including Unicode font
names, fractional metrics and translucent color, plus explicit invalid/future
payload failures. This establishes a contract, not completion of the text tool.

### Typography layout rendering — 2026-09-12

Added the opt-in `photo_text_layout.h` renderer using the versioned style model
and QTextLayout. It supports fractional document-pixel font sizes, face/weight,
italic/underline, tracking, proportional line spacing, baseline offsets,
paragraph wrapping, alignment, padding and overflow reporting. Output resolution
scales raster sampling while retaining logical wrapping. Color alpha is applied
once to the complete text raster to avoid increased opacity at glyph/underline
overlaps; a failing test exposed and verified this correction.

The rebuilt `photo_text_layout_test` and `photo_text_style_test` passed (0.27s).
Checks cover changed decorated pixels, wrapping stability at 2x output, spacing,
overflow, translucent alpha and oversized-raster rejection. This is a new service
awaiting document/controller integration; legacy text remains on its existing
renderer. No native UI or real queued-export proof is claimed.

### Opt-in typography persistence and renderer routing — 2026-09-12

Shared text-layer preparation now routes an explicit `textTypography` payload to
the versioned renderer. Absent payloads retain legacy layout; unsupported or
malformed styles fail rendering explicitly. Desktop JSON emits recipe version 15
when typography exists, including nested layer discovery. Automatic migration
preserves version 15 and explicit downgrade below 15 is rejected. Envelope
validation checks the style version; complete style validation occurs in the
renderer. Canonical asset/queued-worker qualification and UI commands remain open.

Rebuilt and passed `edit_recipe_state_test`, `photo_text_rasterizer_test`, and
`photo_text_missing_runtime_test` (1.36s). Evidence covers desktop payload
round-trip/version selection/downgrade rejection and actual glyph raster routing
with unsupported-style failure. No native UI completion is claimed.

### Typography controller command — 2026-09-12

Added `photoLayerTypography` and `setPhotoLayerTypography` for validated partial
style updates through existing layer history/persistence/render intents. Legacy
values seed the new style only on an explicit typography edit. Invalid parameters
and locked/non-text targets are rejected. Existing text-style commands synchronize
the versioned payload after an upgrade. The rebuilt
`editor_preview_coalesced_undo_test` passed (1.60s), covering fractional typography,
invalid-edit transactionality and undo back to the exact legacy layer. Visible
advanced typography controls and native workflow proof remain to be implemented.

### Typography inspector controls — 2026-09-12

Added `PhotoTypographyPanel.qml` to the Studio text inspector with a collapsed
Typography section, explicit paragraph-layout upgrade, italic/underline toggles,
fractional size, tracking, line height and baseline shift. Numeric fields validate
ranges and restore saved values on invalid input or Escape. Locked layers disable
editing. The upgrade explains its layout impact and uses the existing undo command.
Fixed legacy alignment/style updates to retain untouched fractional metrics,
numeric weight and color alpha from the versioned payload.

Direct qmllint on the new component passed without output. The native
`photo_editor_app` build and rebuilt `editor_preview_coalesced_undo_test` passed;
the latter includes alignment preserving exact typography (1.60s). Actual native
interaction/screenshot proof remains open, as do canvas typing, rich ranges,
canonical/worker persistence qualification and the other plan phases.

### Styled-text output-path evidence — 2026-09-12

Extended the GUI-enabled layer/output fixture with versioned italic/underlined
text, fractional size, tracking and translucent color. The test serializes and
reloads the recipe, compiles at 2x dimensions, checks placement and glyph alpha,
and renders through the actual CPU output entry point. Styled output differs
from the plain canvas and matches the reloaded recipe exactly. An unsupported
active typography version refuses output rather than silently rendering legacy
text. Rebuilt `photo_layer_color_input_test`; both color-input and text-scale
CTest entries passed (3.39s). This is offscreen CPU output evidence, not native
interaction, real RAW, canonical asset persistence or queued-worker proof.

### Canonical typography asset persistence — 2026-09-12

Added a styled Unicode text fixture to `edit_recipe_v13_persistence_test`.
Asset-backed canonical save/projection preserves the exact typography payload,
including fractional metrics and alpha color, and its desktop projection emits
version 15. Changing tracking changes canonical recipe/content identity, while
reopening the earlier snapshot still returns its original content. The rebuilt
focused test passed (0.40s). Existing generic layer-content storage supports this
without a new asset format. This verifies persistence/state identity, not rendered
cross-machine font equivalence or real queued-export-worker behavior.

### Styled text background file export — 2026-09-12

Verified the direct `startFlattenedPhotoExport` worker path with styled text in
`photo_export_recovery_test`: synchronous reference and background-written PNG
reopen with identical pixels/profiles, even when text changes immediately after
export admission. A subsequent export of the changed caption differs, guarding
against matching empty-text outputs. This uses the production QtConcurrent
producer, captured recipe, encoder and file write within QGuiApplication. The
rebuilt test passed (1.30s). Catalog-backed queue/relaunch handoff, Windows font
runtime and native visual inspection remain separate open gates.

### Paragraph overflow feedback — 2026-09-12

Added `photoLayerTextLayoutInfo` and an inspector overflow/error message for
versioned text. Layout diagnostics use the same logical dimensions and font
layout as output with a one-pixel raster target, avoiding a full layer allocation
for inspector feedback. Diagnostics are requested only while the Typography
section is expanded. The text-layout test verifies identical line count, height
and overflow for diagnostic and full raster sizes. Rebuilt layout/controller
suites passed (1.84s), and direct component qmllint passed. Native visible-layout
proof remains open; these checks do not certify interaction latency on long text.

### Initial canvas text draft editor — 2026-09-12

Added `PhotoTextOverlay.qml`, opened from Edit on canvas or double-clicking the
selected layer transform frame. It uses the existing frame position/scale/rotation
and a plain-text draft with selection, multiline input, Done/Cancel, Escape and
command/control Return. Done sends one text command; cancellation never changes
the recipe. Layer/tool changes, locking or external text changes discard the draft.
The focused field claims shortcut overrides to prevent workspace tool shortcuts
from intercepting typing. Transform interaction is disabled during editing.

The initial editing surface has an opaque background and simplified font layout;
it does not yet match all final typography metrics, photographic color or complex
transforms exactly. Click/drag creation, point text, rich ranges, outside-click
focus policy, IME/native keyboard proof and polished visual behavior remain open.

The native app build and direct component qmllint passed. A real QQmlComponent
harness (`photo_text_overlay_test`) passed (0.19s), checking cancellation, one
Unicode/multiline commit and target-change cancellation. This harness is state
proof, not a screenshot or pointer/keyboard/IME workflow proof.

### Canvas keyboard and composition event tests — 2026-09-12

Extended the canvas editor harness with an offscreen QQuickWindow and dispatched
key/input-method events. It verifies a tool shortcut is active outside editing
but suppressed during typing, selection replacement, local text undo, Escape
without commit, and composed Japanese text saved once by Control+Return. The
rebuilt `photo_text_overlay_test` passed (0.37s). These are Qt event-path tests;
they do not establish native macOS/Windows IME candidate-window behavior, actual
pointer interaction or visual layout fidelity.

### Canvas draft scrolling and outside-click commit — 2026-09-12

Added a clipped Flickable editor viewport with vertical scrollbar and caret
visibility tracking, allowing editing beyond the text box height. Clicking the
canvas outside the editor commits once; Done/outside commit requests input-method
composition completion first. Escape continues to discard. The expanded offscreen
window test dispatches a real Qt mouse press/release and checks long-draft caret
scrolling; it passed (0.45s). Direct qmllint and the native app build passed.

Retried Computer Use through the prescribed plugin wrapper; it still fails with
`Computer Use could not load @oai/sky from the cua_node runtime`. Native visual
inspection remains unavailable. Qt event tests do not replace that gate.

### Installed font face selection — 2026-09-12

Added installed-face enumeration and an Automatic/named-face selector in the
Typography panel. Selecting a face derives its weight/italic attributes from
QFontDatabase; unavailable selections are rejected transactionally. Changing
family clears a previous face unless explicitly supplied, and manual weight or
italic changes clear the named face so the explicit attributes take effect.
Legacy family/weight controls clear conflicting face selection too. Canvas drafts
now use the selected face and numeric weight.

Native app build and both modified-component qmllint checks passed. The rebuilt
export fixture checks installed face selection, invalid-face transactionality and
explicit italic override; export/overlay tests passed (1.66s). Native appearance
and cross-machine missing-face handling remain open. Updated the phase table to
reflect partial filters and text implementation without closing release gates.

### Document-positioned paragraph creation command — 2026-09-12

Added `addParagraphTextLayer`, creating a versioned paragraph with an explicit
position, box dimensions and document canvas dimensions in a single history
operation. It uses a fresh UUID, preserves intentionally empty text and rejects
invalid geometry before mutation. This is the controller foundation for canvas
creation gestures; those gestures are not wired yet. The rebuilt
`editor_preview_coalesced_undo_test` passed (2.12s), checking stored geometry,
empty draft preservation, invalid input and one-step undo of the entire layer.

### Paragraph creation gestures — 2026-09-12

Added Draw Text Box to the layer add menu and a canvas creation overlay. Dragging
in either direction defines a normalized document-pixel box; clicking creates a
512x128 paragraph box. Successful creation selects the new layer and opens its
canvas draft editor. Leaving the creation tool cancels unfinished dragging. This
is paragraph creation, not point-text layout. Cancelling the subsequent draft
currently leaves its newly created empty layer, removable through undo.

Native app build and direct new-component qmllint passed. The rebuilt offscreen
window test passed (0.48s), covering reverse drag at a 0.5 display scale, default
click creation and interrupted drag. Actual native placement under crop/rotation,
creation-plus-edit transaction refinement and dedicated Text tool-rail/shortcut
integration remain open.

### Text tool discovery — 2026-09-12

Added a Text entry and proportional text icon to the photo tool rail, with
paragraph creation lifecycle/status and layers-inspector routing. The T shortcut
requires an active photo workspace, an asset, no focused text input, no active
canvas draft and no brush stroke. Draw Text Box now uses the same tool activation
path as the rail and shortcut. Native app build and icon qmllint passed; the
existing canvas event regression test passed (0.23s). That test covers editor
shortcut ownership, not a complete Studio rail/T-key interaction. Native discovery
and layout proof remain open.

### Canvas draft save failure recovery — 2026-09-12

Fixed canvas Done closing the editor even when the controller rejects the edit.
A failed changed-text commit now retains the draft/focus and shows a retry message.
Unchanged text closes without an unnecessary command. Repeated activation cannot
reset an existing draft, and invalid targets cannot begin editing. Expanded tests
cover rejected commit/retry, repeated activation and external revision cancellation.
The rebuilt overlay test passed (0.42s), component qmllint and native build passed.
This establishes state recovery behavior, not native error-message visual proof.

### Transactional paragraph creation — 2026-09-12

Changed canvas creation to hold box geometry and text locally until Done. A
successful commit creates the paragraph and its text through one controller
command; Cancel creates no empty layer and no history entry. Rejected creation
retains the draft for retry. Geometry follows the viewport during the draft;
leaving the Text tool discards unfinished creation. Existing-layer editing keeps
its previous update path. This supersedes the earlier empty-layer cancellation
limitation.

Native build and component qmllint passed. The rebuilt overlay test passed
(0.43s), covering no creation on Cancel, failed creation/retry and exactly one
successful creation with the final caption. Existing controller tests establish
single-command undo; full native creation/undo interaction remains unverified.

### Document-bound canvas drafts — 2026-09-12

Canvas drafts now capture document identity, check it before commit and cancel on
identity changes. The controller assetChanged signal also cancels drafts on
reload/local-file switches even if an identifier is reused or empty. Both new-box
and existing-layer drafts are covered. The rebuilt overlay test passed (0.41s),
including same-layer-id document changes and explicit asset reload notification;
component qmllint and native app build passed. Native document-switch interaction
proof remains open.

### Unicode paragraph layout — 2026-09-12

A new regression fixture exposed inconsistent Unicode paragraph-separator
handling. The versioned renderer now normalizes U+2029 before paragraph layout,
preserving blank and trailing paragraphs like LF, CRLF and CR. The rebuilt layout
test passed (0.29s), requiring identical pixels for those equivalent separators.
A mixed Arabic/Latin/CJK/combining-mark smoke fixture also checks visible output
and stable line metrics at doubled raster size. It does not establish native IME,
font fallback quality or reference-grade multilingual shaping fidelity.

### Higher-precision typography raster — 2026-09-12

Versioned typography now uses an explicitly sRGB-tagged RGBA64 intermediate
before float working-space conversion. Legacy text remains unchanged. The raster
pixel limit is 32M at eight bytes/pixel, retaining a 256 MiB raster allocation cap
(not a claim about total pipeline memory). The coverage regression requires
translucent edge alpha values that cannot be represented on an 8-bit grid.
Rebuilt layout and layer-output targets; all three layout/color-input/text-scale
CTest entries passed (3.74s). This does not add HDR or wide-gamut text color
entry, remove the existing color model's 8-bit serialized swatches, or establish
native/cross-backend visual parity.

### Sixteen-bit PNG export confirmation — 2026-09-12

Rebuilt the real background export fixture against the RGBA64 typography change.
Added checks of the written PNG IHDR bit depth and reopened QImage channel depth;
both establish sixteen-bit encoding rather than an eight-bit PNG expanded only
on read. Reference/background pixel equality and captured-caption isolation still
pass. `photo_export_recovery_test` passed (1.38s). This fixture does not qualify
other encoders, RAW/HDR media or the durable catalog job queue.

### Known invert effect codec validation — 2026-09-12

Closed a persistence validation gap: invert_linear_rgb version 1 now runs its
operator validation during serialization/deserialization, including disabled
instances. Unexpected opaque settings are rejected before rendering. Future
algorithm versions still round-trip opaque settings unchanged. Rebuilt the
headless effect codec target; its valid/malformed/future-version tests passed
(0.32s). No new filter UI or algorithm is claimed by this fix.

### Canonical zero-radius effect identity — 2026-09-12

Restored canonical zero serialization for Gaussian radius after the shared box
codec changes: +0 and -0 now produce identical effect payloads, matching their
identical rendering semantics and avoiding needless cache/snapshot differences.
The rebuilt headless codec regression test passed (0.22s), including exact
positive/negative zero identity. Broader effect implementation remains incomplete.

### Independent masks for duplicated effects — 2026-09-12

Duplicating a masked effect now copies its selection set and every component
with fresh IDs instead of sharing the original mask reference. Effect and mask
creation share a single history operation; unresolved mask references reject the
copy before mutation. Unmasked duplication retains its existing path. Rebuilt
`editor_preview_layer_mask_history_test`; it passed (1.11s), checking independent
mask identity, fresh component IDs, component count and exact undo of both stacks.
Native editing of the two masks independently remains an open visual/workflow gate.

### Duplicated effect mask reopen/edit independence — 2026-09-12

Extended controller coverage to serialize duplicated masked effects, reopen them
in a fresh controller, and edit the duplicate mask's opacity. Exact effect/mask
payloads survive reopening, the opacity edit takes effect, and every original
mask component remains unchanged. The rebuilt focused test passed (1.05s). This
is desktop persistence/controller proof, not native visual or rendered-mask parity.

### Posterize CPU reference and typed codec — 2026-09-12

Added posterize_linear_rgb version 1 with typed 2–256 levels, nearest-grid
quantization per linear working RGB channel (half ties away from zero), and
extended-grid negative/HDR behavior without clipping. It shares transactional
point-effect execution, amount/mask mixing and alpha-zero preservation. Codec
round-trip, equality and invalid-level rejection are implemented. Rebuilt
posterize/Gaussian/codec headless suites all passed (0.65s), including exact
boundary/HDR fixtures and partial coverage. Registry admission, desktop commands,
controls, tiled/mixed-stack output and native proof remain to be connected.

### Posterize desktop stack integration — 2026-09-12

Registered posterize_linear_rgb v1 and admitted it in desktop canonical render
planning. Added the validated add/edit controller command, Levels row, effect
browser entry/title and four-level reset behavior. Generic amount, bypass, masks,
duplication and history remain shared with the stack. Native app build and direct
stack-component qmllint passed. The rebuilt layer/history test passed (1.64s),
covering creation, invalid levels, reset and exact undo. Mixed/tiled rendered
posterize output and actual native interaction still require proof.

### Posterize mixed-stack/tile proof — 2026-09-12

Extended the posterize reference suite with a 37x29 HDR/negative/translucent
fixture and fractional mask coverage. Gaussian followed by posterize agrees
between full-frame and 8-pixel tiled execution within 1e-6 per channel across
Transparent/Extend/Reflect edges. Reversing the operators changes pixels, and an
invalid later posterize leaves the entire input unchanged. The rebuilt headless
test passed (0.28s). Desktop output/reopen and native visual proof remain open.

### Posterize desktop output/reopen — 2026-09-12

Added a two-axis color-gradient fixture through the desktop CPU output entry
point under legacy and ACES2 photographic models. Posterize measurably changes
output, JSON recipe reopening produces identical pixels, and bypass exactly
restores the unfiltered result. The rebuilt `photo_layer_color_input_test` passed
(1.79s). Real image/native presentation, accelerated execution and durable export
queue proof remain separate open gates.

### Posterize canonical persistence identity — 2026-09-12

Added posterize to asset-backed canonical persistence coverage. Seven-level
settings reopen exactly; changing to nine levels changes both canonical recipe
payload and cache key. The rebuilt `edit_recipe_v13_persistence_test` passed
(0.43s). This verifies canonical state/hash handling, not native UI, GPU output
or durable catalog export execution.
