# Framehouse: Spectrum design direction

Date: 2026-09-19. Status: rejected visual direction, not implemented in the native app. This document is an exploration record, not an approved implementation plan. User feedback: the concepts are weaker than the existing design. Re-establish the visual direction before adopting these prescriptions; subtle caustic completion animations remain requested.

## Intent

A precise photographic instrument with a restrained spectral identity. Neutral graphite surrounds the photograph. Cyan, violet, and magenta appear in small, purposeful interaction surfaces. The interface becomes expressive when used and quiet when released.

This proposal responds to the five supplied screenshots and current QML. It supersedes the champagne/brass palette and the restriction against layout changes in the earlier frontend/premium-polish plans for this proposed redesign. Preserve the useful principles: image dominance, readable values, consistent controls, discoverable depth, and trustworthy editing behavior.

Deliverables: this specification and `concept.html`, a standalone visual study. The study uses image regions from the supplied screenshots, illustrative controls, and synthetic scope data. It does not render RAW files, change recipes, create masks, or export photographs. Its view switcher is for comparing designs, not proposed application navigation.

## Diagnosis grounded in this checkout

| Area | Evidence | Design consequence |
| --- | --- | --- |
| Global color | `Theme.qml` centralizes champagne interaction tokens, warm paper text, and several aliases. | Replace roles at the source, then audit overrides. Recoloring only one button family cannot produce cohesion. |
| Duplicate headings | `StudioWorkspace.qml` builds an inspector title, Adjust tab, active-tool-family header, and target banner. | Give each fact one home. Keep tabs plus one compact target row; avoid another Adjust heading. |
| Selection | Screenshot 3 has two toolbar bands, an overlapping tool menu, right-side controls, and repeated slash-delimited state. | Use one contextual command bar and one inspector. Move implementation/debug descriptions into diagnostics. |
| Filmstrip | The active Studio path uses 118-pixel delegates, 10-pixel gaps, 10-pixel tray margins, and `StudioThumbnailCard` with aspect-fit images. Visible-range prefetch assumes a 128-pixel stride. | Portrait letterboxing and fixed cells amplify apparent gaps. Use aspect-aware widths; update virtualization/prefetch arithmetic with the layout. `EditFilmstripThumb` is not this active delegate. |
| Compare | Screenshot 4 labels a two-image view Split, shows Crop above the inspector, and leaves a largely unused right panel. | Fix state-derived titles and mode naming; allow comparison to own the available width. |
| Scopes | `StudioScopeDockContent.qml` has separate flows for five domains, four placement controls, and seven toggles/options before the scope grid. | Graphs first. Keep domain visible; move placement and rendering options into compact menus. Histogram and scopes share one analysis system. |
| Export | `ExportView.qml` prioritizes Background Jobs; configuration sits in a 400-pixel preferred-width panel. | Make the deliverable the main content and jobs a secondary, expandable tray. |

Screenshots are evidence of those visible states, not a complete audit of every current runtime path. In particular, no histogram/scopes screenshot was supplied; that diagnosis is from source structure. Do not infer mask quality or rendering correctness from this design review.

## Visual system

### Color roles

Candidate sRGB values, to be measured against each actual rendered surface during implementation:

| Token | Value | Role |
| --- | --- | --- |
| surface.canvas | `#101010` | Neutral image surround; never spectral |
| surface.app | `#121212` | Application background |
| surface.panel | `#191919` | Inspector and toolbar |
| surface.raised | `#232323` | Inputs, menus, raised controls |
| line.subtle | `#343434` | Quiet structural separators |
| text.primary | `#F1F2F3` | Labels, headings |
| text.secondary | `#ADB0B6` | Secondary context |
| action.primary | `#68E5F5` | Primary CTA, selected control edge |
| accent.violet | `#B3A0FF` | Secondary spectral ingredient |
| accent.magenta | `#EE86C3` | Subtract tool cue / spectral ingredient |
| status.success | `#8AD6A8` | Completed / valid |
| status.warning | `#E8B66C` | Warning only |
| status.error | `#FF858C` | Error / destructive action |

Amber remains a functional warning, never the default selected state. Use neutral numeric text; mark a changed value with a small dot and a reset affordance instead of turning the entire row gold. Preserve the small coral brand mark initially; reduce the large serif wordmark to a compact navigation identity. A new wordmark is a separate branding decision.

