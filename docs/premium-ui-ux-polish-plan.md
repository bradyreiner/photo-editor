# Virel Studio Premium UI/UX Polish Plan

Status: planning document only. This plan assumes the current workspace architecture mostly stays in place. It is not a big redesign plan.

## Objective

Make the existing app feel dramatically more polished, expensive, and cohesive while keeping the current layout, pages, panels, and core workflows familiar.

The goal is not to hide half the app or rebuild the information architecture. The goal is to make every existing surface feel intentional:

- Cleaner visual hierarchy.
- Less noisy text.
- Better control states.
- Better tool behavior.
- Better feedback while editing.
- Easier editing: tools are visible, expandable, switchable, and customizable without cluttering the workspace.
- Fewer competing borders, buttons, labels, and status strings.
- More confidence that each tool does exactly what the user expects.

## Product Stance

Keep:

- Top workspaces: Library, Edit, Carousel, Timeline, Export.
- Current side-panel concept.
- Edit left tool rail.
- Right inspector pattern.
- Filmstrip pattern.
- Library grid and inspector pattern.
- Carousel board and right settings panel.
- Timeline source/program/timeline structure.
- Export presets, selected assets, settings, and queue structure.

Polish:

- Color, spacing, hierarchy, text density, state behavior, hover/focus, tool HUDs, and interaction feedback.
- Tool behavior so each tool has a clear start, active, committed, canceled, disabled, and failure state.
- Edit tooling so users can quickly see what tools exist, expand a tool for variants/settings, switch tools without losing context, and customize tool defaults.

Avoid:

- Collapsing everything by default.
- Replacing the app's layout model.
- Moving major workflows to new pages.
- Renaming large parts of the product before the interaction quality is fixed.

## Scope Guardrails

This plan should optimize the current product, not become a disguised redesign.

Non-negotiables:

- Keep the current workspace map.
- Keep the current left-tool, center-canvas, right-inspector Edit shape.
- Keep Library, Carousel, Timeline, and Export recognizable from current screenshots.
- Improve shared primitives before rebuilding individual screens.
- Prefer compact menus, HUDs, and inspector sections over new full-page flows.
- Do not move a tool just because another app organizes it differently.
- Do not hide existing power features unless the same action is still one click or one menu away.

Allowed changes:

- Rebalance visual hierarchy.
- Standardize controls.
- Convert crowded button groups into menu buttons or split buttons.
- Make tool buttons expandable into clear menus, popovers, or inspector sections.
- Add clear active, pending, commit, cancel, disabled, and failure states.
- Add better empty, hover, drag, rename, reorder, and menu behavior.
- Improve layers as a premium-feeling stack without changing the basic location of layers.

## Professional Modern Aesthetic Standard

The target aesthetic is modern professional software: calm, precise, premium, fast to scan, and low-clutter. The app should feel like a serious creative tool, not a dashboard template, not a gamer skin, and not a debug panel.

The design should communicate:

- Confidence through alignment, restraint, and predictable behavior.
- Depth through subtle surfaces, not heavy shadows or glow.
- Power through visible tools, not through visual noise.
- Precision through stable controls, exact values, and clean feedback.
- Brand through selective accent color, not constant decoration.

The app should not communicate:

- Visual busyness.
- Generic SaaS card stacks.
- Decorative gradients or glowing borders.
- Debug/status text as primary UI.
- Random spacing, inconsistent row heights, or arbitrary rounded boxes.
- Every feature competing for the same visual priority.

### Minimal Clutter Standard

Minimal clutter does not mean hiding professional capability. It means every visible element earns its place.

Rules:

- One local primary action per panel or tool state.
- One active state treatment per selected object.
- One accent use per control group.
- One row height system per list.
- One toolbar baseline per zone.
- One way to open secondary actions: overflow, menu button, split button, or inspector, not all at once.
- Remove repeated labels when proximity and iconography already explain the control.
- Move diagnostics into tooltips, details, logs, or expandable disclosure.
- Prefer quiet default states and clearer hover/active states.

Clutter budget:

- A panel can have many controls, but only one should visually shout.
- A toolbar can have many buttons, but only one group should be active.
- A row can have many states, but only the most important state gets accent treatment.
- A menu can have many actions, but actions must be grouped and ordered by intent.

### Premium Restraint Rules

The design should be strict. If everything is highlighted, nothing is highlighted.

Use less of:

- Bright borders.
- Full-card outlines.
- Heavy shadows.
- Repeated chips.
- Long labels.
- Dense helper text.
- Multiple accent colors in one zone.
- Icons plus labels for obvious repeated actions.
- Nested cards inside panels.

Use more of:

- Alignment.
- Fixed columns.
- Stable row heights.
- Subtle fills.
- Consistent typography.
- Deliberate whitespace.
- Short labels.
- Tooltips for secondary explanation.
- Menus for secondary actions.
- Clear hover/focus/active behavior.

### Strong Design Aesthetic Checklist

Every important screen should pass this checklist:

- The main content is visually dominant within 1 second.
- The active workspace is obvious.
- The active tool is obvious.
- The primary action is obvious.
- Secondary actions are discoverable but quiet.
- Related controls are grouped through spacing and alignment.
- Unrelated controls are separated through spacing, not heavy boxes.
- Text hierarchy is clear without using too many font sizes.
- Accent color appears intentionally and rarely.
- Borders support structure without turning the screen into a grid.
- Hover states reveal affordance without moving layout.
- Keyboard focus is visible only when keyboard navigation is active.
- Empty, loading, disabled, and error states look designed.

### Design Choice Rules

Use these rules when a design choice is ambiguous.

Border vs fill:

- Use fill when grouping controls inside the same surface.
- Use a divider when separating large regions.
- Use a border when the object needs a boundary.
- Use an accent border only when there is no better selected/active treatment.
- Remove the border if spacing, fill, or alignment already explains the grouping.

Card vs no card:

- Use a card for a distinct object: layer row, preset row, export queue item, dialog, popover.
- Avoid cards for ordinary section grouping inside an already framed panel.
- Avoid card-inside-card unless the child is a real nested object such as a mask inside a layer.
- Prefer unframed inspector sections with spacing when the content is part of one continuous tool.

Button vs menu:

- Use a button for a direct command.
- Use a split button for a direct command with common variants.
- Use a menu for mode or action variants.
- Use a popover for controls that need preview, precision, or more than one value.
- Do not turn a frequent one-click action into a menu unless clutter is worse than the extra click.

Text vs icon:

- Use text for final, high-risk, or ambiguous commands.
- Use icons for repeated, spatially familiar controls.
- Use icon plus text when the action is important and not universally recognizable.
- Use tooltip text for secondary explanation.
- Remove labels that repeat the section title.

Accent vs neutral:

- Use accent for active, selected, primary, changed, warning, or error states.
- Use neutral for default structure, hover, inactive icons, and ordinary section headers.
- Use red/destructive only for destructive choices or destructive confirmations.
- Use amber/warning for caution states that do not block work.
- Use muted text for metadata and helper information.

Visible vs hidden:

- Keep professional controls visible when they define the workflow.
- Hide advanced or rare commands behind overflow menus.
- Hide diagnostics unless the user asks for detail or a failure occurs.
- Reveal row actions on hover only if keyboard access remains available.
- Never hide the current active state.

Density choice:

- Use compact density when the user is scanning many items.
- Use standard density when the user is editing values.
- Use relaxed density when the user is deciding, confirming, or recovering from an error.
- Do not mix density levels inside one row or toolbar.

## Easy Powerful Editing Model

The main product idea is that editing should be easy without becoming shallow. A user should be able to see the available edit tools, expand them when they need more power, move between them quickly, and customize them without the app becoming cluttered.

Editing should feel like this:

1. See the tool.
2. Select the tool.
3. Expand only when needed.
4. Adjust precise settings.
5. Apply, cancel, or switch cleanly.
6. Return to a calm workspace.

The app should never force users to choose between full functionality and a clean interface. The design should expose primary tools clearly, tuck secondary choices into understandable expansions, and keep advanced controls near the tool that owns them.

### Tool Visibility Standard

Professional editing tools should be visible enough to learn and fast enough to use.

Rules:

- Primary tool families remain visible or directly reachable from the Edit rail.
- The active tool is visible in the rail, canvas/HUD, and inspector.
- Last-used variants are visible on the tool button or nearby label.
- Tool buttons show concise tooltips with shortcut and current variant.
- Disabled tools remain visible if they teach the workflow, but explain why they are unavailable.
- Rare or advanced variants move into menus/popovers, not permanent rail buttons.
- Tool families should not be hidden under generic names if the user needs to find them quickly.

Primary Edit rail families are a grouping target, not a literal requirement that every item below becomes a permanent rail button. The rail should stay compact while keeping these families visible, grouped, or one expansion away.

Core rail groups:

- Crop/Transform.
- Adjust.
- Masks/Selections.
- Brush.
- Retouch.
- Layers.
- Compare/View.

Inspector/edit families:

- Light.
- Color.
- Tone Curve.
- Color Grading.
- Effects.
- Detail.
- Export/Send.

The exact rail grouping can stay compact, but the user should never feel like tools are buried or mysterious. If the rail becomes crowded, group related families behind a visible menu button rather than adding more permanent buttons.

### Tool Expansion Standard

Every tool family should have a predictable way to expand.

Expansion types:

- Menu: quick variants and modes.
- Split button: default action plus related variants.
- Popover: presets, color, brush tips, and controls that need preview.
- Inspector section: persistent precision controls.
- HUD: active canvas interaction controls.
- More menu: rare or destructive actions.

Rules:

- The primary click selects the tool or performs the default action.
- The chevron/overflow opens variants or secondary actions.
- Expansion appears near the trigger unless it would cover the image interaction.
- Expanded controls should be grouped by user intent, not internal implementation.
- Expansion should close cleanly on Escape.
- Switching tools closes irrelevant expansions.
- Returning to a tool restores useful last settings without restoring stale transient operations.
- A tool can have advanced controls, but the first expanded view should show the most common choices.

Examples:

- Brush button selects Brush. Brush menu exposes Add, Erase, Refine Add, Refine Erase, presets, size source, and pressure options.
- Mask button selects the last-used mask source. Menu exposes Subject, Object, Sky, Background, Linear, Radial, Rectangle, Ellipse, Brush, Lasso, and Polygon.
- Retouch button selects the last-used retouch mode. Menu exposes Remove, Heal, Patch, Clone, Dust, and Red-eye.
- Layer Add split button adds the default adjustment layer. Chevron exposes Adjustment, Fill, Text, Source/Image, and Group.
- Compare button toggles the default compare view. Menu exposes before/after split, hold-to-compare, side-by-side, and reference view.

### Tool Switching Standard

Switching tools should feel instant and safe.

Rules:

- Selecting a new tool updates the rail active state immediately.
- The canvas cursor updates immediately.
- The inspector scrolls or swaps to the relevant section without a disorienting jump.
- The HUD changes to the active tool when needed.
- Committed edits stay intact.
- Transient operations are canceled, applied, or explicitly preserved based on the tool lifecycle.
- If a tool has pending work, switching tools asks only when silent cancellation would lose meaningful work.
- Keyboard shortcuts switch tools using the same lifecycle rules as clicking.
- The previous tool and last-used variant should be remembered where useful.

Switching must not:

- Leave stale handles on the canvas.
- Leave an old menu open.
- Keep a disabled Apply button from a previous tool.
- Change the selected layer unless the new tool explicitly targets another layer.
- Hide the user's current edit context.

### Tool Customization Standard

Customization should make repeated work faster without bloating the interface.

Customization should be phased. Do not implement deep preferences before the basic tool expansion and switching model works.

Phase 1 customization:

- Last-used tool variant.
- Brush size, hardness, opacity, flow, smoothing, and preset.
- Mask overlay color/opacity.
- Crop ratio and grid overlay.
- Retouch mode and source behavior.
- Inspector section expansion state.
- Favorite presets.

Phase 2 customization:

- Layer defaults such as adjustment type, opacity, blend mode, and clipping.
- Toolbar visible/hidden compact actions where appropriate.
- Workspace-level preferences.
- Saved custom tool menus or favorite action sets.

Rules:

- Customization lives in tool menus, popovers, inspector sections, or preferences.
- The default interface stays clean.
- A changed/customized state is visible but quiet.
- Reset to default is always available for meaningful customization.
- Saved presets should be named in user language, not internal terms.
- Customization should follow the user across sessions only when it improves speed and does not resurrect stale edits.
- Phase 1 customization should be completed and verified before Phase 2 customization begins.

### Full Functionality Without Clutter

The app should keep professional depth while staying clean.

Pattern:

- Primary tools visible.
- Common variants one click away.
- Precision controls in the inspector.
- Live interaction controls in the HUD.
- Rare actions in More menus.
- Diagnostics hidden until needed.
- Destructive actions separated.

Rules:

- Do not remove functionality to make the UI look cleaner.
- Do not expose every action permanently to prove the tool is powerful.
- Do not duplicate the same action in rail, toolbar, inspector, and context menu unless each placement has a clear use.
- Do not make users hunt through unrelated panels for the settings of the active tool.
- Keep tool-specific settings close to the active tool's inspector or HUD.
- Keep global settings out of the active editing surface unless they affect the current task.

## Editing Visibility Model

The app should be deliberate about what is visible at every moment in the editing process. Clean UI does not mean fewer capabilities. It means each capability has the right visibility level for the current state.

Visibility levels:

