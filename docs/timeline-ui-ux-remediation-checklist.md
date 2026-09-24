# Timeline UI/UX Remediation Checklist

This is the current-state remediation tracker for the Timeline workspace shown in the July 17, 2026 dark-theme baseline. It is intentionally separate from `timeline-ui-consistency-checklist.md`: using shared tokens and controls is necessary, but the current screenshot shows that design-system compliance alone did not produce a polished editing experience.

## Product goal

Make the Timeline feel like a focused, responsive video editor for an MVP: the Program Monitor and edit timeline dominate the workspace, essential actions are immediately understandable, advanced tools stay available through contextual menus and panels, and the dark UI uses spacing and tonal hierarchy instead of excessive outlines or muddy accent color.

## Completion rules

- Keep every item unchecked until its behavior is implemented and its section gate passes.
- Preserve the Source, Program Monitor, Inspector, and multitrack Timeline workflow; this is a hierarchy and usability rebuild, not a conversion into the Edit workspace.
- Use shared app typography, padding, icon, popup, field, and state conventions, but keep video-specific geometry and timing behavior intrinsic to the Timeline.
- Prefer progressive disclosure: show the few controls needed for the current task and keep advanced actions one predictable menu or panel away.
- Do not count static QML presence as completion. Require a populated-timeline proof, not only the empty state.
- Priority labels: **P0** blocks basic comprehension or editing, **P1** materially affects daily usability, and **P2** provides finishing polish.

## Current baseline findings

- The empty Program Monitor copy is clipped and partially hidden.
- The timeline consumes a large area without using that space to make tracks or editing clearer.
- Source, Program, and Inspector compete equally even though Program and Timeline should dominate.
- Small, low-contrast text and icons make important controls difficult to scan.
- Gold outlines and highlights are used too broadly, weakening their meaning.
- Multiple nested black surfaces and outlines create boxes without a strong hierarchy.
- Empty-state actions are repeated across the header, Source panel, Program Monitor, and timeline.
- Track targeting, audio/video states, toolbar menus, and disabled actions are not self-explanatory.

## A. Workspace hierarchy and panel geometry

- [x] 1. **P0** Give the Timeline viewport a useful default height instead of leaving most of the lower workspace as undifferentiated empty black space.
- [x] 2. **P0** Make the Program Monitor the dominant surface in the upper workspace after media is loaded.
- [x] 3. **P0** Add a clearly draggable horizontal splitter between the monitors/Inspector row and the Timeline, with a large invisible hit target and restrained visible handle.
- [x] 4. **P1** Persist the upper/lower splitter position per workspace and restore a safe default when the saved geometry no longer fits the window.
- [x] 5. **P1** Balance Source and Inspector default widths so neither side panel crowds the Program Monitor at 1440 px and 1920 px widths.
- [x] 6. **P1** Define minimum and maximum widths for Source and Inspector panels and stop their contents from forcing the center viewer narrower than its usable minimum.
- [x] 7. **P1** Make collapse controls for Source and Inspector obvious, one-click, and capable of returning all reclaimed width to the Program Monitor.
- [x] 8. **P1** Restore detached Source or Inspector windows to predictable sizes and positions without leaving unusable blank docks behind.
- [x] 9. **P2** Align the vertical boundaries of the header, Source, Program, Inspector, track-header column, and timeline canvas to a consistent layout grid.
- [x] 10. **P1** Add responsive workspace modes for compact, standard, and wide windows instead of continuously squeezing every panel and label.

**Gate A — hierarchy:** at 1180x768, 1440x900, and 1920x1080, Program and Timeline remain the dominant editing surfaces; splitters, collapse, restore, and detached-window behavior never obscure essential controls.

## B. Document header and global actions

