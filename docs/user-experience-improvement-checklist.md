# User Experience Improvement Checklist

Status: active product checklist

## Goal

Make the app feel like one dependable photographer workflow rather than a collection of capable workspaces. A user should be able to import a real shoot, cull quickly, edit one or many photos, understand exactly what an action will affect, recover from mistakes or missing media, and export trusted results without needing to understand the app's implementation.

The product path covered by this checklist is:

`first launch -> import -> cull -> batch edit -> refine -> compare -> export -> relaunch/recover`

## Status Rules

- `[ ]` Not complete or not proven.
- `[~]` Useful behavior exists, but the completion gate is not fully proven.
- `[x]` The user-facing behavior and its completion gate have both been proven.
- Existing controls, controller methods, tests, or checked items in another document do not automatically complete an item here.
- Do not mark an item `[x]` until its evidence is recorded under that item.
- A passing source scan is supporting evidence, not sufficient user-experience proof by itself.

Supporting September 12 editor specifications and partial precision-control evidence:
[editor experience audit](editor-experience-audit-2026-09-12.md) and
[items 3/12 evidence packet](evidence/editor-experience-2026-09-12/README.md), and
[item 7 exposure evidence](evidence/editor-experience-2026-09-12/exposure.md), and
[item 11 comparison evidence](evidence/editor-experience-2026-09-12/section-comparison.md).
These component and controller checks do not change the owner gates below.

## Overall Completion Gate

This checklist is complete only when all of the following are true:

- [ ] All 15 item-level completion gates are checked and have linked evidence.
- [ ] A new user can complete the full product path without developer assistance or implementation terminology.
- [ ] A representative workflow test covers at least 500 mixed RAW/JPEG assets, 50 culled keepers, a 20-photo batch edit, one masked local refinement, and a 20-photo export.
- [ ] The workflow includes a relaunch, an undo/recovery action, one missing-media recovery, and one failed-export retry.
- [ ] Previewed and exported results agree for the edits exercised by the workflow.
- [ ] No blocking crash, unrecoverable state, clipped primary control, silent failure, or unexplained disabled action occurs during the workflow.
- [ ] Keyboard-only and pointer-driven passes both complete the core import, cull, edit, and export tasks.
- [ ] Evidence includes automated coverage where deterministic, screenshots or recordings for visible behavior, and a short manual acceptance report using real media.

**Current automated representative-workflow evidence:** `ux_representative_workflow_test` passes in 10.62 seconds through one integrated controller path with 500 catalog assets, 50 keepers, selective Light/Color edits on 20 photos, one brush-mask local refinement, exact recipe undo/redo, and 20 decoded, artifact-verified Web Proof exports. The [machine-readable manifest](/Users/bradyreiner/.codex/visualizations/2026/07/14/019f624a-dd6a-7291-a8e9-a62170ae5322/representative-workflow-manifest.json) and [20 delivered JPEGs](/Users/bradyreiner/.codex/visualizations/2026/07/14/019f624a-dd6a-7291-a8e9-a62170ae5322/representative-workflow-delivery) preserve the result. This is supporting evidence, not a completed overall gate: the 225 `.dng` entries are explicitly catalog-path fixtures (`rawDecodeExercised: false`), and real RAW decoding, the pointer-driven pass, relaunch/relink, and failed-export recovery still require the final real-media run.

## Priority Order

- **P0 — Core workflow:** Items 1-5, 8, 10, and 12.
- **P1 — Trust and recovery:** Items 6, 7, 9, 11, and 14.
- **P2 — Differentiation and inclusion:** Items 13 and 15.

---

## 1. First-Run Guided Workflow

**Goal:** A first-time user reaches a successful edit and export without configuring internal services or learning the app's architecture.

**Status:** `[~]` The resumable Quick Start, sample-photo path, own-photo path, persistence, runtime import, and real export are implemented and proven. The five-person usability pass remains open.

- [x] Offer a clear choice between opening a sample project and importing the user's own photos.
- [x] Explain managed versus referenced import in outcome-based language.
- [x] Guide the user through one short import, cull, edit, and export path.
- [x] Keep database, provider, model, render-backend, and service terminology out of the default flow.
- [x] Let the user skip or restart guidance without losing work.
- [x] End with a real exported file and an obvious next action.

**Completion gate:** In a clean-profile usability pass, five first-time users can import or open the sample project, rate a photo, make an edit, and export it without developer help; at least four finish in 10 minutes or less, nobody is blocked by setup terminology, and restart/skip behavior is covered by an automated UI test.

**Evidence:** `app_shell_controller_test` proves clean-profile pending state, versioned completion persistence, restart behavior, three generated editable PNGs, and meaningful sampled color variation. `app_bootstrap_test` proves the Rust helper fallback uses an absolute repository manifest instead of the launch working directory. `job_queue_model_test` proves an older failed import remains inspectable without leaving stale global failure chrome after a newer success. Import preflight now explains `Managed copy` as copying originals into Framehouse storage while leaving the source untouched, and `Leave in place` as linking originals that can go offline if moved or disconnected; `catalog_asset_model_test` and the Item 10 visual proof verify both outcomes and destinations. The packaged QML module and full `photo_editor_app` build passed. An isolated runtime pass using `PHOTO_EDITOR_APP_DATA_DIR=/tmp/virel-ux-proof3-data` opened Quick Start and imported exactly three sample assets without developer setup; visual evidence: [first-run-sample-import.png](/Users/bradyreiner/.codex/visualizations/2026/07/14/019f624a-dd6a-7291-a8e9-a62170ae5322/first-run-sample-import.png). The 130%-scale keyboard acceptance follows the own-photo path through Quick Start, cull, edit, and Export, starts the primary export by keyboard, waits for a real completed job, and preserves the decoded 12 KB result as [first-run-keyboard-export.png](/Users/bradyreiner/.codex/visualizations/2026/07/14/019f624a-dd6a-7291-a8e9-a62170ae5322/first-run-keyboard-export.png); the [accessibility manifest](/Users/bradyreiner/.codex/visualizations/2026/07/14/019f624a-dd6a-7291-a8e9-a62170ae5322/accessibility-manifest.json) records the completed job and artifact byte count.

