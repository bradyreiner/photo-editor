# Progressive lens and film preview

The settled viewer now has a path for displaying intermediate finite-aperture
images instead of retaining the pinhole draft until the entire optical render
finishes. This improves inspection of focus and bokeh during refinement; it does
not improve reconstructed geometry, change the lens model or calibrate film.

## Contract

- For settled optical texture previews, publish after 4, 8, 16, 32 and 64 samples
  when the selected final sample count is greater than that prefix. Aperture
  samples keep their existing deterministic order and normalized running mean.
- Each prefix runs the same exposure/look/grain/halation conversion as the final
  image. Grain keeps the saved seed and full film-plane coordinates. Low-sample
  prefixes may show aperture sampling noise; they are labeled refining, not final.
- A prefix owns a separate completed immutable BGRA GPU texture. The mutable
  integration texture is never handed to Qt, and no preview CPU readback is added.
  Publishing must allocate a new output texture before the next conversion.
- A mutex-protected latest-only mailbox holds at most one unpublished image. The
  GUI's existing 100 ms progress timer consumes it. Slow consumers skip outdated
  prefixes rather than growing a queue. The currently displayed/render-thread
  leases and worker textures remain separate; whole-app peak memory needs stress
  testing, especially at 4096-pixel previews.
- Publication checks the request generation, scene epoch, cancellation, visibility,
  navigation and export state. A completed final frame clears the mailbox and
  stops the progress timer. Detached workers own their mailbox and no QObject.
- Motion drafts, explicit photo readback, tiled still export and video export do
  not publish prefixes. Cached film-only edits still use one conversion of the
  completed source. Final sample count and saved-photo settings are unchanged.

## Validation scope

Native lens tests exercise the immutable 16-sample prefix of a 32-sample film
render, comparison against an independently rendered final image, cached final
parity and readback/draft exclusion. Viewport tests exercise stale scene/request,
cancellation, navigation, export, interactive, hidden and invalid-prefix guards.
The actual Main.qml viewer proof records prefix metrics/timing and a screenshot
during post-motion refinement, followed by the resolved photo.

## Native and viewer evidence

- Six focused native tests pass in 12.82 seconds: scene renderer, space-time
  renderer, film export, saved views, camera path and viewport handoff. The
  32-sample progressive final matches a separate renderer's full image exactly;
  the immutable 16-sample prefix matches the separate 16-sample render exactly,
  including grain/halation, and survives subsequent renderer reuse unchanged.
- All thirty motion-frame pixel hashes exactly match the prior baseline.
  Receipt: `/private/tmp/framehouse-progressive-optics-8J0Bo6/motion-parity.json`.
- Actual `Main.qml` viewer proof `ui/receipt.json` in that evidence root is
  eligible and passes. It displays 4- and 8-sample film/lens previews before the
  final 16-sample photo. Visually inspected `progressive-after-motion.png` and
  `resolved-after-motion.png`: the first shows the resolving label and expected
  sparse-aperture artifacts; the final replaces it with the photo-preview label
  and more converged blur. The baseline reconstruction's defects remain visible.
- That first UI run observed its first prefix at 1143 ms and final at 3062 ms;
  the motion-parity check overlapped part of the run, and the high-detail trainer
  remained active. These are functional progress-publication observations, not
  clean-hardware performance comparisons or input-to-photon measurements. The
  proof also captures a screenshot during refinement, adding observation cost.
- Confirmation run `ui-confirm/receipt.json` is also eligible and passed, without
  the simultaneous motion-parity check. It observes 4 samples at 944 ms, 8 at
  2353 ms and the final at 2592 ms. The intermediate screenshot was visually
  inspected. Training remained active and screenshot capture still adds cost;
  this establishes visible progressive behavior, not a controlled speedup.
- The production app bundle was not rebuilt or replaced. A pre-existing QString
  range-copy warning in the viewport test remains separate from this change.

High-detail training remains owned by pipeline session 29220. Its exact parent
PID 16095 and Brush child PID 24355 were confirmed live; the child showed active
CPU use. A long elapsed time or stale persisted log is not grounds to restart it.

Physical focus picking, calibrated sensor/scene scale, named lens response,
measured film response and representative maximum-size memory/latency remain
open. Early visibility is not higher final reconstruction quality or realtime
full-resolution lens simulation.
