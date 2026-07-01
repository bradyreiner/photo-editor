# Virel Studio UI/UX Redesign Plan

Status: planning document only. This document does not prescribe immediate code changes and does not remove existing functionality.

## Goal

Virel Studio has enough capability to feel like a serious editor, but the current UI exposes too much of that capability at the same time. The redesign should keep the full product surface while making the app feel calmer, more professional, and easier to operate for photographers and creators.

The new interface should:

- Make the image, timeline, or board the center of attention.
- Reduce visible text by replacing repeated labels with stable icon controls, tooltips, concise section titles, and contextual panels.
- Move debug, contract, provider, and implementation wording out of the default UI.
- Keep expert controls available through progressive disclosure.
- Make every workspace feel like one coherent application rather than separate experiments.
- Preserve all current capabilities: import, catalog, culling, edit adjustments, masks, brush, retouch, layers, crop, compare, scopes, carousel, timeline, export, preferences, jobs, recovery, and command access.

## Evidence Reviewed

Screenshots:

- `image-1.png`: Library with source rail, filter bar, justified grid, inspector, metadata, review actions.
- `image-2.png`: Edit Adjust with library context panel, image stage, filmstrip, right inspector, scopes, adjustment controls.
- `image-3.png`: Edit Mask/Brush with tool rail, stacked top mask bars, floating mask/layer panel, mask overlay, right inspector.
- `image-4.png`: Edit Layers close-up with layer target cards, layer rows, nested mask details, action buttons.
- `image-5.png`: Edit advanced color controls with split toning/color wheels.
- `image-6.png`: Carousel with board canvas, tool rail, board/media/guides/layers inspector.
- `image-7.png`: Export with presets, selected assets, output settings, queue, destinations.

Code surfaces:

- `apps/desktop/qml/Main.qml`: shell, workspace navigation, menu actions, activity/failure status, shortcuts, workspace loader.
- `apps/desktop/qml/workspaces/StudioWorkspace.qml`: current Library, Edit, and Export pages, plus most edit/mask/layer/control primitives.
- `apps/desktop/qml/workspaces/LibraryView.qml`: fuller catalog/culling/import/project/export workflow surface.
- `apps/desktop/qml/workspaces/ImportView.qml`: focused import browser and selection workflow.
- `apps/desktop/qml/workspaces/CarouselView.qml`: social board/carousel composition.
- `apps/desktop/qml/workspaces/TimelineView.qml`: timeline setup, source/program monitor, tracks, effects, markers, media picker.
- `apps/desktop/qml/workspaces/ExportView.qml`: fuller export queue/preflight/job view.
- `apps/desktop/src/*controller*.h`, `catalog_asset_model.h`, `export_job_controller.h`: exposed user-facing capability families.
- `docs/frontend-design.md`: existing visual direction.
- `docs/tool-polish-audit.md`: existing tool-quality expectations.

## Current Diagnosis

### What Works

- The application has a strong professional foundation: top-level workspaces, persistent side panels, real catalog data, nonblocking jobs, direct image editing, masks, layers, scopes, carousel boards, and timeline tools.
- The dark visual language is already close to a focused editing environment.
- The app uses stable primitives such as panels, rails, mini buttons, inspector groups, sliders, thumbnails, and status indicators.
- Many advanced operations already have shortcuts, deterministic state signatures, and tests. Those should remain, but they should not dominate the default visual read.

### Main UI Problems

1. Too many command surfaces compete at once.
   - Top menu, workspace tabs, left rails, filter bar, floating toolbars, right inspector tabs, bottom filmstrip, in-canvas HUDs, side panels, and footer actions all appear concurrently.

2. Text is often implementation-centered.
   - Examples: "Editing Base Image", "Global image adjustments", "Mask target", "Brush selection / Add / Overlay", provider/status strings, contract-like signatures, and long metadata labels.

3. Accent color is overused.
   - Cyan/mint currently appears on many borders, selected states, buttons, status text, and overlays. This reduces the signal value of active state and primary action.

4. Buttons look equally important.
   - Many secondary actions use the same rectangular treatment as primary decisions. Users cannot quickly tell what to do next.

5. Mask and layer workflows expose internal stack mechanics too early.
   - The system is powerful, but the default view shows too many nested rows, source summaries, action buttons, and status strings before the user asks for detail.

6. Library and Export use three-column layouts that feel balanced structurally but visually crowded.
   - The side panels are useful, but too much metadata and too many filter/action controls are always visible.

7. Carousel and Timeline inherit the same density without enough domain-specific hierarchy.
   - They need stronger "canvas first" and "timeline first" rules, with inspectors that reveal only relevant settings for the current object.

## North Star

The redesigned app should feel like a professional studio desk:

- The photo, board, or sequence is the working surface.
- Tools feel precise and available, not noisy.
- Panels behave like drawers and inspectors, not dashboards.
- Default mode is clean.
- Expert mode is one click or one shortcut away.
- The user always knows three things: where they are, what is selected, and what will change if they interact.

## Global Design Model

### Three-Layer Interface

The app should separate UI into three stable layers:

1. Primary workspace layer
   - The image grid, image stage, carousel board, timeline, or export queue.
   - Always the largest and quietest area.

2. Context layer
   - Left rail/panel and right inspector.
   - Shows only the controls relevant to the active workspace and selected object.

3. Command layer
   - Menus, command palette, shortcuts, contextual overflow, dialogs.
   - Holds secondary, rare, destructive, and technical operations.

### Visibility Rules

- Default view shows only the controls needed for the next common action.
- Advanced view reveals deeper controls per section, not app-wide clutter.
- Debug/contract/status signatures remain available only in a diagnostics mode.
- When an on-canvas tool is active, generic canvas chrome hides.
- When an inspector tab is active, unrelated inspector sections collapse or disappear.
- Repeated labels become icons plus tooltips.
- Numeric values stay visible for professional precision, but explanations move to tooltips.

### Density Modes

Keep density as a real feature, but make it purposeful:

