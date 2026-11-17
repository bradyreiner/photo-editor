# Editor authoring workflow repair plan

Date: 2026-10-28. Status: **implementation in progress; focused controller/component checks recorded, full native/agent/human acceptance still open**. Current results and remaining gaps are recorded in the [execution report](editor-authoring-repair-2026-10-28.md).

This plan covers selection/mask creation and refinement, deleting masked layers,
inpainting, text creation/editing, Boards, return to the source photo, original
preview/reset, viewer controls, and agent execution. It supplements
[Capability verification](capability-verification-plan.md) and extends the existing
[editor regression contract](editor-regression-protection.md). Older completed
checkboxes and descriptive proof strings do not close the cases below.

The requested result is a photographer being able to predict the next action,
see what it affects, undo it, and recover the same document after reopening.
Controls, ownership, rendering, persistence and agent commands must agree.

## Audit boundary and evidence

Three read-only agent investigations covered masks/inpaint, text/viewer chrome,
and Boards/RAW. The coordinator inspected the shared undo and regression paths
and ran 11 existing offscreen binaries. No production behavior was changed by
this planning pass, no user's editor was closed, and no live catalog was used.

Existing-executable baseline: **9 passed, 2 failed**. The two failures were:

- `photo_text_content_editor_test`: `Newlines must persist as plain text`.
- `photo_text_overlay_test`: QML could not load `PhotoTextOverlay` because its
  registered type did not expose `sourceText`.

These binaries were not rebuilt for this audit. Current source **does** declare
`sourceText` in `photo_text_document_style.h`, and production/test CMake lists
that header. Fresh autogen/build and production QML-load checks must distinguish
an interface/build mismatch from a current product defect. The passing tests
also do not qualify the dirty concurrent source or the native interaction.

Logs, executable hashes and source fingerprints are in
`build/editor-workflow-audit-2026-10-28/baseline-tests.log` and
`baseline-provenance.json`. Source lines below identify the audited version;
function names and recorded hashes are authoritative if concurrent edits move
the lines.

### Findings that determine repair order

F01–F11 below record the pre-repair audit, not the current implementation status. Use the linked execution report for repairs and evidence; these historical source references retain the audit boundary above.

