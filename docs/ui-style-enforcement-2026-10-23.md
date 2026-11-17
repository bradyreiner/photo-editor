# Application style enforcement

The authoritative geometry and palette rules are in
[ui-style-contract.md](ui-style-contract.md). Older design documents now link to
that contract so historical radius and spacing examples cannot serve as the
current specification.

Shared popup surfaces now have 4 px corners, neutral fills, and no resting
outline in standard contrast. Dialog/popover edge padding is 8/12/16 px for
Compact/Balanced/Comfortable, with coordinated header and action insets. High
contrast retains a structural outline, and keyboard focus retains its visible
outline without changing control size. Docked surfaces remain square.

The repository-wide source audit initially found 15 violations across Import,
Timeline, agent, and shared surfaces. They included raw menus, custom surface
geometry, fixed edge padding, and redundant surface overrides. Those uses now
inherit shared chrome. Specialized docked popups use the shared docked mode.

CI and CTest now enforce the source audit, including headless CTest presets.
Python is required when configuring tests so this check cannot be skipped
silently. The audit follows QML inheritance and handles comments, strings,
nested scopes, inline components, grouped properties, and dotted properties.
Ten regression tests exercise deliberate bypasses and valid syntax.

`ui_style_policy_test` checks real QML instances in 18 density/scale/contrast
combinations. It checks actual property values, painted pixels, neutral palette,
coordinated header/body/action insets, and keyboard focus without resizing. It
also deliberately introduces rounded, bordered, tinted, and over-padded
replacements and verifies rejection.

The real desktop shell and Import fixtures audit their application object trees
by default. The common screenshot route repeats that audit before accepting a
frame, checking evaluated popup surfaces and bounded edge insets rather than
contract-description strings. The native Import fixture explicitly activates
the Cocoa test application before frame/input checks. No layout tolerance or
acceptance assertion was relaxed.

## Validation

- Desktop app and focused test builds passed. The app build reported one Qt
  generated-code integer-conversion warning in ExportView, outside this change.
- Five focused CTests passed: popup inventory, audit regressions, runtime style
  policy, existing popup behavior, and the real Import/preflight fixture.
- Both source style tests passed through `test-ci-headless` after configuring
  `ci-headless`.
- The full native Cocoa Import proof passed with the new style audit active.
  Storage and path-match comparison captures were visually inspected.
- `git diff --check` passed.

Logs, source/binary hashes, native captures, and deterministic primitive frames
are in `docs/evidence/ui-style-enforcement-2026-10-23`. Evidence is scoped to the
shared dirty workspace; unrelated Timeline and Edit changes were preserved.
Remote CI execution, Windows execution, all workspace compositions, and general
release readiness are not established by these local checks.
