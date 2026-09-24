# Timeline UI Consistency Checklist

This checklist brings the Timeline workspace into the same visual system as the rest of Framehouse without turning it into the Edit workspace or weakening its video-editor workflow.

The product contract is:

> Keep Source on the left, Program Monitor in the center, the contextual Inspector on the right, and the multitrack Timeline below. Standardize the ordinary buttons, fields, panels, popups, typography, spacing, radii, icons, and interaction states with the app's existing shared components and `Theme.qml` tokens.

## Completion rules

Use `[ ]` until the item is implemented and its stated gate has passed. Use `[x]` only after proof exists.

- Preserve all existing Timeline editing behavior unless a checklist item explicitly describes a behavior correction.
- Reuse `Theme.qml` and the shared QML components. Do not introduce a second Timeline-only palette, font scale, spacing scale, or control family.
- Treat colors, geometry, and overlays that communicate media or time as editor content, not ordinary app chrome. Those may remain Timeline-specific when documented below.
- Replace hard-coded values only when they represent app chrome. Do not mechanically replace playhead width, ruler cadence, lane height, waveform geometry, clip duration mapping, trim-handle width, safe-area geometry, or user-selected title colors.
- Validate dark and light appearances, hover, pressed, checked, selected, focused, disabled, loading, empty, offline, and error states where applicable.
- Require focused QML contracts, lint/build proof, representative screenshots, and a real editing workflow before declaring the checklist complete.

### MVP applicability boundary

This pass styles and verifies every state exposed by the current Timeline MVP. It does not invent backend features solely to create visual states: mixed/inherited/keyframed properties and proxy/grouped/nested/adjustment clips are not present in the current controller model. Items 72 and 87 therefore apply to the supported default, modified, reset, selected, disabled, linked, muted, offline/missing, title, caption, transition, and ordinary media states; unavailable future states retain explicit disabled or empty guidance instead of simulated controls.

## Protected video-editor structure

The following are non-goals for this work:

- Do not copy the Edit workspace layout, tool rail, inspector arrangement, or image-editing workflow.
- Do not remove or merge the Source panel, Program Monitor, Effects Inspector, or multitrack Timeline.
- Do not replace tracks and clips with a filmstrip, card board, or social-template wizard.
- Do not hide professional editing information merely to make the workspace look simpler.
- Do not alter timeline math, edit semantics, playback behavior, preview/export parity, or project persistence as part of a visual refactor.
- Do not force meaningful media colors through generic surface tokens. Track identity, clip type, selection, markers, playhead, waveforms, and warning states must remain immediately distinguishable.

## Audited implementation surface

- `apps/desktop/qml/workspaces/TimelineView.qml`
- `apps/desktop/qml/components/TimelineSourcePanel.qml`
- `apps/desktop/qml/components/TimelineEffectsPanel.qml`
- `apps/desktop/qml/components/TimelineClipContent.qml`
- `apps/desktop/qml/components/TimelineTrackHeader.qml`
- `apps/desktop/qml/components/TimelineTrackToggleButton.qml`
- `apps/desktop/qml/components/TimelineMediaAssetRow.qml`
- `apps/desktop/qml/components/TimelineEffectLibraryRow.qml`
- `apps/desktop/qml/components/TimelineTransitionLibraryRow.qml`
- `apps/desktop/qml/components/TimelineMarkerCard.qml`
- `apps/desktop/qml/components/TimelineMarkerPin.qml`
- `apps/desktop/qml/components/Theme.qml`
- Shared controls including `ActionButton`, `AppIcon`, `FilterChip`, `InfoPill`, `InspectorSection`, `InspectorSlider`, `MenuButton`, `MiniButton`, `PopupMenuRow`, `PopupSurface`, `SectionHeader`, `SplitButton`, `StatusChip`, `ToneComboBox`, `ToneTextField`, `ToolButton`, `ToolOptionToggle`, and `ToolValueSlider`.

## A. Scope and visual contract