| ID | Finding and confidence | Current evidence | Why existing proof misses it |
| --- | --- | --- | --- |
| F01 | **Confirmed:** deleting a masked layer changes the layer stack without deleting its owned mask set. Nonneutral mask-local adjustments can still render; later mask editing can recreate a scaffold. Exact reported residual pixels still need a fresh reproduction. | `editor_preview_controller.cpp:38029` `deletePhotoLayer`, `:47186` `applyPhotoLayerStackChange`, `:47018` `applyMaskStackChange`; `desktop_pipeline_state_compiler.cpp:384` independently compiles masks. | `editor_preview_layer_mask_history_test.cpp:111` checks layer count/undo, not orphan masks, retained pixels, resurrection or reopen. |
| F02 | **Confirmed:** Add/Subtract controls described as affecting the next component immediately modify the existing component's operation. | `StudioWorkspace.qml:8609` tooltip, `:8631` `setMaskCreationOperation`; controller `:26889` active-mask operation setter. Brush paint/erase routing repeats this at QML `:9449`. | Tests must check that choosing a mode alone leaves coverage, recipe and history unchanged, then perform the actual stroke. |
| F03 | **Confirmed:** retouch Apply/Cancel owns all pending masks of the same mode, rather than an explicit individual repair. | `editor_preview_controller.cpp:7648` pending-mask combination and `:40476` cancellation. | One-region tests cannot detect two pending repairs being merged or discarded together. |
| F04 | **Reported, not localized:** inpainting fails and brush cannot add to a mask through the visible workflow. Same-set brush support and asynchronous inference already exist. | `startSelectionBrush`, `startSelectionEdgeBrush`, `applyActiveRetouch` and stale-result validation in `editor_preview_controller.cpp`. | Native selection tests use injected fixtures/fake segmentation; the retouch Cancel test uses a fake controller; the quality test does not establish real LaMa removal. |
| F05 | **Confirmed:** final-Board removal is blocked in both UI and controller; permitted removal always activates another Board. | `Main.qml:1786`, `:1692`; `carousel_document_controller.cpp:21143` `removeEditBoard`. | Existing tests deliberately require a surviving Board; they cannot qualify delete-all → source-return. |
| F06 | **Confirmed:** main Undo routing recognizes selected Board action labels but omits ordinary add/delete. It can fall through to photo history. | `Main.qml:1722` `canUndoBoardMove`, `:1742` `canUndoCreativeBoardAction`, `:1763` `performPhotoUndo`, `:2354` Undo enabled state. | Controller tests call `carouselDocument.undo()` directly. Native Board deletion tests do not immediately press the main Cmd+Z command. |
| F07 | **Confirmed:** Base means the current document's adjustment target; Original is comparison. Neither is a durable return-to-source command. Creation preserves the initial source/recipe, but lacks a separate origin/return contract. | `Main.qml:1606` Board activation, `:1643` creation; `StudioWorkspace.qml:8349` Base target, `:6664` Original hold. | Reopening Boards is different from restoring the exact originating catalog RAW and its working recipe. This is not evidence that source files were lost. |
| F08 | **Confirmed:** the command described as resetting all edits excludes RAW and preset groups. | `EditorPreviewController::resetAdjustments`; `edit_recipe_group_normalization.cpp:5` lists RAW/presets as separate groups. | Existing reset tests do not seed the complete RAW/profile/preset/text/retouch state. |
| F09 | **Confirmed:** canvas and Inspector text differ on Escape and rejected edits; text creation exposes duplicate Done/Cancel groups and switches to Layers after commit. | `PhotoTextContentEditor.qml:29–59,125`; `PhotoTextOverlay.qml:140–181,411,442`; `StudioWorkspace.qml:16857–16904,18289`. | Component correctness and successful creation do not prove consistent focus, cancellation, failure recovery or continuation. |
| F10 | **Confirmed:** text does not participate in the reduced-chrome rules used by other direct-manipulation tools; its opaque editing frame can cover the photograph. | `StudioWorkspace.qml:16344` HUD visibility and viewer rails `:17393` onward; `PhotoTextOverlay.qml:345`. | Need visible bounds, hit routing and actual control enumeration, not object existence. |
| F11 | **Confirmed:** some UX signatures literally declare success-like properties such as `duplicate-context:false`. | `StudioWorkspace.qml:7965–8014`. | A fixed string cannot establish that only one command group is visible or that it targets the correct object. |

## Product behavior to implement

### One explicit editing context

Every command and pending job must resolve the same context:

`document ID → source asset/Board image ID → layer ID → selection-set/component or repair ID → session/generation`.

Stable IDs, ownership and transaction receipts belong in the native document
workflow layer. QML displays state and routes actions. Array indices, display
names, whichever photo happens to be selected, and undo-label strings must not
determine the resource that gets edited or undone.

The viewer identifies its current Photo/Canvas/Board and selected target in one
compact location. Inspecting properties or selecting a layer/mask thumbnail in
the current document must not create a layer, mutate a mask or add history.
Photo/Board filmstrip clicks are explicit navigation: resolve the current session
before changing documents; never reinterpret navigation as a creation gesture.

### Return, compare and reset have separate meanings

