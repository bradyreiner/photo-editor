# Live local-agent runtime smoke

Date: 2026-09-26. Status: **local model transport and image-grounded planning/proof critique passed; creative-tool qualification remains open**.

The release-debug `agent_local_model_test` ran its opt-in live Ollama path against
the installed `qwen3-vl:32b` model with a synthetic 640×512 highlight fixture.
The planner completed in 86,734 ms and the proof critique in 34,039 ms. Both
requests used `http://127.0.0.1:11434`, included the image, reported the Ollama
backend, and used no fallback. The final receipt states `localOnly=true`,
`cloudUsed=false`, `automaticCommitAllowed=false`, and
`photographerApprovalRequired=true`.

See [`ollama-runtime-smoke.json`](ollama-runtime-smoke.json) for the model
family, quantization, transport telemetry, planning summary, and critique.
This smoke proves local model availability and a bounded planning/critique
round trip. Its request is a global evening-look adjustment, not a creative
filter/text/brush brief; it has no actual MCP tool trace, final edited recipe,
native editing session, or human taste score. It counts toward none of the
plan's 18 creative agent runs or 216 visual-review cells.

A separate [composition MCP dispatch contract](creative-mcp-dispatch.md) now
verifies live `AgentMcpClient` transport for inspect and typed text creation.
That contract uses a stub editor handler and does not convert this model smoke
into a creative agent run.

The live planner now explicitly routes requested text, typography, paint,
selection, mask, layer-order, and Photo Fragment operations through supplied
composition MCP tools. Its Ollama request also tells the model to use exact
asset/mask/layer ids already supplied and keep dependent calls ordered in one
tool-call plan; the deterministic model test checks that instruction.

The opt-in local composition workflow then ran `qwen3-vl:32b` against the real
`AgentLocalModel` planner. It returned two ordered calls, `add_text` and
`copy_selection`, which `AgentMcpClient` dispatched over localhost into the
real `EditorPreviewController`. The selected pixels sit above the headline:
the rendered proof has 848 glyph pixels restored to the underlying image, and
the serialized recipe reopens to a pixel-identical render. The exact model
prompt, response audit, call arguments and compact controller reports are in
[`tool-plan.json`](composition-run-synthetic-2026-09-26/tool-plan.json); the
reproducible source, selection mask, final recipe, image, and hashes are in the
[run receipt](composition-run-synthetic-2026-09-26/run-receipt.json) and its
artifact directory.

This run used a 480×320 synthetic solid-color fixture and a rectangular mask,
and the MCP endpoint was a test adapter that called the production controller.
It demonstrates live model-selected composition calls and actual editable
controller behavior, but it does not establish photo quality, native app
interaction, production bridge-to-controller wiring, or final agent workflow
qualification. It is deliberately excluded from the required 18 creative runs
and 216 human visual-review cells.

### Production bridge run on a real portrait

The first counted creative agent run now uses the authenticated production
`PhotoAutomationBridge` localhost endpoint connected to the real
`EditorPreviewController`, with the pinned 2687×3356 Obama portrait and retained
SAM2 subject mask. `qwen3-vl:32b` selected ordered `add_text` and independent
`copy_selection` calls. The run returned a stable text-layer ID; the model then
used that ID in a follow-up MCP planning round to apply Georgia typography,
size, fill, stroke, and shadow. The subject cutout is topmost, and 1,906 fully
opaque subject pixels restore the underlying photo over text glyphs. The final
recipe parses and renders pixel-identically.

This is one successful agent-workflow run (**1/18**), not a taste approval:
the rendered treatment still needs human visual review, and the family and
finished-composition counters do not advance. It is excluded from the 216 human
review cells. The exact prompt, both model plans, typed arguments, MCP reports,
source/mask hashes, final recipe, and render hashes are in the [run receipt](composition-run-production-bridge-2026-09-26/run-receipt.json).
The [text-over-photo render](composition-run-production-bridge-2026-09-26/text-over-photo.png),
[final render](composition-run-production-bridge-2026-09-26/text-behind-subject.png),
and [editable recipe](composition-run-production-bridge-2026-09-26/final-recipe.json)
are retained alongside it.

The full-resolution independent cutout serializes to 168,351,671 bytes. Recipe
parse and pixel-identical render were checked, but this run does not qualify
practical save size, app close/reopen latency, storage pressure, or export
performance. Those remain open persistence and heavy-work gates.

### Production-bridge A1 repeat — 2026-09-26

