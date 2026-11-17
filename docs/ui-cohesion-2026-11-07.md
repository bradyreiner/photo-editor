# UI cohesion pass — November 7, 2026

This pass tightens the existing workspace layout and neutral design system.
It is a first implementation pass, not app-wide release qualification.

## Rules applied

- Keep frequent commands visible and close to their content.
- Give occasional actions one contextual overflow menu with an accessible name.
- Avoid duplicate visible controls for commands already available in that menu.
- Keep command labels short; put shortcuts in the menu's shortcut column.
- Reflow controls when space is limited instead of shrinking their text.
- Preserve document warnings and recovery information when reducing chrome.

## Changes

Shared secondary menus now use one compact ellipsis button. Library selection,
Import, Export destinations, Timeline render jobs and recovery, and media rows
use the same affordance. Explicit menu labels and primary split actions remain
available. Keyboard focus, tooltips, and contextual accessible names are retained.

Edit no longer dedicates a permanent inspector footer to Reset and Save Preset.
Both commands live beside the inspector tabs in Adjustment actions. Reset keeps
the previous text/selection session guards, clip/board/photo scope, and Undo.
The existing Layout, Layers, and Typography sections retain their hierarchy.

The canvas toolbar keeps a labelled Fit control, magnification, and zoom buttons.
Fill remains in View. The redundant Zoom heading and Fill toolbar button are
removed. View commands now resolve the live canvas through its registered handle,
fixing out-of-scope references in the previous menu implementation. Ruler and
guide shortcuts use the shared shortcut column.

Library places Import alongside collection navigation and retains its stable
search and view-control rows. Import's selection commands appear when there is
applicable media or a selection.
The Scene depth settings drawer uses the shared AppDrawer surface.

## Verification

Evidence and check logs are under `build/ui-cohesion-2026-11-07/`.
The existing command-style matrix checks compact overflow defaults, contextual
accessible naming, stable sizing, keyboard/accessibility activation, disabled
behavior, and labelled-menu resizing. The application layout proof also exercises
Save Preset, Reset/Undo, and Fill/Fit through the consolidated commands.

The desktop Debug application builds successfully. The command-style matrix,
inspector-style matrix, grouped options, menu bounds/lifecycle, popup source
inventory, style-audit regression, save-queue check, and full application layout
proof pass. The final layout run and its captures are recorded in
`build/ui-cohesion-2026-11-07/verified-layout.log` and `verified-layout/`.
The layout fixture explicitly restores Photos after canvas creation and keeps the
same settings-on-right Export assertion at standard and compact widths.
The broader shell
source-contract test stops at an existing masking-input-role expectation; this
pass does not change those masking input paths.

Rendered evidence uses isolated fixture documents. It does not establish complete
real-photo usability, release readiness, or native displayed-frame performance.

## Outstanding findings

The already-running release editor displayed “Edits not safely saved” with the
diagnostic `legacy save cannot overwrite an asset already migrated to canonical
V13`. This pass does not resolve or hide that catalog-save failure. The active
editor and its document were left open; the fixture save-queue check is not proof
that the user's pending edits have reached the catalog.

Further product review should cover complete Library → Edit → Export and
Board → Timeline workflows with real media, narrow and enlarged layouts,
empty/loading/error states, and pointer-only and keyboard-only use. Scene and
Timeline still need full workflow reviews beyond their shared menu changes.
The overlapping Presets and Export rail entries noted in this pass are addressed
by the November 11 continuation below.

## Continuation — November 11

Edit's rail now contains editing tools. Presets remains directly available in the
inspector; Export remains in workspace navigation and existing command routing.
Both commands remain in the tool catalog so command and shortcut callers keep
working. Adjust remains a direct way to return from a canvas tool.

Single-photo Export uses one ordinary primary action. Batch Export offers only
the current-photo alternative in its split menu. Destination changes stay beside
the destination field, removing the duplicated start and folder commands.
Preflight, blocked-state explanations, and export admission remain unchanged.

Scene uses the concise Project, Tools, Setup, and View labels. Its 3D Motion menu
item no longer claims that a point cloud is playing. Native inspection also found
that advanced samples, bokeh, and focus criteria exceeded the narrow inspector;
those controls now use labelled, bounded rows within the existing disclosure.
Sensor and recording-format fields can also fit their available width without
shrinking the type size. A long scene-unit command imposed a 262 px minimum on
the 248 px content area, expanding the other camera controls too. Its label is
now “Use scene units”; the explanation lives in its tooltip. Scale and focus
criteria use the shared numeric column; bloom and halation entries can shrink
within their steppers. The four main camera values stack in a narrow inspector
and retain the two-column layout when there is room, keeping full numbers legible.

The earlier masking source-check failure concerns wheel-event delegation into
StudioImageViewport. The check now verifies the input component's delegation,
the viewport handler, and pointer-anchored zoom in the owning component. Selection
input, brush, lasso, polygon, and grab checks remain in place.

A controller recovery regression now opens a canonical photo while an older
legacy edit is pending. It requires the canonical recipe to stay active and the
exact older edit to survive in the recovery journal without an automatic retry.
This does not reconcile the user's historical recovery data.

Current check logs and captures are under `build/ui-cohesion-2026-11-11/`.
The November 7 results above describe that earlier build and proof run.

The November 11 application-layout proof passes with 46 captured states.
Single/batch Export interaction and preflight checks pass, including real-photo
review at three widths. Save status, queue, journal, and controller recovery
checks pass. Native focused Scene input verifies focus cancellation, exact entry,
fine steppers, and access to the advanced optics controls.

The full native Scene workflow also passes, with 20 captures in
`scene-inspector-proof/` and a receipt in `scene-inspector-proof.log`. It covers
physical sensor and scale entry, focus and aperture, grain/halation, saved-shot
recall, inspector resizing, compact layout, and workspace recreation. The native
window was exposed but inactive; this establishes layout and behavior, not
foreground input latency. `desktop-accepted-build.log` records the final Debug
application build.

The broader source-contract check now reaches a separate mask-preview expectation
about the normal V-key cycle and explicit inspection modes. That check remains
failing; this continuation does not change the mask-preview modes.
