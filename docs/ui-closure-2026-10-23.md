# Remaining UI/style issues closed

This follows [command consistency](command-style-2026-10-23.md). The two Agent
test failures, shader animation lifetime issue, and remaining Agent chrome
exceptions were addressed without restoring a character to the editor.

## Changes

- Agent view now uses `PopupSurface` for its outer frame. The blue bezel,
  duplicate inner outline, phosphor stripes, and nested working-card border are
  removed. Status content aligns at the top. Input fields, choices, prompts,
  revisions, and selectable failure details use the shared Tone controls.
  Integer panel dimensions and a small shared inset prevent layout rounding
  from clipping the bottom action row at smaller sizes.
- The character remains in Agent view. Its gaze follows actual activity when
  presentation finishes; the periodic timer no longer replaces that direction
  with an unrelated side-to-side movement. Editor status stays optical.
- Shader motion requires an attached, visible window and nonzero item size.
  Uniform animation jobs explicitly stop during destruction. The old lifecycle
  allowed an effect to animate before attachment and after detachment. The new
  regression fails against that behavior and exercises all three effect owners:
  `OpticalLightField`, `LooksyAgentCharacter`, and `CrtSignalSurface`.
- The original crash stack ends in Qt's shader uniform update. Qt 6.10's
  [implementation](https://raw.githubusercontent.com/qt/qtdeclarative/6.10/src/quick/items/qquickshadereffect.cpp)
  accesses the effect's window during that update. The attachment guard removes
  the unsafe lifetime interval in application code; Qt itself was not patched.
- Agent tests inspect rendered activity instead of a removed `sweep` property.
  Obsolete climbing/contact proofs were replaced with Agent-only placement,
  optical status, reduced motion, resize, Close/Stop, failure details and focus,
  approval, retry, revert, result, and revision checks. The evaluated shared
  style audit now runs on their real QML trees and requires the shared frame.
- The production-event test retains real `AgentEditController` execution,
  accepted-recipe provenance, inspector value/selection/focus noninterference,
  draft preservation, and rollback. It uses a deterministic rulebook model and
  an automatic isolated fixture instead of silently skipping without an
  evidence-directory environment variable. Native resize requests fit the
  display's available area and frame margins.
- The source gate rejects characters outside Agent view and stock text input
  controls outside their shared definitions. There are 21 audit regression cases.
- Shared action width comes directly from text/icon metrics, avoiding stale row
  layout measurements when labels change. The command matrix changes a compact
  menu label from "More" to "More export options" and requires it to resize and
  fit. The gallery's Pin toggle keeps a stable label and checked state.
- The native gallery harness activates the requested gallery window before
  checking Escape. Activating the application alone could select the editor
  window. The active-window, shortcut, and focus assertions remain intact.

## Evidence

Logs, screenshots, animation measurements, and source/binary hashes are in
`docs/evidence/ui-closure-2026-10-23`.

- Application and affected test targets build successfully.
- The Agent popup and complete session tests pass after the chrome changes.
  Both also pass with native Cocoa/threaded rendering. Prompt and working-state
  captures were visually inspected. The native session exercises plan review,
  explicit approval, execution, editor status, Stop, Retry, Revert, results,
  revision, and failure recovery at supported sizes.
- All 17 focused tests pass in the final integrated run: source policy, popup
  behavior, the three style matrices, shader lifecycle, loading scenes, Agent
  popup/session/production receipts, icons, button/shared accessibility, and
  text field/area/dropdown behavior. The production-event test also passes its
  follow-up virtual/native size check.
- The production-event CTest runs and passes without a caller fixture variable.
  Its deterministic controller receipts are distinguished from live AI output.
- Three consecutive native gallery passes complete with the final button
  measurement and explicit window activation. Each covers all three densities,
  rendered interaction states, popup keyboard behavior, and the actual Import
  selected/review route. The native Edit/color-controls proof also passes.
- The native production-event proof passes at display-fitted sizes, preserving
  inspector values, selection and focus, the accepted draft, and rollback.
- Shader lifecycle proof passes 960 transitions on both offscreen and native
  Cocoa/threaded renderers, including window attachment/detachment, hidden
  windows, resize to zero, reduced motion, and destruction of active effects.
- All 17 animated surfaces render through a deliberate 900 ms GUI stall:
  107–110 frames each, maximum observed gap 16.71 ms. Reduced motion produces
  zero frames during the following 250 ms observation intervals.
- The headless preset's two source checks pass. The production audit covers 290
  QML files and reports no violations, including 331 shared input declarations.
- `git diff --check` passes. Unrelated concurrent workspace changes are retained.

The evidence is scoped to these UI contracts and exercised desktop workflows.
It does not establish autonomous live-model creative quality, a complete real
shoot, arbitrary camera-media coverage, Windows qualification, or release
readiness. Earlier failed logs remain as diagnostics rather than being counted
as current passing evidence.