## 2. Keyboard-First Culling

**Goal:** A photographer can review a shoot at speed without repeatedly reaching for the pointer or waiting for the UI.

**Status:** `[~]` The complete keyboard workflow and automated 500-photo latency/state pass are proven. A human 100-photo pointer-free pass remains open.

- [x] Provide stable next/previous navigation with no focus loss.
- [x] Support picks, rejects, star ratings, color labels, and undo from the keyboard.
- [x] Provide compare, survey, and full-screen focus-check views.
- [x] Make filter changes immediately reflect culling decisions.
- [x] Keep filmstrip/grid state synchronized with the active photo.
- [x] Show immediate, unambiguous confirmation for every rating decision.

**Completion gate:** A scripted 500-photo culling pass completes with no lost keystrokes, incorrect active-photo changes, or stale rating/filter state; a manual tester can rate or reject 100 photos without using the pointer, and p95 next-photo visible latency meets the project's interactive performance budget.

**Evidence:** `catalog_asset_model_test` runs a deterministic 500-photo pass through rating, pick, reject, color-label, next-photo, and filter state, with exact active-photo assertions and a measured `culling_500_photo_p95_ms=8` against the 50 ms interactive budget. The same test proves persisted color labels plus culling Undo/Redo. Library actions now publish review state optimistically, save a local recovery copy, and serialize catalog synchronization off the interaction path. `photo_editor_app_qmllint` passes with the repository's existing warning baseline, and `cargo test -p photo-catalog --lib --bins` passes all 14 Rust catalog tests plus all catalog binaries. Existing desktop smoke coverage exercises survey population, compare modes, focus-check, full-screen editing, synchronized compare viewports, and deterministic shortcuts. Manual 100-photo pointer-free evidence is still required before closing the completion gate.

## 3. Selective Batch Editing

**Goal:** Users can safely apply a look or correction to many photos while controlling exactly which edits transfer.

**Status:** `[x]` Complete and proof-gated.

- [x] Make multi-selection and batch scope visible before applying changes.
- [x] Let users choose which adjustment groups to copy or synchronize.
- [x] Preview affected photos and conflicts before committing.
- [x] Preserve per-photo exceptions that were excluded from synchronization.
- [x] Update thumbnails, recipes, history, and export consequences consistently.
- [x] Support one-step undo for the complete batch operation.

**Completion gate:** A 20-photo mixed-edit batch can selectively synchronize light and color while excluding crop and masks; all included photos update, all excluded state remains byte-for-byte unchanged, thumbnails and export outputs reflect the new recipes, and one undo restores every affected photo.

**Evidence:** Studio's Adjust inspector exposes `Sync Selected Photos` only for a multi-photo selection, names the first affected files, shows the target/conflict count, and lets the user include or exclude RAW, Light, Color, Detail, Crop, and Masks before committing. `catalog_asset_model_test` runs a 20-photo mixed-recipe gate: Light and Color synchronize on every target; Crop and Mask fields remain unchanged; all 20 thumbnails refresh; final export-render pixels reflect the synchronized recipes; one batch Undo restores every recipe byte-for-byte, refreshes every thumbnail again, and restores every final export output. Recipe persistence is optimistic and serialized off the interaction path while the local recovery state is written as one batch. `photo_editor_app_qmllint` and the packaged `photo_editor_app` build pass.

## 4. Simpler Studio Hierarchy

**Goal:** The photo stays visually dominant and users see only the controls relevant to the current task.

**Status:** `[~]` The hierarchy, deterministic responsive layouts, and minimum/typical/wide screenshots are proven. The five-second human identification pass remains open.

- [x] Keep essential light and color controls in the default adjustment surface.
- [x] Place advanced color, scopes, diagnostics, and technical controls behind disclosure.
- [x] Ensure one surface owns tool switching, one owns immediate tool actions, and one owns detailed properties.
- [x] Avoid duplicated commands across the rail, canvas HUD, and inspector.
- [x] Preserve expert access through shortcuts, search, or explicit advanced sections.
- [x] Verify clean layouts at every supported window width and density mode.

**Completion gate:** Screenshot and interaction tests at minimum, typical, and wide window sizes show no duplicated primary action, clipping, overlap, or competing active context; a user can identify the selected tool, selected target, and next primary action within five seconds in each major Studio mode.

**Evidence:** Studio declares and runtime-tests a single tool rail, contextual command bar/HUD, detailed inspector, stable image stage, and collapsed diagnostics contract. Essential White Balance, Light, and Color remain in Adjust; advanced tone/detail, Liquify, scopes, overlays, and color-transform diagnostics are disclosed or developer-gated. Responsive interaction coverage asserts deterministic small/collapsed behavior at 980 and 1119 px, compact behavior at 1180 px, and full Pro behavior at 1440 px, including fixed panel-width/visibility signatures. The text-fit source gate passes after profile and diagnostic labels were converted from truncating elision to wrapped full text, and the packaged QML/app build passes. The real QML shell now captures settled, unclipped hierarchy proofs for [Clean Adjust at 980 x 760](/Users/bradyreiner/.codex/visualizations/2026/07/14/019f624a-dd6a-7291-a8e9-a62170ae5322/studio-hierarchy-clean-adjust-980x760.png), [Pro Crop at 1180 x 820](/Users/bradyreiner/.codex/visualizations/2026/07/14/019f624a-dd6a-7291-a8e9-a62170ae5322/studio-hierarchy-pro-crop-1180x820.png), and [Inspect Masks at 1600 x 1000](/Users/bradyreiner/.codex/visualizations/2026/07/14/019f624a-dd6a-7291-a8e9-a62170ae5322/studio-hierarchy-inspect-masks-1600x1000.png), with selected tool, selected target, and primary action visible. The human five-second identification check remains required before closing the completion gate.

## 5. Clear Active-Edit Targeting

