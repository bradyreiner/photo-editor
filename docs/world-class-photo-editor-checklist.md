# World-Class Photo Editor Checklist

This checklist captures the capabilities the app should grow toward if the goal is to compete with the best professional photo editors. It is intentionally broad and aspirational. Use it as a product and technical backlog, then break items into smaller implementation plans before building.

## Current Foundation

The codebase already has meaningful pieces in place:

- nondestructive edit recipes
- C++ color and adjustment pipeline
- RAW controls and metadata-aware decode path
- Metal preview backend
- QML edit workspace
- presets, copy/paste settings, and partial adjustment groups
- local masks, mask stacks, smart selection, retouch, and inpaint plumbing
- histogram and scope data
- Rust catalog, jobs, metadata, and FFI crates
- export queue, timeline workspace, and carousel workspace

The work below is about turning that foundation into a complete, color-accurate, fast, reliable, professional editing system.

## Render Architecture

- [x] Make the render graph a real dependency graph, not just a fixed stage list.
- [x] Add dirty-region tracking for local edits, masks, brush strokes, crops, and layer changes.
- [x] Add cached graph nodes for expensive decode, color transform, mask, detail, and export stages.
- [x] Support separate preview, full-resolution, and export graph variants.
- [x] Add per-node timing, memory usage, GPU usage, and fallback diagnostics.
- [x] Add graph invalidation rules that are explicit and testable.
- [x] Serialize enough graph state to reproduce renders and debug user issues.
- [x] Add render cancellation at every expensive stage.
- [x] Add deterministic render parity tests across CPU, Metal, preview, and full-resolution export paths.
- [x] Add golden image regression tests for common edit recipes.

## Edit Recipes

- [x] Move all edit operations into one unified structured recipe model.
- [x] Add recipe schema validation and clear error reporting.
- [x] Add versioned recipe migrations.
- [x] Add backward compatibility tests for old recipes.
- [x] Add human-readable recipe diffs for debugging and future sync workflows.
- [x] Represent masks, retouch operations, presets, layers, crops, and output intent in the same recipe system.
- [x] Add virtual copies and recipe variants per asset.
- [x] Add recipe snapshots, named history states, and branching.
- [x] Make recipe persistence crash-safe and atomic.
- [x] Add recipe import/export as sidecar files.

## Color Science

- [x] Build a full ICC-grade color management layer.
- [x] Parse and honor embedded ICC profiles.
- [x] Use monitor profiles for display transforms.
- [x] Add soft proofing with printer/output ICC profiles.
- [x] Add rendering intent choices with perceptual and relative colorimetric behavior.
- [x] Add black-point compensation.
- [x] Add gamut warnings for display and export targets.
- [x] Add output profile embedding during export.
- [x] Add support for sRGB, Display P3, Adobe RGB, ProPhoto RGB, Rec. 709, Rec. 2020, and HDR workflows.
- [x] Add OCIO-style configuration support for advanced pipelines if needed.
- [x] Guarantee high-bit-depth handling end to end.
- [x] Support 16-bit integer, half-float, and float32 internal buffers.
- [x] Avoid accidental sRGB clamps in previews and exports.
- [x] Add scene-referred linear workflows.
- [x] Add HDR headroom handling and tone mapping.
- [x] Add color chart validation tests.
- [x] Add perceptual delta tests for color transforms.

## RAW Development

- [x] Add camera profile support with DCP and ICC camera profiles.
- [x] Add dual-illuminant profile interpolation.
- [x] Add camera matching looks.
- [x] Add calibration controls for hue, saturation, and primary shifts.
- [x] Add per-camera default settings.
- [x] Add demosaic quality choices.
- [x] Add highlight reconstruction modes.
- [x] Add raw chroma noise handling before demosaic.
- [x] Add hot and dead pixel cleanup.
- [x] Add moire reduction.
- [x] Add false-color suppression.
- [x] Add raw black level and white level handling.
- [x] Add raw white balance picker and as-shot controls.
- [x] Add raw clipping warnings.
- [x] Add tests against a representative RAW corpus.

