# Space-time artifacts in Scene Lab

Follow-up: `gaussian-timed-film-2026-09-08.md` documents the subsequently added
timed film workflow. The limits below describe the initial workspace stage.

## Delivered

Scene Lab can now open a project containing the native 4D artifact profile,
verify its saved hash in a worker, render it with explicit scene time, scrub or
play that time independently of the camera, and export a frozen-time PNG/JPEG.
This is artifact viewing and photography, **not capture-to-4D training**.

The existing **Open scene** folder picker accepts this project envelope:

```json
{
  "schema": "framehouse-gaussian-scene-v1",
  "mode": "spacetime-4d",
  "state": "artifact-ready",
  "artifactValidation": {
    "artifactSha256": "<64 lowercase hex characters from the artifact writer>"
  }
}
```

Save the envelope as `scene.json`; the artifact must be at
`scene/scene.safetensors` inside that project. Its content follows
`gaussian-spacetime-artifact-2026-09-07.md`. Persisted absolute artifact paths
are ignored. Missing/outside artifacts, unsupported modes and malformed pins
are rejected. This is the producer handoff contract; there is no new generic
checkpoint picker or trainer adapter. No existing prepared capture is relabeled
as a trained scene.

Opening never executes persisted commands or modifies the project. Controller
verification reports `artifact-ready`, not `trained`, with training unavailable.
The viewport independently verifies the same pin while loading its scene data.
This currently reads the artifact twice; large-file admission/memory optimization
is still required. Conflict admission, cancellation and fresh reopening retain
the existing worker-owned lifecycle.

## Time and export behavior

- The time slider displays elapsed seconds from the artifact interval's start.
  Rendering receives `timeOriginSeconds + localTimeRange[0] + elapsedSeconds`.
  Seeks clamp to the interval and ignore non-finite input.
- Playback follows elapsed wall time, stops at the end, and restarts from the
  beginning when played at the end. Hiding or changing the source stops playback.
  It uses the existing single-render worker with latest-request coalescing, not
  an unbounded queue. This is not proof of foreground realtime frame rate.
- Camera selection, orbit, pan, dolly and saved camera views remain independent
  of scene time. Saved views currently save cameras, not temporal bookmarks.
- Photo acceptance freezes the scene, camera and absolute scene time. Later
  scrubbing, source replacement and viewport teardown do not change an accepted
  queued photo. The sidecar adds `sceneSeconds` and `timeUnit: seconds`.
- Fixed a preexisting queue bookkeeping defect exposed by this integration:
  starting a queued frozen export previously consumed newer preview requests.
  Only a preview submission now marks those requests submitted, so the newest
  camera/time is rendered after the frozen export.
- Temporal film export is explicitly rejected and its camera-path export row is
  hidden for 4D artifacts. Static film behavior is unchanged. Actual timed film
  sampling/export remains unfinished, not silently replaced by a static film.

## Evidence

Proof root: `/private/tmp/gaussian-time-workspace-proof-rLz21S`.

- `workspace-verified/receipt.json`: real controller-to-native-viewport loading
  of a synthetic temporal artifact; 100 scrubs around a queued photo; exact
  frozen-time photo pixels against a native direct-render oracle; interval/NaN,
  play/pause/end/restart/hide, source-clear and bad-pin checks. Canceling a queued
  reopen leaves the manifest untouched; fresh reopening succeeds. A queued photo
  survives source clearing and viewport destruction with exact time and pixels;
  measured teardown was 32 microseconds on this run.
- All 16 Gaussian CTests pass in 4.99 seconds. Log:
  `/private/tmp/gaussian-time-workspace-verified-regressions.log`.
- Packaged app `native-verified/receipt.json` passes actual QML slider `moved`
  and play/pause `clicked` handlers, camera interaction, PNG and JPEG export.
  Both export sidecars record scene time 86400.597 seconds. Its final screenshot
  was visually inspected. The rendered yellow primitive is deliberately synthetic,
  not a recovered real moving object.
- `native-static/receipt.json`: the existing real 753,038-splat static project
  still opens with a ready native viewport and `temporal: false`; screenshot
  inspected. No static project files were rewritten by reopening.
- Release app and focused test targets build. Focused `qmllint` exits zero with
  no diagnostics. The existing optional Vulkan-header and missing PostgreSQL
  packaging-runtime notices are unchanged.
- Native proofs isolate preferences/cache/catalog and deliberately use a closed
  database port. The visible catalog recovery banner is expected; no user catalog
  or model weights were changed. Embedded runtime still reports no installed model.

The first end-to-end run exposed the queued-preview defect described above.
Subsequent test-harness fixes canonicalized macOS temporary-path aliases and
tested hide last for a windowless QQuickItem; actual visible controls were then
verified in the packaged app. No production correctness gate was relaxed.

## Remaining scope

Real checkpoint conversion with verified parameter conventions and licensing,
genuine synchronized-capture reconstruction/training, held-out moving-view/time
quality, timed film export, time bookmarks, convenient artifact production/import,
large-artifact memory/latency and foreground playback performance remain open.
The broader RAW/color, loading, model/search/agent and professional editing-tool
requirements in `active-app-completion-goal.md` remain independently unfinished.
