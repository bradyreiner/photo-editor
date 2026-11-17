# Enforced application style

This is the authoritative specification for application chrome. It supersedes
conflicting palette, radius, border, and inset examples in older design plans.
Product imagery, color-editing visualizations, artwork, and native operating
system menus/file pickers have their own rendering requirements.

At 100% interface scale:

| Property | Required behavior |
| --- | --- |
| Neutral chrome | Opaque graphite steps with equal red, green, and blue channels. Popup fill is `#242424`. |
| Commands | 3 px corners; no resting outline in standard contrast. Ordinary actions and filter chips rest transparent; explicit primary and secondary actions retain restrained graphite fills. |
| Tabs, chips, and navigation | Shared command surfaces; selection changes fill or the shared tab underline. Selection must not change size or font weight. |
| Command typography | Use the shared UI face for labels, captions, status, and shortcuts. Numeric values and timecodes use `Theme.fontNumeric` (the same system UI face as `fontUi`) with `Theme.numericFontFeatures` for tabular lining figures; reserve monospace for code-like diagnostics. Fixed shared type size: 13 px labels and 11 px secondary captions before interface scaling. Long navigation labels and shortcuts wrap; they do not shrink to fit. |
| Body typography | BodyLabel owns scalable body text in Agent, recovery, and Copy Edits. Default 13 px, captions 12 px, minimum 11 px before interface scaling. No fit-to-box shrinking. |
| Checkboxes and switches | ToneCheckBox and ToneSwitch own neutral indicators, shared labels, focus, and disabled states. Checkbox descriptions wrap at 12 px; labels wrap at 13 px. Mixed checkboxes show a minus. |
| Short or enlarged layouts | Keep actions reachable while body content scrolls. Show folder or file names at rest. Full paths remain available through tooltips, accessible descriptions, and explicit editing or keyboard-accessible details with Copy path. |
| Command icons | Frequent viewer, overlay, scope, dock and menu-header actions use shared icons with full tooltips and accessible names. Keep short labels for less familiar commands, values and choices. Labelled commands can explicitly opt into a leading icon. Icon controls keep square geometry and interface scaling; unknown static icon names fail the source gate. |
| Assistant character | Confined to Agent view. Editor status and loading scenes use optical activity; character placement elsewhere fails the source gate. |
| Panels and floating popup surfaces | Shallow 4 px corners; no resting outline in standard contrast. |
| Docked surfaces/drawers | Straight boundaries, 0 px corners. |
| Selected shared panels | Neutral fill change identifies selection; use-site border/color overrides are rejected. |
| High contrast | A 1 px structural outline remains available. |
| Keyboard focus | A visible outline of at least 2 px, scaled with the interface; focus must not resize controls. |
| Panel edge insets | Compact 8 px, Balanced 10 px, Comfortable 14 px. Avoid nested cards that repeat the same padding. |
| Dialog/popover edge insets | Compact 8 px, Balanced 12 px, Comfortable 16 px. |
| Header and actions | Same horizontal inset as the body; the header bottom and footer top do not add a second content gap. |
| Compact menus/tooltips | Shared spacing tokens, bounded to 16 px before interface scaling. |
| Input fields and steppers | Shared InputFieldSurface, 3 px corners, neutral fill, no resting outline; hover changes fill. Focus and high contrast retain outlines. |
| Inspector numeric columns | Standard value/reset columns are 80/28 px and scale with the interface. The compact brush HUD uses a 48 px value column; other rows use the shared 80 px value column. Numeric fields keep the shared UI face and tabular lining figures at every size. |
| Slider rows | Compact when the label and controls fit; narrow or long-label rows put the full-width track below the header. Labels wrap at their normal size. |
| Inspector sections | Transparent, square, borderless grouping. Titles wrap; disclosure and reset remain keyboard accessible. |
| Tool groups | Related commands use the density-aware `gapCommand` token (2/3/4 px); separate groups retain larger gaps. Resting surfaces stay subdued. The neutral secondary hover is `#2E2E2E`, visibly separated from the `#242424` popup surface; pressed states darken and keyboard focus keeps its explicit outline. |
| Menu selection | Checked rows use a checkmark; the row fill follows pointer hover or keyboard focus. Menus use one outer surface and a 2 px row gap, without persistent boxes around individual choices. |
| Slider geometry | 3 px track, 12–16 px visual thumb inside a fixed 16 px handle reservation, scaled with the interface. Circular thumbs are functional control geometry. |

