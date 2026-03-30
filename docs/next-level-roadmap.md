# Next-Level Roadmap

## Product direction

The app already has a strong native foundation:

- multi-workspace desktop shell
- local-first catalog direction
- nondestructive edit recipes
- preview-driven editor and export queue

What it still needs is one workflow that is clearly better than fragmented alternatives.

The near-term product bet should be:

`import -> cull -> batch edit -> export`

This is the path most likely to make the app feel like a real professional tool instead of a promising scaffold.

## Strategic rules

1. Optimize for full-shoot throughput, not isolated features.
2. Prioritize photo workflow depth before expanding video ambitions.
3. Prefer keyboard-first and batch-first workflows anywhere professionals repeat actions.
4. Add AI only where it shortens real work.
5. Keep every new workflow grounded in the existing recipe, catalog, and export architecture.

## Priority stack

### 1. Serious culling workflow

This is the highest-leverage product addition.

Required outcomes:

- star ratings, flags, rejects, and color labels
- fast left-right review with no modal friction
- compare and survey views
- burst grouping and near-duplicate clustering
- focus-check zoom and instant loupe
- keyboard-first culling flow

Primary implementation targets:

- `apps/desktop/qml/workspaces/LibraryView.qml`
- `apps/desktop/src/catalog_asset_model.h`
- `apps/desktop/src/catalog_asset_model.cpp`
- `rust/crates/catalog/migrations/`
- `rust/crates/catalog/src/`

Why this matters:

- it turns the library into a production workspace
- it makes the app useful before the editor reaches full parity
- it creates the selection foundation for later batch edit workflows

### 2. Real batch editing

After culling, the next differentiator is whole-shoot speed.

Required outcomes:

- copy and paste settings
- sync selected adjustments across many images
- partial sync by adjustment group
- preset save, load, and overwrite
- apply preset to multi-selection

Primary implementation targets:

- `apps/desktop/src/editor_preview_controller.h`
- `apps/desktop/src/editor_preview_controller.cpp`
- `apps/desktop/src/edit_recipe_state.h`
- `engine/include/photo_editor/pipeline/edit_recipe.h`
- `rust/crates/catalog/src/bin/save_edit_recipe.rs`

Why this matters:

- without batch operations, a photo editor feels like a single-image demo
- this builds directly on the existing nondestructive recipe model

### 3. Real mask stack and selection acceleration

The current mask tooling is still scaffold-heavy. It needs to become a first-class editing system.

Required outcomes:

- multiple masks per image
- add, subtract, and intersect operations
- brush, linear, and radial masks
- luminance and color range masks
- per-mask local adjustments
- AI-assisted subject, sky, and object masks

Primary implementation targets:

- `apps/desktop/qml/workspaces/MaskView.qml`
- `apps/desktop/qml/workspaces/EditWorkspace.qml`
- `apps/desktop/src/editor_preview_controller.h`
- `apps/desktop/src/editor_preview_controller.cpp`
- `engine/include/photo_editor/pipeline/edit_recipe.h`
- `engine/src/pipeline/color_adjustments.cpp`

Why this matters:

- advanced masking is where pro editors start to separate from entry-level tools
- the current recipe model already points in this direction, but it needs structural follow-through

### 4. Output quality features

Once the workflow is fast, image quality must hold up.

Required outcomes:

- crop and geometry parity between preview and export
- lens correction
- chromatic aberration correction
- sharpening
- noise reduction
- export presets tuned for proofing, web, and delivery

Primary implementation targets:

- `engine/src/pipeline/render_graph.cpp`
- `engine/src/pipeline/color_adjustments.cpp`
- `apps/desktop/src/export_job_controller.cpp`
- `apps/desktop/qml/workspaces/EditView.qml`
- `apps/desktop/qml/workspaces/ExportView.qml`

Why this matters:

- professionals judge editors by final output quality, not control count
- this is the line between "good preview" and "deliverable image"

### 5. Catalog reliability and recovery

This should become a product differentiator because the architecture already supports it.

Required outcomes:

- missing-file and offline-drive visibility
- relink workflows for moved folders and drives
- managed and referenced import confidence
- duplicate detection during import
- dependable export recovery and retry

Primary implementation targets:

- `engine/src/catalog/asset_location.cpp`
- `rust/crates/catalog/src/local_database.rs`
- `rust/crates/catalog/src/importer.rs`
- `docs/import-workflow.md`
- `apps/desktop/qml/workspaces/LibraryView.qml`

Why this matters:

- photographers trust apps that survive messy storage reality
- this fits the repo's local-first, multi-drive product direction

## Signature differentiator

If one higher-order feature is added after the core workflow is credible, it should be `shoot intelligence`.

That means:

- burst ranking
- best-shot suggestions
- duplicate and near-duplicate grouping
- suggested sync groups by camera, lens, and lighting setup
- AI masks as accelerators inside the editing workflow

This is a better fit for the current product than generative editing features.

## Explicit deprioritization

The timeline and video workspace can remain in the repo, but it should not drive near-term product prioritization unless the product strategy changes.

Reason:

- the photo workflow still needs depth in culling, batch editing, masking, and reliability
- shipping a hybrid media product too early will dilute execution across two hard categories

## Recommended delivery order

1. Culling workflow
2. Batch editing and presets
3. Mask stack and AI-assisted selections
4. Lens/detail/output quality
5. Catalog relink and recovery

For the concrete 20-task execution sequence, see [docs/next-level-execution-backlog.md](/Users/bradyreiner/projects/PersonalProjects/photo-editor/docs/next-level-execution-backlog.md).

## Success test

The roadmap is working when a photographer can:

1. import a shoot
2. rapidly reject and rate images
3. sync a base grade across a selected set
4. refine keepers with masks and crop
5. export dependable client-ready files

If the product cannot do that smoothly, new side features are lower priority.
