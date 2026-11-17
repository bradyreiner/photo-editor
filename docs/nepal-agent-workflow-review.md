# Nepal board: creative work and agent workflow review

Updated 2026-11-15. Native controller checks and catalog persistence passed.
Foreground release-app verification remains incomplete because macOS reports a
locked screen. The editor has not been restarted.

## Current creative production

The subsequent **NEPAL / NEAR & FAR** production pass is saved as document
`4bec5106-bd3e-4172-888c-74cdff2dc031`: six boards, nine original photographs and
41 editable native layers. Three native render iterations refined the skyline
mask, typography, image selection, hierarchy and portrait tones. The exact saved
catalog record was reloaded in an isolated native controller; all six renders and
exports matched the reviewed images pixel for pixel. The running release's
read-only font catalog confirms both design fonts. A 16-second review film and
delivery PNGs are available. See [production evidence and limits](nepal-near-far-production.md).

Foreground UI verification remains pending screen unlock. The original Nepal
document and the prior Unfolded draft below were preserved unchanged during the
new production pass.

## Earlier Unfolded result

Saved a separate document, **NEPAL / UNFOLDED**, ID
`f48e66b1-1645-45a1-9584-bda0168731fb`, in project `Nepal 2026`.
Four 1920 × 1536 export boards share a 7680 × 1536 panoramic source.
The document contains 18 editable native layers: six source photographs plus a
second masked instance of the mountain photograph, native text, an ink fill,
a Bezier sequence line and three vector markers. A hand-traced editable mask
places the original foreground figure in front of the large title. The sequence
line also passes behind that figure.

The visual progression moves from crowded night streets through the camera
portrait and suspension bridge into the mountain panorama. The chartreuse curve
is a symbolic sequence graphic, not a geographic route. No location, peak name,
elevation, itinerary or documentary fact was invented. Production pixels come
from the user's photographs, not the earlier generated art-direction concept.
Full-resolution embedded camera JPEGs were extracted without recompressing their
pixel data; orientation was retained as metadata. Original RAW files were not
modified.

The seven backing files, including the blank panoramic canvas, were copied into
Looksy's app-owned `carousels/sources/<new-document-id>/` directory. Copy hashes
matched. The new document was saved through the existing catalog helper with a
fresh UUID, an explicit absence check and exact JSON readback. It was not written
through an unrestricted reflected command or by changing MCP permissions.

The original `Nepal 2026` document, ID
`9f23df9b-c309-4070-8404-0ab2d1393733`, was read before and after creation and
remained exactly unchanged. It still contains the original eight boards plus the
four task-created drafts from the rejected pass. The two added cover labels and
those drafts have not been removed. Backups and the named restore point remain
available; cleanup should use native undoable actions after unlocking.

Private local artifacts live in `.photo-editor/nepal-unfolded/`:

- `unfolded-native-preview.jpg` and `unfolded-native-master.png`.
- Four board PNGs and a delivery manifest in the directory recorded in
  `native-verification.json`.
- `saved-document-receipt.json`, including the catalog ID, saved-content hash,
  original-document preservation result and app-owned media hashes.
- `sources.json`, native layout specification, reproducible isolated proof tool,
  and a standard preset bundle carrying the editable layers.

## Verification and limits

| Check | Evidence |
| --- | --- |
| Standard preset bundle import and apply | Passed in isolated native EditorPreviewController |
| Exact Undo/Redo | Passed; one undo removes the composition, redo restores the exact layer recipe |
| Native save/reopen | Passed; four boards and all 18 layers retained |
| Board rendering | Passed through native CarouselDocumentController |
| Configured export with preflight | Passed; four 1920 × 1536 PNGs with valid color profiles and delivery manifest |
| Save to user's catalog | Passed through supported helper; exact readback |
| Durable backing media | Passed; app-owned copies with matching hashes |
| Preserve original document | Passed; complete before/after catalog records equal |
| Open new document in foreground release UI | Pending screen unlock |
| Release UI pause/notes/export interaction | Not qualified by the controller checks |
| Direct external MCP editing | Still unavailable in the running session |

An initial geometry check caught that extending a narrow blank board does not
widen its underlying source raster. Applying a wide authored recipe to that
source distorts the layers. The saved composition instead starts with a genuine
7680 × 1536 source and uses four native board crops. This is a workflow finding,
not a claim that the general extension behavior has been fixed.

