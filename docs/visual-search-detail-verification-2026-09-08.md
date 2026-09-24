# Photo verification for missing search-description details

## Behavior

Indexed search now distinguishes explicit matches, explicit contradictions and
insufficient descriptive evidence. An omitted query detail is not automatically
treated as an absent object. Insufficient cases are checked against the actual
local preview with the existing query-conditioned vision path.

The rank response must include every candidate exactly once and a valid evidence
classification. Invalid/incoherent responses fail atomically. Unknown evidence
is verified even when its model relevance score is high. Explicit descriptive
matches and contradictions retain the text-only path.

The verifier is a direct-only child controller using the same local endpoint,
verified managed-runtime boundary, reduced/color-managed image preparation,
strict response schema, content/query/model score cache and cancellation guards.
It cannot recursively start another description verification pass. Only candidates
inside the current 32-photo batch are supplied. Results preserve frozen catalog
IDs and original input order for tied scores across both evidence paths.

Verified matches carry `evidenceSource: image-verification`; descriptive matches
retain `saved-description`. The UI distinguishes the two and warns that missing
details can require slower photo checks. `detailVerifiedCount` counts successfully
scored detail candidates, including valid session-cache reuse, not necessarily new
model inferences.

Cancellation stops the child, invalidates old callbacks and retains prior completed
matches. Detail failures keep their pending candidate list for retry without
rebuilding descriptions or double-counting successful batches. A new query clears
the pending list; verification cannot leak matches into its replacement query.

## Evidence so far

- Baseline `/private/tmp/search-detail-baseline-test.log`: the new regression
  fails because insufficient evidence silently loses a valid image match.
- `/private/tmp/search-detail-proof-versHS/regressions.log`: four focused suites
  pass in 9.48 seconds (visual search, managed runtime, model artifact and
  search/edit workflow). Search cases cover missing-detail recovery, high-score
  uncertainty, explicit positive/negative paths, invalid evidence, failed-check
  retry, cancellation/replacement, cached direct-score reuse and 32/35-item bounds.
- `/private/tmp/search-detail-proof-versHS/integrated-build.log`: production
  desktop, native QML proof driver, search test and agent controller build cleanly.
  Focused `qmllint` on `VisualSearchPanel.qml` is also clean.
- Existing reference JPEG, inspected visually:
  `/private/tmp/framehouse-six-raw-source-v3/hdrplus-nexus6-skin-0218/hdrplus-nexus6-skin-0218-reference.jpg`.
  Its saved description omits the visible dark hat. The preceding run returned no
  match for "a close-up portrait wearing a dark hat"; see
  `managed-search-restart-2026-09-08.md`.
- `/private/tmp/search-detail-proof-versHS/hat-query.json`: that same query now
  reuses one description, generates none, performs one photo detail check and
  returns the portrait at model relevance 90 with actual-image evidence. It took
  60,421 ms. This is a one-photo functional result, not realtime retrieval, a
  calibrated confidence score or a broad quality benchmark.
- The CLI receipt now exposes `detailVerifiedCount` and uses null managed-runtime
  proof fields for an external-server run, avoiding a misleading shutdown claim
  when no owned server was started. Its rebuild is `receipt-build.log`.
- `absent-glasses.json`: "a person wearing bright red eyeglasses" correctly
  returns no match after one photo check, reusing the description without creating
  another. It takes 50,621 ms. This is the same portrait, not an independent corpus.
- `contradiction.json`: "a snowy mountain landscape without any people" correctly
  returns no match, but unnecessarily checks the photo despite a described person
  contradicting the required absence. It takes 54,332 ms. The ranking instruction
  now explicitly prioritizes a contradicted required condition over unrelated
  missing details. With this clarification, `final-contradiction.json` returns
  no match with zero photo checks in 17,892 ms, reusing the same description.
  Different model/cache conditions and one query do not establish a general
  latency improvement or reliable logical classification across a photo corpus.
- `final-build.log`: the final production app, native proof driver, search and
  agent controller test targets link successfully. A live process check confirms
  that build is no longer running. `final-regressions.log`: all four focused
  suites pass in 9.79 seconds, including the added mixed descriptive/verified
  equal-score ordering regression.
- `primary-ui-proof.log`: the native QML harness selects the configured external
  local model and invokes the actual enabled **Find photos** button. It reuses
  one description, performs one photo check and finds the omitted hat with
  `evidenceSource: image-verification`. Result-to-Library filtering, clearing the
  filter, focused-photo versus explicit-selection edit-dialog scope and unchanged
  recipe assertions pass. Wide and compact captures are retained in `primary-ui/`;
  the wide result screenshot was visually inspected and shows the actual photo
  and photo-check explanation. This run predates the final contradiction-priority
  wording; its final-build positive rerun is recorded separately below.
- `final-primary-ui.log`: the final-build rerun exits successfully, exercises the
  actual **Find photos** action and still returns the hat match at relevance 90
  after one photo check with one reused description and no new descriptions.
  All the same filter, clear, dialog-scope and unchanged-recipe assertions pass.
  Both `final-primary-ui/visual-search-results.png` and
  `final-primary-ui/visual-search-compact.png` were visually inspected: the result
  image and evidence text are visible at wide width, and the help text wraps
  without clipping at compact width. Compact capture follows intentional filter
  clearing, so the match explanation is not shown there. All build/test/proof
  processes owned by this validation are terminal; the existing external model
  service was neither stopped nor reconfigured.

The native harness loads filesystem QML with isolated in-memory catalog fixtures,
not a real user catalog or a packaged-install launch. Opening the edit dialog
tests its target scope, not a model-generated edit. The harness's intentionally
invalid catalog endpoint and existing Inspector placeholder are separate limits;
these captures do not prove the whole app is warning-free or realtime.

## Boundaries

The model can still misclassify descriptive evidence or miss visual details.
The new classifier is not a formal logical proof or exhaustive object detector.
A missing-detail query can require up to 32 sequential image checks and remain
slow. An image/text embedding index with import/update/delete lifecycle, larger
photographic retrieval evaluation, edited RAW-preview coverage and true packaged
managed-model inference are still open.

The current `VisualSearchIndex` is a key-to-description SQLite cache, not an
embedding index or corpus retrieval service. The next search milestone requires
image/text retrieval with durable model/content identity, import/edit/delete
invalidation, cancellation and actual labeled multi-photo recall/latency evidence.
That work must preserve the visible evidence and constrained edit handoff; the
single-photo results above are not a substitute for it.

Only the existing external local model and temporary derived indices are used.
No model weights were copied or downloaded, and no photos, recipes or catalog
records were edited. Permission for the separate private copy-on-write model
store remains pending. RAW/color/realtime rendering, Gaussian 3D/4D, professional
editing and export throughput/shutdown requirements remain active in
`active-app-completion-goal.md`.
