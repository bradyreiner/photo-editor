# Complex design and long-running agent audit

Checked against the current working tree on October 6, 2026. This is a source
audit with focused transaction tests, not qualification of unrestricted model
creation. The full implementation scope remains in
`agent-full-app-execution-plan.md`.

## Findings and current resolution

### 1. Whole-job step limit addressed; durable planner recovery still pending

The original audit found eight calls total and six planning rounds in
`planDraftForScope()`. The updated planner retains eight calls per model batch,
with no total round/call limit. `AgentToolProgress` detects repeated identical
receipts and observation cycles while allowing distinct document states to
continue. Async polling is outside this progress check.

The 79-call controller fixture passed, retained real returned IDs after 64
history entries, and remained discardable. A second fixture canceled after
14 mutations; a third stopped repeated observations. These use deterministic
model output and a composition fixture. Planner resumption after actual app
relaunch and unrestricted real-model design quality remain unproven.

### 2. Native photo and first Timeline document commands connected; other adapters pending

`photo_mcp_server.cpp::composition_operation_schema()` exposes a limited set of
photo composition operations. The native API already provides:

- `CatalogAssetModel::createBlankCanvas()`;
- `EditorPreviewController::addPhotoShape()` and `addFillLayer()`;
- `addSourceLayer(file_path, name)` for another image or logo;
- layer groups, duplication, blend modes, clipping, and per-layer adjustments.

The shared `AgentAppCommandRegistry` now exposes native photo fills, shapes,
source-image placement, groups, duplication, typography, layer effects,
blend/clipping controls, per-layer adjustments and selected global controls.
Discovery and dispatch share typed schemas; layer indices are replaced by
stable IDs resolved at invocation. Mutations use the existing photo task journal
and retain license and active style/editing-freedom checks. Read-only native
inspection has a separate read-capability tool. Const file-writing functions
remain unavailable until their output owner is registered.

The first native Timeline adapter now admits synchronous document commands
through a shared task coordinator. It captures canonical Timeline JSON and
returns real created clip IDs; native clip indices are resolved from stable IDs.
A single journal owns photo and Timeline changes and preflights every enrolled
resource before reversing any of them. Review/commit also rereads every owned
document and requires its last checkpoint. Production Timeline mutations require
the Timeline ID in the admitted task targets, so ordinary photo tasks cannot
edit an unrelated open Timeline. Generic target selection/admission still needs
implementation before this adapter is reachable through a normal Timeline
agent workflow. Timeline heavy jobs, file writers and
controls relying on implicit clip selection remain unavailable.

The registry also discovers native Library, Carousel, Scene, Export, preferences
and job commands, but their mutations remain explicitly unavailable until their
own resource/cancellation/reversal adapters are implemented.
Discovery is not a claim of complete access. Blank-canvas creation and native
settings/selection/gesture/async controls still require those adapters.

Those functions are not represented as corresponding legacy composition operations.
The legacy `add_source_layer` calls `addSourceLayer(file_path_, ...)`, using the
current source photo rather than accepting another asset. Separate image/logo
placement is now connected through the native registry's `addSourceLayer`.
The planner still requires at least one scoped asset and an active style;
starting an empty poster remains a tool-access gap even though the app has
native blank-canvas functionality.

The editing phase allowlists now include the shared native command tools.
Existing Timeline tools in MCP discovery are still filtered out
of this planner. The new Timeline commands use the native registry route; they
do not establish independent Timeline task admission or access to all workspaces.

### 3. Safe tool-error correction connected; automatic repair cannot edit layout

`execute_model_mcp_tool_plan()` withholds subsequent calls after an error. The
planner now continues when the authoritative tool receipt confirms both
`recoverableError=true` and `mutationNotApplied=true`. Native registry admission
checks carry that contract; the native photo owner can also certify an unchanged
synchronous rejection after successfully journaling it. Permission failures,
uncertain completion, pending work and journal failures do not acquire this flag.
The failed receipt and withheld calls return to the model, completed earlier
operations remain applied, and repeated failed results use the existing progress
guard. A final intent immediately after an unresolved rejection cannot publish a
successful draft. Four correction/controller fixtures and native MCP rejection
checks passed in `evidence/agent-tool-correction-2026-10-06/`.

`model_repair_recipe_patch_from_validation()` explicitly sets
`allow_tool_call_plan = false`. Automatic repair has two model turns and only
produces recipe patches. It cannot issue composition operations to fix a clipped
headline, wrong stacking order, missing cutout, or misplaced CTA. The initial
builder and repair path need the same typed editing capabilities.

### 4. Whole-draft revisions now continue asynchronously; other entry points still need coverage