A second independent A1 attempt completed with the exact fixed editorial-
portrait prompt and the same pinned portrait and SAM2 mask. The production MCP
bridge connected to the real `EditorPreviewController`; `qwen3-vl:32b` planned
and dispatched editable text, an independent subject cutout, and a typed
typography follow-up. The recipe reopened pixel-identically. Its 168,351,671
byte recipe produced the same pixel counts as attempt 1: 30,537 subject-
affected headline pixels and 1,906 fully opaque subject pixels restored over
the glyphs. It is a successful workflow attempt only; neither A1 render has
human visual approval. A separate [attempt journal](composition-run-production-bridge-2026-09-26/A1-attempt-02-corrected/run-status.json)
records the prompt, model plan, dispatched tool results, and completion status;
the [full receipt](composition-run-production-bridge-2026-09-26/A1-attempt-02-corrected/run-receipt.json)
and [render](composition-run-production-bridge-2026-09-26/A1-attempt-02-corrected/text-behind-subject.png)
are stored in a new directory so the first run remains immutable.

One earlier setup attempt is retained at
[`A1-preflight-failure-no-model-call`](composition-run-production-bridge-2026-09-26/A1-preflight-failure-no-model-call/run-status.json).
It used an empty model-manifest directory, so the model was never invoked and
it does not count toward the 18 runs. The runner now stores this preflight
failure explicitly and rejects attempts to reuse a non-empty output directory.

### Production-bridge A1 completion — 2026-09-26

The third A1 attempt completed the same fixed brief through the production
bridge. Its receipt includes the source revision, scoped working-tree diff
hash, and source-file hashes in addition to the model plan, MCP trace, recipe,
render, and exact reopen result. It measured 1,906 fully opaque subject pixels
restoring the image over glyphs. This completes A1's workflow repetitions at
**3/3** and brings the overall model-run count to **3/18**; A2–A6 remain unrun.
Visual review remains open, so no family or finished-composition counter moves.
See the [third A1 receipt](composition-run-production-bridge-2026-09-26/A1-attempt-03/run-receipt.json)
and its [attempt journal](composition-run-production-bridge-2026-09-26/A1-attempt-03/run-status.json).

