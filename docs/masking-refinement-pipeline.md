# Masking Refinement Pipeline

This document defines the matte refinement order used by preview and render pipeline code.

## Source Stage

Each source computes its own matte first. Source-specific refinement is applied before the source participates in a mask set:

1. Source geometry or raster matte. Stored raster Feather is prepared on a render copy using three separable box passes. The authored alpha and brush history remain unchanged; zero feather restores them exactly. The full authored brush matte is filtered before crop resampling so crop boundaries do not become false feather boundaries.
2. Raster morphology where available: grow, shrink, blur, smooth, despeckle, fill holes, remove islands. Compiled stored mattes already include spatial Feather, so it is not applied again by the per-pixel morphology evaluator.
3. Source-guided Fine Edge.
4. Range constraints for luminance, color, depth, and edge confidence.
5. Scalar source refinement: Shift Edge, Smooth, Fine Edge, Contrast Edge.
6. Source invert.
7. Source density cap.
8. Source opacity.

The result of this stage is the source contribution used by Add, Subtract, Intersect, Replace, Exclude, and blend-mode composition.

## Mask-Set Stage

After all enabled sources in a mask set are composed, the set matte can be refined separately:

1. Composed set matte.
2. Set Shift Edge.
3. Set Smooth.
4. Set Fine Edge.
5. Set Contrast Edge.
6. Set invert.
7. Set density cap.
8. Set opacity.

Invert is intentionally after matte refinement and composition, but before density and opacity. Density and opacity never change the shape computation; they only limit or scale the already-refined matte strength.

## Editing and preview

The Studio edge controls name their target. Adding a refine brush keeps these controls on the established non-brush source in the same set; a brush-only selection uses its first painted source. This keeps an empty newly appended brush from swallowing edits intended for the existing selection. The active paint component remains selected while edge values change. Density, Opacity, and Invert update the composed selection-set settings across all its components, so a newly added empty brush does not absorb strength edits. Brush-tip feather remains separate from mask-edge feather.

Shift Edge expands or contracts hard rectangle, ellipse, radial, polygon, and lasso geometry, rather than remapping only fractional alpha. Smooth samples neighboring geometric coverage; Fine Edge and Contrast Edge operate on the resulting edge coverage. Raster morphology retains its source-pixel neighborhood behavior.

Overlay and Mask previews bind to the asynchronous mask-preview worker. Editing edges switches an outline/hidden preview to Overlay and selects the refined result. Outline remains available explicitly. Mask mode uses a black backdrop and full-strength white alpha independently of the overlay-opacity setting. The preview and rendered layer masks use the same compiled source matte.

Reset edges clears the selected edge target's refinement without deleting brush strokes, detaching a layer mask, or changing mask inversion/strength. Locked target sources and locked owning layers reject refinement.

## Selection delivery and view state

Selection target navigation keeps the working selection session, brush, and panel context active. A pending stroke settles before a queued target switch; explicit inspector editing still commits the session. Canceling refinement keeps the most recently chosen target.

Spatial Feather is displayed in source pixels: Gaussian sigma for stored/painted mattes, inward width along the shorter shape axis for rectangles/ellipses, and an approximate short-image-axis width for polygons/lassos. Range-source softness remains a percentage because it describes a value range. Recipes retain their existing normalized feather fields. View includes 100% inspection, light/dark previews, and an optional edge-aware refinement brush.

Independent cutouts show a bounded, color-managed result preview with checker, light, and dark backgrounds. Returning to the composition or pressing Escape dismisses view state only. A subsequent layer edit dismisses stale preview pixels. The preview does not solo layers or add undo entries.

Export cutout PNG renders a captured selection/source snapshot asynchronously, trims only fully transparent outer borders, and writes tagged sRGB PNG with alpha. Exporting a materialized result uses its baked pixels so feather is not applied twice. Export leaves the selection, layers, history, and original file intact.

A guarded cutout path evaluates a single geometric mask without decoding/converting source RGB again. It uses the same engine mask-set rasterizer and excludes RAW, crops, oriented/unsupported image readers, source-sensitive range constraints, and Fine Edge/decontamination. These cases use the existing canonical source-pixel path. Pixel parity and both sparse/dense performance guards are required when changing this path.
