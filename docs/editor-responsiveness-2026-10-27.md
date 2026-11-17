# Editor responsiveness follow-up

The user approved a fresh round covering styled text drag/resize, brush commit,
Selection feedback, slider pacing, and Auto Level on real photographs. Visible
Mac checks remain paused at the user's request. CPU/offscreen measurements do
not establish native presentation latency or a realtime frame-rate guarantee.

Use at most three measured candidates per scenario in this round. Accept a
target CPU improvement of at least 15%, with no protected regression above 5%.
Keep source, pixels/alpha, undo, cancellation, persistence, and small-input
behavior as correctness gates. Preserve unrelated dirty-tree work.

Baseline source snapshots and hashes are under
`/private/tmp/editor-responsiveness-2026-10-27/baseline/`. The October 26 report
records the earlier accepted fixes and rejected rendering/scheduling candidates.

Current hypotheses are being validated independently: exact CPU ICC output for
the actual styled text path; bounded independent brush tile compression;
pixel-coordinate Selection raster work; and photographic Auto Level fixtures.
Slider work must target measured processing cost rather than repeat the rejected
timer handoff. No candidate is accepted by this initial work log.

## Accepted component results

- Brush commit uses the existing bounded CPU pool to compress up to four
  independent immutable tiles concurrently, then inserts identical encoded
  strings in original key order. It applies only at 32 or more touched tiles
  totaling at least 16 MiB; smaller strokes use the original serial path.
  Five alternating 6000x4000 / 1024 px probes improved median commit from
  130.786 to 38.1923 ms (70.8%). Raster plus finish remained 87.9474 versus
  87.1619 ms. Every draft and compressed payload hash matched. The permanent
  test also covers held draft immutability, restore parity, small/thin inputs,
  and cancellation before commit. Cancellation during compression is checked
  per tile; the controller rejects canceled results before applying edits.
- Selection circular rasterization reuses pixel-space distances without
  changing alpha bytes or dirty rectangles. Strict legacy-formula tests cover
  portrait/landscape, erasing, feather, subpixels, exact edge/rounding cases,
  and one-pixel axes. The CMake-built probe improved 2560x1675 raster work
  from 0.425583 to 0.295666 ms (30.5%); 512x335 improved 25.7%.
- Exact CPU ICC conversion passed bitwise RGBA, alpha, small-input, cancellation,
  and invalid-transform guards. The component benchmark improved 80.266 to
  24.009 ms (70.1%). The actual full v10 styled CPU-fallback preview improved
  median completed render from 48 to 18 ms across five alternating pairs
  (62.5%), comparing 3,430,400 pixels bit for bit. The source photo, Georgia
  bold/italic/underline styling, selected pink/outlined word, ACES2 model, and
  active monitor ICC were retained. No GPU-disabling policy was used; backend
  and zero Metal-dispatch assertions verified the normal fallback route. The
  disabled-text control changed only 868 glyph pixels. The default remains
  serial; only interactive text transforms opt in.

## Auto Level qualification

The estimator itself was unchanged: the current thresholds passed the expanded
photo corpus. Two credited NOAA assets are tracked in
`tests/fixtures/auto-horizon/` with source URLs, hashes, and use guidance. The
clear ocean horizon estimates -0.75 degrees consistently at source, half, and
640 px scales; known +/-4 degree rotations produce the corresponding relative
changes. Hazy ocean and mountain scenes are refused rather than forced level.
The expanded synthetic and real-photo test passed. This qualifies the tested
corpus, not all photographic horizons or native crop interaction.

## Slider source reuse

The canonical Decode callback now reuses an exact linear source for interactive
drafts only. It retains at most one entry and 32 MiB, separate from the existing
legacy LRU. Source revision, decode/color/RAW settings, preview size, and crop
remain in the key; overlay and retained RAW-highlight analysis bypass it.
Consumers copy the buffer before adjustments. Settled/full-resolution requests
cannot populate this new cache. Completed-render aggregate telemetry includes
its bytes; the older shared photo-cache budget remains scoped to the legacy
caches.

The corrected same-mode 1600x1047 probe compares decoded-QImage-warm linear
misses with linear hits at three Exposure settings, alternating measurement
order and rejecting adjusted/rendered cache hits. Outputs match byte for byte.
Median completed render improved 38.0108 to 28.1952 ms (25.8%); linear-source
work improved 8 to 0 ms at integer-millisecond telemetry precision. Source
replacement, RAW-input-state, and isolated crop invalidation passed, as did
settled/full-resolution retention guards. This is a CPU/offscreen slider
fixture result, not a native slider-presentation measurement or full-RAW test.

No visible Mac checks or native frame-rate claims are included in this round.

Brush guard probes measured 204.339 versus 204.324 ms for 30 small strokes
(median of seven alternating batches), well within the 5% regression cap.
Separate offscreen host processes retained identical compressed payload hashes
and peaked at 209,813,504 bytes current versus 210,812,928 reference. This is
a narrow peak-RSS guard for the large-brush fixture.

The first slider test run was rejected because adjusted-cache reuse skipped
Decode. The corrected test assigns a unique adjusted identity and asserts
canonical Decode completion with no adjusted or rendered cache hit. A later
full-resolution guard exposed missing scoped-cache telemetry on the existing
full-resolution path; completed-render logging now always snapshots scoped
retention. Full resolution can still use its pre-existing legacy cache; the
assertion concerns the new single-entry draft cache.

The RAW invalidation fixture was corrected after the recipe compiler rejected
simultaneous `exposure` and `rawExposure` values. It now tests a valid RAW input
state and restores the previous state before separately checking crop identity.
No production admission policy was relaxed.

## Final verification and delivery

The Release app build completed successfully. The final combined offscreen
suite passed **24/24**, with no failures or skips, in 48.44 seconds. Physical
Metal was available for device-alpha/color and styled fallback checks. This
includes the previous 19 text, crop, layer, paint, document, and color checks,
plus the four new component/cache checks and styled ICC integration check.

Final confirmation measured styled renders at 47 versus 16 ms, slider renders
at 35.0283 versus 24.8553 ms, and Selection raster work at 0.279959 versus
0.19775 ms (reference versus candidate). These corroborate the initial
accepted measurements; all comparisons retain exact pixels/alpha or payloads.
No further candidates were attempted after the gates passed.

Built app: `build/release-macos/apps/desktop/Looksy.app`. Executable SHA256:
`93e9c4589c914fabb2e381994b8c89d88b2c4d6b617d65f74ff22ea46e60d206`.
Source hashes, test logs, and component evidence are in the local ignored
`docs/evidence/editor-responsiveness-2026-10-27/receipt.json` package. Production
source hashes still match the coordinated build. Final cache test fixture
corrections were compiled/linked using captured target commands; the app's
production source remained unchanged.

The running editor was not closed, relaunched, or modified. Save current work
and launch the rebuilt app to use these changes. Native text/brush/slider
presentation cadence, real pointer interaction, and integrated crop feel still
require the paused visible QA pass; these CPU/offscreen results do not certify
60 fps or end-to-end input-to-photon latency.