## Critique of the rejected work

The first two cover labels did not satisfy the creative request. The subsequent
cream/gold editorial pages relied on familiar serif headlines, slogans, passive
photo rectangles and decorative contour marks. They were also flattened into
page images inside Looksy. Three interiors were imported and a temporary bridge
study was added to the original document. Those are retained drafts, not the
accepted new direction. Their separate SVG sources and backups remain in
`.photo-editor/nepal-editorial-pass/`.

## Agent and product improvements

1. **Expose connection readiness in the app.** The running release advertises
   disabled HTTP MCP and read-only capabilities. Show the active app/session,
   enabled tools, affected resources and client configuration. Do not silently
   widen the session or require users to discover launch flags.
2. **Add a typed board resource adapter.** Expose stable document/board IDs,
   order, source dimensions, shared spans, editable layers and saved/render
   revisions. Structural mutations need bounded batches, cancellation,
   conflict checks, rollback and completion receipts. Reflected carousel
   commands currently lack the native resource/journal owner required by the
   agent policy. Do not replace this with unrestricted method reflection.
3. **Tie context to the visible document.** Legacy live status exposed a stale
   catalog photo while a board was open. Existing authenticated composition
   inspection can read an active board using its exact virtual asset ID; reuse
   it rather than inferring a target from a selected library photo.
4. **Provide visual project asset selection.** The project has 634 photos, yet
   Add photo board opens a filesystem picker. Support catalog search, contact
   sheets, edited previews and batch placement by asset ID. RAW support differs
   between Add photo board and Place Image Layer. A `.CR2` versus `.png` native
   dialog alert was observed; its root cause has not been reproduced, so no
   save/open mode diagnosis is claimed.
5. **Make completion observable.** Separate draft preview, final render,
   persistence and export. A method returning successfully does not establish
   that the user sees the correct composition. This pass needed visual checks
   to catch distorted geometry despite successful save/reopen assertions.
6. **Improve canvas and typography setup.** Expose source size alongside board
   crops, provide a direct multi-board blank-canvas creation path, and keep
   text content, box size and exact placement together. The previous default
   512 × 128 title box clipped large text. Some native AX actions had no visible
   effect and require a fresh foreground reproduction before assigning cause.
7. **Give the user control over ongoing work.** Continue the requested compact
   canvas companion plus expanded workspace: visible target and current step,
   affected boards, change previews, pause after an in-flight operation, cancel,
   scoped notes with acknowledgement, and checkpoint restore. Completion should
   point to the actual saved document and outputs.

## Source fixes from the earlier live pass

The MCP launcher now prefers the authoritative native session file, respects
explicit session/app-data configuration, redacts tokens and reports disabled
HTTP MCP rather than silently selecting a stale nested Qt session. Eight focused
launcher tests passed.

`addEditPhotoBoard()` applied the default photographic view to already developed
raster layouts. The source now sets `legacy-oklch-v1`, matching design canvases.
A native paper/gold Add photo board → save → reopen → render regression failed
before the patch and passed afterward within two RGB values. The complete
`photo_board_controller_test` passed and the Debug desktop app built successfully.
The running release was not replaced. The new panoramic document explicitly uses
the design-canvas rendering mode, so it does not depend on relaunching into that
import fix. The independent six-board Near & Far document has now received the
foreground qualification below; this does not qualify the earlier panoramic draft.

## Foreground walkthrough, 2026-11-16

Opened **NEPAL / NEAR & FAR** from Library → Boards in the existing release.
All six boards displayed correctly after Fit. Authored layer counts were
8, 6, 6, 7, 4 and 10; the Layers inspector adds the Base entry to each count.
Temporary caption editing and visibility changes were restored with Undo;
visibility also passed Redo. Saving, switching to the earlier Unfolded document
and reopening Near & Far retained the saved slide and group JSON exactly.
Actual UI board export produced six profiled 1920 × 1536 PNGs and a matching
sRGB manifest with verified hashes. Small text-edge differences from the isolated
native helper are recorded rather than hidden as exact pixel parity.

Observed friction:

- The main Export tab presented **IMG_4637**, unrelated to the active board.
  Board export worked through Cmd+Shift+C, with its own folder chooser. A user
  should see the same explicit target across Edit, agent and Export.
