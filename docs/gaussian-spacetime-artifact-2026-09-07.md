# Data-only space-time Gaussian artifact

## Status

Subsequent workspace opening, time controls and frozen-time photo integration
are documented in `gaussian-spacetime-workspace-2026-09-07.md`. The status and
proof below describe the earlier artifact-only implementation stage.

Implemented native artifact read/write and save/reload rendering proof. This is
an internal desktop API, not yet an Open Scene route or a checkpoint converter.
It does not execute Python, deserialize optimizer state, train a scene, or prove
dynamic reconstruction quality. Existing static and prepared timed projects
remain unchanged.

The container follows the [SafeTensors format](https://github.com/safetensors/safetensors/blob/main/README.md).
Interoperability was checked with the official Python package version 0.6.2 in
an isolated temporary environment; the application has no new Python dependency.
No Torch, NumPy, trainer package or model weights were installed for this proof.

## Native contract

`apps/desktop/src/gaussian_spacetime_artifact.h` exposes loading with a required
lowercase SHA-256 pin and export to a new absolute filename. The reader hashes
the same bytes it decodes and publishes no scene before the pin matches.

The file has an eight-byte little-endian header size, a JSON header padded with
spaces, and exactly one contiguous little-endian F32 tensor named `gaussians`.
The tensor shape is `[pointCount, stride]`. `__metadata__` contains exactly one
string, `framehouse.scene`, whose value is the scene metadata JSON.

| Columns per point | Meaning |
| --- | --- |
| 0–3 | XYZ mean at the temporal mean, peak opacity |
| 4–7 | Conditional XYZ velocity, temporal mean in local seconds |
| 8 | Positive inverse temporal standard deviation |
| 9–17 | Conditional spatial square-root factor, three unpadded columns; covariance is F F-transpose |
| 18–65 | All 16 spatial SH bases, RGB interleaved |
| 66–113, when temporal appearance is enabled | First cosine order, all 16 RGB SH bases |
| 114–161, when temporal appearance is enabled | Second cosine order, all 16 RGB SH bases |

Stride is 66 when temporal appearance degree is zero, otherwise 162. Inactive
spatial coefficients are preserved; temporal degree one preserves the second
stored temporal order too. Degree zero requires no temporal appearance vector.
GPU struct padding is not serialized. Spatial covariance convenience fields are
reconstructed from the authoritative factor on load.

Required metadata specifies schema `framehouse-gaussian-spacetime-v1`, metres,
seconds, sRGB display-referred appearance, top-left-corner pixels, the explicit
row layout identifier, spatial degree 0–3, temporal degree 0–2, double time
origin, local float time interval, focus distance and calibrated cameras.
Camera poses use the existing right-handed COLMAP right/down/forward contract.
This profile does not imply scene-linear/HDR appearance or arbitrary color-space
support. Native rendering uses the existing time and cosine convention described
in `gaussian-spacetime-rendering-2026-09-07.md`.

Bounds include a 1 MiB header, 2 GiB file, 2 million points, 2,000 cameras and
finite, bounded parameters. Tensor shapes, offsets, exact payload length, units,
camera calibration, opacity, temporal scale and nonsingular factors are checked.
Duplicate JSON keys, including escaped aliases and nested scene-metadata keys,
are rejected. Existing symlink targets are rejected. This is not a sandbox for
hostile concurrent filesystem mutation.

Export validates before atomically claiming a new filename with `NewOnly`;
existing files are never overwritten. Cancellation or write failure after file
creation retains the partial new file and returns failure, not a valid receipt.
Call these APIs from workers: they intentionally perform synchronous I/O and
bounded block processing. Maximum-size memory/admission performance is unmeasured.

## Proof

Artifacts: `/private/tmp/gaussian-artifact-proof-DtBRUq`.

- `artifacts-final/receipt.json`: 12 exact parameter roundtrips across all
  supported spatial/temporal degrees, 27 malformed-file rejections, symlink,
  wrong/missing hash, cancellation and overwrite protection checks. An 8,257-point
  case crosses two 4,096-row streaming boundaries and a partial final block.
- `interop/receipt.json`: 12 native-to-official-SafeTensors-to-native roundtrips;
  every canonical native artifact hash remains identical after the independent
  library reads and rewrites the container.
- `render/receipt.json`: 144 native Metal frames remain pixel-exact after artifact
  save/reload across appearance degrees, two cameras and six times. Independent
  time-slice image oracle maximum channel error remains 1/255 (gate 2/255).
  Thirty native no-readback preview frames and eleven invalid render requests pass.
  The synthetic image `render/view-0-frame-132.png` was visually inspected.
- Fifteen Gaussian CTests pass in 4.86 seconds; log
  `/private/tmp/gaussian-artifact-regressions.log`. App and focused targets build.
  Configuration still reports the existing missing PostgreSQL packaging-runtime
  directory and optional Vulkan-header notices; these are not repaired here.

The official interoperability check is opt-in, not a CTest dependency:

```sh
gaussian_spacetime_artifact_test NEW_FIXTURE_DIRECTORY
python tests/gaussian_spacetime_artifact_interop.py NATIVE_TEST NEW_FIXTURE_DIRECTORY NEW_INTEROP_DIRECTORY
```

Use a disposable environment with `safetensors==0.6.2` for that Python command.

## Still required

An explicit, license-reviewed trainer adapter must translate actual checkpoint
parameter conventions into this profile and verify against that trainer's own
renders. No adapter or genuine dynamic checkpoint was tested. Scene project
publication, background open/reopen, time controls, timed novel-view photo/film
export, actual synchronized capture training and held-out dynamic quality remain
open. Synthetic artifact and image checks do not complete those requirements or
establish foreground realtime performance.