- Always visible: core navigation, active workspace, main media surface, primary tool families, selected asset/layer context, one local primary action.
- Contextually visible: active tool HUD, active variant, relevant inspector section, pending operation controls, selected row actions, layer/mask targeting.
- One expansion away: tool variants, presets, secondary actions, advanced options, sorting/filtering options, view modes, export variants.
- On hover/focus: row actions, reorder handles, quick visibility/lock actions, compact affordances where space is tight.
- Diagnostic only: logs, model/runtime detail, internal IDs, verbose failure details, raw technical status.

Rules:

- Do not remove a function to reduce clutter. Move it to the correct visibility level.
- Do not permanently show every function to prove it exists. Use menus, popovers, inspectors, and HUDs.
- Do not hide the active state, selected target, pending operation, primary action, or failure recovery.
- Do not hide tool families so deeply that users cannot learn the editor.
- Do not hide disabled controls when their visibility teaches the workflow; show the reason instead.
- Do not show diagnostics in the primary UI unless recovery depends on them.

### Editing State Visibility

At rest in Edit:

- Visible: top workspace nav, Edit rail/tool families, image stage, selected asset/layer context, relevant inspector sections, filmstrip, bottom rating/flag actions.
- One expansion away: tool variants, presets, compare/view modes, secondary layer actions, advanced settings.
- Hidden/quiet: transient HUDs, pending Apply/Cancel controls, diagnostics.

When selecting a tool:

- Visible: active rail state, active tool/variant label, relevant inspector section, cursor change if applicable.
- Contextual: HUD only if the tool needs live canvas interaction.
- One expansion away: variants, presets, customization, advanced options.
- Must not happen: old tool HUDs, stale menus, or old pending states remain visible.

While actively editing on canvas:

- Visible: canvas feedback, cursor/handles/brush/mask/crop/transform overlay, compact HUD with current operation.
- Contextual: Done/Apply/Cancel only when an operation can be committed or discarded.
- Quiet: unrelated inspector sections and nonessential chrome.
- Must not happen: the image becomes visually secondary to controls.

When a tool has pending work:

- Visible: pending state, Apply/Done, Cancel, and the affected target.
- Contextual: Regenerate/Retry if the tool supports it.
- One expansion away: secondary settings that can still affect the pending result.
- Must not happen: switching tools silently destroys meaningful pending work.

When a menu/popover is open:

- Visible: trigger, selected/current item, grouped actions, disabled reasons, keyboard focus.
- Quiet: unrelated hover actions behind the menu.
- Must not happen: destructive actions appear first, active state becomes unclear, or menu covers critical canvas feedback.

When using layers:

- Visible: selected layer, active mask if any, visibility/lock/mask state, opacity/blend summary, Add Layer action.
- Contextual: row actions, reorder handle, rename field, mask actions, transform state.
- One expansion away: duplicate, export layer, clipping, advanced mask actions, destructive delete.
- Must not happen: the user cannot tell whether edits target the layer, mask, or base image.

When an error or disabled state occurs:

- Visible: concise reason and recovery action.
- One expansion away: technical detail.
- Must not happen: the canvas is blocked for recoverable failures or the user gets a disabled control with no reason.

When exporting or processing:

- Visible: queue/progress state, current item, destination, retry/cancel/reveal actions.
- Quiet: finished rows and non-actionable metadata.
- One expansion away: detailed logs, exact encoder/runtime details, advanced destination settings.

### Function Preservation Rule

Every current function should be assigned to one of these buckets before implementation:

- Primary visible.
- Contextually visible.
- One expansion away.
- Hover/focus visible.
- Diagnostic only.
- Deprecated intentionally.

Do not delete or hide functionality unless it is explicitly placed in `Deprecated intentionally` with a reason. If a control is moved into a menu, popover, HUD, or inspector section, the implementation ticket must state where it moved and how a user discovers it.

### Visibility Audit Checklist

Each page/ticket should answer:

- What must always be visible on this page?
- What appears only when a tool, row, layer, clip, or asset is selected?
- What appears only during active pointer/keyboard interaction?
- What appears only while work is pending?
- What actions are one expansion away?
- What actions are hover/focus only?
- What information is diagnostic only?
- What functionality moved, and where did it move?
- Can a new user find the primary workflow?
- Can a power user reach the full functionality quickly?

## What Was Wrong With The Bigger Redesign Plan

The earlier design document correctly identified clutter, but it overcorrected. It proposed too many structural changes:

- Collapsed Library rails.
- New Import stepper.
- New Edit target model.
- Moving Brush under Select.
- Making Retouch a separate family.
- Object-aware Carousel inspector.
- Timeline and Export layout rewrites.

Those are too large for the current need. The better approach is an optimization pass over the existing app.

## Critique Of This Plan

This plan is much closer to the right direction, but it still needs discipline.

Problems to correct:

1. It is broad enough to become a wishlist.
   - Correction: drive implementation through shared primitives, then one tool family at a time.

2. It names modular components but does not yet force ownership.
   - Correction: define which component owns active state, menu state, disabled reasons, and commit/cancel behavior.

3. It says buttons should expand into menus, but not when.
   - Correction: use the button/menu decision matrix below so menus improve clarity instead of hiding features randomly.

4. It gives layers good visual direction, but not enough row-level structure.
   - Correction: add a strict layer row spec with zones, state priority, hover rules, and drag behavior.

5. It describes tool behavior per section, but needs one shared lifecycle.
   - Correction: every tool should follow the same inactive, armed, active, pending, committed, canceled, disabled, and failed lifecycle.

6. It needs stronger acceptance gates.
   - Correction: verify polish through screenshots, keyboard behavior, resize behavior, and interaction state checks, not only visual opinion.

The key critique: keep the ambition high, but make the work modular enough that every pass leaves the current app better without forcing a large IA change.

## Plan Corrections For Implementation

These corrections prevent the plan from being misread during implementation.

1. Treat "premium" as measurable behavior.
   - Use spacing, sizing, state, screenshot, and interaction gates instead of subjective taste language.

2. Do not use border/card count as a vanity metric.
   - The real rule is that every visible border or card must carry structure. Remove decorative borders, but keep useful boundaries.

3. Keep the Edit rail compact.
   - Tool families should be visible, grouped, or one expansion away. Do not turn every edit family or variant into a permanent rail button.

4. Phase customization.
   - Last-used variants, brush settings, mask overlay, crop ratio, retouch mode, inspector expansion, and favorite presets come first. Deep preferences and workspace-level customization come later.

5. Isolate layer work.
   - Layers are high-risk because visual rows connect to masks, selection, transforms, ordering, visibility, opacity, and inspector targeting.

6. Require behavior proof, not only screenshots.
   - Screenshots prove visual hierarchy. They do not prove keyboard behavior, Escape semantics, undo grouping, disabled reasons, pending cleanup, or layer/mask targeting.

7. Never hand the master spec to an agent as one task.
   - Convert it into a small ticket with exact files, non-goals, design rules, behavior rules, and verification.

## Premium Polish Principles

### 1. Keep Power Visible, Reduce Noise

Professional users do not want tools hidden. They want the visible tools to be orderly.

Rules:

- Keep the current controls available.
- Reduce repeated labels.
- Downgrade secondary actions.
- Move diagnostic strings out of primary surfaces.
- Keep exact values visible for active controls.
- Keep canvas/tool feedback immediate.

### 2. One Active Thing

Every workspace should make one thing visually dominant:

- Library: selected image/contact sheet.
- Edit: current photo and active tool.
- Mask/brush: active selection area.
- Layers: selected layer.
- Carousel: selected board/object.
- Timeline: selected clip/playhead.
- Export: selected preset and queue readiness.

### 3. Every Tool Needs A Behavior Contract

Each tool should define:

- Entry: how it starts.
- Active state: what changes visually.
- Primary action: what the user is expected to do.
- Feedback: what happens during pointer/keyboard interaction.
- Commit: what confirms the action.
- Cancel: what backs out safely.
- Disabled state: why the tool is unavailable.
- Error state: how recovery works.

### 4. Controls Should Behave Like Instruments

Premium means precise and calm:

- Sliders move smoothly and update the image without jank.
- Value readouts are stable and do not resize rows.
- Dragging groups into one undo step.
- Hover reveals affordance without visual shouting.
- Active state is obvious but not neon.
- Disabled controls explain why in a tooltip or small inline reason.

### 5. Motion Should Confirm, Not Perform

Use motion sparingly:

- 100-160 ms fades for hover/active state.
- 140-220 ms slide for panel open/close.
- No bouncing.
- No animated decorative effects.
- Brush/crop/mask overlays update immediately.
- Progress uses quiet, determinate indicators when possible.

### 6. Make The UI Modular

The current app should feel like a set of reusable professional instruments, not one giant hand-built surface per workspace.

Rules:

- Tool rails, split buttons, menu buttons, inspector sections, layer rows, HUDs, status chips, and swatch controls should be reusable modules.
- A tool should be able to declare its icon, label, shortcut, active state, enabled state, disabled reason, primary action, secondary menu actions, HUD, inspector section, cursor, and status text.
- Button menus should absorb overflow instead of showing every command as a permanent button.
- Tool switching should be driven by one shared tool model, not ad hoc button state scattered across the UI.
- Layers should use one shared row system everywhere they appear.

### 7. Buttons Should Expand Into Menus

Buttons that represent a family of actions should become menu buttons or split buttons.

Rules:

- Primary click performs the obvious default action.
- Chevron, long press, or right click opens related actions.
- Menu labels use plain user-facing language.
- Menu items include icons where they improve scanning.
- Dangerous items stay grouped at the bottom.
- Disabled menu items explain why.
- Menus should be short. If a menu needs more than 8-10 items, group it or use a popover.

Examples:

- Import split button:
  - primary: Import to VIREL Vault
  - menu: Import Folder, Import Selection, Import With Preset, Refresh Sources
- Export split button:
  - primary: Start Export or Queue Export
  - menu: Export Selected, Export With Crop, Reveal Destination, Manage Presets
- Mask source menu:
  - Subject, Object, Sky, Background, Brush, Lasso, Polygon, Linear, Radial, Rectangle, Ellipse
- Layer Add split button:
  - primary: Add Adjustment Layer
  - menu: Adjustment, Fill, Text, Source, Group
- Layer row More menu:
  - Rename, Duplicate, Move Up, Move Down, Clip To Below, Transform, Export Layer, Delete
- Brush menu:
  - Brush Add, Brush Erase, Refine Add, Refine Erase, Brush Presets

## Visual Polish System

### Color

Current problem: bright mint/cyan is doing too many jobs: active state, primary action, success, border, mask, status, and decoration.

Recommendation:

- Keep mint/cyan as the brand/system accent, but use it less.
- Use color only for state and action.
- Use neutral contrast for most structure.
- Use subtle fill for selected rows instead of loud borders.
- Use destructive red only for destructive actions.

Token behavior:

- Active tool: one accent edge or small filled indicator.
- Selected row: subtle tinted fill plus one accent edge.
- Hover: neutral lift, not accent.
- Primary action: accent border/fill.
- Changed value: tiny accent dot or value color.
- Mask overlay: cyan/mint overlay, but control chrome stays quieter.

Accent discipline:

- Accent should never be the default border color.
- Accent should not appear on every slider thumb at rest.
- Accent should not be used for generic hover.
- Accent should not be used for ordinary icons unless the icon is active or changed.
- Success green and brand mint should not compete in the same zone.
- Storage, progress, and status meters should use quieter variants unless they require attention.
- If a screen has more than three simultaneous accent elements, reduce the lowest-priority ones to neutral.

### Borders

Current problem: too many rectangular controls have high-contrast borders.

Rules:

- Default controls use low-contrast borders.
- Hover increases fill, not border.
- Active state uses one strong edge.
- Focus ring appears only for keyboard focus.
- Avoid double-accent: do not combine bright border + bright text + bright corner mark on every button.

Professional border behavior:

- Borders define structure; they should not decorate every object.
- Use dividers for major panel boundaries.
- Use fills for grouped controls.
- Use accent edges for selection.
- Use focus rings only for keyboard focus.
- Use warning/error borders only with an accompanying message or chip.
- Remove any border that repeats a nearby divider or fill contrast.

### Typography

Current problem: too much mono and too many status-like strings make the UI feel technical rather than luxurious.

Rules:

- Use mono for values, file names, dimensions, timecode, and exact readouts.
- Use UI sans for labels, commands, section names, and descriptions.
- Avoid long inline technical sentences.
- Use sentence case for most labels.
- Use compact labels in controls, longer descriptions in tooltips.

Type hierarchy:

- Workspace tabs: medium emphasis, stable position.
- Panel titles: small caps or semibold only if already used consistently.
- Section titles: medium weight, not oversized.
- Row labels: regular weight, muted but readable.
- Values: mono or tabular figures, fixed width where values change.
- Metadata: low emphasis.
- Warnings: short, clear, and paired with an icon/chip.

Rules:

- Avoid using all caps for ordinary controls.
- Avoid more than 3 text sizes in a single panel.
- Avoid mixing mono and sans in the same label/value unless the value needs precision.
- Do not use body text to explain obvious controls inside the UI.
- Put long explanation in tooltips, documentation, or diagnostics.
- Prefer "Exposure" over "Exposure Adjustment".
- Prefer "+1.23 EV" over "Exposure value +1.23 EV".

### Text Fit And Truncation Rules

Truncated labels with visible `...` make the UI feel unfinished. The default goal is complete, readable text.

Rules:

