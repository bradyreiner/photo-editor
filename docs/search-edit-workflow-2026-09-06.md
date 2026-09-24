# Search, confirm, edit, review

## Scope

One instruction can now launch a local visual search and hand an explicitly confirmed subset to the existing reversible agent draft pipeline. Entry: Library → AI Search → Find & edit. This is not a claim of general Photoshop parity, automatic full-library indexing, or universally reliable segmentation.

The local model separates the visual query from the edit instructions. The popup displays the interpreted query, matching thumbnails and evidence, and editable instructions. Checked targets are bound to a single-use result-generation token. Confirmation rejects unknown/duplicate ids and originals or recipes changed since the search snapshot. Source checks use canonical path, size, modification/birth timestamps and canonical recipe, not a full source-file hash. The snapshot is the current filtered library view; scanning continues explicitly in batches of 32. This workflow uses direct image inspection to avoid losing fine details through saved captions.

`confirmed-assets` resolves explicit catalog ids, independently of focus and filters, throughout draft planning and review. A missing member fails the set rather than substituting a different photo. No selection rewrite, catalog authority change, original-file write, or automatic commit is part of search confirmation. Existing plan-risk/segmentation approvals still apply when required.

## Agent interface

- `photo.search_edit_start`: draft capability; instruction only, configured selected model, asynchronous search, local confirmation required.
- `photo.search_edit_status`: read capability; plan, progress and candidate ids/evidence, without paths or approval token.
- `photo.search_edit_cancel`: draft capability; cancels unconfirmed search, not an existing draft.

There is deliberately no MCP target-confirmation tool. Existing unrelated draft/commit tools retain their own capability policies.

## Live issue found and fixed

The first JPG run searched and dispatched correctly, but its vision planner hit its 30-second request deadline. The existing automatic fallback applied a global portrait preset to a request to warm only the background and leave people unchanged. The optimistic fast-preview path also allowed this kind of global substitution.

Confirmed workflows now prohibit deterministic planner substitution. Spatial/protection requests also block the old fallback and speculative global preview. Successful model responses requesting protected local treatment cannot acquire global changes merely because their local action is missing. General whole-image looks retain their existing behavior. Timeouts now stop this path instead of being presented as a successful protected edit. Local-model timeout configuration is still respected; the live proof uses an isolated 180-second configuration for the installed 32B vision model.

Simple, complete commands also have an explicit `semantic-command-v1` path: warm/cool/brighten/darken/desaturate the whole background/subject/sky, optionally protecting the rest or the people. This is a bounded command grammar, not keyword matching that drops additional edits. Contradictory subject protection and compound instructions are not accepted by this path. Initial local deltas are temperature ±12, exposure ±0.25 or saturation −12, and remain editable. It generates no synthetic visual findings and claims no language-model invocation. The normal pipeline must produce a real semantic mask; confirmed edits and these commands reject elliptical or evidence-guided substitutes for an unavailable semantic selection.

## Verification status

The connected JPG/RAW workflow has now passed the bounded live checks recorded below. This is not completion of the full app goal or autonomous model verification. The initial failed live attempt is retained under `/private/tmp/photo-editor-search-edit-SJmVJX/jpg` and must not be counted as a masked-edit success.

The 32B model (`jpg-v3`) and installed Qwen 3.6 27B model (`jpg-qwen36`) both subsequently timed out during the general edit-plan stage at the isolated 180-second deadline. Both stopped without a draft or global fallback. The search/confirmation thumbnails were visibly verified. These failures motivated the bounded semantic-command path; they remain evidence that complex free-form local planning is not yet a reliable or realtime completion path on this setup.

Six focused tests (visual search, agent walkthrough contract, search/edit scope/approval, search tool capabilities, local planner and canonical recipe adapter) passed together after the protected-scope fixes. The whole automation bridge suite still stops on its HTTP GET authorization assertion; baseline status was not independently established. The focused search-tool capability test passes. Its Qt file-permission assertion was corrected to check owner access and absence of group/other access, rather than treating Qt's effective-user permission flags as a leak.

