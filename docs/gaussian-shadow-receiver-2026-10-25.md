# Captured-color shadow receiver

The Scene composition panel now offers **Receive shadows** beside Captured
appearance and Light captured colors. It preserves the photographed colors in
unoccluded regions while added objects cast approximate shadows onto the scan.
This closes a specific integration gap; finished-scene reference parity remains
open.

## Rendering contract

For each Gaussian center, the renderer estimates a surface normal from its
covariance, oriented toward the observer, using the existing normal estimator.
Explicit lights produce two fields: received light with authored-object
visibility, and reference light with that visibility disabled. Ambient fill is
present in both. The per-channel ratio multiplies captured linear RGB before
display encoding. Capture brightness multiplies this ratio; at brightness 1,
an unoccluded Gaussian retains its original stored color without a redundant
decode/encode operation. A zero reference channel uses ratio 1.

Shadow strength interpolates object visibility. Light color, position, source
shape, intensity and ambient fill determine the resulting shadow. Finite lights
use the same sample positions and weights in both fields. GPU batches accumulate
received and reference fields separately before dividing: averaging individual
batch ratios would bias penumbrae. Two float4 accumulators add 32 bytes per
Gaussian while this mode is active, and are released when leaving the mode.
Normals plus illumination plus accumulators total 64 bytes per Gaussian.

The ratio also applies when the scan is sampled by spatial reflection rays.
Capture object-emission preferences are retained but inactive in this mode.
Existing approximate relighting keeps its prior behavior. Environment illumination
of authored objects remains separate from the explicit-light shadow ratio.

The complete composition uses `looksy-scene-composition-v17`, retaining the v16
fields and introducing `captureLighting.mode = "shadow-receiver"`. Earlier
schemas reject the new mode. Existing v1-v16 compositions retain their formats.
Save/reopen, undo/redo, saved views and frozen export recipes use the same codec.

## Evidence

Evidence root: `/private/tmp/looksy-shadow-receiver-v1`.

- Independent analytic checks cover zero reference, unoccluded identity, full
  shadows, strength, colored key/fill and ambient, rotated correlated covariance,
  inactive emission preference, zero brightness and hidden lights. A separate
  512x512 double-precision area integral checks the renderer's 1024-sample
  penumbra, including its batched ratio accumulation.
- Tiled and full renders differ by at most one RGB code in the synthetic case.
  The captured-mode baseline has the same maximum difference. The first strict
  byte-equality trial failed at 21 pixels by one code; that result is retained
  under `native-v3`. This is bounded raster roundoff, not exact tiled identity.
- `regression-v2.log` records five passing receiver/object/cache tests, including
  native and software traversal. `related-regression.log` records eleven passing
  haze, object-emission, spatial-reflection and film/sequence tests.
- `native-ui/receipt.json` and `dense-ui/receipt.json` pass native pointer mode
  selection, undo/redo, quality and effect retention, unoccluded photo identity,
  frozen twelve-frame export during a concurrent mode edit, reopen, shot recall,
  compact controls and emission-preference retention. The dense project contains
  830,842 Gaussians. Both windows were exposed but inactive; these receipts do
  not establish foreground input latency.
- `film-replay/receipt.json` and `dense-film-replay/receipt.json` independently
  replay all twelve exported frames in each workflow with zero RGB error.
- `staged-receiver/` contains a real reconstruction with an imported copper torus
  and a chrome sphere. Its `source-neutral.png` and `mode-background.png` are
  byte-identical, SHA256
  `65e918cf72d383494c08c2eb1bf4504622598cfbe3ebe201fd3ccd6b4bfeca37`.
  Shadows change the combined object/scan render while the unoccluded background
  remains unchanged. This diagnostic placement is not a finished beauty shot.
- `legacy-image-comparison.json` verifies all four staged captured/relit PNGs
  remain byte-identical to the pre-change renderer outputs.
- The `photo_editor_app` desktop target builds with the new control and renderer.

## Qualification limits and next work

This is differential compositing using an approximate covariance normal and
Gaussian center depth. It does not recover unlit albedo, reconstruct a geometric
surface, remove existing photographed shadows or solve the original illumination.
The final artistic shot still needs convincing object placement, contact,
reflections, lighting, haze and a moving-camera inspection. Imported-mesh film
replay, broader captures and sustained active-foreground performance remain
separate open gates. No full video-parity claim follows from these results.
