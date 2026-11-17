# Readable text, shared choices, and enlarged layouts

This pass extends the [application style contract](ui-style-contract.md) to body
text and choice controls, then follows those changes through the actual layouts.
Interface density remains independent of text scale.

## Visible changes

- Agent, recovery, and Copy Edits use shared scalable body typography instead of
  8–9 px labels or shrinking text to fit. Agent plan details scroll separately
  from the action footer; questions show one at a time at enlarged scale.
- Checkboxes and switches share neutral indicators, fixed-size wrapping labels,
  descriptions, focus, disabled behavior, and accessibility activation.
  Preferences no longer draws a private switch. Timeline interchange uses the
  same checkbox. Mixed checkboxes show a minus.
- Copy Edits scrolls its option groups while keeping Copy and Cancel visible.
  Keyboard focus brings each group into the viewport.
- Import adapts to short viewports and 200% scaling: source selection combines
  into one split control, secondary selection actions stay in Review, browsing
  tools reflow, and the full folder name wraps. A Folder details dialog exposes
  and copies the exact full path, closes with Escape, and returns focus.
- Offline recovery wraps long information and reflows its actions. Narrow
  Library filter labels retain their normal size instead of clipping.
- Preferences navigation backgrounds explicitly stack behind their labels.
  The shared navigation row now uses Qt's standard Button content/background
  structure. Runtime auditing rejects the covering-background regression.
- A deferred first-run guide request no longer resets a walkthrough already in
  progress. Recovery still takes precedence over opening the guide.

## Enforcement and evidence

Artifacts and logs are in [the evidence directory](evidence/readability-2026-10-23).

- The source policy scans 292 production QML files and rejects stock choices,
  private indicators/labels, and unscaled typography in the migrated body-text
  areas. All 23 source mutation regressions pass.
- The readability matrix passes 36 density, scale, contrast, and logical-width
  combinations. It checks literal long paths, descriptions, mixed state,
  pointer/keyboard/accessibility activation, disabled behavior, stable geometry,
  and deliberate tiny-text, shrinking-text, and tinted-indicator mutations.
- The integrated 16-test suite passes. Follow-up tests cover the final Agent
  window readback, Preferences stacking policy, Copy Edits, and source contracts.
  Both Agent routes pass at default and 200% scale.
- Native Cocoa/threaded proofs cover enlarged Agent layouts and motion, Copy
  Edits, Import browsing/preflight/completed receipts, and Preferences/shortcuts
  at 100% and 200%. Rendered captures were reviewed; earlier failures are kept
  under diagnostics rather than presented as current passing frames.

The final broad accessibility route captured and exercised Preferences, then
failed a later Library workspace shortcut. An earlier integrated native route
passed before the final navigation-owner change. The expanded native gallery
reached all density/section captures but failed to expose its dropdown's first
option. These broader routes remain unqualified with the final tree; their logs
are retained. They are not included in the passing focused-test count. Native
gallery input uses QPA events and checks uninterrupted clicks separately from
pressed-state frame readback.

The full application builds. Three narrow QVariant conversions in a concurrent
Timeline performance probe were corrected to unblock that build; this is not
qualification of the probe or the concurrent Timeline work. Unrelated dirty-tree
changes were retained.

This evidence qualifies the exercised UI behavior and concrete style rules. It
does not establish all application text has migrated, arbitrary window sizes,
Windows behavior, live-model creative quality, or release readiness.
