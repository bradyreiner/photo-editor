# Timeline State-of-the-Art Backlog

This is the concrete 20-task push list for turning the current timeline from a capable scaffold into a modern, professional editing workspace.

The current codebase already has important building blocks:

- timeline documents, save/load, and export
- source clip loading plus insert and overwrite entry points
- track-aware sequence state
- trim, split, marker, transition, effect, and transport primitives
- inspector controls for clip framing, opacity, gain, fades, and playback rate

The next step is not random feature count. It is tightening editorial speed, playback reliability, and high-leverage professional workflows in the right order.

## Execution rules

1. Finish timeline fundamentals before adding prestige features.
2. Prefer features that reduce edit time over features that only increase control count.
3. Keep the data model aligned with real editorial operations: gaps, linked media, track targeting, and nested sequences.
4. Make playback and trimming feel responsive before layering on advanced automation.
5. Ship keyboard-first and mouse-direct workflows together.

## P0: Editorial foundations

- [ ] 1. Gap-aware ripple editing
  - Implement real insert, delete, lift, and extract semantics against the track model.
  - Acceptance criteria: timeline gaps are represented explicitly or handled consistently enough that ripple edits preserve downstream timing without corrupting adjacent clips.

- [ ] 2. Magnetic snapping system
  - Snap to playhead, clip edges, edit points, markers, transitions, and optional audio peaks.
  - Acceptance criteria: snapping can be toggled globally, shows visible guides, and uses stable thresholds across drag, trim, and marker moves.

- [ ] 3. Roll, slip, and slide tools
  - Add true trim-tool behavior beyond direct trim-in and trim-out adjustments.
  - Acceptance criteria: roll preserves total duration across an edit boundary, slip changes source timing without changing clip position, and slide changes position while preserving neighboring duration.

- [ ] 4. Live trim preview
  - Show outgoing and incoming frames in the monitor while trimming.
  - Acceptance criteria: edge drags update monitor feedback in real time and expose exact timecode deltas.

- [ ] 5. Linked A/V selection and clip grouping
  - Make linked picture and sound behave as one unit unless explicitly unlinked.
  - Acceptance criteria: move, trim, delete, duplicate, and overwrite operations preserve link groups and allow temporary override.

- [ ] 6. Track targeting and patching
  - Expand track headers into a real edit-routing system.
  - Acceptance criteria: insert and overwrite operations honor targeted destination tracks and source-to-sequence patch rules.

- [ ] 7. Audio waveforms with caching
  - Replace placeholder audio visuals with generated waveforms that scale by zoom level.
  - Acceptance criteria: waveform generation is cached, survives reload, and does not block basic timeline interaction.

- [ ] 8. Keyboard-first editorial transport
  - Add J/K/L shuttle, next and previous edit navigation, go to in and out, match frame, and reliable frame stepping.
  - Acceptance criteria: the main editorial loop can be driven without reaching for the mouse for common review and trim operations.

## P1: Professional depth

- [ ] 9. Per-clip and per-track keyframe lanes
  - Move from slider-only properties to timeline-visible animation.
  - Acceptance criteria: transform, opacity, gain, and pan can be keyframed directly in expandable lanes.

- [ ] 10. Bezier curves and easing
  - Add interpolation controls that make keyframes usable for real motion design and audio ramps.
  - Acceptance criteria: keyframes support linear and eased interpolation with direct curve manipulation.

- [ ] 11. Nested sequences and compound clips
  - Allow users to collapse complex sections and reuse them.
  - Acceptance criteria: a sequence can be inserted as a clip, opened for editing, and updated without flattening the parent cut.

- [ ] 12. Proxy media workflow
  - Add lightweight media attachment and switching for smoother playback on large files.
  - Acceptance criteria: clips can resolve between original and proxy sources without breaking relink or export.

- [ ] 13. Render cache and background pre-render
  - Cache expensive sections so effects-heavy timelines remain playable.
  - Acceptance criteria: cached ranges are invalidated predictably and timeline playback quality degrades gracefully before dropping responsiveness.

- [ ] 14. Timeline search and filtering
  - Make large edits navigable.
  - Acceptance criteria: users can filter or jump by clip name, marker, media type, effect, offline state, and selected track scope.

- [ ] 15. Versioned timeline snapshots
  - Provide safe branching for alternate edits and client revisions.
  - Acceptance criteria: users can create named versions, restore them, and compare against the current edit without manual file duplication.

- [ ] 16. Smart markers and review notes
  - Turn markers into production communication tools instead of plain labels.
  - Acceptance criteria: markers can carry note text, status, assignee-style labels, and completion state, and can be filtered in the timeline UI.

## P2: State-of-the-art differentiators

- [ ] 17. Multicam editing
  - Support synchronized angle groups for interviews, podcasts, and events.
  - Acceptance criteria: users can sync angles, play multicam, and cut between cameras while preserving source relationships.

- [ ] 18. Transcription and text-based editing
  - Let spoken content drive rough-cut workflows.
  - Acceptance criteria: clips can generate transcript segments and users can create selections or cuts from transcript ranges.

- [ ] 19. Silence and filler-word detection
  - Accelerate talking-head and podcast cleanup.
  - Acceptance criteria: detected silence and filler regions are reviewable suggestions, not destructive auto-edits, and can be batch-applied with preview.

- [ ] 20. Caption and subtitle tracks
  - Keep delivery and accessibility inside the product.
  - Acceptance criteria: caption items live on dedicated tracks, can be edited against timecode, and export to both burn-in and sidecar formats.

## Recommended delivery order

1. Gap-aware ripple editing
2. Magnetic snapping system
3. Roll, slip, and slide tools
4. Live trim preview
5. Linked A/V selection and clip grouping
6. Track targeting and patching
7. Audio waveforms with caching
8. Keyboard-first editorial transport
9. Per-clip and per-track keyframe lanes
10. Bezier curves and easing
11. Nested sequences and compound clips
12. Proxy media workflow
13. Render cache and background pre-render
14. Timeline search and filtering
15. Versioned timeline snapshots
16. Smart markers and review notes
17. Multicam editing
18. Transcription and text-based editing
19. Silence and filler-word detection
20. Caption and subtitle tracks

## Immediate implementation targets

The first eight tasks map directly to the current timeline architecture and should drive the next engineering cycle:

- `apps/desktop/src/timeline_document_controller.h`
- `apps/desktop/src/timeline_document_controller.cpp`
- `apps/desktop/src/timeline_playback_controller.h`
- `apps/desktop/src/timeline_playback_controller.cpp`
- `apps/desktop/qml/workspaces/TimelineView.qml`
- `apps/desktop/qml/components/TimelineTrackHeader.qml`
- `apps/desktop/qml/components/TimelineSourcePanel.qml`
- `apps/desktop/qml/components/TimelineEffectsPanel.qml`

## Success test

The timeline is on the right path when an editor can:

1. build a cut with insert, overwrite, ripple, and trim tools without fighting clip collisions
2. navigate and adjust edits quickly from the keyboard
3. hear and see enough timeline detail to edit sound and timing confidently
4. keep playback smooth on complex sequences
5. branch versions, review notes, and finish delivery tasks without leaving the app