Geometry scales through Theme. Shared PopupSurface, AppDialog, AppPopup, AppMenu,
AppDrawer, AppToolTip, and command primitives own their appearance. Custom
background rectangles, palette overrides, raw popup controls, and use-site
border/radius overrides must fail the style gate. A specialized docked popup can
use the shared `docked` surface mode rather than defining its own radius.
Raw Button/RoundButton and qualified stock ToolButton use sites are also
rejected. Local inline aliases are resolved so they cannot bypass these rules.
Stock TextField/TextArea/SpinBox/ComboBox use sites must use their shared Tone
counterparts, including read-only selectable detail views.
Stock CheckBox, Switch, and RadioButton use sites are rejected. Choice controls
cannot replace indicators, label content, or the shared type scale. Preferences
navigation backgrounds must stack behind their labels.
Commands retain semantic danger/warning/success colors where the action needs
them; ordinary selected controls use the neutral palette.

## Menu and tool interaction

- Clicking a menu opener again closes its popup. Dismissing on pointer press
  must not let the forwarded release reopen it. Opening a different transient
  menu closes the previous one; workflow dialogs keep their own lifecycle.
- Pointer-opened menus take focus without a keyboard outline until keyboard navigation starts.
- The shared transient Escape shortcut owns dismissal and focus return; the built-in Escape close policy is reserved for workflow popups.
- Escape returns focus to the opener. Clicking outside dismisses without
  taking focus away from the newly clicked control. Choosing an action must not
  take focus back from a dialog that the action opens.
- Complex Timeline options use focused Panels, Viewer, Playback, Workspace,
  and Background groups. Interface scale and density remain in Settings.
- Curve numeric edits, Delete, and Reset form complete undo transactions.
  Narrow graphs reflow their numeric fields at the shared readable type size.
- Layer visibility, locking, and More actions do not change the editing target.
  Layer names retain selection, double-click rename, and drag-to-reorder.
- Resize handles expose a resize cursor and visible keyboard focus. Keyboard
  resizing respects the same panel bounds as pointer resizing.

### Menu copy and chooser detail

Secondary action menus use one ellipsis opener: set `secondaryActionMenu: true`,
keep the semantic `More` label, and supply a contextual `menuTooltipText` or
`accessibleName`. The shared MenuButton supplies compact geometry, hides the
generic label and redundant chevron, and opens the menu from its primary button.
Explicitly labelled menus and primary split actions retain their labels.

Ordinary menu rows show the command label, optional shortcut, and selection
state. A checked row retains its identifying icon and shows its check separately.
Avoid duplicate commands for controls already exposed beside the menu; panel and
saved-layout commands belong in the Layout menu. Explanations and disabled
reasons stay available through tooltips and accessibility descriptions. Menu
captions and shortcuts use the system UI face;
numeric readouts and timecodes use its tabular lining figures, while monospace
is reserved for code-like diagnostics.

Routine file choosers identify items by a friendly label or filename. Full
system paths remain available through tooltips, accessible descriptions, or a
focused details/copy action instead of taking over the row.

## Required checks

- `popup_system_inventory_test` scans every production QML file, follows custom
  popup/control inheritance, and rejects shared-component bypasses, background
  replacement, surface overrides, attached stock tooltips, and unsupported edge
  insets. Native Menu exceptions require an actual application MenuBar ancestor.
- `ui_style_audit_regression_test` injects bad implementations, including custom
  subclasses, inline components, grouped/dotted overrides, oversized token-based
  padding, and stock controls. It verifies rejection and checks that comments,
  strings, and nested JavaScript do not create false violations.
- `ui_style_policy_test` instantiates real QML primitives across three densities,
  three interface scales, and both contrast modes. It checks evaluated palette,
  geometry, coordinated insets, keyboard focus, and rendered pixels. Deliberately
  rounded, bordered, tinted, and over-padded mutations must be detected.
- `inspector_style_policy_test` checks 36 combinations of density, scale, narrow/
  standard inspector width, and contrast. Values and labels must fit, numeric
  columns align and stay stable across value changes, input pixels have no
  resting outline, and typed values, reset, and disclosure remain functional.
