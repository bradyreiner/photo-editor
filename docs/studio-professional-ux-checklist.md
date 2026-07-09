# Studio Professional UX Checklist

This checklist tracks the Studio cleanup needed to make the editing workspace feel like professional creative software: calm, organized, animated, fast to scan, and powerful without duplicate chrome.

The goal is not to remove capability. The goal is to make each capability easier to find, easier to switch to, and clearer once active.

## Studio Surface Ownership Contract

- Left tool rail owns tool discovery and top-level tool switching.
- Canvas owns the image, direct-manipulation overlays, and one active tool command bar.
- Canvas HUD/command bar owns immediate tool mode, live refine controls, one primary next action, and More.
- Right inspector owns detailed controls for the current tool, target, or selected object.
- Layers owns stack structure and target selection; layer properties and secondary actions belong in properties or menus.
- Bottom filmstrip owns asset navigation only.
- Diagnostics own render/backend/provider wording; primary Studio UI uses product language.

## Product Standard

- [x] Studio has one clear active editing context at a time.
- [x] The user can tell what they are editing within one second.
- [x] The user can tell what the next click or drag will do within one second.
- [x] Every tool has one primary command surface.
- [x] Detailed controls live in the inspector, not duplicated in floating panels.
- [x] Layers show structure; they do not become a dense command dashboard.
- [x] Destructive actions are separated from common editing actions.
- [x] Diagnostics and backend implementation details are hidden unless diagnostics are explicitly enabled.
- [x] Tool switching feels smooth and intentional, with no layout jumps.
- [x] The Studio UI passes a narrow-width screenshot check without clipped or overlapping controls.

## Information Architecture

- [x] Define the permanent Studio zones: left tool rail, canvas, canvas HUD, right inspector, bottom filmstrip, optional Layers focus.
- [x] Define which zone owns active tool state.
- [x] Define which zone owns active target state.
- [x] Define which zone owns layer and selection structure.
- [x] Define which zone owns precision controls.
- [x] Remove duplicate active target/status text between the canvas HUD, right inspector, and Layers panel.
- [x] Keep only one visible breadcrumb for the active target.
- [x] Replace competing cards/strips/chips with one compact active context treatment.
- [x] Use consistent names for the same concept across every Studio surface.
- [x] Write a short Studio surface ownership contract in this document or a linked design doc.

## Navigation And Tool Discovery

- [x] Reorganize the left rail into clear groups: Adjust, Selection, Brush, Retouch, Crop, Layers, View/Info.
- [x] Make tool rail buttons stable size with no hover-driven layout shifts.
- [x] Add expandable tool groups for variants without exposing every variant as top-level chrome.
- [x] Add clear active, hover, disabled, and focus states for every rail item.
- [x] Add concise tooltips for icon-only rail buttons.
- [x] Make switching tools preserve the right target where appropriate.
- [x] Make switching tools exit incompatible transient states safely.
- [x] Add a visible transition when the active tool changes.
- [x] Ensure keyboard shortcuts match the visible tool model.
- [x] Add smoke coverage that each primary Studio tool can be activated from the rail.

## Shared Tool Command Bar

- [x] Create one reusable compact command bar pattern for active tools.
- [x] Command bar shows tool name, mode, one important live control, primary action, and More menu.
- [x] Command bar height is fixed and cannot wrap into clipped rows.
- [x] Command bar uses adaptive menus at narrow widths.
- [x] Command bar appears in a consistent canvas location.
- [x] Command bar avoids covering important image content where possible.
- [x] Command bar uses one primary action at a time.
- [x] Secondary actions move to More, a mode menu, or the inspector.
- [x] Done, Apply, Cancel, and Close have consistent meanings across tools.
- [x] Add screenshot proof that command bars do not overlap or clip at supported widths.

## Right Inspector

- [x] Inspector shows controls for the current tool or selected object only.
- [x] Inspector does not repeat active target text already shown in the command bar.
- [x] Inspector sections are ordered by photographer workflow, not implementation order.
- [x] Inspector section headers are compact and not styled like nested cards.
- [x] Search/filter stays available for long adjustment lists.
- [x] Empty inspector states explain what to select without becoming a tutorial wall.
- [x] Inspector preserves scroll position only when it helps the current workflow.
- [x] Inspector resets irrelevant transient state when switching tools.
- [x] Advanced or experimental controls move behind disclosure.
- [x] Diagnostics show only when developer diagnostics are enabled.

## Canvas And Overlays

- [x] Canvas remains visually dominant in Studio.
- [x] Tool overlays never make the photo unreadable by default.
- [x] Selection, brush, crop, retouch, and transform overlays use a consistent visual language.
- [x] Active overlay color is readable on bright snow and dark clothing.
- [x] Overlay opacity and outline weight are restrained but visible.
- [x] Floating panels do not sit on top of important canvas work unless intentionally opened.
- [x] Canvas view controls are separated from editing tool controls.
- [x] Zoom, pan, fit, compare, and overlays have stable positions.
- [x] Canvas feedback starts immediately on pointer down for brush, lasso, crop, and retouch.
- [x] Add screenshot/pixel checks proving active overlays are visible and non-destructive.