- `Clean`: default for new users and focused editing. Minimal side chrome, concise inspector, no debug status text.
- `Pro`: current professional density, but reorganized and quieter.
- `Inspect`: temporary mode for scopes, metadata, diagnostics, and exact stack details.

The current `compact/pro` idea should evolve into this three-mode model.

### Language Rules

Use photographer-facing language in the UI:

- "Selection" instead of "Mask" when the user is choosing pixels.
- "Local edit" instead of "Adjustment layer target" in default surfaces.
- "Base photo" instead of "Base Image target".
- "Add to selection", "Remove from selection", "Intersect" instead of exposing operation strings everywhere.
- "Preview", "Hide overlay", "Show outline" instead of implementation state.
- "Brush", "Subject", "Sky", "Object", "Linear", "Radial", "Lasso", "Polygon" as tool names.
- "Technical details" or "Diagnostics" for provider, source, model, and contract text.

Keep exact technical language in tooltips, diagnostics panels, tests, and logs.

## Visual System

### Palette

The current palette is too dependent on bright mint. Move to a more restrained studio palette:

- App background: graphite-black, not pure black.
- Panels: slightly lifted graphite.
- Lines: low-contrast charcoal.
- Primary action: restrained warm amber or brass.
- Selection/mask state: cool cyan, used only for masks and overlays.
- Success: muted green.
- Warning: amber.
- Destructive: restrained red/rust.

Recommended token direction:

- `bg.app`: `#090B0D`
- `bg.stage`: `#050607`
- `bg.panel`: `#101417`
- `bg.panelRaised`: `#151A1E`
- `line.subtle`: `#232A30`
- `line.strong`: `#343D45`
- `text.strong`: `#F0F2F0`
- `text.main`: `#C9D0D2`
- `text.muted`: `#88939A`
- `text.faint`: `#58626A`
- `accent.primary`: `#D0A35E`
- `accent.mask`: `#63C7D8`
- `accent.active`: `#E6C17F`
- `state.error`: `#B8665B`

### Typography

- Use one UI sans for navigation and control labels.
- Use mono only for numeric values, timecode, file names, dimensions, exact color/metadata values, and short status chips.
- Remove mono from long explanatory sentences.
- Keep control labels at 11-12 px, values at 11-13 px, section labels at 12-14 px.
- Avoid all-caps except tiny status tags.

### Buttons

Button hierarchy:

- Primary: one per decision area. Filled or stronger border.
- Secondary: text + icon only when the text disambiguates the command.
- Icon: square, tooltip-backed, for repeated tools and utility actions.
- Segmented: mode choice where exactly one or a few options may be active.
- Danger: reserved for delete/clear/destructive queue operations.

Most rectangular text buttons in rails should become icon buttons with tooltip and optional expanded labels.

### Panels

- Avoid nested card-on-card structure.
- Use panels as anchored drawers, not floating boxes.
- Section grouping should rely on spacing, headings, and soft dividers.
- Active target should be shown once per inspector, not repeated in every section.

### Status

Use three status surfaces:

- Workspace badge: tiny dot/count in top tab for activity/failure.
- Toast/chip: nonblocking progress or success at lower-right or bottom edge.
- Inspector detail: exact job/model/diagnostic details only when opened.

Do not place long status strings across the canvas by default.

## Information Architecture

### Top-Level Workspaces

Keep the five workspaces:

1. Library
2. Edit
3. Carousel
4. Timeline
5. Export

Import remains a Library mode. Mask, Crop, Compare, Retouch, Layers, Scopes, and Presets remain Edit modes/tabs.

### Global Shell

Current functions:

- Switch workspaces.
- Show selected asset and workspace/version.
- Display activity/failure status.
- Expose File/Edit/View/Timeline/Carousel/Window/Help menus.
- Support command palette, shortcuts, preferences, keyboard shortcuts, app folders, update checks.
- Route selected media into edit, carousel, timeline, and export.

Redesign:

- Keep top nav, but reduce it to brand, workspace tabs, and one compact status cluster.
- Move asset name/version into a small right status pill. Do not make it a two-line text block unless space allows.
- Replace duplicated menu categories: there are two `View` menus today. Consolidate into `View` plus `Help`/`Window`.
- Add a global command button/search affordance near the right status cluster, mapped to existing command palette.
- Use tab badges only for meaningful counts: import running, render running, export failures.
- Activity indicator stays nonblocking and compact.
- Preferences, folders, runtime paths, diagnostics, and updates live under menus/command palette, not primary workspace chrome.

### Navigation Model

Primary navigation:

- Workspace tabs: Library, Edit, Carousel, Timeline, Export.
- Keyboard: `Cmd/Ctrl+1..5`.

Secondary navigation:

- Left tool rail in Edit/Carousel/Timeline.
- Inspector tabs or segmented controls in right panels.

Tertiary navigation:

- Command palette.
- Menus.
- Context menus.
- Overflow buttons.

## Page Redesigns

## 1. Library

### Current Functions To Preserve

- Source selection: VIREL Vault, local sources, import folder, import status, drive/import source list.
- Smart collections: all images, visible, picked, rejected, rated, unrated, unmarked, online, offline.
- Search.
- Filters: media, project, time/import date, review, clear filters.
- View modes: grid, survey; older LibraryView also has compare.
- Multi-select mode, select all, deselect.
- Justified grid thumbnails with rating/pick/reject states.
- Survey review view for bursts/duplicates/nearby candidates.
- Inspector preview.
- Metadata display: camera, lens, captured, ISO, shutter, aperture, imported, project, path.
- Actions: edit, export, selections, compare.
- Review actions: pick, reject, clear, star rating.
- Project assignment, create project, add/remove selection.
- Delete selected assets from catalog.
- Relink selected/offline assets.
- Queue selected exports.
- Activity: refresh, import, delete progress.

### Current UX Issues