| Action | Required result |
| --- | --- |
| **Return to photo** | Retain/save the Boards document and reopen its captured originating asset. Resume the pre-Board working recipe when its source revision is unchanged; preserve independently newer source edits when it has changed. This is navigation, not reset or a write-back of Board edits. |
| **Delete one of several Boards** | Remove the chosen Board atomically and select the nearest survivor. Keep unrelated/shared-image content. Main Undo restores the exact deleted Board. |
| **Delete the final Board** | Tombstone/archive the document and return to its verified photo origin in one document-owned transaction. Undo restores the same document and Board IDs, content, selection and context, even after the return to Photo. Do not activate Board index `-1`. |
| **Final deletion without a photo origin** | Return a standalone Canvas/Boards document to its collection. Never open an unrelated catalog RAW as a fallback. |
| **Original preview** | Temporarily compare with the explicitly named source baseline. Release, focus loss and tool exit restore the current edited view without changing recipe/history. Board-local comparison must not be labelled as returning to the camera RAW. |
| **Revert photo to original** | Restore the source's immutable **developed RAW baseline**: remove user RAW overrides, presets, crop, masks, layers, paint, text, retouch and other pixel edits in one undoable transaction. Preserve necessary camera characterization and display/output color-management identity. A generic zeroed `EditState{}` or sensor bytes are not that baseline. |
| **Reset section / layer / Board image** | Reset only the named scope. The label and affected target must make the smaller scope clear. |

Persist a versioned origin record containing the source asset ID, canonical
source identity/fingerprint, source recipe revision, pre-Board recipe snapshot
and return context. Preserve the original bytes and catalog identity. Returning
must never write an older captured recipe over newer source work: load the
current authoritative source state and retain the captured snapshot for explicit
review/restoration. A deliberate restoration is a separately named undoable
operation. Resolve legacy documents conservatively;
ambiguous provenance needs a focused source-resolution action rather than a
filename/selected-index guess. Save/load failure leaves the current viewer and
document intact with retry available. Same-session Undo/Redo must cross Board
structure and photo-context transitions in actual user action order. Save/relaunch
must preserve the chosen archived or restored result. Do not assume a new global
undo stack survives process restart; retain and test any existing durable-history
guarantees rather than claiming new ones without implementation.

### Selection and mask sessions

- A selection is one named set with editable components and a clear owner.
  Reopening it changes no pixels or history. Brush refinement appends to that
  selected set and preserves its owner; it does not silently create another
  unrelated selection/layer.
- New/Add/Subtract/Intersect/Replace are **draft settings for the next gesture**.
  Editing an existing component's operation is a separate explicit property.
  Merely choosing a mode never changes the current alpha.
- Creation/refinement has a staged session snapshot: target, recipe, alpha,
  display state and history cursor. Each completed stroke or parameter gesture
  has local Undo/Redo. Undo during a live stroke first aborts that gesture; it
  cannot cross the session boundary into unrelated work.
- Done settles pending work, validates the owner and publishes one global
  `Create selection` / `Edit selection` transaction. An untouched session adds
  none. Cancel and Escape invalidate its workers and restore the complete
  session baseline without adding global history. After Done, one global Undo
  restores that baseline, including owned layer creation where applicable.
- Inspector and View inspection remain usable. A dirty session must resolve
  before a different authoring tool, photo, Board or destructive command takes
  ownership. Reuse the existing visible Done/Cancel surface to finish or discard;
  do not silently discard, open another modal window, or let a late job land on
  the new target. Untouched sessions can exit immediately.
- Delete Layer removes that layer's exclusive mask sources, legacy mask-local
  effects and dependent work atomically. Shared geometry still used by another
  owner remains intact. Delete Component, Delete Selection and Delete Layer
  have different scopes and appear in the relevant More menu.

**Legacy caution:** mask-local adjustments currently render before photo layers.
Moving values between them can change nonlinear ordering and pixels. Version
ownership independently of rendering; keep old recipes and compatibility
rendering until conversion is pixel-equivalent. Fix exclusive-owner deletion
without requiring a global render-order migration. Duplicate names and ambiguous
references must not cause broad mask deletion.

### Inpainting / Remove / Repair

Each repair owns its region, mask, source revision, candidate, accepted patch,
job ID and cancellation token. Define these visible states:

`mark region → ready → generating → review → accepted`, with retryable failure
and cancellation returning to the last valid state.

Apply, Retry, regenerate and Discard affect that repair ID only. Cancelling
generation preserves an already accepted patch; Discard repair removes the
current repair draft. Neither can affect another same-mode repair. A stale
result cannot publish after source/target deletion, undo, reset or asset switch.