Treat RGB and CMY as the conceptual source, not a requirement to spread six saturated colors through every screen. Color carries meaning through role, label, geometry, and location. Active tabs and selected controls use cyan consistently; violet is not a competing active-state color. Mask overlay color remains user-selectable for visibility on different photographs.

### Additive light and subtractive ink

Two bounded interaction treatments share the same shape and state system:

- Light: hovering or focusing a primary action brings two faint cyan/violet lobes into overlap inside its clipped surface. Their overlap brightens. Press settles to a stable solid action surface; completion stops motion.
- Ink: a press or a mode transition introduces a soft pigment-like overlap on a small, lighter inset surface, deepening toward blue/violet. Do not attempt dark multiply over an already black panel; it becomes invisible. Use this sparingly in preset swatches, mask-operation previews, and the identity interaction.
- Panels: a 1-pixel spectral edge can briefly travel 8–12 pixels when a panel becomes active, then settle to a quiet neutral edge. No all-panel glow and no animated idle background.
- Technical scope traces keep their channel meaning; decorative blending never alters chart values or image pixels.

Implementation intent: use a bounded shared QML effect with a static fallback. Linear-light additive compositing is `min(1, A + B)`; a screen-like blend is a visual approximation, not the same operation. Multiplication is a pigment-inspired graphic approximation, not a physical pigment or printer model. Test the actual Qt rendering path and alpha behavior before describing it as color accurate. The HTML study demonstrates the visual idea with screen/multiply compositing only.

Suggested durations: hover 110 ms, press 80 ms, selection 150 ms, panel 180–220 ms. Interaction remains immediate; animation follows state rather than postponing it. Use interruptible easing without bounce. Reduced motion removes travel and blend animation but retains instant state feedback. Stop hidden animations and release offscreen effect resources. Never animate the image or mask alpha for decoration.

### Completion signature: subtle caustics

User refinement: completion should produce a subtle, beautiful caustic animation. Make this a defining interaction detail: an organic field of refracted light briefly crosses the relevant surface, then disappears completely. Think light through optical glass, with fine moving ridges and restrained cyan/violet separation.

- Adjustment committed: a small glint in the changed group's header or value surface, about 300–380 ms. Trigger once at committed gesture completion, never on every slider sample. Coalesce repeated rapid commits so routine editing does not create constant sparkling.
- Selection ready: a 450–520 ms caustic ripple inside the mask row or refinement header after the mask is actually published. Never animate across the photograph or selection boundary.
- Export completed: a slightly broader, 600–700 ms passing light field inside the completed queue row, accompanied by the persistent Completed label and Reveal action. Batch completion gets one collective effect; do not flash every row simultaneously.
- Preset applied or layer operation completed: use the small treatment, and only after success. If it would be noisy in frequent use, reserve completion effects for the higher-significance operations.
- The light is achromatic at its brightest ridges with faint cyan/violet dispersion. It has no warm-gold cast, sharp flash, bounce, particle burst, or ongoing watery motion.
- The state label changes immediately. Animation celebrates truthful completion; it does not substitute for progress, delay activation, or imply success when a job only entered the queue.
- Clip to a single small surface. Keep peak composited opacity approximately 12–25%, then tune on actual displays; preserve label contrast through every frame. Do not apply a refracting filter to the text itself.
- Use a bounded shared effect, fed by semantic completion events from the existing controllers. Prefer a small precomputed caustic field or bounded GPU shader in QML; do not carry the study's per-pixel JavaScript canvas routine into production. Do not share this effect with image-processing shaders.
- Reduced motion provides an immediate quiet completion edge and persistent status without spatial motion. Support disabling decorative completion effects independently. Cancel work when a panel is hidden or destroyed; hidden completions update status without replaying old celebrations when reopened.

`concept.html` includes three explicitly simulated completion samples under Color & motion. Each has a single finite animation and a reduced-motion fallback. These are visual approximations of caustics, not optical simulation or native rendering proof.

### Typography, spacing, density