- The left rail lists sources, import controls, smart collections, and storage all at once.
- The top filter bar has too many text-heavy buttons.
- The right inspector duplicates actions already available through double click, toolbar, menus, and shortcuts.
- Metadata consumes a lot of vertical space even when not needed.
- Review controls use text buttons where icon controls would scan faster.

### Proposed Layout

Library should have three modes:

- `Browse`: default contact sheet.
- `Review`: culling/survey/focus work.
- `Import`: intake flow.

Default Browse layout:

- Left: compact source rail, 56 px collapsed by default with icons for Vault, Local, Projects, Smart Filters. Expandable drawer for full names/counts.
- Center: search/filter row + contact sheet.
- Right: inspector collapsed by default until an asset is selected or user pins it.
- Bottom: selection action bar appears only when one or more assets are selected.

Top filter row:

- Search input.
- One "Filters" button that opens a popover/drawer with media, project, date, rating, online/offline.
- Active filters shown as small chips under the search row only when active.
- View mode icons: grid, survey, compare.
- Select mode icon.

Inspector:

- Header: thumbnail, filename, rating/pick status.
- Quick actions row: edit, export, compare as icon buttons with tooltips. Text only on primary export when space allows.
- Metadata collapsed into "Info", "Camera", "File", "Projects" sections.
- Review controls use flag icons and star strip, not Pick/Reject/Clear text buttons.

Selection action bar:

- Appears at bottom center when selected count > 0.
- Shows count and compact actions: Edit, Add to Project, Export, Delete, Clear.
- Project assignment and export options open dialogs/sheets.

### Library Text Reduction

- Replace "Import to VIREL Vault" with "Import" in the primary button; detail appears in tooltip or import drawer.
- Replace "All Images / Visible / Picked..." always-visible list with Smart Filter drawer.
- Replace "9 results" with a small count in the filter row.
- Show metadata labels only in inspector detail view. Use compact icon/value rows in default.

### Library Acceptance Criteria

- Browse mode shows contact sheet as the visual dominant area.
- A user can still import, search, filter, review, project, edit, compare, export, delete, and relink.
- At rest, no more than one left panel and one right panel are open.
- When nothing is selected, inspector is hidden or shows a minimal empty state.
- Multi-select actions appear only when relevant.

## 2. Import

### Current Functions To Preserve

- Choose folder.
- Browse local folder.
- Select/deselect files.
- Select all visible.
- Detect video/photo files.
- Show parent path.
- Import current folder or selected files.
- Import into catalog/VIREL Vault.
- Show import activity and failures.
- Return to Library.

### Proposed Layout

Import should feel like a guided intake bench:

- Step 1: Source
  - Mounted drives and common folders.
  - Choose Folder.
  - Recent imports.

- Step 2: Review
  - File grid/list with type badges.
  - Selection count.
  - Duplicate/offline warning if available.

- Step 3: Destination
  - VIREL Vault default.
  - Project assignment.
  - Optional rename/metadata/develop preset behind "Import Options".

- Step 4: Import
  - One primary Import button.
  - Progress chip, not modal overlay.

### Import Text Reduction

- Use step headings and concise row values.
- Hide long paths behind middle-elided labels with reveal action.
- Use "Options" for rename/checksum/backup/develop preset rather than placing all options in default view.

## 3. Edit

### Current Functions To Preserve

Global edit:

- Load selected/local asset.
- Sync recipe back to catalog.
- Adjust tool.
- Crop tool.
- Selections/masks tool.
- Brush tool.
- Compare tool.
- Export route.
- Undo/redo/reset.
- Copy/paste settings.
- Presets.
- Filmstrip.
- View fit/zoom/pan.
- Before/after and compare.
- Reference image.
- Pixel inspector.
- Scopes and diagnostics.

Adjustment controls:

- White balance temp/tint and sample.
- Light: exposure, brightness, contrast, highlights, shadows, whites, blacks, clarity-style controls, levels/zones.
- Color: vibrance, saturation, HSL/color mix.
- Color grading/split toning.
- Tone curve and parametric curves.
- Presence/detail/sharpen/noise/lens.
- RAW controls.
- Advanced color: LUTs, channel mixer, black and white, color replace, skin tone, harmony, lab/tools where available.
- Per-section reset and before/after.
- Adjustment filter/search.

Local editing:

- Base image target.
- Adjustment layers.
- Fill/text/source/layer groups.
- Layer visibility, lock, solo/isolate, opacity, blend mode, clipping, masks, transform.
- Layer reorder, duplicate, rename, delete.
- Layer visibility states.
- Local adjustment values routed to selected layer/mask.

Selections/masks:

- Object, subject, sky, background.
- Brush add/erase.
- Lasso, polygon.
- Linear, radial, ellipse, rectangle.
- Add/subtract/intersect/replace.
- Overlay/outline/hidden preview modes.
- Invert.
- Feather, density, opacity, smooth, fine edge.
- Full-resolution/HQ preview.
- Delete empty, update generated, retry failed.
- Rename, duplicate, duplicate inverted, duplicate set.
- Source provenance and diagnostics.

Retouch:

- Heal, patch, remove/inpaint, clone, dust, red-eye.
- Clone source picking.
- Patch source controls.
- Apply/regenerate/cancel.
- Before/after.
- Local fallback/model-ready status.

Crop/transform:

- Crop rectangle.
- Crop preset/aspect presets.
- Custom aspect add/remove.
- Guides.
- Reset crop, reset transform.
- Upright guided/off.
- Horizon straighten.
- Rotate, scale, perspective, skew.
- Content-aware crop fill.
- Liquify if available.

Scopes:

- RGB/luma/log.
- Density/trace/grid.
- Image vs large scope mode.
- Histogram/waveform and proofing status.

Info:

- File, camera, lens, captured, resolution, color space, proofing, catalog, review, project.

### Current UX Issues

