# RAW mask profiling and rejected optimization loop

Status: two candidates rejected and reverted. No engine speedup is claimed.
Retained changes are expanded color regressions, a repeated-edit benchmark and
an artifact comparator. The full app completion goal remains open.

Follow-up: `refined-mask-coverage-reuse-2026-09-07.md` implements and verifies
dependency-checked coverage reuse. Its accepted 44% RAW / 58% JPEG gains are
separate from the rejected experiments recorded below.

## Gates and results

The performance-loop skill set a maximum of three attempts, at least 15% lower
median local-edit render time, exact PNG output, no more than 5% higher median
comparison time, and no more than 10% higher host peak memory. Stop early for
flaky measurements or growing architectural risk. This loop stopped after two
misses and substantial candidate timing variation.

Same existing preview request entry point, real Nexus 6 DNG, ACES 2, 1600x1186,
CPU image output, isolated caches and three independent process runs per group:

| Candidate | Baseline render ms | Candidate render ms | Median gain | Result |
| --- | --- | --- | --- | --- |
| Compile neutral local tone/color dispatch | 147, 140, 144 | 142, 135, 230 | 1.39% | Reverted |
| Reuse fixed morphology neighborhood distances | 147, 147, 141 | 180, 141, 132 | 4.08% | Reverted |

Both candidates passed the paired eight-PNG byte-identity, protected-region,
comparison-latency and peak-memory guards. The distance candidate also passed
20,824 exact signed-offset comparisons across all radii 1–24; its temporary
helper and test were removed with the rejected candidate. The strengthened
general local-color test remains.

The comparator `scripts/perf/compare-raw-local-edit.py` fails closed for missing
receipts, host memory, required PNGs or functional guards. Comparing a baseline
to itself exits 1 for zero speedup; missing input runs exit 2. It does not replace
the driver's actual image and reference checks, and is not a promotion corpus.

Artifacts:

```text
/private/tmp/raw-local-current-7fHWlH
/private/tmp/raw-local-dispatch-baWyqE
/private/tmp/raw-local-dispatch-comparison.json
/private/tmp/raw-morphology-before-ZNCk2j
/private/tmp/raw-morphology-after-X5Qe3i
/private/tmp/raw-morphology-comparison.json
/private/tmp/raw-local-comparator-self.json
```

## Profile: repeated image-dependent mask cleanup

The restored engine was sampled with macOS `sample` while rendering 24 changing
local temperatures at the actual 4208x3120 RAW resolution. The profile contains
heavy `local_mask_set_weight -> local_mask_weight ->
apply_raster_selection_morphology` stacks, including edge-aware averages,
neighbor averages and medians. This recipe receives the normal background-mask
cleanup defaults; it is not a mask with all refinements disabled.

In one main-thread subtree, 1,062 of 1,145 sampled local-loop calls descend into
mask weight evaluation; 979 of the nested-mask samples reach the refinement
path. These are sampled stack counts, **not** exclusive wall-time percentages
or a whole-app responsiveness claim. Worker threads and non-render verification
also appear in the profile. Removing fixed distance math barely improves the
measured real workflow, so it is not the architectural solution.

Evidence: `/private/tmp/raw-local-sample-gP8ckB/sample.txt`, `sample.log`,
`render.log`, and `output/receipt.json`. Sampled timings are diagnostic only;
they are not comparable to uninstrumented speed gates. The full-resolution
output was visually inspected; visible RAW noise remains unresolved.

The next implementation target is reusable refined mask coverage in the render
execution graph, with explicit dependencies. Correctness requires matching
source/mask bytes, all refinement and composition settings, edge-reference
pixels, working color context, crop/source coordinate mapping, and prior mask
effects for pixel-dependent ranges. Cancellation, bounded memory and stale
result rejection must hold. Reusing coverage merely by mask ID, local-slider
change, or a pointer is insufficient. This architecture is **not implemented**
by the current profiling work.

## Retained verification improvements

- `compiled_local_adjustment_test` now makes **1,135,872 bit-exact comparisons**:
  four working RGB spaces, three photographic rendering models, two headrooms,
  two adaptations, six weights, all supported local color controls, signed/HDR
  samples, signed zero and controls near neutral thresholds. It compares the
  compiled route with the existing uncompiled evaluator, not an independent
  photographic reference.
- `legacy_preview_locality_driver ... --benchmark-local` runs 24 distinct local
  temperature edits through the actual preview request path. It records render,
  pipeline/output/decode times, backend/fallback, decoded-cache reuse and RGBA
  SHA-256. Every image must differ from neutral and from all previous edits;
  the rightmost quarter must match neutral exactly. Checks and hashing happen
  outside render timing. It preserves normal input characterization and output
  transforms and does not lower source resolution unless explicitly requested.
- Full-resolution RAW: 24 distinct hashes, protected pixels unchanged,
  `/private/tmp/raw-local-sample-gP8ckB/output/receipt.json`.
- JPEG 1600x1186: 24 distinct hashes, protected pixels unchanged,
  `/private/tmp/local-benchmark-jpeg-bpeu6L/output/receipt.json`.
- Restored engine and updated drivers build. Five focused tests pass in 4.69s:
  compiled local adjustment, color pipeline, preview math, and legacy/ACES 2
  reference workflows. `/private/tmp/raw-local-final-regressions.log`.

Reproduce with a new output directory:

```sh
QT_QPA_PLATFORM=offscreen build/release-macos/tests/legacy_preview_locality_driver SOURCE NEW_OUTPUT_DIR aces2 0 --benchmark-local
python3 -B scripts/perf/compare-raw-local-edit.py BEFORE_RUN_ROOT AFTER_RUN_ROOT
```

CPU worker correctness and profiling do not establish foreground realtime,
multi-camera RAW color accuracy, GPU execution, stylus feel, or completed
Gaussian/embedded-model/professional editing workflows. No originals, user
catalog, model weights or preexisting projects were modified.