**Goal:** Before changing a control, the user can tell whether it will affect the base photo, a layer, a selection/local edit, or a batch of photos.

**Status:** `[x]` Complete and proof-gated.

- [x] Show one compact active-target breadcrumb in Studio.
- [x] Use visibly distinct states for base, layer, selection, and multi-photo targets.
- [x] Route every adjustment entry point through the same target-resolution contract.
- [x] Explain disabled controls and incompatible targets in user-facing language.
- [x] Preserve sensible targets when switching tools and reset transient targets safely.
- [x] Prevent silent edits to a different target than the one displayed.

**Completion gate:** A target-routing matrix exercises every primary adjustment family against base, photo layer, adjustment layer, active selection, and multi-photo scope; the displayed target and changed recipe destination agree in every supported case, unsupported cases explain why they are disabled, and no action silently becomes a no-op.

**Evidence:** Studio now keeps one visible, compact target bar immediately above the inspector controls, with distinct Base, Layer, Selection, and `N selected` batch states. Numeric, string, boolean, preset, reset, curve, RAW-only, and crop-only actions use one lock-aware target resolver; incompatible pixel layers, locked layers, retouch-only masks, and base-only controls are disabled with plain-language explanations. The focused `PHOTO_EDITOR_SMOKE_STOP_AFTER_COLOR_ROUTING=1` matrix passed in 102.96 seconds and exercised Base Image, unsupported photo layer, adjustment layer, locked adjustment layer, active brush selection, and multi-photo scope across RAW, white balance, light, color, color grading/preset, tone curve, presence, detail, lens/transform, crop declarations, and explicit Batch Sync. It asserts the visible breadcrumb agrees with the actual base recipe, adjustment-layer recipe, selection-linked recipe, or explicit batch destination and verifies unsupported actions do not mutate Base Image. `editor_preview_local_edit_target_test` separately passes creation, undo/redo, and recreation of selection-linked adjustment layers; `catalog_asset_model_test` proves the 20-photo explicit batch destination and one-step restoration. `editor_preview_outline_trace_test` and `photo_editor_app_qmllint` pass the shared routing/source and packaged-QML gates.

## 6. Understandable Masks and Layers

**Goal:** Users can create, inspect, refine, organize, and remove local edits without needing to understand internal stack mechanics.

**Status:** `[x]` Complete and proof-gated.

- [x] Show useful thumbnails, names, visibility, selected state, and effect state in each layer row.
- [x] Support rename, duplicate, reorder, visibility, lock, and deliberate deletion.
- [x] Use Add, Remove, Intersect, and Invert consistently for selection refinement.
- [x] Make selection-to-layer and layer-mask relationships visible but not noisy.
- [x] Provide helpful empty, stale, missing-model, and failed-generation states.
- [x] Keep layer and mask deletion recoverable through undo.

**Completion gate:** A user can create a local edit, refine it, rename it, reorder it, toggle visibility, duplicate it, delete it, and undo deletion without leaving Studio or opening diagnostics; screenshots prove the affected region and parent layer are identifiable at normal and narrow inspector widths.

**Evidence:** `editor_preview_controller_test` passed in 229.78 seconds and directly exercises layer rename, visibility, lock rejection, duplicate-with-masks and duplicate-without-masks policies, reorder, deliberate deletion, mask-source Add/Subtract/Intersect/Invert behavior, and exact undo restoration after layer and mask deletion. `editor_preview_layer_adjustment_test` passed in 22.03 seconds and verifies useful layer/mask thumbnails plus current, stale, missing-input, and failed-generation publication states. The focused Studio smoke passed in 56.64 seconds and creates a brush selection, routes its adjustment only to the selection-linked layer, verifies the Base recipe remains unchanged, and captures the selected mask header plus its parent `Layer / Mask` row at both widths. Visual evidence: [normal 1440 x 900](/Users/bradyreiner/.codex/visualizations/2026/07/14/019f624a-dd6a-7291-a8e9-a62170ae5322/mask-layer-identity-normal-1440x900.png), [narrow 980 x 760](/Users/bradyreiner/.codex/visualizations/2026/07/14/019f624a-dd6a-7291-a8e9-a62170ae5322/mask-layer-identity-narrow-980x760.png), and the machine-readable [capture manifest](/Users/bradyreiner/.codex/visualizations/2026/07/14/019f624a-dd6a-7291-a8e9-a62170ae5322/mask-layer-identity-manifest.json).

## 7. Human-Readable Edit History

**Goal:** Users can understand what changed, return to a known point, and recover from accidental edits.

**Status:** `[x]` Complete and proof-gated.

- [x] Label history entries with user-facing actions and values.
- [x] Coalesce continuous slider drags and brush strokes into meaningful undo steps.
- [x] Include batch, crop, layer, mask, retouch, and AI actions in the same history model.
- [x] Support named snapshots or restore points.
- [x] Preserve history correctly across asset switches and relaunch where intended.
- [x] Clearly distinguish preview-only actions from recipe-changing actions.

**Completion gate:** A mixed 25-action session produces an ordered, readable history with no implementation labels; undo and redo reconstruct the exact expected recipe and visible result at every step, a named snapshot restores correctly after further edits, and compare/preview actions create no history mutations.

