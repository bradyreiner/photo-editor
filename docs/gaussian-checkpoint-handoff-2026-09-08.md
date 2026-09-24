# 4D trainer checkpoint handoff: compatibility findings

## Decision

Do not label a Fudan/OMG4 checkpoint as a native v1 scene by tensor conversion
alone. No trainer was installed, no checkpoint was deserialized, and no genuine
dynamic reconstruction was validated in this investigation. The existing native
space-time artifact/render/export path remains usable with its own documented
conventions. It is not yet a faithful implementation of this external trainer.

## Licensing and real evidence

The Fudan repository has an [MIT root license](https://raw.githubusercontent.com/fudan-zvg/4d-gaussian-splatting/main/LICENSE),
but its [model implementation](https://raw.githubusercontent.com/fudan-zvg/4d-gaussian-splatting/main/scene/gaussian_model.py)
and [CUDA rasterizer](https://raw.githubusercontent.com/fudan-zvg/4d-gaussian-splatting/main/diff-gaussian-rasterization/cuda_rasterizer/forward.cu)
retain Inria non-commercial research/evaluation notices. This is unresolved
provenance/permission evidence, not a conclusion that the root license clears
all incorporated code. Do not bundle that trainer on the root license alone.

The authors of [OMG4](https://github.com/MinShirley/OMG4) publish a
[pretrained checkpoint folder](https://drive.google.com/drive/folders/1WB7WYOUlvemfYZE35lkl_WV4fiF3p68v).
Public folder listings were inspected, not the weight payloads. The six listed
4dgs_pretrained files range from 3.49 to 8.79 GB. Their suitability, redistribution
terms and reference-render reproducibility have not been established. Listing
HTML is retained under /private/tmp/gaussian-checkpoint-handoff-2R1B1B.
No multi-GB download, Torch installation, external model-store import or remote
training/upload was performed.

## Parameter/configuration gap

The model's 4D capture tuple has 19 fields, including log scales, two rotations,
opacity logits, appearance tensors, temporal mean, optimizer state and active
degrees. It omits duration, physical calibration and several constructor/render
settings. Rotating versus nonrotating temporal-scale branches also differ.
These must be explicit inputs to any adapter, not inferred defaults.
See the model implementation linked above.

## Rendering gap

Current native implementation: apps/desktop/src/gaussian_splat_shader.h.
The following comparison is against the upstream default CUDA path, not every
Python/configuration variant:

| Behavior | Native v1 | Upstream CUDA |
| --- | --- | --- |
| Near clipping | z below 0.01 | z at or below 0.2 |
| Temporal culling | Resulting opacity below 1/255 | Additional marginal threshold 0.05 |
| SH view direction | Conditioned, moving center | Original center |
| Temporal SH activation | Supported at all spatial degrees | Nested under spatial degree 3 |
| Alpha cap | 0.999 | 0.99 |
| Coverage | Opacity-dependent rectangle | Eigenvalue-derived radius and tile coverage |
| Pixel coordinates | Top-left-corner convention | Integer-center NDC mapping |

Sources: [forward rasterizer](https://raw.githubusercontent.com/fudan-zvg/4d-gaussian-splatting/main/diff-gaussian-rasterization/cuda_rasterizer/forward.cu)
and [projection helpers](https://raw.githubusercontent.com/fudan-zvg/4d-gaussian-splatting/main/diff-gaussian-rasterization/cuda_rasterizer/auxiliary.h).
This is source inspection, not a measured image comparison. It is sufficient to
reject an assumption of equivalence, not to quantify trained-scene quality.

## Next implementation gate

1. Select a trainer with reviewed dependency and weight permissions; pin its
   revision and exact training/render configuration.
2. Obtain a real trained checkpoint plus calibrated views/times and reference
   renders. Use data-only tensor extraction with strict bounds; do not enable
   arbitrary pickle execution or load optimizer code inside the app.
3. Specify an explicit versioned rendering profile or supported conversion
   subset. Reject unsupported settings instead of silently using native defaults.
4. Verify parameter conditioning and actual pixels against the trainer at
   multiple observed/held-out cameras and interpolated times. Include temporal
   tails, close clipping, opaque overlap and higher-order appearance.
5. Only then expose that adapter as supported. Capture-to-training, foreground
   performance and trained novel-view quality still need independent proof.

Synthetic motion, artifact roundtrips and the existing timed MP4 tests do not
substitute for this gate. This investigation did not resolve all trainer choices.
