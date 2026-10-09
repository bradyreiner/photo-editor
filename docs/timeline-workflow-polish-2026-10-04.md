# Looksy Timeline workflow and hierarchy

This continuation focuses on the user's four immediate problems: adding media,
trimming, moving clips, and playback. It builds on the
[three-density studio work](studio-ui-polish-2026-10-03.md). Existing grading,
audio, source monitoring, and delivery tools remain available through purposeful
layouts and contextual actions.

## What changed

- The picker’s primary **Add to Timeline** inserts at the playhead. Previously it
  loaded Source despite its label. **Preview in Source** is a separate action;
  Overwrite stays secondary and explicit. Failed edits leave the picker open.
- Media rows show identity and readiness, with a compact More menu. Selection
  exposes the primary Add action and secondary Preview action in the media panel.
  Double-click, Return, Enter, and Space preview in Source.
- Program transport prioritizes playback and navigation. Clip commands live in
  Timeline Edit, clip context menus, and Source controls. Explicit advanced
  transport controls remain available.
- The upper preset selector says **Layout** and the lower command menu says
  **Clip actions**. Clip labels have a restrained dark backing over thumbnails
  and waveforms. The camera-transform selector shows a concise value with its
  full name in the tooltip, and overflowing media lists expose a scrollbar.
- Clip selection preserves the user's collapsed Inspector. Selecting a layout
  deliberately restores its docked panels. These are separate behaviors.
- Clip-body dragging uses stable timeline-lane coordinates. Previously the clip
  moved underneath its own local pointer coordinates, causing feedback and
  unreliable displacement. Linked audio and one-gesture Undo/Redo are retained.
- Track refresh preserves delegates during trim and move gestures. Pending
  right-edge trims commit before the gesture ends; cancellation clears pending
  timer work and releases the preview without leaving virtualization frozen.
- The Program poster remains visible until actual processed pixels exist. A
  transparent image-provider URL no longer counts as a decoded preview. Loading
  at position zero does not wait for a position-change event that may never fire;
  a late monitor attachment consumes the decoder's available frame.
- Source preview also retains the media poster until its video sink receives a
  frame, so preview-only inspection can open with visible media immediately. A
  layout Item hosts the anchored ScrollView, preventing its content sizing from
  collapsing the Source editor. The poster visibility check converts its Qt URL
  to a string before checking whether it is empty.
- Decoded frames that arrive during a pending seek are held for the matching
  position acknowledgment. Superseding requests discard older candidates;
  same-target queued requests reuse their acknowledgment. Available frame
  timestamps are checked before publication, and pre-seek sink contents are not
  promoted as fresh seek results.
- Edit adjustment focus wraps at narrow widths, and the whole-image target bar
  avoids a redundant Base Image breadcrumb. Layer and mask context remains.

## Verification scope

The new `desktop_shell_timeline_workflow_test` uses the actual application QML,
isolated settings, temporary catalog/document paths, and a generated two-second
moving H.264/AAC test pattern. Native pointer events operate the visible Play/Pause button,
picker, clip edge, and clip body. Assertions cover insertion at the playhead,
preview without sequence mutation, linked trim/move timing, gesture Undo/Redo,
actual decoded image pixels, a changed picture on paused seeking, Source poster
and decoded playback, gesture cancellation, and save/reopen.

The studio matrix covers Comfortable, Balanced, and Compact at 1180×768,
1440×900, and 1920×900 on the native display, with Edit/Color/Audio/Finish Timeline
layouts. The offscreen matrix also covers 1920×1080. Native macOS constrains the
test window's height on this display; captures now wait for pixel dimensions to
match the requested logical size and device scale, rather than accepting an old
backing surface or a constrained window. Evidence
and final outcomes are recorded in
[the verification receipt](evidence/timeline-workflow-2026-10-04/verification.json).

This pass's native workflow result was **partial**: Add, preview without sequence
mutation, pointer trim/move, linked audio timing, Undo/Redo, cancellation, decoded
Program and Source playback, and save/reopen pass. Paused seeking fails the
changed-picture check. The observed decoder frames have no presentation
timestamp, and a position acknowledgment can arrive while the visible picture
remains unchanged. This is an open playback issue; a new image-provider revision
does not establish that a seek has shown the requested frame.