**Evidence:** Studio now has one History inspector containing chronological, user-facing recipe steps; arbitrary-step restore; named persistent snapshots; ordered, one-operation batch history with affected-photo counts and exact backward/forward restore; and assistant run provenance that labels read-only analysis separately while linking recipe-changing assistant work to the undoable recipe steps. `editor_preview_history_test` passed in 0.60 seconds after a mixed 25-action sequence covering a coalesced exposure drag, tone, color, detail, lens, crop, fill layer creation/rename/opacity, mask, selection-local adjustment, invert, and retouch. It verifies every displayed label is free of implementation vocabulary, every undo and redo reconstructs the exact serialized recipe, compare and mask-display changes add no step, asset switching isolates and restores session history, and a named snapshot survives controller relaunch and restores as a new undoable edit. `catalog_asset_model_test` passed in 11.65 seconds and proves chronological user-facing batch entries plus exact arbitrary-step restoration across every affected photo. `agent_edit_controller_test` passed in 52.60 seconds and proves analyze, draft/apply provenance, and rollback entries used by the same inspector. The focused offscreen Studio smoke passed in 78.42 seconds, opens History through the normal tab router, creates the persistent `Color approved` restore point, verifies both unified batch and assistant contracts, restores a real two-photo batch backward and forward, and rejects failed preview states before visual capture. `photo_editor_app_qmllint` also passes. Visual evidence: [History at 1440 x 900](/Users/bradyreiner/.codex/visualizations/2026/07/14/019f624a-dd6a-7291-a8e9-a62170ae5322/edit-history-normal-1440x900.png) and the machine-readable [capture manifest](/Users/bradyreiner/.codex/visualizations/2026/07/14/019f624a-dd6a-7291-a8e9-a62170ae5322/edit-history-manifest.json).

## 8. Immediate and Honest Preview Feedback

**Goal:** Every edit feels immediate while the app remains truthful about draft, refining, and full-resolution states.

**Status:** `[x]` Complete and proof-gated.

- [x] Show visible feedback on the first interaction frame for continuous edits.
- [x] Preserve a valid higher-resolution preview until a newer correct result is ready.
- [x] Make refining or full-resolution settlement compactly visible when relevant.
- [x] Prevent stale render results from replacing newer edits.
- [x] Preserve zoom, pan, overlay, and active-tool context while previews update.
- [x] Maintain preview/export parity for the exercised editing paths.

**Completion gate:** Automated latency and stale-publication tests cover sliders, crop, brush, masks, layers, compare, and tool switching; first-visible feedback meets the interactive budget, the final preview reaches full-resolution backing, no older result publishes over a newer edit, and visual proof matches exported pixels within the defined parity tolerance.

**Evidence:** Studio now reports `Draft preview • refining` when a lower-resolution interactive result is in flight and `Full resolution held • refining` when a still-valid recipe-matching full-resolution backing remains visible. The full-resolution signal uses the output dimensions and recipe signature of the last accepted full render, preventing both a lower-resolution draft from being mislabeled as full resolution and a valid cropped/rotated full render from being mislabeled as a draft. The focused `editor_preview_feedback_test` passed in 4.21 seconds and enforces a 33 ms first-frame dispatch budget for sliders, crop geometry, brush points, mask adjustments, layer adjustments, and compare; zero-delay interactive scheduling for slider, mask, and layer renders; bounded interactive and settled render budgets; exact source-resolution and cropped-resolution settlement; rejection of a forged stale full-resolution result; preservation of a valid 4000 x 3000 texture for view-only viewport work; and replacement of that texture by a correct draft when content changes. The focused Studio first-interaction smoke passed in 21.31 seconds and verifies every current primary rail tool synchronously exposes transition state within its 100 ms UI budget while preserving the active workspace context. `editor_preview_pixel_zoom_test` passed in 2.06 seconds and proves queued viewport requests retain the latest zoom/pan region, full-resolution visible-tile backing, overlays, and brush state. `render_parity_test` passed in 1.76 seconds and proves the exercised global, crop, layer, and mask preview pixels remain within the defined full-resolution export tolerances. `photo_editor_app_qmllint` and the focused diff check also pass.

## 9. Strong Before/After Comparison

**Goal:** Users can judge an edit quickly without changing the recipe or losing their viewing position.

**Status:** `[x]` Complete and proof-gated.

- [x] Support press-and-hold original preview.
- [x] Support split, side-by-side, and snapshot comparison modes.
- [x] Keep zoom and pan synchronized where comparison mode requires it.
- [x] Make the compared states and orientation obvious.
- [x] Restore the edited full-resolution preview immediately after comparison.
- [x] Ensure comparison never writes recipe or history state.

**Completion gate:** Original hold, split, side-by-side, and snapshot comparisons all preserve source position, clearly label the compared states, survive zoom/pan and tool changes, restore the current edited full-resolution result on exit, and leave recipe/history hashes unchanged.

**Evidence:** Studio now supports press-and-hold Original from the canvas toolbar or `B`, Original/external-reference/named-snapshot sources, equal-geometry split, draggable wipe, and synchronized side-by-side layouts. Every surface labels the historical/reference state and `After · Current edit`; wipe orientation explicitly follows the edited-left/before-right geometry. Named snapshots are rendered through a separate complete historical recipe state rather than restoring or writing the current recipe. `editor_preview_before_after_recipe_test` passed in 0.66 seconds and proves global, selection-local, preset-preview, Original, and named-snapshot comparison leave the serialized recipe and chronological history unchanged; it also verifies the comparison request contains the saved snapshot values while the edited request retains current values, and deletion safely clears an active snapshot reference. `editor_preview_feedback_test` passed in 4.21 seconds and proves recipe-matching full-resolution native backing, including cropped output geometry, is preserved across view-only work. The focused offscreen Studio smoke passed in 121.66 seconds with the actual Compare tool: it selected the persistent `Color approved` snapshot, exercised split, wipe, and side-by-side, verified Before/After labels and wipe orientation, synchronized 1.75x zoom plus x/y pan, changed tools, pressed and released Original, settled back to the edited full-resolution result, and compared recipe/history hashes before and after. `photo_editor_app_qmllint` passes. Visual evidence: [Before/After at 1440 x 900](/Users/bradyreiner/.codex/visualizations/2026/07/14/019f624a-dd6a-7291-a8e9-a62170ae5322/before-after-comparison-1440x900.png) and the machine-readable [capture manifest](/Users/bradyreiner/.codex/visualizations/2026/07/14/019f624a-dd6a-7291-a8e9-a62170ae5322/before-after-comparison-manifest.json).

## 10. Safer Import Experience

**Goal:** Before import starts, users understand what will be copied, referenced, skipped, or renamed and where it will live.

**Status:** `[x]` Complete and proof-gated.