- Avoid ellipses in buttons, menus, tabs, tool names, preset names, primary actions, and visible workflow labels.
- Do not design controls whose common labels only fit when truncated.
- Use shorter user-facing labels before using truncation.
- Increase menu/popover width when the label is important and the container can grow.
- Let menu items wrap to two lines only when the menu is designed for multi-line items.
- Use responsive columns for preset/style grids so names have enough width.
- Use icon-only buttons with tooltips for repeated actions when text would not fit cleanly.
- Use fixed-width value columns for values, but not for descriptive labels.
- Use middle truncation only for file paths, long filenames, URLs, and technical identifiers.
- If truncation is unavoidable, provide the full value in tooltip, detail panel, or accessible name.

Preferred fixes before truncation:

1. Rename the label shorter.
2. Widen the menu/popover/control.
3. Move the label into a tooltip and use an icon.
4. Wrap to two lines in a purpose-built row.
5. Move detail text into a secondary line.
6. Use a responsive layout with fewer columns.
7. Truncate only low-priority metadata.

Do not:

- Use `Clean Commer...` for a selected style when `Clean Commercial` could fit in a wider cell.
- Use `Cancel & Roll B...` for an important command.
- Use multiple truncated preset names in the same grid.
- Truncate a destructive, primary, or final action.
- Truncate enough labels that the user has to hover every item to understand the UI.

### Spacing And Rhythm

Rules:

- Side panel internal margins should be consistent.
- Rows should have stable heights.
- Buttons in a segmented group should share height and width rhythm.
- Inspector section spacing should create hierarchy without extra boxes.
- Avoid nested cards inside cards.

Modern spacing behavior:

- Use spacing to group, not borders.
- Use fixed gutters for panel interiors.
- Keep left edges aligned inside each panel.
- Keep value columns aligned inside inspector sections.
- Do not center-align controls in dense professional panels unless the whole group is centered, such as bottom rating controls.
- Do not use large empty gaps to make a dense tool look premium; refine hierarchy instead.

### Icons

Rules:

- Repeated commands should become icon-first.
- Text remains where ambiguity matters: Export, Save, Apply, Delete, Start Export.
- Every icon-only control needs a tooltip and accessible name.
- Icon sizes should be consistent per surface:
  - Top/tool rail: 16 px.
  - Inspector buttons: 14-16 px.
  - Status chips: 10-12 px.

Icon style:

- Icons should use one stroke weight per surface.
- Icons should align optically, not just mathematically.
- Active icons may use accent; inactive icons should be neutral.
- Tool icons should be recognizable at rail size without labels.
- Avoid decorative icons in section headers if they add clutter.
- If an icon cannot be understood quickly, use text or icon plus text.

### Measurement System

The UI should use a small spacing scale so screens feel designed instead of hand-positioned.

Base unit:

- Use a 4 px base grid.
- Prefer these spacing values: 2, 4, 6, 8, 12, 16, 20, 24, 32, 40.
- Avoid one-off values unless matching image aspect ratio, thumbnail crop, or platform text metrics.

Default rhythm:

- Tight icon/text gap: 6 px.
- Button icon/text gap: 8 px.
- Row internal horizontal padding: 12-16 px.
- Compact row vertical padding: 8 px.
- Inspector section internal padding: 16 px.
- Panel edge padding: 20-24 px.
- Gap between major panels: 16-20 px.
- Gap between canvas and filmstrip: 20-24 px.
- Gap between related controls: 8 px.
- Gap between unrelated control groups: 16-20 px.

Do not use spacing to create drama. Use spacing to make control groups readable, predictable, and calm.

### Layout Zones

The current screenshot has a strong overall structure. Preserve it, but make every zone obey a shared rhythm.

App shell:

- Top navigation height should be stable across workspaces.
- Top nav items should share equal vertical alignment and active underline position.
- Workspace content begins on one consistent Y axis below the top nav.
- App version/file status stays visually secondary and should not compete with active workspace tabs.

Left tool rail:

- Rail width should remain fixed.
- Tool button hit target: 44-48 px.
- Tool icon: 16-18 px.
- Tool button visual container: 40-44 px.
- Vertical gap between tool buttons: 8-12 px.
- Active tool uses one accent fill or one accent edge, not both plus glowing icon.
- Rail separators should be used only between real tool groups.
- Bottom settings button aligns to the same rail centerline as all other tools.

Library/sidebar panel:

- Panel width should be stable.
- Section headers use consistent top spacing.
- Tree/list rows use one row height system.
- Selected list row uses subtle fill plus one accent edge.
- Counts align to a fixed right column.
- Storage meter should sit in a quiet utility zone and not look like a primary task.

Canvas/stage:

- Canvas should be the visual dominant object.
- Canvas chrome should be quieter than the image.
- Top canvas controls should align to one toolbar baseline.
- Image corners should be consistent with the app surface radius.
- Empty stage, loading, and failure states should use the same stage bounds.

Filmstrip:

- Filmstrip container should feel like a dock, not another heavy card.
- Thumbnail sizes should be stable.
- Selected thumbnail uses a single clear state: accent border or accent fill, not multiple decorations.
- Thumbnail badges stay inside one corner and never overlap the subject enough to obscure the image.
- Filmstrip actions below should align to the centerline and not float randomly.

Right inspector:

- Inspector panel width should be stable.
- Inspector sections should align to a shared left edge.
- Section headers, slider rows, value readouts, and chevrons should use fixed columns.
- Collapsed sections should share one height.
- Expanded sections should avoid nested cards unless the nested object is truly separate.
- Bottom action bar should have a fixed height and clear primary/secondary split.

### Surface And Radius Rules

Surface hierarchy should be visible through fill, elevation, and radius, not heavy outlines.

Radii:

- App/window shell: follow platform/container radius.
- Major panels: 10-14 px.
- Canvas/image: 6-8 px.
- Inspector sections/cards: 8-10 px.
- Buttons, rows, and chips: 6-8 px.
- Tiny badges: 4-6 px.

Surface levels:

- Level 0: app background.
- Level 1: panels, rails, timeline wells, filmstrip dock.
- Level 2: inspector sections, selected list containers, toolbars.
- Level 3: menus, popovers, active HUDs.

Rules:

- Do not put a Level 2 card inside another Level 2 card unless it is a real nested object.
- A surface may use fill contrast, border, or shadow. Avoid using all three at full strength.
- Hover should usually change fill, not radius or layout.
- Active state should be local and precise, not a glow across the whole component.

### Border And Outline Rules

The screenshot already has many soft rectangles. The premium version needs fewer visible outlines, not more.

Border tokens:

- Hairline divider: 1 px low-contrast line.
- Default control border: 1 px, barely above fill.
- Hover border: same width, slightly higher contrast.
- Active border: 1 px accent only when there is no accent edge or fill.
- Focus ring: 2 px, keyboard-only, outside the component when possible.
- Error border: 1 px warning/destructive color plus message or chip.

Rules:

- Never use bright accent border on every selected, active, and primary element at the same time.
- Prefer a left accent edge for selected rows in lists and layers.
- Prefer a filled active state for selected tool buttons.
- Prefer a single accent underline for active top nav.
- Use dividers between large regions, not around every small control.
- Menus and popovers may use a stronger shadow than panels because they float above the workspace.

### Button Rules

Buttons should have predictable sizes and visual weight.

Button sizes:

- Rail tool button hit target: 44-48 px.
- Compact icon button: 32-36 px.
- Inspector row icon button: 28-32 px.
- Standard text button: 40-44 px high.
- Primary bottom action button: 44-48 px high.
- Toolbar segmented button: 36-40 px high.

Hierarchy:

- Primary: one per local area, filled or strong accent.
- Secondary: neutral fill with low-contrast border.
- Tertiary: icon-only or text-only with hover fill.
- Destructive: neutral until confirmation moment, then red/destructive.
- Disabled: visible, muted, explanatory.

Rules:

- Do not show more than one primary button in the same local action zone.
- Icon-only buttons need tooltips and accessible names.
- Buttons in the same group must share height.
- Split buttons must clearly separate primary click from menu chevron.
- Button labels should use verbs: Import, Export, Reset, Apply, Cancel, Rename.
- Avoid long button text inside compact inspectors.
- Button labels should fit without ellipses.
- If a button label does not fit, shorten the label, widen the button, use icon-only plus tooltip, or move the action into a menu.
- Primary, destructive, Apply, Cancel, Done, Export, Save, and reset commands must not be truncated.

### Slider Rules

Sliders are a major part of the perceived quality of the editor. They must feel precise and stable.

Slider row anatomy:

- Label column: fixed width per inspector group.
- Track column: flexible width.
- Value column: fixed width.
- Reset/changed affordance: fixed position.

Sizing:

- Row height: 44-48 px.
- Track height: 2 px at rest.
- Filled track: 2 px.
- Thumb size: 12-14 px.
- Hover thumb size may increase by 1-2 px without shifting layout.
- Value readout width must handle the largest expected value.

Behavior:

- Drag updates preview continuously.
- Drag release creates one undo step.
- Double-click value or reset icon returns to default.
- Shift/Option modifiers can provide fine/coarse adjustments if supported.
- Changed values show a subtle dot or value tint.
- Disabled sliders keep the label readable and explain why.

Visual rules:

- Do not make every active slider bright mint.
- Use neutral track plus subtle accent fill only where the changed/active state matters.
- Value readout should be quieter than the image but clearer than helper text.
- Slider rows should not resize when values change.

### Toolbar Rules

Toolbars should read as compact instruments, not miscellaneous button bins.

Canvas toolbar:

- Place view and interaction controls on one baseline.
- Keep zoom/fit controls grouped separately from compare/view mode controls.
- Use segmented controls for mutually exclusive view modes.
- Use icon buttons for repeatable commands.
- Keep toolbar height stable.
- Toolbar should not overlap the canvas content.

Bottom action toolbar:

- Center rating/flag/tag controls when they relate to the selected image.
- Keep batch or overflow actions on the right.
- Use dividers only when groups have different intent.
- Do not let bottom controls compete with the filmstrip selection state.

Inspector toolbar/header:

- Title left, reset/overflow right.
- Changed indicator near title.
- Collapse chevron aligned to the same right column in every section.
- Avoid placing primary save/export actions inside section headers.

### Menu And Popover Rules

Menus are for quick decisions. Popovers are for richer controls.

Menu geometry:

- Menu min width: 180 px.
- Menu max width: 320 px before using a popover.
- Menu item height: 32-36 px.
- Menu padding: 6-8 px.
- Icon column: fixed.
- Shortcut column: fixed right alignment.
- Destructive group separated by divider.

Menu behavior:

- Open from chevron, long press, right click, or keyboard.
- First item should not be destructive.
- Checked/current item is clearly marked.
- Disabled item explains why in tooltip or secondary text.
- Escape closes menu without changing selection.
- Arrow keys navigate; Enter selects.
- Menus should not contain sliders, large previews, or multi-step forms.
- Menu item labels should fit without ellipses.
- If multiple menu labels truncate, widen the menu, shorten labels, group items differently, or use a popover.
- Checked/current items should show the full selected label somewhere visible.

Popover behavior:

- Use for brush preset editing, color picking, export preset details, and any control that needs preview or precision.
- Popovers should have a clear title only when the trigger label is not enough.
- Popovers close on outside click only if there are no unsaved transient changes.
- Done/Cancel appears when closing could discard meaningful state.

### Density Rules

Premium does not mean sparse. It means every dense area has rhythm.

Use compact density for:

- Tool rails.
- Filmstrip actions.
- Slider rows.
- Layer rows.
- Timeline clips.

Use standard density for:

- Inspector sections.
- Library project rows.
- Export preset rows.
- Menus.

Use relaxed density for:

- Empty states.
- Confirmation dialogs.
- Import/export progress summaries.

Rules:

- Do not mix compact and relaxed density inside the same control group.
- Dense controls need stronger alignment and quieter borders.
- Relaxed controls need fewer labels and more direct action hierarchy.

### Screenshot-Level Aesthetic Review

Every polished screen should be judged like a product screenshot.

First-glance review:

- Can the viewer identify the workspace?
- Can the viewer identify the selected asset/object/clip/layer?
- Can the viewer identify the active tool?
- Does the image/canvas/board/timeline dominate?
- Does any secondary control pull too much attention?
- Does the screen feel calm before reading text?

Close-inspection review:

- Are panel gutters consistent?
- Do row heights match within each list?
- Are values aligned?
- Are icons aligned and consistent?
- Are section headers consistent?
- Are borders lower contrast than content?
- Are selected, hover, active, disabled, and focus states distinct?
- Do menus and popovers feel related to their trigger?
- Does text elide cleanly before overlap?

Clutter review:

- Count accent elements. Reduce any accent that is not selected, active, primary, changed, or warning.
- Count visible borders. Remove borders that duplicate spacing, fill, or dividers.
- Count labels. Remove labels that repeat a section heading or obvious icon.
- Count chips. Remove default-state chips and keep only meaningful exceptions.
- Count nested surfaces. Flatten any card-inside-card pattern that is not a true nested object.

Premium failure signs:

- The UI looks like many independent components pasted together.
- The canvas feels less important than the controls.
- Every section has the same visual weight.
- Hover state changes layout.
- Values make sliders jump.
- Menus are long, ungrouped, or destructive at the top.
- Active state uses border, fill, text color, icon color, and shadow at once.
- The screen needs paragraphs of helper text to explain itself.

### Per-Surface Design Rules

Top navigation:

- Active workspace uses one underline or small accent marker.
- Inactive workspaces stay neutral.
- Do not use pill buttons for every workspace tab.
- Keep file/status metadata low emphasis on the far side.

Left rail:

- Rail buttons should feel like tools, not navigation cards.
- Keep icons centered and consistent.
- Use a single selected state.
- Use tooltip labels instead of permanent text.
- Group only when the group changes tool intent.

Project/library list:

- Selected row uses subtle fill and one accent edge.
- Counts align to the right.
- Folder/project icons stay neutral unless active.
- Section labels should be quieter than selectable rows.
- Storage meter is utility information, not a primary action.

Canvas controls:

- View controls should feel detachable from image editing controls.
- Zoom, fit, compare, layout, and fullscreen should use compact grouped controls.
- The toolbar should not look brighter than the photo.
- Active view mode uses one selected segment.

Filmstrip:

- Selection state should be clear but not louder than the main image.
- Badges should be small and consistent.
- Thumbnail gutters should be even.
- Filmstrip actions should not fight the selected thumbnail.

Inspector:

- Expanded section is allowed to be visually stronger than collapsed sections.
- Collapsed sections should not look like large buttons competing with primary actions.
- Slider rows need fixed label/value columns.
- Bottom action buttons should have clear hierarchy: Reset secondary, Save Preset primary.
- Section icons should help scanning or be removed.

Layers:

- Layers are a premium surface and deserve the strictest alignment.
- Row thumbnails, names, states, opacity, and actions must sit on fixed columns.
- Active layer, active mask, locked, hidden, and hover cannot all use the same styling.
- Default states should not be chipped.
- Destructive actions stay in menus or confirmation flows, not exposed beside selection.

Menus:

- Menus should feel crisp and purposeful.
- Keep item labels short.
- Group related actions.
- Put destructive actions last.
- Show checked/current state for active mode.
- Use popovers when the user needs precision, previews, or multiple values.

Dialogs and confirmations:

- Use relaxed density.
- Keep text short.
- Make the irreversible action explicit.
- Primary action should match the user's likely intent.
- Destructive confirmation should not inherit the app's positive accent.

## Modular UI Architecture

This is the practical direction for making the app cleaner without moving the major layout.

### Core Modules To Standardize

1. `ToolRail`
   - Vertical or horizontal list of tool buttons.
   - Supports active tool, disabled reason, shortcut tooltip, grouped tools, and last-used variant.
   - Makes primary tool families easy to see and switch.
   - Does not permanently show every variant.

2. `ToolButton`
   - Icon-first button used in rails and compact toolbars.
   - Has active, hover, pressed, disabled, warning, and pending states.
   - May show a small variant indicator when a last-used variant matters.
   - Opens a menu or popover only when the tool family has variants.

3. `MenuButton`
   - Button with attached menu.
   - Used when one command has related variants.
   - Supports default action plus menu actions.
   - Keeps secondary choices discoverable without turning the rail into a grid of buttons.

4. `SplitButton`
   - Left side performs primary action.
   - Right chevron opens menu.
   - Best for Import, Export, Add Layer, Mask Source, Apply Preset, and Start Retouch.
   - Must make the primary/default action obvious.

5. `InspectorSection`
   - Standard header: title, changed dot, before/after, reset, overflow.
   - Standard body: rows, sliders, swatches, segmented controls.
   - Holds persistent precision controls for the active tool.

6. `ToolHud`
   - Compact on-canvas or stage-adjacent active tool control.
   - Used by Crop, Brush, Polygon/Lasso, Retouch, Layer Transform, and Pixel Inspector.
   - Holds temporary interaction controls such as Done, Cancel, size, mode, and live hints.

7. `LayerRow`
   - Shared visual row for base image, adjustment layer, fill layer, text layer, source layer, group, and nested mask rows.
   - Supports collapsed and expanded states.

8. `StatusChip`
   - One-line state surface for rendering, importing, exporting, model/runtime, retry, and warning states.

9. `ValueSliderRow`
   - Consistent label, slider, value, reset, changed marker.
   - Prevents slider/value text from resizing rows.

10. `SwatchControl`
   - Used for background, shape, text, mask overlay, labels, and color pickers.

### Component Ownership Rule

Each reusable UI primitive should own one layer of behavior and expose the rest through simple data.

Ownership:

- `ToolRail` owns tool grouping, active tool display, last-used variant display, and shortcut tooltips.
- `ToolButton` owns hover, pressed, selected, disabled, pending, and warning visuals.
- `MenuButton` owns menu open/close, keyboard navigation, checked states, disabled reasons, and dangerous-action grouping.
- `SplitButton` owns primary click versus menu click behavior.
- `InspectorSection` owns expansion, reset, before/after, changed state, and overflow.
- `ToolHud` owns transient tool feedback, compact values, Done/Cancel, and live operation hints.
- `LayerRow` owns row selection, row hover, row menus, drag affordance, rename, nested mask display, and row state priority.

State ownership:

- Active workspace belongs to the app shell.
- Active tool belongs to the shared tool model.
- Active tool variant belongs to the tool model, not an individual button.
- Active layer belongs to the layer model.
- Active mask belongs to the layer model as a child target.
- Temporary canvas operations belong to the active tool controller.
- Committed edits belong to the edit pipeline/history.

This prevents one-off QML states from creating slightly different behavior for every surface.

### Button/Menu Decision Matrix

Use this matrix before adding or changing a button.

Plain button:

- Use for one command with one obvious result.
- Examples: Reset, Apply, Cancel, Delete, Rename, Reveal.
- Keep text only when the command is high-risk, final, or ambiguous.

Icon button:

- Use for repeated low-texture actions.
- Examples: visibility, lock, solo, reset section, before/after, zoom fit.
- Requires tooltip and accessible name.

Segmented control:

- Use for mutually exclusive modes that users compare quickly.
- Examples: Add/Erase, Linear/Radial, Fit/Fill, Before/After view.
- Do not use if the set has more than 5 choices.

Menu button:

- Use for one visible command with related alternatives.
- Examples: Mask Source, Brush Preset, Sort, View Options, Retouch Mode.
- Primary surface shows the current selected option.

Split button:

- Use when there is a safe common default plus secondary options.
- Examples: Import, Export, Add Layer, Apply Preset.
- Primary side performs the default. Chevron opens variants.

Popover:

- Use when choices need previews, sliders, color swatches, or multi-row controls.
- Examples: color picker, brush preset editor, export preset details.
- Do not force rich controls into a menu.

Inspector section:

- Use when the user needs persistent values and precision.
- Examples: adjustment sliders, crop ratio, layer opacity/blend, export settings.
- Do not hide precision controls behind hover-only menus.

Rule of thumb:

- If the user is choosing a mode, use a segment or menu.
- If the user is executing a command, use a button.
- If the user needs both a default command and variants, use a split button.
- If the user needs precision or preview, use an inspector section or popover.

### Tool Model Contract

Each tool should be represented by data rather than custom button logic.

Required fields:

- `id`
- `label`
- `icon`
- `shortcut`
- `category`
- `family`
- `variant`
- `lastUsedVariant`
- `enabled`
- `disabledReason`
- `active`
- `primaryAction`
- `menuActions`
- `customizationActions`
- `hudComponent`
- `inspectorSection`
- `cursor`
- `statusText`
- `pendingState`

This makes tools easier to switch, render, search, and expose in the command palette.

Tool discovery behavior:

- Rail renders tool families.
- Tool button or HUD shows the active variant.
- Menu renders available variants and secondary actions.
- Inspector renders precision controls for the active tool.
- HUD renders temporary live controls.
- Command palette/search can find tools, variants, and actions by label.

### Menu Action Contract

Each menu action should declare:

- `label`
- `icon`
- `shortcut`
- `enabled`
- `disabledReason`
- `destructive`
- `checked`
- `separatorBefore`
- `handler`

Menu labels should describe outcomes:

- Good: "Add adjustment layer"
- Good: "Duplicate layer"
- Good: "Remove from selection"
- Bad: "Layer op 2"
- Bad: "Mask source generated"
- Bad: "Target provider"

### Inspector Section Contract

Each section should declare:

- `title`
- `icon`
- `visible`
- `expanded`
- `changed`
- `disabledReason`
- `beforeAfterAvailable`
- `resetAvailable`
- `menuActions`

This gives every tool consistent behavior:

- Changed state is always in the same place.
- Reset always behaves the same.
- Before/after always reads the same.
- Overflow menus prevent button clutter.

### Tool Lifecycle Contract

Every interactive tool should move through the same lifecycle. The visuals can vary by tool, but the state names and user expectations should stay consistent.

States:

- `inactive`: tool is available but not selected.
- `disabled`: tool is visible but unavailable with a clear reason.
- `armed`: tool is selected and ready for pointer or keyboard input.
- `active`: user is currently dragging, painting, selecting, transforming, or previewing.
- `pending`: operation has produced a temporary result that can be applied, regenerated, or canceled.
- `committed`: operation has been accepted into history.
- `canceled`: transient state was discarded without mutating committed edits.
- `failed`: operation could not complete, with Retry or fallback available.

Required behavior:

- Switching tools clears only transient state, not committed edits.
- Escape cancels the current transient operation first.
- Escape a second time exits the tool only when that tool has no pending operation.
- Enter or Done commits when a pending operation exists.
- Undo reverts the last committed operation, not every pointer movement.
- Disabled tools remain visible and explain why.
- Failed operations remain pending until the user retries, cancels, or accepts a fallback.

This lifecycle should be the shared mental model for Crop, Brush, Lasso, Smart Selection, Retouch, Layer Transform, Carousel object tools, and Timeline trim/drag tools.

## Global UI Behavior Standards

### Tool Activation

When a tool is selected:

- Tool rail active state changes immediately.
- Canvas cursor changes immediately if relevant.
- Right inspector scroll position should move to the relevant section without disorienting jump.
- Generic canvas chrome that conflicts with the tool should fade or quiet down.
- Bottom/inline HUD appears only if the tool needs live interaction.

Acceptance:

- User can tell the active tool without reading more than one label.
- Switching tools clears transient state safely.
- Cancel returns to the previous stable tool or Adjust.

### Hover

Hover should answer "what can I do here?"

Rules:

- Buttons lift subtly.
- Rows reveal overflow/actions on hover only where space is crowded.
- Canvas handles get slightly larger hit feedback.
- Tooltips appear after a short delay.
- Hover should never change layout size.

### Press/Drag

Rules:

- Press state should be immediate.
- Dragging a slider, brush, crop handle, layer handle, or timeline clip should create continuous visual feedback.
- Continuous drags should coalesce into one undo step.
- Text/value updates should not shift neighboring controls.

### Commit

Commit should feel final but lightweight:

- Slider release settles preview.
- Crop Done exits crop mode.
- Brush stroke ends as one undoable action.
- Polygon Complete creates selection and exits drawing state.
- Retouch Apply commits patch.
- Export Start queues jobs and shifts focus to queue progress.

### Cancel

Every active canvas tool needs a visible and keyboard cancel path:

- Escape cancels active transient operation.
- Cancel button appears in HUD when a tool can discard in-progress work.
- Cancel never destroys previous committed edits.
- If cancel cannot discard because action already committed, it should exit the tool rather than silently undo.

### Disabled

Disabled controls should not feel broken.

Rules:

- Disable with muted opacity.
- Tooltip explains why.
- If common missing requirement exists, show a concise inline reason: "Select a photo", "Choose a layer", "No mask active", "RAW controls unavailable for this file".
- Avoid showing advanced unavailable controls in prime screen real estate.

### Error/Failure

Rules:

- Never block the canvas for recoverable tool failures.
- Show a compact failure chip near the affected tool or row.
- Provide Retry when possible.
- Use exact detail in expandable diagnostics, not the primary row.

## Tool Behavior Plan

## 1. Library Browse And Selection

### Keep Layout

Keep the current Library structure:

- Left sources panel.
- Center search/filter row and grid/survey.
- Right inspector.
- Top workspace tabs.

### Behavior Problems

- Filter buttons and view buttons have similar weight.
- Inspector actions compete with review actions.
- Selection mode is visually subtle relative to its behavior change.
- Metadata is always visually dense.

### Polish Behavior

Search:

- Typing filters after a short debounce.
- Active search displays one clear chip or highlighted search field state.
- Clear button appears only when search has content.

Filters:

- Active filters should be visibly different from inactive filters through fill or accent dot, not bright borders on all buttons.
- Filter changes should not move the grid unexpectedly unless the result set changes.
- Clear filters should be disabled when no filters are active.

Selection:

- Entering selection mode changes cursor/thumbnail affordance.
- Thumbnail hover shows a select affordance.
- Selected thumbnails use one consistent overlay/badge.
- Multi-select count appears in a compact bar or inspector header.

Review:

- Pick/reject/rating should update immediately.
- Auto-advance should feel intentional: show a tiny advancing status only when enabled.
- Rating strip should not resize as stars activate.

Inspector:

- Metadata should be scan-first: most important rows visible, long file/path details elided.
- Edit/Export remain visible but visually differentiated: Export primary, Edit secondary.
- Selections/Compare are secondary.

Acceptance:

- User can search, filter, select, rate, pick/reject, edit, compare, and export with no layout surprise.
- The selected state is readable at thumbnail size.
- The right inspector feels useful, not like a form dump.

## 2. Import

### Keep Layout

Keep Import inside Library/local source flow.

### Behavior Problems

- Import can feel like file management instead of intake.
- Folder paths and file rows can become visually noisy.
- Import progress competes with Library status.

### Polish Behavior

Source selection:

- Choosing a folder gives immediate visible confirmation.
- Empty folder state tells the user what file types are supported.
- Long paths are middle-elided.

Selection:

- Select all/deselect state reflects visible file count.
- Mixed photo/video selection is allowed but clearly badged.
- Offline/unreadable files show reason on hover.

Import:

- Primary button is enabled only when importable files exist.
- Importing shows progress in one place.
- Completion returns the user to Library with newly imported assets visible or highlighted.
- Failure keeps selected source and offers Retry/Choose another folder.