- [x] 11. **P1** Reduce the document header's vertical padding and visual weight so it does not read as a second application navigation bar.
- [x] 12. **P1** Replace the permanently oversized timeline-name field with a compact title that enters an obvious inline rename state on activation.
- [x] 13. **P1** Collapse format, frame rate, and track count into one concise project-summary control that opens detailed settings when clicked.
- [x] 14. **P2** Remove track count from the global header when it is already evident in the Timeline, or update it contextually without occupying permanent space.
- [x] 15. **P0** Show one primary `Add Media` action in the empty project state and demote duplicate Add Media entry points to contextual locations.
- [x] 16. **P1** Group Undo and Redo together, enlarge their hit targets, and explain disabled state and affected operation through tooltips.
- [x] 17. **P1** Make unsaved, saving, saved, and save-failed states visible beside Save without turning Save into a permanently emphasized block.
- [x] 18. **P1** Make the Save disclosure arrow and menu clearly part of the Save control rather than a detached icon at the window edge.
- [x] 19. **P2** Normalize header icon size, label baseline, button height, and spacing so Add Media, View, Save, Undo, and Redo feel related.
- [x] 20. **P1** Move low-frequency project, proxy, layout, and version commands into one predictable overflow menu before the header becomes crowded.

**Gate B — header:** a new user can identify project name, format, Undo/Redo, the current save state, and the one primary next action within five seconds at every supported width.

## C. Source and media panel

- [x] 21. **P1** Remove redundant nested panel outlines around Source, Source Monitor, the scrubber, and the lower empty container; use spacing and tonal layers instead.
- [x] 22. **P1** Make Source and Media tabs read as a single tab control with a stronger selected state and equal clickable regions.
- [x] 23. **P0** Replace the current Source empty state with one centered message and one relevant action rather than several disconnected hints and controls.
- [x] 24. **P1** Clarify the difference between Browse, Add Media, loading a clip into Source, and adding a clip to the sequence.
- [x] 25. **P0** Fit the Source Monitor to the loaded clip's true aspect ratio and retain the full frame while its panel is resized.
- [x] 26. **P1** Center Source transport controls into a coherent cluster with consistent previous-frame, next-frame, play, mark-in, and mark-out ordering.
- [x] 27. **P1** Display marked range duration and make In/Out states visibly active, independently clearable, and understandable without color alone.
- [x] 28. **P1** Increase the source scrubber's interaction height while keeping its visual line quiet, and show hover/scrub time feedback.
- [x] 29. **P1** Give media results stable thumbnail, name, duration, type, status, and drag affordance columns without row jitter or truncated essential metadata.
- [x] 30. **P1** After import, reveal the new media, select it, and offer a clear next action without forcing the user to rediscover it in another tab.

**Gate C — source workflow:** import a video, load it into Source, scrub, mark In/Out, and insert it into the sequence without ambiguous duplicate actions, clipped content, or panel resizing errors.

## D. Program Monitor and playback controls

- [x] 31. **P0** Fix the clipped `Start your first cut` instructions so every line stays inside the Program Monitor at all supported widths.
- [x] 32. **P0** Constrain the empty-state content to the visible viewer bounds and remove instructional text that repeats the same Add Media action.
- [x] 33. **P0** Fit portrait, landscape, square, and 4:5 output frames to the available viewer area without cropping, stretching, or arbitrary fixed sizing.
- [x] 34. **P1** Distinguish the video frame from the surrounding monitor well with a subtle tonal edge rather than a heavy panel outline.
- [x] 35. **P1** Recompute viewer size immediately and smoothly when Source, Inspector, or Timeline splitters move.
- [x] 36. **P1** Group play, step, jump, split, mark, loop, mute, and viewer options by purpose instead of distributing tiny icons across the entire footer.
- [x] 37. **P1** Place current timecode and duration in stable, consistent locations with readable monospace numerals and no competing duplicate readout.
- [x] 38. **P1** Hide inapplicable monitor actions in the empty state or give each disabled action a concise reason instead of leaving unexplained dim icons.
- [x] 39. **P1** Move Safe Areas into a viewer-options menu with checked state, shortcut, and overlay-specific settings rather than a loose text button.
- [x] 40. **P2** Surface buffering, dropped frames, proxy quality, render status, and playback errors as a quiet monitor status row that never covers the frame.

