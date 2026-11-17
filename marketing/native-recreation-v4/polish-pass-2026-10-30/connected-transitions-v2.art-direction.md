# Connected transition proposal v2 — RAW → Library → Canvas → Carousel

Status: native-authoring proposal only; unbound and not rendered. The saved pass3 JSON supplies IDs, timing, base geometry and keyframe-count preconditions. It is not live execution state. Root must bind the exact current ready native document hash and confirm every target against fresh inspection before execution.

## RAW → Library (57–64s): one continuous move into an adjacent-frame match-cut

Keep the existing broad delivery preview at 57–62s and the existing `IMG_4969` Library card at 62–67s. Hold the outgoing preview through local 3s, then use one native ease-in-out gesture to land on the incoming card’s local-0 geometry on the outgoing clip’s final rendered frame at 61.9667s. Hold the outgoing frame fully opaque through that last frame. Make only the incoming card opacity channel fully visible from local 0; leave its existing Y and rotation entrance curves unchanged. The first incoming frame at 62s therefore continues the existing geometry entrance with no alpha dip or timing change.

At the adjacent-frame cut, both clips use X=-0.095, Y=-0.183657, and rotation=-18°. The outgoing preview is scaled to .307 and constrained by a sequence-space rectangle mask to the same card bounds. The assets are different crops/renditions of the same boats photograph, so matching bounds and angle do not prove a pixel-perfect match. Inspect the pair of native frames and adjacent motion for a one-frame crop/content jump.

## Canvas → Carousel (157–165s): preserve entrance, bridge only the late move

Keep the existing shoe poster and all three panorama images. Preserve the shoe’s saved geometry and mask keys strictly before local 8.5s, including the sampled local-1.4–2.8s entrance and its held poster pose. The shoe operation passes those existing keys through unchanged, retains the exact key values at local 8.5s, and changes only the boundary interpolation to start one ease-in-out gesture from local 8.5 to the first-panel pose at local 9.5. It holds that pose to the clip end. Its existing opacity channel is omitted and remains unchanged.

The pass3 panel-1 local-0 right edge is `.3717708333`; the shoe aperture ends at that actual edge. The three existing `IMG_5071` clips reveal through their native rectangle masks, left to right; their later keys stay intact. The transition veil is existing content. This is a 2D window match-cut between distinct artwork, not a morph, group transform, or camera move.

## Native limits, guard and review

The inspected schema supports per-clip 2D offsets, uniform scale, rotation, rectangle masks and `hold`/`ease-in-out`/`ease-out` animation. It does not expose an animated whole-composition group transform or a 2.5D camera. The proposal adds no tracks, assets, generated images, titles, effects or source swaps. It does not touch the triptych’s later curves or the shoe opacity curve.

`connected-transitions-v2.commands-fragment.json` contains 22 typed actions and a fresh-document-hash sentinel. Draft batches must stay at or below 16 operations. The shoe path retains its saved pre-8.5 keyframes exactly; only its late sampled return is replaced. Reinspect after any uncertain action; never replay it blindly. `connected-transitions-v2.storyboard.json` is a capture proposal; only root’s native rendered output and moving-frame review can qualify the result.