- [x] 1. Add a named Timeline UI consistency contract near the root of `TimelineView.qml` so shell tests can verify that the workspace intentionally follows the shared design system.
- [x] 2. Document the protected four-region video-editor layout in the QML contract: Source, Program Monitor, Inspector, and multitrack Timeline.
- [x] 3. Inventory every user-visible Timeline surface and assign it one role: app chrome, editor canvas, media content, transient overlay, or semantic status.
- [x] 4. Inventory every raw color, font size, radius, margin, spacing, control height, border width, and animation duration in the audited QML files.
- [x] 5. Classify each raw value as a shared-theme replacement, an intrinsic timeline measurement, a user/content value, or a documented exception.
- [x] 6. Record the allowed intrinsic exceptions beside the relevant properties so future cleanup does not incorrectly normalize timeline math or media visuals.
- [x] 7. Capture baseline screenshots at 1180x768, 1440x900, and 1920x1080 before changing visual geometry.
- [x] 8. Capture the baseline keyboard focus order and popup lifecycle so the styling pass cannot silently regress interaction.

**Gate A — audited contract:** the inventory covers every file listed above, every raw value has a disposition, baseline screenshots exist, and a focused contract test protects the four-region layout.

## B. Theme, typography, spacing, and surfaces

- [x] 9. Route all ordinary Timeline panel backgrounds through the existing `Theme.qml` surface tokens instead of local or literal colors.
- [x] 10. Route ordinary separators, outlines, and dividers through `Theme.lineSoft` or `Theme.lineStrong` according to hierarchy.
- [x] 11. Route primary, secondary, muted, and faint app-chrome text through the matching shared text tokens.
- [x] 12. Replace Timeline-only accent choices for ordinary navigation and controls with the app's existing accent, focus, success, warning, and error roles.
- [x] 13. Remove literal chrome colors such as `#0b1014`, `#4f1713`, `#e58767`, and `#ffd7ca` after mapping them to semantic tokens; retain user-selected title colors as content values.
- [x] 14. Use `Theme.alpha(...)` for translucent chrome and state layers instead of embedding separate translucent color literals.
- [x] 15. Use `Theme.fontUi` for ordinary labels, fields, buttons, tabs, menus, inspector controls, and helper text.
- [x] 16. Reserve `Theme.fontDisplay` for genuine workspace or section headings rather than ordinary control labels.
- [x] 17. Use `Theme.fontMono` for timecode, frame values, timeline measurements, and other fixed-width timing data.
- [x] 18. Replace ad hoc chrome font sizes with the existing app type scale and keep a readable minimum for dense labels.
- [x] 19. Standardize font weight by semantic role so selected tabs, headings, buttons, metadata, and disabled labels match the rest of the app.
- [x] 20. Add elision and tooltips to truncated clip-independent labels, media names, effect names, and narrow-panel control text.
- [x] 21. Replace raw panel margins and padding with `Theme.panelPadding*`, `Theme.sectionPadding*`, and related shared spacing tokens.
- [x] 22. Replace raw row and column spacing with the closest semantic `Theme.gap*` or `Theme.space*` token.
- [x] 23. Replace ordinary chrome radii with `Theme.radiusControl`, `Theme.radiusChip`, `Theme.radiusPanel`, or `Theme.radiusSurface` according to role.
- [x] 24. Standardize ordinary control heights, icon sizes, border widths, and animation durations with the shared theme while leaving intrinsic timeline geometry unchanged.

**Gate B — token compliance:** an automated scan reports no unexplained literal chrome colors, font families, chrome radii, or chrome spacing values in the audited files; dark and light screenshots retain clear hierarchy.

## C. Shared controls and interaction states

- [x] 25. Replace raw ordinary text inputs with `ToneTextField`, adding a shared multiline equivalent only if `TextArea` behavior cannot be expressed by an existing component.
- [x] 26. Replace raw ordinary combo boxes and selectors with `ToneComboBox` or the established shared selector pattern.
- [x] 27. Replace one-off push buttons with `ActionButton`, `MiniButton`, or `ToolButton` based on importance and density.
- [x] 28. Replace binary toolbar states with `ToolOptionToggle` so snapping, linking, ripple, visibility, and similar controls share checked-state behavior.
- [x] 29. Replace one-off split tool selectors with `SplitButton` or `MenuButton` and the app's standard disclosure icon treatment.
- [x] 30. Replace one-off filter and tab pills with `FilterChip` when they have the same selection semantics.
- [x] 31. Replace ordinary metadata badges with `InfoPill` and semantic progress or health labels with `StatusChip`.
- [x] 32. Replace inspector group shells with `InspectorSection` and numeric sliders with `InspectorSlider` or `ToolValueSlider` as appropriate.
- [x] 33. Use `AppIcon` for all ordinary chrome icons and remove local text glyphs or inconsistent icon sizing where a shared icon exists.
- [x] 34. Give every interactive control the same hover, pressed, checked, focused, disabled, and busy visual grammar as its shared counterpart.
- [x] 35. Ensure disabled controls remain legible, explain why they are unavailable through a tooltip or nearby state, and cannot receive misleading activation feedback.
- [x] 36. Make the full visible button or row target clickable and keep compact Timeline targets at least 32 logical pixels unless the target is an intrinsic edit handle.
- [x] 37. Standardize focus rings through the shared focus token without using a permanent bright outline that resembles keyboard focus.
- [x] 38. Standardize control transitions with shared animation durations and suppress nonessential motion when reduced-motion behavior is requested.