Fast previews are draft candidates, never implicitly final accepted patches.
Apply finalizes the chosen repair at the required output quality before reporting
acceptance; while finalization runs, retain the previous valid result and show
that pending state. A finalization failure remains retryable and cannot be
reported as success. Export preflight must finish any required finalization or
block with the specific repair/retry action; it must never silently export a
preview-quality patch as a final repair. Imported legacy preview patches follow
that same rule. Finalization updates the same repair ID and transaction rather
than creating a duplicate repair or undo item.

The normal flow shows region, the main action and one result/retry state. Model
selection and installation live in Settings; a missing/invalid model has a clear
reason and route there. Model readiness, an actual successful inference, a
materialized patch, visible changed pixels and saved/exported pixels must all be
checked. LaMa removal must not be presented as text-driven generative fill.

### Text authoring

- Text tool → click for point text or drag a paragraph box → type directly on
  the canvas. One draft, one caret and immediate keyboard focus; no preliminary
  separate text-copy form is required.
- Return inserts a newline. Cmd/Ctrl+Return and Done commit. Escape/Cancel
  restore the current session baseline, including pending style changes.
- Canvas and optional Inspector text entry attach to the **same** session,
  stable document/layer IDs, selected range and undo owner. They do not maintain
  independent debouncers with contradictory cancellation behavior.
- Typography controls, selection-scope controls, Inspector copy entry and their
  popups belong to the same text session. Using them preserves the selected
  range, draft, target and undo owner. Clicking an unrelated canvas area, or
  explicitly choosing another tool, commits valid text before that transition.
  Document/Board changes commit against the original IDs before switching. A
  rejected commit keeps the draft and target visible, blocks the dependent
  transition and offers retry/cancel.
- Local Undo/Redo while editing is bounded by the text session; exhausting it
  cannot undo unrelated document work. Style changes and copy share the session
  history. Done publishes one global undo operation for a changed session and
  none for an unchanged session; Escape/Cancel leaves global history unchanged.
- Commit keeps the text selected in Text context unless the user explicitly
  requested another tool/document. It must not jump into Layers automatically.
  Double-click edits copy; a single
  click selects the object. Drag/resize/formatting remain immediately available.
- Untouched/empty new drafts create no layer on cancel; Done on an empty new
  draft dismisses it without creating an invisible object. Deliberately empty
  existing text remains valid.
- The Inspector clearly distinguishes **Selected text** from **Whole layer**.
  Range styling changes only the intended characters; geometry/paragraph/path
  settings have an explicit layer scope. Preserve IME, graphemes, RTL, font-face
  warnings and mixed styles.
- Render the live text over the photograph with a restrained caret/selection
  outline. Remove the opaque editing box. Qualify path/material preview parity
  rather than silently showing a different draft appearance.

## UI ownership: fewer competing surfaces

This is the design specification for the repair, not a completed redesign.

| Surface | Owns | Moves elsewhere |
| --- | --- | --- |
| Document/view header | Compact Photo/Canvas/Board identity, source breadcrumb/return action, View menu; stable Fit/100%/zoom and correctly scoped comparison. | New Canvas/Add Boards are document actions; scopes, diagnostics, layouts and secondary view choices go under View. |
| Left tool rail | Select/Move, Selection, Brush/Retouch, Text and other creation tools; current tool is unambiguous. | Avoid alternate tool launchers scattered over the viewer. |
| One tool command surface | Current target, high-frequency tool settings, **one** Done/Cancel or Apply/Retry group appropriate to current state. Use a consistent reserved viewer-safe position, with no extra top rails. | Do not repeat the action group in the Inspector and canvas. No permanent Cancel when there is no cancellable draft. |
| Right Inspector | Current target properties. Mask mode: create/edit/refine context and owner. Text mode: family/face, size, fill, alignment and range scope. | Advanced typography, material/path/geometry, detailed mask composition and diagnostics use named disclosures. |
| Layers | Structure, selected owner and child mask components; edit/visibility/lock plus contextual More. | No second independent selection-editor panel. |
| Viewer overlay | Actual geometry, caret, brush footprint, selected mask and direct manipulation handles. | No opaque editing windows over artwork or menus over the active caret/handles. |
| Image-window status | Existing bottom-right spinner, plus actionable failure when needed. | No loading row that shifts controls or duplicate progress chrome. |

