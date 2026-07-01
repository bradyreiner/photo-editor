# Masking, Brush, Selection UX Rebuild Checklist

This checklist tracks the concrete failures reported from the current masking and brush workflow. The goal is to keep the existing editing functionality while making the UI usable: clear overlays, predictable tools, no hidden exits, no overflowing top chrome, responsive smart selection, and adjustment layers that match the user's mental model.

## Reported Failures

- Diagonal hatch lines cover the image during selection and brush work.
- Brush outline and live brush overlay are hard to see or disappear while painting.
- Overlay color is confusing and should default to one solid slightly opaque color.
- Selection tools overflow across the top toolbar and truncate into unreadable labels.
- Brush mode is hard to exit once active.
- Smart selection does not show an obvious hover/candidate preview before commit.
- Object selection can freeze the app instead of showing nonblocking progress.
- Mask creation should create/select an adjustment layer and apply color effects only to that layer.
- The UI exposes technical Stack/Source/Set language too prominently.
- White balance/tint and other adjustments appear to retain color after reset.
- Tool switching feels slow, especially selection/brush/lasso.
- Lasso and brush need a direct usability pass with visible feedback and reliable commit/cancel states.
- Canvas toolbar has low-value buttons in the primary mouse path.
- The right panel should remain focused on layers/color/adjustments while tools move to an expandable left rail.

## Priority 0: Immediate Canvas Readability

1. [x] Remove diagonal hatch lines from the image in mask/brush modes.
2. [x] Keep any non-image canvas background treatment outside the painted image rect only.
3. [x] Make Color Overlay mode a single solid color by default.
4. [x] Keep active brush matte visible while painting instead of hiding it during a stroke.
5. [x] Raise live brush overlay opacity enough to read on bright snow and dark clothing.
6. [x] Keep the brush cursor outline visible during brush mode even when the mouse hover state drops.
7. [x] Render brush cursor as a high-contrast ring with a simple inner hardness ring.
8. [x] Add smoke coverage rejecting the old diagonal hatch overlay over the image.
9. [x] Add smoke coverage requiring live brush paint to keep the matte/overlay visible.

## Priority 1: Tool Layout And Escape Routes

10. [x] Move selection, brush, lasso, linear, radial, ellipse, and rectangle entry points out of the overflowing top row.
11. [x] Add a left expandable tool rail for primary tools.
12. [x] Use icon-first buttons in the rail with short tooltips and optional expanded labels.
13. [x] Keep Add/Subtract/Intersect/Replace in a compact secondary strip instead of the primary tool row.
14. [x] Add an always-visible active tool chip with tool name, Add/Erase state, Done, and Cancel.
15. [x] Make `Esc` cancel or exit every active canvas tool consistently.
16. [x] Make `Enter`/Done commit active brush/lasso/polygon workflows where applicable.
17. [x] Remove undo/redo from the primary canvas toolbar; keep them as keyboard/menu commands.
18. [x] Keep zoom/fit/view controls grouped separately from mask creation tools.
19. [x] Add minimum-width tests proving no tool row truncates off-screen.

## Priority 2: Brush And Lasso Workflow

20. [x] Make Brush Add and Brush Erase one-click modes from the left rail.
21. [x] Show size, hardness, flow, density, and opacity in a compact brush panel.
22. [x] Make brush painting update a live overlay immediately without waiting for full preview render.
23. [x] Commit brush strokes asynchronously without blocking pointer feedback.
24. [x] Make lasso start, live path, close, commit, and cancel visually obvious.
25. [x] Show lasso fill/outline during drag before commit.
26. [x] Keep pan/zoom from stealing active brush/lasso pointer grabs.
27. [x] Add explicit tests for brush and lasso start/drag/release paths in Studio.

## Priority 3: Smart/Object Selection Responsiveness

28. [x] Show hover/candidate outline for smart/object selection before commit.
29. [x] Debounce smart hover previews so they remain responsive without flooding the renderer.
30. [x] Show a canvas loading animation/chip while object selection analyzes.
31. [x] Make smart selection requests cancelable and discard stale results.
32. [x] Prioritize visible overlay/candidate updates over background full-resolution renders.
33. [x] Prevent object selection from blocking UI input.
34. [x] Add observability and tests for queue wait limits during visible overlay work.

## Priority 4: Layers And Adjustment Model UX

35. [x] Make creating a selection automatically create or select an adjustment layer.
36. [x] Show `Base Image` as the original image layer.
37. [x] Show user-facing `Adjustment Layer` entries instead of requiring users to understand mask sets first.
38. [x] Apply color/exposure effects only to the active adjustment layer's mask by default.
39. [x] Move technical mask source/set details behind an advanced disclosure.
40. [x] Keep the right panel focused on Layers, active layer adjustments, mask properties, and color controls.
41. [x] Add layer-scoped reset controls that do not mutate the base/global recipe.
42. [x] Add tests proving layer-local WB/tint/exposure reset returns to exact neutral state.

## Priority 5: Color Reset And Recipe Correctness

43. [x] Reproduce the reported WB/tint reset residual-color issue with a regression test.
44. [x] Verify global adjustment reset returns every affected value to neutral.
45. [x] Verify layer adjustment reset returns only the active layer values to neutral.
46. [x] Verify adjustment order is deterministic and does not create hidden color drift.
47. [x] Add image-diff or pixel-sample coverage for neutral reset returning to the original visual state.

## Priority 6: Copy And Vocabulary Cleanup

48. [x] Replace visible `Stack` wording with user-facing layer/selection language in the main UI.
49. [x] Rename technical `Source` labels in primary UI to tool-specific labels where possible.
50. [x] Keep technical source/set metadata only in advanced or diagnostic surfaces.
51. [x] Shorten all visible masking tool labels to fit compact layouts.
52. [x] Add tests rejecting truncated primary masking labels at supported widths.

## Progress Evidence

- 2026-06-19: Removed the Studio masked-image diagonal hatch canvas, so selection and brush modes no longer draw diagonal lines across the photo.
- 2026-06-19: `SharedMaskOverlay` now keeps live brush feedback visible while painting through three paths: committed preview source, native live brush preview, and a vector fallback drawn directly from live brush points when the native target is unavailable.
- 2026-06-19: Brush live overlay opacity is no longer reduced during active painting, making the matte readable on bright and dark image regions.
- 2026-06-19: The brush HUD exit action is now labeled `Exit Brush` and returns to mask editing without dropping the active selection.
- 2026-06-19 validation: `git diff --check` passed, `cmake --build --preset build-dev-macos-debug` passed, and `ctest --test-dir build/dev-macos-debug -R "desktop_shell_smoke_test" --output-on-failure` passed.
- 2026-06-19: Added the Studio `maskToolRail` with the `left-rail|icon-first|expandable:v1` contract. Primary selection, generated selection, brush, lasso, and geometry tools now live in the left rail, while the top mask toolbar keeps only compact operation/status controls.
- 2026-06-19 validation: `git diff --check` passed, `cmake --build --preset build-dev-macos-debug` passed, and `ctest --test-dir build/dev-macos-debug -R "desktop_shell_smoke_test" --output-on-failure` passed after the rail change.
- 2026-06-19: Replaced main Studio masking copy from `Stack`/`Source` terminology to `Selections`, `selection group`, and `component` language across the primary strip, status text, refine actions, and tooltips.
- 2026-06-19 validation: `git diff --check` passed, `cmake --build --preset build-dev-macos-debug` passed, and `ctest --test-dir build/dev-macos-debug -R "desktop_shell_smoke_test" --output-on-failure` passed after the vocabulary cleanup.
