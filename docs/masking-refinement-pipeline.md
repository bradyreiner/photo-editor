# Masking Refinement Pipeline

This document defines the matte refinement order used by preview and render pipeline code.

## Source Stage

Each source computes its own matte first. Source-specific refinement is applied before the source participates in a mask set:

1. Source geometry or raster matte.
2. Raster morphology where available: grow, shrink, feather, blur, smooth, despeckle, fill holes, remove islands.
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