**Gate C — control parity:** each ordinary Timeline control maps to a shared primitive or has a documented exception; a state matrix proves hover, press, check, focus, disabled, and busy behavior.

## D. Timeline setup, document header, and global actions

- [x] 39. Restyle the new-timeline/setup screen with the same surface hierarchy, heading scale, field spacing, and primary-action treatment used elsewhere in the app.
- [x] 40. Restyle recent-timeline cards with shared surfaces, padding, radii, metadata typography, hover state, selection state, and empty-state treatment.
- [x] 41. Convert the editable timeline name in the workspace header to the shared field pattern with consistent height, inset, focus ring, validation, and save feedback.
- [x] 42. Align Format and Tracks metadata pills to the shared `InfoPill` sizing and baseline rules.
- [x] 43. Normalize Undo, Redo, New, Add Media, Source, Inspector, Save, and Jobs buttons to the correct shared button variants and icon spacing.
- [x] 44. Keep only the highest-priority action visually primary in a header state and prevent Add Media, Save, and Jobs from competing equally.
- [x] 45. Give saving, saved, unsaved, rendering, offline, and failure statuses consistent `StatusChip` semantics and accessible text.
- [x] 46. Move low-frequency header actions into the established overflow/menu pattern before the header becomes cramped.
- [x] 47. Define responsive header behavior so the timeline name, timing metadata, and essential editing actions remain usable at the minimum supported width.
- [x] 48. Make setup, recent documents, and header actions follow a logical left-to-right keyboard focus order with visible focus restoration after dialogs close.

**Gate D — shell consistency:** setup, recent documents, and the live document header look like the same application at all target widths without changing how a timeline is created, opened, named, saved, or managed.

## E. Source and media panel

- [x] 49. Restyle `TimelineSourcePanel.qml` outer chrome with shared panel surface, border, radius, and padding tokens while preserving its video-specific layout.
- [x] 50. Standardize the Source and Media tab treatment with the app's shared tab/filter selection pattern and equal hit targets.
- [x] 51. Restyle Browse, Add, Insert, Overwrite, and source transport actions with the correct shared button hierarchy and disabled explanations.
- [x] 52. Keep the source preview canvas media-black where needed, but separate it visually from the surrounding themed panel chrome.
- [x] 53. Render source timecode and in/out timing with the shared monospace timing style and consistent numerical alignment.
- [x] 54. Restyle `TimelineMediaAssetRow.qml` with shared row padding, text hierarchy, hover, selected, focused, offline, unsupported, and drag states.
- [x] 55. Give media thumbnails, type badges, duration labels, and status indicators consistent alignment and reserve stable space to prevent row jitter.
- [x] 56. Replace source-panel loading, empty-library, unsupported-media, and offline-media presentations with the app's established empty/error state grammar and useful recovery actions.
- [x] 57. Standardize Source panel scrolling, scrollbars, truncation, resize boundaries, collapsed state, and restored width without obscuring media or controls.

**Gate E — source workflow:** a user can browse, preview, mark, insert, and overwrite media at every target size; all states look app-consistent and the source canvas remains recognizably video-editor-specific.

## F. Program Monitor and transport

- [x] 58. Restyle Program Monitor chrome, header, footer, border, and surrounding padding with shared surface tokens while keeping the image area neutral black.
- [x] 59. Keep safe areas, transform overlays, guides, and frame boundaries visually subordinate to the video but distinct from ordinary panel borders.
- [x] 60. Standardize play, pause, seek, previous/next frame, jump, loop, mute, and monitor options with shared icon buttons and consistent tooltip language.
- [x] 61. Make the current transport state unmistakable through icon, checked state, and accessible label rather than color alone.
- [x] 62. Render Program timecode, duration, frame position, and playback-rate values with the shared monospace timing style.
- [x] 63. Replace one-off monitor sliders and value controls with `ToolValueSlider`, `ToneComboBox`, or the matching shared primitive.
- [x] 64. Present dropped-frame, unavailable-media, decode, and preview-render failures with semantic status styling that does not cover critical transport controls.
- [x] 65. Normalize monitor zoom, fit/fill, quality, and overlay controls into a quiet secondary hierarchy that does not compete with playback.
- [x] 66. Preserve monitor focus, keyboard transport shortcuts, pointer scrubbing, and full-frame visibility at all supported panel sizes and display scales.