- The Edit screen has left workspace context, left tool rail, top canvas controls, mask top bars, floating mask stack, right inspector tabs, bottom filmstrip, and activity chips competing.
- The right inspector exposes target cards and technical state labels in every mode.
- The layer/mask UI shows too many nested concepts: target, source, layer, mask, provider, active selection, preview bypass, solo, visibility, refinement.
- Brush mode stacks top text status, tool rail, floating selection panel, right inspector controls, overlay labels, and activity chips.
- The user cannot easily distinguish "what am I editing?" from "what controls are available?"

### Proposed Edit Layout

Use a stage-first layout:

- Left rail: tool families only.
- Center: image stage with filmstrip optionally visible.
- Right inspector: selected tool or selected target controls.
- Bottom status strip: zoom, view mode, before/after, activity, active target.

Default Edit state:

- Left tool rail icons:
  - Adjust
  - Crop
  - Select
  - Retouch
  - Layers
  - Compare
  - Export

Note: Brush becomes a Select subtool, not a top-level peer unless active. This removes duplicated "Selections" and "Brush" concepts.

Right inspector tabs:

- Adjust
- Presets
- Layers
- Scopes
- Info

When a tool is active, the inspector defaults to the matching tab:

- Adjust tool -> Adjust tab.
- Crop tool -> Crop section in Adjust tab or temporary Crop inspector.
- Select tool -> Selections inspector.
- Retouch tool -> Retouch inspector.
- Layers tool -> Layers tab.
- Compare tool -> Compare inspector.

### Edit Target Model

Show one persistent "Editing" strip at the top of the inspector:

- Base Photo
- Local Edit: Layer name
- Selection: Selection name
- Text/Fill/Photo layer

This strip should include:

- Thumbnail/icon.
- Target name.
- Scope badge: Base, Layer, Selection, Retouch.
- One overflow menu.

Do not repeat target breadcrumbs inside every section.

### Adjust Inspector

Default visible sections:

- White Balance
- Light
- Color
- Detail

Collapsed by default:

- Tone Curve
- Color Grading
- RAW
- Lens
- Advanced Color
- Technical

Search/filter remains, but it should be a small icon or field at top of inspector. Matching sections appear with highlighted titles. Empty "no controls" explanations should be concise.

Section rows:

- Label left, slider center, value right.
- Changed state is a small dot or accent tick, not a large colored border.
- Reset is an icon in section header.
- Before/after is an icon in section header.

### Crop Inspector

When Crop is active:

- Hide generic adjustment groups.
- Right inspector shows:
  - Aspect
  - Guide
  - Rotate/Straighten
  - Transform
  - Fill
  - Reset actions
- Canvas shows crop handles and guide overlay.
- Bottom status shows dimensions/aspect/rotation.

Crop controls should be task-labeled:

- "Aspect" not "Crop W/H/X/Y" by default.
- Exact X/Y/W/H move into "Geometry" advanced section.

### Selections/Mask Inspector

Rename default user surface to "Selections".

Selection workflow should be:

1. Choose source: Subject, Object, Sky, Background, Brush, Lasso, Polygon, Linear, Radial.
2. Choose combine operation: Add, Remove, Intersect, Replace.
3. Refine: Feather, Density, Opacity, Smooth, Edge.
4. Apply local edits: Light/Color/Detail controls scoped to selection.

New layout:

- Left secondary rail inside canvas only when Select is active:
  - Source icons grouped by Smart, Draw, Gradient.
  - Expanded labels on hover or pin.
- Top mask toolbar reduced to one compact strip:
  - Source name.
  - Operation segmented control.
  - Overlay mode icon.
  - Done.
- Floating selection stack replaced by either:
  - Right inspector "Selections" list, or
  - A bottom compact chip strip when canvas focus is needed.

Selection list default row:

- Visibility icon.
- Thumbnail/matte icon.
- Name.
- Affected target.
- Small chips: Feather, Density, Opacity if changed.
- Overflow.

Expanded row:

- Source provenance.
- Combine order.
- Duplicate/invert/move/delete.
- Retry/update/full-res controls.

Debug mode only:

- Provider, alpha target size, deterministic flags, source contracts, trace signatures.

### Brush HUD

Brush mode should not show both full right-panel controls and large top status text by default.

Default brush HUD:

- On-canvas cursor ring.
- Compact bottom-left HUD:
  - Size
  - Flow
  - Feather/Hardness
  - Mode Add/Remove
- Keyboard shortcuts and exact pressure data only in tooltip or expanded HUD.

Right inspector:

- Brush Settings section.
- Selection Refinement section.
- Local Edit section.

Remove long text like "Add Preview 164 px / Stored 6% radius / Final 720 x 480 px..." from default canvas; move it to Inspect/Diagnostics.

### Layers

Layer workflow should explain stack ownership without showing all mechanics by default.

Right inspector Layers tab:

- Top: target strip.
- Layer toolbar:
  - Add
  - Fill
  - Text
  - Duplicate
  - Delete
  - Isolate
  - More
- Layer list:
  - Base Photo row locked at bottom.
  - Adjustment layers.
  - Fill/text/source layers.
  - Groups.

Collapsed layer row:

- Thumbnail/icon.
- Name.
- Kind badge.
- Visibility.
- Opacity mini readout.
- Mask badge if masks exist.

Expanded layer row:

- Blend mode.
- Opacity slider.
- Lock/solo/clipping/transform.
- Linked masks.
- Reorder/duplicate/rename/delete.

Selection/mask details under a layer should be collapsed until the user expands "Masks".

### Retouch

Retouch should be its own tool family, not hidden under masks.

Left tool rail:

- Heal
- Patch
- Remove
- Clone
- Dust
- Red-eye

Right inspector:

- Tool mode.
- Required next step.
- Brush controls.
- Source/patch controls.
- Apply/Regenerate/Cancel.
- Before/after.

Canvas:

- Shows source pins, target regions, patch offsets.
- Only one retouch status badge.

Experimental tools remain hidden from primary UI until enabled in an Experimental section.

### Compare

Compare should be a focused proofing state:

- Hide left library context panel.
- Inspector becomes Compare:
  - Before/After
  - Split/Wipe/Side-by-side
  - Reference image
  - Sync zoom/pan
  - Loupe
- Bottom filmstrip remains for candidate switching.
- Scopes can be pinned as a bottom/side overlay in Inspect mode.

### Filmstrip

Filmstrip should be optional:

- Default visible in Edit Browse/Adjust.
- Auto-hide in Crop/Select/Retouch unless pinned.
- Use thumbnails only, with rating/pick badges.
- Avoid blank placeholder thumbnails looking like failed content; use a clear loading/empty treatment.

## 4. Carousel

### Current Functions To Preserve

- Create carousel with name, aspect, slide count.
- Recent carousels.
- Board/slide management: add, duplicate, remove, navigate, format.
- Canvas tools: select, pan, text, media, shape.
- Formats: 4:5, 1:1, 9:16, 5:4.
- Templates: single, split pair, linked pair, stacked triptych.
- Background solid/horizontal gradient/vertical gradient, swatches, hex values.
- Media slots: choose, clear, fill, fit, scale, offsets.
- Edited library quick media.
- Guides: snapping, thirds, safe, opacity, custom vertical/horizontal guides, clear.
- Layers: photo/text/shape, ordering, visibility, lock, delete.
- Transform: X/Y/R/W/H, flip, fill/fit, radius, frame, shadow, opacity, align/distribute.
- Shape controls: kind, fill, secondary color, stroke, radius.
- Photo/video controls: fill/fit, crop shape, pan.
- Text controls: content, color, box, size, tracking, stroke, font family, weight, alignment, italic.
- Save and export.

### Current UX Issues

- The right panel shows board, media, guides, layers, transform, shape/photo/text all in the same vertical scroll.
- Many controls use text buttons for simple symbols: `+`, `Dup`, `x`, `Del`, `Top`, `Mid`, `Btm`.
- The canvas placeholder has overlapping "Add media" text in the screenshot.
- Board actions, media actions, and layer actions are not visually separated enough.

### Proposed Layout

Carousel should feel like a composition tool:

- Left rail: Select, Hand, Text, Media, Shape.
- Center: board canvas with board strip at bottom.
- Right inspector: one of Board, Selection, Media, Text, Shape, Guides.

Default right inspector:

- If no object selected: Board inspector.
- If media selected: Media inspector.
- If text selected: Text inspector.
- If shape selected: Shape inspector.
- If multiple selected: Align/Distribute inspector.

Board inspector:

- Format
- Template
- Background
- Board list

Selection inspector:

- Transform
- Style
- Layer

Move Guides and Layers into separate inspector tabs or collapsible drawers, not always visible under Media.

### Carousel Text Reduction

- Use icons for add, duplicate, delete, previous/next, lock, visibility, align, distribute, flip.
- Keep labels for formats and templates because they are domain choices.
- Replace "Edited Library" with "Library" and a filter icon.
- Replace "No layers" text box with subtle empty row.

### Carousel Acceptance Criteria

- The board is visually dominant.
- The right inspector changes based on selected object.
- Board controls and object controls are never mixed at the same level.
- All current board, media, text, shape, guide, layer, save, and export capabilities remain available.

## 5. Timeline

### Current Functions To Preserve

- Create timeline with name, project, resolution, custom width/height, FPS, audio, color, export format.
- Recent timelines.
- Source media picker and project-scoped media.
- Load to source, insert, overwrite.
- Source/program monitor.
- Playback, pause, step, seek.
- Timeline tracks, track heights, zoom, zoom to fit.
- Tools: select, razor, slip, roll.
- Snapping and ripple mode.
- Ruler, playhead, snap guide, marquee select.
- Split at playhead.
- Move clips earlier/later.
- Lift/extract clip.
- Insert/overwrite source at playhead.
- Markers: add, edit, status, completed, jump, filter/search.
- Effects and transitions library.
- Apply effect to clip.
- Apply transition to clip edge.
- Save/render jobs.
- Show jobs/export status.
- Restore layout.

### Current UX Issues

- Timeline has many legitimate pro controls, but source, program, effects, markers, tracks, and job state can overwhelm a still-photo-oriented app shell.
- Text-heavy effect descriptions and marker notes compete with the timeline.
- Setup flow is form-heavy.

### Proposed Layout

Timeline should use a video-editor layout but simplified:

- Top: timeline name, format, timecode, playback, save/render, jobs.
- Left drawer: Source media and bins.
- Center top: Program monitor.
- Center bottom: Timeline tracks.
- Right drawer: Inspector with tabs:
  - Clip
  - Effects
  - Transitions
  - Markers
  - Sequence

Setup state:

- Create timeline card centered in workspace.
- Use presets first: Social 4:5, Landscape 16:9, Vertical 9:16, 4K, Custom.
- Advanced sequence fields collapse under "Sequence Details".

Timeline active state:

- Timeline tracks dominate lower half.
- Effects and transitions are searchable lists, not visible descriptions by default.
- Marker panel opens when marker tool/filter is active.

### Timeline Text Reduction

- Use icon buttons for playback, step, split, razor, snapping, ripple, panel toggles.
- Use timecode/value labels only where needed.
- Hide effect descriptions until hover or row expansion.
- Move render/job detail into Jobs popover.

## 6. Export

### Current Functions To Preserve

- Export presets.
- Create/manage presets.
- Destinations: local drive, VIREL Cloud, Dropbox, Google Drive, OneDrive.
- Selected assets preview.
- Add more assets.
- Compact/pro density toggle.
- Delete/clear.
- Output settings: color space, resize, sharpening, watermark, file naming, destination.
- Export summary.
- Preflight.
- Queue current selection/batch exports.
- Crop constraint exports.
- Retry/cancel/remove jobs.
- Clear finished jobs.
- Job progress/status/failures.
- Default destination/pick destination.

### Current UX Issues