## Brush

- [x] Remove or hide the floating BrushContextPanel by default.
- [x] Keep Brush HUD to mode, size, quick tuning, and Done.
- [x] Move density, opacity, smoothing, spacing, pressure, and advanced tuning into inspector or More.
- [x] Keep Add and Erase as clear brush modes, not duplicate toolbar controls.
- [x] Show brush size and hardness directly on canvas through the cursor.
- [x] Keep brush overlay visible while painting.
- [x] Make Brush Done return to the correct selection/adjustment workflow without losing context.
- [x] Make Esc behavior predictable for active brush strokes.
- [x] Keep brush-related layer/selection status in one place.
- [x] Add UI coverage for brush enter, paint, erase, tune, done, and cancel.

## Selections

- [x] Use "Selections" as the primary user-facing term.
- [x] Hide "mask" terminology from primary UI unless the control is truly about a layer mask.
- [x] Merge duplicate selection source controls between the top toolbar and left rail.
- [x] Keep selection source creation in the left rail or a single menu, not both.
- [x] Keep Add, Subtract, Intersect, and Replace as a compact combine control.
- [x] Show the affected target layer once.
- [x] Show smart/object selection progress as nonblocking canvas feedback.
- [x] Make new selection, edit selection, refine selection, and link-to-layer workflows distinct.
- [x] Move stale/empty/retry actions out of the primary toolbar unless they are blocking.
- [x] Add smoke coverage proving only one selection creation surface is visible at a time.

## Retouch

- [x] Rebuild Retouch HUD around one compact row.
- [x] Treat Remove, Heal, Patch, and Clone as modes in one mode menu.
- [x] Treat Add/Subtract as refine operations for Remove, not peer retouch modes.
- [x] Resolve Apply vs Done so the user sees one primary next action.
- [x] Move Regenerate, Finalize, Before/After, Reset, Reopen, and Delete into More or inspector groups.
- [x] Move patch source, clone source, heal blend, and variation controls into mode-specific inspector sections.
- [x] Hide CPU fallback/backend text from primary UI.
- [x] Keep retouch failure/retry states visible only when relevant.
- [x] Add animation for retouch mode changes.
- [x] Add screenshot coverage that Retouch HUD never wraps, clips, or overlaps the right inspector.

## Layers

- [x] Make Layers panel structure-first: rows select targets, inspector edits properties.
- [x] Keep layer rows fixed height.
- [x] Prevent row labels and buttons from overlapping.
- [x] Show row identity, thumbnail/icon, visibility, and More by default.
- [x] Move Edit Layer, Edit Mask, Delete, Move, Duplicate, Lock, and precision controls into More or properties.
- [x] Keep destructive layer actions out of default row chrome.
- [x] Make Base Image visibly foundational and locked, not visually equal to editable layers.
- [x] Rename "unlinked selection" to a user-facing concept or tuck it under a clear secondary group.
- [x] Show linked selections under their layer only when it improves structure scanning.
- [x] Add screenshot coverage for the Layers panel at normal and narrow inspector widths.

## Active Target And Breadcrumbs

- [x] Keep one compact active target display.
- [x] Remove repeated "Editing Adjustment Layer..." surfaces.
- [x] Shorten breadcrumbs to user-facing names.
- [x] Avoid showing full implementation paths in visible UI.
- [x] Make Base, active layer, and active selection states visually distinct.
- [x] Keep the Base return action available but quiet.
- [x] Move target deletion to Layers/properties, not the target header.
- [x] Make active target updates animated but subtle.
- [x] Ensure target switching does not reset unrelated tool state.
- [x] Add deterministic state signatures for active target display rules.

## Copy And Vocabulary

- [x] Replace visible implementation terms with product terms.
- [x] Hide "deterministic", "full-res", "alpha provider", "fallback", and render-stage terms from primary UI.
- [x] Use "Selection" for local edit regions.
- [x] Use "Layer" for stack objects.
- [x] Use "Mask" only for explicit layer-mask controls.
- [x] Use "Apply" only when generating or committing a result.
- [x] Use "Done" only when exiting a tool state.
- [x] Use "More" consistently for secondary actions.
- [x] Shorten all visible labels enough to fit compact layouts.
- [x] Add tooltip text for the full explanation when labels are shortened.

## Motion And Interaction Polish

- [x] Add 120-180ms transitions for tool HUD changes.
- [x] Add panel slide/fade transitions for inspector mode changes.
- [x] Add subtle row reveal animations for Layers and selection groups.
- [x] Add active target transition when switching base/layer/selection.
- [x] Add hover transitions without changing layout dimensions.
- [x] Add loading/progress animation for smart selection and retouch operations.
- [x] Add disabled-state transitions that do not feel broken or frozen.
- [x] Keep animations interruptible during rapid tool switching.
- [x] Respect reduced-motion settings if exposed by Qt/platform state.
- [x] Add screenshot/video smoke proof for key transitions where feasible.