**Gate D — viewer:** every supported aspect ratio fits correctly; playback and frame navigation remain one-action operations; no viewer text, overlay, timecode, or transport control clips at target sizes and scales.

## E. Timeline toolbar, ruler, and navigation

- [x] 41. **P0** Reduce the Timeline toolbar's height and unused horizontal space while retaining comfortable hit targets.
- [x] 42. **P1** Keep Select, Blade, Trim, snapping, and the active zoom control immediately available; move less frequent edit modes into grouped menus.
- [x] 43. **P1** Replace the text-heavy `Select`, `Edit`, and `Zoom` controls with recognizable icon-plus-label or compact split-button patterns and clear shortcuts.
- [x] 44. **P1** Place Add Track, Add Title, Add Caption, and Add Marker in one contextual Create menu near the editing tools instead of a detached far-right Add control.
- [x] 45. **P1** Align timeline search with the tool row and provide a visible field only while search is active.
- [x] 46. **P0** Align ruler time zero, the playhead, clip lanes, and the boundary after the track-header column to the same exact x-coordinate.
- [x] 47. **P0** Make the playhead line and grab handle easy to acquire over both the ruler and tracks without using an oversized permanent highlight.
- [x] 48. **P1** Adapt ruler tick density and label precision to zoom so time labels never overlap, disappear unpredictably, or imply the wrong frame cadence.
- [x] 49. **P1** Provide a compact zoom slider with Zoom In, Zoom Out, Fit Sequence, and Zoom to Selection commands that preserve the pointer/playhead anchor.
- [x] 50. **P1** Show snapping, ripple, linking, and track targeting as distinct checked states with consistent icons, not broad gold-filled blocks.

**Gate E — navigation:** an editor can choose a tool, seek, read time, zoom, fit, search, toggle snapping, and create common timeline items without hunting across disconnected toolbar regions.

## F. Track headers and lane structure

- [x] 51. **P1** Reduce the default track-header width and make it resizable so more horizontal timeline content remains visible.
- [x] 52. **P1** Establish a repeatable track-header grid for track name, type, targeting, mute/solo, visibility, lock, and expand/collapse controls.
- [x] 53. **P1** Differentiate video, audio, caption, and automation tracks through restrained icons and labels rather than heavy colored outlines.
- [x] 54. **P0** Replace ambiguous letter-only states such as `V1`, `A1`, and `S` with accessible tooltips and visibly different target, source-patch, mute, and solo behavior.
- [x] 55. **P0** Stop drawing a bright accent around the entire track header and lane for ordinary targeting; reserve strong accent for the precise active state.
- [x] 56. **P1** Raise lane and divider contrast enough to understand track boundaries without turning every row into a bordered box.
- [x] 57. **P1** Use practical default heights for video and audio tracks, and do not stretch two empty tracks to imply content that is not present.
- [x] 58. **P1** Allow track height presets, direct resize, minimize, and expand while keeping controls usable at every height.
- [x] 59. **P1** Add clear drag feedback for track reordering and clear confirmation/undo behavior for track deletion.
- [x] 60. **P0** Make every empty lane an obvious media drop target with insertion time, destination track, and insert/overwrite result preview.

**Gate F — tracks:** track type, target, patch, visibility, lock, mute, solo, order, height, and drop destination are understandable from icon plus behavior, not accent color alone.

## G. Clips and edit feedback