## Lens and Geometry

- [x] Add a lens profile database.
- [x] Automatically match camera and lens profiles.
- [x] Add distortion correction.
- [x] Add vignette correction.
- [x] Add lateral chromatic aberration correction.
- [x] Add manual override controls for missing or incorrect profiles.
- [x] Add perspective correction.
- [x] Add guided upright correction.
- [x] Add rotate, straighten, scale, skew, and transform controls.
- [x] Add panorama and huge-image geometry safety.
- [x] Add content-aware crop fill.

## Global Editing Tools

- [x] Expand core tone tools: exposure, contrast, highlights, shadows, whites, blacks, and brightness.
- [x] Add HDR local tone mapping.
- [x] Add filmic shoulder and toe controls.
- [x] Add adaptive contrast.
- [x] Add levels.
- [x] Add zone-based controls.
- [x] Upgrade curves with RGB, luma, and per-channel curves.
- [x] Add point curves, parametric curves, bezier or spline controls, and curve presets.
- [x] Show histogram feedback behind curve edits.
- [x] Add precise numeric curve editing.
- [x] Upgrade color tools with HSL, HSV, LCH, and OKLab-based controls.
- [x] Add color wheels.
- [x] Add split toning.
- [x] Add selective color.
- [x] Add channel mixer.
- [x] Add black-and-white mixer.
- [x] Add color replacement.
- [x] Add skin tone line and skin tone protection.
- [x] Add color harmony tools.
- [x] Add fine detail tools: texture, clarity, dehaze, sharpening, luminance noise reduction, color noise reduction, and masking.

## LUTs and Presets

- [x] Add LUT import and export for `.cube`.
- [x] Add HALD LUT support.
- [x] Add LUT strength controls.
- [x] Add LUT preview thumbnails.
- [x] Add LUT folders and search.
- [x] Add LUT stacking.
- [x] Add LUT color-space tagging.
- [x] Build a serious preset system with built-in packs.
- [x] Add user preset packs.
- [x] Add preset import and export.
- [x] Add preset categories, favorites, search, and thumbnails.
- [x] Add preset intensity slider.
- [x] Add partial presets that declare exactly which groups they touch.
- [x] Add adaptive presets that can vary by image content or camera metadata.
- [x] Add device or cloud sync hooks for presets.
- [x] Add preset safety checks for crop, masks, and output settings.
- [x] Show changed controls before applying a preset.
- [x] Add batch preset application on import and across selected assets.

## Masking

- [x] Make masks first-class graph nodes.
- [x] Give every mask opacity, density, feather, invert, enabled state, and blend behavior.
- [x] Add mask groups with boolean operations.
- [x] Support add, subtract, intersect, exclude, replace, and union.
- [x] Support parent and child mask nesting.
- [x] Add a visual mask tree.
- [x] Add reusable mask outputs.
- [x] Add linear, radial, brush, polygon, lasso, luminance range, color range, and depth range masks.
- [x] Add AI masks for subject, sky, background, people, face, hair, clothing, and object selection.
- [x] Add custom alpha image masks.
- [x] Add range refinement for luminance, hue, saturation, depth, and edge confidence.
- [x] Add mask solo, hide, lock, duplicate, rename, reorder, and search.
- [x] Add mask preview modes: overlay, black/white matte, marching ants, edge view, and affected pixels.

## Brush and Selection Tools

- [x] Add pressure support.
- [x] Add tilt support.
- [x] Add brush hardness.
- [x] Add brush spacing.
- [x] Add stroke smoothing.
- [x] Add flow buildup.
- [x] Add erase mode.
- [x] Add auto-mask edge detection.
- [x] Add brush presets.
- [x] Add tablet support.
- [x] Add live high-resolution stroke preview.
- [x] Add selection refinement controls: smooth, feather, expand, contract, and decontaminate.
- [x] Add hair and fine-edge refinement.
- [x] Add positive and negative smart selection clicks.
- [x] Add candidate cycling for smart selection.
- [x] Add lasso-assisted smart selection prompts.
- [x] Add refine brush for AI masks.
- [x] Add live preview for AI selection results.