- Opening the new document retained the previous viewport pan. The opening
  cover was initially outside the view; Fit corrected it. Board navigation also
  required explicit framing. All boards gives a very small horizontal strip;
  a useful document overview should use the available area more effectively.
- A text row's Edit action selected the Layers inspector; the Text rail was
  needed to reach typography. Closing typography did not end the separate
  on-canvas text session. Done restored ordinary navigation. These transitions
  need a single understandable edit state.
- Layer actions appear disabled until their row receives hover/focus. The
  actions work after selecting the row; they are not broken permissions.
- The document displayed an older-edit-copy notice whose description named the
  unrelated **IMG_4811.CR2**. That recovery item was left untouched.

The in-app agent's target chooser offered **Current Photo**, **Filtered Photos**,
and **Nepal 2026 / Every photo in this project**, even with a board document open.
A read-only request to identify the exact current document, board and authored
layer count reached Needs attention. Its explanation said inspection was required
but its planning instructions prohibit tool dispatch. The relevant planning prompt
is in `agent_local_model.cpp` and explicitly says read-only conversation planning
cannot dispatch tools. The observed result establishes that this request did not
receive enough usable supplied context; it does not establish the model provider
or that every creative request fails.

No executable plan was produced. Pause and scoped notes were therefore not tested,
and no alternate mutating task was started merely to expose their controls.
External MCP permissions were not widened and the release app was not restarted.

## Proposed next experience: an observable creative brief

The first view should make a specific promise using the actual document. For
this task: “Build a six-board story that moves from mountain scale to street life,
walking, people, quiet and shared moments.” Show the six board thumbnails, proposed
sequence, protected originals and editable outputs. Replace “1 selected target”
with the document name, exact board scope and a readable selection summary.

Before Start, use supplied document snapshots or permitted read-only inspection
to establish what already exists. Keep draft planning separate from mutation,
but allow planning to know the document. A ready brief should show the intended
visual change and why it supports the user's goal, plus observable acceptance
criteria: original photo sources retained, editable text, all six boards saved,
legible preview and exports verified. A read-only question needs an answer path,
not a mandatory plan containing editing actions.

During execution, the compact companion should show one current action, affected
board thumbnails and the latest visible result. Its primary controls should be
Pause, Add note and Expand. Pause should acknowledge the request immediately,
finish or safely cancel the current operation, then report the exact stopping
point. A note should show whether it applies now, to the next step, or to a named
board, and the agent should acknowledge the resulting change of direction.

The expanded workspace should lead with the artwork and a short list of reviewable
changes. Keep the brief, checkpoints and activity available without making a
terminal transcript the main interface. A user should be able to compare a
checkpoint, keep selected changes and resume with a note. Completion should link
the saved document and verified outputs. Errors should identify the failed step
and preserve the last useful result.

Use animation to preserve continuity: the companion expands from its current
position, a selected board moves into the review area, and previews crossfade
when a real render arrives. Keep controls stationary, honor reduced motion and
never represent a timer as measured progress. These are proposed product changes,
not features claimed to have shipped in this creative pass.

## Agent Studio opening failure, 2026-11-16

The next live check reproduced the user's report that Agent Studio would not open.
The running release process was present, but native UI control timed out after
the Agent button was clicked. A one-second stack sample placed the main thread
in a model-probe completion notification, QML binding evaluation and
`aiEditWorkflowState()`, including `assignmentStoryMapForScope()` and batch
clustering. The conversational screen was evaluating hidden legacy photo-workflow
bindings that can parse project recipes synchronously.

`AgentWalkthroughDialog.workflowState()` now returns without querying the legacy
getter when a conversational session controller is present. The session owns its
own state. A regression check records legacy workflow reads during opening and
after model-probe and selected-model notifications: the original code failed;
the patched code performs zero such reads and the full
`agent_session_terminal_ui_test` passed in 52.17 seconds. This duration is for the
whole interaction suite, not the time needed to open the panel. The Release app
built successfully.

Additional checks exposed failures in the legacy model-connection UI and an
existing visual-search source contract. These are recorded separately; the full
agent suite is not claimed to pass. Evidence lives in
`.photo-editor/agent-opening-2026-11-16/`, including the blocked stack and test logs.
The running process has not been restarted, so the patched release opening still
needs foreground verification. Restart approval was requested because closing an
unresponsive editor can lose unsaved work. The document-context and read-only
planning improvements above remain open.
