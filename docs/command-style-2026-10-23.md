# Command, navigation, and icon consistency

Buttons, tabs, chips, and navigation now use the same shallow graphite command
surface. The [application style contract](ui-style-contract.md) is the policy
owner, and its CI gate and evaluated application audit cover these controls.

## Changes

- `CommandButtonSurface` owns corners and focus/contrast outlines. Ordinary
  selection and hover change neutral fill; danger actions keep their semantic
  color. Use-site outlines, corners, backgrounds, and font sizes are rejected.
- `NavigationTab` unifies workspace navigation, Edit inspector tabs, and Import
  media filters. It supports both fill and underline selection, keyboard
  activation, and an accessible tab press action.
- Sidebar and Preferences rows wrap long titles at their normal size. Selection
  keeps a constant font weight and geometry. Menu shortcuts wrap instead of
  shrinking. Command labels retain the shared 13 px type size, scaled with the
  interface; small secondary captions retain 11 px.
- Agent command wrappers, creative-tool preview buttons, and collection/filter
  delegates now inherit shared actions. Custom preview content is retained.
  Raw Button/RoundButton and qualified stock ToolButton use sites fail the gate;
  local inline component aliases cannot conceal a replacement background.
- Shared icons retain square geometry. The source gate rejects unknown static
  names, including quoted menu-map keys. The missing `jump-end` and `unlink`
  drawings were added to AppIcon; the generated catalog contains 138 names.
- The real application screenshot audit checks command surfaces, readable text,
  clipping, and icon geometry. Rejected captures save a diagnostic image. This
  caught a compact More label rounding error and smaller curve action fonts;
  both were corrected without relaxing the assertions.
- Deferred input backgrounds safely resolve their initial hover/focus state,
  avoiding an undefined-to-boolean QML warning.

## Verification

Evidence and retained logs: `docs/evidence/command-style-2026-10-23`.

- The application and focused targets build successfully.
- `command_style_policy_test` passes 18 density/scale/contrast configurations
  across 16 real controls. It checks geometry stability, readable text, minimum
  hit targets, focus, exactly-once keyboard/accessibility activation, and
  disabled behavior. Rounded, bordered, tinted, tiny-text, and stretched-icon
  mutations are rejected.
- The popup and inspector runtime suites pass (18 and 36 configurations), along
  with icon rendering, button accessibility, shared accessibility, and loading
  scenes. Nineteen source-audit mutation tests pass.
- The source gate passes for 290 production QML files. Both source checks also
  pass through the headless CTest preset.
- Native Cocoa UI Primitives proof passes in Compact, Balanced, and Comfortable
  densities, including interactions and actual Import selected/review captures.
  The command captures were visually inspected.
- Native Cocoa Edit/color-controls proof passes after correcting the curve font
  overrides. Mixer, curve, levels, compact grading wheel, and numeric controls
  are captured with the evaluated command audit enabled. The mixer capture was
  visually inspected.

## Follow-up

The two Agent failures and shader lifetime issue recorded during this pass are
addressed in [remaining UI/style closure](ui-closure-2026-10-23.md), which also
moves Agent chrome onto shared surfaces and inputs. The original failed logs
and crash stack remain in this pass's evidence directory. Use the follow-up
report for current verification results.

This pass qualifies the exercised command primitives and native captures. It
does not establish that every workspace composition is finished or qualify
Windows or release readiness. Unrelated concurrent Timeline/Edit work was
preserved; evidence manifests identify the shared dirty-workspace state used.
