# Editor stall fixes — September 10, 2026

The repeated two-second pauses came from five legacy recipes being rejected by
canonical-only persistence before a helper could start. Each retry rewrote the
roughly 5.7 MB outbox twice. The thumbnail scheduler also enqueued the whole
catalog, and hidden gallery updates repeatedly rebuilt its rows.

## Changes

- Save explicit `legacy-oklch-v1` recipes through bounded stdin without changing
  their renderer or requiring canonical decoder provenance. PostgreSQL rejects
  legacy writes over an already canonical asset. All recipe writers and migration
  acceptance share the per-asset transaction lock.
- Preserve save failures in the save status, report the actual failure, and back
  off retries from 2 to 60 seconds. New edits reset the delay. Successful outbox
  cleanup clears a previous cleanup error, while pending helper errors remain.
- Queue thumbnails only for the selected photo, viewport and 18-item prefetch
  margin. Drop pending requests after scrolling away. The edit filmstrip reports
  its visible range, and the hidden gallery no longer rebuilds on each thumbnail.
  Logs separately measure pool wait and completed-result delivery delay.
- Publish full previews from memory when an image provider is present; omit the
  blocking full-size PNG encode. Native texture and full-resolution backing stay
  intact. File-only render callers retain the disk-cache path.
- Restore flattened legacy file export by decoding through its compatibility
  path. Canonical exports keep their admitted Decode stage and validation.
- Default the lightweight launcher to Release. `--debug` is explicit. Restart
  either build from this checkout before acquiring the shared outbox. Other
  checkouts are excluded. Build only the app target and its dependencies.
- Compile only the large shell smoke scenario driver without optimization. Its
  previous optimized compilation used about 17 GB and ran for several minutes;
  application, controller and engine compilation remains optimized.

## Verification

- All five recovered snapshots passed exact preparation and real PostgreSQL
  persistence in a disposable cluster. Stale legacy writes over canonical rows
  were rejected without changing either stored recipe.
- The rebuilt optimized app was reopened against the existing catalog. All five
  original snapshots match their committed JSON exactly, with no renderer
  migration. The outbox is empty and no save failure occurred during recovery.
- On that launch, thumbnail queue waits peaked around 1.83 seconds and drained.
  Subsequent sampled edit frames had maxima around 34–36 ms; the repeated
  checkpoint/retry pauses stopped. This is startup/idle evidence, not a complete
  interactive input-to-photon benchmark.
- Real recovered CR2 curve edit: draft/reference max channel difference 0/255;
  native full preview/export max difference 1/255. Full output visually inspected.
  The same recipe's Release full edit render changed from 8,253 to 6,531 ms;
  PNG encoding changed from 1,621 to 0 ms. Other machine activity was present,
  so these single runs are diagnostic measurements, not benchmark percentiles.
- Seven focused CTest cases passed: V13 persistence, bounded Studio input,
  preview feedback, save status, save queue, save journal, and corrupt cache.
- Catalog `--search-preview-contract` passed, including the new 1,000-photo
  bounded queue and obsolete-demand checks plus rendered-thumbnail tests.
- Desktop PostgreSQL round trip passed rapid switching, retry backoff, exact
  canonical reopen, crash-before-debounce recovery, migration acknowledgement
  replay, and coordinated close/outbox failure recovery.
- `cargo test --workspace --locked` and all 11 isolated launcher tests passed.

## Remaining limits

Full-resolution legacy RAW processing still uses the exact CPU input path and
can take several seconds (6.53 s in the measured edit). The camera-LUT fidelity
fallback was deliberately retained. A foreground interaction run is still needed
to establish drag, photo-switch and zoom latency percentiles.

The full catalog test reaches an import-completion timeout, also with an isolated
PostgreSQL server; the focused thumbnail contract passes. The separate packaged
UI proof did not produce its screenshot receipt, and Computer Use was unavailable
because its runtime could not load `@oai/sky`. Native pixel and QML component
checks passed; a final whole-window capture is not claimed.

Evidence and private backups: `/private/tmp/framehouse-stall-fix/`. The original
backup is `pending-recipes-before.json`; another snapshot was taken immediately
before relaunch. These artifacts contain private recipes and should stay local.

## Focused local commands

```sh
./scripts/run-native.sh --no-build
./scripts/run-native.sh --build-only --jobs 4
python3 scripts/test-run-native.py
cmake --build --preset build-release-macos --target catalog_asset_model_test editor_recipe_catalog_roundtrip_test
QT_QPA_PLATFORM=offscreen build/release-macos/tests/catalog_asset_model_test --search-preview-contract
python3 scripts/test-legacy-recipe-save.py
PHOTO_EDITOR_TEST_POSTGRES_BIN_DIR=/opt/homebrew/opt/postgresql@14/bin QT_QPA_PLATFORM=offscreen build/release-macos/tests/editor_recipe_catalog_roundtrip_test
```

The PostgreSQL proof commands create disposable clusters and never reset the
user's catalog. Supply `--postgres-bin` to the Python proof on another setup.
