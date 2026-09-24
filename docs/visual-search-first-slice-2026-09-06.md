# Visual search: first working slice

This document records the original direct-inspection slice. The subsequent [persistent description-index pass](visual-search-persistent-index-2026-09-06.md) adds a default indexed mode; the session-only limitations below still apply to direct inspection.

## User workflow

Library → **AI Search** → describe visible content → **Find photos**. The existing metadata filters define the starting scope. The app inspects up to 32 photos per explicit batch, ranks clear matches, and shows the vision model's visible evidence for the selected match. **Scan next 32** continues the original scope. **Show all in view** removes only the visual filter.

Select the intended matches, then **Edit selected** to open the existing AI Edit dialog with current-photo or selected-assets scope. Search descriptions are not edit commands. Nothing is automatically applied or committed; existing draft review, masks, and keep/rollback behavior remain authoritative. An active agent draft must be resolved before starting another edit from search.

## Implementation boundary

- This is query-conditioned local vision retrieval, not metadata synonym matching and not an embedding index. Each uncached photo requires a vision inference. It is an initial feature, not a claim of instant library-wide or state-of-the-art retrieval.
- The selected available Ollama vision model is reused; otherwise the first configured available vision model is chosen. No model is silently downloaded or installed.
- Only re-encoded, reduced JPEG previews (at most 768 pixels on either edge) go to the model. Valid tagged previews convert to sRGB; EXIF, filenames, and catalog metadata are not included in the model prompt. RAW images use their catalog preview; unavailable or undecodable previews are counted as skipped. JPEG can fall back to its source raster.
- A score of at least 70/100 is treated as a match, ordered descending with stable ties. These are model relevance scores, not calibrated probabilities. Negation is requested in the prompt, not guaranteed correct by a formal parser.
- Decoding runs on a worker; HTTP is asynchronous with cancellation, stale-generation guards, a three-minute deadline per request, bounded responses, no proxy, and no redirects. A complete schema-valid JSON object can be recovered from Qwen's alternate response field; free-form reasoning is never extracted or displayed.
- Local endpoint and model preflight checks precede image requests. Nonloopback URLs, uninstalled models, models without vision capability, and declared cloud-backed models are rejected. The local Ollama server itself is a trusted dependency.
- A bounded **session-only** cache keys scores by prompt version, query, endpoint, installed model digest, and encoded preview content. No caption database, persisted index, or background whole-library scanning was added.
- Results intersect existing filters. Empty results produce an empty view, not an unfiltered catalog. Hidden selected assets are reconciled by the catalog before the agent handoff. Changing filters or starting the agent cancels an in-progress scan.

The transport follows Ollama's [chat API](https://docs.ollama.com/api/chat) and [structured-output contract](https://docs.ollama.com/capabilities/structured-outputs). The response is independently validated; requesting a schema alone is not treated as validation.

## Verification

Focused test targets:

```sh
cmake --build build/release-macos --target visual_search_controller_test catalog_asset_model_test
build/release-macos/tests/visual_search_controller_test
build/release-macos/tests/catalog_asset_model_test
```

The visual-search suite uses synthetic images and an isolated loopback server for protocol, privacy, ranking, duplicate-preview caching, model/content invalidation, cancellation/stale replies, malformed output, redirects, bounded batches, continuation, and unavailable RAW previews. It does **not** measure real-model search quality. Catalog tests cover stable result order, duplicates/unknown IDs, metadata intersection, selection pruning, clearing, and empty results.

Opt-in real-model smoke (only the supplied fixture is inspected):

```sh
build/release-macos/tests/visual_search_controller_test --live /absolute/photo.jpg qwen3-vl:32b 'a close-up portrait'
```

Actual-QML window proof uses the existing isolated-catalog harness:

```sh
QT_QPA_PLATFORM=cocoa build/release-macos/tests/actual_app_workflow_benchmark_driver \
  --source /absolute/photo.jpg --visual-search-proof-dir /absolute/proof-directory \
  --visual-search-model qwen3-vl:32b --visual-search-query 'a close-up portrait'
```

This harness loads real `Main.qml`/`StudioWorkspace.qml` from the filesystem, not the packaged AOT QML module. It verifies the entry point, layout, result-to-catalog projection, and clearing at wide/compact sizes. It also invokes the actual panel's handoff signal and checks the existing dialog preserves current-photo and selected-assets scope without changing the recipe. It has no AgentEditController, so that check proves dialog routing only, not live masked draft generation. Use `--visual-search-preview /absolute/raw-derived-preview.png` alongside a RAW `--source` to exercise RAW catalog identity with an existing decoded preview. The packaged app must also build successfully.

### Observed live evidence

Artifacts: `/private/tmp/photo-editor-visual-search-2AR93V`.

- Installed local model: `qwen3-vl:32b`; local capability preflight passed.
- JPEG portrait query, “a close-up portrait of a person wearing a dark hat”: matched at model relevance 95 with an appropriate visible-content explanation. Actual library result projection and clearing passed; wide and compact PNGs were visually inspected.
- Same JPEG, “a landscape without any people”: one inspected photo, zero matches.
- RAW catalog asset with the existing decoded `raw-skin-inspection.png` preview: the portrait query matched at relevance 90. Result projection, clearing, and actual dialog routing for a focused photo and a one-photo explicit selection passed without recipe mutation. RAW proof captures are under `raw/`; `raw-proof-final.log` records the successful run. The test pauses thumbnail regeneration and publishes the supplied preview through the catalog completion API; it does not demonstrate a fresh RAW decode during search.
- The initial live run caught the alternate-response-field compatibility problem; after fixing it, the live query and new deterministic regression cases passed.
- Focused controller/catalog/agent-dialog contract tests pass; `VisualSearchPanel.qml` has no qmllint warnings. Runtime logs still contain warnings from `StudioInspectorGroup.qml`, outside the new panel; this is not a warning-free app claim.
- Final Release desktop application and proof/test targets build successfully. CMake also reports an unrelated missing temporary PostgreSQL packaging-runtime directory; no catalog authority or packaging configuration was changed.

These are small functional smoke checks, not a search-quality benchmark or comprehensive RAW-camera corpus proof.

## Next product steps

1. A persisted local image/text embedding index with import/update/delete lifecycle, coverage, and background scheduling; retain vision reranking for detailed queries.
2. Agent-callable search with frozen, reviewable target IDs and a real search → subject/sky mask → editable draft demonstration.
3. Broader retrieval evaluation on the user's photography, including negation, lighting, composition, small objects, edited RAW previews, and large libraries.
4. In parallel product planning, define Scene Lab capture requirements and a static multi-view 3D reconstruction slice. 4D motion, virtual-camera rendering, and film emulation remain distinct later capabilities.

This slice does not implement Gaussian splatting, a film emulator, or Photoshop-wide tool parity.
