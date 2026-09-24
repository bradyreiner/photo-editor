# Global-slider draft fidelity

The preceding goal turn made verified loading/cache progress. This turn follows
the remaining slider failure through actual preview pixels; it does not close
the broader RAW, loading, reconstruction, embedded-model and editing-tool goal.

## Reproduced defects and changes

1. Ordinary global-slider drafts cleared local masks and retouch state in the
   compiled pipeline. A 128x96 P3 input with an edited left-half mask differed
   from its same-resolution settled render by 37/255 in legacy and 38/255 in
   ACES2. The unmasked right quarter stayed unchanged, proving a real local edit
   in the reference. Evidence: /private/tmp/global-mask-fidelity-before.log.
   Draft reduction now preserves those edits unless the request explicitly uses
   the mask-preview base-only path.
2. ACES draft publication selected the interactive output LUT while settled
   publication used the final LUT. After restoring masks, this still caused
   2-3/255 differences. Both canonical and working-image publication now use the
   final photographic output LUT. The same-resolution CPU test then becomes
   pixel-exact for both models in all four supported working profiles: Linear
   sRGB, Linear Display P3, Linear ProPhoto RGB and Linear Rec.2020. Evidence:
   /private/tmp/global-mask-final-cpu.log. These are eight specific raster/mask
   cases, not independent all-camera, HDR, ICC device or colorimetric validation.
3. Actual native global requests still lost the mask: their propagated dirty
   stages included mask rasterization, and that membership was incorrectly used
   to select the mask-only base path (which also clears photo layers). The native
   discrepancy was 8/255 in the masked quarter and 1/255 in the protected quarter.
   Evidence: /private/tmp/global-mask-regions.log and
   /private/tmp/global-mask-native-diagnostic/{draft,matched-reference,full}.png.
   The routing now uses the originating dirty domain/render reason, not its list
   of downstream invalidations. The explicit mask-preview path and mask-commit
   exception remain unchanged.

## Regression contract

`global_slider_mask_fidelity_test` compares real renderer output with matched
decode/output dimensions and neutral detail/lens controls. It requires <=1/255
draft/settled difference, a visible mask effect and unchanged protected pixels.
The request includes downstream mask-rasterization invalidation.

`interaction_workflow_benchmark_driver --global-mask-preview` captures the first
actual native texture after a global exposure setter, before current-generation
full quality. It requires a draft, scheduled/pending full work, final current
generation, <=2/255 matched-resolution reference agreement and <=2/255 full native
versus flattened-export agreement. The reference includes a mask-free negative
control. It does not compare a separately downscaled full render against a draft,
which would conflate resampling/demosaic differences with missing edits.

Both new tests pass with real Metal access in
/private/tmp/global-mask-intent-tests.log (4.55 seconds). The native workflow
uses a 1600x1200 generated source and a 512x384 draft.

Real photographic native tests also pass:
/private/tmp/global-slider-hdrplus-nexus6-skin-0218.dng.log and
/private/tmp/global-slider-hdrplus-nexus6-skin-0218-reference.jpg.log. RAW draft
256x189 and JPEG draft 512x379 both match the same-resolution reference within
1/255; all four image quarters meet that bound. Masked pixels visibly change,
protected pixels do not, and full 4208x3120 native output agrees with flattened
export within 1/255. Both reach full generation 2/current generation 2. Saved
draft/reference/full images are under /private/tmp/global-slider-photographic-proof
in per-source directories; the RAW draft was visually inspected. This is one
camera and matching JPEG, not a multi-camera or full-resolution realtime corpus.

This proves queued full work actually completes, so the older broad-controller
assertion now accepts the pending-full path as well as a timer, and expects the
intentional first live draft (priority 0, 100ms budget). The feedback test's stale
requirement for *no* full work after a local-mask color edit is replaced with a
full-work requirement and explicit final generation check. Immediate preview
scheduling is still required; no latency ceiling or pixel tolerance was raised.

The updated feedback test passes in 7.38 seconds
(/private/tmp/global-slider-feedback-contract.log). All 17 focused regressions
pass in 52.53 seconds (/private/tmp/global-slider-final-regressions.log), including
independent Metal ACES output-LUT parity, four existing local-mask native tests,
five selection/retry cases and corrupt-cache repair. The broad controller test
passed the original first-slider block and next exposed the same stale timer
assumption for global exposure over masks; that block and its neighboring local
color block now require actual current-generation completion after settling.

The two real photographic runs observed worker draft renders of 11ms for the
256x189 RAW draft and 91ms for the 512x379 JPEG draft, each with 2ms queue wait.
These individual runs are not an FPS/latency distribution or controlled speed
comparison, and do not prove full-resolution realtime.

## UI stall investigation

The preceding native app smoke recorded a 4656ms observation tick gap spanning
the proof's synchronous source-layer export/undo/redo actions. This is a location
to investigate, not proof that ordinary rendering alone blocked for that time.
The unchanged app proof now records layerExportElapsedMs, undoElapsedMs and
redoElapsedMs separately. The proof deadline, source data and export remain
unchanged. No asynchronous-export fix or realtime claim is made yet.

The instrumented rebuilt app passes with inspected screenshot and receipt at
/private/tmp/global-slider-app-proof-6X0yHQ/{startup.png,receipt.json}.
ElapsedMs is 35993, source-layer export/preview/undo-redo pass, and the observation
gap is 4664ms. The synchronous actions account for it: layer export 3521ms, undo
49ms, redo 1084ms. This isolates the next responsiveness work to export and
history admission rather than attributing that interval to the preview worker.
The catalog-recovery banner is expected under the isolated closed-port catalog.
No user catalog or original was modified.

Source inspection finds `exportPhotoLayer` directly called by this proof on the
GUI thread; no current QML or agent call site was found. Its public invokable
implementation is synchronous, as is `exportFlattenedPhoto`. Normal export-panel
behavior therefore needs separate tracing rather than extrapolating this proof
to every export. `redo()` calls `applyHistoryState()` synchronously. Both routes
need bounded worker/snapshot execution or admission profiling before a responsive
full editing workflow can be claimed.

Remaining color-path audit: `retained_preview_surface.h` still chooses an
interactive photographic-output LUT for the noncanonical retained-surface path;
`canonical_native_device_lut.h` still selects interactive versus final monitor
ICC LUTs. Neither path is exercised by this turn's canonical sRGB native proof.
They require their own matched-state/device-profile checks; these fixes do not
establish parity for every publication path or monitor profile.

Final broad-controller check: /private/tmp/global-slider-merged-contract.log
still fails at 10.32 seconds, now at "the first advanced tone edit should schedule
a settled full-resolution render". It passes the repaired global-over-mask and
coalesced local-color blocks, including their current full-generation checks.
The coalesced case checks dependent local-adjustment invalidation rather than
requiring it to replace earlier mask-rasterization invalidation. The broad suite
is not green; its remaining advanced-tone scheduling expectation and later
blocks have not been validated by this turn's focused proof.