Measure actual visible/enabled controls, bounds, focus owner and target IDs.
Replace fixed “no duplicates” signatures as proof. Tests must establish one
active primary command group, no conflicting shortcut owner, no control/caret/
handle/spinner overlap, readable labels and reachable advanced features.

Keyboard routing follows focus and the session: Delete in a text field edits
text; it cannot delete a layer or Board. An open popup/menu or active IME
composition consumes Escape first, preserving the authoring draft. With no such
focused transient/composition, Escape on the authoring surface cancels its
session; it must not leak into workspace navigation. Cmd/Ctrl+Z goes to the active text/mask session,
then to the typed document transaction owner, never a hardcoded action-label
whitelist or an unrelated hidden workspace.

## Implementation packages and agent boundaries

| Package | Priority / owner | Work | Exit gate |
| --- | --- | --- | --- |
| R0 — Fresh reproductions and interaction contract | P0 / integration + QA agent | Rebuild the two failing text targets with fresh autogen. Capture the exact user journeys and before/after state. Record each reported issue as confirmed, unlocalized, fixed-awaiting-proof or verified. Replace completion claims with linked case evidence as work lands. | Fresh-source red reproductions or an explicit unresolved branch for every report; proposed interaction/state contract reviewed. No fake model result counted as real inference. |
| R1 — Document identity, origin, original baseline and Undo | P0 / document workflow agent | Stable context/owner API, durable source return, typed chronological undo dispatch, complete original baseline, final-Board transaction and failure rollback. | B01–B06 and O01–O03 through the main commands, including Cmd+Z after return, save/reopen and unchanged original bytes. |
| R2 — Mask ownership and draft operations | P0 / selection agent | Atomic exclusive-owner deletion, no orphan effects/resurrection, draft Add/Subtract isolation, same-owner brush append. Add staged session/local undo on the agreed context API. | S01–S10, including legacy/shared/locked cases, evaluated alpha and actual rendered pixels. |
| R3 — Owned repair lifecycle | P0 / selection + runtime agent | Individual repair IDs, readiness/job/failure visibility, correct inference/apply/cancel/finalization, no cross-repair or late-result publication. | I01–I06 using the packaged production runtime and reviewed real photographs. Missing-model and failed-inference paths also pass. |
| R4 — Shared text session and direct creation | P0 behavior, P1 layout / text agent | One text session, direct click/drag entry, consistent Escape/newline/failure/Undo, retained Text context, unobscured canvas rendering and scope-aware properties. | T01–T07, fresh QML registration, save/reopen/export and manual/agent conflicts. |
| R5 — Viewer and Inspector consolidation | P1 / UX integration agent | Apply the ownership table to working sessions, remove duplicate rails/actions, keep advanced features accessible, and measure actual controls/focus/bounds. | X01–X03 and human task review at supported sizes/scales, with no target ambiguity or duplicated primary action. |
| R6 — Agent parity and enforced journey gates | Required for closure / QA + automation agent | Shared stable-ID commands, exact result/undo receipts, real-job completion, whole-journey tests and additions to the existing gate/CI inventory. | A01–A04 and X04–X06; named cases cannot silently skip, disappear or pass from static strings. |

R0 precedes changes. R1 defines shared ownership/session interfaces. R2 and R4
can then run in parallel; R3 reuses R2's ownership. R5 is integrated with those
sessions rather than landing as an unrelated cosmetic rewrite. R6 adds the
regression for each repair in the same change and performs combined acceptance.

Use separate worktrees from one captured integration baseline, including the
approved dirty-source snapshot; preserve the existing workspace. Avoid multiple
agents editing `StudioWorkspace.qml`, `Main.qml` or the large controller file
simultaneously. Integrate shared code/API changes first, then assign function or
component ownership. Share Git objects and bounded build/runtime outputs rather
than copying the repository, models and Rust targets per agent.

