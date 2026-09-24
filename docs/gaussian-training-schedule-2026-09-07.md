# Capture-sized Gaussian training schedule

## Experiment contract

The existing 3,000-step default leaves Brush's refinement interval at 200 and
growth stop at 15,000. The latter extends beyond the entire requested run.
The installed Brush v0.3.0 help and its
[training configuration](https://raw.githubusercontent.com/ArthurBrussee/brush/v0.3.0/crates/brush-train/src/config.rs)
recommend relating refinement frequency to view coverage. Its
[trainer](https://raw.githubusercontent.com/ArthurBrussee/brush/v0.3.0/crates/brush-train/src/train.rs)
uses the absolute growth-stop iteration rather than scaling it to total steps.

Test a bounded candidate on the existing identity-safe reconstructed dataset:
3,000 steps, 800-pixel edge, seed 42, maximum one million splats, every eighth
view withheld from training. Change only refinement frequency to 32 iterations
and growth stop to 2,400. All camera poses, images and held-out identities remain
fixed. The existing same-dataset baseline is 21.366087 dB / 0.5870927 SSIM.

Acceptance before inspecting candidate results: at least 1 dB PSNR and 0.04
SSIM improvement, inspect all four held-out images for new gross artifacts,
validate the complete PLY, then reproduce through production integration.
This is a single-scene improvement gate, not a general quality benchmark.
Record elapsed time and memory; increased scene complexity is an explicit cost,
not a speedup. Run trainers sequentially and keep all outputs isolated under
`/private/tmp/gaussian-training-schedule-f27hw9`; do not overwrite prior projects.

COLMAP model analysis on the fixed dataset reports 32 registered images, 7,170
points, 29,721 observations and 0.221751-pixel mean reprojection error. This does
not prove correct unseen geometry or independently calibrated cameras, but
supports testing the training schedule before another camera-model change.

## Results

The same-data candidate passes the predeclared gate. Independent saved-image
metrics are calculated by `gaussian_image_quality_test`, a double-precision CPU
implementation of stored-RGB PSNR and 11x11, sigma 1.5, zero-padded SSIM. Identity,
known channel error, symmetry, invalid inputs and Brush's
[published float-input SSIM example](https://github.com/ArthurBrussee/brush/blob/v0.3.0/crates/brush-train/src/ssim.rs)
are covered. The historical baseline's independently measured 21.366087 dB /
0.587099 SSIM closely matches its trainer-reported 21.366087 / 0.587093.
Stored 8-bit output metrics are not identical to metrics on unquantized tensors.

| Run on fixed cameras/images | Mean PSNR dB | Mean SSIM |
| --- | --- | --- |
| Existing baseline | 21.366087 | 0.587099 |
| Fresh unchanged-schedule control | 20.514627 | 0.579486 |
| Capture-sized schedule candidate | 22.819708 | 0.711260 |

Every candidate held-out view improves over the existing baseline. All four
were visually inspected: brickwork and foliage are substantially more defined,
but ghosting, silhouette errors and edge coverage holes remain. Explicit seed
42 does not make this backend fully deterministic; the control rerun variation
is evidence against that claim. The candidate clears the gate against both
controls, not a general or statistically calibrated quality certification.

The candidate validates as 759,882 splats, 179,333,703 bytes, SHA-256
`2b65bcaf076b887f986658daa72857adbbd137b4e2b6723272e6c0bbfbc84c10`.
Training alone takes 129.60 seconds versus 41.56 seconds for the fresh control.
macOS `/usr/bin/time -l` reports peak memory footprints of 3,485,271,864 versus
1,153,581,992 bytes; maximum RSS is 1,420,640,256 versus 298,696,704 bytes. These
are process measurements, not whole-machine GPU memory accounting. The control
overlapped a small C++ build; these timings are indicative, not an isolated
performance benchmark. This is a quality/resource tradeoff, not a speedup.

## Integration

The controller derives an explicit schedule from registered view count: refine
every 32–200 steps, additionally bounded by one fifth of very short requested
runs; growth ends at 80% of the run or iteration 15,000, whichever comes first.
The existing one-million-splat cap remains. Seed 42 and every-eighth evaluation
split are explicit. The schedule is persisted and survives read-only reopen;
legacy projects without schedule metadata do not invent it. The UI discloses
resource cost before capture and puts the actual schedule in Details. Growth stop does
not disable the backend's other optimizer, pruning or replacement behavior.

Supported bounds, invalid configurations, protocol arguments and schedule
persistence have focused tests. All ten Gaussian tests pass with native Metal
in 0.50 seconds (`native-tests.log`); desktop builds and Scene QML lint pass.

Fresh production capture-to-trained-scene verification registers all 32 views,
persists the 32-step / 2,400-growth-stop / 600-final-step schedule, and validates
753,038 splats (177,718,519 bytes). It takes 178.482 seconds end to end. The 50ms
event-loop timer records a maximum 68ms gap, not foreground interaction latency.
Trainer metrics are 22.9415 dB / 0.7133037 SSIM; independent saved-image metrics
are 22.941502 / 0.713309. This fresh reconstruction is integration evidence;
the fixed-camera experiment above is the controlled schedule comparison.

The app's native Metal renderer on the 759,882-splat candidate reproduces the
trainer's saved evaluation image at 70.026779 dB, maximum channel difference 2
and 99th-percentile difference 0 on the 8-bit scale. Thus the conspicuous blur
is already in training output, not introduced by the app's renderer. Thirty
camera-motion render calls measure 28.17ms median / 30.45ms p95 at the capture
camera's 800-pixel image edge. This is isolated renderer evidence, not window
presentation, full-resolution realtime or input-to-photon proof.

Packaged native runs reopen the integrated scene with a GPU frame and visible
controls, and separately show the new-capture resource hint. Screenshots were
inspected. The final layout keeps schedule parameters under Details to preserve
viewport area. Proof profiles deliberately use an unavailable isolated catalog,
so their recovery banners do not describe the user's catalog. All owned empty
model runtimes stop; no weights were installed or loaded.

Native/UI evidence:

```text
/private/tmp/gaussian-training-schedule-f27hw9/integrated-receipt.json
/private/tmp/gaussian-training-schedule-f27hw9/integrated-quality.jsonl
/private/tmp/gaussian-training-schedule-f27hw9/native-render.json
/private/tmp/gaussian-training-schedule-f27hw9/native-tests.log
/private/tmp/gaussian-training-schedule-f27hw9/accepted-ui-build.log
/private/tmp/gaussian-training-schedule-f27hw9/qmllint.log
/private/tmp/gaussian-schedule-setup-GcQ1OO/receipt.json
/private/tmp/gaussian-schedule-setup-GcQ1OO/startup.png
/private/tmp/gaussian-schedule-final-ui-CfOXJR/receipt.json
/private/tmp/gaussian-schedule-final-ui-CfOXJR/startup.png
```

Evidence root: `/private/tmp/gaussian-training-schedule-f27hw9`, including
`candidate.log`, `control.log`, `baseline-quality.jsonl`, `control-quality.jsonl`,
`candidate-quality.jsonl`, `integration-build.log`, `metrics-final-build.log`
and `focused-tests.log`. Candidate logging was not enabled, so its quality
numbers come from independent saved-image comparison, not invented trainer logs.

This remains one static JPEG capture. Diverse real RAW scenes, independent pose
calibration, foreground realtime, large captures, backend packaging, temporal 4D,
film emulation, embedded inference and the professional-tool goal remain open.