The fixed brief table and current counter are in the
[creative tools plan](../../../creative-tools-plan.md#fixed-agent-run-briefs).
Agent workflow progress does not advance family qualification or human-review
counters.

The focused [release CTest log](production-bridge-contract-ctest.txt) also
records passing controller composition and production multi-round MCP contracts.

Reproduce after building `photo_composition_workflow_test` and starting the
local Ollama service with `qwen3-vl:32b` installed:

```sh
QT_QPA_PLATFORM=offscreen \
PHOTO_EDITOR_RUN_PRODUCTION_BRIDGE_COMPOSITION=1 \
PHOTO_EDITOR_LIVE_COMPOSITION_APP_DATA_DIR=/private/tmp/photo-editor-live-ollama-agent-app-data \
PHOTO_EDITOR_LIVE_COMPOSITION_MODEL_ID=ollama-qwen3-vl-32b \
PHOTO_EDITOR_LIVE_COMPOSITION_RECEIPT_DIR="$PWD/docs/evidence/creative-tools/agent-live-workflow/composition-run-production-bridge-2026-09-26" \
PHOTO_COMPOSITION_REVIEW_SOURCE=/private/tmp/photo-editor-creative-tools-corpus/assets/public-obama-portrait.jpg \
PHOTO_COMPOSITION_REVIEW_MASK="$PWD/docs/evidence/creative-tools/real-segmentation/sam2-obama-mask.png" \
build/release-macos/tests/photo_composition_workflow_test
```

### Production-bridge A5 glass-lettered still life — 2026-09-29

Three model-selected A5 runs now complete against the licensed 768×1024
Mirto bottle photo, using the local `qwen3-vl:32b` model and the authenticated
production `PhotoAutomationBridge` connected to `EditorPreviewController`.
Each run creates editable `HELD IN LIGHT` text, uses its returned layer ID for
a typed material-typography call, stays within the canvas bounds, and saves a
recipe that reopens pixel-identically. The retained receipts are
[attempt 7](composition-run-production-bridge-2026-09-29/A5-attempt-07/run-receipt.json),
[attempt 8](composition-run-production-bridge-2026-09-29/A5-attempt-08/run-receipt.json),
and [attempt 9](composition-run-production-bridge-2026-09-29/A5-attempt-09/run-receipt.json).
The first uses pale `frosted_glass`; its
[render](composition-run-production-bridge-2026-09-29/A5-attempt-07/attempt-final-render.png)
is restrained but low contrast. The following runs use dark-tinted
`smoked_plastic` at 0.20 transmission; see the
[attempt 8 render](composition-run-production-bridge-2026-09-29/A5-attempt-08/attempt-final-render.png)
and [attempt 9 render](composition-run-production-bridge-2026-09-29/A5-attempt-09/attempt-final-render.png).
The latter treatments keep the label unobscured and the headline readable at
fit. This is model workflow evidence, not human visual approval; the 216-cell
review gate remains open.

The first six A5 attempts are retained as nonqualifying journals. They exposed
local-model timeouts, incorrect aliases for typed typography fields, a canvas
dimension mistaken for the asset ID, and the fresh-point-text material-style
upgrade failure. A focused regression in
[`photo_typography_workflow_test`](../../../../tests/photo_typography_workflow_test.cpp)
now creates material text from a fresh point-text style through the production
composition command, verifies its version-8 fields, and checks Undo. The
controller now supplies the effect defaults and empty OpenType axes object
required when upgrading to version 8. Both this typography workflow test and
the [composition workflow test](composition-agent-routing-ctest.txt) pass on
the release build.

The A5 repetitions bring total completed creative agent runs to **12/18**:
A1, A2, A3, and A5 are each 3/3. Qualified families remain **0/9** and human
visual-review cells remain **0/216**.

### Production-bridge A6 contour typography — 2026-09-29

Three A6 runs completed against the licensed North Face of Lyskamm photograph
and its matching monochrome selection mask. Each local model run created
editable `NORTH / 04` text, attached it to the selection with the returned
text-layer ID, applied typed typography, and saved a recipe that reopens
pixel-identically. See the [attempt-03 receipt](composition-run-production-bridge-2026-09-29/A6-attempt-03/run-receipt.json),
[attempt-04 receipt](composition-run-production-bridge-2026-09-29/A6-attempt-04/run-receipt.json),
and [attempt-07 receipt](composition-run-production-bridge-2026-09-29/A6-attempt-07/run-receipt.json).
The [smoothed attempt-07 render](composition-run-production-bridge-2026-09-29/A6-attempt-07/attempt-final-render.png)
is the current visual reference.
The photograph is by Giorgio Galeotti, CC BY 4.0; see the
[pinned corpus record](../visual-review-corpus/README.md).

An initial run found that `text_path` could arrive before asynchronous preview
outline tracing completed. The controller now traces a single raster
selection directly for the detached baseline and applies a bounded
three-pixel simplification to that text path; the selection itself and vector
lasso/polygon geometry remain unchanged. The runner accepts valid single
contours without requiring a multi-contour `pathContours` array and checks the
fixture's baseline point count. The first two retained renders had conspicuous
glyph collisions. The third run uses the smoothed path and removes those
collisions, though photographic taste review remains open. Four other attempts
are retained as nonqualifying diagnostics for the async-outline race, the
single-contour check, and cached/fallback geometry mismatch.

The A6 repeats brought the total to **15/18** at that checkpoint: A1, A2, A3,
A5, and A6 were each 3/3. Qualified families were **0/9**, and human
visual-review cells were **0/216**.

### Production-bridge A4 performer-protected afterglow — 2026-10-01

Three counted A4 runs now complete the live local-model workflow against the
licensed Lit concert photograph and its performer-protection mask. The model
creates editable headline text and a separate paint layer, applies typed
typography plus optical-diffusion and dragged-shutter effects, and links the
shutter effect to the supplied mask. All three receipts verify exact target
IDs, committed paint tiles, visible image change, and pixel-identical recipe
reopen: [attempt 13](composition-run-production-bridge-2026-09-29/A4-attempt-13/run-receipt.json),
[attempt 14](composition-run-production-bridge-2026-09-29/A4-attempt-14/run-receipt.json),
and [attempt 15](composition-run-production-bridge-2026-09-29/A4-attempt-15/run-receipt.json).

Attempt 13 overstates motion and lets the headline collide with lamps. Attempts
14 and 15 are cleaner, but still await human review across the required photos,
strengths, and zoom levels. A4 is therefore **3/3 workflow runs**, not a
qualified family. The total is **18/18 agent runs**; all nine families remain
unqualified and visual review remains **0/216**. See the [source and mask
receipt](A4-source/README.md) and current [plan scorecard](../../../creative-tools-plan.md#completion-metrics).

### Bounded multi-round MCP planning

The production recipe planner supports up to three model-selected MCP tool
rounds and eight requested calls per draft. A focused host CTest uses the real
`PhotoAutomationBridge` localhost endpoint and `AgentMcpClient`, while a
scripted Ollama sequence requests `photo.live_status`, then
`photo.get_recipe_patch_schema`, then returns a semantic edit intent. The test
verifies both calls succeed once, each model follow-up resumes as a
request-owned stage, a reversible draft is applied, and rollback restores the
original state. See the retained
[`agent_model_mcp_tool_rounds_contract` CTest log](model-tool-rounds-contract-ctest.txt).

This is a transport and continuation contract, not a creative model run: the
model is scripted, both selected MCP tools are read-only, and no composition
command consumes a returned layer or mask id. It contributes nothing to the 18
creative agent runs, 216 review cells, or 9 qualified families. The full
`photo_automation_bridge_test` proceeds beyond this contract and still fails
later in its batch-export receipt check.

The updated release regression log for the composition-routing prompt and the
controller-backed workflow is [here](composition-agent-routing-ctest.txt).
Reproduce the local synthetic-fixture model run after building the test target:

```sh
QT_QPA_PLATFORM=offscreen \
PHOTO_EDITOR_RUN_LIVE_COMPOSITION_AGENT=1 \
PHOTO_EDITOR_LIVE_COMPOSITION_APP_DATA_DIR=/private/tmp/photo-editor-live-ollama-agent-app-data \
PHOTO_EDITOR_LIVE_COMPOSITION_MODEL_ID=ollama-qwen3-vl-32b \
PHOTO_EDITOR_LIVE_COMPOSITION_RECEIPT_DIR="$PWD/docs/evidence/creative-tools/agent-live-workflow/composition-run-synthetic-2026-09-26" \
build/release-macos/tests/photo_composition_workflow_test
```

Reproduction, from the repository root after the `agent_local_model_test`
target is built:

```sh
PHOTO_EDITOR_RUN_LIVE_OLLAMA_SMOKE=1 \
PHOTO_EDITOR_LIVE_OLLAMA_APP_DATA_DIR=/private/tmp/photo-editor-live-ollama-agent-app-data \
PHOTO_EDITOR_LIVE_OLLAMA_MODEL_ID=ollama-qwen3-vl-32b \
PHOTO_EDITOR_LIVE_OLLAMA_RECEIPT_PATH=docs/evidence/creative-tools/agent-live-workflow/ollama-runtime-smoke.json \
build/dev-macos-debug/tests/agent_local_model_test
```

The temporary app-data model manifest points to the already-installed local
Ollama tag; it does not change the app's installed settings.

### Typed photo-layer effects over MCP

The production `photo.edit_composition` schema supports typed creation for all
nine registered layer effects plus `update_effect`, `remove_effect`,
`reorder_effect`, `mask_effect`, and `clear_effect_mask`. Updates accept
effect-specific controls and preserve the stable effect ID. Reordering uses a
zero-based row within the target layer. Masking snapshots the chosen selection
into an effect-specific mask; clearing it leaves the effect intact. Operations
use the same undoable layer-effect stack as the editor.

The controller-backed [composition workflow CTest log](mcp-effect-stack-ctest-2026-09-29.log)
verifies update, reorder, mask, clear-mask, remove, and Undo restoration through
AgentMcpClient and the real editor controller. The test passed three fresh
process repetitions; the [repeat log](mcp-effect-stack-ctest-repeat-2026-09-29.log)
retains them. It also checks that inspect exposes typed stable effect IDs and
that the final stack retains all nine uniquely identified effect types. The
dedicated [MCP schema/validation log](mcp-effect-stack-schema-ctest-2026-09-29.log)
passes with `composition_tool_contract=passed`.
Earlier retained [creation coverage](composition-add-effect-controller-ctest.log)
and [tool schema coverage](composition-add-effect-mcp-contract.log) verify
effect creation and malformed-value rejection.

This closes composition-tool operation parity for the effect stack. It does
not qualify visual effect quality, GPU parity, or the nine feature families;
human review remains 0/216 and qualified families remain 0/9. A2 also records
live model-driven Imperfect Print creation on an editable photo layer.

### Agent font-axis and paint-layer discovery

The production `photo.inspect_composition` tool accepts optional
`values: {family, face}` and returns the selected face's real supported
OpenType `fontAxes` tags and ranges. `photo.edit_composition` also exposes
`add_pixel_layer {}`; it creates a transparent, editable paint layer through
the normal undoable controller path and returns `createdLayerId`. Together,
these allow an agent to choose a real width axis and then create/target a
separate layer for a drypoint annotation instead of guessing an axis or
painting destructively into the source photograph. Focused controller and MCP
tests cover axis discovery, invalid inspection fields, layer ID reporting,
and tool forwarding. The retained [controller CTest log](font-axis-paint-layer-controller-ctest.log)
and [production MCP schema/dispatch log](font-axis-paint-layer-mcp-contract.log)
capture the focused verification. This is A2 workflow groundwork; the completed
model-driven A2 runs are recorded below.

### Production-bridge A2 print-cover qualification runs

Three independent repetitions of the fixed A2 brief now pass through the local
Qwen planner, production `PhotoAutomationBridge`, MCP client, and real editor
controller on the checksum-pinned architecture photograph. The model created
separate linked source, editable text, and transparent paint layers, inspected
Skia's installed `wdth` axis, styled the title with that non-default axis,
added seeded Imperfect Print with subtle opposing plate offsets and paper/ink
controls, and drew a seeded three-point Dry Ink annotation. Each run retains
the complete model plans, six MCP request/results, recipe, render, and exact
recipe-reopen check.

The successful receipts are [repeat 1](composition-run-production-bridge-2026-09-26/A2-two-plate-annotation-attempt-01c/run-receipt.json),
[repeat 2](composition-run-production-bridge-2026-09-26/A2-three-point-stroke-attempt-02b/run-receipt.json),
and [repeat 3](composition-run-production-bridge-2026-09-26/A2-three-point-stroke-attempt-03/run-receipt.json).
Repeat 1's [final render](composition-run-production-bridge-2026-09-26/A2-two-plate-annotation-attempt-01c/attempt-final-render.png)
and [editable recipe](composition-run-production-bridge-2026-09-26/A2-two-plate-annotation-attempt-01c/final-recipe.json)
are retained with its [status journal](composition-run-production-bridge-2026-09-26/A2-two-plate-annotation-attempt-01c/run-status.json).
Its source photograph SHA-256 is
`43a75500ddba0d7fc0fca3f3221cb9323caf84112b3ccbdfbdfe1bee6b69a76a`; the
local model was `qwen3-vl:32b` and no cloud endpoint was used. Repeat 1 changed
9,968,914 render pixels overall, with 2,356 pixels attributed to the drypoint
layer; its recipe reopened pixel-identically. Repeat 3 records 7,701 changed
pixels from its annotation and the same overall print-render change count.

Several non-counting attempts remain preserved as error-state evidence. They
identified normalized fractional text coordinates where source-pixel integers
were required, an under-specified model tool-call array that permitted
incomplete call entries, and a one-point paint result that was not an adequate
annotation stroke. The runner now supplies canvas dimensions, requires each
planned call to be a named object with arguments, and requires at least three
brush points. The first successful render was visually inspected and retained
the building's identity with legible lower-facade type and restrained plate
drift; formal human review remains 0/216.

The A2 repeats complete that brief's 3/3 model-workflow gate and bring the
overall run count to **6/18** with A1 also at 3/3. Qualified families remain
**0/9** pending visual, native, persistence, backend, and other shared gates.
Finished compositions remain **0/6**.

Reproduce an A2 run after building the composition workflow test and preparing
the local `ollama-qwen3-vl-32b` model manifest with tool-call planning enabled:

```sh
QT_QPA_PLATFORM=offscreen \
PHOTO_EDITOR_RUN_PRODUCTION_BRIDGE_COMPOSITION=1 \
PHOTO_EDITOR_LIVE_COMPOSITION_APP_DATA_DIR=/private/tmp/photo-editor-live-a2-agent-app-data \
PHOTO_EDITOR_LIVE_COMPOSITION_MODEL_ID=ollama-qwen3-vl-32b \
PHOTO_EDITOR_LIVE_COMPOSITION_BRIEF_ID=A2 \
PHOTO_EDITOR_LIVE_COMPOSITION_ATTEMPT=1 \
PHOTO_EDITOR_LIVE_COMPOSITION_RECEIPT_DIR="$PWD/docs/evidence/creative-tools/agent-live-workflow/composition-run-production-bridge-2026-09-26/A2-local-reproduction" \
PHOTO_COMPOSITION_REVIEW_SOURCE=/private/tmp/photo-editor-creative-tools-corpus/assets/architecture-academic-building.jpg \
build/release-macos/tests/photo_composition_workflow_test
```