- [x] Show source, destination, import mode, file count, file types, and estimated disk usage.
- [x] Detect duplicates and explain the available resolution choices.
- [x] Identify unsupported or unreadable files before committing where possible.
- [x] Preview folder/project organization and filename consequences.
- [x] Keep long imports nonblocking with pause, cancel, retry, and useful progress.
- [x] Provide a completion summary with successes, skips, duplicates, and failures.

**Completion gate:** A mixed import containing duplicates, unsupported files, RAW/JPEG pairs, and an interrupted copy produces an accurate preflight and final summary; cancel leaves no unexplained partial state, retry imports only unresolved files, and managed versus referenced outcomes match the previewed destination behavior.

**Evidence:** Import now opens an explicit preflight before any job is queued. The same controller result used by the job shows source, exact managed or referenced destination, mode, eligible/media/RAW/JPEG counts, selected bytes, additional disk usage, preserved folder/filename behavior, duplicate count and `Skip Existing`/`Replace Existing` choices, plus unsupported and unreadable issues. The review is persisted with the source, eligible paths, mode, project, and duplicate policy, so pause/resume and retry replay the reviewed operation. Import jobs remain asynchronous and expose progress, pause, resume, cancel, and retry in the Import surface; a running pause or cancel terminates the helper at a safe boundary and removes `.virel-partial` artifacts. Managed copies publish each file atomically. A resumed job compares finished destination bytes, reuses exact matches, and copies only unresolved content. The Rust importer emits `import-result:v1`; the job stores successes, resumed files, skips, duplicates, unsupported/unreadable items, and failures for the visible completion summary.

`catalog_asset_model_test` passed against an isolated PostgreSQL instance and proves a successful real importer run persists the structured final summary. Its deterministic mixed preflight covers a repeated/catalog duplicate, unsupported text, missing/unreadable path, RAW/JPEG pair, photo/video counts, managed disk/destination behavior, referenced zero-copy destination behavior, and persistence of the reviewed facts. `job_queue_model_test` proves queued/running import cancellation, queued pause/resume, and failed-import retry with persisted metadata. Six `photo-catalog` importer tests pass, including stale interrupted-partial cleanup, atomic managed copy, exact-match reuse on resume, referenced import, selected-path ordering, RAW/JPEG/audio support, and unsupported-file reporting. `photo_editor_app_qmllint` passes. The named `desktop_shell_import_safety_test` passes in 5.09 seconds with real controller data at 1440 x 900 while requiring the mode, fact, duplicate-resolution, and fixed primary-action controls to be visible. Visual evidence: [managed mixed-media preflight](/Users/bradyreiner/.codex/visualizations/2026/07/14/019f624a-dd6a-7291-a8e9-a62170ae5322/import-safety-preflight-1440x900.png), [referenced leave-in-place preflight](/Users/bradyreiner/.codex/visualizations/2026/07/14/019f624a-dd6a-7291-a8e9-a62170ae5322/import-safety-referenced-1440x900.png), and the machine-readable [capture manifest](/Users/bradyreiner/.codex/visualizations/2026/07/14/019f624a-dd6a-7291-a8e9-a62170ae5322/import-safety-manifest.json).

## 11. Offline and Missing-Media Recovery

**Goal:** A disconnected drive or moved folder becomes a clear, recoverable state rather than a broken catalog.

**Status:** `[x]` Complete and proof-gated.

- [x] Distinguish offline drive, moved file, permission failure, and permanently missing media.
- [x] Keep catalog metadata and available previews visible while originals are offline.
- [x] Offer one clear locate/relink action from Library, Edit, and Export.
- [x] Relink a folder or drive in bulk instead of repairing every asset separately.
- [x] Prevent destructive catalog cleanup from being presented as the default fix.
- [x] Resume blocked preview/export work after a successful relink.

**Completion gate:** A referenced library is imported, its drive is disconnected, and the app relaunches without data loss or modal-error loops; the user can bulk relink the moved folder, all asset identities and edits remain intact, and a previously blocked export resumes successfully.

**Evidence:** The catalog now exposes `drive-offline`, `moved-file`, `permission-denied`, and explicitly confirmed `permanently-missing` states with outcome-based titles and recovery guidance. Offline entries retain their catalog identity, metadata, ratings, edit recipes, and last known cached preview. A persistent, nonmodal recovery banner appears on Library and Edit, with a matching banner on Export; `Relink This` is the primary action, `Relink All` recursively matches a folder tree, `Recheck` handles reconnected sources, and `Mark Missing` requires a second explicit confirmation while preserving the catalog entry and edits. No delete or cleanup action is presented as the default recovery. A successful relink clears the missing state, updates the persisted source path, refreshes preview availability, and automatically requeues failed exports whose original was unavailable while preserving their recipe and destination.

`catalog_asset_model_test` passes a deterministic referenced-media recovery gate with two originals in separate nested folders. It proves cached-preview/metadata/recipe retention while offline, all four availability classifications, confirmation before permanent-missing state, two-file bulk relink, stable asset IDs/ratings/recipes, relink-path persistence after controller relaunch, and a blocked export changing from `Failed` to `Queued` with the recovered source and `resumedAfterRelink` evidence. `photo_editor_app_qmllint` passes with the repository's existing warning baseline. The named `desktop_shell_offline_recovery_test` passes in 5.98 seconds at 1440 x 900 and requires the visible recovery title and recheck, selected relink, bulk relink, and explicitly confirmed missing controls on the live Library, Edit, and Export surfaces. Visual evidence: [Library recovery with cached preview and metadata](/Users/bradyreiner/.codex/visualizations/2026/07/14/019f624a-dd6a-7291-a8e9-a62170ae5322/offline-recovery-library-1440x900.png), [Edit recovery without a modal loop](/Users/bradyreiner/.codex/visualizations/2026/07/14/019f624a-dd6a-7291-a8e9-a62170ae5322/offline-recovery-edit-1440x900.png), [Export recovery beside delivery settings](/Users/bradyreiner/.codex/visualizations/2026/07/14/019f624a-dd6a-7291-a8e9-a62170ae5322/offline-recovery-export-1440x900.png), and the machine-readable [capture manifest](/Users/bradyreiner/.codex/visualizations/2026/07/14/019f624a-dd6a-7291-a8e9-a62170ae5322/offline-recovery-manifest.json).