- Presets, destinations, selected assets, settings, summary, queue, and storage all appear at once.
- Output settings are represented as repeated cards, which adds height without aiding scanning.
- "Start Export" and "Clear Queue" compete visually.
- Queue rows contain too many columns at equal visual weight.

### Proposed Layout

Export should feel like a delivery bench:

- Left: Presets and destinations, collapsible.
- Center: Selected assets and queue.
- Right: Preflight and output summary.

Default flow:

1. Choose preset.
2. Choose destination.
3. Review preflight.
4. Start export.

Center:

- Selected assets as a compact horizontal strip or list.
- Queue as primary table.
- Add more assets drop zone only when queue is empty or user drags.

Right:

- Preflight status at top:
  - Ready
  - Warnings
  - Blocking issues
- Output summary as compact key/value list, not cards.
- Details expandable by category: Color, Size, File Name, Metadata, Watermark.

### Export Text Reduction

- Replace repeated cards with table rows.
- Keep one primary "Start Export" button.
- Move `+ Add More`, Clear, Delete, retry/cancel into toolbar/overflow.
- Show preset as "Web / sRGB / 2048px" once in header and per row only if rows differ.

## Dialogs And Secondary Surfaces

### Command Palette

Preserve and elevate it as a main decluttering tool.

Should include:

- Open workspace.
- Run edit tools.
- Toggle panels.
- Search commands.
- Queue export.
- Open preferences/shortcuts.
- Open folders/logs.
- Retry failures.
- Create timeline/carousel.

### Keyboard Shortcuts

Keep existing shortcuts, but present them in a searchable grouped dialog:

- Global
- Library
- Edit
- Selections
- Retouch
- Carousel
- Timeline
- Export

### Preferences

Keep:

- Startup workspace.
- Reopen last workspace.
- Timeline defaults.
- Culling auto advance.
- Dark mode.
- Runtime paths.
- Cache/log/backups.
- Preview backend.
- Model/profile locations if present.

Redesign:

- Use a simple sidebar with sections.
- Do not mix runtime paths with creative preferences.

### Project Assignment

Keep project add/remove/create functions.

Redesign:

- Use a compact sheet with searchable project list.
- Show applied/mixed/not applied states with icons.
- Move explanatory paragraph out of default; use tooltip/help.

### Export Selection Dialog

Keep as quick queue path from Library.

Redesign:

- Preset dropdown.
- Destination picker.
- Queue Export primary.
- Advanced output options link to Export workspace.

## Function Preservation Matrix

This section maps current user-facing functions to their redesigned homes. Helper-only functions, formatting helpers, and test signatures should remain internal and are not exposed as primary UX.

### Global Shell

| Current function family | Examples | Redesigned home |
| --- | --- | --- |
| Workspace navigation | `switchToWorkspaceIndex`, `switchWorkspaceByNumber`, workspace tabs | Top workspace tabs and command palette |
| Tool routing | `activatePrimaryEditTool`, edit tool actions | Edit left rail and command palette |
| Activity/failure handling | `primaryActivityTitle`, `cancelPrimaryActivity`, `retryPrimaryFailure` | Compact activity chip plus Jobs/Diagnostics detail |
| Selection routing | `openSelectedPhotoInEdit`, `openSelectedTimelineDocument`, `assignSelectedMediaToCarousel`, `queueExportFromMenu` | Context actions, selection bar, command palette |
| Import/create | `beginImportWorkspace`, `createTimelineFromMenu`, `createCarouselFromMenu` | New buttons in relevant workspaces plus File menu |
| Timeline controls | save, media browser, playback, split, tracks, zoom, snapping, ripple | Timeline toolbar, timeline menus, shortcuts |
| Carousel controls | add/remove/move slides, format, assign media, export | Carousel toolbar/inspector |
| App utilities | preferences, shortcuts, folders, updates, about | App menu, Help menu, command palette |

### Library And Import

| Capability | Current function examples | Redesigned home |
| --- | --- | --- |
| Search/filter | `clearFilters`, filter properties, `activeFilterSummary` | Search row + Filters drawer + active chips |
| Selection | `selectAll`, `deselectAll`, `toggleSelectionIndex`, `selectionToolbarText` | Bottom selection bar |
| Import | `chooseImportFolder`, `openLocalFolder`, `importCurrentFolder`, `importFolderPath` | Import stepper |
| Culling | `toggleSelectedPick`, `toggleSelectedReject`, `setSelectedRating`, review shortcuts | Review mode icons, keyboard, inspector |
| Projects | `createProjectForSelection`, `toggleSelectedProjectMembership`, project dialog | Project sheet and Library drawer |
| Export from Library | `openExportDialog`, `queueSelectedExports` | Selection bar Export sheet |
| Relink/delete | `chooseRelinkFolder`, `deleteSelectedAssets` | Inspector/selection overflow with confirmation |
| Compare/survey | `compareAssetModel`, `surveyAssetModel` | Review mode view switch |

### Edit

| Capability | Current function examples | Redesigned home |
| --- | --- | --- |
| Adjustments | `primaryAdjustmentValue`, `setPrimaryAdjustmentValue`, `resetPrimaryAdjustmentGroup` | Adjust inspector |
| Presets | `selectStudioPreset`, `previewStudioPreset`, `applyStudioPreset`, `saveStudioPreset`, import/export preset bundle | Presets tab and command palette |
| Crop | crop rect, aspect preset, guide, upright, transform functions | Crop tool inspector |
| Compare/reference | compare mode, reference image dialog, sync zoom/pan | Compare tool inspector |
| Scopes | scope toggles, proofing status, large scopes mode | Scopes tab / Inspect mode |
| Pixel inspection | `beginPixelInspectorSampling`, `updatePixelInspector`, `clearPixelInspector` | Inspect mode, not default canvas |
| Selection sources | `createMaskSource`, `createGeneratedSelectionSource`, lasso/polygon/object functions | Select tool source rail |
| Selection operations | `setMaskCreationOperation`, add/remove/intersect/replace shortcuts | Select toolbar |
| Brush | `startMaskBrushSource`, brush size/flow/hardness/density/opacity functions | Brush HUD + Select inspector |
| Refinement | selection refine, HQ/full preview, edge reset | Select inspector expanded refine section |
| Retouch | `startRetouchTool`, clone/patch/heal/remove/apply/regenerate/cancel | Retouch tool inspector |
| Layers | layer add/delete/rename/duplicate/opacity/blend/visibility/lock/solo/transform | Layers tab |
| Local edit target | `setSelectedEditTarget`, target signatures | One top target strip; diagnostics hidden |

