# Square controls and quieter Timeline chrome

This pass continues the [desktop Timeline refinement](timeline-desktop-refinement-2026-10-04.md).
It removes the decorative left accent strip and rounded treatment from shared
buttons, then refines clips and the docked inspector. Existing dirty-tree work
is preserved; no playback, document, render, or catalog controller is changed.

## Changes

- ActionButton, ToolButton, MiniButton, and their shared CommandButtonSurface use
  shallow two-unit scaled corners. The surface no longer draws a left accent
  stripe or decorative top hairline. Hover, pressed, active, primary/danger,
  disabled, reduced-motion, and full keyboard-focus treatments remain. Legacy
  accent/top-line properties remain accepted for existing callers.
- Clip content loses its decorative left selection rail. Status items are plain
  inline icons and text. Linked clips keep a colored link icon and its tooltip
  and accessible description; the repeated LINK caption is removed. Groups,
  offline, warnings, effects, proxies, and cache states retain their labels.
- Clip bodies and text backing have nearly square corners. Selection uses a
  complete hairline outline, strengthened in high contrast. Trim regions retain
  their existing 18-unit hit width and gesture logic while losing the filled
  rounded gutter. Grip feedback brightens on hover/press. Fade markers become
  quiet square-ended markers with stronger hover feedback.
- Clip metadata starts with duration and adds non-default opacity, speed, gain,
  or fades. Zero opacity is explicitly displayed. Defaults such as 100% opacity,
  1.00× speed, zero gain, and zero fades no longer repeat on every clip. Offline,
  adjustment, and silent-bed descriptions remain explicit.
- The Timeline inspector removes static section accent badges and flattens the
  effects list, effect rows, adjustment-layer rows, and keyframe toolbar. Compact
  density tightens repeated color-control spacing. Selected adjustment-layer
  outlines and all existing controls and handlers remain.

## Verification

The production app and test helpers built successfully. Fifteen QML tests and
the native Add/Source/trim/move/Undo/Redo/cancel/play/pause/seek/save/reopen workflow
passed. Both 49-view matrices passed across Comfortable, Balanced, and Compact;
all manifest PNG dimensions match the window size and device scale. A sample of
native Timeline and offscreen Edit captures was reviewed.

- [Timeline after refinement](evidence/timeline-square-controls-2026-10-04/density-native/timeline-edit-balanced-1440-1440x900.png)
- [Color inspector](evidence/timeline-square-controls-2026-10-04/density-native/timeline-color-compact-1440-1440x900.png)
- [Edit shared controls](evidence/timeline-square-controls-2026-10-04/density-offscreen/edit-balanced-1440-1440x900.png)
- [Verification receipt and logs](evidence/timeline-square-controls-2026-10-04/verification.json)

The source-contract corner expectation was updated for the intended ToolButton
geometry. Running `timeline_ui_consistency_contract_test` still stops at its
pre-existing persistent-delivery queue assertion, before subsequent styling
checks. The expected `queueOrder` literal remains absent from unrelated dirty
`job_runner.cpp` changes; this pass does not alter the queue.

Native proof uses source application QML, canonical controllers, and isolated
temporary data. It does not qualify packaged releases, all individual track and
fade pointer paths, physical trackpad/VoiceOver behavior, long-camera-media
playback, audio synchronization, export parity, or photographic fidelity.
