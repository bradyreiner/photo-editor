# Persistent visual-description index

## What changed

AI Search now defaults to **Reuse saved image descriptions**. The first time a preview is encountered, the installed local vision model describes it independently of any query. Later queries reuse that description, including after restarting the app. Up to 32 descriptions are semantically ranked together in a single text-only local-model request. Unchecking the option restores the original query-conditioned direct photo inspection.

This is a **description index**, not a CLIP/SigLIP embedding index or an instant whole-library nearest-neighbor search. The app still validates preview content, works in explicit 32-photo batches, and uses a local model for ranking. Small details omitted from the description can be missed; use direct inspection for those queries. No new model download was added.

The two-stage structured-output transport follows the installed Ollama [structured-output API](https://docs.ollama.com/capabilities/structured-outputs). Both descriptions and batch rankings are independently validated. Indexed explanations are evidence from the saved description, not a fresh view of the image.

## Data and lifecycle

- Storage: `<AppBootstrap::appDataLocation()>/visual-search/descriptions.sqlite3`. `PHOTO_EDITOR_APP_DATA_DIR` continues to override the app-data root. This is a derived cache, not a change to PostgreSQL catalog authority.
- The SQLite file is owner-readable/writable only. Records contain a versioned content/model hash, bounded description, and insertion time—not pixels, file paths, asset IDs, or queries. Descriptions themselves can contain private image content; they are not encrypted at rest.
- Identity covers the description prompt/preview encoding version, local endpoint/model digest, and encoded preview pixels. Changed previews or model revisions regenerate descriptions. Identical rendered previews can share a record, but every result retains its own current catalog ID.
- Reads/writes run on background workers with connection-local SQLite operations, a one-second busy timeout, transactions, and a 10,000-record bound. Eviction removes only old derived descriptions. Read failures cause reanalysis; write failures show an explicit session-only warning while preserving search.
- Each search starts from current catalog-view assets. Stale index entries cannot independently become search results. Old descriptions can remain after an original is removed; **Forget index** clears saved descriptions and the in-memory description cache without touching photos, edits, or catalog records. Automatic background indexing and per-asset erase propagation are not implemented.
- Local-model/cloud checks occur before either images **or saved descriptions** are sent. Proxies and redirects remain disabled. Model rankings must return every supplied ordinal exactly once; invalid, duplicate, missing, or out-of-range targets fail atomically and can be retried without image reanalysis.
- Existing explicit agent target scopes and draft review/commit boundaries remain unchanged. No agent-callable search/mask orchestration was added in this pass.

## Verification commands

```sh
cmake --build build/release-macos --target visual_search_controller_test photo_editor_app actual_app_workflow_benchmark_driver
build/release-macos/tests/visual_search_controller_test
qmllint apps/desktop/qml/components/VisualSearchPanel.qml
```

The loopback suite covers the original direct search plus persistence across controller destruction/recreation, new-query reuse, reversed batch response/tie ordering, model invalidation, bounded continuation, invalid ordinal retry, cloud rejection before description ranking, write-failure fallback, and forgetting the index. Synthetic responses prove mechanics, not search quality.

Opt-in independent-process live reuse:

```sh
build/release-macos/tests/visual_search_controller_test --live /absolute/photo.jpg qwen3-vl:32b 'portrait wearing a hat' --index-path /absolute/temp-index.sqlite3
build/release-macos/tests/visual_search_controller_test --live /absolute/photo.jpg qwen3-vl:32b 'landscape without people' --index-path /absolute/temp-index.sqlite3
```

The JSON report distinguishes newly generated and reused descriptions, index warnings, results, and elapsed time. These are functional smoke timings, not representative performance benchmarks.

Actual-QML proof adds `--indexed-search --visual-index-path /absolute/temp-index.sqlite3` to the existing `--visual-search-proof-dir` harness route. It still loads filesystem QML and uses temporary catalog fixtures, not the packaged app's live user catalog.

## Observed live evidence

Artifacts: `/private/tmp/photo-editor-visual-index-T22yUO`.

- First process: generated and persisted one description using `qwen3-vl:32b`; completed in 69,251 ms including description generation and ranking.
- Second, independent process with a different query: reused one description, generated zero descriptions, and returned the portrait at relevance 90 in 3,261 ms. No storage warning. This proves cross-process reuse, not a general 21x speedup: the queries differ, model warm-up is not controlled, and the sample is one image.
- Known recall limitation: the generic description omitted the dark hat. The hat-specific query therefore produced no match, while direct inspection in the earlier pass had matched it. The new UI explicitly warns that saved descriptions may omit details and offers direct inspection. Do not call this a validated general-purpose retrieval-quality improvement.
- Actual-QML independent-process reuse passed: one saved description reused, zero new, one portrait returned. Current-photo and explicit-selection dialog scopes remained intact with no recipe mutation. `ui-final-proof.log` and the wide/compact images in `ui-final/` contain the final evidence. The desktop preview encoded differently from the command-line fixture, so it correctly created its own content-keyed record on its first run; subsequent app-window runs reused that record.
- Final loopback regression suite, catalog model test, and agent walkthrough contract test passed. Release desktop and harness builds passed; the new panel passed qmllint. SQLite integrity check returned `ok`. Existing `StudioInspectorGroup.qml` runtime warnings and the temporary PostgreSQL packaging-directory warning remain outside this feature.

## Next

An image/text embedding backend, automatic index lifecycle scheduling, larger-library retrieval evaluation, and agent-callable frozen search targets remain separate next steps. The description index is reusable groundwork, not a substitute for those capabilities.
