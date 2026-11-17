# Library capture order and thumbnails

Photos now open in capture order, first shot to last. The previous default was
newest imports. Sorting uses the full camera timestamp (including milliseconds),
then natural frame-number/filename order for ties rather than random asset IDs.
Unknown dates stay last in both chronological directions. Existing catalogs
missing a full timestamp recover EXIF capture time from original-file headers in
the snapshot workers; filesystem modification time is not used as shot time.
Files without readable capture metadata retain the known date/filename fallback.
Virtual-copy persistence now retains the source capture timestamp on relaunch.

Photos exclude app-created canvases and Board backing sources. Canvases has its
own collection, with transparent/blank documents rendered over a checkerboard in
both the grid and inspector. Boards remains a saved-document collection, and
its thumbnails now render the saved first board in an independent background
renderer. This also handles empty/transparent boards. Cached covers are keyed
by document ID and saved revision; no open document is replaced to make a cover.

Image selection uses a warm gold edge and a small bottom edge for batch
selection, without a gray image wash or a checkmark circle. Pick flags remain
separate from selection. File extensions and the CANVAS type appear in a small
corner label. Filled ratings use a faceted gold star with an upper highlight and
a darker rim, shared by the gallery, inspector, filmstrip, and review surfaces.

## Verification

Artifacts: `build/library-refinement-2026-10-28/`.

- `catalog-verified.log`: collection and sorting regression passes, including
  reversed import order, millisecond timestamps, natural numeric ties, unknown
  dates last, and all canvases excluded from Photos while preserved in Canvases.
- `catalog-suite-final.log`: catalog model and metadata persistence tests pass,
  2/2. The existing canonical-duplicate test selects its intended source by ID
  rather than assuming it will be at row zero under the new default sort.
- `component-test.log`: shared thumbnail selection/filmstrip test passes with an
  edge indicator and no former circular check indicator.
- `native-final/manifest.json`: seven native Cocoa captures of real decoded
  photos, gold ratings, selected edges, a transparent blank canvas, a rendered
  saved Board cover, and normal/compact collection navigation. Saved Timeline
  open/create behavior also passes. The final run exits successfully with a
  temporary-directory cleanup warning during teardown.
- Release C++ and QML/resource builds, final link, and bundled agent/search
  runtime verification pass. `packaged-smoke.json` confirms that the packaged app
  reaches `qml.route.ready` with isolated data/cache directories. Source and
  binary hashes are recorded separately.

The global popup audit still flags four unrelated Scene stock-tooltip violations;
there are no new violations in these Library changes. The all-pages source
contract stops at an unrelated masking pointer-handler assertion. Its failure is
recorded in `source-contract.log`; focused functional/native tests above pass.
These proofs do not establish capture-time accuracy for every camera or for
missing/unreadable originals, and do not measure full-library startup speed.
