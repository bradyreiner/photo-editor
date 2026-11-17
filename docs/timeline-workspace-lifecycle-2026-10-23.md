# Timeline workspace lifecycle and navigation

This change keeps Timeline in the same Looksy application while separating the
editing session from resources that can be suspended or recreated. It preserves
the existing dirty checkout. Luna xhigh specialists handled playback resource
release, scheduling, and the packaged-app probe; integration and native UI
verification were handled together.

## Behavior

- Navigation commits and finishes when the destination is ready, using deferred
  callbacks guarded by a route generation. It no longer waits for the former
  45 ms commit and 110 ms finish QML timers. Superseded routes cannot consume a
  newer tool request or clear its transition.
- Leaving Timeline pauses playback and cancels pending preview work immediately.
  After a two-second grace period, an inactive Program controller unloads its
  primary and prefetch media sources, releases its native frame references,
  processed-image state, and grade sessions. Reactivation reloads the retained
  source at the document playhead. On the Darwin media backend, a fresh decoder
  needs bounded, silent native preroll before a paused seek can produce the
  correct picture. Warmup keeps the document paused, detaches audio output,
  discards the initial frame, seeks to the retained target, and pauses before
  publishing the target picture. It has a two-second deadline and waits for
  the native output window to become exposed. A stale release callback cannot unload a
  controller that has become active again.
- A Play request arriving during decoder warmup is retained until the target
  picture is ready. Pause, source changes and leaving the workspace cancel it;
  rapid scrubbing coalesces toward the latest target. Program, fullscreen and
  multicam transport controls show Pause while Play is pending, and Space can
  cancel that request. Actual document playback stays paused during warmup.
  Queued audio/video callbacks carry generations so old deliveries cannot
  survive a source, seek or suspension boundary.
- After ten seconds away, the Timeline shell releases its dock content. Panel
  visibility, placement, layout, searches, media presentation, source position,
  authoring drafts and selected panel pages survive independently of the objects.
  Closed dock content is also eligible for eviction on a thirty-second sweep.
  Only exposed panels need to load on return; all eleven panel buttons remain
  available. The timeline document and Undo history are retained.
- Source shows a short loading state while a fresh decoder obtains the retained
  picture, rather than displaying its unrelated first-frame poster.
- The playing-to-paused transition keeps the last complete Program picture
  visible while its paused render is prepared. Paused crop/settings edits, source
  changes and actual hibernation still invalidate stale image identity.
- Only an exposed Source viewer owns Source playback. Media and Source no longer
  duplicate decoding. Source pauses immediately on leaving, with a two-second
  release grace. Hidden media panels stop filtering and cannot repeatedly clear
  the foreground Library's thumbnail range. Pending searches apply on return.
- The app publishes the destination and transition state to background scheduling.
  Stale hidden-workspace publications cannot replace foreground ownership. Queue
  priority updates are coalesced, with precomputed clip/path ranks rather than
  full document scans inside sort comparisons. Queued deferrable Timeline work
  yields to the foreground workspace. Export, render, autosave and recovery remain
  eligible. Compatibility callers retain their interaction-release behavior.

## Measurement and safeguards

The source-QML driver has a dedicated native lifecycle route with twenty measured
Edit/Library/Timeline cycles, presented-frame timing, macOS physical-footprint and
RSS sampling, document/selection/playhead/Undo checks, an actual idle release, and
panel reconstruction. Its first diagnostic run is retained, along with subsequent
failed or ineligible runs; it is not production AOT evidence.

The packaged app has a separate opt-in lifecycle probe that uses its actual
bundled QML, native window, catalog/controller path and presented frames. It
requires caller-owned runtime/cache paths and a local test database, writes a
complete receipt, and preserves the 1500 ms first-Timeline, 250 ms warm-route,
and 64 MiB repeated-cycle growth gates. Foreground eligibility is required. AppKit samples independently record native
application activation, main/key window state, occlusion and frontmost process
identity; they do not change focus or bypass eligibility checks.

Performance work follows the bounded baseline/change/identical-route comparison
workflow. The user deferred final foreground timing on October 23 after Chrome
took focus during the packaged run. The AppKit receipt identifies another
frontmost process at loss; these interrupted runs do not establish route latency
or repeated-cycle memory stability. The timing thresholds remain unchanged. A baseline affected by concurrent compilation or foreground loss is
diagnostic evidence, not a qualified before/after speedup.

Playback diagnostics report actual source ownership, native frame references,
processed-image bytes, worker activity and grade-session counts. They separately
report the shared processed-frame cache, which keeps its existing 128 MiB/240-frame
bound. The cache is not falsely counted as reclaimed when one controller
hibernates. Qt Quick texture reclamation is internal to the renderer; process
footprint and native reference counts do not provide a separate GPU allocation
measurement or a hard cap on all application memory.

## Verification

The full native editing/lifecycle receipt is a passing run before the final
loading-label presentation change. The final Source presentation is additionally
verified in a focused native component using the real QML/decoder with isolated
model fixtures. A later broad rerun stopped earlier at its independent media-bin
and smart-collection interaction; that failure remains in the evidence. The
focused result does not replace the broad test or claim another complete pass.

