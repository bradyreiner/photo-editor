# Video reconstruction retry

User input: `/Users/bradyreiner/Downloads/IMG_0894.MOV`.
FFprobe reports 3,805 frames over 127.151667 seconds. The original is unchanged.

The first production UI attempt sampled 160 frames at 1600 pixels maximum edge.
Its largest camera component contained 12 views; the controller correctly stopped
before training because fewer than half the inputs connected. This is not proof
that the source cannot be reconstructed. The original attempt is retained at
`/Users/bradyreiner/Pictures/Framehouse Scenes/IMG_0894 2026-09-09 222037-536`.

The retry uses 960 uniformly time-sampled frames, 1600 maximum edge, 5,000 training
steps and a 500,000-splat budget. This is a denser retry, **not adaptive or
sharpness-ranked selection**. Video matching now uses COLMAP sequential matching
with overlap 20, quadratic overlap and guided matching. Vocabulary-based loop
detection is explicitly off, avoiding an implicit model download. Static photo
matching remains exhaustive. The existing registration acceptance gate remains.

The UI default is now 480 samples, with a validated upper bound of 1,920. This
stays within the existing 2,000-view training schedule limit. Larger input does
not guarantee registration, sharpness or novel-view coverage. The failure text
now reports the actual largest-component count and required count.

The real app was rebuilt and launched through its existing visible creation
sheet and Build action. `--scene-video-frames 960` overrides the visible field
before Build, not the backend independently of the UI. Production project:
`/Users/bradyreiner/Pictures/Framehouse Scenes/IMG_0894 2026-09-09 223746-945`.
Native state/screenshots:
`/private/tmp/framehouse-video-ui-proof-UcGPJ0/retry-960`.
Initial receipt verified active/exposed window, no UI error and extraction
advancing. Reconstruction/training success is pending, not implied by launch.

Focused controller, codec/color-transfer and lifetime tests passed (3/3, 4.76s),
including real 960-frame extraction, out-of-range rejection before frame-folder
creation and the exact guided sequential-matching arguments. Production and
actual-app UI proof targets built successfully. The actual production selection
screenshot was inspected: 960 samples and the intended source/destination are
visible with the 5000/1600/500k settings, without clipped creation controls.
The existing separate SUV trainer was not stopped or replaced.

## Actual alignment result and approved partial build

The 960-frame retry finished with 110 views in the largest component, not the
required 480. COLMAP model analysis reports 5,362 points, 43,715 observations,
mean track length 8.15 and mean reprojection error 1.106 pixels. This remains
partial coverage; it is not a connected reconstruction of the full video.

A bounded calibration-only diagnostic on a copied database also topped out at
110 views. Its temporary files are under
`/private/tmp/framehouse-video-calibration-06uXkI`. A subsequent optional exhaustive
matching diagnostic was stopped when the user approved building the partial
section, freeing resources without changing either production reconstruction.

The user explicitly requested: “build the partial scene.” The app now exposes
`buildPartialScene()` through a visible **Build partial scene** action. Opening a
failed project alone never starts it. The action preserves `scene.json` as
`scene-before-partial.json`, rejects existing dataset/training outputs, validates
owned folder paths/settings, reuses the largest existing connected component,
and starts at undistortion rather than re-extracting or rematching frames.
Partial mode keeps the eight-view minimum, while explicitly bypassing the
whole-capture coverage threshold only after this action. A private authorization
flag is reset on open/new capture; a persisted partial flag is not authorization.

The UI header permanently discloses **Partial scene · 110 / 960 views**; the
saved receipt includes a missing-coverage warning. Tests cover explicit recovery,
no automatic resume, no repeated feature extraction, unchanged prior receipt,
no re-training an existing artifact, and preserved partial status on reopen.
Focused tests passed 3/3 in 4.66 seconds; the production app rebuilt successfully.

Actual app launch uses `--scene-partial-project` and explicit
`--scene-partial-start` to invoke the same visible partial-build button. Native
proof is at `/private/tmp/framehouse-video-ui-proof-UcGPJ0/partial`. Its first
receipt confirms active/exposed app, no UI error, 110 selected views, and
undistortion advancing. Training/viewer completion is still pending.

### Completed partial scene

At 23:12 local time the partial build completed all 5,000 steps in 194 seconds,
using 96 training and 14 held-out views. The validated artifact contains 65,360
splats and SHA-256
`23f70339a304f8d56653fee88b92821d48b574f8a704eee58056819d2ac84483`.
Artifact: `trained/scene.ply` in the production retry project above.
Trainer-reported held-out PSNR is 31.656736 dB and SSIM 0.9467767; these are nearby
frames from the same video and do not prove independent photographic fidelity
or missing-scene coverage.

The actual production viewer automatically loaded the validated artifact:
`state=ready`, `frameAvailable=true`, `previewPhase=photo`, no UI/viewer error,
active and exposed window. `partial/trained.png` was visually inspected. It shows
the reconstructed room section in the portrait capture aspect, with visible
softness; it is not a full-room or perfectly sharp reconstruction. The existing
Work recovered banner remains over the top controls until the user clicks Done.
The app remains open for user navigation. No broader photographic/SOTA goal is
marked complete.
