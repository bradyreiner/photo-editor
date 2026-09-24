# Nonblocking Gaussian viewport teardown

## Verified defects

Destroying `GaussianSceneViewport` called `waitForFinished()` on its load and
render watchers on the GUI thread. Additionally, a photo accepted while a
preview was active existed only in the viewport's pending fields. Destruction
waited for the preview but never delivered the callback that would submit the
photo, silently dropping the accepted export.

An executable regression fails before the fix. Deliberately held file/render
workers are released by a 500ms safety deadline so the old destructor cannot
deadlock the test runner. Before-fix teardown measures 506–514ms for load,
preview, active photo and queued-photo cases; the queued photo is missing.
These delays are injected, not claimed measurements of normal photographic IO.
Evidence: `/private/tmp/gaussian-teardown-before-test.log` (exit 1).

## Ownership change

Viewport destruction signals cooperative load/film cancellation without waiting
for file or GPU work. Submitted workers already capture their immutable inputs,
scene, renderer and cancellation state by value. Their completion connections
are scoped to the viewport QObject and disconnect when it is destroyed.

A queued, already accepted photo is transferred into an owned worker closure.
It waits for the preceding render **off the GUI thread**, then uses the same
serialized renderer and its original frozen scene/camera/destination. Active
photo exports also finish independently of the view. The common rendering and
file-writing routine is shared with normal foreground-owned exports; color,
image encoding, new-file-only protection and receipt contents are unchanged.
No worker closure accesses the destroyed viewport.

This relies on documented [Qt future lifetime behavior](https://doc.qt.io/qt-6/qfuture.html#dtor.QFuture):
destroying the future handle does not wait for or cancel the computation.
Cancellation here is the app's existing shared token, not an assumption that
`QtConcurrent::run` automatically stops when a watcher is destroyed.

## Proof

- Deterministic lifecycle checks cover held load cancellation, held preview,
  active photo and a queued photo after camera movement and source clearing.
  Photos retain their frozen camera and artifact identity, produce a decodable
  PNG and camera receipt, and release worker-owned renderer objects afterward.
  The queued-photo check asserts exactly two renderer calls with no overlap.
- Fixed fixture teardown measures 6–12 microseconds in one accepted test run,
  against the predeclared 100ms upper gate. The arbitrary fixture pause no longer
  determines UI teardown latency. This is not an app-wide responsiveness bound.
- The same regression passes with the actual 753,038-splat scene and native
  Metal, plus the existing 100-change coalescing, reentrant completion, frozen
  export camera and stale-source tests. An additional production file-load
  cancellation is unfinished at teardown and exits afterward without publishing
  into the destroyed view. The first real run measures 8 microseconds for that
  teardown and 5–15 microseconds for the held-worker cases.
- Ten Gaussian regressions pass with native Metal. Desktop and test builds pass.
- An isolated packaged-app scene load/shutdown smoke test checks normal runtime
  integration; it is not the held-worker reproduction. The profile uses an
  unavailable isolated catalog and an empty managed-model store. No user catalog,
  originals, prior scene projects or model weights are changed.

Evidence:

```text
/private/tmp/gaussian-teardown-before-build.log
/private/tmp/gaussian-teardown-before-test.log
/private/tmp/gaussian-teardown-fixed-build.log
/private/tmp/gaussian-teardown-final-test.log
/private/tmp/gaussian-teardown-accepted-test-build.log
/private/tmp/gaussian-teardown-native-scene.log
/private/tmp/gaussian-teardown-accepted-native-scene.log
/private/tmp/gaussian-teardown-native-regressions.log
/private/tmp/gaussian-teardown-ui-sVT70s/receipt.json
/private/tmp/gaussian-teardown-ui-sVT70s/startup.png
```

Reproduce fixture or actual-file/native-GPU cases:

```sh
QT_QPA_PLATFORM=offscreen build/release-macos/tests/gaussian_viewport_handoff_test --lifetime
QT_QPA_PLATFORM=offscreen build/release-macos/tests/gaussian_viewport_handoff_test --scene /absolute/path/to/scene-project
```

## Remaining boundaries

This removes viewport-local waits, not all app-shutdown waits. Qt's global thread
pool can still drain running work at process exit; the reconstruction controller
still owns blocking process/preparation shutdown paths. There is no claim of
forced GPU cancellation, durable export recovery after a crash, or a global
notification for an export that fails after its UI has been destroyed. Such
failures retain the existing partial-file behavior. Broader loading/memory,
foreground realtime, photographic reconstruction quality, 4D, film fidelity,
embedded inference and editing-tool completion remain open.