- One UI sans family across navigation, headings, and controls. Keep the existing platform UI font first; changing the font is not necessary to fix hierarchy.
- Monospace/tabular figures only for values, coordinates, dimensions, time, and short identifiers.
- Logical-pixel starting scale: 12 secondary, 13 controls, 14 body, 16 panel title, 20 workspace title. Essential labels do not shrink to fit. Respect interface scaling.
- Spacing scale: 4, 8, 12, 16, 24, 32. Four between closely related items; 16–24 between groups. Remove ad hoc 7/9/14/18 spacing from new surfaces.
- Radii: 6 controls, 10 popovers; image corners 0–2. Major docked panels use straight boundaries rather than card-in-card stacks.
- Standard control rows 36–40 high, compact 30–32. Primary actions 44. Keep primary hit targets accessible; don't shrink everything to solve overflow.
- Inspector 336–384 wide at normal scale, resizable. Tool rail 48–52. Workspace header 48–52. Filmstrip 72–88 including tray padding.
- Density is a preference affecting row and group spacing, not font legibility or image framing. At larger UI scale, reduce simultaneous columns/docks and offer scrolling instead of clipping.

### Shared control states

| State | Treatment |
| --- | --- |
| Rest | Neutral surface, readable neutral label |
| Hover | One surface step brighter; optional bounded spectral treatment on designated actions |
| Pressed | Slightly deeper fill; no geometry shift |
| Selected | Cyan edge or inset marker plus quiet tint; consistent within each control family |
| Keyboard focus | Separate 2-pixel high-contrast ring, survives selected/hover states |
| Modified | Small dot next to neutral value, accessible modified description, reset available |
| Disabled | Muted stable geometry with a discoverable reason; not a silent nonresponse |
| Busy | Label describes the real operation, progress only when known, cancel when supported |
| Error | Message plus icon and recovery action; never color alone |

Tabs share one selected underline. Segments share one filled selected cell. Buttons share one primary/secondary/quiet/destructive hierarchy. These are deliberate family differences; no unique cut corner, gold slash, and extra underline stacked on one control. Reset appears on hover AND keyboard focus and remains in the group menu for discoverability.

## Layout and workflows

### Global shell

Retain Library, Edit, Carousel, Timeline, Export, and Scene. Use a smaller mark/wordmark, stable navigation, and one asset/project context location. Relocate version text and persistent healthy GPU/model/catalog/export indicators to an activity/status popover. Failures remain visible where action is required. The resident assistant becomes a compact entry in the shell with a real activity badge; avoid reserving a large filmstrip cell or covering a photo. Its proof/activity workflow remains intact.

Keep the photograph's immediate surround achromatic. Color lives in small controls away from the image. Provide a focus-view toggle that hides chrome and a neutral surround preference for photographic judgment.

### Library

- One toolbar: Search, Filters with active count, sort, view, Import. Expand filter chips only when active or explicitly opened. Preserve AI search as an explicit search mode.
- Justified contact sheet, 4-pixel gutters, adjustable row height, whole-image framing. Selected asset gets one cyan boundary and a distinct multi-select check; rating/review state stays separate.
- Inspector: filename, compact rating/flag line, optional preview, prioritized capture metadata, project membership. Full path opens through location/reveal details instead of wrapping through the primary panel.
- Put Edit and Export near the asset summary. Review actions stay a compact row with keyboard shortcuts. Avoid full-width stacks of infrequent actions.
- Preserve collection/project navigation and multi-selection behavior; don't replace useful filters with a decorative search-only screen.

### Edit and adjustments

Shell: compact rail / largest possible canvas / one inspector / dense filmstrip. A 40-pixel canvas toolbar contains fit, 100%, zoom, before-after, and analysis entry points. Tool-specific options replace its contextual region rather than creating another band.

Inspector top: `Adjust | Presets | Layers | History`, then `Target: Base image` or `Target: Subject · Layer 1`. Target is interactive and reachable with keyboard. Remove outer Adjust heading, repeated active-family heading, and verbose editing-target card. Use the group heading to explain the next controls.

Default visible groups: Light and White balance. Color, Detail, Optics, and Geometry remain discoverable as concise group headers. Remember expanded groups per workspace; let people pin favorites and show only changed controls without losing search.

Rows: label / track / exact value / contextual reset. Use consistent columns. Exposure, contrast, highlights, shadows, whites, and blacks fit together. Keep less common brightness/advanced tone controls within the same group's disclosure, not removed from the product.

White balance: Temp and Tint stay visible. The large 2D pad becomes an expandable precision view, alongside eyedropper and preset. Preserve drag anchoring, fine adjustment, axis lock, cancellation, and coalesced history behavior. The compact view and expanded pad edit the same values.