Acceptance:

- User always knows source, selected count, destination, and import state.

## 3. Edit Tool Switching

### Keep Layout

Keep the current left rail, image stage, filmstrip, context panel, and right inspector.

### Behavior Problems

- Tools can stack HUDs/status panels.
- Active tool can be clear in the rail but unclear on canvas.
- Switching tools risks stale transient state.
- Tool variants can feel hidden when they are buried too deeply.
- Tool variants can also clutter the workspace when too many are shown as permanent buttons.
- Customization can become hard to find if it is split across rail buttons, inspector rows, HUDs, and unrelated settings.

### Polish Behavior

Tool entry:

- Selecting Adjust, Crop, Masks, Brush, Compare, Layers, or Export changes one active rail state.
- The canvas shows one active tool chip or HUD, not several.
- The inspector moves to the relevant tab/section.
- The active tool family and active variant are both visible.
- The most common tool action is one click.
- Variants and settings are one expansion away from the active tool.

Tool exit:

- Escape exits transient canvas modes first.
- Done commits current transient tool.
- Switching tools preserves committed edits but clears armed states.
- Switching tools closes stale menus, popovers, and HUDs.

Tool availability:

- If no photo is loaded, edit tools are visible but disabled with a clear reason.
- If a tool requires a selection/layer, it should either create one or explain what is missing.

Tool customization:

- Last-used variants are remembered.
- Tool presets live in the tool menu, popover, or inspector section.
- Common customization stays close to the tool.
- Advanced customization moves to More or preferences.
- Reset to default is available for meaningful custom settings.

Acceptance:

- At any moment, there is one obvious active tool and one obvious next action.
- Users can find the main edit tools without searching.
- Users can expand a tool to see variants and settings without scanning unrelated panels.
- Users can switch tools without losing committed edits or leaving stale canvas state.
- Users can customize repeated tool behavior without making the default interface busier.

### Tool Switching Menu Behavior

Tool switching should be fast from the rail and clear from menus.

Rules:

- Top-level tool families stay visible in the rail.
- Related variants live in a menu attached to the rail button, active tool chip, or HUD.
- Clicking a top-level rail button activates the last-used variant in that family.
- Opening the menu lets the user choose a specific variant.
- The active variant appears in the HUD and inspector header.
- Keyboard shortcuts switch directly to common variants.
- Tool menus must never hide the currently active state; the active item is checked.
- Tool menus use user-facing names and group variants by task.
- Tool menus expose customization only when it is related to the active tool.
- Tools with previews or multiple values use a popover instead of a long menu.
- Permanent rail buttons are reserved for tool families, not every sub-mode.

Recommended tool families:

- Adjust:
  - Adjust, Presets, Copy Settings, Paste Settings, Reset All
- Crop:
  - Crop Box, Transform, Horizon
- Masks:
  - Subject, Object, Sky, Background, Brush, Lasso, Polygon, Linear, Radial, Rectangle, Ellipse
- Brush:
  - Add, Erase, Refine Add, Refine Erase, Presets
- Retouch:
  - Remove, Heal, Patch, Clone, Dust, Red-eye
- Layers:
  - Select Layer, Transform Layer, Add Layer, Visibility States
- Compare:
  - Before/After, Split, Reference, Loupe

Acceptance:

- Common tools are one click away.
- Variants are one click plus one menu choice away.
- The UI does not need permanent buttons for every variant.
- Full tool functionality remains available without turning the rail, canvas, or inspector into a control dump.

## 4. Adjustments

Tools:

- White balance.
- Light.
- Color.
- Color grading.
- Tone curve.
- Presence/detail.
- Lens.
- RAW.
- Advanced color.
- Presets.

### Behavior Problems

- Many sliders have equal weight.
- Reset/before-after buttons add repeated text.
- Advanced groups can make the inspector feel endless.
- Sample tools need clearer armed state.

### Polish Behavior

Sliders:

- Dragging updates preview quickly.
- Value readout remains fixed width.
- Double-click or reset icon returns to default.
- Changed sliders show a subtle changed marker.
- Hover can reveal exact reset/default info.

Section behavior:

- Each section header includes:
  - title
  - changed indicator
  - before/after icon
  - reset icon
- Text labels "Before / after" and "Reset" should become icon-first in dense sections.
- Collapsed sections still show if they contain changed values.

White balance sampling:

- Clicking Sample arms eyedropper mode.
- Cursor changes.
- HUD says "Sample white balance" with Cancel.
- Sample click updates Temp/Tint and exits sampling.
- Escape cancels sampling.

Presets:

- Hover/selection previews should feel reversible.
- Applying preset confirms with a short status chip.
- Preset search/filter should not jump the rest of inspector.
- Preset warnings should be concise and only shown when relevant.

Tone curve:

- Points should have clear hover/drag handles.
- Drag should show input/output values.
- Reset curve and reset selected point are distinct.
- Curve interaction should not accidentally scroll the panel.

Color wheels:

- Wheel focus state should be clear.
- Dragging on wheel shows hue/sat/value readout without covering the wheel.
- Reset per wheel is available but visually quiet.

Acceptance:

- Every adjustment is reversible, readable, and stable while dragging.
- Changed controls are visible without making the inspector visually loud.

## 5. Crop And Transform

Tools:

- Crop box.
- Aspect presets.
- Guide modes.
- Horizon straighten.
- Transform.
- Perspective/skew.
- Crop fill.
- Liquify when available.

### Behavior Problems

- Crop has several submodes and can feel text-heavy.
- Exact crop geometry controls compete with primary crop interaction.
- Done/Reset/Transform need cleaner hierarchy.

### Polish Behavior

Entry:

- Crop rail button activates crop overlay and crop inspector section.
- Canvas gets a subtle active crop border.
- Crop HUD shows only mode, aspect, and Done.

Direct manipulation:

- Handles are easy to hit but visually refined.
- Hover highlights active edge/corner.
- Dragging shows aspect/dimensions in a small HUD.
- Guide lines are subtle and do not overpower the image.

Aspect:

- Aspect preset changes update crop immediately.
- Custom aspect add/remove is secondary and should not dominate.

Horizon:

- Horizon mode shows a clear line interaction.
- User drags a line, sees angle.
- Apply Straighten is enabled when angle exists.
- Escape cancels horizon mode.

Transform:

- Transform handles differ from crop handles.
- Rotation/scale/perspective controls show exact readout.
- Reset Transform does not reset Crop.
- Reset Crop does not reset Transform.

Acceptance:

- User can enter crop, adjust visually, use exact controls if needed, and exit confidently.

## 6. Compare

Tools:

- Before/after.
- Split/wipe.
- Side-by-side if available.
- Reference image.
- Sync zoom/pan.
- Loupe/focus inspection.

### Behavior Problems

- Compare can look like another mode without enough proofing behavior.
- It should be clear whether the user is seeing before, after, or reference.

### Polish Behavior

Entry:

- Compare button activates a dedicated canvas state.
- Inspector/tab shows compare controls.
- Left context panel should quiet if it distracts, but not necessarily disappear.

Split/wipe:

- Divider has a premium handle.
- Dragging divider feels smooth.
- Labels "Before" and "After" appear only near divider/edges and fade when idle.

Reference:

- Loading a reference shows it clearly as external reference.
- Clear reference is easy but secondary.

Sync:

- Sync zoom/pan active state is visible.
- If sync is off, each viewport state is independent and obvious.

Acceptance:

- User never has to guess what image state they are judging.

## 7. Masks / Selections

Tools:

- Object.
- Subject.
- Sky.
- Background.
- Brush add/erase.
- Lasso.
- Polygon.
- Linear.
- Radial.
- Ellipse.
- Rectangle.
- Add/subtract/intersect/replace.
- Overlay/outline/hidden.
- Invert.
- Feather/density/opacity/smooth/fine edge.
- Full/HQ preview.
- Duplicate/delete/update/retry/rename.

### Behavior Problems

- The current mask surface exposes too many rows, labels, status strings, and operation details at once.
- Some text reads like debug output.
- Operation, source, target, and preview state are all visible but not prioritized.

### Polish Behavior

Entry:

- Masks tool should show a clear creation state when no active mask exists.
- If a mask is active, the active mask row and canvas overlay agree.

Source tools:

- Source buttons can remain where they are, but should be quieter:
  - icon-first when rail is compact
  - text when expanded
  - strong active state only for selected source
- Smart/generated sources show progress as a chip tied to that source.

Operations:

- Add/subtract/intersect/replace should be visually grouped.
- The active operation should affect cursor badge and overlay color/outline.
- Modifier-key temporary operation should show temporary HUD text.

Overlay:

- Overlay, outline, and hidden are mutually legible states.
- Overlay opacity should be stable and not obscure photo judgment.
- Hidden state still leaves a tiny active selection indicator in inspector.

Mask list:

- Default row shows name, preview, visibility, affected layer, and changed chips.
- Expanded row shows source details and advanced actions.
- Provider/alpha/source-contract details live in diagnostics only.

Rename:

- Rename dialog should be lightweight.
- Enter commits, Escape cancels.
- Empty names do not commit.

Failure:

- Failed generated mask shows row-level Retry.
- Failure does not block other tools.

Acceptance:

- User can answer: what selection is active, what it affects, how it is displayed, and what operation the next input will perform.

## 8. Brush

Tools:

- Add brush.
- Erase brush.
- Brush size.
- Flow.
- Hardness.
- Density.
- Opacity.
- Spacing/smoothing/pressure where available.
- Tablet input.

### Behavior Problems

- Brush currently risks too many simultaneous surfaces: top toolbar, HUD, context panel, right inspector, cursor ring, status labels.
- Some status text is too technical for default use.

### Polish Behavior

Entry:

- Brush Add/Erase activates brush cursor immediately.
- Active selection target is visible in a compact chip.
- If no selection exists, brush creates or prompts to create one.

Cursor:

- Ring shows size.
- Inner/outer rings communicate feather/hardness.
- Add/erase mode shown by subtle icon or color shift.
- Cursor label should be minimal and not follow too noisily.

HUD:

- Keep one brush HUD visible.
- HUD shows:
  - Add/Erase
  - Size
  - Flow
  - Hard/Soft
  - Done
- Density/Opacity can be in compact context panel or inspector.
- Technical values like preview raster size move to diagnostics.

Stroke behavior:

- Pointer down begins one stroke.
- Drag adds points smoothly.
- Pointer up commits one undo step.
- Escape during active stroke cancels current stroke if possible.
- Tablet pressure changes stroke but does not spam status text.

Keyboard:

- Bracket shortcuts adjust size.
- Dedicated shortcuts for flow/hardness/density/opacity should show a short HUD pulse.
- Temporary erase modifier should visibly switch mode and restore previous mode on release.

Acceptance:

- Brush feels direct, fast, and quiet. The user watches the image, not the chrome.

## 9. Lasso And Polygon

Tools:

- Lasso draw.
- Polygon points.
- Undo point.
- Clear.
- Complete.
- Simplify.
- Smooth.
- Cancel.

### Behavior Problems

- These tools need strong in-progress state and clear completion.
- Self-intersection warnings must not feel like errors from nowhere.

### Polish Behavior

Lasso:

- Pointer down starts path.
- Path draws with stable line style.
- Release closes/commits if valid.
- Escape cancels.

Polygon:

- First click starts polygon.
- Each point has a visible handle.
- Hover over first point indicates close.
- Enter/Return completes.
- Backspace removes last point.
- Complete disabled if invalid.
- Self-intersection warning is inline near HUD and points to fix: undo last point or redraw.

Refinement:

- Simplify and Smooth are secondary actions visible only when polygon exists.
- Applying them should not exit polygon unless user completes.

Acceptance:

- User never loses work accidentally and always knows whether shape is incomplete, valid, or invalid.

## 10. Smart/Object Selection

Tools:

- Object click.
- Subject.
- Sky.
- Background.
- Smart preview path/stroke.
- Candidate cycling.
- Accept/reject/refine.

### Behavior Problems

- Smart selection can feel magical but uncertain unless preview, progress, and accept states are clear.

### Polish Behavior

Object click:

- Cursor communicates pick mode.
- Click location produces immediate pending feedback.
- Preview appears before final commit where possible.
- User can accept/reject if there are candidates.

Generated masks:

- Subject/Sky/Background show progress tied to the selected source button or active mask row.
- If model/runtime fallback is used, show concise label only when it matters.
- Stale generated masks show "Update" in row, not global alarm.

Candidate cycling:

- Candidate index shown subtly.
- Arrow/shortcut cycles candidates.
- Accept commits into current operation.
- Reject returns to previous stable mask state.

Acceptance:

- AI-assisted selection feels controllable, reversible, and recoverable.

## 11. Mask Refinement

Tools:

- Add/erase refine.
- Smart refine.
- Original/refined preview.
- HQ/full preview.
- Invert.
- Duplicate/copy/copy inverted/copy all.
- Reset edge.

### Behavior Problems

- Refinement controls currently read as a dense command strip.
- Original/refined/HQ/full are valid but need clearer hierarchy.

### Polish Behavior

Refine strip:

- Primary actions: Add, Erase, Smart.
- Preview toggles: Original/Refined, HQ, Full grouped separately.
- Structure actions: Invert, Copy, Reset in overflow or secondary group.

Preview:

- Original/Refined changes preview only, not recipe values unless committed.
- HQ/Full shows a small processing chip if expensive.

Reset:

- Reset Edge should clearly not delete the selection.

Acceptance:

- User can refine edges without fearing they are changing the wrong selection or losing the mask.

## 12. Retouch

Tools:

- Remove/inpaint.
- Heal.
- Patch.
- Clone.
- Dust.
- Red-eye.
- Apply.
- Regenerate.
- Cancel.
- Clone source.
- Patch source.

### Behavior Problems

- Retouch shares mask mechanics but should feel like object correction.
- Clone/patch source behavior needs especially clear feedback.

### Polish Behavior

Entry:

- Retouch mode shows mode-specific cursor and HUD.
- Active retouch mode is clear: Remove, Heal, Patch, Clone.
- Unsupported/experimental modes stay quiet unless enabled.

Remove/Heal/Patch:

- Brush defines affected region.
- Pending region is visible.
- Apply button enabled only when region exists.
- Regenerate enabled only after generated output exists.
- Cancel discards pending region without touching previous committed retouch.

Clone:

- Before source is set, HUD says "Pick source".
- Source click creates pin.
- After source is set, painting shows source-to-target relation.
- Aligned/non-aligned state is visible but compact.

Patch:

- Source region appears with handle.
- Moving source updates preview.
- Patch source label should not cover important image content.

Failure:

- Model failure falls back or shows Retry without blocking canvas.
- Failed retouch result remains pending, not silently committed.

Acceptance:

- Retouch feels like a deliberate repair workflow with obvious pending, apply, cancel, and source states.

## 13. Layers

Tools:

- Add adjustment/fill/text/source/group.
- Select layer.
- Rename.
- Reorder.
- Duplicate.
- Delete.
- Visibility.
- Lock.
- Solo/isolate.
- Opacity.
- Blend mode.
- Clipping mask.
- Layer mask enabled.
- Transform.
- Save/apply/delete visibility states.

### Behavior Problems

- Layer rows have many controls and can become visually heavy.
- Nested masks and layer actions compete.
- Selected layer, selected mask, and active adjustment target can be hard to parse.

### Polish Behavior

Layer row:

- Row selection uses subtle fill plus left accent.
- Primary row content: thumbnail, name, kind, visibility, opacity, mask badge.
- Secondary actions appear on hover or stay icon-only.
- Locked state is visible but not visually dominant.

Layer visual design:

- Base image row should look foundational and locked, not disabled or broken.
- Adjustment layer row should feel editable and precise.
- Fill, text, source, and group rows should use distinct but subtle type icons.
- Group rows should visually contain children without heavy boxed nesting.
- Mask children should be indented with a thin guide line, not large nested cards.
- Active layer and active mask should not fight for the same accent treatment.
- Use one selected-layer accent and a smaller active-mask indicator.

Layer row anatomy:

- Left: drag/reorder handle, thumbnail or type icon.
- Middle: name, kind, compact state chips.
- Right: visibility, mask badge, opacity readout, More menu.
- Expanded body: opacity slider, blend mode, lock/solo/clip/transform, masks.

Layer row spec:

- Collapsed row target height: 44-48 px.
- Expanded row body target height: stable per layer type, not content-jumping.
- Thumbnail/type icon zone: fixed width.
- Name zone: flexible width with clean elision.
- State zone: compact chips with strict priority.
- Action zone: fixed width for visibility, mask, opacity, and More.
- Drag handle appears on hover/focus but keeps reserved space if reorder is available.
- More menu is always reachable by keyboard focus.
- Row height must not change when hover controls appear.
- Row height must not change when opacity changes from one to three digits.
- Locked, hidden, selected, expanded, active mask, and transform-active states must be visually distinct.

Layer state priority:

1. Selected layer.
2. Active mask inside selected layer.
3. Transform-active layer.
4. Pending operation.
5. Error/warning.
6. Locked.
7. Hidden.
8. Hover.

When states combine, do not stack multiple bright accents. Use one primary selected-layer accent, then secondary icons/chips for the rest.

Layer state chips:

- `Locked`
- `Hidden`
- `Solo`
- `Clipped`
- `Masked`
- `Edited`

Only show chips that matter. Do not show default states like `Visible` or `Normal` unless the user opens details.

Layer menus:

- Add Layer split button:
  - Adjustment Layer
  - Fill Layer
  - Text Layer
  - Source/Image Layer
  - Group
- Row More menu:
  - Rename
  - Duplicate
  - Move Up
  - Move Down
  - Clip To Below
  - Transform
  - Export Layer
  - Delete
- Mask badge/menu:
  - Show/Hide Mask
  - Edit Mask
  - Invert
  - Duplicate Mask
  - Remove Mask

Layer switching:

- Clicking a row selects the layer.
- Clicking a mask badge selects the mask inside that layer.
- Double-click name renames.
- Drag handle reorders.
- Transform icon enters layer transform tool.
- Escape exits transform but keeps layer selected.

Layer empty/add state:

- When there are no user layers, show a refined empty state:
  - "No layers yet"
  - primary: Add adjustment layer
  - menu: Fill, Text, Source, Group
- Avoid large blank boxes.

Rename:

- Click/focus name field to rename.
- Enter commits.
- Escape reverts.
- Locked layers prevent edit with clear tooltip.

Opacity:

- Slider plus exact percent readout.
- Dragging coalesces undo.
- Value field fixed width.

Blend:

- Dropdown shows current blend.
- Hover explains less-common blend modes only in tooltip.

Mask nesting:

- Mask children are readable under layer row.
- Collapsed state shows mask count and active mask indicator.
- Expanded state shows reorder/visibility/preview only.
- Technical mask source data is diagnostics.

Transform:

- Transform icon from row activates on-canvas handles.
- Canvas and row both show transform active.
- Escape exits transform handle mode.

Acceptance:

- User can select a layer and understand what will be edited without reading a long stack summary.

## 13A. Layer Visual Quality Bar

Layers are one of the places where the app can feel premium fastest. They should look like a high-end stack editor, not a debug tree.

Visual requirements:

- Rows are aligned on a strict grid.
- Thumbnails are consistent size.
- Text never overlaps buttons.
- Names elide cleanly.
- Opacity readout is fixed width.
- Blend mode does not dominate the row.
- Visibility, lock, and mask icons are consistent.
- Active, hover, and selected states are distinct.
- Nested masks are visibly related to the parent layer.
- Destructive delete is separated from primary selection and inspection actions.

Behavior requirements:

- Selecting, renaming, dragging, toggling visibility, and changing opacity never changes row height.
- Expanded row height is stable.
- Drag reorder shows insertion indicator.
- Locked rows allow inspection but block mutation.
- Hidden rows remain selectable.
- Solo/isolate state is obvious and easy to clear.
- Mask selection updates the inspector without losing layer context.

Premium details:

- Layer hover reveals More menu and reorder handle.
- Layer selected state uses a soft fill and single left accent.
- Mask child rows use smaller typography and indentation.
- Warnings use small amber chips, not full-row alarm styling.
- Thumbnail failures show a refined placeholder, not black emptiness.
- Dragging a layer shows a precise insertion line, not a giant placeholder.
- Rename mode keeps the thumbnail, state icons, and row actions stable.
- Keyboard focus uses a ring only around the focused row or field, not the whole stack.
- The Add Layer split button should look like the main positive action for the stack without overpowering selected rows.

Layer menu quality:

- Keep selection actions at the top.
- Keep structural actions in the middle: duplicate, move, group, clip.
- Keep transformation/export actions below structure.
- Keep destructive delete separated at the bottom.
- Never mix Delete beside Rename or Duplicate in a tight row action cluster.
- Menu item labels should say what happens to the selected layer, not expose internal model terms.

Layer interaction acceptance:

- A user can tell whether they are editing the layer, its mask, or its transform handles within one glance.
- A user can add an adjustment layer, rename it, lower opacity, toggle visibility, and delete it without hunting across unrelated panels.
- A user can drag a layer without accidental rename, accidental visibility toggle, or row height shift.

## 14. Layer Transform

Tools:

- Move.
- Scale.
- Rotate.
- Snap to canvas.
- Align/distribute.

### Polish Behavior

Handles:

- Hover reveals handle purpose.
- Drag shows X/Y, scale, rotation readout.
- Holding modifiers constrains movement/scale/rotation.
- Snapping shows quiet guide line.

Commit:

- Drag release commits one undo step.
- Escape during drag cancels current transform if possible.

Acceptance:

- Direct manipulation feels precise and does not fight the image.

## 15. Presets

Tools:

- Search.
- Filter category/pack/favorites.
- Preview.
- Apply.
- Save.
- Rename.
- Move pack.
- Delete.
- Import/export bundle.

### Polish Behavior

Preset preview:

- Hover or selected preview should be clearly temporary.
- Apply makes it permanent.
- Leaving preset area clears preview unless pinned.

Preset rows:

- Show name, pack/category, favorite, changed groups.
- Safety warnings only if relevant.

Save:

- Save Preset asks for name only when needed.
- Overwrite confirmation appears only for existing name.

Acceptance:

- User can explore presets without fear of accidental permanent edits.

## 16. Scopes And Pixel Inspector

Tools:

- Histogram.
- Waveform.
- RGB/luma/log/density/trace/grid toggles.
- Large scopes.
- Soft proofing status.
- Pixel sample.

### Behavior Problems

- Scopes are technical but should feel premium and trustworthy.
- Pixel inspector should not visually dominate the photo unless active.

### Polish Behavior

Scopes:

- Scope toggles should read as display settings, not image edits.
- Updating state should be quiet.
- Large scopes mode should make the image/scopes relationship clear.
- Soft proofing state should be visible but compact.

Pixel inspector:

- Sampling mode changes cursor.
- HUD appears only while sampling.
- Before/after values use mono.
- Escape exits sampling.
- Copy/sample actions should be available but not always visible.

Acceptance:

- Technical review feels calm and exact, not like debugging.

## 17. Carousel Tools

Tools:

- Select.
- Pan.
- Text.
- Media.
- Shape.
- Board format/template.
- Background.
- Media slot choose/clear/fill/fit/scale/offset.
- Guides/snapping.
- Layers.
- Transform.
- Text style.
- Shape style.
- Save/export.

### Keep Layout

Keep the board canvas and right settings panel.

### Polish Behavior

Tool rail:

- Active tool is obvious.
- Tool cursor changes.
- Keyboard shortcuts pulse active tool state.

Board:

- Empty media placeholder must never overlap text.
- Add media button should be centered and clear.
- Board boundary and guides should look refined, not debug-like.

Media:

- Choose opens media picker.
- Fill/Fit changes preview immediately.
- Scale/offset sliders show exact values and drag smoothly.
- Clear asks only if content would be lost.

Text:

- Text tool click creates editable text.
- Text edit mode has caret/focus.
- Escape exits text edit before leaving tool.
- Typography controls update selected text immediately.

Shape:

- Shape tool creates shape with default style.
- Shape handles match transform handles.
- Fill/stroke controls use swatches and exact values.

Guides:

- Snapping feedback should be quiet and precise.
- Custom guides are removable without cluttering default view.

Layers:

- Selected canvas object and layer row stay synchronized.
- Lock prevents canvas mutation but allows inspection.

Acceptance:

- Carousel feels like a polished composition surface without moving the existing inspector model.

## 18. Timeline Tools

Tools:

- Source media picker.
- Load to source.
- Insert.
- Overwrite.
- Playback.
- Step.
- Seek.
- Select.
- Razor.
- Slip.
- Roll.
- Snap.
- Ripple.
- Split.
- Move/lift/extract.
- Markers.
- Effects.
- Transitions.
- Zoom.
- Save/render.

### Keep Layout

Keep the current source/program/timeline/effects pattern.

### Polish Behavior

Playback:

- Play/pause is visually dominant in monitor controls.
- Timecode is stable width.
- Playback failure appears in monitor area with recovery.

Tools:

- Active timeline tool changes cursor.
- Razor hover previews cut point.
- Slip/roll show trim deltas.
- Snap guide appears only during drag.

Clips:

- Selected clip has clear outline.
- Hover reveals trim handles.
- Dragging clip shows ghost position and snap feedback.
- Invalid drop state is obvious.

Markers:

- Add marker at playhead gives immediate marker chip.
- Marker editing is compact.
- Completed/todo state is visually clear.

Effects/transitions:

- Applying effect gives row-level confirmation.
- Effect descriptions appear on hover/expanded row, not always.

Acceptance:

- Timeline keeps its existing structure but behaves more like a serious editing surface.

## 19. Export Tools

Tools:

- Preset selection.
- Destination selection.
- Selected asset preview.
- Add more.
- Queue.
- Start export.
- Retry/cancel/remove/clear.
- Preflight.
- Output summary.

### Keep Layout

Keep left presets, center assets/queue, right settings.

### Polish Behavior

Preset:

- Selected preset is obvious.
- Preset details appear once in right settings and compactly in queue rows.
- New/manage preset are secondary.

Destination:

- Selected destination has clear status.
- Cloud disconnected state is muted with reason and action.

Preflight:

- Right panel should state Ready/Warning/Blocked.
- Warnings are grouped and actionable.
- Start Export disabled only for blockers, not warnings.

Queue:

- Start Export is the main action.
- Clear Queue is secondary/destructive and visually quieter until needed.
- Row actions appear as icons or overflow.
- Progress rows should show status, output preset, destination, and failure/retry clearly.

Completion:

- Completed export gives reveal/open location action.
- Failures stay in queue with Retry.

Acceptance:

- Export feels like a delivery workflow without reworking the whole page.

## Cross-Tool Behavior Checklist

Every tool should pass this checklist:

