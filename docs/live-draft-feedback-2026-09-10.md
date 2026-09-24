# Live draft feedback and hidden scopes, 2026-09-10

This pass fixes missing feedback while dragging after a full native preview has
already been displayed. It does not claim to eliminate all main-thread stalls.

## Causes and changes

- A coalesced edit could advance the generation while its draft was still
  rendering. The controller now retains the newest pending edit until the
  interactive/overlay worker finishes. Ending the transaction still dispatches
  the final state and preserves a single undo step.
- Full-size native previews routed subsequent drafts into the viewport-overlay
  path. That path processes the full source region before resizing and only
  supports Qt image decoders. Live drafts now use the existing bounded shared
  preview pipeline, with its existing resolution caps and RAW/color handling.
  RAW and native TIFF previews cannot enter the Qt-only inspection overlay.
- Routed inspection overlays used the main preview generation while their
  publisher compared the independent overlay generation, which also advances
  on viewport changes. Cancellation and publication now use the same clock.
- `StudioScopeTile` ignores paint requests while hidden. Its non-histogram
  canvas also skips Histogram mode, which already has a dedicated histogram
  component. A native sample found substantial Canvas work on the GUI thread;
  temporary paint timing recorded 209 ms of redundant Histogram-canvas work
  across 15 calls in one fixture run. Visible scope painting is preserved.

No recipe schema, persistence acknowledgement, checkpoint durability, image
processing math or draft resolution limit changes in this pass.

## Measurements and limits

The native QML test loads the same large-mask recipe and 1800 × 1200 synthetic
source used in the preceding pass. Three single curve edits precede a burst of
30 updates. The corrected baseline counts overlay publications as well as
native texture revisions: both were zero during input.

| Metric | Baseline | Candidate |
| --- | ---: | ---: |
| Median first visible frame, three edits | 560 ms | 205 ms |
| Presented native updates during drag | 0 publications | 9 frames |
| Worst setter during drag | 13 ms | 13 ms |
| Burst input duration | 524 ms | 1353 ms |
| End of input to settled result | 742 ms | 689 ms |
| Maximum GUI heartbeat gap | 325 ms | 328 ms |

First-frame latency improves 63.4%, and intermediate drafts are actually
presented. Total burst time increases from 1266 to 2042 ms: displaying the new
intermediate results creates work that the broken baseline skipped. This is
not a universal latency improvement. The roughly 330 ms maximum GUI gap remains
unresolved. The first targeted scheduler-only attempts still produced no visible
updates; the request routing and generation fixes were necessary.

The test now counts native and overlay frames and supports
`--require-live-feedback`. Its input loop always yields between events, even
when a setter exceeds the nominal 16 ms interval. Previously such a setter could
make the test run all remaining inputs synchronously and artificially starve
render timers. This correction especially affects the heavy RAW scenario;
old RAW burst timings cannot be compared directly with the corrected test.

The corrected real CR2 workflow passes the live-feedback requirement:

- First visible result: 254 ms.
- Fourteen presented native updates during thirty curve changes.
- Final current-generation native preview: 6024 × 4020.
- Input duration: 4414 ms; worst setter: 87 ms; total through settled result:
  10345 ms; maximum GUI heartbeat gap: 1137 ms.

RAW setters, main-thread publication/bindings and save preparation still need
work. The fixture intentionally has no catalog helper; its save warnings and
retries are isolated from the user's catalog. Persistence was not changed.

## Validation

- `editor_preview_coalesced_undo_test` passes, including new bounded-draft/RAW
  routing and retained pending-drag tests, plus existing undo/redo coverage.
- `studio_bounded_input_test` passes. Its existing unrelated recovery-banner
  null-width warning remains; this pass does not alter that component.
- Native RAW proof retains a 1024 × 683 draft, zero draft/reference difference,
  zero cached viewport difference and at most 1/255 preview/export channel
  difference. Draft, full and matched-reference PNGs match the prior pass
  byte-for-byte.
- Actual QML screenshots and a scope reveal check accompany the native proof.
  The visible waveform, parade and vectorscope were inspected in
  `scopes-visible/scopes-after.png` after opening the real scope dock.
- Release build completed without compiler warnings. The app was relaunched
  with `./scripts/run-native.sh --no-build` (PID 17418, startup ready at
  20:30:38 UTC). The existing journal was backed up with zero pending/active
  saves before relaunch; no catalog reset or migration was performed.

Artifacts: `/private/tmp/framehouse-live-save/`, including archived binaries and
source snapshots, profiling samples, full telemetry, screenshots and build/test
logs. `before-overlay-*` is the corrected publication baseline;
`synthetic-verified-*` and `raw-ui-verified-*` contain the accepted interaction
checks. `raw-parity-final*` is the final pixel check and runs alongside the app
build, so its timing is not a performance measurement.

```sh
build/release-macos/tests/editor_preview_coalesced_undo_test
QT_QPA_PLATFORM=offscreen build/release-macos/tests/studio_bounded_input_test
QT_QPA_PLATFORM=cocoa \
PHOTO_EDITOR_OBSERVABILITY_RUN_DIR=/private/tmp/live-feedback-telemetry \
build/release-macos/tests/actual_app_workflow_benchmark_driver \
  --color-interaction-proof --color-control curve --iterations 3 \
  --require-live-feedback --capture-scopes --timeout-ms 120000 \
  --recipe-file /path/to/large-mask-recipe.json \
  --capture-dir /private/tmp/live-feedback-captures
```