**Follow-up:** [Timeline playback and visible edit mode](timeline-playback-and-edit-mode-2026-10-04.md)
records the later fix and passing native workflow. Repeated decoder positioning
during forward playback reproduced the failure; normal playback now lets the
decoder advance. The later checks require changed pictures during Play and paused
seeks to 0.6, 1.2, and 0.3 seconds. This pass's partial receipt remains historical.

These fixtures establish specific UI and workflow behavior. They do not qualify
long timelines, representative camera codecs, realtime grading throughput,
full-resolution color fidelity, audio sync, export parity, physical trackpad
gestures, or VoiceOver navigation.

## Continue the product work

1. **Core workflow:** make selection, Source preview, insert/overwrite, trim and
   drag outcomes visible and predictable; qualify short real-camera sequences
   with linked audio, gaps, locked tracks, and repeated seeking.
2. **Hierarchy:** keep one workspace/layout control, one Timeline command group,
   and contextual inspectors. Give overflow panels visible scrolling. Keep clip
   labels readable over every thumbnail and waveform.
3. **Capability:** qualify existing tools through complete user tasks before
   adding more chrome. Next tasks are precise keyboard trims, snapping and edit
   modes, multiselection/group moves, ripple behavior, source patching, audio
   fades/meters, and delivery recovery.
4. **Looksy identity:** apply the shared restrained geometry, optical active
   edge, typography, motion, and warm/cool role accents consistently. Comfortable
   provides larger targets, Balanced is the general studio, and Compact supports
   dense expert work. Density must preserve access to every essential action.

The next pass should expose the existing edit behavior clearly before expanding
the toolbar. A user should be able to tell which edit mode is active, where media
will land, what snapping will attach to, and which linked clips will move. Use
short contextual labels and previews, with advanced settings in the inspector
or Clip actions. Confirm these outcomes on locked tracks, gaps, and multiselection.

| Density | Presentation | Essential actions |
| --- | --- | --- |
| Comfortable | Larger targets and persistent text labels; fewer open panels | Add, Preview, Play, trim, move, Undo remain direct |
| Balanced | Default studio spacing; contextual inspector and docked tools | Same actions and shortcuts |
| Compact | Tighter track rows and controls; icon tooltips and numeric precision | Same capabilities, with accessible names and visible focus |

Looksy's recognizable interaction should be consistent across the editor and
Timeline: a quiet control at rest, a clear hover response, a small press response,
and an optical accent for the active state. Panel opening can animate briefly;
dragging, trimming, seeking, and meters must track input immediately. Honor reduced
motion and preserve focus when panels close. Avoid animating every slider change
or placing heavy borders around every control.

## Reproduce

```sh
cmake --build --preset build-dev-macos-debug --target photo_editor_app desktop_shell_timeline_workflow_test desktop_shell_studio_polish_test timeline_frame_scheduling_test timeline_playback_controller_test
ctest --test-dir build/dev-macos-debug -L '^qml$' --output-on-failure
ctest --test-dir build/dev-macos-debug -R '^(app_shell_controller_test|timeline_frame_scheduling_test|timeline_playback_controller_test)$' --output-on-failure
PHOTO_EDITOR_SMOKE_NATIVE_POINTER=1 PHOTO_EDITOR_VISUAL_PROOF_DIR="$PWD/docs/evidence/timeline-workflow-2026-10-04/native" build/dev-macos-debug/tests/desktop_shell_timeline_workflow_test
PHOTO_EDITOR_SMOKE_NATIVE_POINTER=1 PHOTO_EDITOR_VISUAL_PROOF_DIR="$PWD/docs/evidence/timeline-workflow-2026-10-04/density-native" build/dev-macos-debug/tests/desktop_shell_studio_polish_test
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software PHOTO_EDITOR_VISUAL_PROOF_DIR="$PWD/docs/evidence/timeline-workflow-2026-10-04/density-offscreen" build/dev-macos-debug/tests/desktop_shell_studio_polish_test
```

Native tests need access to the macOS display and FFmpeg for fixture generation.
