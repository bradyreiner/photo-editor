# Color Tools Remediation Checklist

> Supporting remediation record. Completed checks here are component evidence, not overall product
> completion. The current execution and release authority is
> `docs/color-engine-and-app-foundations-completion-strategy.md`.

This checklist tracks the work needed to make Framehouse's adjustment tools reliable, understandable, and defensible. Each item includes the current problem, the target behavior, and the implementation path.

## Tone And Levels

- [x] **Split toning hue scaling**
  - Problem: The UI stores grading hue as a normalized wheel value (`0..1`), but the engine treated it as signed percent. Most hue choices collapsed into a tiny range.
  - Goal: Hue wheels map the full 0-360 degree range consistently on CPU and Metal.
  - Approach: Interpret grading hue with `wrap_unit(hue) * 2pi`, mirror CPU/Metal, and add a regression test that proves blue/cyan/yellow grade hues land in different directions.

- [x] **Shadows and highlights quality**
  - Problem: Strong recovery can look crunchy or synthetic, especially on RAW-like dark ranges and clipped highlights.
  - Goal: Shadows reveal detail without gray mush; highlights recover with smooth rolloff and preserved color.
  - Approach: Replace broad tone-anchor pushes with luminance-masked recovery, protect midtone contrast, add clipped-highlight and dark-ramp visual tests.
  - Status: Core tone math has been retuned to use identity-preserving seven-point tone anchors, protected highlight chroma, and exact per-pixel preview/export rendering for high-risk tone recipes. `render_golden_test` includes deterministic night-recovery and clipped-highlight rollback fixtures with locked render hashes plus luminance-region behavior checks. `synthetic_raw_pipeline_invariant_test` covers constructed camera/profile RAW transform behavior without external image files; it is explicitly not real-corpus evidence. No licensed real RAW/night/high-contrast fixture corpus is currently tracked in the repo; real-image approval is an external QA pass until that corpus exists.

- [x] **Contrast quality**
  - Problem: Global contrast changes too much of the curve and can crush blacks or make highlights brittle.
  - Goal: Contrast should add midtone punch while preserving endpoints.
  - Approach: Rework contrast as a perceptual S-curve around middle gray with toe/shoulder protection and CPU/GPU parity tests.
  - Status: Contrast now uses a midtone-weighted photographic tone curve with protected toe/shoulder anchors, and preview/export parity covers the high-risk global tone path.

- [x] **Whites and blacks semantics**
  - Problem: Whites/Blacks are mixed into the same tone anchors as shadows/highlights and can feel weak or unclear.
  - Goal: Whites and Blacks should behave like endpoint placement controls.
  - Approach: Move endpoint behavior into a dedicated luminance stage, keep color ratios stable, and add tests proving visible endpoint movement.
  - Status: Whites and blacks now drive endpoint-biased anchors separately from brightness and shadows/highlights. The math tests cover visible black/white movement and the preview/export test covers the exact render path.

- [x] **Levels control defaults and labeling**
  - Problem: Engine neutral white level is stored as `0`, while the UI reset/default was `100`; the labels also did not explain the input-level semantics.
  - Goal: Levels behave like standard input black, gamma, and input white controls.
  - Approach: Keep saved recipe compatibility, invert the UI white slider so `100` displays neutral input white, and label controls by input role.

## Presence And Detail

- [x] **Presence tools**
  - Problem: Texture, Clarity, and Dehaze are present but the visual effect is weak or mushy.
  - Goal: Texture affects fine detail, Clarity affects midtone local contrast, Dehaze changes haze/black depth without color damage.
  - Approach: Replace box-blur style signals with edge-aware/guided filters, separate scale ranges, and test synthetic detail/edge fixtures.
  - Status: Engine behavior is now edge-aware: Texture, Clarity, and Dehaze use guided luminance bases at separate radii, clarity/dehaze are gated by local activity, and positive dehaze separates low-contrast haze while adding controlled true-shadow depth. Tests cover fine texture, broad clarity, low-contrast haze separation, negative dehaze softening, hard-edge halo protection, and a locked detail-render golden. Representative real-photo goldens are optional future QA once a licensed fixture corpus exists.