Filmstrip: one row, common image height about 60–64, widths from source aspect ratio, 4-pixel gaps, 6-pixel outer padding. Set practical panorama width limits and preserve aspect fit within them. Keep keyboard navigation and scroll-to-current. If aspect metadata is unavailable, use a stable placeholder ratio and update without jumping the selected asset. Replace fixed-stride prefetch with geometry-aware visible-index lookup. Do not create a speed regression in a large library.

### Selections

Three explicit stages with one inspector slot:

1. Create: Subject, Sky, Object, Brush, Gradient, Range as a compact labeled tool picker. Advanced source variants live under the appropriate tool.
2. Refine: compact mask stack, one Add/Subtract/Intersect/Replace selector, brush/edge properties for the chosen tool, and a View menu. No floating menu covering the photograph after tool choice.
3. Adjust: the same target row identifies the selected mask/layer; controls return to the adjustment inspector. Selection remains easy to revisit.

Mask management and preview: use names/thumbnails, visible lock state, one opacity control for mask strength, separately labeled overlay opacity inside View. Avoid ambiguous density/opacity twins. View choices are Outline, Overlay, Black & white, and Hidden. Original/refined preview, inversion, and quality live in View/Refine options, not permanent top-level buttons.

Keep Feather and Smooth prominent; expand edge-specific controls when requested. Duplicate/copy/copy inverse/reset move to the mask menu. Common Add/Subtract remain immediately reachable. A short contextual bar can read `Subject 1 · Brush   Add / Subtract   Size   Done`; advanced operations stay in a selector. Never show the old multi-line slash-delimited diagnostic string by default.

Refinement retains explicit Done/Cancel, visible pending state, Escape rollback, undo/redo, and non-destructive layer targeting. Switching tools must follow the existing transaction contract; settle or ask about unsaved refinement rather than silently discarding it. Locked masks remain viewable. Existing mask correctness issues require their own proof, separate from UI polish.

### Compare

- One toolbar: reference selector, `Side by side | Wipe | Difference`, linked pan/zoom, fit/100%. Difference only appears as enabled once the renderer supports it; it is an optional capability, not a prerequisite for this visual redesign.
- Reserve Split for an actual divided single-image presentation, or remove the term. Do not call two independent images Split.
- Collapse the right inspector by default. Put small Before/After labels next to the corresponding image viewport. Optional history/snapshot chooser opens as a drawer.
- Fit both images into the shared available area; use identical magnification, reference bounds, and linked pan. Preserve deliberate aspect-ratio letterboxing rather than cropping photographs to eliminate all blank space.
- Wipe uses one uncluttered drag handle and keyboard-adjustable divider. Comparison must not change the underlying edit or selected filmstrip asset.
- Tone/Color/Detail/Selections comparison filters move into a comparison-options menu, with a concise active filter chip when used. Show an accurate reference label, including snapshot name.

### Histogram and scopes

One Analysis dock, available from a single chart icon, with Histogram, Waveform, RGB parade, and Vectorscope. Histogram is the compact default. Allow a resizable bottom dock or right dock and an expanded analysis view; placement lives in the dock menu.

Graphs occupy the majority of the dock. Header contains scope type, domain selector, and options. Keep the chosen domain legible and meaningful: Scene, Working, Output, Proof, or Display. Proof shows the selected proof profile and its enabled state. Do not silently merge these domains because their pixels differ.

Render with fine grids, clear units/axes, meaningful channel colors, clipping indicators, and consistent type. Density/trace/grid/sampling options move to one popover. Sampling state is visible when analysis is approximate or stale. Numeric readout appears on hover/focus, not as permanent status prose. Do not fabricate numeric accuracy or treat synthetic prototype charts as engine output.

### Export

Lead with what will be delivered: selected image preview/contact sheet, file count, filenames, destination, size, format, color profile. Use the center as a delivery review; keep an organized settings panel on the right. For batches, show a small virtualized list of selected deliverables.

Settings groups: Preset; Format and quality; Size and crop; Color; Destination and naming; Metadata. A preset explains its resolved values. From recipe must show the resolved profile per asset; mixed profiles get an explicit Mixed summary. Preserve 16-bit export, existing-file protection, collision naming, and preflight checks.

Sticky footer: `Export 1 photo` plus compact resolved destination summary. Do not put all useful action at the bottom of a long scrolling form. Native directory picking remains available. Existing-file handling is legible before commit.