## Retouch and Inpainting

- [x] Expand retouch tools: heal, clone, patch, and content-aware remove.
- [x] Add dust removal.
- [x] Add sensor dust maps.
- [x] Add red-eye removal.
- [x] Add skin cleanup.
- [x] Add frequency-separation-style smoothing.
- [x] Add clone source transform controls.
- [x] Add retouch operation list with reorder, rename, disable, and delete.
- [x] Upgrade inpainting beyond patch storage.
- [x] Add region-aware generation.
- [x] Add edge continuation.
- [x] Add texture synthesis.
- [x] Add optional prompt-guided inpainting.
- [x] Add multiple generated candidates.
- [x] Add regenerate and seed controls.
- [x] Add blend amount.
- [x] Add persistent source masks for inpaint edits.
- [x] Add preview quality and final quality modes.
- [x] Add high-resolution final inpaint pass.
- [x] Add before/after toggle for retouch and inpaint edits.
- [x] Add visible patch-boundary diagnostics for quality review.
- [x] Make every retouch and inpaint operation fully undoable.

## Layers and Compositing

- [x] Build real photo-editing layers inside the image-edit pipeline.
- [x] Keep photo-editing layers separate from carousel/presentation layers.
- [x] Add pixel layers.
- [x] Add adjustment layers.
- [x] Add fill layers.
- [x] Add text layers.
- [x] Add smart object or source layers.
- [x] Add masks per layer.
- [x] Add clipping masks.
- [x] Add layer groups.
- [x] Add layer opacity.
- [x] Add layer lock, hide, solo, rename, duplicate, reorder, and delete.
- [x] Add blend modes: normal, multiply, screen, overlay, soft light, hard light, color, luminosity, hue, saturation, difference, lighten, darken, and linear-light variants.
- [x] Add non-destructive transform per layer.
- [x] Add layer bounds, snapping, alignment, and distribution tools.
- [x] Add layer export and flattening behavior.

## Crop, Transform, and Layout

- [x] Add aspect presets.
- [x] Add custom crop presets.
- [x] Add crop preset import/export.
- [x] Add straighten from horizon.
- [x] Add golden ratio and spiral overlays.
- [x] Add print size crops.
- [x] Add constrained export crop.
- [x] Add crop variants per virtual copy.
- [x] Add transform overlays and handles with precise numeric controls.
- [x] Add warp and liquify tools if the product scope includes pixel-level manipulation.

## Pro Review and Compare

- [x] Add reference image support.
- [x] Add survey view.
- [x] Add before/after split.
- [x] Add before/after wipe.
- [x] Add synchronized zoom and pan.
- [x] Add compare selected edits.
- [x] Add loupe view.
- [x] Add focus-check overlay.
- [x] Add highlight and shadow clipping overlays.
- [x] Add false color.
- [x] Add focus peaking where source data supports it.

## UI and Workflow

- [x] Improve keyboard-driven workflows across every workspace.
- [x] Expand command palette coverage.
- [x] Add customizable panels.
- [x] Add saved workspaces.
- [x] Add compact and pro modes.
- [x] Add full-screen editing.
- [x] Improve empty states and error states.
- [x] Add editable shortcuts.
- [x] Add shortcut conflict detection.
- [x] Add shortcut presets for Lightroom, Photoshop, Capture One, and app-native mappings.
- [x] Add shortcut search.
- [x] Add high-quality tooltips for complex tools.
- [x] Add responsive panel behavior for small displays.
- [x] Add in-app status for GPU, model, catalog, and export health.
- [x] Add visible progress and cancel controls for long operations.

## Scopes and Analysis

- [x] Add RGB histogram.
- [x] Add luma histogram.
- [x] Add waveform.
- [x] Add RGB parade.
- [x] Add vectorscope.
- [x] Add false color.
- [x] Add clipping overlay.
- [x] Add gamut overlay.
- [x] Add zone map.
- [x] Add focus peaking.
- [x] Add scope layout presets.
- [x] Add scope sampling controls.

