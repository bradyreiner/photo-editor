# Next exact ROI seam

## What the current graph does

`ensureTitleFrame()` rasterizes title/caption text and shapes into a full sequence-size `QImage` (`apps/desktop/src/timeline_document_controller.cpp`). This protects absolute title placement and the QPainter layout; the PNG itself often has a small nontransparent region.

`timelineRenderArguments()` passes each title PNG to FFmpeg as an independent looping input. `clipVideoFilter()` then treats the image as a full-canvas source: source crop/color handling, frame-mode scale, corner-radius mask or quad warp, optional frame shadow, dynamic scale and rotation, a multi-frame transparent safety pad, and crop back to the target canvas. Clip effects and masks, opacity, track/group transform, and sequence masks follow before the full-size clip output is overlaid in z-order. The early crop+pad experiment restored the full canvas before these operations and measured worse time and memory.

For the 44–45 s interval in the captured snapshot there are 112 active title inputs and 111 unique PNGs. 111 unique inputs have sparse alpha; one is fully opaque across all 2,073,600 pixels. The 100 blank-text shape clips include 64 uniquely filled mosaic cells. Across the 112 active titles, persisted keyframe channel counts are: `frameOffsetX` 9,059; `frameOffsetY` 9,100; `frameScale` 3,278; `opacity` 1,771; and `frameCornerRadius` 88. Thus most grid cells move and scale independently; merging their curves or assuming a shared transform would change the edit.

Only 10 of 112 active titles satisfy a conservative static/opacity-only eligibility screen (stretch, full-frame geometry, no geometry keyframes, no masks, shadows, or effects). That optimization would be exact and low-risk for those titles, but it would not address most of the dense moving grid.

## Smallest exact path worth prototyping

Keep the authored clips and their keyframes intact. Introduce a sparse-region value for the title source (`RGBA pixels + original canvas size + integer origin`) and carry it through a specialized affine-title path to the final timeline overlay. The first useful implementation should support only the P7 grid subset with stretch mode, full-frame `frameWidth`/`frameHeight`, no source crop, no rotation, no perspective/quad warp, no shadow, no clip/source/sequence mask, and no effects. Keep `frameOffsetX`, `frameOffsetY`, `frameScale`, and `opacity` as independent authored curves.

For each render segment, derive a conservative output ROI from the source alpha box transformed at every output frame by the existing native parameter evaluator. Expand it for the resampler support. Render pixels in that ROI in the same global pixel coordinate system, then feed the small frame to the sequence compositor with the computed integer origin. Keep the fully opaque source and any unsupported geometry on the current renderer. Do not replace per-clip transforms with shared row/column transforms.

This path must first prove parity for fractional per-frame scale/offset and alpha edges against the current full-canvas FFmpeg graph. A candidate ROI resampler must preserve the current scale filter's global sampling phase; scaling a crop independently can differ at edges or when output dimensions round differently. Add masks, corner radius, shadow, blur, perspective, rotation, or group transforms only with dedicated pixel fixtures that include their full filter support/padding.

## Evidence boundary

The frame hashes prove the crop+pad output pixels match in the one-frame isolated overlay test. The measured time/RSS comparison disproves a performance benefit for padding the crop back to full canvas before transforms. This is not a full native export measurement. Source snapshot identity is recorded in `measurements.json`; source PNG SHA-256 values and exact input order are in `fixtures/`.

## Coordinate and clock details

For the common P7 cell case (`stretch`, source and target both 1920×1080, no source crop, no rotation/quad/shadow), `clipVideoFilter()` scales the full image to `scaled_width × scaled_height`, pads it by `2W × 2H` with the image at `(W,H)`, then crops `W × H` at `((scaled_width + W)/2 - offset_x, (scaled_height + H)/2 - offset_y)`. Therefore a source pixel at `(x,y)` maps to the destination as:

```text
x' = x * scaled_width / W + (W - scaled_width) / 2 + offset_x
y' = y * scaled_height / H + (H - scaled_height) / 2 + offset_y
```

The current `offset_x` / `offset_y` include the authored `frameOffsetX/Y` value multiplied by one quarter of the canvas dimension, animated title-position deltas, and caption emphasis offsets. The dimensions include `frameScale`, title-font-size scaling, and caption emphasis scaling. This is a geometric starting point only: an ROI implementation must match FFmpeg’s integer dimension rounding, scale sampling phase, crop coordinate rounding, and alpha edge behavior exactly.

The renderer's visual `cut_points` are generally clip boundaries and retime/caption-word boundaries; ordinary animation keyframes are evaluated inside a segment. For an exact fixed ROI per segment, calculate each rendered output frame on the global sequence `n/fps` clock with the existing curve semantics, transform the original alpha bounds, expand for the exact resampler/effect support, and union the resulting integer pixel regions. Do not use only first/last keyframe values or assume frame segments stop at each animation key.

A first parity fixture should cover noninteger scale plus fractional positive/negative offsets at multiple sequence frames, translucent antialiased edges, and an ROI that crosses the canvas edge. It should compare the final 1920×1080 RGBA composite to the existing full-canvas path. Only then should the renderer feed the region to its final sequence overlay. Keep full-frame fallback for the opaque source and for any unsupported crop, corner treatment, shadow, effects, masks, perspective, rotation, source matte/cutout, or transformed bounds that cannot be proven.
