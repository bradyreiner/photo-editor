# Asset-bound recipe saving

## Defect and behavior

`loadAsset` called `persistCurrentState` before changing the active asset. That
method only cached the old state in memory and started a 350 ms timer. When the
timer fired, `flushPersistedState` read the then-current asset ID, path and recipe.
Rapid switching could therefore save the new photo while leaving the outgoing
photo's edits uncommitted. The in-memory history cache could hide this until a
fresh app/controller loaded the catalog.

The controller now captures an asset-bound snapshot when persistence is
requested. The save queue retains one immutable in-flight snapshot and coalesces
the newest pending state per asset. The callback uses the captured asset, path,
recipe and migration evidence, never the currently selected document. Successful
completion advances the queue. Failed helper startup, process exit or preparation
retains pending work and schedules a two-second retry; a failed older ordinary
revision cannot overwrite a newer queued edit. Save failure status names the
affected asset.

Rendering-migration approval is an ordered transaction, not an ordinary
coalescible save. Failed in-flight approval retains its exact recipe and evidence
ahead of subsequent edits; the evidence is never attached to a different recipe.
Switching assets clears the active UI's old migration evidence after capturing
its save snapshot.

## Verification

`editor_recipe_save_queue_test` covers asset identity, debounce coalescing,
immutable active snapshots, multiple assets, failure ordering, retry and the
approved-migration barrier.

`editor_recipe_catalog_roundtrip_test` creates a new private PostgreSQL cluster
and imports temporary media through the real catalog helper. It edits the
first photo with a linked image layer, transform, layer mask and gradient mask
with local exposure/temperature edits, switches immediately
to the second photo, edits it and waits for saves. It also injects a PostgreSQL
save rejection, checks that the previous committed recipe survives, removes the
rejection and tests saving the newest edit after switching again. It reads the
actual legacy/canonical recipe pair, resolves stored canonical assets, opens fresh
controllers and compares persisted state plus actual output pixels. An optional
RAW path exercises both a DNG document and linked DNG alongside the JPEG document.
The test accepts a PostgreSQL binary directory,
never an existing database URL; all database/media/cache/backup roots are private.

The first database runs verified save rejection/retry but caught an additional
projection mismatch: canonical unit-rectangle geometry reopened a neutral image
as an explicit crop. Neutral full-frame geometry at zoom 1 now restores the
desktop's no-crop sentinel. Other zoom/rectangle behavior is not rewritten.
The persistence regression verifies this and canonical rewrite stability.

Accepted real PostgreSQL runs:

- JPEG: `/private/tmp/recipe-catalog-jpeg-accepted.log`.
- DNG document + JPEG document + linked DNG:
  `/private/tmp/recipe-catalog-raw-roundtrip.log`.

Both pass rapid-switch saves, failed-transaction preservation, newest-edit retry,
actual canonical/legacy database readback, fresh-controller state equality and
identical rendered pixels. Each private cluster reports a successful stop; ports
62821 and 62922 independently report Connection refused afterward. Temporary
clusters/media are cleaned up; no existing user catalog or original RAW fixture
is changed. This is fresh-controller reopen, not a complete app relaunch proof.

Three focused regressions pass in 14.83 seconds:
`editor_recipe_save_queue_test`, `edit_recipe_v13_persistence_test` and
`source_layer_relink_test`. Log: `/private/tmp/recipe-snapshot-acceptance-tests.log`.
The final app and test builds succeed with no compiler warnings/errors in
`/private/tmp/recipe-snapshot-crop-build.log`; scoped `git diff --check` passes.

The first final-build native smoke run,
`/private/tmp/framehouse-recipe-queue-native-MDvV7Z/`, exceeded the existing
45-second proof deadline. The relink settled render took 9,873 ms after a
4,005 ms interactive render; no receipt was produced. This is not accepted UI
or realtime evidence. A scoped process check confirmed no bundled model runtime
remained. The unavailable-catalog pending-save shutdown warning remains expected
until close/recovery coordination is implemented.

The same enabled-save workflow at
`/private/tmp/framehouse-recipe-queue-recheck-ZaQFSA/` also exceeded the deadline:
relink interactive 3,754 ms, settled 11,334 ms. Again no bundled model runtime
remained after exit. Do not dismiss this repeated slowdown as an isolated run.

A controlled diagnostic run with `PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1`
at `/private/tmp/framehouse-recipe-disabled-comparison-DT8Ekf/` passed the same
workflow in 37,295 ms; relink interactive 866 ms, settled 1,604 ms. Its private
runtime stopped and port 63831 independently reported Connection refused.
This implicates persistence-related contention but does not identify its root
cause. It is not acceptance of enabled-save UI responsiveness. Next work must
measure preparation/retry costs and protect interactive rendering while retaining
asset-bound saves; pending native acceptance must remain explicit.

Later instrumentation in `recipe-stdin-transport-2026-09-07.md` produced an
enabled-save pass without an optimization: 15 ms preparation and a 1,645 ms
settled relink. That prevents attributing the earlier variable timing to save
contention without further controlled measurements. No speed-up is claimed.

## Remaining limits

The original queue-only implementation was in memory. The later
`recipe-crash-recovery-2026-09-07.md` adds a local outbox, actual killed-editor
recovery and an independent save-status/error chip. Close coordination when a
checkpoint fails and user-facing recovery repair still need work. Preparation and
serialization still run on the UI thread. The later stdin-transport slice removes
the command-line size/privacy problem, with an explicit 256 MiB payload cap.
Immutable linked-file snapshots, self-contained
projects, broad camera/mask corpora and foreground realtime rendering remain
separate gates. None of these is claimed complete by a normal catalog roundtrip.
Crop preset labels, legacy implicit crop/zoom, and non-neutral geometry need their
own complete persistence corpus; the neutral crop repair does not prove them.