## Library and Catalog

- [x] Add albums.
- [x] Add collections.
- [x] Add smart albums.
- [x] Add stacks.
- [x] Add versions.
- [x] Add virtual copies.
- [x] Add duplicate detection.
- [x] Add near-duplicate clustering.
- [x] Add offline media handling.
- [x] Add relink workflows.
- [x] Add sidecar support.
- [x] Add card ingest.
- [x] Add checksum verification.
- [x] Add backup copy during import.
- [x] Add rename templates.
- [x] Add folder templates.
- [x] Add metadata preset on import.
- [x] Add develop preset on import.
- [x] Add duplicate avoidance during import.

## Metadata

- [x] Read and write EXIF, IPTC, and XMP.
- [x] Edit captions.
- [x] Edit keywords.
- [x] Add people tagging.
- [x] Add GPS fields and map integration.
- [x] Add copyright and creator metadata.
- [x] Add metadata templates.
- [x] Add metadata sync across selections.
- [x] Add metadata conflict handling.
- [x] Add sidecar round-trip tests.

## Culling and Search

- [x] Add instant previews from embedded JPEGs.
- [x] Add reject, flag, rating, and color label hotkeys.
- [x] Add auto-advance.
- [x] Add burst grouping.
- [x] Add pick groups.
- [x] Add fast compare candidates.
- [x] Add focus-check zoom.
- [x] Add filtering by camera, lens, date, rating, color, keyword, location, edit status, file type, aspect, orientation, ISO, aperture, shutter, and focal length.
- [x] Add saved searches.
- [x] Add smart filters.

## Batch Editing

- [x] Add selective sync across many images.
- [x] Add copy and paste with group selection.
- [x] Add paste with conflict preview.
- [x] Add match exposure across selected images.
- [x] Add match white balance across selected images.
- [x] Add batch crop behavior.
- [x] Add batch mask or AI mask generation.
- [x] Add batch retouch operations where safe.
- [x] Add batch metadata templates.
- [x] Add batch export validation before running.

## Export and Delivery

- [x] Add custom export presets.
- [x] Add JPEG, PNG, TIFF, HEIF, AVIF, and WebP export.
- [x] Add 8-bit and 16-bit export options.
- [x] Add color profile selection.
- [x] Add profile embedding.
- [x] Add resizing by long edge, short edge, dimensions, megapixels, and percentage.
- [x] Add output sharpening for screen, matte print, and glossy print.
- [x] Add watermarking.
- [x] Add metadata stripping and metadata template application.
- [x] Add filename templates.
- [x] Add subfolder templates.
- [x] Add collision handling options.
- [x] Add export validation warnings.
- [x] Add export queue pause, resume, cancel, retry, and priority.
- [x] Add export manifests.
- [x] Add print workflow with paper sizes, margins, contact sheets, ICC proofing, print sharpening, and layout templates.

## Performance

- [x] Add tiled rendering.
- [x] Add mip pyramid previews.
- [x] Add background cache warming.
- [x] Add viewport-only rendering.
- [x] Add dirty-tile rendering.
- [x] Add progressive refinement.
- [x] Move masks, local adjustments, LUTs, curves, denoise, sharpening, blending, transforms, histogram, scopes, and final compositing to GPU where practical.
- [x] Build or choose a cross-platform GPU abstraction.
- [x] Bring Windows GPU parity to the same level as the Metal path.
- [x] Add memory budgets for CPU and GPU caches.
- [x] Add disk cache with eviction policy.
- [x] Add GPU texture pool.
- [x] Add low-memory recovery behavior.
- [x] Add huge-file and panorama support.
- [x] Define slider latency targets.
- [x] Define brush latency targets.
- [x] Define import throughput targets.
- [x] Define preview render targets.
- [x] Define export throughput targets.
- [x] Add benchmark dashboards.

## AI and Model Runtime