**Gate F — monitor clarity:** playback and frame navigation remain one-action operations; video content dominates the panel; error, focus, and transport states are unambiguous in dark and light modes.

## G. Effects Inspector

- [x] 67. Restyle `TimelineEffectsPanel.qml` outer surface, header, tab row, dividers, padding, and scrollbar with shared inspector tokens.
- [x] 68. Convert inspector tabs to the established selection component and keep their active indicator consistent with the rest of the app.
- [x] 69. Use `SectionHeader` and `InspectorSection` for Video, Audio, Transform, Opacity, Speed, Transition, Title, Caption, and effect groups.
- [x] 70. Replace raw inspector fields, sliders, combo boxes, toggles, and reset buttons with their shared equivalents.
- [x] 71. Align inspector labels, values, units, steppers, reset affordances, and disclosure arrows to one repeatable grid.
- [x] 72. Make mixed values, inherited/default values, modified values, keyframe availability, disabled properties, and reset states visually distinct without adding clutter.
- [x] 73. Restyle `TimelineEffectLibraryRow.qml` and `TimelineTransitionLibraryRow.qml` with the same shared row anatomy as media and menu rows.
- [x] 74. Preserve content-specific title and caption color swatches while styling the picker shell, labels, inputs, and popup through the shared system.
- [x] 75. Replace inspector empty-selection, unsupported-property, missing-effect, and load-failure states with concise guidance and the correct recovery action.
- [x] 76. Ensure inspector sections remember disclosure state where appropriate and remain usable when the panel is narrow, collapsed, reopened, or restored from settings.

**Gate G — inspector consistency:** every supported selection exposes a stable, aligned inspector; shared controls cover ordinary properties; title, caption, effect, and transition behavior is unchanged.

## H. Timeline command area, ruler, tracks, clips, and feedback

- [x] 77. Restyle the timeline command bar with shared surface, spacing, divider, button, selector, toggle, slider, and focus treatments.
- [x] 78. Group edit tools, insert/overwrite actions, snapping/link/ripple options, add-track actions, markers, search, captions/titles, zoom, and Fit by task with restrained separators.
- [x] 79. Use the same selected-tool treatment for pointer, blade, trim, slip, roll, and other editing modes without removing their video-editor-specific icons or shortcuts.
- [x] 80. Restyle marker search, clip search, review, filter, and result panels with `ToneTextField`, `FilterChip`, `PopupSurface`, `PopupMenuRow`, and shared empty states.
- [x] 81. Restyle the timeline viewport shell and corner area with shared surfaces and borders while reserving strong focus color for actual keyboard focus.
- [x] 82. Keep ruler height, tick cadence, time mapping, and playhead geometry intrinsic, but apply shared mono typography and semantic line/text colors.
- [x] 83. Restyle `TimelineTrackHeader.qml` with shared padding, typography, selection, focus, hover, locked, muted, soloed, hidden, armed, and disabled states.
- [x] 84. Restyle `TimelineTrackToggleButton.qml` as a dense shared toggle with a clear state, tooltip, accessible name, and full hit target.
- [x] 85. Keep lane height, track spacing, resize handles, and timeline-to-pixel mapping intrinsic while making drag affordances and hover feedback consistent.
- [x] 86. Restyle `TimelineClipContent.qml` text, thumbnails, duration metadata, badges, borders, selected state, disabled state, and focus state while retaining meaningful clip-type identity colors.
- [x] 87. Make offline, missing, proxy, muted, disabled, linked, grouped, nested, title, caption, transition, and adjustment clip states readable through icon plus text or pattern, not color alone.
- [x] 88. Standardize trim handles, fade handles, transition regions, speed badges, effect badges, and hover feedback while preserving their precise editing geometry and pointer behavior.
- [x] 89. Preserve waveform truth and density while tuning waveform, clip-background, and selection contrast for both themes and all supported zoom levels.
- [x] 90. Keep playhead, snap guide, range selection, marquee, drop target, insertion indicator, drag ghost, and overwrite/ripple previews semantically distinct and above the correct z-layers.
- [x] 91. Restyle `TimelineMarkerPin.qml` and `TimelineMarkerCard.qml` with shared typography, popup/card chrome, focus, hover, selected, edit, validation, and color-label treatment.