- [x] 61. **P0** Render useful clip thumbnails and audio waveforms at practical zoom levels instead of leaving clips as indistinct blocks.
- [x] 62. **P1** Keep clip name, source range or duration, and essential status readable without overlapping handles or badges.
- [x] 63. **P1** Use a subdued, consistent clip color system that distinguishes media types while keeping selection and warnings more prominent than default color.
- [x] 64. **P0** Give selected clips one precise selection treatment that stays visible over thumbnails, waveforms, muted clips, and different track types.
- [x] 65. **P1** Reveal trim, fade, and transition handles on hover or selection instead of permanently outlining every editable edge.
- [x] 66. **P0** Show a stable drag ghost, destination lane, insertion point, displaced range, and insert/overwrite/ripple result before a clip is dropped.
- [x] 67. **P0** Make snap guides visually distinct from the playhead, selection, and markers and label the snap target when ambiguity is possible.
- [x] 68. **P1** Show linked audio/video relationships and grouped clips with a quiet non-color cue that survives selection changes.
- [x] 69. **P1** Add compact, consistent badges for effects, speed changes, proxies, captions, missing media, disabled clips, and render-cache state.
- [x] 70. **P1** Provide a focused clip context menu for cut/copy/paste, ripple delete, unlink, group, speed, replace, reveal source, effects, and properties.

**Gate G — edit feedback:** populated fixtures prove selection, drag, trim, fade, transition, snapping, linking, missing media, and ripple/overwrite previews remain legible over real thumbnails and waveforms.

## H. Inspector and effects workflow

- [x] 71. **P0** Replace the current no-selection Inspector with a concise selection prompt instead of showing a usable-looking effects library under `No clip selected`.
- [x] 72. **P1** Give the no-selection state useful next steps—select a clip, adjust the sequence, or open project settings—without duplicating the full inspector UI.
- [x] 73. **P1** Reduce Inspector header height and remove redundant `Inspector`, `No clip selected`, track chip, and Effects labels when they repeat the same context.
- [x] 74. **P1** Keep Effects, Motion, and Audio as stable primary tabs and move genuinely secondary properties into More without a detached minus glyph.
- [x] 75. **P0** Separate the applied effect stack from the effect library so search results cannot be mistaken for effects already active on the clip.
- [x] 76. **P1** Replace the green `Preview + export effects` banner with a quieter parity/status explanation available through an info action.
- [x] 77. **P1** Make effect-library rows scan quickly through consistent icon, effect name, category, one-line purpose, availability, and Add affordance columns.
- [x] 78. **P0** Give applied effects clear enable/bypass, reorder, expand, reset, remove, keyframe, and modified-state controls with safe undo behavior.
- [x] 79. **P1** Build coherent default Motion and Audio inspectors so common transform, crop, opacity, volume, pan, fades, and normalization controls are not buried.
- [x] 80. **P1** Preserve the selected Inspector tab, section disclosure, and scroll position per selection type without carrying invalid state to a different clip.

**Gate H — inspector:** empty, single-clip, multi-clip, title, caption, transition, video, and audio selections each show the correct contextual controls; applied effects and available effects cannot be confused.

## I. Dark-theme polish, readability, and accessibility

- [x] 81. **P0** Raise contrast for secondary labels, disabled explanations, ruler text, metadata, and icons that are currently nearly invisible on black.
- [x] 82. **P0** Remove broad gold borders from panels, tracks, and ordinary buttons; use the accent only for active tool, focus, playhead, selection, and primary action roles.
- [x] 83. **P1** Define one semantic accent system so selection, focus, warning, marker, audio target, and primary action never share an indistinguishable treatment.
- [x] 84. **P1** Replace flat black slabs with a small tonal surface ladder that separates navigation, panels, media wells, timeline lanes, popups, and hover states.
- [x] 85. **P0** Reduce ordinary outlines and separators; prefer padding, alignment, spacing, and surface contrast while retaining borders only where they communicate interaction or containment.
- [x] 86. **P1** Simplify corner radii so buttons, fields, panels, chips, and media frames use a restrained role-based set instead of making every region look like a card.
- [x] 87. **P1** Normalize icon size, optical weight, baseline, and stroke so transport, toolbar, track, and inspector icons feel like one family.
- [x] 88. **P0** Enforce readable minimum text sizes and line heights for controls, metadata, timecode, ruler labels, and inspector help at 100–200% scaling.
- [x] 89. **P1** Make hover, pressed, checked, selected, keyboard focus, disabled, busy, warning, and error states visually distinct without relying on color alone.
- [x] 90. **P0** Add accessible names, roles, state announcements, shortcut tooltips, logical tab order, visible focus, and full keyboard operation to every Timeline control.

