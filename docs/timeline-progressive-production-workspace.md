# Timeline Progressive Production Workspace

The Timeline should feel calm when an editor is assembling a simple social cut and become deep only when the task or selection calls for it. Capability is not measured by how many controls are visible at once.

## Product rules

1. Keep only media, undo/redo, save, transport, the active tool, and zoom continuously reachable.
2. Reveal clip actions after selection, marker review after markers exist, error status after a failure, and render state while work is active.
3. Put creation, layout, editing behavior, zoom, and infrequent document actions in focused icon menus.
4. Let Source and Inspector dock, collapse, hide, or detach without duplicating editing semantics.
5. Use one command registry so menus, shortcuts, the command palette, contextual actions, and future scripting invoke the same operations.
6. Never advertise an effect or production tool until Program Monitor, persistence, undo/redo, and export agree.
7. Load advanced panels and analysis asynchronously; the playhead, pointer, trim handles, and transport must not wait for them.

## Density layers

### Layer 1 — calm core

Always visible: Add Media, undo/redo, save, playback, active edit tool, contextual split, and Timeline zoom. The Source panel is available for assembly; the Inspector remains collapsed until a selection makes it useful.

### Layer 2 — contextual work

- Clip selected: Inspector, split, lift/extract, replace, speed, framing, audio, and title/caption properties.
- Edit point selected: roll, transition, source-handle, and precision trim controls.
- Audio selected: gain, fades, channel mapping, ducking, cleanup, waveform, and loudness tools.
- Title/caption selected: typography, safe-area, animation, transcript, translation, and accessibility tools.
- Marker or review selected: notes, status, assignee, navigation, comparison, and approval actions.
- Render or media failure: diagnosis and recovery actions without covering transport.

### Layer 3 — focused menus and command palette

- **Create:** media, marker, title, captions, track pair, nested sequence, adjustment clip, generator, and template.
- **Edit:** select, razor, slip, roll, slide, trim modes, insert/overwrite, ripple, linked selection, snapping, lift/extract, replace, and duplicate.
- **View:** panel visibility, detachable windows, focus Program, workspace presets, overlays, scopes, minimap, waveform detail, and interface density.
- **Zoom:** out, 100%, fit sequence, fit selection, edit-point detail, and zoom in.
- **Find:** clips, transcript, markers, effects, offline media, unused media, duplicates, and review notes.

### Layer 4 — modular production rooms

Workspace presets should rearrange the same panels rather than create parallel editors:

- **Cut:** Source, Program, Timeline; contextual Inspector.
- **Vertical/Social:** Program-first layout, safe zones, captions, reframing, platform preview, and delivery presets.
- **Audio:** larger Timeline, mixer, meters, loudness, cleanup, buses, and automation.
- **Color:** Program, scopes, grading stack, shot matching, and reference stills.
- **Motion:** Program, keyframes, curves, tracking, masks, stabilization, and compositing.
- **Review:** Program, markers, versions, comments, comparisons, and approvals.
- **Dual display:** full-screen Program on one display and docked/detached production panels on the other.

## High-function tool families

These are capability groups, not permanent toolbar buttons. Their implementation remains gated by the professional and industry-leading checklists.

- Precision editorial: J/L cuts, match frame, replace edit, three/four-point edits, trim monitor, sync, multicam, nests, compound clips, adjustment clips, freeze frames, variable speed, optical flow, and edit-decision history.
- Motion and compositing: keyframes, graph editor, expressions, tracking, stabilization, masks, rotoscoping, blend modes, chroma key, corner pin, camera motion, and reusable motion templates.
- Color and finishing: scopes, curves, wheels, qualifiers, shot match, LUT/OCIO/ACES management, HDR, gamut checks, skin-tone tools, grain, halation, and output transforms.
- Audio: clip and track mixer, buses, sends, automation, EQ, compression, limiter, denoise, de-reverb, ducking, ADR, loudness targets, stems, and channel mapping.
- Text and social: text-based editing, transcript search, caption styling, translation, safe zones, auto reframe, platform previews, hook variants, b-roll suggestions, silence removal, and publish packages.
- Review and collaboration: version stacks, annotations, marker states, side-by-side comparison, approvals, change lists, portable review links, and audit history.
- Delivery: background render queue, smart render, proxies, cache management, presets, batch variants, QC, captions/subtitles, stems, interchange, archives, and resumable jobs.