### Carousel

| Capability | Current function examples | Redesigned home |
| --- | --- | --- |
| Setup | `beginCarouselSetup`, `createCarouselFromSetup` | Centered setup screen |
| Board format/template | format/template buttons, slide geometry | Board inspector |
| Canvas tools | `setActiveTool`, `handleCanvasToolPress`, nudge | Left rail and canvas shortcuts |
| Media placement | `openMediaPicker`, `applyPickedMedia`, `placeLibraryAsset`, slot helpers | Media inspector and picker sheet |
| Guides/snapping | guide candidates, custom guides, snapping functions | Guides inspector |
| Layers | current slide layers, visibility/lock/delete | Layers drawer/tab |
| Transform/style | resize fields, align/distribute/flip/shadow/frame | Selection inspector |
| Text/shape/photo | text style, shape fill, photo crop/pan | Object-specific inspector |
| Save/export | document controller save/export | Header primary actions |

### Timeline

| Capability | Current function examples | Redesigned home |
| --- | --- | --- |
| Setup | `beginTimelineSetup`, `createTimelineFromSetup`, resolution presets | Centered setup screen |
| Media source | `openMediaPicker`, `load/insert/overwritePickedMedia` | Left source drawer |
| Playback | `togglePlayback`, step, seek, timecode | Program monitor controls |
| Timeline editing | split, delete, move, lift/extract, marquee, snapping | Timeline toolbar and context menu |
| Tracks | track height, zoom, ruler | Timeline area controls |
| Markers | `openMarkerEditor`, `saveMarkerEditor`, filters/search/jump | Markers inspector |
| Effects/transitions | `applyEffectToClip`, `applyTransitionToClipEdge` | Effects/Transitions inspector |
| Save/jobs | `saveTimelineToLibrary`, render job status | Header action and Jobs popover |

### Export

| Capability | Current function examples | Redesigned home |
| --- | --- | --- |
| Presets | preset selection, create/manage | Left preset drawer |
| Destination | default/pick destination, cloud destinations | Destination drawer |
| Asset list | selected asset list, add more | Center selected assets/queue |
| Preflight | `exportPreflight`, `currentPreflight` | Right preflight panel |
| Queue | `queueBatchExports`, `queueCurrentSelection`, retry/cancel/remove/clear | Queue table and row overflow |
| Output details | color space, resize, sharpening, watermark, naming, metadata | Right summary with expandable sections |

## Decluttering Strategy

### What Moves Out Of Default UI

- Provider/contract/deterministic/debug text.
- Full path strings unless the user opens info.
- Long model/runtime explanations.
- Repeated "Editing..." target explanations.
- Mask alpha/source provenance.
- Brush render dimensions.
- Full layer/mask nested controls.
- Effect descriptions.
- Export details that match the selected preset.

### What Remains Visible

- Active workspace.
- Selected asset/object/clip.
- Active tool.
- Active target.
- Primary action.
- Changed state.
- Warnings/failures.
- Exact values for active controls.

### Progressive Disclosure Levels

Level 1: Default

- Common controls only.
- One primary action.
- Minimal text.

Level 2: Expanded Section

- More sliders/actions in the current task.
- Exact values and reset/before-after.

Level 3: Details/Inspect

- Metadata, scopes, provider, source, model/runtime, diagnostics.

Level 4: Command/Advanced

- Rare operations, export/import bundles, custom presets, runtime folders, recovery.

## Proposed Screen Specifications

### Library Default

- Top: search + filter button + view icons.
- Left: collapsed source/project rail.
- Center: contact sheet.
- Right: hidden until selection or pinned.
- Bottom: selection bar when selected.

### Library Review

- Center: survey/compare/focus view.
- Left: minimal collection rail.
- Right: review inspector with rating, pick/reject, metadata collapsed.
- Keyboard-first culling.

### Import

- Stepper: Source, Review, Destination, Import.
- Primary visible command: Import.
- Activity as compact chip.

### Edit Adjust

- Left: icon tool rail.
- Center: image stage.
- Bottom: filmstrip and status.
- Right: target strip + Adjust inspector.
- Sections: WB, Light, Color, Detail default.

### Edit Select

- Left: tool rail + selection source rail.
- Center: image stage with overlay.
- Bottom: compact brush/selection HUD if active.
- Right: target strip + Selections inspector.

### Edit Layers

- Center: image stage.
- Right: Layers inspector.
- Layer rows collapsed by default; masks nested only on expansion.

### Edit Crop

- Center: crop overlay.
- Right: Crop inspector only.
- Bottom: crop dimensions/status.

### Edit Retouch

- Center: retouch overlay and pins.
- Right: Retouch inspector.
- Bottom: apply/cancel state if a generated operation is pending.

### Edit Compare

- Center: split/side-by-side/wipe.
- Right: Compare inspector.
- Filmstrip optional.

### Carousel

- Left: composition tool rail.
- Center: board canvas.
- Bottom: board strip.
- Right: object-aware inspector.

### Timeline

- Left: source drawer.
- Center top: program monitor.
- Center bottom: timeline tracks.
- Right: clip/effects/markers inspector.
- Header: timecode, playback, save/render/jobs.

### Export

- Left: preset/destination drawer.
- Center: selected assets + queue.
- Right: preflight + summary.
- Primary: Start Export.

## Component-Level Recommendations

### `MiniButton`

