# Reactive layer-inspector snapshot

This bounded performance pass targets the measured foreground redo stall in the
real RAW/JPEG placed-layer app workflow. The preceding export turn is verified
progress, not completion of the active app-wide goal.

## Baseline and acceptance gates

The normal Export panel routes through `ExportJobController` and persistent
jobs, not the single-image compatibility APIs. That existing queue must not be
replaced with a nonpersistent single-flight API merely to claim integration.
This pass instead follows the confirmed everyday editing bottleneck.

The native profile at `/private/tmp/redo-admission-baseline-ZxoR9M/app-sample.txt`
attributes 175 of 176 sampled redo stacks to `controlsChanged` notification from
`applyState`. Inspector bindings repeatedly read `photoLayers`, rebuilding
presentation maps and crossing the QML/native reference-sequence boundary.
Profiler attachment distorted undo timing; it is diagnostic evidence only.

The clean, unprofiled baseline is
`/private/tmp/redo-admission-clean-baseline-O0xptH/receipt.json`:

- Redo admission: 1079ms; undo: 49ms.
- Proof elapsed: 35977ms; maximum observation gap: 1137ms.
- Peak memory footprint: 4885680616 bytes; maximum RSS: 4692000768 bytes, from
  the `/usr/bin/time -l` output in `launch.log`.
- Real 4208x3120 HDR+ Nexus 6 DNG base and placed reference JPEG; isolated
  appdata/cache/preferences and intentionally unavailable catalog service.

Following the bounded performance-loop guidance: at most three candidate
attempts, redo admission at least 80% lower (at most 215.8ms), whole-proof time
and peak memory no more than 10% worse, unchanged rendered output and history
semantics, and the existing 45-second app deadline. No preview-quality reduction,
lossy cache, skipped UI notification or dropped thumbnail work is acceptable.

## Candidate 1

`PhotoLayerSnapshot.qml` depends on `photoLayerCount`, which shares the exact
`controlsChanged` notification with `photoLayers`, including thumbnail updates.
The new `photoLayerSnapshot()` invokable returns the same presentation data as
a copy sequence. Studio inspector reads share that reactive snapshot instead
of repeatedly reading the controller's native reference sequence. No recipe,
renderer, history, persistence or worker algorithm changes are made.

[Qt documents the reference-sequence read behavior](https://doc.qt.io/qt-6/qtqml-typesystem-references.html).
The focused QML test checks repeated reads, same-count edits, thumbnail-only
notifications, reorder/removal and detached captured values. App timing, rendered
output, memory and visual checks decide whether the candidate is accepted.

## Results and comparison validity

The three focused native tests pass: `photo_layer_snapshot_test`,
`source_layer_relink_test` and `photo_export_recovery_test` (16.48 seconds total;
`/private/tmp/redo-snapshot-focused-tests.log`). The new component passes qmllint
without diagnostics. Both native builds finish without compiler warnings/errors.
The source-contract expectation now references the snapshot-backed selected row;
the complete source-contract/broad controller suites have not been rerun here.

The first candidate observation at
`/private/tmp/redo-snapshot-candidate-L1DrwE/` is **not accepted**. Redo improves to
200ms but the proof takes 45395ms, including an 11285ms export phase, violating
the declared elapsed/deadline gates. Receipt analysis then finds all 135 sampled
window states inactive, unlike all 156 active baseline samples. The native proof
itself reported ok despite late completion; the independent comparison correctly
rejects it. Its deadline handling is not strengthened by this optimization.

Computer Use could not load its runtime, so no external activation was performed.
The unchanged-candidate rerun at
`/private/tmp/redo-snapshot-confirmation-cENn2S/` was independently recorded active
for all 151 samples and passes every gate:

| Metric | Clean baseline | Matched foreground candidate |
| --- | ---: | ---: |
| Redo admission | 1079ms | 56ms |
| Undo admission | 49ms | 59ms |
| Whole proof elapsed | 35977ms | 32292ms |
| Peak memory footprint | 4885680616 bytes | 4886811112 bytes |

Redo improves 94.8%, whole-proof time improves 10.2%, and peak memory changes
about +0.02%. Undo is reported separately; the predeclared timing regression
guard applies to whole-proof elapsed time, not this small single-call sample.
The placed-layer PNG is byte-identical, including its color metadata. The native
proof additionally requires inspector rows to disappear on undo and restore the
correct source path on redo. The captured screenshot was visually inspected:
the photo, source-layer thumbnail and controls remain visible and coherent.

`scripts/perf/compare-layer-history-proof.py` owns the numerical gates and now
also rejects mismatched/non-foreground execution. The unchanged-baseline negative
control fails as required. Per-run `comparison.json` records all gate decisions.
Only one code candidate has been attempted; the inactive observation is retained,
not silently discarded or reclassified as successful performance evidence.

Final repeat: `/private/tmp/redo-snapshot-final-ZugD6x/comparison.json` also passes
all gates with foreground execution, redo 55ms, undo 55ms, whole proof 33183ms,
peak memory 4921283048 bytes (+0.73% versus baseline), and the same export hash.
**Candidate 1 is accepted** on these two matched foreground observations. This
is not a p95 distribution. Maximum whole-proof observation gap still reaches
1020ms; export-phase observation gap is 261ms on the 200ms timer. Other UI
stalls remain and require separate profiles.

Reproduce the gate with:

```sh
python3 -B scripts/perf/compare-layer-history-proof.py \
  --baseline /private/tmp/redo-admission-clean-baseline-O0xptH \
  --candidate /private/tmp/redo-snapshot-final-ZugD6x
```

This is a bounded source-layer inspector improvement, not full-resolution realtime,
all-tool/large-layer-stack responsiveness, independent color correctness, background
QoS, or completion of the embedded-model and 3D/4D reconstruction requirements.
