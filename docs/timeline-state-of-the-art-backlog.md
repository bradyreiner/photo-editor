# Timeline State-of-the-Art Backlog

This is a post-MVP backlog for turning the current timeline from a capable scaffold into a modern, professional editing workspace. It does not expand the active [Timeline MVP Checklist](timeline-mvp-checklist.md).

Preview/export truth, compositing, retiming, audio, and release proof are tracked in the more detailed [Timeline Professional Parity Checklist](timeline-professional-parity-checklist.md).

The longer-term 200-capability program is tracked in the proof-gated [Timeline Industry-Leading Checklist](timeline-industry-leading-checklist.md).

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

- [x] 1. Gap-aware ripple editing
  - Implement real insert, delete, lift, and extract semantics against the track model.
  - Acceptance criteria: timeline gaps are represented explicitly or handled consistently enough that ripple edits preserve downstream timing without corrupting adjacent clips.
  - Proof: `timeline_document_controller_test` covers insert splitting, overwrite range replacement, ripple trims, lift/extract, multi-selection extract, and marker ripple behavior.

- [x] 2. Magnetic snapping system
  - Snap to playhead, clip edges, edit points, markers, transitions, and optional audio peaks.
  - Acceptance criteria: snapping can be toggled globally, shows visible guides, and uses stable thresholds across drag, trim, and marker moves.
  - Proof: a single controller resolver returns typed snap results for playhead, video/audio edits, transition edges, named markers, frame fallback, and optional cached waveform peaks. Global and audio-peak toggles persist, pixel thresholds scale with zoom, semantic targets take priority over frames, move/trim/roll/marker gestures show a typed guide and timecode, and marker movement previews then commits once for clean undo. Focused controller and UI contracts plus the full desktop target pass.

- [x] 3. Roll, slip, and slide tools
  - Add true trim-tool behavior beyond direct trim-in and trim-out adjustments.
  - Acceptance criteria: roll preserves total duration across an edit boundary, slip changes source timing without changing clip position, and slide changes position while preserving neighboring duration.
  - Proof: `timeline_document_controller_test` covers roll and slip invariants plus slide source-handle limits, adjacent-clip retiming, and preserved outer sequence duration. The Timeline workspace exposes direct Roll, Slip, and Slide tools with N, Y, and U shortcuts.

- [x] 4. Live trim preview
  - Show outgoing and incoming frames in the monitor while trimming.
  - Acceptance criteria: edge drags update monitor feedback in real time and expose exact timecode deltas.
  - Proof: `TimelineView.qml` snapshots the selected and adjacent edit-point clips, maps the proposed boundary to outgoing and incoming source frames, and seeks two persistent Program Monitor players on a 24 ms throttle. The overlay exposes clip names, source timecodes, sequence-boundary timecode, total delta, gap state, and restores the normal monitor on release or rolls the edit back on Escape/cancel. `timeline_ui_consistency_contract_test`, `desktop_shell_source_contract_test`, QML lint, and the full desktop application build pass.

- [x] 5. Linked A/V selection and clip grouping
  - Make linked picture and sound behave as one unit unless explicitly unlinked.
  - Acceptance criteria: move, trim, delete, duplicate, and overwrite operations preserve link groups and allow temporary override.
  - Proof: `timeline_document_controller_test` uses real video-with-audio fixtures to verify exact paired move/trim/split/delete/duplicate/overwrite timing, explicit unlink/relink, Option/Alt one-side edits, atomic undo, and detached-state save/reopen. Timeline QML exposes a quiet chain badge, clip-menu Link/Unlink and Duplicate commands, `Ctrl+D`, and edit-scoped Option/Alt overrides.

- [x] 6. Track targeting and patching
  - Expand track headers into a real edit-routing system.
  - Acceptance criteria: insert and overwrite operations honor targeted destination tracks and source-to-sequence patch rules.
  - Proof: compact `V#` and `A#` controls select one independent destination per stream and allow video or audio to be disabled. The controller carries explicit cross-lane audio routing through linked edits, undo/redo, and JSON persistence with no hidden target fallback. Real A/V fixtures cover V2/A2 insert, aligned cross-lane overwrite, V-only, A-only, and save/reopen; the focused controller/UI contracts and full desktop application build pass.

- [x] 7. Audio waveforms with caching
  - Replace placeholder audio visuals with generated waveforms that scale by zoom level.
  - Acceptance criteria: waveform generation is cached, survives reload, and does not block basic timeline interaction.
  - Proof: media-revision cache identity, a bounded serial background worker, atomic transparent 600/2400/9600 assets, immediate cross-controller reuse, exact trim-source cropping, zoom-adaptive tier selection, progressive fallback, and viewport-lazy loading are implemented. Focused fixtures prove nonblocking generation, dimensions, transparency, reuse, and invalidation; the timeline runtime smoke and full desktop build pass.