`AgentEditController::reviseDraft()` now queues a fresh request against the
original task instead of rolling back and rebuilding its draft. It retains the
original baseline, existing layer IDs and original target assets, forces the
current recipe as the revision base without changing the user's base-mode
setting, and awaits pending pixel work. A reviewed/interrupted task can continue
only when enrolled resources match their checkpoints and there are no pending,
prepared, accepted-but-incomplete or uncertain operations. A failed journal write
preserves the previous in-memory status. Commit is blocked during revision or
failed native finalization.

Five revision fixtures pass for continuation, pending-pixel cancellation,
cancellation before revision work, rejected tools and manual-edit conflict
preservation. They use the built-in app's in-process MCP route and deterministic
model output. Whole-task Revert restores the baseline before the first draft,
including after a successful or rejected revision. The native mixed-document
fixture also passes 27 initial edits plus two continuation edits with exact
photo recipe/pixel and Timeline reversal. Neither fixture establishes actual
model design quality or native UI acceptance.

Synchronous `runDraftForScope()` acquires ownership before tool mutation and uses
the common finalizer. Its legacy dispatcher still stops on pending native pixels;
ownership does not make every synchronous entry point asynchronous.
`keepValidatedFastDraft()` preflights conflicts, cancels owned work and finalizes
before clearing async state. Failed finalization is persisted and remains
revertable. Final catalog application checks enrolled native resources before
writing. Finding/point corrections, sample approval, alternative-direction
restoration, direct catalog writes and catalog resources outside enrolled native
documents still need ownership coverage before all entry points can claim
whole-task Revert.

### 5. Cancellation is not uniform across native tools

The new photo paint/cutout adapter tags queued jobs with task IDs, cancels their
tokens, and prevents late publication into a reverted document. Focused native
tests establish that behavior.

However, `photoCompositionCommand("select_subject")` calls `addAIMask()`, which
decodes and calls `generate_semantic_mask()` synchronously. The native Vision
implementation uses synchronous `performRequests`; this public API has no task
cancellation token. The in-process MCP client requires and executes the handler
on the workspace thread. The agent's cancellation predicate cannot interrupt
that native call. A slow segmentation request can delay the UI and Stop until
the call returns. No duration or freeze was measured in this audit.

All heavy tools need task-owned asynchronous completion and cancellation, rather
than cancellation only around the model and selected pixel jobs.

### 6. Recovery and Revert cover only part of the resource surface

`AgentTaskJournal` persists operation acceptance/completion and exact baseline
bytes. The first native adapter captures the currently open photo's asset-bound
recipe. The shared coordinator also enrolls the active Timeline on its first
owned native mutation. Its document adapter preserves exact canonical JSON,
excluding the volatile updatedAt timestamp, and guards against a closed or busy
owner. Tasks without an open scoped photo can now receive an empty journal,
but planner entry still requires a photo. Carousel, Scene, catalog, settings and
output-file owners remain pending.

The composition API also exposes save/delete/import/export of Photo Fragment
presets. Those operations change files through the preset store, while the
journal captures only the photo recipe. Recipe reversal cannot restore an
overwritten/deleted preset or remove an exported file. Each modified resource
needs its own baseline and restore operation before its command is admitted.

`recoverAgentTask()` is currently called only by the native fixture, not by app
startup or recovery UI. Planner stage results and mutation caches remain in
memory. Durable photo rollback is not yet resumable whole-job execution after
relaunch. Interrupted/uncertain operations must be reconciled without replay.

The journal also needs stricter recovery schema validation, prepared-operation
transition guards, and failure-injection coverage for writes after admission.
Current passing storage tests cover initial admission failure, not every later
checkpoint failure.

### 7. Long-task evidence still has size and resolution limits

`agent_local_model.cpp::compact_prompt_evidence()` retains the first 64 entries
of generic lists, omits long strings, and `prompt_json()` replaces packets larger
than 48,000 bytes with `evidenceOmittedForSize`. Composition history receives
additional compaction, but the generic fallback remains. A long task needs a
compact current document snapshot plus relevant recent receipts and durable
lookup of older completed steps, rather than losing its latest state.

Long composition history now uses a compact current-composition packet, a map
of created IDs, and the latest 16 receipts after 24 calls. The 79-call fixture
checks that the latest state and IDs are not evicted by generic list truncation.
Other large evidence packets still need equivalent size-aware handling.

Planning output is clamped to 1,536 tokens. The Qwen3.6 workspace configuration
uses an 8,192-token base context and 768-pixel planning images; effective context
can grow up to 24,576. These are capacity/quality constraints, not timeouts. Small
text and mask edges need appropriately cropped or higher-resolution visual
inspection. The photo planner now replaces the image and recipe evidence before
each construction follow-up, as described in finding 13. Detail crops and compact
state contracts for other resource owners remain pending.

### 8. External HTTP MCP has an additional rate-limit failure mode

