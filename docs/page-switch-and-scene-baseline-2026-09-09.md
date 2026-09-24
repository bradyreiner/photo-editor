# Page switching and Gaussian Scene priority

## Outcome

User priority is Gaussian-splat workflow and page-switching performance. Search
model-manager UI work is parked; the full completion goal remains open.

Kept: a rapid-navigation correctness fix and an actual Main.qml scene/navigation
proof. Not kept: the experimental removal of the 45/110 ms transition delays.
The bounded optimization loop stopped after an inconsistent protected cold-load
comparison. No page-switch speedup is claimed for the final application code.

## Correctness change

`Main.qml` now stops the previous route's finish timer before scheduling another
route, stops both timers on completion, and cancels pending navigation when the
user selects the already-current page. This prevents an old completion from
clearing a newer request or a queued route from winning after a request to stay.
The final native proof exercises rapid Scene -> Library -> Scene and
Scene -> Library -> Edit requests through the actual shell routing function.

## Measurement repair

The older generic actual-app driver directly assigned `currentWorkspace` and
omitted Scene. Its `perf-workspace-switch` suite alias is **not** the new proof;
use the explicit command below. The added driver mode:

- uses the actual navigation function and awaits visible content, input-overlay
  release and a later Qt frame swap;
- opens an existing integrity-checked trained project read-only, waits for its
  native Metal frame, and checks viewport identity and the orbited camera on
  returning to Scene;
- isolates application data/settings, completes onboarding only in those test
  settings and uses Auto preview selection, matching the app's preference;
- separates first visits from five subsequent warm rounds across all six pages;
- records foreground/exposure eligibility and event-loop gaps, including gaps
  over one second that the generic QML frame summary categorizes as idle;
- uses the existing benchmark wrapper for process logs and peak RSS.

`compare-workspace-switch.py` requires v2 evidence, matching source/scene/rounds,
complete foreground samples, camera preservation and memory data. It requires
20% lower warm overall and Scene medians, and permits at most 10% regression in
page p95s, first visits, total time, maximum heartbeat gap and peak RSS. Twelve
synthetic tests validate the comparator, not application performance.

## Final v2 baseline

Host artifact root: `/private/tmp/framehouse-page-scene-loop-bOarrB`.
Debug native desktop, QML disk cache disabled, one real 800x598 JPEG and an
existing 32-capture/753,038-splat static scene. This is not a large-catalog,
cold-machine, release-build, RAW, 4D-training or input-to-photon benchmark.

`v2-baseline-proof/receipt.json` is foreground eligible with no proof error.
The wrapper exits normally (0); peak RSS is 1,785,808 KiB. Observed first visits:

| Destination | Presented-ready time |
| --- | ---: |
| Edit | 1,747 ms |
| Carousel | 4,250 ms |
| Timeline | 4,713 ms |
| Export | 276 ms |
| Scene, including validation/load | 6,118 ms |

Warm switching median is 184.5 ms over 30 samples. Maximum UI heartbeat gap is
4,587 ms. Cold page construction and scene loading are the next measurable
targets; these are not realtime results.

Wrapper artifact:
`v2-baseline/20260909T163730.596805Z-perf-workspace-switch-ae5cf0f6`.

## Excluded experiment

One speed hypothesis skipped fixed transition delays for already-loaded pages.
Earlier v1 observations reduced warm medians from 186 ms to 33.5/24 ms and Scene
from 209 to 46 ms, but the confirmation exceeded the first-Edit guard (1330 to
1744 ms). The candidate was reverted, not promoted.

Visual review then exposed incomplete harness setup: onboarding could obscure
the page and the inherited driver forced CPU photo previews. Initial runs also
mixed QML cache state. V1 artifacts, including the individually passing
`controlled-comparison1.json`, are therefore diagnostic only. The final comparator
rejects v1 receipts. The corrected baseline also measures a 1747 ms first Edit
visit with original delays: the older difference does not establish that the
candidate caused that stall. A fresh valid matched comparison is still needed.

## Gaussian and visual verification

`v2-visual-proof/receipt.json` is foreground eligible, with an unobstructed Scene
UI, preserved viewport/camera and passing rapid-navigation checks. Both 800-edge
novel-view PNG exports have SHA-256:
`21e3bcb5f06e6093e6a18fb3e3198ddf0d96921fe595643a2fe73ae625df8abb`.
The scene was not retrained or modified. Screenshots were visually inspected;
reconstruction ghosting remains visible. This proves navigation/export stability,
not improved reconstruction quality or realtime Gaussian orbiting.

Seven focused CTest checks pass, including controller lifecycle, viewport handoff,
native renderer, saved views, foreground-proof rules and the comparator's twelve
tests. Actual-app driver builds. Existing Export preflight QML binding warnings
and isolated-catalog helper warnings remain visible; no app-wide clean-warning or
full-suite claim is made.

## Reproduce

Use a fresh output directory for each run. The supplied project is read-only;
the proof creates its own settings, catalog fixtures and optional photo exports.

```sh
QT_QPA_PLATFORM=cocoa QML_DISABLE_DISK_CACHE=1 PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1 \
python3 -B scripts/perf/run-benchmark.py \
  --scenario perf-workspace-switch \
  --app build/dev-macos-debug/tests/actual_app_workflow_benchmark_driver \
  --output-root NEW_WRAPPER_ROOT --event benchmark.workspace_switch.completed \
  --timeout 150 --settle-seconds 1 -- \
  --workspace-switch-proof-dir NEW_PROOF_ROOT \
  --scene-project /private/tmp/gaussian-training-schedule-f27hw9/integrated-project \
  --source /private/tmp/framehouse-gaussian-runtime-1JGerq/brush-dataset/images/P1180142.JPG \
  --iterations 6 --timeout-ms 20000
```

For separate functional screenshots/identical-export/rapid-click checks, add
`--capture-dir NEW_PROOF_ROOT` after `--`; these runs are excluded from timing
comparisons. The wrapper can terminate an app after its completion event, so
always inspect the proof receipt, not just wrapper success.