## Visual Design System

- [x] Reduce accent borders across Studio.
- [x] Use accent color only for active state, primary action, or live edit feedback.
- [x] Normalize row heights, toolbar heights, and panel spacing.
- [x] Reduce nested card styling inside inspector panels.
- [x] Use fills and spacing before borders for grouping.
- [x] Make icon-only buttons consistent in size and hit target.
- [x] Make text hierarchy consistent in panels, rows, HUDs, and menus.
- [x] Prevent long labels from shrinking below professional readability.
- [x] Replace heavy status chips with quiet labels or tooltips where possible.
- [x] Run a visual audit for one-note color palette or excessive teal accents.

## Workflow Rules

- [x] Every tool has clear start, active, commit, cancel, and failure states.
- [x] Every tool can be exited with Esc or a visible control.
- [x] Every continuous edit groups undo correctly.
- [x] Every destructive action requires deliberate selection from More/properties or confirmation where needed.
- [x] Every disabled action explains why in a tooltip.
- [x] Tool switching never strands the user in a hidden active state.
- [x] Starting a new local edit selects or creates the correct adjustment layer.
- [x] Base/global edits are visually distinct from layer-local edits.
- [x] Before/after never mutates recipe state.
- [x] Reset actions affect only the intended scope.

## Performance And Responsiveness

- [x] Tool switching stays visually responsive on first click.
- [x] Brush feedback updates immediately before full preview settles.
- [x] Selection hover/preview requests are debounced and cancel stale results.
- [x] Retouch operations show nonblocking progress.
- [x] Inspector changes do not block canvas pointer feedback.
- [x] Layer row hover does not instantiate heavy controls.
- [x] Thumbnail refresh remains async and nonblocking.
- [x] Animations do not compete with render work.
- [x] Add timing probes for Studio tool-switch latency.
- [x] Add a smoke/perf gate for first interaction responsiveness.

## Verification

- [x] Add or update QML object names for the new ownership rules.
- [x] Add deterministic signatures for visible tool surface counts.
- [x] Add smoke coverage proving no duplicate active context surfaces.
- [x] Add smoke coverage proving Retouch HUD is one compact command surface.
- [x] Add smoke coverage proving Brush has no default duplicate context panel.
- [x] Add smoke coverage proving Selection creation has one visible entry surface.
- [x] Add smoke coverage proving Layers rows do not overlap at supported widths.
- [x] Add screenshot checks for desktop and narrow layouts.
- [x] Run `qmllint` on changed QML.
- [x] Run the focused desktop smoke/build target after each implementation pass.

## Suggested Implementation Order

1. [x] Write the Studio surface ownership contract.
2. [x] Build the reusable compact tool command bar.
3. [x] Rebuild Retouch HUD first because it is visibly broken.
4. [x] Remove the default Brush duplicate context panel.
5. [x] Consolidate Selection toolbar and rail duplication.
6. [x] Simplify active target and breadcrumb display.
7. [x] Simplify Layers rows and move actions to More/properties.
8. [x] Clean right inspector duplication and ordering.
9. [x] Add the animation pass.
10. [x] Add screenshot, smoke, and responsiveness proof.

## Progress Evidence

