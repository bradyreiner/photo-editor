# Actual QML Workflow Performance Investigation — 2026-09-23

## Measured baseline

The current `actual_app_workflow_benchmark_driver` was rebuilt from this checkout before
measurement. Its five-iteration run is in
`/private/tmp/photo-editor-perf-user-report-baseline/20260924T035654.245600Z-perf-actual-app-workflow-4f478171`.

The gate failed on first-use readiness:

| Stage | p95 | max | Budget p95 / max |
| --- | ---: | ---: | ---: |
| QML shell load to ready | 2,425 ms | 2,425 ms | 1,000 / 1,500 ms |
| Import route to visible | 422 ms | 527 ms | 100 / 200 ms |
| Carousel route to visible | 1,750 ms | 2,182 ms | 350 / 500 ms |
| Timeline route to visible | 2,250 ms | 2,810 ms | 200 / 350 ms |

The first Carousel and Timeline route in the run is slow; subsequent visits measured 17–20 ms
and 8–9 ms respectively. Photo opening measured 100 ms p95, brush feedback 11 ms p95, and
brush commit visibility 1 ms p95. Export route readiness passed at 74 ms p95.

## Bounded optimization attempt

Making the Carousel and Timeline `Loader`s asynchronous was tested in the otherwise identical
five-iteration workflow. The candidate artifact is
`/private/tmp/photo-editor-perf-user-report-baseline/20260924T040224.823525Z-perf-actual-app-workflow-9b66c497`.
It was rejected: Carousel route p95 rose to 3,067 ms (+75%) and Timeline to 3,436 ms (+53%).
The frame summary also exposed a 1,209 ms interval and additional jank during the cold load.
The asynchronous Loader changes were reverted.

The QML frame sampler now includes visible-window intervals over one second in its jank summary
instead of discarding them as idle gaps. That preserves a long pause when the sampler receives a
callback after one. It does not replace route readiness measurements or identify the cause by
itself.

## Proof limits and next step

- The workflow process completed; the wrapper returned failure because the performance budgets
  failed. No application crash was reproduced by this run.
- Per-process start/end RSS samples are absent because this environment blocks `ps`. The
  benchmark runner now records the child process's `getrusage(RUSAGE_CHILDREN).ru_maxrss` peak
  as a fallback, but that does not provide an in-run RSS delta.
- The benchmark driver loads `Main.qml` from the checkout, while the production app loads the
  compiled `PhotoEditor.Main` module. Route loader timings remain useful for this QML source, but
  startup timings need confirmation against the rebuilt production app before treating them as
  production baselines.
- The next optimization should target first-use Carousel/Timeline cost with host memory samples
  available, then repeat the same cold and warm route measurements. The asynchronous-only change
  did not meet the latency gate.

## 2026-09-24 bounded prewarm follow-up

The staged secondary-workspace prewarm helper in `Main.qml` has no caller. A candidate could start
that sequence after the Edit workspace loads, but the existing idle gate checks workspace
transitions and background work only; it does not detect pointer, keyboard, or slider interaction.
The workflow driver also emitted only one QML frame summary in its five-iteration baseline, from
the final Export route, so the current artifact cannot measure frame stalls during initialization.
Without those signals, a prewarm trial would not protect the interaction-smoothness requirement.
No eager loads were enabled and no prewarm candidate was run.

The new peak-memory fallback was exercised in a fresh five-iteration baseline at
`/private/tmp/photo-editor-ui-prewarm-loop-baseline/20260924T121423.766317Z-perf-actual-app-workflow-230937fe`.
It reported a child-process maximum of 6,011,024 KiB; `ps` start/end samples remain unavailable.
This peak is recorded for later same-harness comparisons, not as a memory-growth delta.

## 2026-09-24 current-checkout route baseline

After the Timeline inspector QML changed, the workflow driver was rebuilt and the same five-iteration
scenario was rerun. The artifact is
`/private/tmp/photo-editor-ui-cold-route-loop/20260924T134919.330343Z-perf-actual-app-workflow-cead2823`.
The route-readiness results remain over budget:

| Stage | p50 | p95 | max | Existing p95 / max budget |
| --- | ---: | ---: | ---: | ---: |
| QML shell load to ready | 2,611 ms | 2,611 ms | 2,611 ms | 1,000 / 1,500 ms |
| Carousel route to visible | 50 ms | 1,949 ms | 2,412 ms | 350 / 500 ms |
| Timeline route to visible | 30 ms | 2,276 ms | 2,837 ms | 200 / 350 ms |
| Import route to visible | 1 ms | 323 ms | 403 ms | 100 / 200 ms |

The five brush-feedback samples measured p95 48.8 ms (49 ms max); tool draft scheduling measured
p95 26.8 ms (27 ms max). The artifact reports one QML frame summary, for Export only (20 samples,
49 ms maximum interval). It therefore cannot show whether the first Carousel or Timeline visit
causes frame stalls while the route becomes visible. RSS start/end samples are unavailable; the
6,026,832 KiB `getrusage` process maximum is not a memory-growth measurement.

For a bounded follow-up, the gate is at most three single-hypothesis attempts, at least 20% p95
improvement per attempt, and eventual compliance with the existing route p95/max budgets. Brush
feedback and tool-draft latency must remain within their existing budgets; memory growth must stay
under the existing 64 MiB cap when measurable. No product optimization was attempted from this
baseline because route-local frame and concurrent-input measurements are missing. Starting the
idle prewarm helper without user-input detection could move the cold-load stall onto active editing.
The next step is to add route-scoped frame and input telemetry before revisiting prewarm or other
first-use changes.

## 2026-09-24 route-scoped frame follow-up

The benchmark driver now enables QML frame summaries only around the Carousel and Timeline route
transitions and resets the sampler timestamp at each boundary. Production leaves
`qmlFrameCaptureEnabled` false. The scoped captures are in:

- `/private/tmp/photo-editor-ui-cold-route-loop/20260924T141730.435253Z-perf-actual-app-workflow-62fc738d`
- `/private/tmp/photo-editor-ui-cold-route-loop/20260924T141907.804691Z-perf-actual-app-workflow-3e8e0d87`

They show the first Carousel frame gap at 2,390 ms and the first Timeline frame gap at 2,658 ms,
matching cold route maxima of 2,389 ms and 2,657 ms. Later Carousel frame gaps range from 53 to
90 ms; later Timeline gaps range from 31 to 60 ms. The route p95 values remain over budget at
1,929 ms for Carousel and 2,132 ms for Timeline. This ties the cold readiness delay to a long
visible-frame pause instead of a quiet asynchronous wait.

These runs are diagnostic, not accepted performance candidates. Tool-draft p95 measured 46.6 ms
against its 40 ms budget, and Export route p95 measured 545.8 ms against 100 ms; both were within
budget in the earlier pre-capture baseline, so the run does not pass the protected-regression
gate. No production prewarm or loader change was attempted. The remaining prerequisite is an
input-event probe during route construction and a safe application-wide idle signal; the current
benchmark measures brush feedback before route construction, not concurrent input responsiveness.

## 2026-09-24 route input responsiveness follow-up

The workflow driver now posts a harmless key event from a worker thread 15 ms after each route
starts, followed by an ordered marker. It records the key's queue-to-dispatch delay and confirms
that `RecentUserInputFilter` observed the event before its idle timer can clear the flag. The
existing route gate now also checks a 100 ms p95 / 200 ms maximum budget for this input delay.

The five-iteration run is
`/private/tmp/photo-editor-route-input-probe/20260924T215923.530362Z-perf-actual-app-workflow-de8df583`;
its report is `actual-app-workflow-report.json`. The input filter observed all 10 injected keys.
The gate failed substantially:

| Stage | p50 | p95 | max | Budget p95 / max |
| --- | ---: | ---: | ---: | ---: |
| Carousel route to visible | 174 ms | 3,601 ms | 4,452 ms | 350 / 500 ms |
| Carousel key dispatch delay | 331 ms | 3,811 ms | 4,664 ms | 100 / 200 ms |
| Timeline route to visible | 831 ms | 4,395 ms | 5,196 ms | 200 / 350 ms |
| Timeline key dispatch delay | 949 ms | 4,511 ms | 5,312 ms | 100 / 200 ms |

