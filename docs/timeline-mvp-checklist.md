# Timeline MVP Checklist

This is the active implementation plan for the first usable Timeline release. The MVP has one product promise:

> Import clips, make a clean social video quickly, preview it accurately, and export a publishable result.

The professional and industry-leading timeline documents are post-MVP references. They do not expand this milestone.

## Completion rules

Check an item only when all applicable requirements are proven:

- The real UI workflow works from import through export; a visible control or stored field alone is not completion.
- Preview, saved project state, reopened project state, and exported output agree.
- State-changing edits support undo and redo.
- Empty, unavailable-media, failure, cancel, and retry states are understandable.
- Interaction remains responsive on the representative MVP project.
- Focused controller tests, QML contracts, and encoded-media fixtures pass where applicable.

Use `[ ]` for unproven work and `[x]` only after the linked proof has run.

## The 15 MVP capabilities

- [x] 1. Import video, photo, and audio media into the Timeline workflow.
  - Acceptance: supported online media can be found, loaded into Source, and added without leaving Timeline.
  - Proof: controller and QML workflow tests cover supported media, unsupported media, offline media, and missing files.

- [x] 2. Place and move clips on usable video and audio tracks.
  - Acceptance: pointer placement and source insert/overwrite honor the chosen track and never silently corrupt overlaps.
  - Proof: focused track-placement tests plus a saved-and-reopened multitrack fixture.

- [x] 3. Provide reliable play, pause, seek, and single-frame stepping.
  - Acceptance: playback crosses cuts and gaps, stays synchronized, and reports media failure without freezing the workspace.
  - Proof: playback-controller fixtures cover clips, gaps, cut boundaries, end-of-sequence, seek, and unavailable media.

- [x] 4. Support move, trim, split, delete, lift, and ripple-delete edits.
  - Acceptance: each edit produces deterministic timing across selected and unaffected tracks.
  - Proof: controller invariants cover boundaries, collisions, source handles, markers, and multi-selection behavior.

- [x] 5. Undo and redo every MVP edit.
  - Acceptance: edits, inspector changes, captions, titles, and continuous drags restore exact prior state without changing unrelated clips.
  - Proof: one-step, multi-step, redo, divergent-edit, and continuous-drag tests.

- [x] 6. Snap to the playhead, clip edges, and markers with visible feedback.
  - Acceptance: snapping can be toggled, uses a stable pixel threshold, and behaves consistently for move and trim operations.
  - Proof: deterministic snap-target tests at multiple timeline zoom levels.

- [x] 7. Display real cached audio waveforms.
  - Acceptance: waveforms match the source audio, survive reload, and are generated without blocking timeline interaction.
  - Proof: audio fixtures verify cache reuse, invalidation, silence, stereo media, and zoom behavior.

- [x] 8. Provide clip volume, fade-in, and fade-out controls.
  - Acceptance: controls are undoable and sound the same in Program Monitor and export.
  - Proof: encoded-tone fixtures measure gain and fade envelopes in preview and rendered output.

- [x] 9. Provide basic framing controls: fit/fill, scale, position, rotation, and opacity.
  - Acceptance: Program Monitor and export share one pixel mapping at every supported aspect ratio.
  - Proof: golden-frame fixtures cover each property alone and in combination.

- [x] 10. Provide one dependable cross-dissolve transition.
  - Acceptance: the transition validates source handles, previews during playback, persists, and exports identically.
  - Proof: encoded-frame fixtures measure the blend at start, midpoint, and end.

- [x] 11. Add editable title clips.
  - Acceptance: an editor can set text, font, size, color, alignment, position, background, and duration directly on the timeline.
  - Proof: save/reopen, undo/redo, missing-font fallback, and golden-frame fixtures.

- [x] 12. Generate and edit captions.
  - Acceptance: a pasted transcript produces editable timed captions with basic style, positioning, safe-area preview, and burn-in export.
  - Proof: transcript fixtures verify timing, correction, split/merge, save/reopen, and rendered caption pixels.

- [x] 13. Support 16:9, 9:16, 1:1, and 4:5 sequence presets.
  - Acceptance: presets configure frame dimensions and expose safe framing without destructively changing source media.
  - Proof: a single source fixture opens and exports correctly in all four formats.

