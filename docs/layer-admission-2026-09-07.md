# Copy-free, strict layer admission

## Changes and correctness defect

`compile_fill_layer_program` copied the whole edit state, including every raster,
then zeroed texture, clarity, sharpening and noise-reduction controls before
checking admission. Current admission supports these detail programs directly,
so that copy is unnecessary. For the measured 4208x3120 source, its raster alone
contains 210,063,360 bytes. Admission now reads the original const state.

The zeroing also masked invalid inputs. A failing-before regression admitted 28
of 60 NaN/infinity detail-control cases. Admission now validates the actual detail
values, including texture/clarity, instead of overwriting them before validation.
All 60 invalid cases are rejected. Eighty finite cases across five controls and
four working spaces remain admitted, retain their active detail-stage flags and
produce the same rendered checksum, `3231156354630797909`, before and after.

After measuring the remaining source-validation cost, the finite-pixel check was
changed from short-circuiting channel checks to an unsigned reduction over all
RGBA components. This permits block/vector execution without skipping values,
trusting revisions, weakening nonfinite rejection or changing image pixels.
Both normal finite and invalid input spans are fully scanned by this reduction.
The existing payload byte-comparison and lifetime ownership checks remain intact.

## Bounded optimization loop

The `photo-editor-perf-loop` skill governed the work: maximum three attempts,
at least 20% median warm improvement, unchanged output hashes/upload counts,
and no more than 10% p95 or peak-memory regression. Two attempts were used.

Artifact root: `/private/tmp/layer-admission-loop-EE2des/`.

1. Copy removal and actual-detail validation preserved output but improved warm
   median only 11.6% (31.947 to 28.238 ms). `attempt1-comparison.json` correctly
   rejects it as insufficient for the performance gate.
2. An additional admission probe measured about 10 ms for the finite source
   validation pass. The reduction-based implementation measures about 6 ms in
   that diagnostic. `probe-before.log` and `attempt2.log` use the same extra
   diagnostic work outside the timed publication. Their comparison passes.
3. For the final promotion comparison, the extra admission probe is disabled,
   restoring the original work sequence. This is a confirmation of attempt two,
   not a third optimization. `final-comparison.json` passes every original gate.

| Original work-sequence measurement | Baseline | Final |
| --- | --- | --- |
| Warm median | 31.9465 ms | 22.7820 ms |
| Warm p95, nearest rank | 40.8168 ms | 31.3017 ms |
| Cold publication | 140.5051 ms | 141.3143 ms |
| Peak native memory footprint | 1,616,299,736 B | 1,616,152,280 B |

Warm median improves **28.69%**; cold time and peak memory are effectively
unchanged. Removing a large temporary copy is not evidence of lower process peak
memory, because other allocations and allocator retention can dominate the peak.
All twelve GPU-readback SHA-256 hashes and upload counts match exactly. Every
iteration changes opacity and produces a distinct image; this is not repeated
publication of a stale frame.

The accepted comparison uses `before.log` and `final-same-harness.log`:

```sh
/usr/bin/time -l build/release-macos/tests/photo_layer_sampling_image_test \
  /private/tmp/framehouse-six-raw-source-v3/hdrplus-nexus6-skin-0218/hdrplus-nexus6-skin-0218.dng \
  NEW_ABSOLUTE_OUTPUT_DIRECTORY --metal --scale 0.12 --full-resolution --benchmark-frames 12

python3 -B scripts/perf/compare-layer-publication.py \
  --before /private/tmp/layer-admission-loop-EE2des/before.log \
  --after /private/tmp/layer-admission-loop-EE2des/final-same-harness.log
```

The optional `--probe-admission` flag enables the extra diagnostic in future
runs; it is intentionally off for the final comparison. The source is decoded
at 4208x3120, placed at 12% scale/17-degree rotation on a 697x567 canvas in Linear
ProPhoto. Publication includes native readback but excludes source decoding and
desktop compilation. No builds or other tests run concurrently with the timed
comparison. This remains a local component benchmark, not foreground UI proof.

## Regression evidence

- `photo_layer_detail_admission_test`: 80 finite detail cases retain their
  before/after rendered checksum, 60 invalid detail cases are rejected, and 72
  nonfinite raster cases test every channel at six positions in a 257-pixel
  span, including the odd tail. Logs: `detail-before.log`, `detail-final.log`.
- `photo_layer_payload_reuse_test` additionally tests NaN and positive/negative
  infinity in first/last RGBA channels, and admits finite signed zero,
  subnormals and positive/negative maximum finite floats. Existing mutation,
  metadata and ownership tests remain active: `validation-test.log`.
- Eleven focused regressions pass in 11.22 seconds: `regressions.log`. They
  cover detail admission, payload reuse, sampling, photographic sampling, alpha,
  photographic alpha, layer input color, layer LUT parity, canonical working
  fusion, editor preview math and general render parity.
- Native Metal sampling (3,240 cases), alpha (4,096 cases plus invariants), and
  the broader color-pipeline parity test pass unchanged numerical gates:
  `native-sampling.log`, `native-alpha.log`, `native-pipeline.log`.
- The real 4208x3120 JPEG reduction passes independent working-pixel comparisons
  on CPU/retained/Metal across all four working spaces and one-level visible
  preview/output agreement: `jpeg-proof.log`. Its ProPhoto composite was
  visually inspected. The RAW repeated-edit outputs remain byte-exact against
  the pre-change native baseline. Input originals were not modified.
- The desktop and focused targets build without reported warnings in the final
  logs: `final-build.log`, `final-probe-build.log`. Scoped whitespace checks pass.

## Limits

No whole-app realtime claim follows from a 23 ms warm layer publication. Decode,
catalog/source loading, desktop recipe compilation, global color stages, queues,
presentation and foreground contention are outside the benchmark. Cold
publication still takes about 140 ms, and output dimensions remain preview-sized
even though the source is full-resolution. The finite reduction trades early
exit on malformed pixels for a complete scan; it still rejects all tested
nonfinite inputs.

Diverse-camera color calibration, broader catalog/runtime recovery, embedded
model provisioning and inference, temporal 4D and film fidelity, and complete
professional editing coverage remain unfinished in the active goal.