On the first cold visit, route readiness took 4.5 seconds for Carousel and 5.2 seconds for
Timeline, with matching visible-frame gaps of 4.5 and 5.2 seconds. This confirms that the long
route wait includes a blocked QML frame and event loop, rather than only a delayed readiness
signal. It does not isolate QML object construction from render-worker CPU contention: this
offscreen run had no Metal device and used the CPU fallback while editing and thumbnail jobs were
still active. The source-loading benchmark also runs checkout `Main.qml`, not the compiled
production QML module. Brush feedback p95 was 101.8 ms (108 ms max), and tool-draft scheduling
p95 was 77.2 ms; this run therefore also failed those protected budgets. The process peak RSS
was 3,901,216 KiB from `getrusage`, not an in-run growth measurement.

No product loading change was made from this diagnostic. The asynchronous Loader trial above
already regressed cold route readiness; prewarming these workspaces could move the same blocking
work into active editing. Next, compare against a route-only native macOS run with the production
renderer, or a no-preview route-only fixture, before selecting a production optimization.

## 2026-09-24 no-preview route isolation

The benchmark driver now has a route-only mode that switches from Library to Carousel and Timeline
without selecting an asset, starting Edit, or scheduling preview, mask, thumbnail, or export work.
It injects the same queued no-op key, samples each route's frame interval, and reports the wall time
spent inside the `currentWorkspace` property setter. `--route-only-no-render` hides the window after
shell readiness and checks the selected Loader/item visibility properties without waiting for a
rendered frame.

The visible offscreen baseline is
`/private/tmp/photo-editor-route-only/20260924T232226.914553Z-perf-actual-app-workflow-99b52cfd`.
Both routes report `preview_asset_loaded=false`. The cold Carousel setter/route took 3,980 ms and
the queued key waited 3,964 ms; the cold Timeline setter/route took 5,492 ms and the queued key
waited 5,477 ms. Warm route switches took 1–2 ms. The five-sample route p95 values are 3,184 ms
for Carousel and 4,394 ms for Timeline.

The hidden-window run is
`/private/tmp/photo-editor-route-only-no-render/20260924T232427.090580Z-perf-actual-app-workflow-2c99b9c4`.
It still measured 5,681 ms for cold Carousel and 5,394 ms for cold Timeline, with key dispatch
delayed by 5,664 ms and 5,378 ms. A two-sample follow-up added setter timing directly:
`/private/tmp/photo-editor-route-only-no-render/20260924T232550.451663Z-perf-actual-app-workflow-441e92fe`.
With the window hidden, the `currentWorkspace` setter itself took 4,058 ms for Carousel and
5,233 ms for Timeline, while the paired key waited 4,041 ms and 5,217 ms. The offscreen source
harness therefore shows a UI-thread block during cold Loader activation even without an image
preview or visible-window rendering. This is evidence about the checkout `Main.qml` source-loading
fixture; it does not establish the cost in the compiled production QML module.

One runtime-only asynchronous Loader candidate was compared on the visible route-only scenario:
`/private/tmp/photo-editor-route-only-async/20260924T232854.522858Z-perf-actual-app-workflow-bc4d3ad0`.
It reduced cold key dispatch to 15.0 ms on Carousel and 19.1 ms on Timeline, but cold route p95
grew to 5,017 ms and 6,530 ms (6,271 ms and 8,162 ms maxima). The captured frame summaries still
show a 1,075 ms Carousel gap and a 235 ms Timeline gap, with repeated janky frames during creation.
This candidate improves event dispatch while making the visible route much slower, so it is not
accepted. A production fix needs a genuinely lightweight first surface and measured progressive
creation, or a production-renderer route-only measurement that shows the source harness is the cause.

The remaining prerequisite is a route-only measurement against the compiled production module on
the native renderer, followed by a targeted split of the first-use workspace construction if that
result confirms the same UI-thread block. Keep the existing async Loader trial rejected and do not
enable idle prewarming during active input.
