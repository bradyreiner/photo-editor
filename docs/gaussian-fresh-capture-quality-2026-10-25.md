# Fresh corrected-color reconstruction and full source coverage

The [capture color correction](gaussian-capture-color-2026-10-25.md) now has
fresh alignment and trained photographic evidence. The 96-photo result below
misses the absolute photographic targets. The completed 255-photo run now passes
them on the same 37 excluded reference photographs. Full video parity remains open.

## Fresh 96-photo alignment

`/private/tmp/looksy-corrected-color-alignment-v1/plan-before-v2.json` freezes
the same 96 source photos and 37 excluded reference photos, shared estimated
SIMPLE_RADIAL intrinsics and strict matching. No reference numeric calibration
or poses enter reconstruction. All 96 prepared PNG hashes match the separate
corrected-color preparation exactly.

The native alignment-only run completes in 603.64 seconds, retains 96 cameras
in one component and passes all frozen absolute and incremental diagnostics.
Corrected images produce 1,136,599 keypoints versus 955,725 from darkened inputs;
both match databases contain all 4,560 possible pairs.

| Training-camera diagnostic | Older preparation | Corrected preparation |
| --- | ---: | ---: |
| Registered cameras | 96/96 | 96/96 |
| Median relative focal error | 0.0908% | 0.1306% |
| Median rotation error | 0.21763° | 0.09036° |
| Maximum rotation error | 0.38496° | 0.17545° |
| Median normalized center error | 0.003039 | 0.001608 |
| Maximum normalized center error | 0.014424 | 0.004784 |

Focal error increases slightly while orientation and position improve.
`alignment-audit/report.json` and `comparison.json` retain per-camera data.
The 58.5% median rotation improvement exceeds the predeclared 20% diagnostic
target without sacrificing coverage or increasing the worst center error.

## Matched-budget training and independent photographs

`training-5000-v1/plan-before.json` freezes 5,000 steps, 500,000 splats, 1600-edge
inputs, seed 42, refinement every 96 and growth stop 4,000. Its budget amendment
replaces the provisional 15,000/1m reservation because the completed capacity
experiment did not improve external quality. Prepared images, trainer identity,
schedule and twelve named internal evaluation views match the corrected-color,
older-geometry baseline. Fresh calibration, poses, point geometry/tracks and
seed colors change together; this is not a pure pose-only ablation.

Undistortion takes 7.06 seconds and preserves all poses and input names. Training
completes in 300.36 seconds with dataset and binary hashes unchanged. Model SHA:
`5b883892406b315f0b8bf32c235b76a1fc0cddc09cc9462f18dfc4724f603e89`.

The evaluation mapping uses the fresh training-camera similarity only. No
held-out pose or pixel is fitted. Original reference grids/focal values remain
fixed, and renderer/metric tools are byte-identical to the earlier comparison.
The 37-view comparison completes in 234.65 seconds with pinned identities intact.

| External photographic metric | Corrected color, older geometry | Fresh corrected reconstruction | Target |
| --- | ---: | ---: | ---: |
| Mean PSNR | 22.02049 dB | 24.94575 dB | ≥28 dB |
| Mean SSIM | 0.649668 | 0.775294 | ≥0.9 |
| Worst-view PSNR | 18.95436 dB | 20.01140 dB | ≥22 dB |

Every view improves in both metrics, but all three absolute targets still fail.
Full-resolution DSCF5565 and its worst crop were inspected: fine structure and
smearing remain unresolved. `photographic-comparison.json` retains each delta
and failed gate. This does not certify unmeasured viewpoints or video parity.

At one identical fresh-model camera, native versus Brush is 73.90449 dB, mean
absolute channel error 0.002615 codes and 99th-percentile error zero. Seven
pixels exceed two codes, with maximum four. This localizes the large visible
detail loss upstream of a comparably large native rendering mismatch; it does
not close the original CUDA rasterizer audit.

## Full 255-photo production workflow

`/private/tmp/looksy-bonsai-full-source-v1/` contains all 255 eligible source
photos, excluding the same 37 evaluation names. The prior 96 are hash-verified
copies; 159 new JPEG ZIP members use exact HTTP ranges, archive ETag validation
and ZIP CRC checks. Network traffic is 152,965,780 bytes. No archive calibration,
pose or trained-weight member is opened or supplied to reconstruction.

`capture-plan-before.json` freezes a complete production-controller run using
shared estimated intrinsics and strict matching. It continues through training
with 15,000 steps, one million splats, 1600-edge inputs and seed 42. At full
coverage the production schedule uses refinement every 200, growth stop 12,000
and 3,000 settling steps. Source count and budget both change; this is not a pure
coverage or capacity comparison.

Repository executables and evaluation scripts are hash-verified copies under
`frozen-tools/`. At the undistortion transition, the wrapper audits the completed
cameras before training starts. If that audit passes, the completed model is
automatically evaluated on the same 37 photographs with the unchanged absolute
targets and training-only mapping protocol. Reference photos, poses and numeric
calibration remain outside the pipeline input.

The owned process and `capture-run-receipt.json` determine current state. A
project directory alone does not prove success. Full capture, photographic
detail, sustained interaction and finished-scene qualification remain separate.

The full capture has now completed training successfully in 5,035.55 seconds
overall. All **255/255** cameras register in one component and pass the frozen
camera gates: median/max rotation error **0.02550° / 0.08484°**, relative focal
error **0.06947%**, and median/max normalized center error **0.000603 / 0.001646**.
The accepted artifact contains **830,842** Gaussians, SHA256
`ceba31f1a0f2463c5250389ff6f491e8675a9afad17ebdee684391950fc6f565`.
Frozen binaries remain unchanged. Independent photographic evaluation completes
in 314.54 seconds with all pinned identities unchanged. Every reference view
improves in both PSNR and SSIM compared with the fresh 96-photo model.

| External photographic metric | Fresh 96-photo model | Full 255-photo model | Frozen target |
| --- | ---: | ---: | ---: |
| Mean PSNR | 24.94575 dB | **30.02280 dB** | ≥28 dB |
| Mean SSIM | 0.775294 | **0.915408** | ≥0.9 |
| Worst-view PSNR | 20.01140 dB | **22.83792 dB** | ≥22 dB |

All three targets pass. `photographic-comparison.json` records each of the 37
paired results and identical reference hashes. Full-resolution DSCF5565 and its
worst crop were inspected: detail improves substantially, but the blanket crop
still shows softer texture than the photograph. Source count, sparse geometry
and production training schedule change together, so this is an integrated
workflow result rather than a pure coverage ablation. One static indoor dataset
and these named views do not certify arbitrary captures, viewpoints, moving
content, finished lighting integration or equivalence with the video's renderer.
The general verifier intentionally retains `qualityCertified=false`.