- Entry is obvious.
- Active state appears in rail, canvas, and inspector where relevant.
- Cursor matches active operation.
- Primary action is visually dominant.
- Secondary actions are available but quieter.
- Hover states do not shift layout.
- Dragging gives live feedback.
- Continuous drag/stroke creates one undo step.
- Escape cancels transient state.
- Done/Apply commits.
- Reset only resets the intended scope.
- Disabled state explains why.
- Failure state offers recovery.
- Before/after does not mutate values.
- Preview/export parity is maintained where relevant.
- Text labels are concise.
- Technical/debug strings are hidden outside diagnostics.

## Agent Implementation Contract

This section turns the design plan into instructions an implementation agent can safely follow. The agent should not treat the full document as permission to redesign the app. Each implementation run should choose one narrow pass, touch the smallest practical set of files, and prove the result with behavior checks and screenshots.

### Master Spec vs Agent Ticket

This document is the master spec. It is too broad to use as a single implementation prompt.

Rules:

- Agents should receive one scoped ticket derived from this document.
- A ticket should cite only the relevant plan sections.
- A ticket should name exact target files and non-goals.
- A ticket should include visual, interaction, and build/test proof.
- The agent should not optimize unrelated surfaces just because the master spec mentions them.
- If a ticket grows beyond one surface or one shared primitive family, split it.

### Agent Non-Goals

Do not:

- Move major workspace regions.
- Replace the app information architecture.
- Convert the app into a new landing-page-style design.
- Hide professional editing functionality to make the screen look simpler.
- Create duplicate one-off controls when an existing QML primitive can be extended.
- Hardcode spacing, radius, color, or button values repeatedly across screens.
- Refactor unrelated C++/Rust/editor pipeline code for a visual polish task.
- Change undo/history semantics unless the task explicitly targets tool state behavior.
- Touch layer/mask/retouch state plumbing casually; these are high-risk.
- Make broad screenshot-wide visual changes without a before/after proof.

### Agent Operating Rules

Every implementation task should follow this order:

1. Identify the target pass and target surface.
2. Read the existing QML/component files for that surface.
3. Find the shared primitive that should own the behavior.
4. Patch the shared primitive first when possible.
5. Patch one workspace surface only after the primitive is ready.
6. Preserve current layout, workflow, object names, and test hooks unless the task explicitly changes them.
7. Run the smallest relevant build/check.
8. Capture or describe visual proof.
9. Report exact files changed and remaining risk.

The agent should stop and ask for scope confirmation if a task requires moving major panels, changing the edit recipe pipeline, changing layer/mask data ownership, or touching more than one workspace family at once.

### Existing File Anchors

Start from the existing codebase. Do not invent new top-level UI architecture before checking these files.

Shared QML primitives:

- `apps/desktop/qml/components/Theme.qml`
- `apps/desktop/qml/components/ActionButton.qml`
- `apps/desktop/qml/components/ConfirmActionButton.qml`
- `apps/desktop/qml/components/SidebarButton.qml`
- `apps/desktop/qml/components/CommandButtonSurface.qml`
- `apps/desktop/qml/components/InspectorSection.qml`
- `apps/desktop/qml/components/InspectorSlider.qml`
- `apps/desktop/qml/components/PopupSurface.qml`
- `apps/desktop/qml/components/GlassSurface.qml`
- `apps/desktop/qml/components/InfoPill.qml`
- `apps/desktop/qml/components/FilterChip.qml`
- `apps/desktop/qml/components/ToneComboBox.qml`
- `apps/desktop/qml/components/ToneTextField.qml`

Workspace surfaces:

- `apps/desktop/qml/workspaces/StudioWorkspace.qml`
- `apps/desktop/qml/workspaces/EditView.qml`
- `apps/desktop/qml/workspaces/EditWorkspace.qml`
- `apps/desktop/qml/workspaces/MaskView.qml`
- `apps/desktop/qml/workspaces/MaskInspectorSections.qml`
- `apps/desktop/qml/workspaces/LibraryView.qml`
- `apps/desktop/qml/workspaces/CarouselView.qml`
- `apps/desktop/qml/workspaces/TimelineView.qml`
- `apps/desktop/qml/workspaces/ExportView.qml`

Relevant controllers/state surfaces:

- `apps/desktop/src/app_shell_controller.*`
- `apps/desktop/src/editor_preview_controller.*`
- `apps/desktop/src/edit_recipe_state.h`
- `apps/desktop/src/agent_edit_controller.*`
- `apps/desktop/src/carousel_document_controller.*`
- `apps/desktop/src/timeline_document_controller.*`
- `apps/desktop/src/export_job_controller.*`

Implementation preference:

- Prefer extending existing primitives such as `Theme`, `ActionButton`, `SidebarButton`, `InspectorSection`, `InspectorSlider`, `PopupSurface`, and local `MiniButton` patterns before adding new primitives.
- If a new primitive is necessary, name it clearly, keep it narrow, and migrate one existing use in the same pass.
- If `StudioWorkspace.qml` contains local component definitions that should become shared primitives, extract only when it reduces duplication in the current pass.

### Agent Ticket Template

Every implementation ticket should be written in this format.

Title:

- One sentence naming the target surface and outcome.

Objective:

- What user-visible problem is being fixed.

Target files:

- Exact files expected to change.

Non-goals:

- What must not change.

Design rules used:

- Cite the relevant sections of this plan.

Behavior rules:

- Active, hover, disabled, focus, commit, cancel, error, and keyboard behavior as applicable.

Visibility rules:

- What is always visible.
- What is contextually visible.
- What is one expansion away.
- What is hover/focus only.
- What is diagnostic only.
- Any functionality moved and where it moved.

Acceptance checks:

- Visual checks.
- Interaction checks.
- Screenshot checks.
- Build/test checks.

Rollback risk:

- What could break and how to recognize it.

### Component-First Priority Model

Implementation should prioritize component families across all pages, not one page at a time. This prevents the app from becoming polished in one workspace and inconsistent everywhere else.

Priority order:

1. Foundations: theme tokens, spacing, radius, typography, borders, focus, accent rules.
2. Navigation and rails: top workspace nav, left rails, sidebars, active states.
3. Buttons and actions: primary, secondary, tertiary, icon, segmented, destructive, split buttons.
4. Menus and popovers: menu geometry, grouping, disabled reasons, checked states, keyboard behavior.
5. Inspectors and sections: section headers, collapse, reset, before/after, overflow, empty/disabled states.
6. Sliders and value controls: fixed columns, stable rows, precision behavior, changed/reset state.
7. Lists, rows, and cards: library rows, project rows, layer rows, preset rows, export rows, timeline rows.
8. Media surfaces: canvas, image stage, filmstrip, carousel board, timeline monitor, thumbnails.
9. Tool behavior: active tool, variant, HUD, cursor, pending/commit/cancel/failure states.
10. Visibility state: always visible, contextual, one expansion away, hover/focus, diagnostic-only, moved functionality.
11. Page-specific workflows: Library, Edit, Mask, Carousel, Timeline, Export.

The first six priorities should establish shared behavior. The last four priorities should apply it to each page and workflow.

### Page Coverage Matrix

Every component pass must include page coverage. "Included" does not mean every page must change in every ticket; it means every page must be audited and either updated or explicitly marked already compliant.

Pages/workspaces:

- App shell / top navigation: `Main.qml`, `StudioWorkspace.qml`, `AppIcon.qml`, `BrandWordmark.qml`.
- Library: `LibraryView.qml`, `LibraryWorkspace.qml`, library sections inside `StudioWorkspace.qml`.
- Import: `ImportView.qml`, import dialogs/surfaces.
- Edit: `EditView.qml`, `EditWorkspace.qml`, edit sections inside `StudioWorkspace.qml`.
- Masks: `MaskView.qml`, `MaskInspectorSections.qml`, `SharedMaskOverlay.qml`, `MaskCanvasOverlay.qml`.
- Carousel: `CarouselView.qml`.
- Timeline: `TimelineView.qml`, `TimelineSourcePanel.qml`, `TimelineEffectsPanel.qml`.
- Export: `ExportView.qml`, export sections inside `StudioWorkspace.qml`.
- Shared dialogs/preferences: `CommandPaletteDialog.qml`, `AppPreferencesDialog.qml`, `GalleryPickerDialog.qml`, `KeyboardShortcutsDialog.qml`.

Component coverage:

- Theme/tokens: all pages.
- Top nav/workspace switcher: app shell, every workspace.
- Left rail/sidebar: Library, Edit, Masks, Timeline, Export where present.
- Buttons: all pages, dialogs, toolbars, rows.
- Menu/split button/popover: Import, Edit tools, Layers, Carousel, Timeline, Export, dialogs.
- Inspector sections: Edit, Masks, Carousel, Export, Library inspector where present.
- Sliders/value rows: Edit, Masks, Carousel, Export settings, Timeline controls where present.
- Lists/rows/cards: Library project/photo rows, Layers, Carousel cards, Timeline sequence/clip rows, Export queue/preset rows.
- Media surfaces: Library grid, Edit canvas, Mask overlay, Carousel board, Timeline monitor/timeline, Export asset preview.
- Status/empty/error states: all pages.
- Visibility/function preservation audit: all pages.

### Component Ticket Breakdown

Use these as the implementation tickets. Each ticket should audit every page in the coverage matrix, then make the smallest set of changes needed for that component family.

Ticket 1: Foundations and tokens

- Target: `Theme.qml` first, then shared primitives that consume theme values.
- Covers: all pages.
- Goal: establish spacing, radius, border, typography, focus, disabled, accent, and surface tokens.
- Proof: screenshots from Library, Edit, Carousel, Timeline, and Export showing the same token language.

Ticket 2: Navigation, rails, and sidebars

- Target: top nav, workspace switcher, left tool rail, library sidebar, timeline sidebar, export/settings panels.
- Covers: App shell, Library, Edit, Masks, Timeline, Export.
- Goal: consistent active states, icon sizing, row height, section spacing, tooltips, disabled reasons.
- Proof: active workspace and active rail item are visible on every page without accent overuse.

Ticket 3: Buttons and action hierarchy

- Target: `ActionButton.qml`, `ConfirmActionButton.qml`, `SidebarButton.qml`, `CommandButtonSurface.qml`, local `MiniButton` patterns.
- Covers: all pages and dialogs.
- Goal: standard primary/secondary/tertiary/icon/segmented/destructive behavior.
- Proof: each page has one obvious local primary action and quieter secondary actions; important button labels fit without ellipses.

Ticket 4: Menus, split buttons, and popovers

- Target: `PopupSurface.qml`, menu-like local patterns, new narrow primitives only if required.
- Covers: Import, Edit tools, Layers, Carousel, Timeline, Export, dialogs.
- Goal: variants and secondary actions are discoverable without permanent clutter.
- Proof: menus are grouped, keyboard navigable, checked/current states show, destructive actions are separated, and menu labels fit without ugly truncation.

Ticket 5: Inspector sections and panels

- Target: `InspectorSection.qml`, inspector content in Edit, Masks, Carousel, Export, Library inspector.
- Covers: Library, Edit, Masks, Carousel, Export.
- Goal: consistent section headers, collapse/expand, reset, before/after, overflow, disabled, and changed states.
- Proof: inspector sections align and behave consistently across every page that has an inspector.

Ticket 6: Sliders and value controls

- Target: `InspectorSlider.qml`, value fields, combo boxes, toggles, segmented value controls.
- Covers: Edit, Masks, Carousel, Export, Timeline where applicable.
- Goal: stable row height, fixed value columns, smooth drag, changed/reset state, disabled reason.
- Proof: dragging and value changes do not shift layout.

Ticket 7: Lists, rows, cards, and queues

- Target: library rows/cards, layer rows, carousel cards, timeline rows/clips, export queue rows.
- Covers: Library, Edit/Layers, Carousel, Timeline, Export.
- Goal: stable row/card anatomy, selected/hover/focus/disabled/error states, overflow actions.
- Proof: selection is clear and row/card controls do not overpower content.

Ticket 8: Media surfaces and preview chrome

- Target: canvas/stage, filmstrip, thumbnails, carousel board, timeline monitor/timeline, export preview.
- Covers: Library, Edit, Masks, Carousel, Timeline, Export.
- Goal: media remains visually dominant; chrome is quiet, aligned, and stable.
- Proof: main content dominates each page at first glance.

Ticket 9: Edit tool discovery, expansion, and switching

- Target: Edit rail/tool controls, tool HUDs, tool menus, inspector headers.
- Covers: Edit and Masks first, then any tool-like controls in Carousel/Timeline.
- Goal: users can see tool families, expand variants, customize common settings, switch safely.
- Proof: active family and active variant are visible; switching clears stale menus/HUDs without losing committed edits; moved tool functions are still one expansion away or closer.

Ticket 10: Layer stack

- Target: existing layer row code in Edit/Studio workspace.
- Covers: Edit/Layers only, with interaction proof.
- Goal: premium layer stack with stable rows, readable active layer/mask/transform states, clean menus.
- Proof: select, rename, reorder, visibility, opacity, mask select, transform entry/exit, Escape behavior.

Ticket 11: Visibility and function preservation audit

- Target: visibility/function placement across every page.
- Covers: Library, Import, Edit, Masks, Carousel, Timeline, Export, shared dialogs.
- Goal: assign every current function to always visible, contextual, one expansion away, hover/focus, diagnostic-only, or intentionally deprecated.
- Proof: no functionality disappears silently; every moved control has a documented new location and discovery path; visible labels remain understandable without hover-only decoding.

Ticket 12: Page-specific workflow polish

- Target: one page at a time after shared components are in place.
- Covers: Library, Import, Edit, Masks, Carousel, Timeline, Export.
- Goal: finish page-specific states without breaking shared component rules.
- Proof: each page passes first-glance, close-inspection, clutter, and interaction review.