## Required regression journeys

Each case needs explicit preconditions, visible steps, expected target/recipe/
history, failure variant and evidence. “Click” means the actual production
control and pointer/keyboard route. Controller invocation is additional state
proof, not a replacement for the route being tested.

| Case | Journey and decisive assertions |
| --- | --- |
| S01 | Real subject → local exposure → delete parent layer. Exclusive masks/effects and references disappear; rendered image returns to the correct pre-layer state. One Undo/Redo restores/removes the entire owned edit. |
| S02 | Complete S01, then brush/refine a surviving owner or an explicitly created new selection, deselect/reselect, save and reopen. Deleted IDs/effects never resurrect or reappear in export; legitimate new owner creation remains allowed. |
| S03 | Delete one owner of shared geometry. Other owners retain their alpha, recipe and pixels. |
| S04 | Legacy mask-only, neutral scaffold + nonneutral mask, nonneutral layer + mask, duplicate names and repeated load/save. Preserve pre-delete pixels and remove only the intended exclusive edit. |
| S05 | Choose Add/Subtract/Intersect/Replace without drawing. Recipe, alpha and history are unchanged. The following gesture performs the chosen operation. |
| S06 | Subject/range/brush selection → Add Brush outside its original bounds → Subtract. Same set/owner stays selected, coverage expands/shrinks correctly and no unrelated layer appears. |
| S07 | Repeated strokes and edge refinement under crop/rotation/non-square source/display scale. Live, committed, reopened and exported coverage agree. |
| S08 | Local Undo/Redo, mid-stroke Undo, Done, untouched Done and Cancel/Escape. Assert session boundaries, global entry count and exact baseline restoration. |
| S09 | Native UI first enforces dirty-session Done/Cancel before destructive/navigation transitions. Separately force controller lifecycle invalidation during pending generation/raster (owner deletion, reset, undo or source switch); late work cannot write into the removed or new target. |
| S10 | Locked/hidden/renamed/collapsed target and selection reentry. Respect lock policy, reveal the intended owner and avoid silent retargeting. |
| I01 | Mark a real unwanted object → real verified model inference → review → accept → save/reopen/export. Verify provider/job/materialized patch, locality and photographic result. |
| I02 | Two pending same-mode repairs → apply/cancel/discard one. The other repair and previously accepted pixels remain unchanged. |
| I03 | Missing/corrupt model, unavailable provider and inference failure. Explain why, retain the region/draft, route setup to Settings and allow retry without duplicate history. |
| I04 | Cancel and target/source switch at each job phase. No late candidate is applied; prior accepted patch and user work remain. |
| I05 | Smooth sky, hair/skin edge and textured fabric/bark. Reviewed full-resolution quality/locality checks and preview/export parity, not merely nonempty output. |
| I06 | Preview candidate → finalization → accept/export; repeat with legacy preview, cancellation and finalization failure. No preview silently ships as final, no false success, duplicate repair or extra acceptance transaction. |
| T01 | Text → click → type multiline → Done → Undo/Redo. One caret, one new object, newline retained, one changed-session transaction and Text context retained. Untouched/empty-new completion creates neither a layer nor history. |
| T02 | Drag paragraph → type → select words → change font/fill/size through Inspector and popups → move/resize. Controls preserve range/draft/session; only the selected range changes; layout and committed rendering match. |
| T03 | Repeat new/existing editing through canvas and Inspector: local Undo exhaustion/Redo, styling then Cancel, popup Escape, authoring Escape, unrelated outside click and explicit tool change. Popups close without draft loss; local history never crosses its boundary; changed Done yields one global Undo, unchanged Done none; an explicit context switch is respected. |
| T04 | IME, emoji, RTL, empty draft/existing text and unavailable font. Preserve content, graphemes, focus and clear fallback/warnings. |
| T05 | Commit rejection, lock/delete/reorder and Board/asset switch during typing. Retain rejected draft; no edit lands on a different ID. |
| T06 | Material/path/mixed-style text: draft → commit → save → fresh-process reopen → export. Preserve typography, geometry and relevant pixels. |
| T07 | Exactly one Done/Cancel group; caret/handles remain visible near each viewer edge, zoom and rotation. Text-entry shortcuts cannot trigger global destructive actions. |
| B01 | Edited RAW → Boards → Return to photo. Reopen the exact source asset and unchanged pre-Board recipe; saved Boards remain; original file hash is unchanged. |
| B02 | Shared/independent Boards → edit → add Board → edit → delete → **main Cmd/Ctrl+Z for each action, then Redo each**. Restore exact IDs/content and chronological owners; repeat across final-delete/source-return followed by a source-photo edit. |
| B03 | Final photo-origin deletion → source return → same-session main Undo/Redo restores/rearchives exact IDs/resources/context. Separately test delete → save/relaunch and delete → Undo → save/relaunch: archived and restored states each persist. Save/load failure leaves the visible document unchanged. |
| B04 | Final deletion of a standalone Canvas/Boards document. Return to its collection; never open an unrelated selected RAW. |
| B05 | Legacy verified and ambiguous origins, missing source, relink and pending text/brush/repair. Use a declared safe resolution; preserve the current document on failure. |
| B06 | Create Boards → return → edit source → reopen Boards → return. A stale captured recipe cannot overwrite the newer source revision. Explicit old-snapshot restoration is separate and undoable. |
| O01 | Original hold/compare → release, focus loss and tool switch. No recipe/history change; clearly distinguish Photo original from Board-local baseline. |
| O02 | Change RAW/WB/profile override, preset, crop, layer, mask, paint, text and retouch → Revert photo to original. Compare full recipe and developed-baseline pixels; one Undo restores every edit/reference; reopen/export agree. |
| O03 | Section/layer/Board-image reset affects its named owner, including explicitly declared linked image/text instances, while preserving unrelated owners and the source photo. Shared scope is visible; local-only reset first detaches an instance when required. |
| X01 | Enumerate actual visible/enabled command groups and target IDs for each tool state. One primary group, no duplicate selection editor, no opaque editing window. |
| X02 | Keyboard-only creation, focus transfer, Delete/Undo/Escape, popup/IME dismissal and hidden workspace. Exactly one valid shortcut/transaction owner; transient dismissal preserves the underlying draft. |
| X03 | Supported minimum 1024×720, 1280×800 and 1440×900; 90/100/200% UI scale and relevant HiDPI. Reach every primary action; no clipped normal-size labels or overlay/handle/spinner collisions. Smaller sizes are supplementary only if supported. |
| X04 | Combined real RAW → select/refine → text → Boards → delete/return → retouch → original reset/undo → export → new-process reopen. Verify identity, history, full state, resources and decoded export. |
| X05 | Close/recovery and job/save/export failure at transition boundaries. No falsely acknowledged save, half-published edit or cross-document recovery. |
| X06 | Preserve existing qualified render/brush/text latency, memory and pixel budgets. Measure input-to-visible behavior, not an unrelated frame or an offscreen result labelled as native. |
| A01 | Repeat selection creation/Add Brush/delete parent through an approved agent task. Compare manual and agent normalized state, alpha, owner IDs, history and rendered output. |
| A02 | Agent point/paragraph/range text and original/Board lifecycle commands. Same resource semantics, exact affected IDs and one task Undo/revert. |
| A03 | Agent launched during both new and existing text/selection/repair drafts. Explicitly defer/reject conflicting mutation while preserving the draft. Cancellation must not roll back unrelated human edits. |
| A04 | Missing capability/model, stale ID/source revision, rejected command and failed job. No success claim from “queued” or an invokable return alone; inspect final state and rendered proof. |

