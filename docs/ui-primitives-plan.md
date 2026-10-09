# Looksy UI primitives

Open **Help → UI Primitives**, or search for **UI Primitives** in the command palette. This is the central live reference and planning surface for shared UI. It opens in its own desktop window and leaves the current document and workspace available.

## Design direction

Build a recognizable desktop editing studio: neutral photographic surroundings, shallow rectangular command geometry, clear type hierarchy, and purposeful spacing. Avoid ornamental left badges and excessive rounded button chrome. Color should communicate an action or state. Selection and keyboard focus must remain distinct.

The chosen character is **Optical studio with a friendly technical feel**: sharp, colorful, and clean. Small controls have a shallow light-catching finish; hover uses cool optical light, pressing feels inset, and keyboard focus remains a full white outline. Blue identifies selection, coral belongs to the brand, and amber identifies recoverable attention. Menus and tooltips share an opaque graphite surface with crisp edges. Aperture indicators animate only while working; warnings use steady symbols and an appropriate recovery action. Reduced motion uses static indicators and immediate state changes.

Foundations starts with a working Optical studio interaction reference: hover/press/selection, a menu with checked/disabled/destructive items, a shortcut tooltip, processing/completion, and an actionable warning. These use the shared application components. Preview actions remain local. Shadows are disabled on the software scene graph so popup surfaces remain opaque on fallback rendering.

Comfortable, Balanced, and Compact should preserve the same capability and hierarchy while changing spacing and density. The page previews all three through the actual shared `Theme`; its local context also supports reduced motion and high contrast. These previews do not save application preferences or change document state.

## Central reference

| Page | Live reference | Planning purpose |
| --- | --- | --- |
| Foundations | Theme colors, typography, density geometry | Agree on semantic tokens before workspace-specific styling. |
| Commands | ActionButton, ToolButton, MiniButton, MenuButton, SplitButton, FilterChip, ColorSwatchButton | Compare roles, action priority, target sizes, selected states, disabled reasons, and keyboard focus. |
| Inputs & inspectors | Tone fields, combo box, checkbox, spin box, InspectorSlider, InspectorSection, StudioCollapsibleGroup | Compare field shapes, typed entry, resets, standard/compact slider rows, and the competing group headers. |
| Navigation & feedback | PreferenceNavRow, ImportSourceRow, ImportFolderRow, StatusChip, activity/progress controls, InfoPill, metadata, empty state, StudioPanel | Separate destinations, actions, and passive information; review recovery and layout. |
| Icons | Actual AppIcon names and aliases | Keep stroke geometry and meaning consistent. |
| Component index | Searchable shared QML inventory with family, coverage, and adoption guidance | Keep every shared component visible in the plan, including compositions needing workspace review. |

“Live specimen” means the page has a working example. “Workspace review” means the source is indexed but requires review with its real document/controller context. Neither label means the app-wide UX is qualified. Workspaces themselves live in `qml/workspaces/`; they are assessed during adoption rather than presented as generic primitives.

## Shared visual system — October 5 polish

The shared primitives now use the native platform UI typeface in both the application and the gallery. Controls use a medium weight; section headings use semibold. The common scale is 13 px for body/control text, 12 px for captions, and 11 px for compact metadata, with the existing interface-scale preference applied throughout. Numeric steppers retain monospace alignment.

Graphite panel and field surfaces create hierarchy without heavy black outlines. Controls use shallow 3 px corners and panels use 4 px corners at 100% scale. Selected surfaces, pointer hover, pressed states, and keyboard focus have separate roles. Primary actions use muted blue; recovery and destructive states retain semantic colors. Navigation no longer needs decorative left markers. Import source and folder rows also use flat selected surfaces with readable wrapping labels, without the old rails, dots, and redundant Active/Open chips. The photographic canvas remains dark.

| Area | Shared implementation | Remaining review |
| --- | --- | --- |
| Commands | Consistent typography, 3 px corners, graphite secondary surfaces, quiet embedded actions, distinct hover/press/focus. | Replace remaining bespoke workspace buttons where the role matches. |
| Primary actions | ActionButton, MenuButton, and SplitButton use density-based primary heights: 36 px Comfortable, 32 px Balanced, and 30 px Compact. Split menu segments reserve their actual width so labels fit. | Review long labels and adjacent controls in complete workflows. |
| Swatches | Density-based 32 / 28 / 26 px pointer targets around a compact color face. | Confirm placement and keyboard navigation in real color workflows. |
| Inputs | Shared font, neutral field fills, thin edges, and consistent focus styling across fields, text areas, combos, checkboxes, and steppers. | VoiceOver and contextual validation remain separate checks. |
| Inspectors | Shared heading weights and sizes; aligned slider readouts; standard and compact arrangements preserve typed entry/reset behavior. | Prefer flat sections; adopt nested disclosure only where it helps scanning. |
| Feedback | Content-sized information readouts, calmer activity surfaces, real panel subheads with full-text hover access when abbreviated. | Choose one appropriate progress presentation for each actual task. |
| Navigation | Flatter rows with clear selected/focus surfaces and no decorative left markers. | Review all destinations and saved-layout behavior in each workspace. |
| Motion | Short shared color/opacity transitions; a texture-based aperture rotates during activity, while attention symbols remain still. Reduced motion stops rotation, popup transitions, and progress interpolation. | Human review of complete transitions and VoiceOver remains open. |

A narrow Timeline layout correction also reserves space for the track pane and splitter before sizing the monitor minimum, preventing the Finish layout from overflowing a compact laptop window.

The follow-up compact refinement removes hardcoded large target floors. Typical compact commands use 30 / 28 / 26 px heights and ordinary commands use 34 / 30 / 28 px heights. Fields use 36 / 32 / 30 px heights. Primary actions are distinguished through their role and color, while the 13 px label size stays readable. Interface scaling can enlarge the whole UI when needed. Tooltips use restrained corners and tighter padding.

