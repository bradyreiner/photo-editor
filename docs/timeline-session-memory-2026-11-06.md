# Long-session memory follow-up — November 6, 2026

## Outcome

Found and corrected native-object retention in the polling test harness. The
harness called media APIs directly between event-pump iterations; temporary
Objective-C objects from those calls could outlive the cycle despite the Qt
controller/player having been destroyed. Supplying a cycle autorelease boundary
reproduced normal native-event ownership and removed most of the growth in an
isolated native-player test. This is a test correction, not a claim that every
editor memory path is leak-free. No production source, cache budget, or eviction
policy changed.

Luna xhigh agents reviewed lifecycle, audio, and fixture/cache ownership. Root
owned all builds and native runs, holding both release/debug build locks.
Artifacts are in `perf-artifacts/timeline-session-memory-2026-11-06/`.

## What changed

- The full Timeline fixture now owns an autorelease pool around each cycle and
  measures again after both controller and document destruction. Release builds
  validate QObject destruction with QPointer; DeferredDelete is drained explicitly.
- Memory checkpoints distinguish RSS, all-zone live/reserved heap, live blocks,
  native physical footprint/media/graphics memory, and thread count. Optional
  vmmap/heap/thread snapshots run only when requested; they are attribution tools,
  not latency measurements.
- A windowless native-player regression uses actual H.264/AAC media and actual
  QMediaPlayer/QVideoSink/QAudioOutput teardown. Default 24 cycles require successful
  loading, a real decoded frame, object destruction and available memory samples.
  After six warm-up cycles, runs with at least twelve further samples enforce a
  live-heap span of at most 8 MiB and a thread-count span of at most twelve.
  RSS is recorded without treating allocator-reserved pages as a live leak.
- `--without-cycle-autorelease-pool` reproduces the old ownership boundary and
  remains subject to the memory guard. Short diagnostic runs explicitly report
  when too few samples exist to enforce the guard.
- A separate silent audio-sink isolator distinguishes sink recreation from native
  media-player retention. Lost window exposure is reported as unavailable native
  qualification, rather than a decoded-frame assertion against an unavailable UI.

Qt 6.10.2's Cocoa dispatcher constructs its autorelease pool *inside*
`processEvents()`. A new nested pool cannot release objects that were autoreleased
by direct test calls into the outer pool. The source and installed binary were
checked: [Qt Cocoa dispatcher](https://github.com/qt/qtbase/blob/v6.10.2/src/plugins/platforms/cocoa/qcocoaeventdispatcher.mm#L263-L295).
The new test helper uses the public Foundation NSAutoreleasePool API; it neither
flushes caches nor calls allocator pressure relief.

## Fixed-binary B-C-C-B evidence

All four runs use the same frozen executable and generated 640×360 H.264/AAC
fixture. Only the explicit cycle-pool switch differs. Each run completed all
24 loads, decoded frames, and QObject destruction checks: 96 successful cycles.

| Order | Cycle pool | Final RSS MiB | Final live heap MiB | Late heap growth MiB/cycle | Final threads |
|---|---|---:|---:|---:|---:|
| B1 | off | 107.94 | 28.88 | 1.003 | 78 |
| C1 | on | 82.95 | 5.51 | 0.032 | 29 |
| C2 | on | 82.84 | 5.43 | 0.026 | 28 |
| B2 | off | 108.00 | 28.92 | 1.005 | 78 |

Mean final RSS fell **23.2%**; post-warm live-heap growth fell **97.1%**. The native
load/frame/destruction checks were identical. This meets the declared attribution
target (50% lower post-warm growth and 10% lower final RSS), but is not an app
latency benchmark or a physical displayed-frame measurement.

Evidence: `native-player-comparison.json`, `native-player-{manual,pool,pool2,manual2}.jsonl`,
and the hashed `native-player-comparison-bin`. The original binary predates the
regression guard; the final binary makes the old path fail that guard.

## Audio control

All four combinations of sink recreation/reuse and cycle pool off/on passed
12×5 source starts: 28,800 accepted silence frames, zero sink errors and no surviving
QAudioSink QPointers. Recreating sixty sinks versus restarting twelve sinks left
RSS around 69 MiB, live heap around 3.5 MiB, and threads fixed at ten. This does
not support a production sink-reuse patch for the memory issue. Same-format restart
also recreates the Darwin audio stream/AudioUnit internally, so QObject reuse
would not establish a native resource leak fix.

Evidence: `audio-{recreate,recreate-pool,reuse,reuse-pool}.jsonl` and `audio-review.md`.

## Limits and earlier data

The earlier full 96-clip fixture warmed a bounded 128 MiB processed-frame cache and
showed RSS around 230→403 MiB. It sampled before the document destructor and lacked
the cycle native lifetime boundary. Those numbers are not sufficient evidence of
a production leak. The isolated reproduction attributes a substantial confound;
we have not yet measured the corrected full mixed-codec Timeline in a visible UI.

Current full-workflow attempts lost window exposure; one earlier attempt also
hit the recorded original-ProRes paused-preroll timeout while still exposed.
Neither condition is being hidden or counted as a passing full workflow. The user
chose to continue with windowless checks. Native-player teardown checks therefore
continue independently of the unavailable Timeline window. The frame cache remains
unchanged, and full editor/HDR/multidisplay presentation is not qualified here.

## Final verification

- **128 native cycles passed**: every source loaded, every decoded frame arrived,
  every QObject was destroyed, and every required memory sample was available.
  After warm-up, live heap spanned **1,095,840 bytes (1.05 MiB)** and thread count
  spanned five, passing the unchanged 8 MiB/twelve-thread limits. Final live heap
  was **5.86 MiB**, RSS **83.86 MiB**, and thread count 28. Live heap was already
  5.83 MiB at cycle63, so the late run remained nearly flat.
- **Negative control works**: all 24 loads/frames/destructions still passed, but
  omitting the cycle pool produced **17.12 MiB** post-warm live-heap span and a
  **36-thread** span. Both guards failed and the process returned 1 as expected.
- **Explicit event-loop mode passed** 24 cycles and both memory guards.
- **Five focused Timeline/audio tests passed**, and the registered 24-cycle CTest
  memory regression passed with the backend pinned to Darwin.
- Four independent audio control runs passed. Production-source hashes match
  the prior verified build; only tests/configuration/documentation changed.
  Scoped whitespace checks passed; source and binary hashes are recorded.

Evidence: `final-native-128.jsonl`, `final-negative-control.jsonl`,
`final-event-loop.jsonl`, `focused-tests.log`, `registered-native-test.log`,
`final-manifest.json`, and `diff-check.log`.

The corrected full Timeline fixture was compiled, but was not rerun in a visible
window after the user selected windowless checks. The earlier full 96-clip RSS
observation should remain unqualified pending that corrected run. No product
memory win or full-app leak-free claim is inferred from this isolated proof.
