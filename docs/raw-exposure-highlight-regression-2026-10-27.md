# RAW exposure highlight regression

Follow-up on 2026-10-28: both decoder contracts now defer EV, and canonical
recipes accept the sum of RAW and Light exposure. Preview/export and canonical
save/reopen tests cover additive and cancelling combinations. Canonical storage
normalizes the controls into one total EV, preserving pixels rather than the
original group allocation. Local highlight recovery and cache invalidation are
also corrected; see `docs/editor-regression-protection.md` for the guarded
test inventory and normalization contract. The original evidence below records
the narrower 2026-10-27 fix.

The integrated follow-up was checked in an isolated Debug build with debug
symbols disabled, under `build/color-engine-fixes-2026-10-28/native-check`.
The selected 45 checks pass, including three host Metal parity checks; the
regression-gate validator passes 17 tests. Three checksum-pinned Canon RAWs
pass draft/settled, both decoder contracts, mixed EV, save/reopen, total reset,
cancellation, cache reuse, and editable restored-control checks. A cropped
preview/export comparison using a full-resolution 6024 x 4020 Canon source is
byte-identical in all four
default/custom RAW-level and recovery on/off combinations.

The broader `editor_preview_controller_test` still fails its final large-mask
undo/redo settle check. Its 64 x 64 PNG and 10,000 brush dabs do not activate
the RAW/highlight changes; no matching baseline establishes whether that
failure predates this work. This remains an unresolved gate. The exact screenshot
source and independent photographic color qualification remain unavailable.
Logs, test XML, source/binary hashes, and the final result summary are retained
under `build/color-engine-fixes-2026-10-28/`.

Raising editable RAW exposure to +1.90 EV could turn neutral clouds pink.
LibRaw applied EV to its unsigned 16-bit camera samples before engine-owned
white balance and camera characterization. Channels hit the integer ceiling at
different points, destroying sensor ratios and creating false sensor clipping
for highlight analysis. This was pixel corruption, not a warning overlay.

Camera-linear decoding now defers EV to the shared engine float scene-exposure
stage. Highlight analysis sees the original sensor clipping, and RAW EV adds
to Light exposure without integer clipping. Decoder-characterized fallback
output retains its existing exposure ownership. The capability report records
exposure as deferred for camera-linear output.

## Verification

- `raw_camera_linear_decoder_test`: camera samples remain byte-identical at
  -4, +1.9 and +4 EV; exposure is not reported as decoder rendering.
- `raw_exposure_render_test`: synthetic DNG output matches the independent
  float Light-exposure route exactly at +1.9, -2 and +4 EV, with recovery on/off,
  bounded/full decode, and combined RAW plus Light exposure. Neutral output is
  checked separately so ignoring EV cannot pass.
- `raw_exposure_preview_test`: actual canonical draft and settled request
  renderer matches the Light-exposure route at those three EV values.
- The preview check also passes on the real Canon CR2 below. All 12 render
  cases pass on that CR2 too, including full 6024 x 4020 output, both recovery
  states and combined RAW plus Light exposure, with byte-identical pixels.
- `photo_editor_app` rebuilt successfully in `build/dev-macos-debug`.

The real test source is the existing local fixture
`build/app-audit-fixes-2026-10-25/real-shoot/source/IMG_3665.CR2`, SHA-256
`0521326dc6cf367fdeb2dc849982e2bd1f7ecc39689b5cbe8a4cf82fad971345`.
The pre-fix render reproduced broad pink clouds at +1.9 EV. The corrected
bounded and full-resolution renders have white clouds and match the float
reference exactly.
Evidence is in `build/raw-exposure-regression/`, including before/after PNGs,
`ctest.log`, `real-preview.log`, `verification.json`, source/app hashes and
`build.log`. This is a different photo from
the user's screenshot; the exact source was not supplied during this run.

The bridge's new exposure assertions pass, but its existing source-contract
section still fails: it requires exactly one shared compiler call in preview
and export, while those working-tree sources each have two. This fix does not
change those sources or weaken the contract.

## Reproduce

```sh
cmake --build build/dev-macos-debug --target photo_editor_app \
  raw_camera_linear_decoder_test raw_exposure_render_test preview_linear_cache_test -j 6
ctest --test-dir build/dev-macos-debug \
  -R '^(raw_camera_linear_decoder_test|raw_exposure_render_test|raw_exposure_preview_test)$' \
  --output-on-failure
QT_QPA_PLATFORM=offscreen build/dev-macos-debug/tests/preview_linear_cache_test \
  --raw-exposure /path/to/photo.CR2
QT_QPA_PLATFORM=offscreen build/dev-macos-debug/tests/raw_exposure_render_test \
  /path/to/photo.CR2 build/raw-exposure-review
```

For manual QA, open the same RAW in the rebuilt app, move RAW exposure from 0
to +1.90 EV, and inspect neutral clouds while dragging and after settling.
Toggle recovery, reset, undo/redo, export, and reopen. This run exercises the
production renderers offscreen; it does not claim a foreground slider or
all-camera visual qualification.
