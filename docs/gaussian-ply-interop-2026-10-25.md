# Independent renderer check for cleaned Gaussian PLY

The actual dense cleanup export has been loaded and rendered in a local Chrome
WebGL2 viewer using pinned PlayCanvas 2.23.1. It contains **1,203,946 Gaussians
with degree-three spherical harmonics**. All 62 properties at three sampled
rows decode exactly; the independent byte audit separately verifies every
retained export row against the original source and visibility mask.

The viewer uses the exported camera basis and position without a fitted model
transform. Native-resolution captures are 1559×1039. Orbit changes both pose
and pixels; restoring the exported camera restores the position, orientation
and PNG bytes exactly. The upright Bonsai scene and contextual browser captures
were inspected. Remaining reconstruction softness is visible.

Evidence is under `/private/tmp/looksy-gaussian-external-viewer-v1/run-v3/`:
`interaction-audit.json`, `input.json`, `evidence/`, `initial-page.png` and
`left-page.png`. Export SHA256:
`cbeacb397e4af7155c700f56579377389a40ab956dac32680d6d60dab0324f74`.
Pinned engine bundle SHA256:
`40e34f0c45c293f4078a8e2a7bfb8b99ea303a5d15cfd3ee3713c2226e59b3b6`.

[`serve-gaussian-ply-interop.py`](../scripts/serve-gaussian-ply-interop.py)
verifies the export receipt, standard float PLY layout, point count, camera and
engine version before creating a new local evidence directory. It serves only
loopback files, copies the engine license and accepts captures only from its own
origin. No scene upload or remote asset load is used. This fixture currently
requires centered square-pixel pinhole camera metadata.

This proves one real export's interchange with PlayCanvas. It does not prove
Cinema 4D/Octane compatibility, pixel equivalence with another renderer, or
transfer of Looksy's objects, lights, haze and post effects through Gaussian PLY.
PlayCanvas documents Gaussian PLY as a [GSplat asset](https://developer.playcanvas.com/user-manual/editor/assets/inspectors/gsplat/)
and its [engine integration](https://developer.playcanvas.com/user-manual/gaussian-splatting/building/your-first-app/engine/).
