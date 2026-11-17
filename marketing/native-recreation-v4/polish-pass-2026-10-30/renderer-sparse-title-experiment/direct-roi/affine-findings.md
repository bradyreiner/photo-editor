# Sparse title ROI findings

## Result

The post-scale sparse ROI path is pixel-exact for the tested subset and materially reduces both wall time and peak RSS when each PNG decoder is constrained to one thread. This is a title-input graph experiment, not a full native project render or a production end-to-end performance claim.

## Measured comparisons

- Eight editable title layers over frames 1323–1352 (44.1–45.1 s), with scale, X/Y offset, and opacity curves: all 30 composited frames matched byte-for-byte after RGB decode. Both PNG decoders and filter-complex execution were limited to one thread. Details and hashes are in `affine-probe/multilayer-sparse-roi-1323-1352.json`.
- The preserved 112-title/caption input order and P8 curves at frame 1337: baseline and candidate framemd5 both `e28b689ebbcfd01f9f75b4add0323e98`. Under one-thread PNG decoding, baseline measured 15.813 s / 21,903,654,912 B max RSS; sparse candidate measured 6.661 s / 9,588,539,392 B max RSS. Candidate layout: 16 direct sparse inputs, 94 post-scale crops, and 2 conservative full-frame fallbacks. See `112-affine-benchmark/bounded-decoder-policy/results.json`.
- A separate 8-PNG decoder probe also matched its framemd5 exactly and measured 1,678,311,424 B default-thread RSS versus 318,750,720 B with one decoder thread. This establishes that decoder concurrency materially affects measurements; it is why the bounded-policy 112-input comparison is the relevant benchmark.
- The controller test directly calls the production `clipVideoFilter()` with an eligible 15-frame animated title and asserts that it emits a smaller ROI plus dynamic overlay origin. A pixel-identical disabled-effect case is asserted to stay on the full-canvas fallback. The integrated 4K review-excerpt workflow also passed with 60 H.264 frames, stereo AAC, and frame-to-frame title animation; this proves the path composes in delivery but is not a golden comparison for the whole sequence.
- The integrated 4K excerpt workflow took 210.76 seconds on this test host. It is a correctness pass, not a claim that dense full-project export is now acceptably fast.

## Exact path and fallback

The prototype retains the original full-canvas PNG and applies the same frame scale first. It then crops a fixed transparent ROI that encloses alpha support at every sampled scale, compensates overlay origin for the original centered crop and frame offsets, and composites the smaller layer. No source asset is cropped, rewritten, or flattened. The ROI size is fixed for the bounded segment; any unsupported clip geometry remains on the original full-canvas path.

The controller integration is limited to bounded H.264 title clips with full-frame stretch, normal clip blend, supported frameScale/frameOffsetX/frameOffsetY/opacity curves, and no spatial grade, effect, mask, rotation, perspective, corner, shadow, transition, scene attachment, or nonidentity group transform. Frame-clock misalignment, out-of-range crop origins, transparent/opaque-bound failures, or a non-material ROI size keep the original path.

## Limits

The 112-input sample is a single frame and the 8-layer sequence is a title-only oracle. The full P9 project includes media, transitions, and title layers outside this subset. A packaged native worker must still render the actual edited sequence and pass its frame, audio, and delivery checks before claiming full-project export performance or parity.

One untested follow-up is single-decode looping for each static PNG input (`loop` filter after decode instead of repeatedly demux-decoding the same PNG). It may reduce repeated decode work, but it is not part of this patch and needs a moving 30-frame parity, runtime, and memory comparison against the current one-thread input policy.