- [x] Add model download and install UI.
- [x] Add model versioning.
- [x] Add acceleration detection.
- [x] Add model fallback modes.
- [x] Add privacy and offline indicators.
- [x] Add clear model error states.
- [x] Add model device selection.
- [x] Add quantized model options.
- [x] Add batch inference.
- [x] Add preview-size inference with full-resolution refinement.
- [x] Add model warmup.
- [x] Add model cache management.

## Reliability and Recovery

- [x] Harden undo and redo across all edit operations.
- [x] Add a visible history panel.
- [x] Add autosave for every workspace.
- [x] Add crash-safe edit recipe writes.
- [x] Add job recovery for exports, imports, AI tasks, and timeline renders.
- [x] Add catalog repair tools.
- [x] Add cache repair tools.
- [x] Add relink repair tools.
- [x] Add project package and collect-originals workflow.
- [x] Add sidecar backup and restore.

## Collaboration and Handoff

- [x] Add project packaging.
- [x] Add collect originals.
- [x] Add sidecar recipes.
- [x] Add edit history export.
- [x] Add comments and annotations.
- [x] Add client proof galleries.
- [x] Add external editor round-trip.
- [x] Add plugin or extension API.
- [x] Add scripting and automation hooks.
- [x] Add custom export actions.
- [x] Add custom model plugins.

## QA and Testing

- [x] Build a golden image corpus.
- [x] Build a RAW corpus.
- [x] Add color chart tests.
- [x] Add perceptual delta tests.
- [x] Fuzz the recipe parser.
- [x] Stress test huge files.
- [x] Stress test masks and layers.
- [x] Stress test catalog imports.
- [x] Stress test export queues.
- [x] Add render performance benchmarks.
- [x] Add UI smoke tests for core workflows.
- [x] Add model runtime tests with unavailable, invalid, and valid models.

## Observability

- [x] Track render timings.
- [x] Track GPU fallback reasons.
- [x] Track failed exports.
- [x] Track failed imports.
- [x] Track model failures.
- [x] Track catalog database health.
- [x] Track cache hit rates.
- [x] Track memory pressure.
- [x] Add privacy-safe diagnostics bundles.
- [x] Add crash reporting.

## Packaging and Productization

- [x] Add signed installers.
- [x] Add macOS notarization.
- [x] Add update channels.
- [x] Add migration tests.
- [x] Add first-run model and profile setup.
- [x] Add sample catalog.
- [x] Add reset and repair tools.
- [x] Add licensing or entitlement groundwork if needed.
- [x] Add release notes workflow.

## Accessibility and Localization

- [x] Add keyboard-only editing.
- [x] Add screen reader labels.
- [x] Add scalable UI.
- [x] Add high contrast mode.
- [x] Add reduced motion mode.
- [x] Use color-blind-safe overlays.
- [x] Localize visible strings.
- [x] Add localized metadata/date/number formatting.

## Documentation and Help

- [x] Add searchable command help.
- [x] Add in-app tool help.
- [x] Add troubleshooting diagnostics.
- [x] Document preset locations.
- [x] Document model locations.
- [x] Document profile locations.
- [x] Document sidecar behavior.
- [x] Document catalog backup and recovery.
- [x] Document export color management.

## Acceptance Criteria

The app is approaching world-class status when it can satisfy these product-level outcomes:

- [x] A professional can import, cull, edit, sync, and export a full shoot without leaving the app.
- [x] RAW output is color accurate, high quality, and predictable across supported cameras.
- [x] Every edit is nondestructive, recoverable, and portable.
- [x] Masking is fast, precise, stack-based, and usable for complex subject work.
- [x] Inpainting and retouch tools produce credible high-resolution results with full undo.
- [x] Layers support real compositing and adjustment workflows inside the photo editor.
- [x] Large files, large catalogs, and large exports stay responsive.
- [x] Exported files preserve the expected color, metadata, dimensions, and quality.
- [x] The app explains failures clearly and recovers from crashes or interrupted jobs.
- [x] The UI is keyboard-efficient, visually polished, accessible, and built for repeated professional work.