These changes affect the existing shared components, not just the gallery. The component index also includes compositions that still require individual workspace review; indexing a component does not qualify it.

## Adoption and acceptance

1. Review and choose primitive roles and geometry here.
2. Change the existing shared components and tokens, preserving their accessibility and interaction contracts.
3. Replace bespoke workspace styling with those components where the role matches.
4. Validate real workflows in Edit and Timeline first: add media, trim, move clips, play, adjust color, and export. Continue through Library, Import, Export, Carousel, Scene, and Search.
5. Review all three densities, narrow and wide desktop windows, long labels, hover/press/focus/selected/disabled states, keyboard navigation, reduced motion, and high contrast. VoiceOver and real-photo/workflow qualification remain separate checks.

The page includes concise guidance and unresolved questions, while this document records the adoption plan. Keep decisions here and the live specimens aligned. Demo values are temporary and restart when changing sections; search and preview preferences last only for the current page instance.

## Paint and layer workspace adoption

The pixel-paint options surface sits below the viewport's zoom and view toolbars. Paint/Erase, color, and horizontal Size/Edge/Opacity/Flow controls form explicit groups; the numeric readouts stay readable and the metric group wraps as a unit when the canvas is narrow. The viewport reserves the options surface height. Pressure, materials, spacing, and fragment/light options remain behind the disclosure control.

The layer stack uses 13 px titles, neutral 12 px state text, shallow selected surfaces, and density-sized action zones. The base image reports its locked state and changed edit-group count in its subtitle rather than decorative badges. Selected-layer properties separate identity, aligned opacity/blend controls, and labeled Mask/Lock/Duplicate/Delete actions. Details exposes less frequent commands. Global Reset/Save Preset controls belong to adjustment/preset/filter tabs.

Paint entry explicitly selects the existing pixel layer, keeping the highlighted row and painting target consistent. Selecting Base Image hides unrelated pixel-layer properties and disables painting until a pixel layer is selected. Native layout/interaction evidence for this adoption lives in `docs/evidence/brush-layer-ui-2026-10-05/`; photographic and export fidelity remain separate qualification.

The selection brush follows the same toolbar language: visible Add/Erase actions, horizontal size/hardness/flow controls, a precise size field, and Options/Done. Controls wrap inside the available canvas width; the viewport reserves the toolbar's actual height below the selection-tool rail. Its Layers summary uses readable target and selection text, and isolation acts on the selected layer. Native follow-up evidence is recorded in `docs/evidence/selection-brush-ui-2026-10-05/`.

Selection and nested mask rows size to their text and progress chips, keeping More in its own action area. The floating brush readout clamps and wraps within the viewport; preview-loading and mask-processing notices share one separated stack. The final selection-brush pass has 70 native layout captures and focused control checks across all three densities.

## Edit inspector adoption — October 5

Everyday controls now follow the editing workflow: White Balance, Light, conditional Raw controls, Levels, Tone Curve, Color, Color Mix, and Color Grading, followed by specialist color and finishing controls. The Light, Color, and Detail filters follow the same grouping. Color Mix has its own visible section; Channel Mixer is available in Specialist Color without an extra Lab switch.

White Balance uses Temperature in Kelvin, Tint, and Pick neutral. Levels has its own histogram, input markers, and precise fields; Gamma accepts the displayed ratio. The curve plot has a smaller inset and quieter frame, with presets behind one toggle and parametric tone ranges in a separate disclosure. Grading uses a large, full-width hue/saturation disc, three range tabs, precision fields below, and global Amount and Balance last. The disc grows with the inspector, uses a neutral center and clean saturated edge, and has a larger reactive handle. Existing selected-target routing, numeric entry, resets, and undo grouping remain part of the adoption contract.

## Maintenance and verification

Refresh the component and icon inventory after adding or removing shared files:

```sh
python3 scripts/update-ui-primitives-catalog.py
```

The script indexes actual shared QML filenames and `AppIcon` switch cases. Its family-level plan labels are guidance, not automated UX judgments; update curated coverage and classification when specimens change. Register new specimens and the JS index explicitly in `apps/desktop/CMakeLists.txt`.

Focused verification is `desktop_shell_ui_primitives_test`. It opens the page through the actual Main shell route with isolated temporary app data. It checks six sections at three densities, nested control geometry, keyboard operation, typed slider entry/reset, recoverable status, filtering, preference/workspace isolation, and close/reopen. The original gallery evidence is under `docs/evidence/ui-primitives-2026-10-05/`. The shared typography/control polish and native workspace captures are under `docs/evidence/ui-primitives-polish-2026-10-05/`, with the smaller-controls follow-up under `docs/evidence/ui-compact-refinement-2026-10-05/`; see its verification record for precise scope and results. The evidence covers rendered layouts and focused interactions; complete media-editing workflow, VoiceOver, photographic-quality, and distribution qualification remain separate.

### Creative tool panel adoption

Paint brush, selection brush, shape creation, shape properties, and typography share a dedicated Tools panel in Edit's right sidebar. The panel sits above the existing inspector, so layers remain available. Its divider adjusts the available space; long tool sections scroll independently. The header's Float/Dock action moves the same control instances into a resizable native tool window. Hide closes the controls without changing the active tool, selection, or document; Layout > Tool controls and reactivating a tool reopen them.

Tool visibility, docking, and panel height are included in workspace settings and named layouts. Full screen and content focus hide tool chrome. Tool panels use compact typography, shallow corners, horizontal numeric sliders, and contextual target labels. Paint errors and busy status remain readable in the narrow panel. Text starts with content, font, color, size, and alignment before the advanced typography and perspective sections.
