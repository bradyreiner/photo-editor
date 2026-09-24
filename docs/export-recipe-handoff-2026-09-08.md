# Complete recipe handoff to Export — 2026-09-08

## Defects and changes

`Main.qml::syncEditorRecipeToCatalog` previously copied only raw, tone, color,
crop, masks and detail groups using a source-agnostic patch. That same-asset
handoff omitted layers, preset provenance and recipe history, and removed
explicit source/profile bindings. ExportView then correctly queued an incomplete
catalog recipe. A regression against the original QML function reproduced two
layers becoming zero while the mask survived.

The handoff now uses `currentAssetBoundAdjustmentPatchJson()` with all recipe
groups. The existing local-file and same-asset-switch guards remain intact.
This does not change reusable copy/paste semantics or replace the persistent
Export-panel job runner with the standalone export API.

A second regression showed that loading an existing recipe into
`EditorPreviewController` dropped its recipe-level branch and named snapshots.
The controller now retains both fields in `applyState` and `currentState`,
including undo/redo. These are distinct from the editor's separate UI history
service and its snapshot list.

## Focused proof

`export_recipe_handoff_test` extracts and executes the production QML handoff
function against real catalog/editor controllers. It verifies complete JSON
equality, two layers plus a mask, profile/preset/history retention, history
through undo, no history leakage into a different asset, unchanged unrelated
assets, guarded local-file/same-asset calls,
and a captured recipe remaining unchanged after subsequent edits. Full-resolution
renders before and after handoff must have exactly equal pixels and profiles;
removing layers must change the image.

- Original QML failure: `/private/tmp/export-recipe-handoff-before.log`.
- Original controller history failure: `/private/tmp/export-handoff-history-before.log`.
- Four focused tests pass: handoff, source relink, layer snapshot, export recovery.
  The five-test run separately fails `desktop_shell_source_contract_test` on
  shared menu/split-button requirements, outside the changed export contract.
  Log: `/private/tmp/export-handoff-regressions.log`.
- Final four-test rerun, including cross-asset history isolation: 4/4 pass in
  24.95 seconds; `/private/tmp/export-handoff-final-regressions.log`.
- Real DNG handoff: exact 4208×3120 pixels/profile with two layers and one mask;
  `/private/tmp/export-handoff-real-dng.log`.
- Real JPEG handoff: the same exact 4208×3120 checks pass;
  `/private/tmp/export-handoff-real-jpeg.log`.

## Native proof and limits

The `--export-handoff-proof` extension to `--placed-layer-proof` seeds a recipe
with history into an isolated in-memory catalog, uses the real Edit-to-Export
workspace transition and `ExportView.queueCurrentSelection`, checks the
persisted job's complete recipe, waits for the normal job runner, and compares
the delivered PNG with an editor-snapshot render. It uses a fresh managed
PostgreSQL cluster beneath the explicitly supplied proof directory, not the
user catalog. Stop that owned cluster after the app terminates; retain artifacts.

The first native attempt, `/private/tmp/export-handoff-native-MROLRA`, failed
before editing because the healthy startup catalog refresh cleared the early
fixture. Fixture seeding now waits for that refresh to finish. Its owned
PostgreSQL server was stopped and status confirmed unhealthy/stopped, with all
files retained. Editor recipe persistence is disabled only for this in-memory
fixture; persistent export jobs remain enabled.

The RAW-base/JPEG-layer native proof passes in
`/private/tmp/export-handoff-native-oBHyuY`: complete editor/catalog/job recipe
equality, loaded history retention, normal persistent job completion, and exact
4208×3120 delivered/reference PNG pixels and profiles. Whole correctness workflow
took 58,497ms. `startup.png` was visually inspected and shows the completed Export
job. Its database was stopped after the app exited. The selection panel is empty
after the job's catalog refresh removes the in-memory-only fixture; this is not
proof of real catalog selection or asset persistence.

The reverse JPEG-base/RAW-layer native proof also passes in
`/private/tmp/export-handoff-jpeg-native-qJdzGK`, with the same full recipe,
history and exact 4208×3120 pixels/profile checks. Whole correctness workflow
took 76,783ms. Its completed Export job screenshot was also visually inspected,
and its isolated database stopped after exit. Both successful receipts have
`ok: true` and `exportHandoffProof.ok: true`. PNG encoded file bytes can differ
because of container metadata; these checks compare decoded pixels and profiles.

The native path
has a separate 120-second correctness deadline; it is not a realtime benchmark
or an extension of the existing 45-second placed-layer performance gate.

Shared-renderer comparisons establish recipe handoff fidelity, not independent
color-science ground truth. Native placed layers are full-canvas and opaque;
the separate two-layer/mask test uses a small placed raster on photographic
sources and supplies the layer-removal negative control. The in-memory catalog
fixture does not establish a
real import/relaunch persistence round-trip. Diverse cameras, all color spaces,
full-resolution realtime, real 4D reconstruction and embedded-model quality
remain open under `active-app-completion-goal.md`.