- [x] **Advanced tone naming**
  - Problem: `HDR Local Tone`, `Filmic Shoulder`, `Filmic Toe`, and `Adaptive Contrast` are not user-facing concepts.
  - Goal: Advanced tone controls are understandable or hidden.
  - Approach: Rename to `Highlight Detail`, `Highlight Roll-off`, `Shadow Fade`, and `Local Contrast`, with tighter defaults and tooltips.

## Color Tools

- [x] **Color Mix simplification**
  - Problem: HSL/HSV/LCH/OKLab modes plus selective CMYK controls make the tool hard to reason about.
  - Goal: One clear color mixer with Hue, Saturation, and Luminance per swatch.
  - Approach: Default the normal UI to HSL, hide the model switch under an advanced affordance, and move CMYK selective color into a separate lab tool.
  - Status: Normal editing starts in HSL, Lab-only model switches are hidden behind the Lab toggle, complex color edits route to the exact CPU path, and Studio now shows an active-channel preview strip with swatch, model/channel label, current H/S/L deltas, and a selected-channel reset action. Shell tests cover channel labels/tooltips, the active-channel signature, and reset behavior.

- [x] **Channel Mixer product decision**
  - Problem: The 3x3 matrix is technically wired but not understandable for normal editing.
  - Goal: Hide it by default or redesign it as a true advanced matrix utility.
  - Approach: Move to an Advanced/Lab section, group by output row, add row resets, monochrome mode, and tests for identity/row changes.
  - Status: Channel Mixer is now treated as a Lab-only advanced matrix utility instead of a normal edit tool. Studio/EditView group the controls by output row, expose per-row reset controls, and provide Identity, Monochrome, and Swap Red/Blue presets. Controller tests cover row reset, full reset, and monochrome preset behavior; shell tests cover the Studio affordances; render parity covers the high-risk exact color path.

- [x] **Color Replace usability**
  - Problem: It has no eyedropper, no mask preview, and default strength is no-op, so users cannot target colors reliably.
  - Goal: Pick source color from image, choose target color, inspect affected pixels.
  - Approach: Add source/target swatches, eyedropper sampling, mask preview overlay, `Range` plus `Softness`, and preserve luminance/saturation toggles.
  - Status: No-op enable is fixed and `Softness` now controls color-replacement hue falloff end-to-end across recipe state, controller, Studio/EditView UI, CPU/Metal render paths, preview/export conversion, and tests. Studio now shows source/target swatches, a readable source-to-target summary, range/softness/strength state, hue-swap, source/target eyedroppers wired to viewport patch sampling, preserve-luminance/preserve-saturation toggles that avoid the old brightness and chroma jump, and a Mask preview overlay for affected pixels. Controller, recipe, shell, parity, and math tests cover the controls, render behavior, and mask preview refresh.

- [x] **Skin Tone redesign**
  - Problem: The standalone line-hue control is abstract and the visual panel is not actionable.
  - Goal: Skin protection should be automatic, inspectable, and mostly invisible until needed.
  - Approach: Fold into grading/color replace as `Protect Skin`, use sampled skin range or detected skin mask, and show mask preview.
  - Status: No-op enable is fixed by seeding a useful protection strength. `Protect Skin` is now exposed directly inside Color Replace with protected hue, hue range, protection strength, a skin eyedropper wired to viewport patch sampling, an `Auto Skin` detector that estimates a warm skin hue/range from the image, and the shared color-tool Mask preview for affected/protected pixels; the old standalone panel is renamed `Skin Protection Lab` and kept behind Lab-only visibility. Controller tests prove sampled and auto-detected warm skin hue enable protection, seed useful strength, exit sampling mode where applicable, and refresh the preview mask. Shell tests cover the Studio affordances.