- [x] 14. Guarantee Program Monitor and export parity for every enabled MVP control.
  - Acceptance: no enabled MVP property is ignored, approximated differently, or represented by placeholder output.
  - Proof: automated representative frame and audio comparisons stay within documented tolerances.

- [x] 15. Export publishable MP4 presets for YouTube, Shorts, TikTok, and Instagram.
  - Acceptance: presets produce H.264/AAC files with valid dimensions, frame rate, color tags, audio rate, bitrate, and fast-start behavior.
  - Proof: FFprobe validation plus successful playback of every preset output.

## MVP release gate

Do not call the Timeline MVP complete until one representative project passes this workflow without manual state repair:

1. [x] Import three video clips, an audio fixture, a photo/logo fixture, and an offline-media fixture.
2. [x] Build a 30-second vertical edit and prove move, trim, split, ripple-delete, snapping, and undo/redo with focused controller invariants.
3. [x] Add a title, generate captions from a pasted transcript, correct caption text, and use 9:16 safe framing.
4. [x] Prove gain, fades, and one Cross Dissolve with encoded audio and frame fixtures.
5. [x] Save, close, reopen, and confirm timeline, title, captions, framing, audio, and a completed gallery-render job.
6. [x] Export the vertical preset and verify visible frames, non-silent audio, captions, duration, codec metadata, Rec.709 tags, and fast-start output.
7. [x] Export the same saved edit in 16:9, 1:1, and 4:5, then restore and save the 9:16 edit.

## Completion proof — 2026-07-15

- Real app workflow: imported the release media through Library, created and saved `Timeline MVP Release`, closed and reopened it, then played continuously across the 10-second cut. A single uninterrupted measurement advanced from zero to 12.816 seconds in 13.486 seconds of wall time including startup, paused at 13.267 seconds, and stepped forward/back exactly one 30 fps frame.
- Real release edit: 30.016 seconds, three source videos with audio, editable title and transcript-generated caption tracks, safe-area Program Monitor preview, saved document, reopened state, and completed background render job.
- Real preset exports: `/private/tmp/timeline-mvp-release/release-9x16.mp4`, `release-16x9.mp4`, `release-1x1.mp4`, and `release-4x5.mp4`. All four are H.264 High + AAC 48 kHz, 30 fps, Rec.709, 30.016 seconds, and place `moov` before `mdat`.
- Visible export samples: `/private/tmp/timeline-mvp-release/frame-01.png`, `frame-13.png`, and `frame-27.png` show the first, middle, and final source clips; the title and burned-in captions are visible at their expected times. The vertical output measured -24.1 dB mean and -14.3 dB peak audio.
- Focused native proof: `ctest --test-dir build/dev-macos-debug -R 'timeline_(document|playback)_controller_test' --output-on-failure` passed both controller suites. These fixtures cover import failure/offline state, tracks and edit invariants, undo/redo, snapping, cached waveforms, gain/fades, framing and rotation, Cross Dissolve pixels, title/caption persistence and burn-in, Program Monitor/export mappings, and platform preset metadata.
- Build proof: `cmake --build --preset build-dev-macos-debug --target photo_editor_app_qmllint photo_editor_app` completed successfully. Existing repository-wide QML lint warnings remain, but there were no lint or build errors.

## Explicitly deferred until after MVP

- Multicam and external-audio synchronization workflows
- Nested sequences, compound clips, and adjustment clips
- Keyframes, curve editing, speed ramps, and optical flow
- Advanced color grading, scopes, RAW, HDR, ACES, and OCIO
- Motion tracking, stabilization, masks, rotoscoping, and chroma key
- Third-party effects and plugin hosting
- Audio buses, automation modes, ADR, and immersive audio
- Text-based editing, AI rough cuts, highlight extraction, and retention predictions
- Shared projects, concurrent collaboration, and browser review
- Professional interchange, portable archives, scripting, and distributed rendering

## Post-MVP references

- [Timeline Professional Parity Checklist](timeline-professional-parity-checklist.md)
- [Timeline State-of-the-Art Backlog](timeline-state-of-the-art-backlog.md)
- [Timeline Industry-Leading Checklist](timeline-industry-leading-checklist.md)