## 12. Delivery-Focused Export

**Goal:** Export clearly communicates what will be delivered and recovers safely from ordinary failures.

**Status:** `[x]` Complete and proof-gated.

- [x] Provide understandable presets with a concise settings summary.
- [x] Preview destination paths and generated filenames before starting.
- [x] Detect collisions, missing originals, unsupported settings, and unavailable destinations in preflight.
- [x] Show per-item and overall progress without blocking editing.
- [x] Support pause, cancel, retry-failed, and reveal-in-Finder actions.
- [x] Report output profile, dimensions, format, metadata, and sharpening in product language.

**Completion gate:** A 20-photo export with a deliberate filename collision, one missing original, and one temporarily unavailable destination identifies every problem before or during execution, preserves successful outputs, retries only failed items, writes expected profiles/metadata/dimensions, and reveals the completed files in Finder.

**Evidence:** Export now has an `export-batch-preflight:v1` delivery plan that calculates every output path before queueing, checks the destination and each source/recipe/backend, and reports ready, blocked, missing-original, unsupported, unavailable-destination, and collision counts. Existing outputs are never silently overwritten: the review shows the exact numbered filename that will be used, the planned path is persisted with the job, and a new collision appearing after review is resolved safely at execution. The always-visible Delivery Review card shows ready/blocked counts, the first generated filename, total remaining files, destination, and overwrite outcome. The detailed review lists every selected output and its `READY`, `RENAMED`, or `BLOCKED` state. A batch with some blocked items can start only the ready deliveries while blocked items remain visible for correction.

Jobs remain asynchronous and now show per-item progress plus an overall finished/total/failed bar. Export rows expose pause, resume, cancel, retry, and completed-file reveal actions. Pausing a queued export preserves its delivery plan; pausing a running export waits for a safe render boundary, discards the incomplete artifact, and resumes by replaying only that job. Failed-job retry increments only the failed record. Each completed rendered export persists `export-delivery-summary:v1` with the actual output path/name, preset, decoded format, bit depth, dimensions, byte size, output profile, artifact/profile verification, the explicit camera-metadata policy, and output-sharpening policy. Original Copy instead reports byte-for-byte metadata preservation. Completed rows present format, dimensions, preset, profile, output path, and a Finder reveal target in product language.

`export_job_controller_test` passes in 10.50 seconds. Its completion gate builds a 20-photo batch with one missing original, one pre-existing filename collision, and a destination that becomes temporarily unavailable. Preflight reports 19 ready, one blocked missing original, one exact numbered rename, and blocks all 20 against the unavailable destination. Nineteen ready JPEGs complete without changing the pre-existing file; readback proves every artifact is 32 x 24, 8-bit JPEG, sRGB/profile verified, and carries the expected metadata and no-additional-sharpening policies. A twentieth real queued export fails when its destination is a file, succeeds after the destination is restored and only that job is retried, and exposes its completed output as the reveal target. The same test now proves an interrupted export remains paused with `awaiting-confirmation`, makes no automatic external write, completes only after explicit resume, clears the confirmation flag, and records `resumed-after-confirmation`. `job_queue_model_test` passes in 0.70 seconds and proves export pause/resume plus overall progress counts. `photo_editor_app_qmllint` passes with the existing warning baseline. The named `desktop_shell_delivery_export_test` passes in 6.83 seconds at 1440 x 900 while requiring the mixed batch review and completed output/progress row. Visual evidence: [20-photo delivery preflight](/Users/bradyreiner/.codex/visualizations/2026/07/14/019f624a-dd6a-7291-a8e9-a62170ae5322/delivery-export-preflight-1440x900.png), [completed verified delivery](/Users/bradyreiner/.codex/visualizations/2026/07/14/019f624a-dd6a-7291-a8e9-a62170ae5322/delivery-export-completed-1440x900.png), and the machine-readable [capture manifest](/Users/bradyreiner/.codex/visualizations/2026/07/14/019f624a-dd6a-7291-a8e9-a62170ae5322/delivery-export-manifest.json).

## 13. One Coherent AI Edit Surface

**Goal:** AI behaves like a reversible editing assistant, not a model, tool, or automation console.

**Status:** `[x]` Complete and proof-gated.

- [x] Present one primary AI Edit entry point.
- [x] Infer scope and offer a compact override.
- [x] Let users choose, learn, or describe a style without editing schemas.
- [x] Create a small sample set before applying large batch changes.
- [x] Show actual before/after proof and affected photos before commit.
- [x] Keep tool calls, model setup, validation details, and fallback reasons under Details.
- [x] Require confirmation for sensitive retouching, large crops, overwrite, or export actions.
- [x] Keep every uncommitted AI result reversible.

**Completion gate:** Starting from a normal photo selection, a nontechnical user can choose or learn a style, generate samples, inspect real before/after results, apply the approved direction to a batch, review only flagged outliers, commit, and roll back without encountering model/MCP/schema terminology; automated proof confirms the displayed recipe changes and final rendered output are the ones actually applied.

**Evidence:** Library and Edit now route their `AI Edit` affordances into one canonical `AgentWalkthroughDialog`; the unused technical choices are not part of the primary path. The dialog infers a multi-photo selection, shows one compact Current/Selected/Project override, and lets the user describe a result, choose a preset look, or learn from edited reference photos in product language. Its four-page path is target, direction, live work, and review. Tool calls, planner identity, raw validation/failure detail, and fallback evidence are shown only after opening Details.

