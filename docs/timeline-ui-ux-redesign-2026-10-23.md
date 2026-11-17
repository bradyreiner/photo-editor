# Timeline UI and UX redesign — 23 October 2026

This change focuses on editing surfaces: finding media, manipulating the
timeline, inspecting effects, animating properties, and changing task layouts.
Three Luna xhigh specialists handled media, the track surface, and animation;
the integrated shell and verification were handled together in the working tree.
Unrelated changes in this already dirty checkout were preserved.

## Visible behavior

- **Media:** a virtualized thumbnail grid is the default. Grid/List and thumbnail
  size are explicit controls. Both views identify files by filename; camera
  metadata is secondary. Selection supports replace, Command/Control toggle,
  Shift range, hover preview, Source preview, drag placement, and explicit
  Insert/Overwrite. Filters preserve selection and disclose hidden selections.
- **Editing:** Select, Blade, Trim, Fit Sequence, and Fit Selection are directly
  available. Link, Snap, Ripple, and Insert/Overwrite state stay visible.
  Secondary creation and clip actions live in Actions. Track names remain
  readable at compact heights, with double-click rename and larger edit targets.
- **Window management:** Window → Workspaces contains Editing, Color, Audio,
  Finishing, Save Current Layout, and Reset Current Layout. Window → Panels and
  viewer options own panel management. Timeline → Sequence Settings owns sequence
  setup. Resolution and framing no longer consume the editing toolbar.
- **Layouts:** each workspace remembers its own panel arrangement and split.
  Responsive constraints change effective geometry without replacing preferred
  user sizes. Selection does not move a manually resized monitor split. Detached
  panels create their content when restored.
- **Effects and animation:** applied Effect Controls are separate from discovery.
  A narrow effect header keeps its full name on a dedicated row and its actions
  below. Animate offers searchable, grouped channels and an Animated-only filter.
  One retained curve editor docks or floats; it can follow selection or pin a
  clip. Pinned edits preserve timeline selection and position. Program actions
  disable for channels unavailable on the selected clip and outside clip bounds.
- **Color:** switching changes the actual panel/scopes arrangement. Requested
  controls finish loading in the foreground and stay retained across workspace
  switches. Short monitors place scopes beside the preview; narrow scopes use a
  selector and labeled accessible icon actions. Unsupported selections receive a
  purposeful selection prompt.

## Correctness boundaries

The new keyframe edit target is scoped, revalidates lock/deletion and document
generation, balances nested scopes, and suspends its override during synchronous
state publication. Inspector callbacks continue to address the selected clip.
Continuous edits remain one undo operation. Controller regressions exercise
off-selection edits, reentry, document replacement, lock/delete, and cleanup.

UI tests retain real pointer interaction. They wait for modal exit transitions,
filtered delegates, and actual loaded controls rather than treating a visible
container as readiness. Legacy floating-editor checks explicitly select floating
mode; additional tests cover docking. Collapsed-panel captures require the new
visible rail. Dark-header readiness checks contrast and bright detail and rejects
black, gray, and white placeholder frames. No test is skipped and no existing
timeout, memory limit, or latency budget is increased for this work.

## Verification

Final receipts and reviewed captures are recorded in
`docs/evidence/timeline-ui-ux-2026-10-23/verification.json`.

The final native workspace proof is run `workspace-r` (22.645 s). It uses the
generated video and repository photo fixtures, the native desktop window, real
Window actions and splitter input, eight captures, and destruction/recreation of
the Timeline view. It checks independent layouts, reset isolation, detached
restoration, state preservation, and Color controls after rapid tab switching.
Its first visible Timeline grid took 1,383 ms, first Color controls took 390 ms,
and warm switch p95 was 72.8 ms for Color and 65 ms for Editing. All 12
workspace-switch samples report an active, exposed native window. The 1180×768
landscape Color layout retained a usable preview; its 2360×1536 Retina capture
was reviewed and is included.
Budgets remain 1,500 ms for first Timeline load, 1,000 ms for first Color
controls, and 250 ms for warm workspace switches.

The evidence gallery selects fourteen captures across the native workspace,
Inspector, media browser, and keyframe views.

That timing run loads QML from source with the ordinary Qt disk cache. The test
does not invalidate that cache before launch, so 1,383 ms is not a strict
cache-cold result. It also does not establish runtime parity with the bundled
AOT build. The separate compiled-snapshot diagnostic used `qmlcachegen
--only-bytecode` on 281 files in 15.043 seconds without instantiating the UI; it
passed its cold grid gate but warm Editing took 1,131 ms and failed the existing
250 ms budget. That experiment preceded the frame-presentation fix from
`requestUpdate()` to `QQuickWindow::update()`. No latency budget changed.

Failed and diagnostic workspace runs remain in the evidence bundle. Runs j and
k missed the first-grid budget at 2,002 ms and 5,929 ms. Run l recorded a
1,029.6 ms warm Editing switch without an explicit activation transition. Run m
met its performance gates but failed laptop Color geometry before producing
the full capture set; its cause is unresolved. Run o missed the first-grid gate
at 1,754 ms and logged a 3,915 ms QML load return along with concurrent dirty
checkout warnings from `ImportView.qml`; that correlation does not establish a
cause. After the source edit, run p measured a 5,552 ms QML route and 6,136 ms
to the visible grid. These results are retained as nonacceptance history, not
replaced by the successful run.

The media proof covers both presentations, filenames, modifiers/range selection,
hover, Source preview, insertion/overwrite, real drag delivery, filters, hidden
selection, accessibility, and narrow/125-percent layouts. The Inspector proof
covers a 320-pixel effect header, channel filtering, docking/floating, pin/follow,
undo/redo, gesture closure, target-specific retiming bounds, shared channels,
out-point guards, and persisted geometry.

The final native edit workflow run passed in 38.124 s, including pointer Add,
preview-only Source, trim and linked move, gesture Undo/Redo, decoded playback,
and save/reopen. Its log also contains `ImportView.qml` undefined-value errors
for `loading` and `path`; the final build log records an integer-conversion
warning in generated ImportView code. The app build completed and linked. Those
warnings are preserved as separate follow-up evidence and are not attributed to
the Timeline UI changes. One recovery run against the final current binary
passed in 27.607 s. Its foreground admission diagnostic used 128 clips and 40
checkpoints in a 40,155,393-byte journal; discovery admission was 0.024 ms and
open admission was 0.008 ms. A separate scale fixture used 10,000 clips and
reported 111 ms load, 179 ms projection, 5 ms seek p95, 1 ms selection p95,
16 ms edit p95, 4 ms undo p95, and 397 MiB RSS growth. This is one final-binary
measurement; three earlier recovery repeats are archived separately and are not
presented as repeats of the final binary.

These receipts qualify this UI change on the tested fixtures and host. They do
not establish full Premiere feature parity, realtime 4K performance, or public
release readiness for the entire application.

## Manual acceptance

1. Import a mixed shoot; compare Grid/List and adjust thumbnail size. Filter a
   multiselection, preview in Source, and drag clips onto different tracks.
2. Use Select/Blade/Trim and fit controls, then resize the window and splitters.
   Switch tasks and verify each layout returns to its own arrangement.
3. Add two effects, collapse them, bypass one, reset it, and edit its curve.
   Check that names and actions remain readable in a narrow Inspector.
4. Pin a curve, select another clip, edit the pinned key, and undo once. Close
   during a gesture, reopen, float/dock, and verify channel and target state.
5. Switch Editing → Color → Audio → Editing. Reopen the app with detached panels
   and check the saved layout, normal keyboard focus, and preview behavior.
