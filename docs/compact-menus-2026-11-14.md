# Compact viewer menus

The viewer, overlay chooser and scope settings now use compact shared icons for
frequent actions, with short tooltips and accessible names. Less familiar menu
commands and parameter choices retain short labels.

- View and Layout have separate icon openers. View omits duplicate Fit, overlay,
  comparison and panel commands already available in the workspace.
- Layout lists Default and Focus once, alongside panel toggles and custom layouts.
- The overlay chooser has seven distinct square icons. Settings appear only for
  clipping or peaking; false color uses a color strip with luminance tooltips.
- Scope settings use four selectors, a gain slider, compact channel/render
  toggles and dock icons. Freeze and held-reference comparison remain available
  in the scope header. The reference control toggles hold/clear in one place.
- Shared menu rows keep their action icon when selected and show a separate check.

Verification: six focused menu/icon/style tests passed (79.01 seconds), and the
real viewer workflow passed (18.18 seconds). Foreground Cocoa/Metal review covers
1280/900-wide menus, actual clipping threshold pixels, native scopes, freeze and
150% interface scaling. Quality and range selectors update the real controller.
This is focused UI evidence, not a full-suite or release qualification.

Logs, screenshots and source hashes are archived under
`build/compact-menus-2026-11-14/`. The existing editor session was preserved.