- `command_style_policy_test` checks 16 real controls in 18 density/scale/contrast
  configurations. It checks normal-size text, clipping, square icons, stable
  hover/selection geometry, scaled hit targets, visible keyboard focus, exactly
  one keyboard/accessibility activation, and disabled behavior. Deliberately
  rounded, bordered, tinted, undersized-text, and stretched-icon mutations must
  be detected.
- `readability_choice_policy_test` checks 36 combinations of density, 90/100/200%
  interface scale, contrast, and 220/360 px logical widths. Shared choices must
  preserve exactly-once keyboard, pointer, and accessibility activation; disabled
  controls stay inert. Long descriptions and literal folder paths wrap at the
  shared type size. Tiny text, fit-to-box shrinking, and tinted indicators are
  deliberately injected and must fail.
- Copy Edits and both Agent UI routes have default and 200% scale tests. Copy
  Edits scrolls focused groups into view; Agent plan details scroll independently
  of the fixed action footer. Native Import proof checks long folder paths at
  90/100/200%, path copying, Escape dismissal, and focus return.
- `shader_animation_lifecycle_test` checks creation without a window, attachment,
  item/window visibility, zero size, detachment, reattachment, reduced motion,
  and destruction. Shader uniform animators must stop when detached; native
  qualification runs the same transitions with the threaded renderer.
- Agent UI tests run the evaluated style audit, require the shared popup frame,
  check visible optical activity, and keep the character out of editor status.
  `agent_production_motion_ui_test` now checks real deterministic controller
  receipts and inspector noninterference using an automatic isolated fixture;
  it no longer silently skips when an evidence directory is omitted.
- Application screenshot routes using `capture_visual_proof` run the same
  evaluated style audit on the actual application object tree before accepting a
  frame. It checks popup, input, and command surfaces, command text and icon
  geometry, body and choice readability, numeric face/features, scaled slider
  columns, and flat inspector sections. Runtime overrides cannot pass just by
  retaining a contract string.
- `popup_system_ui_test` continues checking viewport bounds, actions, Escape,
  and focus return; visual tightening must preserve those behaviors.
- `menu_popover_bounds_test` checks repeated opener clicks, cross-menu and
  selector dismissal, and root-wide popups. `grouped_options_ui_test` checks
  grouping, bounds at normal/enlarged scale, and exactly-once activation.
- The native layout proof captures all six Settings pages and enlarged General
  settings. The layer workspace shard checks row-name selection, rename,
  visibility, locking, and reorder with pointer input.
- `timeline_transform_interaction_test` checks viewport input routing, resize,
  rotation, Escape, tool switching, and save/reopen. The existing full
  `timeline_transform_workflow_test` retains its additional fade-export pixel
  checks; the focused interaction route does not replace that render gate.

The source gate and its regression tests run in the CI repository check and in
CTest, including headless builds. The runtime test runs in the desktop CTest
suites on macOS and Windows. Python is required when configuring tests so the
source gate cannot silently disappear. Geometry/palette policy changes require
an intentional update to this specification, tests, and rendered review.

These checks enforce concrete style rules. New compositions still require
rendered review for hierarchy, clutter, and photographic usefulness; a passing
numeric check is not a claim that every screen is aesthetically finished.

## Native interaction review

1. Open the same menu twice, switch directly to a different menu, dismiss with
   Escape, and click outside. Repeat on the small Boards rail hotspot and the
   shape/brush choosers. An action that opens a dialog must leave focus there.
2. Review Settings at normal and 200% text scale. Navigation and Close stay
   reachable while the current page scrolls; background controls also reflow.
3. Resize a curve inspector, type an exact point value, drag, cancel with Escape,
   and use Delete/Reset followed by Undo. Confirm plot and numeric input agree.
4. In Timeline, open and close Graph from its toolbar button. Activate Crop while
   a poster is displayed, resize the viewer, and drag a crop edge. Its handles
   must align with the source, and Crop/Transform must remain mutually exclusive.
5. With two layers visible, rename one, toggle its visibility and lock while the
   other is selected, then reorder from its name. Each click must affect only
   its intended target, and a drag must not become an accidental rename.