## Performance contract

- Pointer, playhead, scrubbing, trim, and transport feedback must begin in the next visible frame.
- Opening a cached menu should feel immediate; opening a lightweight panel should target under 100 ms.
- Waveform generation, thumbnails, transcription, analysis, proxy creation, scopes, and effect preparation run off the UI thread and publish progressive results.
- Hidden and detached panels do not keep expensive visualizations rendering.
- Preview quality may adapt during interaction, but the last-good frame stays visible and full-quality output replaces it after the gesture settles.
- Every performance optimization requires a measured before/after interaction trace and a deterministic regression gate.

## Quiet editorial chrome

The Timeline keeps its editing layout, but now uses the same control grammar as the rest of the app:

- Buttons are borderless at rest, use a low-contrast fill on hover, and reserve a restrained accent fill for the primary action.
- Cut corners and decorative top highlights are opt-in rather than the default button silhouette.
- Tabs remain flat with a short underline; active tools use a soft tint instead of a filled circle or luminous outline.
- Track targeting uses a two-pixel accent rail. Track and lane surfaces stay borderless until selection or keyboard focus needs a clear edge.
- Clips retain media-type color, while selection adds a soft fill and one-pixel edge. Keyboard focus remains a separate high-contrast state.
- The desktop presentation is dark-only. Its cool neutral surfaces use quieter teal and blue accents, while saturated signal colors remain reserved for warnings, failures, scopes, overlays, and media content.
- The Program Monitor fits the active sequence aspect ratio inside the available stage; its media, titles, and overlays share the same fitted rectangle.
- The Inspector exposes Effects, Motion, and Audio directly, moves secondary modes into More, and uses flat effect-stack rows instead of nested outlined cards. The first production effect library provides Exposure, Contrast, Saturation, Black & White, Blur, Fade, and Punch with searchable add, amount, reset, per-effect bypass, stack bypass, remove, clear, undo, persistence, Program Monitor preview, and export behavior.

## Execution sequence

1. Calm header and Timeline command bar with progressive menus. **Implemented in the first slice.**
2. Collapsible and detachable Source/Inspector windows with Focus Program. **Initial pop-out behavior implemented; docking presets and geometry persistence remain.**
3. Quiet editorial buttons, edges, selection, and a dark-only palette shared with the rest of the app. **Implemented and visually proven.**
4. Shared command registry and searchable command palette.
5. Lazy advanced-panel loading, cached menu models, and interaction latency telemetry.
6. Cut, Vertical/Social, Audio, Color, Motion, Review, and Dual Display workspace presets.
7. Add production tool families in preview/persistence/export parity order, using the linked professional gates.

## Editorial chrome proof — 2026-07-15

- `photo_editor_app`, `timeline_ui_consistency_contract_test`, `desktop_shell_smoke_test`, and the source-contract shard build successfully.
- Focused Timeline and desktop source contracts pass 2/2.
- QML lint reports no errors in the changed shared primitives or Timeline workspace.
- The focused dark-only workflow/visual harness passes and records calm, full, collapsed-panel, title-Inspector, search/marker-review, 4:5, 16:9, and 9:16 states in `/private/tmp/timeline-dark-final-proof-2`.

## Related proof gates

- [Timeline MVP Checklist](timeline-mvp-checklist.md)
- [Timeline Professional Parity Checklist](timeline-professional-parity-checklist.md)
- [Timeline Industry-Leading Checklist](timeline-industry-leading-checklist.md)
- [Timeline State-of-the-Art Backlog](timeline-state-of-the-art-backlog.md)
