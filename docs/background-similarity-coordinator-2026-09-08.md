# Background similarity-search coordinator

## Status and boundary

`LocalSimilaritySearch` now connects the native-owned encoder to the persistent
vector store with asynchronous preview preparation, indexing and ranking. It is
production-compiled and tested with the approved offline SigLIP 2 model. This is
**not yet instantiated in main.cpp or connected to Find photos**. No app model
configuration, user index, model download or chat-model copy changed in this step.

The coordinator indexes an explicitly supplied current-view snapshot on demand;
it is not an automatic whole-catalog indexer. Its caller must supply a stable
catalog namespace and synchronously invalidate on asset, recipe, relink, deletion,
catalog or filter changes. That live application binding is still required.
The existing Find photos description/detail search remains unchanged.

## Contracts implemented

- One owned encoder request and one disk task at a time. Replacement searches
  are rejected while canceled work drains; there is no unbounded admission queue.
- File reads, byte hashing, SQLite staging/commit and ranking run in worker tasks.
  Model loading/inference remains in the owned offline child process. Snapshot
  validation/copying remains on the calling thread and has not been benchmarked
  for a large catalog.
- Catalog namespace, encoder fingerprint and snapshot-contract version determine
  the index space. Asset IDs are hashed; current content includes original path,
  recipe, preview path and preview-byte digest. Changed bytes at an unchanged path
  and recipe changes cause new vectors; unchanged content reuses stored vectors.
- A private, at-most-64-MiB byte snapshot is hashed and sent to the encoder. The
  encoder cannot accidentally read a later replacement of the catalog file.
  Only one preview snapshot is prepared at a time. Current catalog authority is
  still the caller's responsibility; external file changes are re-read on the
  next search, not watched automatically.
- The catalog now exposes `searchPreviewCurrent`. It is granted only when a
  thumbnail completion matches the current source and recipe, cleared when that
  thumbnail is invalidated, and suppressed during transient edits. Historical
  stale-result diagnostics are deliberately separate: rejecting an old worker
  result must not permanently disqualify a newly validated current preview.
- The coordinator only consumes an explicitly current rendered photo preview.
  It never falls back to the original RAW/JPEG, fetches a remote URL, or treats
  missing/failed previews as negative matches. Imported/offline cached thumbnails
  without this runtime provenance remain excluded until validated; persisting
  that provenance across reopen is not implemented here.
- Generation invalidation immediately clears results, cancels future publication
  and suppresses late inference. Background tasks capture values, not the owner.
  Stage/commit are serialized by the coordinator and retain the store's CAS ticket
  checks. An already-entered SQLite operation may finish after cancellation, but
  cannot publish results for the canceled generation.
- Ranking receives only the explicitly prepared asset/content scope. A filtered
  absence never deletes another photo's vectors or broadens the scope. Old derived
  rows may remain on disk; whole-catalog deletion pruning/forget-index UI remains
  open. This is a retrieval cache, not catalog authority.
- Results expose cosine similarity, not a match score/confidence. Status and
  evidence explicitly say these are similar candidates, not verified matches;
  fine-detail/negation verification and calibrated relevance remain separate.

## Proof

`tests/local_similarity_search_test.cpp` checks first indexing, reuse, strict
filtered scope, catalog separation, changed recipe/bytes, stale/transient/missing
previews, no original/remote fallback, decode failure coverage, duplicate scope
rejection, in-flight invalidation/backpressure, responsive heartbeats and reuse
after an owned-worker restart. Its controlled worker is not model-quality proof.

`catalog_search_preview_contract` runs the existing catalog regression through
its real rendered-thumbnail tests, with new current/transient/stale/recovered
provenance assertions, and deliberately stops before later import workflows.
The default full catalog invocation is unchanged. A full run passed those earlier
assertions but failed later with `import should eventually finish` after 42.81 s;
it is **not** a passing whole-catalog regression. Log:
`/private/tmp/similarity-catalog-test.log`.

Real-model artifacts are under `/private/tmp/framehouse-siglip2-O2ha9I/`:

- `coordinator-final-live.json` / `.log`: actual MPS inference, five existing image
  inputs/four scenes, all ten queries ranking an expected fixture first. First
  query including loading and indexing takes 5,703 ms with 570 ten-ms heartbeat
  ticks. Subsequent whole-coordinator calls take 54-65 ms on this tiny scope and
  reuse all five vectors with zero image re-encoding. A deliberately restricted
  one-photo scope cannot return the excluded dog. Restart reuses all five vectors
  and retains the expected top result; both owned workers stop. Stderr is empty.
- `coordinator-live.json` is the earlier successful run (5,794 ms first query,
  54-67 ms later calls); the final receipt additionally verifies restart reuse.
- `coordinator-regressions.log` records the sandbox attempt: the existing local
  HTTP fixtures could not bind. `coordinator-regressions-local-runtime.log` records
  the permitted rerun, with all nine initial focused suites passing in 11.25 s.
- `coordinator-final-regressions.log`: all ten final focused suites pass in
  25.81 s, including the rebuilt catalog-dependent search/edit target and real
  thumbnail/provenance contract. The separate two-contract run also passes in
  16.40 s (`/private/tmp/similarity-provenance-contracts.log`).

These are native coordinator measurements, not QML/Find photos, frame timing,
whole-library latency or representative photographic recall. One image is an
existing RAW-derived preview; no new RAW decode is exercised. Builds overlapped
these correctness runs, so these are observed timings, not controlled performance
comparisons or an accepted benchmark improvement.

Build logs: `/private/tmp/similarity-coordinator-provenance-build.log` and
`/private/tmp/similarity-final-targets-build.log` (desktop, coordinator, catalog and
catalog-dependent search/edit test targets). No QML was changed in this step.

## Next

Bind the coordinator to live catalog/scope changes and a clearly labeled
similarity mode in Find photos, using explicit trusted runtime configuration.
Prove that actual UI actions, filter projection, edits, relink/deletion, cancel
and relaunch cannot display stale or out-of-scope candidates. Then measure larger
labeled libraries, add efficient background indexing/pruning, and complete
packaging and the separate verified-detail/agent workflow. None of this closes
the remaining RAW/color, loading/export, true 4D or professional-tool gates.
