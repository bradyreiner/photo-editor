# Layer System Sparse UI Checklist

Goal: make layers behave predictably, read as a calm stack, expose controls in a separate properties surface, and keep interactions fast.

## Checklist

- [x] Audit current layer UI, controller behavior, and failing proof points.
  - Current layer rows, selected-layer header, adjustment command bar, visibility state row, inline sliders, and structure actions all live in `StudioWorkspace.qml`.
  - Backend layers are real composited render inputs through `pipeline_photo_layers(...)` and `apply_photo_layers(...)`.
  - `editor_preview_layer_adjustment_test`, `layer_lut_render_parity_test`, and CPU render parity passed in the current tree.
  - `editor_preview_controller_test` still reports an unrelated AI/patch-brush failure in the full binary.
  - `desktop_shell_smoke_test` currently reports an unrelated smart-selection source contract failure before the layer-row assertions run.
- [x] Convert the Layers header to a sparse toolbar.
  - Show title, concise count, add split button, isolation state only when active, and move visibility states to overflow/menu-style controls.
- [x] Remove duplicate selected-layer chrome from the default visual flow.
  - The active/editing state should live on the actual row and in the properties panel, not in a separate selected-layer card above the stack.
- [x] Make layer rows sparse and fixed-height.
  - Default rows stay at 48px.
  - Active rows stay the same height.
  - Rows show identity and stack state only: grip, thumbnail, name, compact state text, visibility, and icon-only actions.
- [x] Move all precision controls into a separate Layer Properties surface.
  - Layer opacity and blend mode are the only default controls under the stack.
  - Mask opacity/density, text controls, clipping, mask, transform, solo, lock, duplicate, move, delete, and adjustment copy/paste/sync live outside the stack row and are progressive or menu-driven.
  - The properties surface adapts to layer kind and keeps irrelevant controls hidden.
- [x] Preserve and clarify behavior.
  - Row click edits the layer.
  - Selection/check affordance is only for batch selection.
  - Row visibility and row More menus stay available without expanding row height.
  - Batch actions appear only for multi-selection.
  - Locked layers disable invalid operations with existing disabled reasons.
  - Groups are treated as organizational rows unless true grouped compositing is implemented later.
- [x] Reduce QML and render churn.
  - Avoid hover-driven row height changes.
  - Keep collapsed rows to one stable layout.
  - Instantiate sliders and heavier controls once in the properties surface instead of once per row.
  - Keep thumbnail refresh async and non-blocking.
- [x] Update tests/proof to match the sparse stack/properties contract.
  - Replace expectations for the old selected-layer header and always-visible row controls.
  - Add a sparse/progressive properties contract so the default state stays low-density while controls remain reachable.
  - Keep proof for opacity, blend, mask opacity/density, clipping, transform, visibility states, duplicate/reorder/delete, and adjustment copy/paste/sync.
- [x] Validate the implementation.
  - `qmllint` on changed QML.
  - Focused desktop/QML smoke coverage where available, with unrelated pre-layer blockers recorded.
  - Focused controller/layer adjustment/render parity coverage.

## Validation Log

- [x] `qmllint -I apps/desktop/qml -I build/dev-macos-debug/apps/desktop apps/desktop/qml/workspaces/StudioWorkspace.qml apps/desktop/qml/components/LayerRowActionZone.qml apps/desktop/qml/components/LayerRowChrome.qml`
  - Passed with existing warnings in `StudioWorkspace.qml`, `LayerRowActionZone.qml`, and `LayerRowChrome.qml`.
- [x] `cmake --build --preset build-dev-macos-debug --target desktop_shell_smoke_test editor_preview_layer_adjustment_test PhotoEditor`
  - Passed.
- [x] `QT_QPA_PLATFORM=offscreen build/dev-macos-debug/tests/editor_preview_layer_adjustment_test`
  - Passed.
- [x] `QT_QPA_PLATFORM=offscreen build/dev-macos-debug/tests/layer_lut_render_parity_test`
  - Passed.
- [x] `QT_QPA_PLATFORM=offscreen build/dev-macos-debug/tests/render_parity_test`
  - Passed with Metal parity skipped on this host: `no_system_metal_device`.

## Non-Layer Suite Blockers Observed

- `QT_QPA_PLATFORM=offscreen PHOTO_EDITOR_SMOKE_TRACE_PROGRESS=1 PHOTO_EDITOR_SMOKE_SKIP_MASK_LAYOUT_PROOF=1 PHOTO_EDITOR_SMOKE_SKIP_MASK_GEOMETRY_PROOFS=1 PHOTO_EDITOR_SMOKE_STOP_AFTER_LAYER_ROWS=1 build/dev-macos-debug/tests/desktop_shell_smoke_test`
  - Blocked before layer-row assertions by an unrelated smart-selection source contract: `smart-selection hover prewarm should wait for pointer pause, ignore jitter, and avoid immediate enter/release segmentation`.
- `QT_QPA_PLATFORM=offscreen build/dev-macos-debug/tests/editor_preview_controller_test`
  - Blocked outside the layer UI path: `addAIMask: "sky" "Vision semantic masking is only available on macOS 14+"` and `patch brush should bake an inpaint patch when applied`.
