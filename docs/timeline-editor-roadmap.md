# Timeline Editor Roadmap

## Active milestone

The immediate focus is finishing real video edits with responsive playback,
predictable preview quality, usable crop/transform tools, music and dialogue,
stabilization, grading, and reliable delivery. The current findings and ordered
acceptance gates are in [Video editing readiness](timeline-video-editing-readiness-2026-10-22.md).
Use these gates before expanding the feature inventory below.

The [direct animation workflow](timeline-animation-workflow-2026-10-22.md) adds
Program placement handles, contextual video/text animation controls, a clearer
curve graph, speed access, opacity, and black/white fades within this milestone.

Build creator-grade editing foundations across tracks, compositing, animation, audio, typography, and the timeline UI. The completed [Timeline MVP Checklist](timeline-mvp-checklist.md) remains the short-form workflow baseline; it is no longer the product ceiling.

## Goal

Let a creator finish social and YouTube videos in one coherent workflow: organize footage, cut quickly, animate framing and graphics, shape the image, mix music and dialogue, review, and export. Program playback, saved state, reopened state, and final export must agree throughout.

## Product target

The product targets professional execution on the workflows creators use every day: short-form social edits, talking-head videos, tutorials, interviews, and music-led edits. The interface should expose direct timeline controls and focused inspectors so users can work quickly without leaving the edit to assemble common effects elsewhere.

## Existing foundation

The app already has a dedicated Timeline workspace and a track-based sequence model. It supports:

- source and program monitoring
- a resizable monitor/timeline workspace with usable collapsible Source and Inspector panels
- paired multitrack video/audio lanes with targeting, lock, mute, solo, and visibility state
- insert, overwrite, lift, extract, ripple trim, razor, roll, slip, and slide edits
- magnetic clip-edge and playhead snapping with visible feedback
- cached audio waveforms, clip fades/gain, transitions, effects, framing, opacity, and playback rate
- markers with notes, status, completion, filtering, and search
- undo/redo, native playback, save/load, render export, and queued job status
- editable title and caption tracks, with basic text styling and export rendering
- photo-layer groups with nested parent identities, group compositing, and persistent expand/collapse state
- clip keyframes for transform, opacity, audio, speed, effects, grading, title size, and first-class title/caption position anchors

Those capabilities are useful foundations. Their remaining gaps are consistency across property types, fast direct manipulation, independent track creation, complete group controls, motion-graphics depth, and visual proof across real editing and export workflows.

## Foundation work to finish

### Track and sequence structure

- Add video, audio, music, dialogue, effects, title, and graphics lanes independently from the timeline.
- Make add, reorder, rename, duplicate, lock, mute, solo, visibility, target, and delete actions predictable and undoable.
- Preserve track identity, clips, automation, routing, and group relationships through save/reopen and sequence edits.
- Support track-level transform, compositing, and grade controls where they produce a meaningful result.

### Hierarchical compositing and layers

- Make nested groups work as editable compositions with direct transform, opacity, blend, masking, effects, and color controls.
- Make the same hierarchy visible and manageable in the layer UI: clear indentation, collapse state, drag into/out of groups, selection, and group operations.
- Keep photo layers, video overlays, titles, shapes, and adjustment layers consistent where they share compositing behavior.

### Animation and keyframes

- Use one keyframe model and editor for every animatable clip, track, effect, grade, text, and audio parameter.
- Support precise key placement, selection, move, copy/paste, interpolation, Bezier handles, looping/roving where appropriate, and per-segment speed/easing controls.
- Keep animation reversible, persistent, scrubbable, and identical in the Program Monitor and rendered output.

### Text and motion graphics

- Build text directly in the timeline with typography, paragraph/layout controls, outlines, backgrounds, shapes, and safe-area-aware placement.
- Animate text and graphic properties with the shared keyframe system.
- Provide polished reusable title, caption, lower-third, callout, and social-format motion presets that remain editable after insertion.
- Keep preview, reopen, and export rendering consistent for fonts, transforms, transitions, and effects.

### Music and audio

- Create independent music, dialogue, voiceover, ambience, and effects lanes.
- Make trim, fades, gain, pan, routing, EQ, dynamics, and automation accessible from the timeline and focused mixer controls.
- Support ducking and common creator mix workflows with sample-accurate playback/export behavior.

### Creator-focused interface and proof

- Keep track creation, trimming, transitions, titles, music, grading, and export reachable without hunting through unrelated panels.
- Use direct manipulation in the timeline and monitor, with inspectors for precise values and automation.
- Validate a complete social edit and a complete YouTube edit through capture, edit, graphics, music, review, save/reopen, and final export.
- Measure visual output, audio behavior, responsiveness, recovery, and export parity on real project fixtures.

## Implementation order

1. Finish typed, independently creatable tracks and reliable row/clip editing.
2. Complete group and compositing controls in the UI and renderer.
3. Unify keyframe editing and per-keyframe interpolation/speed controls.
4. Build editable text and motion-graphics workflows on that animation foundation.
5. Complete creator audio tracks, mixing, routing, and automation.
6. Polish the integrated UI and prove complete social and YouTube projects through export.

## Concrete backlog

The older [state-of-the-art backlog](timeline-state-of-the-art-backlog.md) remains a source of ideas. The implementation order above defines the active creator-editing scope.
