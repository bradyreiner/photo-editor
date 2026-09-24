# Surface-based Gaussian focus selection

The Focus page now offers **Pick focus in image**. It closes the photo popup,
arms a crosshair and asks for a solid surface. Left-click starts an asynchronous
estimate; right-click or Escape cancels the armed mode. The existing double-click
camera reset is suppressed for a focus-pick gesture. Manual focus remains
available. Picking does not open a pinhole aperture or alter other lens/film
settings automatically.

## Estimate and ownership contract

- Map the click through the actual aspect-fitted image rectangle, rejecting
  empty borders and invalid coordinates. Use the displayed camera resolution,
  focal scaling and pixel center, not the overall window as the image domain.
- Match the pinhole projected covariance, 0.3 pixel covariance floor, opacity
  cutoff and near-depth convention. At the clicked pixel, sort overlapping
  Gaussian contributions front-to-back and select the first center depth where
  accumulated opacity reaches 0.5. This avoids focusing on an isolated faint
  foreground splat when a stronger surface lies behind it. Insufficient total
  opacity fails without changing focus.
- Depth is along the camera's forward axis, appropriate for the thin-lens focus
  plane, not Euclidean distance. The result is explicitly
  `projected-opacity-median-v1` in scene units. It is a Gaussian-center heuristic,
  not an exact ray/surface intersection, calibrated autofocus or measured metres.
  Floaters, glass, uncertain geometry and layered surfaces can still mislead it.
- Temporal scenes use the current conditional center, covariance factor and
  temporal opacity. The selected capture time must be valid. Color, film grain
  and halation do not alter the geometric focus estimate.
- A worker owns immutable scene/camera/time snapshots and a cancellation token.
  The GUI never waits for projection/sorting or reads GPU preview pixels. There
  is at most one outstanding pick, including completed-but-undelivered results.
  Cancellation is checked during projection and around sorting; a large sort is
  not forcibly interrupted. Up to two million splats are admitted.
- Camera requests, navigation, scene replacement, hiding or destruction cancel
  pending picks. Application also checks scene epoch and request generation.
  A stale result cannot move the current focal plane. Too-close focus that would
  violate the current aperture constraint is rejected; the aperture is preserved.
- Applying focus uses the normal versioned photo-settings path, so saved views
  and accepted photo exports retain their existing settings/frozen-shot rules.

## Verification

The focused math test covers opacity ordering, faint foreground rejection,
insufficient surface opacity, focal scale, forward depth, letterbox mapping,
temporal motion/opacity and cancellation. The viewport test covers asynchronous
application, completed-future ownership, camera-change rejection and the
too-close-aperture case. Actual Main.qml proof uses the Focus button and native
mouse press/release events through the QML navigation surface, not just a direct
call to the picker. Its receipt records the selected depth and checks that all
other photo settings are unchanged.

Seven focused native tests passed (88.34 seconds under concurrent build/training).
The final focus and viewport-handoff rerun passed both tests in 1.83 seconds.

Actual Main.qml proof passed with `eligible: true` and `passed: true`:
`/private/tmp/framehouse-focus-pick-xlk7no/ui-final/receipt.json`.
The existing one-million-splat SUV scene, capture camera 51, selected forward
depth 1.496730566 scene units from 125 overlapping splats at pixel 687.5, 1100.5.
The query took 683 ms and published at 720 ms under concurrent training. Aperture,
sample count, grain and halation were preserved. Escape and right-click canceled
armed selection without starting a query or changing photo settings. The visible
focus field and screenshot were inspected; reconstruction softness remains.
Twelve film-control updates, cached optical-source reuse and progressive optical
frames also passed. Final settle took 2803 ms under load; this is not a clean
performance benchmark or proof of sustained realtime navigation.

The isolated input helper uses the already-installed Qt Test framework through
QPA window-system events. Earlier direct `sendEvent` mouse attempts did not reach
the QML MouseArea and failed; `ui-qpa` and the final receipt supersede those
attempts. Qt Test is linked only to a test helper, not the production app.

Standalone qmllint produced no syntax error but its JSON reports two warnings:
the stale production app.qmltypes lacks the new invokable, and an existing nested
QQuickItem `contentItem` lookup is unresolved. The actual native object invocation
passed; lint is not claimed clean. Only test targets were built. The installed
app bundle, original photos and baseline reconstruction were not replaced.