The HTTP bridge accepts 120 requests per minute and replies 429 when exceeded.
The MCP client has no 429 retry/backoff policy. This is relevant to external HTTP
clients or concurrent/poll-heavy jobs; the built-in agent uses in-process MCP and
does not traverse this limiter. It is not the source of the built-in agent's
former 60-second work timeout.

### 9. Native work still has deadlines outside the photo agent

Removing model/MCP work deadlines did not remove deadlines in all underlying app
tools. The initial source inspection found:

- `job_runner.cpp` catalog import waits up to 120 seconds and then kills the
  running import process;
- `CarouselDocumentController::exportVideoSlide()` calls
  `waitForExportProcess(..., 120000)`, whose deadline branch kills the exporter;
- Timeline beat, loudness, and silence analysis use 120-second single-shot timers
  that kill a still-running analysis process;
- Timeline spatial grade export waits up to 60 seconds for decoded data and
  fails on that wait; renderer/encoder finalization also has 120-second waits.

These are actual native work cutoffs, distinct from test-runner limits and server
connection checks. They affect import, audio, and video workflows, and were not
demonstrated as the cause of the photo-thumbnail failure. Every native tool
included in the all-app agent needs a cancellation-aware wait policy and genuine
process failure detection without terminating productive work by elapsed time.

The current working tree removes these particular import/Carousel/audio/render
cutoffs and adds cancellation-aware process waiting and staged Timeline delivery.
The two new helper tests pass, and the process helper completed 121 seconds of
work in `evidence/agent-native-work-2026-10-06/unlimited-process-ctest.log`.
The production build passes in the subsequent tool-correction evidence, but
native worker cancellation integration is still unproven. This is work in progress,
not a completed all-tool cancellation claim. Other Timeline helpers still use
bounded waits for actual work: frame decoding (15 seconds), audio envelope
decoding (20 seconds), and poster extraction (8 seconds). These need individual
classification and cancellation contracts too.

### 10. Native argument types do not describe design semantics

`AgentAppCommandRegistry::describe()` exposes argument types, required fields,
availability and task contracts. Most photo arguments still use reflected Qt
types without design-specific descriptions, enums, ranges or nested schemas.
For example, `setPhotoLayerShapeFill(index, options)` becomes an arbitrary JSON
object, while the native implementation accepts a specific field list, five
fill modes, four texture types, normalized gradient centers and bounded numeric
values. `setPhotoLayerTypography(index, changes)` has the same nested-map gap.

The design APIs also mix pixel positions/dimensions with normalized selection
coordinates. A model needs the unit, origin, allowed values, optional/default
behavior and target restrictions in the command schema; reflection supplies
none of those semantics automatically. Otherwise it can produce a type-correct
call that the native tool rejects or places incorrectly.

### 11. Transform/perspective no-ops fixed; remaining setters need readback coverage

`setPhotoLayerTransform()` returns false when the requested transform is already
present. `setPhotoLayerPerspective()` also returns false for an unchanged value.
The registry interprets a mutable boolean false as failure unless a
`confirm_satisfied` adapter verifies that the requested state exists. The photo
adapter now supplies that check for opacity, enabled, locked, blend mode, text,
transform and text perspective. The production MCP native fixture verifies
repeated transforms/perspective as already satisfied, invalid IDs as unapplied,
and an invalid font face as a certified unchanged rejection.

Other admitted setters still need equivalent verified readback
that distinguishes an already-satisfied request from a locked/invalid target;
treating every false result as success would hide real failures.

### 12. Dependent commands cannot consume a newly returned ID within a batch

`execute_model_mcp_tool_plan()` sends each call's original arguments directly to
`client.callTool()`. There is no result-reference binding in this dispatch path.
An eight-call batch is useful for independent edits or known IDs, but cannot
create a layer and then reference its newly generated ID in a subsequent call
unless the planner waits for the creation receipt and makes another turn.

The model prompt now explicitly distinguishes existing IDs from newly created
targets and requires another turn after the creation receipt. Typed references
to earlier results would further improve batch efficiency. Placeholder or
predicted IDs must never be dispatched.

### 13. Fresh photo planning proof connected; other documents and detail crops pending

The photo planner now captures the live recipe before its initial model request
and each follow-up after completed tools. The open native photo supplies its
actual asset-bound recipe and task checkpoint; other scoped photos use the
catalog recipe. A request-owned background stage renders that copied recipe,
then checks that the live document still matches before dispatching the model.
Pending native pixel work must complete before this stage begins.

Every stage keeps a separate completed PNG with its asset ID, exact recipe
SHA-256, PNG SHA-256, source and dimensions. The next model request replaces both
its proof image and current recipe with this evidence. Completed stages reuse
their immutable receipts during asynchronous reentry without rerendering earlier
states or replaying mutations. Missing, corrupt or mismatched required proofs
stop planning, including when fallback was otherwise permitted; the sender
cannot substitute the original photo.