**Gate H — editing clarity:** screenshots and interaction tests prove that tools, time, tracks, clips, selection, trim points, markers, waveforms, playhead, snapping, and drag outcomes remain immediately readable at minimum and maximum useful zoom.

## I. Popups, responsiveness, accessibility, and persistence

- [x] 92. Replace raw ordinary `Popup` shells with `PopupSurface` and ordinary menu rows with `PopupMenuRow`, using shared padding, radius, shadow, border, and animation behavior.
- [x] 93. Close Timeline popups and expanded tool menus on workspace switch, Escape, outside activation, document close, or replacement by another popup; never leave a Timeline popup floating over another workspace.
- [x] 94. Constrain and reposition menus, pickers, tooltips, and editors so they remain inside the usable window, avoid clipping, and restore focus to their invoker when closed.
- [x] 95. Verify minimum panel sizes, resizable splitters, collapsed panels, restored settings, header overflow, and scroll behavior at 1180x768, 1440x900, and 1920x1080.
- [x] 96. Verify dark and light themes plus 100%, 125%, 150%, and 200% display scaling without clipped labels, blurred one-pixel lines, broken hit targets, or unreadable overlays.
- [x] 97. Add accessible names, roles, checked/selected state, shortcut descriptions, logical tab order, keyboard operation, visible focus, and non-color cues to every ordinary Timeline control.

**Gate I — resilient interaction:** popup lifecycle, focus restoration, panel persistence, target sizes, keyboard access, screen-reader metadata, theme switching, scaling, and supported window sizes pass focused checks.

## J. Verification and release proof

- [x] 98. Add focused QML contract tests for shared-component adoption, token usage, protected video-editor structure, popup lifecycle, focus order, and the documented intrinsic exceptions.
- [x] 99. Run QML lint, the desktop app build, focused desktop-shell and Timeline controller tests, `git diff --check`, and an exact sequential checklist-number validation with no new failures.
- [x] 100. Complete and record a real app proof: open or create a timeline, add media, source-mark and insert a clip, use transport, select and trim a clip, change an inspector property, add a marker, use search, resize/collapse/restore panels, switch workspaces with a popup open, save, reopen, and compare screenshots at all required sizes and themes.

**Gate J — release:** all 100 numbered items and Gates A-I are checked with linked evidence; no editing behavior, saved-state, keyboard, playback, preview, or export regression is present; before/after screenshots demonstrate a cohesive application without erasing the Timeline's video-editor identity.

## Required proof record

When implementation is complete, append a dated proof section containing:

- The exact build, lint, contract-test, controller-test, and checklist-validation commands and their results.
- Paths to before/after screenshots for every required window size, theme, and representative interaction state.
- The real app workflow recording or a timestamped step-by-step result with the saved test document path.
- Any retained raw-value exception, its file/property, its semantic reason, and the proof that it remains readable.
- Any deferred item, the reason it is blocked, its owner, and the condition required to resume it. Deferred items remain unchecked.

## Proof — 2026-07-15

### Commands and results

- Desktop application and focused proof runner build:
  `cmake --build --preset build-dev-macos-debug --target photo_editor_app desktop_shell_smoke_test timeline_ui_consistency_contract_test` — passed.
- QML lint:
  `cmake --build --preset build-dev-macos-debug --target photo_editor_app_qmllint` — passed with the repository's existing warnings and no new Timeline errors.
- Focused Timeline contracts/controllers:
  `ctest --test-dir build/dev-macos-debug -R '^(timeline_ui_consistency_contract_test|timeline_document_controller_test|timeline_playback_controller_test)$' --output-on-failure` — 3/3 passed.
- Focused desktop-shell workflow and visual matrix, repeated at 125% and again after the final responsive fixes:
  `env PHOTO_EDITOR_SMOKE_STOP_AFTER_TIMELINE_UI_PROOF=1 PHOTO_EDITOR_VISUAL_PROOF_DIR=/private/tmp/timeline-ui-consistency-proof/offscreen-$scale QT_SCALE_FACTOR=$scale ./build/dev-macos-debug/tests/desktop_shell_smoke_test` for `1.0`, `1.25`, `1.5`, and `2.0` — passed at every scale. The test builds and reopens the document before activating the lazy Timeline QML tree, eliminating the previously nondeterministic route/mutation race.
