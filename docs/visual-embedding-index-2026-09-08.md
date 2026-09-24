# Local image/text retrieval index foundation

## Status and purpose

Added a production-compiled SQLite vector store and a focused native test target.
This is groundwork for genuine image/text retrieval, **not an enabled replacement
for the current search UI**, a completed encoder, or photographic quality proof.
The existing description-ranking and photo-verification paths remain unchanged.

The installed local server advertises vision/completion capabilities, not a
dedicated embedding model. Ollama's documented
[embedding endpoint](https://docs.ollama.com/api/embed) accepts text; its presence
does not establish a shared image/text encoder. Google's
[SigLIP 2 Base model card](https://huggingface.co/google/siglip2-base-patch16-224)
explicitly supports image/text retrieval. Its
[published files](https://huggingface.co/google/siglip2-base-patch16-224/tree/main)
total about 1.54 GB. Permission was requested for an isolated test download;
no model was downloaded or copied. This is separate from the pending permission
to clone existing chat-model weights for managed-runtime testing.

## Implemented contracts

`apps/desktop/src/visual_embedding_index.{h,cpp}`:

- Each call owns its connection and is explicitly a blocking **worker** API.
  This layer does not call models, send network requests, or mutate catalog data.
- SHA-256 identity keys isolate catalog/encoder/preprocessing spaces, assets and
  current rendered-preview content. The future coordinator must construct those
  identities from pinned artifacts and actual preview/recipe state; the storage
  layer cannot infer or verify those inputs itself.
- `stage` invalidates a changed content revision before inference and returns a
  random ticket. Same-revision ready vectors can be reused across connections.
  `commit` uses an atomic conditional UPDATE: stale or duplicate workers cannot
  overwrite accepted data or resurrect deleted/recreated rows. `erase` removes
  one exact space/asset identity.
- The catalog-side coordinator must serialize staging/deletion in authoritative
  change order. Only inference completion may arrive out of order. Tickets do not
  turn an arbitrary stale catalog snapshot into current authority.
- Search accepts an explicit current asset/content scope and reads one SQLite
  snapshot. An old vector is excluded even if invalidation has not caught up with
  the caller's current preview content. The future controller must still discard
  a completed query if its catalog/query generation has changed meanwhile.
- Vectors are finite, nonzero, dimension-checked, normalized and stored in an
  explicit little-endian float format. Corrupt length/type/NaN/normalization fails
  the entire query with no plausible partial results.
- Exact cosine top-K uses a bounded heap, keeps original scope order for ties,
  and reports missing/unindexed candidates separately. Similarity is not a
  calibrated confidence or an automatic admission threshold. The query does not
  broaden scope or silently discard low scores.
- Current bounds: 1-4096 dimensions, at most 100,000 scope entries and top-K
  1-1000. Unsupported requests fail explicitly. The implementation is an exact
  scan, not ANN or a measured million-photo solution.
- Read-only search never creates a missing database; invalid paths and symlink
  index files are rejected. Files are owner-readable/writable. These checks do
  not claim encryption, filesystem snapshot erasure or universal path-race safety.
- Cancellation is checked while validating candidates and while scanning rows.
  Interrupted queries return no partial ranking. SQLite busy waits remain bounded
  to one second; the observed cancellation time below is an uncontended case.

## Native evidence

- `visual_embedding_index_test` covers normalized exact ranking/ties, scope and
  space isolation, missing files, invalid vectors/bounds, content invalidation,
  late workers, delete/recreate, two competing commits, cancellation, symlink
  rejection and atomic failure after a valid row followed by corrupt data.
- `/private/tmp/visual-embedding-index-final-regressions.log`: all five focused
  suites pass in 8.63 seconds: embedding index, visual search, managed runtime,
  artifact verification and search/edit workflow.
- `/private/tmp/visual-embedding-index-final-build.log`: test and production app
  compile/link successfully with the cancellation fix. The final test-only
  corruption additions rebuild in `visual-embedding-index-corruption-build.log`.
- `/private/tmp/visual-embedding-index-final-benchmark.json`: six exact top-32
  queries over 20,000 synthetic 768-dimensional vectors take
  **578, 573, 578, 575, 575, 607 ms** in the local debug build. Every result agrees
  with an independently full-sorted reference ranking. Running cancellation
  completes within the timer's one-millisecond resolution in this run.
- The earlier receipt `visual-embedding-index-benchmark.json` measured 144 ms
  cancellation while candidate validation ran without checks. Adding those checks
  is the only production change between the two benchmark candidates. Its timing
  wrapper exited nonzero because the sandbox blocked `sysctl`; the final wrapper
  and executable both exit successfully.
- `/private/tmp/visual-embedding-index-final-benchmark-time.log`: whole-process
  maximum RSS 110,608,384 bytes and peak footprint 100,811,256 bytes. This includes
  synthetic fixture generation, SQLite and the reference-sort test machinery;
  it is not isolated store overhead or model-inference memory.

The benchmark seeds normalized synthetic rows in a single disposable transaction;
it measures retrieval, not import/index-build throughput. The database was just
written, so none of these measurements is a guaranteed disk-cold run. No images,
encoder, RAW decode, UI interaction or user catalog were exercised by this test.
All owned builds/tests/benchmark processes are terminal. Temporary fixture
databases are removed by the test's scoped temporary-directory owner; retained
logs/receipts remain at the paths above.

## Work required before enabling fast photo search

1. Integrate a verified local image/text encoder with pinned artifact and exact
   preprocessing/tokenization identity. Evaluate it on actual photographs and
   queries, not synthetic vectors or chat-generated numeric lists.
2. Wire a background catalog coordinator: import/update/delete/relink/recipe
   invalidation, serialized authority changes, stale-result guards, recovery,
   batching, resource budgets and visible indexing/failure state.
3. Retrieve candidates across the intended library/filter scope; apply bounded
   image verification for fine detail, negation and multi-condition queries.
   Preserve missing-index coverage, visible evidence and constrained edit scope.
4. Prove end-to-end retrieval quality and latency on a labeled multi-photo JPEG
   and edited-RAW corpus, including model changes, cancellation and relaunch.
   Compare exact versus ANN only when scale requires it and quantify lost recall.

RAW/color, loading/export, trained 3D/4D scenes, managed inference and professional
editing requirements in `active-app-completion-goal.md` remain fully open where
their own completion evidence is missing.
