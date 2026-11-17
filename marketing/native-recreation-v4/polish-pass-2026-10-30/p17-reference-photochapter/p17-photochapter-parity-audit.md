# P17 photo chapter parity audit

Scope is the already-authored 39–62s photo chapter. This is a read-only comparison and an unbound native edit proposal. I did not call the app, change the timeline, or request a render. The P16 full render remains in progress; the 52.5s native frame below was decoded from an already-encoded chunk.

## Confirmed issue: the AFTER badge is outside the flower photo

At 52.5s, the native photo and pixel grid are in the accepted composition: the flower photo ends near x=1279, and the source-cell alignment and copy remain clear. Preserve those layers and all 64 cell curves. The native AFTER text currently runs from about x=1180 to x=1383, so it continues roughly 104px beyond the photo edge. Its background plate is centered near x=1271.5 and is about209×34px.

The source implementation in `marketing/animated-trailer/src/creative-launch/ProofTools.tsx` positions the AFTER badge `right: 20`, `top: 20`, with `14px` horizontal and `10px` vertical padding, a 5px corner radius, and the existing “AFTER / THIS EDIT” label. `Photo.tsx` puts it over a 530×795 flower `Surface` at left40. The `Stage` in `Kit.tsx` starts at (650,120) and scales by1.1. This places the photo at about x694–1277, and the AFTER plate wholly inside its right edge.

The extracted reference at52.5s and55s shows the badge inside the flower image. The reference raster bounds at52.5 are approximately x1018–1254, y142–187; its right inset is about23px. At59.5s, the chapter has moved on to the delivery scene and has no AFTER badge. Reference assets are exact CFR frames extracted from `/Users/bradyreiner/Downloads/landscape.mp4`; the source movie SHA-256 is `9ec7b8b422f96818e61a1f2d3987cc3422027537bb6280c671a689c88e3e8640`.

This produces a minimal two-clip native proposal in `p17-after-badge.fragment.json`:

- Move the existing title clip left and down to the source badge text inset. Preserve its wording, Space Mono family, 16.5px projected size, letter spacing, color and 15 sampled opacity keys.
- Move the existing rounded-rectangle plate to the reference badge center; enlarge it to the measured reference bounds and adjust its normalized corner radius so the rendered radius remains about5.5px. Preserve its fill, existing fade and full-canvas outer frame.
- Change no photo, pixel cell, grid/keyframe, source appearance, Before label, or other clip.

The proposed coordinates come from the reference bbox and the source CSS: text X is1033.4/1920; plate center is(1136.5,164.5); plate size is237/1920 by46/1080. The fragment binds to the exact P16 document hash and raw snapshot SHA. It must not be applied until the current render is terminal and a fresh native inspection confirms the hash, both target IDs/times/style/keyframe fingerprints, and active landscape variant. Check native readback after each setter. Re-render review should compare frames1575,1650,1785; P16 has not yet produced native frames1650 or1785.

## Cursor comparison: smaller, but not clipped or distorted

The P16 native pointer at45.733s and48.667s is complete and maintains the same 3:4 aspect as the Remotion arrow; the crop does not show a clipped export boundary or a squashed shape. Source dimensions explain the visual difference: the native vector uses a24×32 viewBox and its authored frame is26.4×35.2px at1920×1080. Remotion draws the arrow path inside a40×48 viewport; after the Stage's1.1 scale, the path bounds are about33×44px. The native pointer is therefore roughly20% smaller. The current cursor keyframes and its hotspot/target motion are intricate; this proposal leaves its size, position, rotation and every motion key untouched. The evidence crops are in `pointer-parity-contact-sheet.png` and the four individual cursor crops.

## Evidence files

`photo-parity-contact-sheet.png` uses top-left native52.5s, top-right reference52.5s, bottom-left reference55s, and bottom-right reference59.5s. `crop-native-1575.png` and `crop-reference-1575.png` enlarge the badge area. The individual source frames and sidecars retain frame indices, times, document hash and native chunk identity.