## Agent command parity

The in-app agent and the coding-agent repair team are separate concerns; both
need the same behavior contract. Existing `agent_photo_commands.h` already uses
stable layer-ID adapters and exact-task-baseline authorization for registered
photo mutations. It deliberately excludes gestures/unowned async work. Reuse
those safeguards and add owned adapters for the missing selection/repair/source
lifecycle; do not bypass them by exposing every Qt invokable.

The proposed command/readback contract must expose document/source/Board/layer/
selection/repair IDs, source and recipe revisions, active session and available
capabilities with disabled reasons. Mutation requests carry expected IDs and
revision; coordinates declare source pixels versus normalized space and the
actual source dimensions. A changed or deleted target rejects safely.

Agent create/refine/remove/reset/return operations use the same document owner as
pointer and keyboard operations. Each approved task records its starting
snapshot and affected IDs, waits for the real job/final render, and returns a
completion receipt plus inspectable pixels/state. Cancellation restores only its
owned changes. New text drafts and draft selections report busy state before a
coalesced history transaction exists, so starting an agent cannot silently steal
the target. Real-model readiness and final-quality repair status remain visible
to the agent as well as the photographer.

## Regression enforcement and honest closure

1. **Fresh component/controller tier:** tests use rebuilt targets and generated
   meta-objects; assert IDs, ownership, masks, resources, history and failure
   transitions, with real pixel/alpha assertions where appropriate.