The focused controller, recovery, scheduling and UI checks run against isolated
local test databases. The original stale-revision and resume assertions, test
timeouts and route thresholds were retained. Native codec checks use generated
red/blue video so a correct timecode with an incorrect picture cannot pass.
The Source test checks the first requested blue frame at 750 ms within 100 ms.
After explicit Play, Pause is checked against the position at the stop request
with the same tolerance; it does not incorrectly require the running clock to
stay at its earlier seek target. The passing receipt records first frame at
785 ms, Pause requested at 831 ms and stopped at 832 ms, and Source restored at
911 ms for the retained 832 ms target. This is not frame-exact decode proof.

Final receipts, exact source/binary hashes, captures and remaining gates are
recorded in `docs/evidence/timeline-workspace-lifecycle-2026-10-23/`.

Review the [loading capture](evidence/timeline-workspace-lifecycle-2026-10-23/captures/timeline-source-loading/source-loading-native.png),
[ready Source capture](evidence/timeline-workspace-lifecycle-2026-10-23/captures/timeline-source-loading/source-ready-blue-native.png),
[passing full lifecycle receipt](evidence/timeline-workspace-lifecycle-2026-10-23/native-runs/looksy-native-timeline-smoke_edit-lifecyclefinal4/captures/workflow.json),
[strict audio failure](evidence/timeline-workspace-lifecycle-2026-10-23/runs/agent-final-native-audio.log),
and [complete evidence manifest](evidence/timeline-workspace-lifecycle-2026-10-23/verification.json).

| Check | Result |
| --- | --- |
| Release app build | Passed with the final lifecycle API and native diagnostics |
| Bootstrap, document controller, recovery | Passed at original 60 s per-test deadlines |
| Playback, frame scheduling, audio output unit tests | Passed; includes paused-image retention, crop invalidation and old audio-generation rejection |
| Native Cocoa playback lifecycle | Passed; real red/blue pixels, paused state, exact hash/Undo/selection preservation, resource release and cancelled Play |
| Media browser, dock, Color, Mixer and timeline UI contracts | Passed focused checks |
| Full native editing and Source eviction | Passed `lifecyclefinal4` in 83.792 s; restored frame verified after actual panel/player destruction |
| Native keyframe workflow | Passed `lifecyclefinal1` in 10.326 s |
| Final Source loading presentation | Passed focused native Cocoa component test in 2.12 s, no QML warnings; requested 750 ms, acknowledged 770 ms, paused blue frame at 807 ms |
| Native Program tone/RMS qualification | Failed: maximum left/right RMS zero on the installed Darwin backend |
| Foreground route latency and repeated-cycle footprint | Deferred by the user; thresholds unchanged |

The regression run initially caught stale-proxy and photo-preview resume failures
from over-broad scheduling/coalescing. Those behaviors were corrected while
retaining their original assertions. The original paused-frame scheduling test
also caught a reload heuristic that incorrectly treated an injected same-source
frame as a missing media source; actual hibernation now uses its explicit state.

The native playback fixture waits for initial metadata/integrity hydration before
capturing the document hash. Its source timestamp oracle follows the active
Program clip, independently of the Inspector selection. Actual Darwin decoding
is checked on Cocoa because this host provides the Darwin media backend, which
does not decode through the offscreen platform event dispatcher. Native decoded
frame checks remain distinct from headless controller tests. Darwin can return
an unknown frame PTS; receipts retain that value and mark it estimated. The
red/blue pixel oracle independently checks the decoded content, with a 100 ms
position tolerance. The matching [Qt 6.10.2 Darwin player](https://github.com/qt/qtmultimedia/blob/v6.10.2/src/plugins/multimedia/darwin/mediaplayer/avfmediaplayer.mm#L793) reports a requested seek position before native completion; its [video renderer](https://github.com/qt/qtmultimedia/blob/v6.10.2/src/plugins/multimedia/darwin/mediaplayer/avfvideorenderercontrol.mm#L135) does not attach a frame PTS. The tone-backed fixture caught a stale red frame accompanying the optimistic blue-frame timecode; timecode alone is therefore insufficient.


### Separate native audio qualification

The strict `timeline_playback_controller_cocoa_audio_qualification_test` is
registered and runnable with `--native-cocoa-audio`; it retains a real tone/RMS
assertion. Its native run completed the shared lifecycle checks and failed audio
activity with maximum left and right RMS both zero. This is a pre-existing
backend capability gap, not a green native-audio result. The installed Qt 6.10.2
Darwin player does not override the base [no-op audio-buffer output method](https://github.com/qt/qtmultimedia/blob/v6.10.2/src/multimedia/platform/qplatformmediaplayer_p.h#L67).
The lifecycle test separately verifies attachment and suppression, and the unit
test checks rejection of queued audio from an old generation. No buffer samples
or meter values are fabricated, and native output is not substituted around the
mixer/DSP chain. Export audio and other platforms are outside this gate.

The final foreground performance run is deferred at the user's request. Keep the
following gates pending until a native run stays active and exposed for the
entire route: first Timeline at most 1500 ms; twenty warm routes per workspace
with p95 at most 250 ms; repeated-cycle physical-footprint growth at most 64 MiB.
A loaded-source test pass or cleared decoder references does not substitute for
those measurements. No qualified before/after speedup is reported.

This work does not qualify sustained 4K editing, arbitrary large-project memory,
or complete application release readiness. Running background subprocesses and
shared framework allocations must be considered separately from queued-work
priority and the resources owned by a suspended workspace.