The actual-QML proof harness now instantiates the real agent and loads filesystem `Main.qml`; it is not a packaged-AOT app launch. It uses temporary catalog/app-data state, invokes the visible search and confirmation controls, records the real draft report and screenshots, requires a generated warm-background mask, and verifies recipe restoration after rollback. It does not commit edits or modify the supplied originals.

## Additional live fixes

- The actual-app harness had inherited a segmentation stub. Its macOS target now explicitly links the real Vision implementation. A native probe verified successful subject/background generation on the portrait; the background center alpha was 1/255. Whole-background commands no longer require the image center to belong to the background. Point-guided selections keep their center check.
- The canonical recipe adapter rejected `background` masks. It now represents their already-inverted raster as a generic semantic object mask without a second inversion. An adapter regression checks source kind and payload binding.
- Binary mask payloads were being serialized into AI proof-review prompts, producing a 3.78-million-token request in a failed run. Prompt evidence now omits binary payloads, bounds recursive/list/string evidence and caps each JSON section. Missing after-render proof cannot silently substitute the original. A model regression covers large payloads and missing proof. A failed model review is explicitly marked as requiring human review.
- Automatic quality repairs reacted to pre-existing clipping/cast warnings by adding global exposure and white-balance changes to a background-only edit. Protected local requests now stop at rendered review with warnings instead of entering those automatic repairs. A controller regression checks unchanged global parameters. This preserves scope; it does not declare the warnings resolved.
- The generic review Fix action also refuses those protected-local repairs without changing the draft or replacing its review report. The user can explicitly revise the local direction, adjust its mask, keep after review, or revert.
- Search and draft use the same configured local vision model. The popup shows it, and confirmation fails if that model is no longer available.

The corrected JPG proof (`jpg-protected`) has no global tone/color recipe diff. At 1600×1186, an independent rectangular cheek sample `(x=.30,y=.32,w=.10,h=.20)` contains 37,920 exactly unchanged pixels. The background sample `(.65,.15,.20,.60)` has mean absolute RGB change 0.004191 and mean red-minus-blue increase 0.012177 (channels normalized to 0–1). These are limited-region, single-fixture checks, not evidence that every subject edge or camera is protected.

That run reached a visible review marked `0 verified · 1 needs review`, with one mask and no automatic revisions. Model review timed out at 180 seconds despite a bounded 16,960-character prompt and the actual after-proof image. Undo restored the exact agent-captured original recipe (matching SHA-1 `86bed666cb00098b9e027d96fb7297e9f9094bb3` including the command-output newline). The harness initially compared it against an empty pre-initialization placeholder and failed; fixtures now start with an explicit canonical recipe and thumbnails tagged with that same recipe. The first RAW attempt exposed the stale fixture-thumbnail tag and stopped before editing. Final runs use the engine's normal 30-second model deadline; timeouts remain human-review results, not successful AI verification.

`raw-final` then passed the workflow/rollback test but failed the independent protected-face check (mean absolute RGB change 0.010904, maximum channel change 0.129412). The segmentation source was decoded camera-linear data passed directly to Vision as an ordinary display image. Selection preparation now uses the canonical color pipeline with camera calibration and baseline tone/color, omitting crop/geometric transforms and local layers to retain source-coordinate mask alignment. This fix requires its own live recheck; the earlier functional RAW pass is not a fidelity pass. The pixel probe now supports explicit `--require-unchanged` and `--require-warmer` exit-status gates so this distinction is executable.

## September 7: isolated RAW backend mismatch

The selection-input correction did not resolve the region failure. A model-free
`agent_edit_controller_test --raw-locality-probe SOURCE OUTPUT_DIR 0` reproduced
it with an entirely transparent mask: maximum channel change 0.145098 and mean
absolute RGB change 0.012461 over 1,897,600 pixels. This probe constructs isolated
recipes and calls the actual proof renderer; it does not supply opaque raster
bytes through the agent patch API or modify originals.

