# Similarity search: live catalog and native UI

Follow-up: `search-runtime-package-2026-09-09.md` verifies a self-contained worker
inside the app resources through the same native UI. That development package
supersedes the external-Python-only boundary below, but managed model setup,
release compatibility/signing and whole-library quality remain open.

## Implementation

The approved SigLIP 2 coordinator is now connected to the production Library UI
through `CatalogSimilaritySearch`. With an explicitly configured local runtime,
AI Search offers a **Similarity search · SigLIP 2** mode. The actual Find photos
button ranks current rendered previews and projects the top 32 candidates into
the Library. Show all in view removes only the visual-result filter. The prior
description/detail search and confirmed agent-edit workflow remain separate;
similarity candidates are never labeled as verified matches or confidence scores.

The model is still an explicit local-runtime opt-in, not a packaged installer.
The approved model and dependencies remain in the isolated proof installation.
No new model was downloaded, no chat weights were copied, and no user catalog
or persisted model configuration was changed by this work.

## Catalog authority

- The native binding owns subscriptions independently of the QML panel lifetime.
  A search snapshots the current non-visual filters and uses the selected catalog
  namespace; it never substitutes the whole catalog for a filtered scope.
- Filter changes, model resets, deletion/insertion, loading, source and recipe
  changes invalidate the coordinator generation. Result-filter publication guards
  its own reset/filter notifications so it does not cancel itself.
- The catalog now emits scoped source/recipe/preview authority changes even for
  assets hidden by top-K results. A hidden candidate's edit can change ranking
  and must invalidate it. Unrelated background previews outside the query scope
  do not repeatedly cancel that search.
- Cleanup is deferred until the model's notification completes, with a revision
  ticket to prevent an old cleanup from clearing a newer search.
- Closing search, leaving the Library, switching search modes, starting an agent
  run or opening Find & edit cancels the similarity binding. Search itself never
  dispatches edits or changes recipes.
- Relinking emits source invalidation before catalog/job persistence calls.
  The earlier coordinator still suppresses canceled replies and reuses only
  current-content vectors. Physical deletion pruning of old derived vectors and
  persisted offline-preview provenance remain separate unfinished work.

## Explicit runtime configuration

The production entry point accepts these environment variables:

| Variable | Required value |
| --- | --- |
| `PHOTO_EDITOR_SEARCH_PYTHON` | Absolute path to the trusted, pinned search virtualenv interpreter |
| `PHOTO_EDITOR_SEARCH_WORKER` | Absolute path to this checkout's `scripts/search_model_worker.py` |
| `PHOTO_EDITOR_SEARCH_MODEL_ROOT` | Absolute path to the verified approved model directory |
| `PHOTO_EDITOR_SEARCH_DEVICE` | `cpu` (default) or explicit `mps` |

No path is inferred from the temporary proof installation and no download is
implicit. Absent configuration leaves the existing detail search unchanged.
The derived index is `visual-search/embeddings.sqlite3` under the app data root;
its space includes the catalog namespace and verified encoder identity.
Automatic packaged setup/model-manager integration remains open.

## Verification

The named `catalog_similarity_search_contract` uses the real catalog and binding
with an owned controlled worker. It verifies filtered search, guarded result
projection, clear/show behavior, filter invalidation, deletion/recreation during
inference, an edit to a candidate hidden by top-K, exclusion of its transient
preview, no unrelated edits, unrelated-preview isolation and owned-process stop.
This is a deterministic lifecycle contract, not model-quality evidence.

The actual-app driver now supports `--similarity-search-proof-dir` with explicit
`--search-python`, `--search-model-root`, `--search-device` and
`--search-fixtures`. It loads the real Main.qml/StudioWorkspace, invokes the mode
control and actual enabled Find photos button, checks the expected first result,
repeat vector reuse, catalog projection/clear, invalidation on text-filter change,
recovery, wide/compact captures and owned-worker shutdown. The catalog is isolated.

Initial real-model proof:
`/private/tmp/framehouse-siglip2-O2ha9I/ui-binding-proof/receipt.json`.
Five inputs/four scenes, expected dog first. Initial search is 6,024 ms with five
image encodes; subsequent UI searches are 251 and 226 ms with all five vectors
reused. These include native UI projection and are not the earlier 54-65 ms
coordinator-only measurement. They do not establish whole-library performance,
frame latency, general recall or fine-detail correctness.

Initial wide/compact captures were visually inspected. The new checkbox label
had insufficient contrast and the mode transition emitted a Boolean binding
warning; both were corrected before the final proof. The inspector's separate
full-resolution loading state is not certified by this search proof.

Final proof: `/private/tmp/framehouse-siglip2-O2ha9I/ui-binding-final/receipt.json`
and `ui-binding-final.log`. The same actual UI actions, expected dog, filter
invalidation/recovery and owned-worker shutdown pass. Initial search is 6,074 ms;
repeat searches are 214 and 269 ms, reusing all five image vectors. Final wide
and compact captures were inspected: the mode label is now legible. There are
no ReferenceError, TypeError, Boolean-assignment or binding-loop messages in
the final log. These are observed tiny-fixture timings, not a general performance
promotion or proof of independent detail accuracy.

Eleven focused contracts pass in 30.83 s in
`/private/tmp/framehouse-siglip2-O2ha9I/ui-binding-regressions.log`.
The final rebuilt run also passes all eleven in 30.90 s:
`/private/tmp/framehouse-siglip2-O2ha9I/ui-binding-final-regressions.log`.
Build logs are `/private/tmp/similarity-scoped-final-build.log` and
`/private/tmp/similarity-handoff-build.log`. The previously recorded full catalog
import timeout remains unresolved; this work does not claim that test passes.
All owned builds, test runs and native UI/encoder proof processes are terminal.

## Remaining full-goal work

Package/manage the approved search runtime, evaluate larger labeled libraries,
index efficiently in the background and prune deleted derived data, calibrate
retrieval/detail verification, and verify real editing/agent workflows. The
independent RAW/color/realtime loading, export responsiveness, true 4D capture and
professional masking/tool completion gates remain open in the active goal.