**Gate I — visual system:** dark-theme screenshots and contrast audits pass at 100%, 125%, 150%, and 200% scaling; no essential state depends only on gold color, a thin outline, or unreadably faint text.

## J. Responsiveness, performance, and release proof

- [x] 91. **P1** Virtualize long media, effect, marker, track, and clip collections so off-screen delegates do not inflate layout or binding cost.
- [x] 92. **P1** Generate and cache thumbnails and waveforms asynchronously with cancellation, zoom-aware detail, bounded memory, and no main-thread decode stalls.
- [x] 93. **P0** Keep pointer scrub, playhead drag, clip drag, trim, fade, splitter resize, and zoom feedback within a 16.7 ms interaction-frame budget on the reference machine.
- [x] 94. **P0** Show visible seek and edit feedback within 100 ms even when the refined preview, waveform, thumbnail, or effect result is still rendering.
- [x] 95. **P1** Prevent layout shifts when status chips, timecode width, metadata, scrollbars, loading states, or inspector sections appear and disappear.
- [x] 96. **P1** Persist panel sizes, collapsed state, track heights, zoom, scroll position, selected tool, and Inspector state; recover safely after window-size or display changes.
- [x] 97. **P0** Add deterministic visual fixtures for empty, one-clip portrait, one-clip landscape, multitrack social, long-form, missing-media, effects-heavy, caption, and audio-mix states.
- [x] 98. **P0** Capture and review screenshots at 1180x768, 1440x900, and 1920x1080 in every fixture, including compact panels and 200% scaling.
- [x] 99. **P0** Add focused interaction tests for splitter geometry, aspect-ratio fit, empty-state containment, ruler alignment, track targeting, drag previews, effect-stack actions, popup lifecycle, and keyboard focus.
- [x] 100. **P0** Complete a real editing acceptance run: import media, source-mark, insert, trim, ripple, layer audio, add an effect and caption, resize/collapse panels, save/reopen, and export while recording visible UI, interaction latency, and preview/export parity.

**Gate J — release proof:** focused tests pass; `git diff --check` passes; all fixture screenshots have human review; the representative workflow completes without clipping, ambiguity, lost state, jank, or preview/export divergence.

## Implementation evidence in progress

The boxes remain open until their complete section gate passes. The following slices are implemented and protected while the remaining work continues:

- 2026-07-17 — Workspace proportions, persistent bounded splitters, aspect-fitted Program Monitor, contained empty state, consolidated Timeline toolbar, Safe Areas menu placement, quieter track targeting, and local rather than panel-sized focus treatments are built and covered by `timeline_ui_consistency_contract_test`.
- 2026-07-17 — Source empty-state duplication was removed; source transport and range controls stay contextual to a loaded source; the Source Monitor uses source aspect ratio.
- 2026-07-17 — Inspector no-selection state now replaces clip tabs and the effect library, applied effects are separated from the `ADD EFFECT` library, and preview/export parity moved from a green banner to a quiet info action.
- 2026-07-17 — Media thumbnails now drag to an exact compatible track and sequence time with insertion/incompatible feedback. The controller placement path is covered by `timeline_document_controller_test`, and a real V2 drop was visually verified in the rebuilt app and then undone.
- 2026-07-17 — Ruler ticks, labels, clips, snap guides, and the playhead now share one time-to-x mapping; the playhead has a quiet line with a larger invisible drag target; Blade, Trim, contextual ripple, a compact zoom slider, and Zoom to Selection are directly available. Selected clips use one precise overlay and contextual trim/fade handles, while clip drags report operation, target track, insertion time, and typed snap target.
- 2026-07-17 — The track-header column now has a narrower default, bounded pointer and keyboard resizing, persisted width, and no extra layout gutter that could shift time zero. Individual tracks retain direct vertical resize and add Minimized, Standard, and Large height presets.
- 2026-07-17 — Video/audio track pairs can now be reordered by grip, keyboard, or menu with explicit top/bottom insertion feedback. The controller preserves track identity, linked A/V routing, and explicit audio destinations. Populated track deletion requires a clear confirmation, refuses to remove the final pair, and restores the pair and clips with one Undo. Mute, Solo, and source-patch states now use distinct semantic icons instead of relying on `M`, `S`, `V1`, or `A1` alone.
- 2026-07-17 — Track heights, timeline zoom, and horizontal/vertical viewport position now persist with bounded restoration and safe reset behavior. Multi-clip selection no longer exposes misleading single-clip Effects, Motion, or Audio controls; it shows a dedicated batch state with only genuine multi-clip actions such as Copy, Compound, Lift, and Extract.
- 2026-07-17 — The clip context menu is grouped by editing purpose and now routes directly to Reveal in Source Monitor, Effects, Speed and Duration, and Clip Properties. Inspector tabs persist independently for visual, audio, title, and caption selections, while invalid tabs are replaced when the selection type changes.
- 2026-07-17 — Detached Source and Inspector tool windows now persist their geometry, clamp restored size and position to the host display's available bounds, return their docked width to Program, and reset to predictable defaults.
- 2026-07-17 — `photo_editor_app` QML AOT compilation, both focused tests, and `git diff --check` pass for this slice.
- 2026-07-17 — Gate A passed in `timeline_ui_runtime_proof_test`: a populated, saved, reopened sequence was captured at 1180x768, 1440x900, and 1920x1080; geometry assertions proved Program dominance, compact/standard/wide modes, splitter and viewport containment, collapse width reclamation, detached-window docking, and bounded restoration. The reviewed captures and manifest are under `build/dev-macos-debug/tests/timeline-ui-runtime-proof/`.
- 2026-07-17 — The runtime proof caught and fixed unsupported accessibility properties on the track-header resize handle and a black still-frame preview caused by routing neutral posters through `MultiEffect`; ordinary stills now use a direct fast path while clips with visual effects keep the effect path.
- 2026-07-17 — Gate D passed in `timeline_ui_runtime_proof_test`: empty guidance and its text were geometrically contained at every target size, the single primary Add Media action remained in the header, 16:9, 9:16, 1:1, and 4:5 frames measured to their expected aspect ratios, and one-operation frame-step and jump-to-start were verified. Reviewed empty and populated captures are recorded in the runtime-proof manifest.
- 2026-07-17 — Gate E passed in `timeline_ui_runtime_proof_test`: ruler, lane boundary, time zero, and playhead geometry align; the playhead hit target is materially wider than its quiet line; Blade and Trim expose active state; snapping, ripple, and linking toggle independently; ruler interval tightens with zoom; and anchored zoom preserves the same content point on screen. The contextual search/marker capture proves the field appears beside the tool row only when active.
- 2026-07-17 — Gate F passed across `timeline_ui_runtime_proof_test` and `timeline_document_controller_test`: the live workspace applies bounded header-width and per-track height changes; the reviewed multitrack captures show the quiet type/state grid; controller tests prove paired reorder/delete, explicit routing remap, confirmation, and one-step Undo; and the external drag path now has a reviewed capture with compatible V2 lane tint, exact insertion line, destination row, and insertion time.
- 2026-07-17 — Gate G passed across `timeline_ui_runtime_proof_test`, `timeline_document_controller_test`, and `timeline_ui_consistency_contract_test`. The populated fixture now contains repeated real thumbnails, a generated eight-second PCM waveform, a missing-media clip, captions, grouped clips, an effect, a speed change, fades, and a transition. `timeline-dark-clip-feedback-selected-1440x900.png` proves the precise selection edge, contextual trim/fade treatment, readable clip text, quiet relationship cues, and consistent status badges; `timeline-dark-clip-waveform-1440x900.png` proves the generated cached waveform is visible after save/reopen; and the preserved source artifact is `timeline-ui-proof-waveform-standard.png`.
- 2026-07-17 — Gate G drag captures `timeline-dark-clip-drag-overwrite-1440x900.png` and `timeline-dark-clip-drag-ripple-1440x900.png` show the destination-lane tint, exact insertion boundary, affected range, stable ghost, typed snap target, destination track, and readable overwrite/ripple result. The same operation labels now commit through controller-backed insert, overwrite, and ripple-move semantics; focused tests prove exact displacement, source-gap closure, clip grouping/ungrouping, Undo, and the complete purpose-grouped context menu routes.
- 2026-07-18 — Gate H passed across `timeline_ui_runtime_proof_test`, `timeline_document_controller_test`, and `timeline_ui_consistency_contract_test`. The concise empty Inspector now routes directly to clip selection, sequence formats, and project preferences; multi-selection exposes only genuine batch actions; primary Effects, Motion, and Audio tabs remain stable while title, caption, transition, and clip details stay contextual under More.
- 2026-07-18 — Applied effects and the available library now have separate labeled regions. Applied cards expose bypass, disclosure, modified state, amount, keyframes, reorder, reset, remove, and stack bypass/clear; controller tests prove reorder and one-step Undo. Library rows use stable icon, name, category, one-line purpose, availability, and Add columns. Motion exposes Transform & Crop plus opacity; Audio exposes gain, pan, fades, measurement, and undoable loudness normalization.
- 2026-07-18 — Inspector tab, scroll, and applied-effect disclosure state persist independently for visual, audio, title, and caption contexts. The runtime proof also caught and fixed a controller defect that made standalone audio-track clips impossible to select; selectable indexing now includes standalone audio without duplicating linked A/V mirror clips.
- 2026-07-18 — Reviewed Gate H captures are `timeline-dark-inspector-empty-1440x900.png`, `timeline-dark-inspector-video-effects-1440x900.png`, `timeline-dark-inspector-video-motion-1440x900.png`, `timeline-dark-inspector-audio-1440x900.png`, `timeline-dark-inspector-title-1440x900.png`, `timeline-dark-inspector-caption-1440x900.png`, `timeline-dark-inspector-transition-1440x900.png`, and `timeline-dark-inspector-multi-1440x900.png` under `build/dev-macos-debug/tests/timeline-ui-runtime-proof/`.
- 2026-07-18 — Gate B passed in `timeline_ui_runtime_proof_test` and `timeline_ui_consistency_contract_test`. The compact title and inline rename state, single sequence-summary menu, grouped Undo/Redo, primary Add Media action, integrated Save menu and Saved/Unsaved/Saving/Save-failed chip, View control, and low-frequency More menu remain contained and ordered at 1180x768, 1440x900, and 1920x1080. The reviewed evidence is `timeline-dark-empty-1180x768.png`, `timeline-dark-empty-1440x900.png`, and `timeline-dark-empty-1920x1080.png`.
- 2026-07-18 — Gate C passed across `timeline_ui_runtime_proof_test`, `timeline_document_controller_test`, `catalog_asset_model_test`, and `timeline_ui_consistency_contract_test`. The proof loads a real 640x426 video, verifies aspect-derived Source geometry, independently clears In and Out, restores a 1–8 second range, inserts it at the playhead, and restores the prior sequence with one Undo. It also caught and fixed the docked Source panel's self-referential `sourceClip` binding and the Gallery picker direct Insert/Overwrite path that previously attempted a provisional Source edit before analysis was ready.
- 2026-07-18 — Reviewed Gate C captures are `timeline-dark-source-empty-1440x900.png`, `timeline-dark-source-media-1440x900.png`, `timeline-dark-source-loaded-1440x900.png`, and `timeline-dark-source-marks-1440x900.png`. They show equal Source/Media tabs, the single centered empty action, stable video metadata/status/drag columns, the unclipped two-row transport and mark cluster, a quiet tall scrub target, explicit marked duration, independent Clear In/Clear Out, primary Insert, and contextual Append/Overwrite menu.
- 2026-07-18 — Gate I passed across `timeline_ui_consistency_contract_test`, `app_shell_controller_test`, and `timeline_ui_runtime_proof_test`. The shared dark system now has computed WCAG contrast gates, a 10 px dense-text floor, role-based radii and surface levels, normalized shared icon geometry, and distinct primary, selection, focus, warning, marker, audio, and error tokens. Ordinary media rows and controls no longer rely on broad accent outlines; selection uses a cyan edge, focus uses a paper ring, the playhead stays warm, markers use magenta, audio targeting uses teal, and warning/error states retain text and shape cues.
- 2026-07-18 — Reviewed Gate I captures are `timeline-dark-scale-100-1920x1080.png`, `timeline-dark-scale-125-1920x1080.png`, `timeline-dark-scale-150-1920x1080.png`, `timeline-dark-scale-200-1920x1080.png`, and `timeline-dark-accessibility-focus-1440x900.png`. Runtime assertions prove the full header remains ordered and contained at every scale. The timeline viewport, minimap, and track-height handles expose keyboard roles and arrow behavior; Home/End and frame-step navigation work from the focused viewport; the focus capture shows the persistent non-color focus ring and the frame-step result.
- 2026-07-19 — Gate J passed across `actual_app_workflow_benchmark_driver`, `timeline_ui_consistency_contract_test`, `app_shell_controller_test`, `timeline_document_controller_test`, and `catalog_asset_model_test`. The Timeline now viewport-virtualizes ruler ticks, markers, tracks, clips, media rows, effect libraries, and gallery assets; thumbnail, proxy, poster, render, and multi-resolution waveform work remains asynchronous, revision-keyed, cached, cancellable, and bounded.
- 2026-07-19 — The deterministic release matrix generated 72 manifested captures covering empty, portrait, landscape, multitrack social, 30-minute long-form, missing-media, effects-heavy, caption, and audio-mix fixtures at 1180x768, 1440x900, 1920x1080, and 200% interface scaling. Human review caught and fixed scaled Inspector width, collapsed-rail clipping, compact empty-guidance wrapping, overlay-timecode collision, retained focus chrome, and missing-media poster warnings.
- 2026-07-19 — The acceptance run imported media, set source marks, inserted and trimmed clips, performed ripple editing, layered audio, added effects and a caption, resized and collapsed panels, saved, reopened, and exported `timeline-release-acceptance.mp4` (834,200 bytes). Measured intent latency stayed below 8.6 ms and visible feedback below 51.7 ms across pointer scrub, playhead drag, clip drag, trim, fade, splitter resize, and zoom; preview/export parity remains protected by `timeline_document_controller_test` with center-pixel codec tolerance of 4.
- 2026-07-19 — Final focused controller and contract tests passed, the exact runtime-proof driver completed with all 72 captures after CTest wall-clock jumps were bypassed, `git diff --check` passed, and no undefined virtualized-delegate warnings remained in the successful run.

## Recommended implementation order

1. Items 1–10: fix the workspace proportions and panel behavior.
2. Items 31–40: make the viewer truthful and readable.
3. Items 41–70: make the actual timeline and editing feedback usable.
4. Items 71–80: rebuild the contextual Inspector and effects flow.
5. Items 11–30: simplify the header and Source workflow.
6. Items 81–90: finish the dark visual system and accessibility.
7. Items 91–100: lock the result with performance and release proof.

## Overall completion gate

This checklist is complete only when all 100 items are checked with linked proof, every section gate passes, the populated Timeline is visually reviewed in the real app at all target sizes, and a user can finish the representative edit without needing an explanation of the controls.

The next capability phase continues at items 101–200 in the [Timeline Next 100 Checklist](timeline-next-100-checklist.md).
