# RAW panel title affine audit (P8)

**Result:** Keep the existing panel-opening title motion in groups 09/10. The G66 transform is a shared panel transform in the P8 document, and the title anchors/font sizes map to the Remotion source at frame 1337. No corrected geometry fragment is needed.

**Scope:** Read-only audit of the P8 snapshot and unbound proposal. No MCP/app calls, project edits, or live mutations. No post-proposal title render exists yet.

## Snapshot and references

- P8 document: `native-projects/looksy-polish-pass8.json`
- P8 SHA-256: `9cf20c9987dc90e246497005beab04e11c57a69712a896ec7b77a9d528fe9c30`
- P8 document hash: `16e930e2e2d74566185eb464cf30884fc11547b2a10710df8c8edf9b71dcf860`
- Native baseline: `native-raw-pass8-clock-probe/frame-global-1337.png` (1920×1080, global 44.5667s)
- Remotion reference: `raw-pass6-reference-review/landscape-frame-1337.png` (same frame/time) and `landscape-frame-1350.png` (45.0000s)
- Proposal: `raw-detail-geometry-p8-guarded.commands-fragment.json`, groups `panel-opening-header` and `panel-opening-footer`

The native P8 baseline still has the header and footer starting at 45s, so frame 1337 has no panel labels. The proposal moves both clips to 44s and gives them the same opening pose/opacity as the panel contents. This audit verifies that pose; it does not claim a rendered post-edit comparison.

## Source-to-native affine mapping

The Remotion source is explicit in `marketing/animated-trailer/src/creative-launch/ProofTools.tsx`:

- `Stage` in `creative-launch/Kit.tsx:231-258` places the 1000×800 artwork at `(650,120)` and scales it by 1.1 in landscape.
- `PixelProof` opens from local `s=5` to `s=6` with quintic smoothstep, starts at the sampled source point, and settles at panel `(440,140)` with `scale(8/512 → 1)` and `transform-origin: 0 0` (`ProofTools.tsx:113-190`).
- The panel header is inset 18px from its top-left (`ProofTools.tsx:193-195`). The footer is inset 18px at the bottom (`ProofTools.tsx:249-259`).
- At the 44.5667s sample, the stage-local sample source point is `(618.181818, 217.727273)`, opening progress is `0.6235264197530863`, and panel scale is `0.6294088194444444`. The panel top-left is `(507.080747,169.262265)` in Stage coordinates.

`ensureTitleFrame` draws each native title at its normalized title position (`timeline_document_controller.cpp:49298+`). `clipVideoFilter` scales the full-canvas title raster, then translates it with quarter-frame offsets (`timeline_document_controller.cpp:54509+`, `54766-54834`, `55227-55234`). With no animated `titlePosition` delta, its rendered anchor is:

```text
screenX = W/2 + (titlePositionX*W - W/2)*frameScale + (W/4)*frameOffsetX
screenY = H/2 + (titlePositionY*H - H/2)*frameScale + (H/4)*frameOffsetY
```

The title clips already use final-state anchors that map back into the Remotion stage:

| Clip | P8 title anchor | Stage-local anchor | Native font size |
|---|---:|---:|---:|
| ADD heading `872a642f-8198-409b-ab2f-b6d273fdf5db` | `(1153.8, 304.36)` | `(458.0, 167.6)` | 17.6 px |
| First status `83ce9c67-b865-42f8-be0a-beec7e2da674` | `(1153.8, 919.48)` | `(458.0, 726.8)` | 14.3 px |

At local title time 0.5666667s (global 44.5667s), the proposal's G66 samples are `frameOffsetX=0.2880660153`, `frameOffsetY=-0.2458843075`, and `frameScale=0.6294088189`. Applying the native formula gives:

| Clip | Native predicted anchor | Source affine anchor | Font at this frame |
|---|---:|---:|---:|
| ADD heading | `(1220.251116, 325.297343)` | `(1220.251116, 325.297343)` | `17.6 × 0.6294088 = 11.077595 px` |
| First status | `(1220.251116, 712.459296)` | `(1220.251116, 712.459296)` | `14.3 × 0.6294088 = 9.000546 px` |

The source side uses the same stage and panel matrices, so the predicted anchors agree to less than 0.000001 px. At the settled frame (45.0s), `frameScale=1` and both offsets are 0; the titles return to their existing P8 anchors. The reference frame 1337 visibly places the header and status on those opening panel edges; frame 1350 shows them settled with the panel.

## Why using G66 is valid here

The fragment uses stable grid-cell clip `687c3d34-45c1-4794-800c-6568ec7974e1` only as a source for the shared panel motion channels; it retains the separate title clips’ own static title positions. I checked every one of the 92 grid/selection-overlay IDs in the proposal against the P8 file, including active `landscape.framingKeyframes`:

- Over local seconds 5–6, every clip has the same `frameScale`, `frameOffsetX`, and `frameOffsetY` curves. Maximum pointwise difference is below `8.5e-16`; rounded to six decimals there is one curve per channel.
- The 64 cells do have eight distinct static X anchors and eight distinct static Y anchors, because each column/row occupies a different grid position. Those are not eight distinct opening-motion curves.
- Therefore the grid-cell X/Y base positions do not transfer to the heading/footer; only the common frame motion does. The proposal correctly preserves the titles’ own anchors and applies the common scale/offset curve.

The panel title/footer start/duration boundaries also match the source phase change: heading 44.0–48.5s, status 44.0–45.9s. This is a timing/visibility fix; it does not need a different affine path.

## Separate typography note

The `Label` helper defaults to `letterSpacing: 1.8px` (`creative-launch/Kit.tsx:105-123`), while the P8 ADD title clip has `titleLetterSpacing=0` and uses `Space Mono`; the source declares `Looksy Mono`. The footer source div sets no letter spacing and its native value of 0 is consistent. These are potential text-width/font-family differences, separate from the verified anchor and size mapping. No style change is included here; compare the native A/B before adjusting typography.

## Recommendation

Keep the existing two title timing and two title animation actions in groups 09/10. No title-only geometry patch is warranted. On the next native render, review frames 44.0, 44.5667, 44.9667, and 45.0 for title visibility, panel-edge alignment, and text width; review letter spacing/font family separately.