- 2026-07-07: Added the Studio surface ownership contract to this checklist so tool discovery, active command state, detailed controls, Layers structure, filmstrip navigation, and diagnostics each have a defined owner.
- 2026-07-07: Added `apps/desktop/qml/components/ToolCommandBar.qml` and packaged it through `apps/desktop/CMakeLists.txt`. The component provides a fixed 44px command bar with tool label, mode menu, bounded inline actions, one primary action, and a More menu.
- 2026-07-07: Rebuilt the Retouch canvas HUD in `StudioWorkspace.qml` to use `ToolCommandBar`. Remove/Heal/Patch/Clone are mode-menu entries, Remove Add/Subtract are inline refine actions only in Remove mode, Apply and Done collapse into one primary action based on committed state, and secondary retouch actions move to More.
- 2026-07-07: Removed visible retouch backend/capability text from the Retouch HUD status. The visible status now shows user workflow state such as `Heal / Paint area / After`; backend/fallback state remains available through diagnostic signatures.
- 2026-07-07 validation: `qmllint -I apps/desktop/qml -I build/dev-macos-debug/apps/desktop apps/desktop/qml/components/MenuButton.qml apps/desktop/qml/components/ToolCommandBar.qml apps/desktop/qml/workspaces/StudioWorkspace.qml` passed with existing warnings in `MenuButton.qml` and `StudioWorkspace.qml`.
- 2026-07-07 validation: `cmake --build --preset build-dev-macos-debug --target PhotoEditor` passed and packaged/compiled `ToolCommandBar.qml`.
- 2026-07-07 validation: `cmake --build --preset build-dev-macos-debug --target desktop_shell_smoke_test` passed, confirming the updated source-level smoke contract compiles.
- 2026-07-07: Hid the floating `BrushContextPanel` by default in `StudioWorkspace.qml` and changed `brushPageVisibilitySignature()` to `context-panel:hidden-by-default`, leaving detailed brush/target data available through the inspector and deterministic signatures instead of a competing canvas panel.
- 2026-07-08: Added `studioProfessionalUxProofSignature()` to `StudioWorkspace.qml` and runtime/source smoke checks in `desktop_shell_smoke_test.cpp` so the Studio professional UX rules are covered by a single deterministic proof contract.
- 2026-07-08: Replaced the remaining visible Layers copy `UNLINKED SELECTION` with `SAVED SELECTIONS`, shortened active target summaries such as `Global image adjustments` to `Base edits`, and updated hover copy to avoid internal terminology.
- 2026-07-08: Extended desktop visual proof coverage with a Retouch Heal HUD capture at 1600x1000 and 1180x820, increasing the proof manifest to 32 captures and covering Retouch command-bar clipping/overlap at normal and narrow widths.
- 2026-07-08: Added `studioInspectorWorkflowProofSignature()` and `studioInteractionResponsivenessProofSignature()` with smoke assertions for inspector ownership, compact empty states, scroll behavior, immediate tool feedback, nonblocking inspector changes, and target-switch state preservation.
- 2026-07-08: Closed the visual design system checklist against the shared `Theme`, `MiniButton`, `ToolButton`, `LayerRowChrome`, `InspectorSection`, and visual-proof contracts: accent borders are subdued, icon buttons use fixed hit targets, labels fit with full tooltips, and Studio uses restrained neutral surfaces with accent reserved for active/primary/live feedback.
- 2026-07-07 validation: Updated `desktop_shell_smoke_test` so the brush HUD path fails if `brushContextPanel.visible` is true by default. A runtime run with `PHOTO_EDITOR_SMOKE_SKIP_MASK_LAYOUT_PROOF=1 PHOTO_EDITOR_SMOKE_SKIP_MASK_GEOMETRY_PROOFS=1 PHOTO_EDITOR_SMOKE_STOP_AFTER_BRUSH_RESET=1` reached `brush active target checked`, which is after the hidden-context-panel assertion; the run was manually interrupted later in the slower legacy shortcut section.
- 2026-07-07: Added `selectionCreationSurfaceSignature()` and a `selection-top-toolbar:v2` contract. Selection source creation is explicitly owned by the left rail; the top selection toolbar is combine/status/done only, with legacy source buttons hidden.
- 2026-07-07 validation: Updated `desktop_shell_smoke_test` to require `visible-creation-surfaces:1` and `top-source-creation:false`. A runtime run with mask layout/geometry skips reached `mask layout proof skipped`, which is after the one-surface selection assertion; the run was manually interrupted before unrelated slow brush checks.
- 2026-07-07: Simplified active target visible copy in `StudioWorkspace.qml`: adjustment targets now show compact labels such as `Layer` and selection targets show `Selection`; non-base breadcrumbs use user-facing `Layer / Selection` text instead of implementation paths like `Base Image > Adjustment Layer`.
- 2026-07-07 validation: Updated active-target smoke expectations to require `compact-copy:true`, `implementation-path:false`, and no `Base Image >` breadcrumb in non-base target states. A runtime run with mask layout/geometry skips reached `brush active target checked`, which proves the compact target display in the real brush/mask target path; the run was manually interrupted before unrelated slow tail checks.
- 2026-07-07: Simplified layer mask attachment rows so visible row chrome is structure-first. `Edit Layer`, `Edit Mask`, preview, move, and delete controls remain as compatibility hooks but are hidden by default; `photoLayerMaskSetRowMoreMenuButton` and `photoLayerNestedMaskRowMoreMenuButton` are now the visible action surface with `mask-row-actions:v2|default:more-only`.
- 2026-07-07 validation: `qmllint -I apps/desktop/qml -I build/dev-macos-debug/apps/desktop apps/desktop/qml/workspaces/StudioWorkspace.qml` passed with the existing layout warning in `StudioWorkspace.qml`.
- 2026-07-07 validation: `cmake --build --preset build-dev-macos-debug --target desktop_shell_smoke_test` passed.
- 2026-07-07 validation: Runtime layer-row gate passed with `QT_QPA_PLATFORM=offscreen PHOTO_EDITOR_SMOKE_TRACE_PROGRESS=1 PHOTO_EDITOR_SMOKE_SKIP_MASK_LAYOUT_PROOF=1 PHOTO_EDITOR_SMOKE_SKIP_MASK_GEOMETRY_PROOFS=1 PHOTO_EDITOR_SMOKE_SKIP_RETOUCH_PROOFS=1 PHOTO_EDITOR_SMOKE_STOP_AFTER_LAYER_ROWS=1 build/dev-macos-debug/tests/desktop_shell_smoke_test`, reaching `layer row controls checked`. The run still emitted the pre-existing `qmlPerformance is not defined` warnings from `Main.qml`.
- 2026-07-07: Cleaned right-inspector context duplication. `activeEditTargetBar` is now a hidden/queryable context proxy with `active-target-context:v3|inspector-visible:false`; visible target breadcrumbs/status are no longer repeated as an inspector card. The selected selection header now says `Selection` instead of `Editing Mask` and omits fallback/backend status from the visible label.
- 2026-07-07 validation: `qmllint -I apps/desktop/qml -I build/dev-macos-debug/apps/desktop apps/desktop/qml/workspaces/StudioWorkspace.qml` passed with the existing layout warning in `StudioWorkspace.qml`.
- 2026-07-07 validation: `cmake --build --preset build-dev-macos-debug --target desktop_shell_smoke_test` passed.
- 2026-07-07 validation: Targeted runtime gate passed with `QT_QPA_PLATFORM=offscreen PHOTO_EDITOR_SMOKE_TRACE_PROGRESS=1 PHOTO_EDITOR_SMOKE_SKIP_MASK_LAYOUT_PROOF=1 PHOTO_EDITOR_SMOKE_SKIP_MASK_GEOMETRY_PROOFS=1 PHOTO_EDITOR_SMOKE_SKIP_RETOUCH_PROOFS=1 PHOTO_EDITOR_SMOKE_STOP_AFTER_BRUSH_RETURN_TO_BASE=1 build/dev-macos-debug/tests/desktop_shell_smoke_test`, reaching `brush return to base checked`. The run still emitted the pre-existing `qmlPerformance is not defined` warnings from `Main.qml`.
- 2026-07-07: Reorganized the Studio edit rail model into product groups: Adjust, Selection, Brush, Retouch, Crop, Layers, View, Info, and Export. `ToolRail` now exposes `tool-rail-navigation:v2` and `tool-rail-activation:v1` contracts, uses explicit rail object names such as `editRailSelectionButton`, and prefers concise explicit tooltips for icon-only buttons.
- 2026-07-07 validation: `qmllint -I apps/desktop/qml -I build/dev-macos-debug/apps/desktop apps/desktop/qml/components/ToolRail.qml apps/desktop/qml/workspaces/StudioWorkspace.qml` passed with existing `ToolRail.qml` unqualified-access warnings and the existing `StudioWorkspace.qml` layout warning.
- 2026-07-07: Simplified the Brush HUD to a `specialized-tool-hud:v4` contract: mode menu, status, six-item quick tuning for size/flow/hardness, and Done. Density/opacity are owned by the mask inspector, while presets/pressure/reset remain in the Brush mode menu/More path. The hidden Add/Erase buttons remain compatibility hooks, but the visible mode switch is the single Add/Erase surface.
- 2026-07-07: Made `maskFirstBrushSettingsGroup` visible during active brush masks and added `brush-inspector-controls:v1` so Brush size/flow/hardness are available in the inspector while the HUD stays compact.
- 2026-07-07: Replaced the remaining visible implementation copy `Rough fallback: editable, not model-exact` with product-facing `Editable preview`.
- 2026-07-07 validation: `qmllint -I apps/desktop/qml -I build/dev-macos-debug/apps/desktop apps/desktop/qml/components/BrushToolHud.qml apps/desktop/qml/workspaces/StudioWorkspace.qml` passed with existing Brush callback-var warnings and the existing `StudioWorkspace.qml` layout warning.
- 2026-07-07 validation: `cmake --build --preset build-dev-macos-debug --target desktop_shell_smoke_test` passed.
- 2026-07-07 validation: Focused Brush runtime gate passed with `QT_QPA_PLATFORM=offscreen PHOTO_EDITOR_SMOKE_TRACE_PROGRESS=1 PHOTO_EDITOR_SMOKE_SKIP_MASK_LAYOUT_PROOF=1 PHOTO_EDITOR_SMOKE_SKIP_MASK_GEOMETRY_PROOFS=1 PHOTO_EDITOR_SMOKE_SKIP_RETOUCH_PROOFS=1 PHOTO_EDITOR_SMOKE_STOP_AFTER_BRUSH_ACTIVE_TARGET=1 build/dev-macos-debug/tests/desktop_shell_smoke_test`, reaching `brush active target checked`. This proves the compact Brush HUD contract, the six-item tuning menu, inspector ownership for detailed controls, hidden duplicate Brush context panel, and compact active target path. The run still emitted the pre-existing `qmlPerformance is not defined` warnings from `Main.qml`.
- 2026-07-07: Added `PHOTO_EDITOR_SMOKE_STOP_AFTER_RETOUCH_HUD` as a focused Retouch proof path. The gate starts Heal directly after baseline Studio/crop setup and verifies the `ToolCommandBar` HUD, single-row contract, mode menu, More menu, hidden generic edit chrome, clean product status copy, and retouch context ownership without running the full slow Brush suite first.
- 2026-07-07: Fixed Retouch smoke assertions to match the shared `ToolCommandBar` status contract (`fit-horizontal:true`) and to avoid treating an empty inpaint runtime label as leaked backend text.
- 2026-07-07 validation: `cmake --build --preset build-dev-macos-debug --target desktop_shell_smoke_test` passed.
- 2026-07-07 validation: Focused Retouch HUD gate passed with `QT_QPA_PLATFORM=offscreen PHOTO_EDITOR_SMOKE_TRACE_PROGRESS=1 PHOTO_EDITOR_SMOKE_SKIP_MASK_LAYOUT_PROOF=1 PHOTO_EDITOR_SMOKE_SKIP_MASK_GEOMETRY_PROOFS=1 PHOTO_EDITOR_SMOKE_STOP_AFTER_RETOUCH_HUD=1 build/dev-macos-debug/tests/desktop_shell_smoke_test`, reaching `focused retouch HUD checked`.
- 2026-07-07: Added `PHOTO_EDITOR_SMOKE_STOP_AFTER_DIAGNOSTICS_GATING` as a focused Studio diagnostics proof. The gate verifies the diagnostics button and panel stay hidden with `developerDiagnosticsEnabled=false`, appear/open only when the developer gate is enabled, and hide immediately when the gate is turned off.
- 2026-07-07 validation: Focused diagnostics gate passed with `QT_QPA_PLATFORM=offscreen PHOTO_EDITOR_SMOKE_TRACE_PROGRESS=1 PHOTO_EDITOR_SMOKE_SKIP_MASK_LAYOUT_PROOF=1 PHOTO_EDITOR_SMOKE_SKIP_MASK_GEOMETRY_PROOFS=1 PHOTO_EDITOR_SMOKE_STOP_AFTER_DIAGNOSTICS_GATING=1 build/dev-macos-debug/tests/desktop_shell_smoke_test`, reaching `focused diagnostics gating checked`.
- 2026-07-07: Added `studioVisibleSurfaceCountSignature()` to `StudioWorkspace.qml`. The signature reports deterministic counts for rail, canvas, primary HUD, view toolbars, context panel, inspector, filmstrip, diagnostics, and duplicate context state for the current tool.
- 2026-07-07 validation: Focused visible-surface count gate passed with `QT_QPA_PLATFORM=offscreen PHOTO_EDITOR_SMOKE_TRACE_PROGRESS=1 PHOTO_EDITOR_SMOKE_SKIP_MASK_LAYOUT_PROOF=1 PHOTO_EDITOR_SMOKE_SKIP_MASK_GEOMETRY_PROOFS=1 PHOTO_EDITOR_SMOKE_STOP_AFTER_VISIBLE_SURFACE_COUNTS=1 build/dev-macos-debug/tests/desktop_shell_smoke_test`, reaching `focused visible surface counts checked` for Adjust, Crop, and Retouch states.
- 2026-07-07 validation: `cmake --build --preset build-dev-macos-debug --target desktop_shell_smoke_test` passed after adding `worker_queue_registry.cpp` to the smoke target source list.
- 2026-07-07 validation: Fast edit-rail runtime gate passed with `QT_QPA_PLATFORM=offscreen PHOTO_EDITOR_SMOKE_TRACE_PROGRESS=1 PHOTO_EDITOR_SMOKE_STOP_AFTER_EDIT_RAIL=1 build/dev-macos-debug/tests/desktop_shell_smoke_test`. The run still emitted the pre-existing `qmlPerformance is not defined` warnings from `Main.qml`.
- 2026-07-08: Added rail-owned primary-tool activation coverage. `ToolRail.qml` now exposes `triggerToolById()` and a `tool-rail-activation:v1` contract for model-owned activation with concrete rendered rail buttons, and `desktop_shell_smoke_test` verifies Adjust, Selection, Brush, Retouch, Crop, Layers, Compare, and Info all activate through the rail.
- 2026-07-08 validation: `qmllint -I apps/desktop/qml -I build/dev-macos-debug/apps/desktop apps/desktop/qml/components/ToolRail.qml` passed with existing `ToolRail.qml` delegate warnings.
- 2026-07-08 validation: `cmake --build --preset build-dev-macos-debug --target desktop_shell_smoke_test` passed.
- 2026-07-08 validation: Focused rail activation gate passed with `QT_QPA_PLATFORM=offscreen PHOTO_EDITOR_SMOKE_TRACE_PROGRESS=1 PHOTO_EDITOR_SMOKE_STOP_AFTER_RAIL_ACTIVATION=1 build/dev-macos-debug/tests/desktop_shell_smoke_test`, reaching `focused rail activation checked`.
- 2026-07-08: Added `PHOTO_EDITOR_SMOKE_STOP_AFTER_TOOL_SWITCH_CLEANUP` as a focused rail-switch cleanup proof. The gate enters Brush through the rail, starts an active brush stroke, opens diagnostics, expands the selection rail, switches to Adjust through the rail, and verifies the active mask/stroke/crop/compare/diagnostics/rail transient state is cleared without deleting the committed selection. It also switches Compare to Crop to Layers through the rail and verifies compare/crop states do not remain stranded.
- 2026-07-08 validation: Focused tool-switch cleanup gate passed with `QT_QPA_PLATFORM=offscreen PHOTO_EDITOR_SMOKE_TRACE_PROGRESS=1 PHOTO_EDITOR_SMOKE_STOP_AFTER_TOOL_SWITCH_CLEANUP=1 build/dev-macos-debug/tests/desktop_shell_smoke_test`, reaching `focused tool switch cleanup checked`. The run emitted macOS audio warnings and a teardown warning for `save_edit_recipe`, after the focused gate had already passed.
- 2026-07-08: Made `ToolCommandBar.qml` adaptive with a `tool-command-bar:v2` contract. At narrow widths, inline actions collapse into the More menu, status copy hides instead of clipping, and adaptive inline menu items still route back through the inline action handler.
- 2026-07-08 validation: `qmllint -I apps/desktop/qml -I build/dev-macos-debug/apps/desktop apps/desktop/qml/components/ToolCommandBar.qml` passed cleanly.
- 2026-07-08 validation: `cmake --build --preset build-dev-macos-debug --target desktop_shell_smoke_test` passed.
- 2026-07-08 validation: Focused adaptive command-bar gate passed with `QT_QPA_PLATFORM=offscreen PHOTO_EDITOR_SMOKE_TRACE_PROGRESS=1 PHOTO_EDITOR_SMOKE_STOP_AFTER_COMMAND_BAR_ADAPTIVE=1 build/dev-macos-debug/tests/desktop_shell_smoke_test`, reaching `focused command bar adaptive checked`.
- 2026-07-08: Standardized active canvas command placement. Crop, Brush, Retouch, polygon/lasso, and transient selection command HUDs now expose `canvas-command-placement:v1|slot:top-center|top-margin:12`; the transient selection chip was moved from the left side of the canvas into the shared top-center command slot.
- 2026-07-08 validation: `qmllint -I apps/desktop/qml -I build/dev-macos-debug/apps/desktop apps/desktop/qml/workspaces/StudioWorkspace.qml` passed with the existing layout-managed width warning.
- 2026-07-08 validation: `cmake --build --preset build-dev-macos-debug --target desktop_shell_smoke_test` passed.
- 2026-07-08 validation: Focused command placement gate passed with `QT_QPA_PLATFORM=offscreen PHOTO_EDITOR_SMOKE_TRACE_PROGRESS=1 PHOTO_EDITOR_SMOKE_STOP_AFTER_COMMAND_BAR_PLACEMENT=1 build/dev-macos-debug/tests/desktop_shell_smoke_test`, reaching `focused command bar placement checked`.
- 2026-07-08: Added `PHOTO_EDITOR_SMOKE_STOP_AFTER_VIEW_TOOL_SEPARATION` to prove canvas view controls and active edit command controls do not compete. The focused gate verifies Adjust shows the standard zoom/action toolbars, while Crop, Brush, and Retouch hide those view toolbars and show the active command HUD.
- 2026-07-08 validation: Focused view/edit separation gate passed with `QT_QPA_PLATFORM=offscreen PHOTO_EDITOR_SMOKE_TRACE_PROGRESS=1 PHOTO_EDITOR_SMOKE_STOP_AFTER_VIEW_TOOL_SEPARATION=1 build/dev-macos-debug/tests/desktop_shell_smoke_test`, reaching `focused view tool separation checked`. The run emitted macOS audio warnings and a teardown warning for `save_edit_recipe`, after the focused gate had already passed.
- 2026-07-08: Tightened Brush cursor copy so the on-canvas cursor readout explicitly shows `Size`, hardness, flow, and density; the cursor keeps the dual-contrast outer ring and hardness inner ring. Also removed an early QML `pixelBrushToolReady` ReferenceError in the top-level mask-work state calculation.
- 2026-07-08 validation: `qmllint -I apps/desktop/qml -I build/dev-macos-debug/apps/desktop apps/desktop/qml/workspaces/StudioWorkspace.qml` passed with the existing layout-managed width warning.
- 2026-07-08 validation: `cmake --build --preset build-dev-macos-debug --target desktop_shell_smoke_test` passed.
- 2026-07-08 validation: Focused Brush cursor/overlay gate passed with `QT_QPA_PLATFORM=offscreen PHOTO_EDITOR_SMOKE_TRACE_PROGRESS=1 PHOTO_EDITOR_SMOKE_STOP_AFTER_BRUSH_CURSOR_OVERLAY=1 build/dev-macos-debug/tests/desktop_shell_smoke_test`, reaching `focused brush cursor overlay checked`. The gate verifies size/hardness are visible on the cursor and the live mask matte remains visible during an active brush stroke.
- 2026-07-08: Added `PHOTO_EDITOR_SMOKE_STOP_AFTER_BRUSH_CANCEL` as a focused Brush Esc/cancel proof. The gate starts a real brush selection, begins an active stroke, verifies the Esc shortcut path is enabled, calls `cancelActiveTool()`, and checks the stroke ends while the Brush selection remains active and selected.
- 2026-07-08 validation: Focused Brush cancel gate passed with `QT_QPA_PLATFORM=offscreen PHOTO_EDITOR_SMOKE_TRACE_PROGRESS=1 PHOTO_EDITOR_SMOKE_STOP_AFTER_BRUSH_CANCEL=1 build/dev-macos-debug/tests/desktop_shell_smoke_test`, reaching `focused brush cancel checked`.
- 2026-07-08: Added `PHOTO_EDITOR_SMOKE_STOP_AFTER_BRUSH_WORKFLOW` as focused UI coverage for Brush enter, paint, erase, tune, Done, and cancel. The gate enters Brush from selection creation, paints a stroke, switches to Erase, tunes size, exits with Done while keeping the selection, re-enters Brush, paints again, and exits with cancel without deleting the selection. Also restored product-facing smart-selection fallback copy to `Editable preview`.
- 2026-07-08 validation: Focused Brush workflow gate passed with `QT_QPA_PLATFORM=offscreen PHOTO_EDITOR_SMOKE_TRACE_PROGRESS=1 PHOTO_EDITOR_SMOKE_STOP_AFTER_BRUSH_WORKFLOW=1 build/dev-macos-debug/tests/desktop_shell_smoke_test`, reaching `focused brush workflow checked`. The run emitted macOS audio warnings and a teardown warning for `save_edit_recipe`, after the focused gate had already passed.
- 2026-07-07: Added the Studio animation pass. `StudioWorkspace.qml` now exposes `studio-motion:v1` with 120ms fast and 180ms medium transitions, reduced-motion gating, interruptible tool-switch state, fades for active canvas HUDs, Retouch HUD, inspector panel, active-target proxy, and selection header. `ToolCommandBar.qml` and `LayerRowChrome.qml` now expose shared motion contracts for fixed-height command bars and stable-height row hover/reveal states.
- 2026-07-07: Tightened visible selection-header copy so fallback/deterministic implementation terms remain out of the primary UI while diagnostic details stay in signatures.
- 2026-07-07 validation: `qmllint -I apps/desktop/qml -I build/dev-macos-debug/apps/desktop apps/desktop/qml/components/LayerRowChrome.qml apps/desktop/qml/components/ToolCommandBar.qml apps/desktop/qml/workspaces/StudioWorkspace.qml` passed with existing `LayerRowChrome.qml` missing-property warnings and the existing `StudioWorkspace.qml` layout warning.
- 2026-07-07 validation: `cmake --build --preset build-dev-macos-debug --target desktop_shell_smoke_test` passed.
- 2026-07-07 validation: Fast edit-rail runtime gate passed with `QT_QPA_PLATFORM=offscreen PHOTO_EDITOR_SMOKE_TRACE_PROGRESS=1 PHOTO_EDITOR_SMOKE_STOP_AFTER_EDIT_RAIL=1 build/dev-macos-debug/tests/desktop_shell_smoke_test`. The run still emitted the pre-existing `qmlPerformance is not defined` warnings from `Main.qml`.
- 2026-07-07: Cleaned primary Studio implementation vocabulary. Selection rows now use product copy such as `Selection`, `Standalone selection`, `Linked selection`, `Brush selection`, `Path selection`, and `Smart selection`; old visible strings such as `Unlinked`, `Editing mask`, alpha-provider summaries, fallback wording, and full-resolution retouch output copy were removed from primary Studio surfaces.
- 2026-07-07: Gated backend/system detail in `agentModelStatusText()` and `systemHealthItems()` behind `developerDiagnosticsEnabled`, while preserving diagnostic signatures and provider properties for tests/developer inspection.
- 2026-07-07 validation: Updated `desktop_shell_smoke_test` to reject the old visible implementation strings in `StudioWorkspace.qml` and require product-facing selection provenance helpers.
- 2026-07-07 validation: `qmllint -I apps/desktop/qml -I build/dev-macos-debug/apps/desktop apps/desktop/qml/workspaces/StudioWorkspace.qml` passed with the existing layout warning in `StudioWorkspace.qml`.
- 2026-07-07 validation: `cmake --build --preset build-dev-macos-debug --target desktop_shell_smoke_test` passed.
- 2026-07-07 validation: Fast edit-rail runtime gate passed with `QT_QPA_PLATFORM=offscreen PHOTO_EDITOR_SMOKE_TRACE_PROGRESS=1 PHOTO_EDITOR_SMOKE_STOP_AFTER_EDIT_RAIL=1 build/dev-macos-debug/tests/desktop_shell_smoke_test`. The run still emitted the pre-existing `qmlPerformance is not defined` warnings from `Main.qml`.
