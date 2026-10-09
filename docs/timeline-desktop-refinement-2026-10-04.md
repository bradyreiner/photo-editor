# Timeline desktop refinement

This pass makes the Timeline read as a docked desktop editor. It extends the
existing Looksy controls and three density levels, with narrow QML changes and
no edits to the playback or document controllers.

## Changes

- Source, Program, inspector rails, and Timeline use square pane geometry rather
  than rounded elevated cards. The main workspace gap is reduced to a shared
  two-unit spacing token; existing splitter grips and saved panel state remain.
- Transport is one row: Play, navigation, scrubber, and timecode. Play uses the
  compact secondary control treatment. Current time and duration appear together
  when the Program panel is at least 480 scaled units wide. Below 400, the extra
  scrubber disappears; the Timeline ruler and existing navigation menu remain.
- Add Track and Focus Tracks move into the editorial toolbar, separated from
  clip commands and zoom. The separate sequence header is contextual for advanced
  metadata, markers, and marker feedback rather than a permanent title band.
- The overview appears when sequence length multiplied by zoom exceeds the
  visible Timeline width. Short sequences no longer consume an extra overview row.
- Media assets use integrated rows with restrained hover/selection treatment and
  a divider. Comfortable/Balanced/Compact baseline row heights are 88/78/68 units;
  preview heights are 58/52/44. Existing menus, preview, drag, ratings, and tags
  remain wired to their existing handlers.
- Track headers lose the broad tinted card. Visibility/mute, solo, source patch,
  and lock use the shared ToolButton, explicitly qualified against the similarly
  named Qt control. Buttons use the dense height token (30/28/26 before scaling),
  share available row width, and retain model-driven states and accessible names.
  The targeted-track accent rail and structural separators remain visible.

The existing source-contract expectations for track buttons were updated for
the shared component and its `onClicked` handlers. No unrelated worktree changes
were discarded.

## Rendered comparison and verification

- [Before, Balanced 1440](evidence/timeline-playback-edit-mode-2026-10-04/density-native/timeline-edit-balanced-1440-1440x900.png)
- [After, Balanced 1440](evidence/timeline-desktop-2026-10-04/density-native/timeline-edit-balanced-1440-1440x900.png)
- [After, Compact 1180](evidence/timeline-desktop-2026-10-04/density-native/timeline-edit-compact-1180-1180x768.png)
- [Verification receipt](evidence/timeline-desktop-2026-10-04/verification.json)

The production app and both test helpers built successfully. Fifteen QML tests,
the native pointer workflow, and both 49-view density/layout matrices passed.
Recorded PNG backing sizes match the manifests and display scale. Native pointer
checks cover Add, Source preview, edge trim, linked move, Undo/Redo, cancellation,
edit-mode selection, changing decoded pictures during Play, paused seeks forward
and backward, and save/reopen. A sample of captures was visually reviewed.

These native checks use actual source application QML with canonical controllers
and isolated temporary data. They do not establish packaged release behavior,
every individual track-toggle pointer path, long-camera-media playback, audio
sync, export/color parity, physical trackpad gestures, or VoiceOver. The existing
delivery-queue source-contract failure remains separate and was not rerun here.

## Continue the desktop direction

The next pass should address clip chrome and inspector composition: quieter trim
handles at rest with clear hover/drag feedback, fewer repeated metadata badges,
compact parameter rows, and stronger grouping of edit tools. Verify those changes
through complete edits, including snapping, locked tracks, and multiselection.