- [x] 8. Keyboard-first editorial transport
  - Add J/K/L shuttle, next and previous edit navigation, go to in and out, match frame, and reliable frame stepping.
  - Acceptance criteria: the main editorial loop can be driven without reaching for the mouse for common review and trim operations.
  - Proof: J/K/L now provide reverse-stop-forward shuttling with repeat-key 1x/2x/4x/8x speeds and automatic boundary stops; Left/Right steps exactly one paused frame; Alt+Left/Right and Alt+Up/Down navigate edits and markers; Shift+I/Shift+O seek Source In/Out; and F loads the active master clip and exact source frame into the Source monitor. The shortcut help matches the live commands. Focused document and playback fixtures plus the timeline UI contract and full desktop build pass.

## P1: Professional depth

- [x] 9. Per-clip and per-track keyframe lanes
  - Move from slider-only properties to timeline-visible animation.
  - Acceptance criteria: transform, opacity, gain, and pan can be keyframed directly in expandable lanes.
  - Proof: Keyframe Lanes v2 combines persistent per-clip scale, position, rotation, opacity, gain, and effect tracks with contextual per-track opacity, gain, and pan lanes. Track rows remain compact until a header menu opens one automation parameter; direct add, two-axis point editing, interpolation menus, deletion, and height restoration all operate against the same undoable points used by Program playback and export. The curve is bounded vector geometry rather than a large timeline-sized canvas. Focused controller tests prove atomic point edits, undo, persistence, and interpolation state; UI contracts, the packaged build, and timeline-only runtime smoke pass.

- [x] 10. Bezier curves and easing
  - Add interpolation controls that make keyframes usable for real motion design and audio ramps.
  - Acceptance criteria: keyframes support linear and eased interpolation with direct curve manipulation.
  - Proof: Bezier Curves v2 adds weighted incoming/outgoing tangent handles to the detachable Curve Editor with direct time/value dragging and persistent Linked or Broken behavior. Cubic time is solved independently from cubic value in controller and QML evaluation; bounded sampled FFmpeg expressions preserve the same curve for transform, opacity, gain, and effect animation. Focused controller coverage proves custom geometry, curve evaluation, undo, save/reopen, and a real rendered opacity curve; UI contracts, the packaged build, and timeline-only runtime smoke pass.

- [x] 11. Nested sequences and compound clips
  - Allow users to collapse complex sections and reuse them.
  - Acceptance criteria: a sequence can be inserted as a clip, opened for editing, and updated without flattening the parent cut.
  - Proof: Compound Truth v2 turns any contiguous multi-clip selection into one reusable parent clip while preserving the selected tracks, timing, effects, keyframes, transitions, and markers in a durable child timeline document. Compounds can nest to arbitrary depth; child undo/redo history is isolated and the exact parent history returns with the breadcrumb. Dependency-ordered background renders prevent an immediate nested return from cancelling or flattening its child, while failed workers remain retryable. Child source duration updates without silently changing the parent's in/out edit or moving downstream clips. Focused two-level controller coverage proves non-blocking creation, visible child edits in final parent export pixels, queued regeneration, repeated navigation, unchanged downstream timing, parent save/reopen, and retained nested edits; the UI contract, runtime smoke, and full desktop target pass.

- [x] 12. Proxy media workflow
  - Add lightweight media attachment and switching for smoother playback on large files.
  - Acceptance criteria: clips can resolve between original and proxy sources without breaking relink or export.
  - Proof: Proxy Playback v1 provides per-clip and timeline-wide generation commands behind existing menus, a bounded background worker, source-revision cache reuse, compact PXY/ORG status, immediate proxy/original switching, save/reopen attachment recovery, automatic failure fallback, and original-only FFmpeg export. Focused controller coverage proves enqueue responsiveness, real proxy creation, cache reuse, switching, persistence, removal, and successful export with a deliberately corrupted active proxy.

- [x] 13. Render cache and background pre-render
  - Cache expensive sections so effects-heavy timelines remain playable.
  - Acceptance criteria: cached ranges are invalidated predictably and timeline playback quality degrades gracefully before dropping responsiveness.
  - Proof: Render Cache v1 divides the sequence into deterministic four-second ranges keyed by render-relevant sequence, track, clip, effect, keyframe, transition, and source-revision state. A cancellable two-thread FFmpeg worker publishes only missing signatures, preserves still-valid ranges, caps preview media to a 1280-pixel edge, and prunes storage at 128 files or 1 GB. Auto playback prefers valid fully composited ranges, falls through to proxies and originals on cache misses or invalidation, and never changes original-only export inputs. A three-pixel ruler hairline communicates queued, rendering, and ready ranges without adding permanent panel density. Focused controller coverage proves non-blocking generation, real two-range output, affected-range-only invalidation, valid-range preservation, live fallback, regeneration, clearing, and cancellation; playback coverage proves composited ranges outrank clip proxies in Auto mode.

