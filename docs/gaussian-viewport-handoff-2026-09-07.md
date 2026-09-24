# Gaussian viewport completion ownership

Follow-up: `gaussian-viewport-teardown-2026-09-07.md` removes the viewport-local
destructor waits noted below and preserves accepted queued photos during
teardown. It does not remove all process-wide shutdown waits.

This fixes a verified export/interaction defect in the static-3D workspace. It
does not add temporal 4D reconstruction, film emulation, or establish foreground
realtime performance.

## Defect

The viewport used `QFutureWatcher::isRunning()` to decide whether its render
watcher could accept another request. A worker can finish before the UI consumes
its completion event. Camera movement in that interval replaced the watcher's
future and export bookkeeping, losing the completed photo's UI notification.
The PNG and camera receipt existed, but `lastExport` was never published.

The new regression reproduced this failure before the fix:
`/private/tmp/gaussian-handoff-before-test.log`.

## Change

Explicit load/render ownership now includes completed-but-undelivered futures.
Render ownership remains active while status signals are emitted, so reentrant
camera edits also queue behind the current completion. After consuming it, the
viewport renders the newest camera request. Source switches similarly consume
the previous load before admitting its replacement; stale camera metadata is
not published.

No renderer math, training, camera interpolation, file format, or export color
contract was changed. Existing blocking viewport-destructor waits remain; this
change is not an app-shutdown responsiveness fix.

## Evidence

- The handoff test exercises actual viewport export and completion delivery,
  100 coalesced camera changes, a reentrant camera change during publication,
  separate frozen export versus current preview cameras, PNG/receipt existence,
  and source-load replacement. The deterministic default uses a renderer fixture.
- The same test additionally ran with the real Metal renderer and the existing
  **380,065-splat** scene. It passed without modifying the source project:
  `/private/tmp/gaussian-handoff-real-scene.log`. Outputs were isolated temporary
  test files, not catalog imports or user destinations. This is native GPU and
  Qt completion evidence, not a foreground window/framerate trace.
- Seven Gaussian tests pass in 0.82 seconds: controller, interaction-proof
  validation, viewport handoff, Metal rendering, saved views, camera paths and
  movie export. None were skipped:
  `/private/tmp/gaussian-handoff-regressions.log`.
- The desktop build and final handoff target build pass:
  `/private/tmp/gaussian-handoff-fixed-build.log` and
  `/private/tmp/gaussian-handoff-accepted-build.log`.

Reproduce the real-scene handoff (read-only project, temporary output):

```sh
QT_QPA_PLATFORM=offscreen build/release-macos/tests/gaussian_viewport_handoff_test \
  --scene /absolute/path/to/existing/scene-project
```

The capture controller currently implements static-3D preparation/COLMAP/Brush
training only. Camera-path playback/export is camera motion through that static
scene. True temporal capture/training, photographic reconstruction quality,
backend packaging, foreground interaction, film-stock physics, and the other
active app completion requirements remain open.