- Diff hygiene:
  `git diff --check` — passed.
- Exact numbering and completion validation is part of `timeline_ui_consistency_contract_test`: exactly 100 sequential checked items and Gates A-J are required.

### Visual evidence

- Geometry baseline before the final responsive Source/splitter/marker pass: [`baseline-1.0`](/private/tmp/timeline-ui-consistency-proof/baseline-1.0/).
- Final 100% proof, including dark/light 1180x768, 1440x900, 1920x1080, title Inspector, collapsed panels, and search/marker review: [`offscreen-1.0`](/private/tmp/timeline-ui-consistency-proof/offscreen-1.0/).
- Final scale matrices: [`125%`](/private/tmp/timeline-ui-consistency-proof/offscreen-1.25/), [`150%`](/private/tmp/timeline-ui-consistency-proof/offscreen-1.5/), and [`200%`](/private/tmp/timeline-ui-consistency-proof/offscreen-2.0/). Each contains both themes at all three target window sizes plus a JSON manifest and the saved/reopened proof document.
- Representative minimum-width result: [`dark 1180x768`](/private/tmp/timeline-ui-consistency-proof/offscreen-1.0/timeline-dark-scale-1-0-1180x768.png).
- Representative maximum-scale result: [`light 200% at 1920x1080`](/private/tmp/timeline-ui-consistency-proof/offscreen-2.0/timeline-light-scale-2-0-1920x1080.png).

### Workflow result

The focused proof created `Timeline UI proof`, added a saturated media fixture, loaded a second source clip, marked 1–8 seconds, inserted it at the playhead, added a title and captions, trimmed the opening clip to 4.5 seconds, changed opacity to 86%, added the `Review opening` marker, changed the title to `Polished social cut`, saved, closed, reopened, and verified the clip count. The durable saved artifact is [`timeline-ui-proof-document.json`](/private/tmp/timeline-ui-consistency-proof/offscreen-1.0/timeline-ui-proof-document.json). Live app inspection additionally verified play/pause/jump-to-start, track toggles, title Inspector updates, Add Media and caption dialogs, dark/light persistence, and closure of an open Timeline tool menu when switching to Library.

### Interaction, focus, and state evidence

- Keyboard order follows document header actions, Source, Program transport, Inspector, then Timeline command/track content. Shared controls expose tab focus, accessible names, tooltips/disabled reasons, checked state, and common focus rings.
- Marker and caption popups use `PopupSurface`, close on Escape/outside activation/workspace hide/document close, and explicitly return focus to the invoking item (falling back to the Timeline viewport).
- Shared primitives cover hover, pressed, checked, focused, disabled, and busy states. Timeline-specific proof covers selected clips/tracks, disabled effect processing with recovery copy, active render status, empty Source, monitor error presentation, marker review, search, collapsed/restored panels, and dark/light themes.
- Source, Inspector, and panel widths persist through `TimelineWorkspaceSettings`; the proof exercises collapsed and restored panels and custom themed SplitView handles at every target size.

### Retained intrinsic/content values

- `TimelineView.qml`: timeline zoom range, time-to-pixel mapping, 48 px minimum clip width, snap threshold, 160 px lane header, 58 px default lane, 24 px ruler, 4 px track gap, tick/playhead/snap/trim geometry, waveform/poster density, and safe-area percentages remain intrinsic editing measurements.
- `TimelineSourcePanel.qml`: the 166 px source canvas and responsive scroll body are media/layout geometry; the canvas uses `Theme.mediaCanvas` while surrounding chrome uses shared surfaces.
- `TimelineMarkerPin.qml`, `TimelineMarkerCard.qml`, and `TimelineTrackHeader.qml`: marker line width/card width and the 4 px resize affordance remain time/edit hit geometry; focus and chrome borders use shared tokens.
- Title/caption `#ffffffff` and `#99000000` fallbacks are saved content values. They remain outside the ordinary chrome palette by design and were checked in both themes.
- Popup/editor widths and responsive panel minima are containment geometry, not a second spacing system. All internal padding, type, borders, radii, and motion use shared tokens.

No items are deferred.

## Related Timeline plans

- [Timeline MVP Checklist](timeline-mvp-checklist.md)
- [Timeline Professional Parity Checklist](timeline-professional-parity-checklist.md)
- [Timeline State-of-the-Art Backlog](timeline-state-of-the-art-backlog.md)
- [Timeline Industry-Leading Checklist](timeline-industry-leading-checklist.md)