Jobs become a collapsible bottom tray; expand automatically when exporting. Pending/running/complete/failed rows include progress, cancel/retry where supported, and Reveal when finished. With no jobs, one quiet line is enough. Never dedicate the main screen to repeated No background jobs messages.

### Remaining surfaces

Carousel (the separate workspace, not the Edit filmstrip), Timeline, Scene, import dialogs, agent dialog, menus, layer lists, and empty/error states all inherit the token and control system. Audit their state colors and density before declaring global completion. Preserve timeline track/channel colors that carry meaning; keep creative content separate from UI accent migration. Detailed structural redesigns of those unsupplied screens need their own visual review.

## Implementation sequence and completion gates

1. Baseline and shared system: record current five screenshot states and compact/large window variants. Inventory existing hard-coded colors and button families. Update Theme roles, shared controls, typography, focus, motion, and density. Maintain temporary aliases so adoption can be incremental, with an explicit retirement checklist. Verify new roles everywhere before deleting old ones.
2. Edit reference implementation: simplify hierarchy, target row, adjustment rows, WB disclosure, and filmstrip. Verify real photographs, keyboard/pointer operation, live adjustment feedback, selected-asset scrolling, and large-library virtualization. Use this as the reference for other inspectors.
3. Selection restructuring: implement the create/refine/adjust flow using existing controllers. Verify mask targeting, locks, pending changes, Done/Cancel, Escape, undo/redo, brush locality, and real edges. UI beauty does not close mask-quality gates.
4. Compare and Analysis: correct labels/state routing, recover canvas width, consolidate analysis controls. Verify sync zoom/pan, wipe, reference persistence, color domains, clipping, and performance on actual assets.
5. Library and Export: consolidate filters/metadata and build delivery-first review. Verify multi-select, sorting, filters, presets, resolved export settings, existing-file protection, success/failure/cancel, and emitted files.
6. Global completion: port remaining workspaces/dialogs and run screenshot, keyboard, reduced-motion, high-contrast, color-vision, scale, and latency review. Update existing documentation/contracts to the actual implemented system; do not leave champagne contracts describing cyan controls.

Source seams: `Theme.qml`; `ActionButton.qml`, `MiniButton.qml`, `ToolButton.qml`, `CommandButtonSurface.qml`; `StudioPanel.qml`, `StudioInspectorGroup.qml`, `StudioAdjustmentSlider.qml`, `StudioAdjustmentPad2D.qml`; `StudioWorkspace.qml`, `StudioThumbnailCard.qml`; `StudioScopeDockContent.qml`, `StudioScopeGrid.qml`, `StudioScopeTile.qml`, `HistogramDisplay.qml`; `LibraryWorkspace.qml`, `ExportView.qml`, and `ExportWorkspaceHost.qml`.

Acceptance must include:

- Each fact has one visible owner: workspace, active tool, target, and pending operation. No duplicate Adjust headings or stale Crop label in Compare.
- Normal editing gives more usable canvas and more useful adjustment rows at the same window size, measured against a saved baseline. No claim based only on smaller fonts.
- Filmstrip has 4-pixel inter-image gaps in normal cases; portraits do not occupy landscape-width empty cells.
- No primary or selected controls use the old gold theme; warnings remain distinguishable.
- At 1280×800, 1440×900, 1920×1080 and 1×/1.5×/2× interface scale, no overlapping labels, clipped essential actions, offscreen Apply/Cancel, or inaccessible dock controls. Adapt layout rather than demanding every dock stay visible.
- Measure text contrast on actual composited surfaces, including hover/disabled/selected; target 4.5:1 normal text and 3:1 necessary graphical controls. Validate keyboard focus and non-color cues. These are design targets, not a compliance claim.
- No decorative color or animation changes photo pixels, masks, or scope measurements. Capture real RAW/JPEG editing, comparison, and exported output.
- Reduced motion removes decorative travel; idle animation stops. Compare input latency and frame timing against baseline on host GPU hardware. No additional full-image renders for hover or focus.
- Run focused native/controller/QML interaction checks plus native screenshots and manual photographic review. Source contracts and lint are not visual proof.

## Review decisions

Recommended defaults: cyan as the single active-state color; violet/magenta in bounded blend effects; neutral panels; compact sans wordmark in the shell; aspect-aware filmstrip; progressive selection inspector; collapsible Compare inspector; delivery-first Export. The visual study makes these choices concrete for review. No native app changes are included in this proposal.