The provisional quick look is excluded from the editing base only while its
exact authored recipe is still current. Preview withdrawal now restores only
that authored state and preserves later manual changes. A manual edit during
proof rendering stops the next model turn and remains intact.

This covers the representative photo construction path. Equivalent rendered
state contracts for Timeline and other documents, targeted detail crops for
small text and mask edges, and real-model/native UI qualification remain pending.

The earlier real-photo result in
`evidence/agent-thumbnail-qwen36-35b-2026-10-05/README.md` records another limit:
the model critic incorrectly described canvas clipping and foreground ordering
despite contrary bounds, stack and rendered-pixel evidence. That is historical
test evidence, not a new-model rerun today. Geometry, expected copy and intended
subject occlusion need deterministic checks alongside visual critique.

### 14. Long model calls do not stream visible progress

The Ollama planning payload sets `stream=false`. Tool-stage events are available,
but model generation is returned only after the request finishes. This leaves
a long reasoning/planning interval without genuine incremental model output.
Removing deadlines does not resolve that feedback gap. A streaming transport
can expose activity and concise status while buffering and validating the final
structured command object before invoking any mutation.

## Current verification and boundaries

Five focused tests passed in `evidence/agent-task-journal-2026-10-06/lifecycle-ctest.log`:

- durable journal with 24 edits across two synthetic resources;
- controller task admission, review, conflict check, and discard lifecycle;
- Stop and partial-failure lifecycle;
- actual native photo composition reversal, pending-paint cancellation, rejection
  of subsequent manual-edit conflicts, and controller recreation/recovery.

The shared-registry checks in `evidence/agent-command-registry-2026-10-06/`
add 21 actual native design operations through the production MCP bridge,
including shape/fill/source/group/typography/color changes, license/style and
ownership guards, stable layer IDs, idempotent setters, changed rendered pixels
and exact recipe/pixel reversal. Four native checks and the final controller,
long-task and cancellation checks passed; the desktop build passed. These use
synthetic images and deterministic commands, not a live-model design brief.

These establish the first photo transaction adapter. A subsequent 79-call
controller fixture in `evidence/agent-long-tasks-2026-10-06/` verifies continuation
past the former limit. These fixtures do not prove all-tool recovery,
actual app relaunch recovery, native UI appearance, or production-quality poster,
thumbnail, and ad exports.

The subsequent shared-photo/Timeline coordinator evidence is in
`evidence/agent-multi-document-2026-10-06/`. Its production MCP fixture verifies
27 interleaved native changes, stable clip IDs, exact recipe/pixel/document
reversal, manual/closed-owner conflicts, native pending-paint cancellation,
Timeline save/reopen and recreated-owner recovery. Completion and scope guards
pass their final rerun, and the production build passes. Twenty-five selected
checks pass across the regression and corrected export admission fixture. These
remain native/component checks with temporary data, not app relaunch, complete
Timeline/tool coverage or unrestricted model creation.

The subsequent entry/completion evidence is in
`evidence/agent-entry-ownership-2026-10-06/`. Six focused ownership/failure checks
and 16 regression checks pass, including manual conflict preservation, exact
catalog recipe reversal, persisted fast-finalization failure and ignored late
model completion. The app and both controller targets compile/link after
refreshing Qt metadata and affected Timeline header consumers with the dev
preset's generated commands. Source and binary hashes remain unchanged after
the final checks. These use deterministic model/composition/task adapters;
they do not qualify native UI, every entry point, queued revision or real-model
design quality.

Acceptance should require unrestricted local-model creation and revision of all
three designs, more than eight successful dependent tool calls, cancellation
during each heavy job family, exact save/reopen preservation, whole-task reversal
including created/imported files, and preservation of later manual changes.

The queued revision evidence is in
`evidence/agent-revision-continuation-2026-10-06/`. The earlier failing runs are
retained: one fixture replaced a response sequence without accounting for the
fake transport's process-wide cursor; another used HTTP and exhausted its
120-request/minute limiter before revision. The corrected fixtures use captured
model-call counts and the app's in-process route. These were test-harness issues,
not evidence that the production revision path finished correctly before the
final rerun.

The fresh photo-planning evidence is in
`evidence/agent-planning-visual-state-2026-10-06/`. All 20 selected final checks
pass: required proof delivery and missing/corrupt/mismatched rejection, fresh
construction images, Stop and manual-change preservation, queued revisions,
79-call continuation/rollback, tool-error correction and native photo/Timeline
reversal. The app and affected targets build; 704 tracked inputs and seven
outputs retain their hashes after verification. Deterministic model responses,
injected snapshot fixtures and synthetic native documents do not establish
unrestricted model design quality, all-workspace ownership or native UI acceptance.