For a batch larger than two photos, the primary action creates two representative samples and leaves every remaining recipe untouched. Review reads the real `sampleReview` artifact paths, shows the rendered Original and Edited Draft, names the full affected-photo count, and makes `Apply direction to N more` the explicit continuation. Sample feedback re-renders only the samples. After continuation, the review surface lists only flagged outliers and blocks commit until each is fixed, explicitly kept, or reverted. Discard restores every active draft recipe. Large crops and sensitive retouching carry visible confirmation requirements; pre-draft high-risk work stops for Confirm or Decline, final sensitive drafts use `Confirm and apply`, and AI-triggered export/overwrite actions retain their separate explicit confirmation gates.

`agent_edit_controller_test` passes in 56.11 seconds. It proves learned/reference style selection, representative sample generation, untouched remaining photos, sample revision, confirmed apply-to-all, flagged-outlier Fix/Keep/Revert behavior, large-crop and retouch approval, whole-batch rollback, and commit confirmation. Its displayed-versus-applied gate records the exact draft recipe and real rendered After image shown in review, commits it, re-renders the committed photo, and requires both the serialized recipe and every output pixel to be identical. `agent_walkthrough_contract_test` passes in 0.01 seconds and requires the single-walkthrough contract, inferred compact scope, sample continuation, real proof routing, outlier-only panel, confirmation surface, reversible actions, and Details gating. `desktop_shell_ai_edit_ux_test` passes in 5.42 seconds with the real QML component at 1440 x 900, two real before/after image files, 20 affected photos, two reviewed samples, 18 waiting photos, reversible discard, explicit large-crop confirmation, and the approved batch-continuation action. `photo_editor_app_qmllint` passes with the repository warning baseline. Visual evidence: [AI Edit sample review at 1440 x 900](/Users/bradyreiner/.codex/visualizations/2026/07/14/019f624a-dd6a-7291-a8e9-a62170ae5322/ai-edit-sample-review-1440x900.png) and the machine-readable [capture manifest](/Users/bradyreiner/.codex/visualizations/2026/07/14/019f624a-dd6a-7291-a8e9-a62170ae5322/ai-edit-ux-manifest.json).

## 14. Autosave and Crash Recovery

**Goal:** A crash, forced quit, or service interruption does not destroy meaningful work or leave the user unsure what was recovered.

**Status:** `[x]` Complete and proof-gated.

- [x] Autosave catalog and recipe changes without interrupting editing.
- [x] Recover active project, selected asset, workspace, edits, and safe queued work.
- [x] Distinguish recovered, retried, cancelled, and potentially incomplete operations.
- [x] Avoid replaying destructive or externally visible actions without confirmation.
- [x] Offer a concise recovery summary with access to details.
- [x] Provide a repair path when catalog startup or migration fails.

**Completion gate:** The app is force-terminated during an edit, import, and export in separate tests; on relaunch it restores all committed catalog/recipe state, clearly reports incomplete operations, safely resumes or retries supported work, never duplicates exported files or destructive actions, and completes a catalog backup/repair rehearsal.

**Evidence:** Session autosave now uses atomic replacement and records the active workspace, project filter, selected asset, timeline document, current committed edit recipe, edit mode, and UTC save time. Relaunch restores the safe navigation and recipe state through the real catalog/editor path and keeps the recovery record available until the user acknowledges the summary. Existing per-edit recipe persistence and catalog/job writes remain asynchronous, so recovery does not interrupt interaction.

Interrupted operations now have an explicit persisted classification. A pausing import becomes Paused, a canceling operation becomes Canceled, an interrupted import becomes Failed and retryable, and safe work that was already Queued remains queued. An interrupted export or timeline render becomes Paused with `requiresRecoveryConfirmation`; it is never launched automatically because it can write externally visible files. The Job Queue exposes a concise count of waiting, incomplete, and canceled work, a Details list with the exact job/action state, Review Work routing, and a durable acknowledgement without deleting history.

`crash_recovery_relaunch_test` passes in 0.47 seconds and uses three actual child processes that are force-killed only after their durable state is written. The edit relaunch restores the exact workspace, project, asset, and serialized recipe. The import relaunch marks the interrupted reviewed import incomplete while preserving a separate safe queued operation. The export relaunch leaves the pre-existing delivered bytes unchanged, creates no numbered duplicate, reports the delivery as paused, and queues it only after an explicit Resume. `app_shell_controller_test` and `job_queue_model_test` pass alongside it and cover atomic clean/unclean markers, dismissible recovery, classification metadata, summary/detail counts, and explicit repair confirmation.

Managed catalogs now expose real `backup` and `restore-latest` commands. Backups are custom-format `pg_dump` files written through a partial file and atomic rename. Repair selects the newest backup by modification time, creates a pre-repair safety dump when the current catalog is readable, runs `pg_restore --clean --if-exists --exit-on-error`, then re-runs schema initialization and health verification. The app's Catalog Recovery surface offers Retry, Create Backup, and a two-step Repair from Backup / Confirm Repair flow; cancel explicitly states that no catalog data changed. The end-to-end rehearsal created a catalog table and one proof row, made a manual dump, deleted the table, restored the newest dump, and read back exactly `restored-exactly-once:1`; the managed database then stopped cleanly. The focused Rust local-database tests pass.

`desktop_shell_crash_recovery_ux_test` passes in 4.89 seconds at 1440 x 900 and proves a nonmodal `Work recovered` summary, one operation waiting for review, expandable details showing `Paused (awaiting-confirmation)`, Review Work, and Done. `photo_editor_app_qmllint` passes with the repository warning baseline. Visual evidence: [recovery summary at 1440 x 900](/Users/bradyreiner/.codex/visualizations/2026/07/14/019f624a-dd6a-7291-a8e9-a62170ae5322/crash-recovery-summary-1440x900.png) and the machine-readable [capture manifest](/Users/bradyreiner/.codex/visualizations/2026/07/14/019f624a-dd6a-7291-a8e9-a62170ae5322/crash-recovery-manifest.json).

## 15. Accessibility and Personalization

**Goal:** Users with different vision, motor, display, and workflow needs can operate the core app efficiently.

**Status:** `[x]` Complete and proof-gated.