The migrated empty-recipe fixture had selected `legacy-oklch-v1`, unlike the
earlier JPG fixture's `aces2-ocio-v1`. The legacy no-mask render used Metal's
sampled color LUT; adding a mask switched to exact CPU rendering. An explicit
ACES 2 comparison (`... 0 aces2`) passed with exactly zero difference at every
stage and output pixel. The actual-QML harness now explicitly accepts
`--search-edit-renderer aces2|legacy`, defaulting to current ACES 2, instead of
implicitly choosing a renderer through empty-recipe migration.

Camera-characterized RAW now rejects the unverified compatibility LUT route in
both desktop admission and the engine's Metal compatibility policy. The legacy
RAW zero-mask recheck (`raw-legacy-exact`) passes with zero difference over all
1,897,600 pixels. `color_pipeline_test` passes with the new policy regression.
This keeps existing legacy photos correct across mask edits; it does **not**
complete accelerated RAW performance. A parity-verified analytic GPU camera
prefix remains part of the full active goal.

`PHOTO_EDITOR_TRACE_RENDER_STAGES=1` enables scoped stderr diagnostics for the
backend route and a fixed source-coordinate pixel after canonical stages. It is
off by default and provides a localization aid, not full-frame color proof.

## Final connected-workflow proof, September 7

Both `raw-aces2-verified` and `jpg-aces2-verified`, under
`/private/tmp/photo-editor-search-edit-SJmVJX`, exited successfully from the
actual-QML harness with the current ACES 2 renderer and installed
`qwen3-vl:32b`. Each receipt records `generatedBackgroundMask=true` and
`rollbackRestored=true`. The source was respectively the original Nexus 6 DNG
and its JPG reference, not the search thumbnail substituted as an original.

| Independent preview measurement | RAW | JPG |
| --- | --- | --- |
| Protected cheek pixels checked | 37,920 | 37,920 |
| Maximum absolute channel change there | 0 | 0 |
| Background mean absolute RGB change | 0.004035 | 0.004191 |
| Background mean red-minus-blue increase | 0.011380 | 0.012177 |
| Entire baseline recipe restored on undo | yes | yes |

Channels are normalized 0–1; proofs are 1600×1186. The normalized cheek ROI is
`0.30 0.32 0.10 0.20`; the background ROI is `0.65 0.15 0.20 0.60`.
The independent pixel probe passed `--require-unchanged` and `--require-warmer`
respectively. These checks do not certify all subject boundaries, all camera
formats, full-resolution export, or other color spaces. The separate transparent
mask check covers the entire legacy RAW preview; it is not a replacement for a
large real-camera/color-space corpus.

Rendered review screenshots show `0 verified · 1 needs review`. There was one
semantic mask, no ellipse fallback, and no automatic global repair. Local-model
proof review timed out at the normal deadline and remains explicitly incomplete.
Original files and the user's actual catalog were not modified.

Reproduce the connected proof with `actual_app_workflow_benchmark_driver
--source SOURCE --search-edit-proof-dir NEW_OUTPUT_DIR --search-edit-renderer
aces2 --visual-search-model qwen3-vl:32b --visual-search-query 'Find portraits of
people, warm only the entire background, and keep the people unchanged.'`.
For RAW, also supply `--visual-search-preview DECODED_PREVIEW` for search.
Use a fresh output directory; the test isolates app data and does not commit.

Remaining gates: model-review reliability/latency, large draft/report payloads,
responsive loading and editing under inference, full-resolution output parity,
representative multi-photo/camera and boundary accuracy, plus all unrelated rows
of `active-app-completion-goal.md`. No SOTA or full-goal completion is claimed.

Final verification: desktop app and focused test targets build successfully;
`color_pipeline_test`, `edit_recipe_v13_adapter_test`, `agent_local_model_test`,
`visual_search_controller_test`, `agent_walkthrough_contract_test`,
`search_edit_workflow_contract`, and `search_edit_tool_contract` pass together
(7/7). The new search/edit popup passes `qmllint`, and `git diff --check` passes.
The broader automation-bridge HTTP authorization assertion noted above is not
covered by the focused search-tool pass and remains a separate unresolved gate.