- Add hierarchy variants that visually differ more clearly:
  - `primary`
  - `secondary`
  - `icon`
  - `segmented`
  - `danger`
  - `quiet`
- Support `labelVisible: false` as a first-class default for rails/toolbars.
- Tooltips should be required for icon-only buttons.

### `Panel`

- Add pinned/collapsed states.
- Use panel title only when title adds context; do not repeat page name.
- Support a compact header with icon + title + overflow.

### `InspectorGroup`

- Make changed state, reset, and before/after consistent.
- Do not show both reset text and icon unless group is expanded.
- Use section headers as information hierarchy, not decorative cards.

### `ThumbnailCard`

- Separate review badges from selection highlight.
- Add loading/failed/offline treatments that do not look like empty black tiles.
- Keep filename hidden by default in dense grid, visible on hover/selection.

### `ImageViewport`

- Canvas should own overlays.
- Only one overlay status badge visible at a time.
- Diagnostic overlays should be mutually exclusive by default.

### `RailButton`

- Make all rails icon-first.
- Expanded labels only on hover, pin, or accessibility setting.
- Active state should use a single accent line/fill, not multiple borders.

## Implementation Roadmap

This is a design plan, not an implementation request. If implemented later, do it in phases to avoid breaking workflows.

### Phase 1: Design System Cleanup

- Normalize color tokens away from bright mint as universal accent.
- Define button hierarchy variants.
- Define target strip component.
- Define inspector group behavior.
- Define collapsed/expanded panel behavior.
- Define diagnostic/inspect mode.

Deliverable:

- Updated QML primitives only, with no workflow changes.

### Phase 2: Global Shell And Status

- Consolidate duplicated menus.
- Simplify top bar.
- Add command palette affordance.
- Normalize activity/failure status chips.
- Move long status detail into Jobs/Diagnostics.

Deliverable:

- Same commands, cleaner shell.

### Phase 3: Library And Import

- Collapse source/smart collection rail.
- Convert filters into drawer/chips.
- Add selection action bar.
- Rework inspector into compact/expanded sections.
- Convert import to guided intake layout.

Deliverable:

- Library feels like a contact sheet, not an admin dashboard.

### Phase 4: Edit Core

- Rebuild Edit around stage-first layout.
- Add target strip.
- Simplify adjust inspector default sections.
- Move crop into focused crop mode.
- Make filmstrip optional/pinnable.

Deliverable:

- Adjust/crop/compare preserve all functions with less visible clutter.

### Phase 5: Selections, Retouch, Layers

- Rename user-facing mask workflow to Selections.
- Merge brush into Select workflow.
- Move retouch into dedicated tool family.
- Collapse layer/mask rows by default.
- Hide provider/contract text in diagnostics.

Deliverable:

- Local editing becomes understandable without losing power.

### Phase 6: Carousel

- Object-aware inspector.
- Separate board controls from selected-object controls.
- Icon-first tool/actions.
- Fix empty media placeholder overlap.

Deliverable:

- Carousel feels like a composition canvas.

### Phase 7: Timeline

- Simplify setup.
- Clarify source/program/timeline/inspector regions.
- Hide effect descriptions by default.
- Move jobs into popover.

Deliverable:

- Timeline feels professional without overwhelming non-video workflows.

### Phase 8: Export

- Convert settings cards to compact summary/detail.
- Preflight-first right panel.
- Queue table with row overflow.
- One primary Start Export action.

Deliverable:

- Export feels like a delivery workflow.

## Validation Plan

For each phase:

- Confirm all existing object-level actions remain reachable by UI, shortcut, menu, or command palette.
- Run existing UI smoke tests for affected workspaces.
- Add visual regression screenshots for:
  - Library default and selection.
  - Import.
  - Edit Adjust.
  - Edit Crop.
  - Edit Select/Brush.
  - Edit Layers.
  - Edit Retouch.
  - Edit Compare.
  - Carousel empty and object selected.
  - Timeline setup and active sequence.
  - Export ready and warning states.
- Add accessibility checks:
  - Icon-only buttons have accessible names/tooltips.
  - Keyboard focus order matches visual order.
  - Text fits at minimum supported window size.
  - Color-only state has secondary indicator.
- Verify no debug/contract strings appear in Clean mode.

## Success Metrics

Visual:

- Primary canvas/grid/timeline/board occupies the largest visual weight in every workspace.
- At rest, only one primary action is visually dominant per workspace.
- Bright accent color appears only for selected/active/action states.
- Long explanatory text is absent from default workspace surfaces.

Workflow:

- New user can import, cull, edit, select, export with fewer visible decisions.
- Expert user can still access every current function through expanded panels, command palette, shortcuts, context menus, or diagnostics.
- Mask/layer target state is obvious from one target strip.
- Export preflight tells the user whether they are ready before they start.

Technical:

- Existing controller capabilities are preserved.
- Existing deterministic signatures and tests remain available.
- Diagnostics mode preserves development visibility without cluttering user mode.

## Open Design Decisions

1. Should the default accent become warm amber while masks remain cool cyan, or should the current mint remain only for success/action?
2. Should "Selections" fully replace "Masks" in user-facing copy, with "mask" reserved for technical/diagnostic detail?
3. Should Brush remain a top-level rail item for speed, or become a Select subtool with shortcut access?
4. Should Library inspector auto-open on selection or remain pinned/off based on user preference?
5. Should Export become a separate full queue workspace or also appear as a drawer from Library/Edit?
6. Should Timeline remain a top-level workspace in a photo-first app or be tucked under Create for users who do not work with video?

## Recommended Direction

Use this product hierarchy:

- Library: organize and choose.
- Edit: refine one photo.
- Selections: choose local areas.
- Layers: structure local edits/composites.
- Carousel: compose social boards.
- Timeline: assemble motion sequences.
- Export: deliver.

The strongest immediate redesign move is not a new skin. It is a stricter information hierarchy:

- One stage.
- One active target.
- One active tool.
- One inspector.
- One primary action.

Everything else should be reachable, but not always visible.