- [x] 14. Timeline search and filtering
  - Make large edits navigable.
  - Acceptance criteria: users can filter or jump by clip name, marker, media type, effect, offline state, and selected track scope.
  - Proof: Timeline Search v2 moves matching and chronological sorting into the controller, covering clip labels and source filenames, marker labels/notes/status/timecodes, media type, effects, transitions, online/offline state, marker review state, and all-track or selected-track scope. The existing compact Search action expands one shared-style row with a typed filter menu only when needed; nonmatches dim through an O(1) result-ID map, while Previous/Next wrap through exact results, select matching clips, and seek matching markers without editing the document. Focused controller coverage proves marker-note and review-state filtering, open-marker exclusion, effect lookup, selected-track isolation, clip activation, and exact marker seeking; the 100-item UI contract, full desktop QML/C++ build, and timeline-only offscreen runtime smoke pass.

- [x] 15. Versioned timeline snapshots
  - Provide safe branching for alternate edits and client revisions.
  - Acceptance criteria: users can create named versions, restore them, and compare against the current edit without manual file duplication.
  - Proof: Sequence Versions v1 adds immutable named checkpoints, clip/marker/duration comparison, one-step undoable restore, automatic pre-restore recovery points, and newer-autosave recovery in one shared-style contextual popup. Controller fixtures cover create, immutable persistence, compare, restore, undo/redo, reopen discovery, and autosave recovery; UI contracts, timeline runtime smoke, and the desktop app build pass.

- [ ] 16. Smart markers and review notes
  - Turn markers into production communication tools instead of plain labels.
  - Acceptance criteria: markers can carry note text, status, assignee-style labels, and completion state, and can be filtered in the timeline UI.

## P2: State-of-the-art differentiators

- [x] 17. Multicam editing
  - Support synchronized angle groups for interviews, podcasts, and events.
  - Acceptance criteria: users can sync angles, play multicam, and cut between cameras while preserving source relationships.

- [x] 18. Transcription and text-based editing
  - Let spoken content drive rough-cut workflows.
  - Acceptance criteria: clips can generate transcript segments and users can create selections or cuts from transcript ranges.
  - Proof: Text-Based Edit v2 adds permission-aware native macOS Speech recognition with a bounded serial unique-source queue, cancellation, on-device preference where supported, explicit failure states, source-trim and playback-rate timing conversion, and all-or-nothing caption replacement as one undoable edit. Generated word-timed captions feed the existing searchable transcript, media selection, and confirmed synchronized ripple extraction across picture, linked audio, captions, markers, and automation. A deterministic fixture proves asynchronous generation, exact 2x timing conversion, undo/redo, and no-edit cancellation; focused controller coverage, UI contracts, timeline-only runtime smoke, and the complete desktop build pass.

- [x] 19. Silence and filler-word detection
  - Accelerate talking-head and podcast cleanup.
  - Acceptance criteria: detected silence and filler regions are reviewable suggestions, not destructive auto-edits, and can be batch-applied with preview.
  - Proof: Cleanup Review v2 adds cancellable bounded FFmpeg silence detection to the existing inert filler and timed speech-gap review flow. Source-time results remap through trims and playback speed, keep 180 ms of natural room, persist across save/reopen, preview by exact sequence seek, and batch-apply only selected non-overlapping regions after a second confirmation. A real A/V fixture proves its known 0.45–1.25 second silent interval, conservative handles, 1.25x mapping, exact synchronized ripple apply, and atomic undo/redo; UI contracts, the timeline-only runtime smoke, and the full desktop build pass.

- [x] 20. Caption and subtitle tracks
  - Keep delivery and accessibility inside the product.
  - Acceptance criteria: caption items live on dedicated tracks, can be edited against timecode, and export to both burn-in and sidecar formats.
  - Proof: Caption Delivery v2 provides a persistent dedicated `CC` lane, clip and word-level text/timing edits, title-safe active-word preview, styled burn-in rendering, and atomic SRT/WebVTT export. Controller fixtures prove editing, undo/redo, split/merge, render pixels, sidecar contents, and save/reopen; UI contracts, the timeline-only runtime smoke, and the desktop app build pass.

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