- [x] Support interface scaling without clipped primary controls.
- [x] Provide visible keyboard focus and complete keyboard access to core workflows.
- [x] Allow shortcut inspection and conflict-safe customization.
- [x] Provide high-contrast and color-safe selection/mask overlays.
- [x] Respect reduced-motion preferences.
- [x] Keep pointer targets large enough for repeated professional use.
- [x] Expose accessible names, roles, states, and values for interactive controls.
- [x] Preserve usable layouts in clean, pro, and inspect density modes if those modes ship.

**Completion gate:** The first-run, import, cull, basic edit, local edit, and export acceptance path completes at the supported maximum UI scale using keyboard-only navigation; automated accessibility inspection finds no unlabeled primary control, focus never becomes trapped or invisible, reduced motion removes nonessential transitions, and overlay states remain distinguishable in grayscale and common color-vision simulations.

**Evidence:** Accessibility & Layout preferences now persist 90%, 100%, 115%, and 130% interface scales; clean, pro, and inspect density; standard/high contrast; standard/reduced motion; and standard, protanopia-safe, deuteranopia-safe, tritanopia-safe, and grayscale selection palettes. The shared theme scales typography and control geometry, strengthens keyboard focus and contrast, removes nonessential animation durations under reduced motion, and routes selection operations through color-safe Add/Subtract/Intersect palettes with dual-contrast halos. Shared action, tool, menu, and split-button controls enforce the 32-pixel dense and 44-pixel primary pointer-target contract and expose accessible names, descriptions, roles, checked state, focus, and keyboard activation. Sliders expose their names, descriptions, roles, focused handles, and named value-entry fields.

Shortcut inspection now edits the persisted live action bindings instead of a display-only copy. `AppShellController` rejects duplicate custom sequences, supports per-command reset, and restores accessibility and shortcut defaults. The real workflow also fixes stale text-field focus when switching tools, so a keyboard tool command cannot leave the next tool's shortcuts silently disabled. Library adds pointer-free pick, reject, one-through-five rating, and previous/next commands, plus a named keyboard route from Import back to culling.

`desktop_shell_accessibility_test` passes in 10.81 seconds through the real QML shell at 130% scale. It uses keyboard events to complete Quick Start and the own-photo import choice, return to Library, pick and rate the seeded photo, enter a basic exposure edit, open local editing, create a selection, open its tool menu, reach Export, and create a real completed output file. The run requires accessible names on every primary control it touches, verifies repeated and primary pointer-target sizes, confirms reduced motion disables the selection-outline animation, exercises all three density modes, and captures an unclipped 1440 x 900 local-edit state. It also checks pairwise operation-color distance in every shipped palette, distinct standard-palette luminance in grayscale, and distinct results under protanopia, deuteranopia, and tritanopia simulation matrices. `app_shell_controller_test` passes the persistence, conflict-rejection, reset, and default-restoration gate. `photo_editor_app_qmllint` passes with the repository's existing warning baseline. Visual evidence: [maximum-scale keyboard local edit](/Users/bradyreiner/.codex/visualizations/2026/07/14/019f624a-dd6a-7291-a8e9-a62170ae5322/accessibility-max-scale-local-edit-1440x900.png), [real keyboard-path export](/Users/bradyreiner/.codex/visualizations/2026/07/14/019f624a-dd6a-7291-a8e9-a62170ae5322/first-run-keyboard-export.png), and the machine-readable [accessibility manifest](/Users/bradyreiner/.codex/visualizations/2026/07/14/019f624a-dd6a-7291-a8e9-a62170ae5322/accessibility-manifest.json).

---

## Final Acceptance Run

Do not run this section as a substitute for the item-level gates. Run it after all 15 items have their own proof.

Record the remaining participant and real-media evidence in the [UX Human Acceptance Report](/Users/bradyreiner/projects/PersonalProjects/photo-editor/docs/ux-human-acceptance-report.md).

**Pre-human automated baseline:** A single current-worktree CTest run passed 21 of 21 selected UX acceptance tests in 151.22 seconds. It covered clean-profile/first-run persistence, application bootstrap, 500-photo catalog and culling state, AI draft/review/apply/rollback, source contracts, real-QML AI, crash recovery, import preflight, offline relink, delivery review, maximum-scale keyboard accessibility with a real export, persistent job recovery, forced-process relaunch, history, immediate preview feedback, before/after recipe isolation, local-edit targeting, outline publication, layer-adjustment state, and 20-photo export failure/retry. This baseline is deterministic supporting evidence only; it does not replace the participant sessions or the representative real-media report below.

**Integrated automated workflow:** A separate current-worktree `ux_representative_workflow_test` pass completed in 10.62 seconds and joined the 500-asset catalog, 50-keeper cull state, 20-photo selective batch edit, masked local refinement, exact undo/redo, and 20 verified exports in one run. Its manifest deliberately records `rawDecodeExercised: false`; therefore none of the real-media checks below are marked complete from this synthetic run.

- [ ] Start with a clean app profile.
- [ ] Complete first-run guidance with a real import.
- [ ] Import at least 500 mixed RAW/JPEG assets with duplicates and one unsupported file.
- [ ] Cull the set using the keyboard and produce at least 50 keepers.
- [ ] Apply a selective batch edit to at least 20 photos.
- [ ] Refine one photo with a layer and local selection.
- [ ] Create a snapshot and verify undo/redo/history behavior.
- [ ] Compare original, edited, and snapshot states at a zoomed position.
- [ ] Disconnect or move referenced media, relaunch, and bulk relink it.
- [ ] Export at least 20 photos, exercise one failure, and retry only the failed work.
- [ ] Force quit during a separate safe operation and verify recovery.
- [ ] Repeat the core path with keyboard-only navigation and maximum supported UI scale.
- [ ] Record preview/export parity evidence and the final delivered files.
- [ ] Record a short usability report listing time-to-completion, errors, recoveries, and remaining friction.
- [ ] Mark the overall completion gate only after every result above is attached and reviewed.