2. **Native UX tier:** actual production controls and visible gestures perform
   creation through deletion/Undo. Fixture injection may set up a test, but
   cannot replace the authoring action being qualified. Fake segmentation is
   useful deterministic coverage and is explicitly labelled as such.
3. **Production inference tier:** run the installed packaged SAM/inpaint paths
   on reviewed real fixtures. Record exact model/provider/source identities,
   requested region, job result and final artifact. Missing real-model evidence
   leaves that real-model case unqualified; fallback results can qualify only
   their separately named algorithm/provider cases, never substitute for it.
4. **Persistence/render tier:** save and reopen in a genuinely new process,
   inspect stable IDs and complete recipes, and decode real exports. An
   in-memory reload or correct layer count is insufficient for these cases.
5. **Human UX review:** extend the existing
   [human acceptance report](ux-human-acceptance-report.md). Give photographers
   task goals rather than developer click instructions; verify they can identify
   active tool, target and next action, finish refinement/text/return/reset and
   recover from an error without developer help. Agent review is additional
   evidence, not human usability approval.

Extend `tests/fixtures/editor-regression/contract.json`, the existing shell
fixtures and `scripts/perf/editor_native_regression.py`; do not create a parallel
unenforced checklist. The current native scenario inventory has gesture/timing
cases but no explicit selection-authoring/delete, repair-session, text-authoring,
last-Board/source-return or agent-authoring parity cases. Add required named
journeys for those gaps while retaining current scenarios and thresholds.

Missing, skipped, fake-provider-only, stale-build or incomplete evidence cannot
close a real-model/native case. Fixed proof strings cannot certify visible UI.
Do not relax a failing baseline to make a repair green. Record fixture hashes,
source/binary identities, commands, state transitions and exact stopping stage.
Remote branch protection and native release qualification remain separate
activation/approval work; this audit does not claim they are enabled.

Keep evidence bounded after the repository cleanup: small pinned fixtures,
before/failure/after frames and receipts for each case; existing 14-day retention
for unpinned runs. Preserve the latest qualified package manifest and necessary
failure evidence without copying full models/catalogs/build trees into reports.

### Definition of done for a repair

- The user-reported trigger has a recorded reproduction or is explicitly still
  unresolved; the change has a narrowly stated behavioral contract.
- Its assigned cases pass against the repaired source and packaged native app,
  including Undo/Redo, cancellation, failure, identity and persistence/export.
- Shared/manual/agent commands agree and retain unaffected content.
- Actual UI inspection and the relevant human task review pass.
- The regression is in the existing enforced inventory; old checklist completion
  is requalified with links to evidence rather than carried forward by wording.

**First implementation batch:** fresh text fixture/QML checks, S01/S02/F01
masked-layer deletion and resurrection, S05 next-operation isolation, and
B02/F06 main Board Undo routing. Establish R1's identity/origin transaction API
before enabling final-Board deletion or a whole-photo original reset.