### Page Completion Checklist

Each page is complete only when these components have been audited:

- App shell/top navigation.
- Page sidebar or rail.
- Primary content/media surface.
- Inspector/settings panel.
- Toolbar/action bar.
- Buttons and menus.
- Sliders/value controls where present.
- Lists/rows/cards/queues where present.
- Text fit/truncation behavior.
- Empty/loading/disabled/error states.
- Keyboard/focus behavior.
- Screenshot proof for default and active states.

Workspace-specific completion:

- Library: project/source list, photo grid, selected asset inspector, filter/sort/actions, storage/footer.
- Import: source selection, import actions, progress, empty/error states.
- Edit: tool rail, image stage, top canvas toolbar, inspector, filmstrip, bottom action bar.
- Masks: source/variant controls, brush/lasso/polygon overlays, mask inspector, pending/apply/cancel states.
- Carousel: board cards, compare/cull actions, right settings, selection and rating states.
- Timeline: project/sequence list, monitor controls, timeline tracks, clip rows, markers, waveform, effects panel.
- Export: presets, selected assets, settings, queue, preflight, destination, progress, completion/failure states.

### Verification Requirements

For QML/UI changes:

- Run the smallest relevant QML/desktop build path available.
- Prefer focused smoke checks before broader builds.
- Capture before/after screenshots when the task changes visual layout or state.
- Verify at least one default state and one active/hover/expanded state.
- Check narrow and wide panel behavior if the changed surface resizes.
- Verify behavior in addition to screenshots when a task touches tool state, menus, focus, keyboard, undo grouping, or pending operations.
- For tool switching changes, verify rail state, HUD/inspector state, Escape behavior, and stale menu/HUD cleanup.
- For layer changes, verify selection, active mask targeting, transform entry/exit, visibility toggles, opacity changes, rename, reorder, and Escape behavior.

Recommended commands from the repo guidelines:

- `cmake --preset dev-macos-debug && cmake --build --preset build-dev-macos-debug` for local macOS desktop work.
- Focused desktop smoke tests when available.
- `cmake --preset ci-headless && cmake --build --preset build-ci-headless` for non-Qt/headless validation when the change touches shared/native code.
- `ctest --preset test-ci-headless --output-on-failure` for native tests.
- `cd rust && cargo test --workspace` only when Rust crates change.

If the agent cannot run the app or capture screenshots, it must say so and provide the exact unverified visual risk.

### Visual Proof Checklist

Screenshots should prove:

- The app still looks like the same app.
- The main content remains dominant.
- Primary edit tools are visible.
- Tool expansion is discoverable.
- Active tool and active variant are visible.
- Controls do not overpower the image.
- Accent use is limited to selected, active, primary, changed, warning, or error states.
- Borders/cards are reduced or justified.
- Text does not overlap.
- Hover/expanded states do not shift layout.
- Menus are understandable and grouped.

Screenshots do not prove:

- Keyboard behavior.
- Escape/Enter/Done/Cancel semantics.
- Undo grouping.
- Disabled reasons.
- Pending operation cleanup.
- Layer/mask targeting correctness.

Those behaviors need explicit interaction checks in the ticket.

### Agent Stop Conditions

Stop and ask before continuing if:

- The task requires moving major workspace regions.
- The task requires changing C++ controller ownership for layers, masks, retouch, or undo history.
- The task would require changing more than one workspace family in one pass.
- The task cannot be visually verified but changes layout significantly.
- The existing component structure conflicts with the requested design rule.

### Agent Definition Of Done

A pass is done only when:

- The implementation uses existing primitives or introduces a justified small primitive.
- The touched surface follows the relevant spacing, button, menu, border, slider, and typography rules.
- Primary functionality remains available.
- Any moved functionality has a documented new location and visibility level.
- Active, hover, focus, disabled, expanded, and error states are handled where applicable.
- The smallest relevant build/check was run or the reason it could not run is documented.
- Visual proof was captured or the visual verification gap is explicitly reported.
- Follow-up risk is listed, especially for tool lifecycle, layers, masks, retouch, and undo/history.

## Execution Plan

Implement this as component tickets, not as one large rewrite and not as isolated page-only polish.

Before implementation, convert each component ticket into an Agent Ticket using the template above. Do not hand an agent the full plan and ask it to "make the UI better." Give it one component family, page coverage requirements, exact files, explicit non-goals, and verification requirements.

### Step 0: Freeze The Shape

Goal: confirm what will not move.

- Capture current screenshots for Library, Edit Adjust, Brush/Mask, Layers, Carousel, Timeline, and Export.
- Mark the panels and tool locations that must stay recognizable.
- Identify visual clutter that can be fixed through primitives instead of layout moves.
- Build the page coverage checklist from the current app state.

Exit criteria:

- The team can point to before screenshots and say what will remain structurally familiar on every page.

### Step 1: Foundations And Tokens

Goal: make the UI system consistent everywhere.

- Implement Ticket 1.
- Establish theme tokens before page polish.
- Verify Library, Edit, Carousel, Timeline, and Export all consume the same visual language.

Exit criteria:

- Shared spacing, radius, typography, border, focus, disabled, and accent behavior exists in primitives rather than one-off page values.

### Step 2: Shared Controls Across Pages

Goal: make repeated controls behave the same on every page.

- Implement Tickets 2-8 in order.
- Navigation/rails, buttons, menus, inspectors, sliders, rows/cards, and media surfaces are audited across all pages.
- Each ticket includes every page in the coverage matrix.

Exit criteria:

- Library, Import, Edit, Masks, Carousel, Timeline, and Export all share the same component behavior where those components appear.

### Step 3: Edit Tool Experience

Goal: make editing easy, visible, expandable, customizable, and safe.

- Implement Ticket 9.
- Tool families are visible or one expansion away.
- Variants and customization are near the active tool.
- Active family and active variant are visible.
- Tool switching closes stale menus/HUDs and preserves committed edits.

Exit criteria:

- Adjust, Crop, Mask, Brush, Retouch, Layers, Compare, and other tool-like controls can be switched without stale state, confusing labels, or layout jumps.

### Step 4: Layers

Goal: make the layer stack premium without destabilizing edit state.

- Implement Ticket 10 only after shared rows, menus, buttons, and inspectors are in place.
- Keep layer work isolated from unrelated tool polish.

Exit criteria:

- The layer stack looks calm at rest, clear on hover, precise during drag, and unambiguous when a layer or mask is selected.

### Step 5: Page-Specific Workflow Polish

Goal: preserve functionality and finish every page using the shared component system.

- Implement Ticket 11 before page-specific cleanup.
- Implement Ticket 12 page by page.
- Assign all functions to a visibility level before moving or hiding controls.
- Library: project/source list, grid, inspector, filter/sort/actions, storage/footer.
- Import: source selection, import actions, progress, empty/error states.
- Edit: tool rail, image stage, toolbar, inspector, filmstrip, bottom actions.
- Masks: source/variant controls, overlays, inspector, pending/apply/cancel states.
- Carousel: board cards, compare/cull actions, settings, selection/rating.
- Timeline: project/sequence list, monitor, tracks, clips, markers, waveform, effects.
- Export: presets, assets, settings, queue, preflight, destination, progress, completion/failure.

Exit criteria:

- Every page keeps its current layout, includes all of its existing functionality, documents moved controls, and shares the same premium control language.

## Polish Gates

Each pass should be checked against these gates before moving on.

Visual gate:

- Spacing uses the shared 4 px measurement system.
- Buttons, rows, sliders, menus, and panels use the documented size ranges.
- The main content is dominant at first glance.
- Only selected, active, primary, changed, warning, or error elements use accent.
- Every visible card or border carries real structure; unnecessary borders are removed or replaced with spacing/fill.
- No unexpected row-height shifts on hover, drag, selection, rename, or value edits.
- No text overlap at narrow and wide panel sizes.
- No nested-card look inside inspectors or layers.
- Accent color appears only for meaningful state or primary action.
- Primary action is obvious without making secondary actions disappear.
- Typography uses a small hierarchy and does not rely on long helper text.

Interaction gate:

- Keyboard focus is visible and scoped.
- Escape, Enter, Apply, Done, Cancel, Reset, and Undo behave consistently.
- Menus are keyboard navigable.
- Disabled actions explain why.
- Dangerous actions are separated and labeled clearly.

Tool gate:

- Active tool is visible in the rail.
- Active variant is visible in HUD or inspector.
- Main edit tool families are visible without searching.
- Tool variants are available through predictable expansion.
- Tool customization is near the active tool and does not clutter the default surface.
- Current functionality is either visible, contextual, one expansion away, hover/focus visible, diagnostic-only, or intentionally deprecated with a reason.
- Moved functionality has a documented new location and discovery path.
- Cursor matches the operation.
- Switching tools clears transient state safely.
- Switching tools closes stale menus, popovers, and HUDs.
- Pending operations can be committed or canceled.
- Failure states offer Retry, Cancel, or fallback.

Screenshot gate:

- Compare before/after screenshots for the same workspace.
- Confirm the app still looks like the same app.
- Confirm the edited screen feels quieter, sharper, and more intentional.
- Check both a default state and an active-interaction state.
- Run the first-glance, close-inspection, and clutter reviews from the Visual Polish System.
- Reject screenshots where the controls overpower the image, board, layer stack, or timeline.

## Priority Polish Backlog

### Tier 1: Highest Visual Impact, Lowest Workflow Risk

1. Reduce accent-border overuse across buttons, panels, and selected rows.
2. Create a stricter button hierarchy in `MiniButton`.
3. Convert repeated Reset/Before-after text buttons to icon-first controls with tooltips.
4. Stabilize slider row widths and value readouts.
5. Hide or soften debug-like status text in mask/brush/layer surfaces.
6. Make active tool chip/HUD styling consistent across Crop, Brush, Polygon, Retouch.
7. Fix Carousel empty media placeholder overlap.
8. Make Export Start primary and Clear Queue secondary.
9. Introduce reusable MenuButton/SplitButton behavior for Import, Export, Add Layer, Mask Source, Brush, and Retouch.
10. Standardize LayerRow visual states and menus.
11. Add shared spacing, radius, border, and control-size tokens.
12. Audit the screenshot surfaces against rail, toolbar, slider, menu, and inspector sizing rules.
13. Reduce unnecessary cards/borders in the inspector and filmstrip without losing grouping or useful boundaries.
14. Make accent usage pass the selected/active/primary/changed/warning-only rule.
15. Apply fixed typography roles for tabs, panel titles, section titles, row labels, values, and metadata.
16. Make the Edit rail show tool families clearly while moving variants into predictable expansion.
17. Add active variant display to HUDs and inspector headers.
18. Define where each tool's customization lives: menu, popover, inspector, HUD, or preferences.

### Tier 2: Tool Behavior Polish

1. Define exact Entry/Active/Commit/Cancel behavior for every Edit tool.
2. Add visible but quiet temporary modifier feedback for mask/brush operations.
3. Make brush HUD the single source of brush interaction feedback.
4. Make crop handles and crop HUD more premium.
5. Make retouch pending/apply/cancel states unmistakable.
6. Make layer row hover/active/locked states cleaner.
7. Make preset preview clearly temporary.
8. Make timeline clip drag/snap/trim feedback more refined.
9. Make tool variants switch through shared menu-button behavior instead of permanent extra buttons.
10. Add clear active variant labels to tool HUDs and inspector headers.
11. Preserve last-used variants without restoring stale pending operations.
12. Add reset-to-default behavior for meaningful tool customization.

### Tier 3: Screenshot-Level Refinement

1. Library screenshot: reduce filter/action visual weight, improve inspector hierarchy.
2. Edit Adjust screenshot: calm right inspector, make image stage feel more expensive.
3. Brush screenshot: reduce stacked chrome and technical labels.
4. Layers screenshot: make rows less dense without moving them.
5. Advanced Color screenshot: refine color wheels, labels, and section rhythm.
6. Carousel screenshot: polish board boundary, placeholder, right panel hierarchy.
7. Export screenshot: clarify preset/settings/queue hierarchy.

## Acceptance Criteria

This polish pass is successful when:

- The app still looks like the same app, only much more expensive.
- Current users do not need to relearn where major things are.
- Editing feels easy: users can see tools, expand them, switch between them, and customize them without clutter.
- Functionality is preserved: controls may move to the right visibility level, but they do not disappear silently.
- Shared controls behave consistently across workspaces.
- Tool variants are easy to discover through clear menus or split buttons.
- Every active tool has a clear behavior model.
- Every active tool has clear switch, commit, cancel, disabled, and failure behavior.
- Layers look like a premium stack editor with stable rows and readable state.
- The canvas/grid/board/timeline is visually dominant.
- Controls feel precise and stable.
- The UI uses less text without becoming cryptic.
- Bright accent color is meaningful again.
- There is one clear primary action per area.
- Tool failures are recoverable and nonblocking.
- Screenshots look coherent at first glance and hold up under close inspection.

## Recommended Next Step

Use this document, not the broader redesign plan, as the execution basis for the next UI pass.

The implementation strategy should be:

1. Freeze the current layout shape with before screenshots for every page.
2. Implement component tickets in priority order, starting with foundations/tokens.
3. Audit every page in each component ticket instead of polishing one page in isolation.
4. Standardize navigation, rails, buttons, menus, inspectors, sliders, rows, and media surfaces before page-specific cleanup.
5. Then implement Edit tool discovery/switching and Layers with behavior proof.
6. Finish with page-specific workflow polish for Library, Import, Edit, Masks, Carousel, Timeline, and Export.
7. Keep every ticket small enough that the app remains recognizable after each step.