- [x] **Harmony product decision**
  - Problem: The current tool shifts hues toward palette anchors without explaining the palette or affected pixels.
  - Goal: Either hide the tool or make it a creative palette workflow.
  - Approach: Hide until it has palette visualization, base hue picker, affected mask, and named creative looks.
  - Status: Harmony is now explicitly a `Harmony Lab` tool: it stays behind the Lab toggle, shows palette anchor swatches that mirror the engine's scheme anchors, exposes named Warm/Complement/Triad starter palettes, and labels strength as `Hue Pull`. Shell tests cover the Studio swatches and named palette action; math/parity tests continue to cover the hue-pull behavior and skin-protection interaction.

## Presets

- [x] **Basic Studio preset browser**
  - Problem: Studio exposed a hardcoded save action but no way to browse and apply the built-in Framehouse presets.
  - Goal: Built-in and user presets are reachable from the Adjust inspector.
  - Approach: Add a compact preset selector, pack/category metadata, intensity control, changed-control chips, Apply, and Save controls backed by the existing preset API.

- [x] **Preset library**
  - Problem: Built-in presets exist in controller code, but Studio still needs full management and preview workflows.
  - Goal: Framehouse built-in presets and user presets are browseable, previewable, adjustable, and manageable.
  - Approach: Build a preset browser backed by existing built-ins, add categories, thumbnails, intensity, save/delete/import/export, and recipe diff preview.
  - Status: Studio now exposes a searchable preset library with generated thumbnails, Framehouse/custom labels, adjustment-group chips, favorite/apply card actions, intensity, changed-control chips, save, rename, move-pack, delete for user presets, and import/export sync-bundle controls. Backend controller tests cover save/rename/delete/export/import/favorites/preview recipe behavior, and shell tests cover the Studio management affordances.

- [x] **Preset quality**
  - Problem: Presets cannot be trusted until the underlying tools and preview path are stable.
  - Goal: Presets should produce polished starting points for RAW, portrait, landscape, monochrome, and night images.
  - Approach: Retune built-ins after tone/color fixes, add golden renders, and support custom preset storage.
  - Status: Tone/color stability is better, Studio now has a full preset library, and `render_golden_test` locks representative Clean Portrait and Landscape Pop preset-style renders with behavior checks for skin hue stability and sky protection. `editor_preview_controller_test` renders the actual built-in `Clean Portrait`, `Landscape Pop`, and `Clean Black and White` registry presets through the output pipeline and locks deterministic image hashes. `synthetic_raw_pipeline_invariant_test` covers constructed RAW profile behavior but is not real-corpus proof. A real-image approval corpus can still improve editorial tuning, but the enforceable in-repo preset contracts now cover RAW-style transforms, portrait, landscape, monochrome, and night workflows.

## Validation Standard

- [x] **Adjustment behavior tests**
  - Problem: Many sliders can regress visually without failing tests.
  - Goal: Every visible tool has a measurable contract.
  - Approach: Add monotonic ramp tests, hue-preservation tests, visible-delta thresholds, CPU/GPU parity tests, and golden renders for representative recipes.
  - Status: Added/expanded focused coverage for split toning, saturation/desaturation, bright-color saturation visibility, tone endpoint behavior, color replace including softness falloff and affected-pixel preview refresh, skin protection sampling/auto-detect/mask preview, color mix, channel mixer presets/resets, B&W, harmony, presence/grading parity, GPU fallback correctness, RAW profile corpus behavior, direct built-in preset output hashes, and deterministic render goldens for night recovery, highlight rollback, portrait preset behavior, landscape preset behavior, and detail. Representative real-photo goldens remain useful future QA, but there is no tracked real-photo fixture corpus to enforce in CI today.

- [x] **Initial tool visibility rules**
  - Problem: Experimental and advanced controls appear equal to core editing controls.
  - Goal: The first editing surface presents fewer, better tools.
  - Approach: Keep Presets, Basic Tone, Tone Curve, Color, Presence, Detail, RAW, and Crop visible; move Channel Mixer, Harmony, Skin Tone, selective CMYK, and advanced color-model switches behind a Lab toggle.
