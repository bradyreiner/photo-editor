# Color and app engine audit — 2026-10-28

The actionable backlog and acceptance criteria are in the
[app improvement plan](app-improvement-plan.md).

## Decision

Looksy has a substantial imaging engine and a broad creative application around it.
Its strongest asset is the scene-linear photo/render foundation. Its largest risk
is consistency between consumers, backends, coordinate systems, and document states.
It is a credible development platform; the evidence does not establish a fully
qualified professional photo/video/3D application or calibrated physical simulator.

This is a current source audit plus previously collected verification. It is not
an exhaustive new run of every UI control or every combination of tools. Product
code was not changed for this audit. A small standalone engine probe was added to
the ignored build evidence directory to investigate HDR retouch behavior.

## Evidence and its limits

- The latest shared brush/color repair has **40 passing focused tests**, **four
  passing native Metal/ICC checks**, and **three passing real Canon RAW fixtures**.
  See [the verification manifest](../build/brush-parity-fixes-2026-10-28/verification.json).
- Those checks cover source coordinates, transformed/cropped paint and erasing,
  all seven retouch modes, native source-guided alpha, reduced/native output,
  persisted local grades, and real asset-backed reopening. They do not cover every
  effect at every resolution or all app workspaces together.
- The broader controller suite remains unqualified: the 12-second Debug undo
  stress deadline varied between runs; a later run passed that case and the
  100-command stack, then failed three built-in preset image hashes.
- Preset diagnostics with matching Final quality give exact agreement with the
  direct shared engine and at most one 8-bit code against the canonical route.
  The old golden-hash drift remains unattributed; the baselines were not reset.
- Checked-in creative qualification records still report **0/9 fully qualified
  families** and **0/216 human review cells**. These are missing qualification
  receipts, not a claim that none of the features works. Older narrative status
  documents must not override current source or newer evidence.

## What the engines actually are

| Layer | Responsibility | Important boundary |
| --- | --- | --- |
| Qt/QML desktop | Controls, pointers, tool state, viewports, inspectors, workspace navigation | A visible control does not prove the rendered/exported behavior. |
| C++ photo pipeline | Color, masks, retouch, layers, effects, geometry, scopes and output | Canonical recipe/plan ownership is the intended common contract; compatibility and specialized routes remain. |
| Metal backend | Selected color/detail/geometry operations, retained textures, output LUTs and device ICC publication | GPU availability does not mean a complete edit stays on the GPU. |
| Rust services/FFI | Catalog, jobs, metadata, telemetry and native integration | Storage/job success must also survive app state, recovery and delivery transitions. |
| Timeline/media runtime | Media frames, editing, animation, audio, shared photo grading and FFmpeg delivery | A sampled color LUT cannot represent spatial masks or image-dependent operations. |
| Gaussian Scene renderer | Dedicated Metal scene/splat rendering, specialized optical/ray and lighting paths | This is separate from the 2D photo-filter models. Its existence does not make photo bokeh depth-aware. |
| Model/agent providers | Segmentation, inpaint/model operations and structured editing actions | Model availability, semantic quality, permission enforcement and deterministic rendering are separate obligations. |

The canonical photo stage order is:

`Decode → NormalizeRaw → CameraCharacterization → SceneAdjustments → Tone → Curves → Look → Layers → Detail → Geometry → PhotographicOutput → Proof → DeviceTransform → Publish`

Layers contain their own local adjustments, masks, effects and retouch operations;
this diagram is the outer stage graph, not an assertion that every operation is
one flat pass. Proof/device stages depend on the requested destination.

## The good

1. **A real color-management foundation.** Float working buffers, scene-linear
   operations, camera characterization, explicit working gamuts, ACES 2/OCIO,
   ICC output/proofing, and CPU/native reference work exist in code. Signed/HDR
   RGB and alpha have meaningful regression coverage.
2. **Useful execution structure.** Recipes, executable stage plans, source/profile
   identities, caches, cancellation, retained surfaces and explicit render purposes
   are substantive infrastructure. The architecture can express the correct order.
3. **Editable creative content.** Masks, paint tiles, layer effects, text parameters
   and immutable assets can survive history and save/reopen. Seeded procedural
   effects can reproduce their appearance rather than randomly changing each frame.
4. **Thoughtful effect contracts.** Operators declare support, document coordinates,
   whole-frame requirements, resource limits and cancellation. Spatial sampling
   uses premultiplied alpha where required. Displacement maps are treated as data,
   not accidentally color-converted photographs.
5. **Real native acceleration.** Metal kernels cover exposure, tone, curves, looks,
   LUTs, geometry, detail/NR and publication. CPU-composited layers now have an
   unchanged-float upload route into native ICC output.
6. **Good verification principles.** The capability and release documents distinguish
   math, parity, state transitions, UI behavior, real hardware, photographic review
   and performance. The remaining problem is completing and maintaining the proof.

## The bad

1. **The common recipe has several consumers with different obligations.** Draft,
   settled, scope, thumbnail, export, video and scene paths still need individual
   admission and publication logic. This is where profile, crop, alpha, resolution
   and cache mistakes propagate into visible disagreement.
2. **Acceleration is hybrid.** Photo layers, local masks, retouch and exact RAW
   characterization can reject the reduced/fused GPU shortcut. A GPU output stage
   after CPU composition is useful, but is not an end-to-end GPU pipeline.
3. **Quality settings can change algorithms.** Detail uses different Interactive
   and Final processing. A fast draft is approximate; settled quality must use the
   destination's intended algorithm and sample basis before being called parity.
4. **Some conversion paths use half-float intermediates.** `ColorPixel` buffers are
   float, but the ordinary QImage-to-working helper requests RGBA16F. Precision
   therefore needs qualification across the entire path, not just the main buffer.
5. **Large-image costs are substantial.** One 45 MP float-RGBA buffer is about
   720 MB / 687 MiB. The effect API defaults to 512 MiB of scratch. Whole-frame
   operations with full-size temporaries can exceed that allowance; a 4K test
   does not establish 45 MP throughput, memory use or cancellation latency.
6. **Photo/video sharing has real limits.** Timeline grading uses the photo engine
   for supported operations, including sampled LUT routes. Spatial and
   image-dependent grades require a frame-graph route and have explicit delivery
   restrictions. Similar controls in two workspaces do not imply identical support.
7. **Feature breadth exceeds integrated qualification.** Import, culling, batch,
   refine, export and recovery must work as one transactionally reliable product.
   Focused render tests do not establish that complete workflow.

## The ugly: concrete findings and structural risks

### P1 — HDR/signed retouch clipping reproduced in the current engine

`separated_heal_pixel()` clamps reconstructed RGB to `[0,1]` before photographic
output. A standalone probe calls the production `apply_local_removal()` with a
constant 9×9 float field, center-only coverage, Final quality and partial alpha:

| Texture/color blend | Input center RGBA | Output center RGBA |
| --- | --- | --- |
| 1 / 1 | `(2.5, -0.2, 0.4, 0.6)` | `(2.5, -0.18055, 0.4, 0.6)` |
| 0.5 / 0.5 | `(2.5, -0.2, 0.4, 0.6)` | `(1, 0, 0.4, 0.6)` |

The second case confirms highlight and negative-channel loss in the separated
healing branch; alpha and the unmasked exterior remain unchanged. The first case
also records signed-channel drift, whose cause was not separately diagnosed.
This is an engine-level probe, not a full UI reproduction. The recent retouch
placement/parity fixtures did not cover this HDR texture/color-blend case.

Evidence: [probe source](../build/engine-audit-2026-10-28/retouch-hdr-probe.cpp),
[probe output](../build/engine-audit-2026-10-28/retouch-hdr-probe.log),
[production helper](../engine/src/pipeline/color_mask_runtime.cpp).
No fix was made during this audit.

### Integration bugs can silently change plausible-looking pixels

The recent pink highlights, alpha resampling, source-coordinate placement,
thumbnail/profile routing and CPU-to-device publication defects were boundary
failures. Each individual subsystem could look reasonable while the combined
result was wrong. These repairs improve the checked paths; they do not prove the
remaining combination space automatically.

### Controllers concentrate too many responsibilities

At inspection, the photo controller was approximately 49,000 lines and the
Timeline controller approximately 57,000 lines. Size alone does not prove a bug,
but the combination of rendering, persistence, tool state, scheduling, publication
and UI adaptation makes dependencies difficult to review and parallel changes
easy to collide. The continuing compatibility routes increase that burden.

### Release confidence is still incomplete

The color completion strategy retains external corpus/reference and human-review
gates. Creative qualification records retain uncompleted photographic reviews.
The broader controller suite is not green. It would be inaccurate to describe
the app as scientifically calibrated, fully cross-platform qualified, uniformly
realtime, or release-qualified from the current evidence.

## Capability and cross-function map

“Implemented” below describes code and workflow surfaces, not universal certification.

| Family | Functionality | Most important interaction/risk |
| --- | --- | --- |
| Library/catalog | Import, metadata, search, albums/collections, rating/culling and media references | Source identity, offline/relink, thumbnail freshness, large-library responsiveness and crash/relaunch recovery. |
| RAW/develop | Decode, normalization, camera/WB/profile characterization and scene exposure | Apply input transforms once; retain channel ratios/headroom; use actual RAW provenance and source geometry. |
| Global color | Exposure, tone, levels/zones, curves, color mix/grading, detail and NR | Operation order, working gamut, Interactive/Final quality and destination transform. |
| Geometry | Crop, rotation, straighten, scale/skew/perspective, lens corrections and liquify | Pointer inverse mapping, pixel centers, parent/layer transforms and source samples outside a crop. |
| Selection/masks | Brush, geometric/range masks, semantic providers, add/subtract/intersect/invert and refinement | Mask alpha is data; color-dependent selections need the right working pixels; authoring and evaluation bases must agree. |
| Paint/erase | Pixel tiles, opacity/flow/pressure, textured materials, light paint and photo fragments | Premultiplied filtering, selection clipping, native authoring dimensions, HDR and erasing a transformed Base Image. |
| Retouch | Clone, heal, remove, red-eye, skin cleanup, frequency smoothing and patch | Full-source neighborhood context, source picks, versioned history and the HDR clipping finding above. |
| Layers | Pixel/source/adjustment/fill/text/group layers, masks, blend/opacity and ordering | Above/below adjustments, nested transforms, local grades, hidden/locked state and backdrop-dependent materials. |
| Filters | 35 registered CPU operators, editable effect stacks, amount/mask and persisted parameters | Color domain, support/padding, effect order, whole-frame admission and resource/cancellation behavior. |
| Typography | Editable fonts/layout, variable axes, contour text and procedural materials | Font availability, raster scale, glyph alpha, lower-stack sampling, parent transforms and reopen/export. |
| Presets/LUT/batch | Built-in/user looks, LUT stacks, copy/paste and selective sync | Declared groups, legacy/structured recipe equivalence, interpolation space, reset behavior and unresolved preset goldens. |
| Scopes/proof | Histogram/waveform/parade/vectorscope, clipping/gamut/false-color and proofing | Sampling the intended stage and revision; avoiding stale or double-transformed display pixels. |
| Export | Flattening, resize, profiles/bit depth, metadata, presets and delivery jobs | Same edit intent, final sampling/quality, alpha/profile embedding, collision/retry/cancel and recoverability. |
| Timeline/audio | Clips, animation, grading/effects, audio controls, proxies/cache and encoded delivery | Shared-photo versus media-filter semantics, temporal evaluation, supported effect combinations and playback/export agreement. |
| Carousel/layout | Multi-image canvases, type/fill/layout and delivery | Explicit source recipes, linked assets, compositing, typography and export dimensions. |
| Scene | Gaussian/splat scene rendering, authored lighting/optics and film-plane processing | Independent camera/material semantics, progressive snapshots, GPU memory and beauty-render latency. |
| Agent/AI | Structured tool actions, local/model providers, semantic selection and generated patches | Provider availability, correct target IDs, approved capabilities, history/persistence and independent quality assessment. |

## How effects, shaders and filters are simulated

A shader is an execution program, not an accuracy guarantee. Metal can execute
the same approximation faster; a CPU operator can implement rigorous math.
Physical accuracy depends on the model, input data, calibration and validation.
Many of the 35 photo operators are CPU implementations even though the app also
has substantial Metal code.

| Operators or material | What is actually computed | Boundary |
| --- | --- | --- |
| Box/Gaussian blur | Uniform or normalized separable kernels | Generic convolution, not measured lens defocus. |
| Motion/Dragged Shutter | Directional sampling paths; dragged shutter adds designed falloff, highlight weighting and optional protection | No recovered scene motion, shutter history or moving occlusion. |
| Optical Diffusion/Halation | Highlight extraction, Gaussian scattering and warm radiance addition | No measured filter/film-layer response or stock-specific emulsion physics. |
| Anamorphic streaks/Starburst | Directional line-spread kernels or symmetric ray-axis averages | No named-lens calibration or aperture-derived diffraction PSF. |
| Lens Bokeh | Premultiplied polygon-aperture integration, optionally with boosted highlights | No depth ordering, occlusion, cat-eye clipping or lens-specific aberration. |
| Tilt Shift | Blur blended outside a feathered 2D focus band | No recovered depth or Scheimpflug camera solution. |
| Zoom/Spin blur | Radial/angular bilinear path integration | Whole-frame sampling; independent padded tiles are insufficient. |
| Surface blur/Unsharp/High Pass | Bilateral smoothing or low/high-frequency separation | Neighborhood cost; linear 0.5 high-pass output is not display-encoded Overlay-neutral gray. |
| Film Grain/Light Leak/Vignette | Seeded noise, Gaussian tinted wash or parametric exposure falloff | Authored appearance, not measured grain, internal reflections or lens shading. |
| Split Tone/Duotone/Soft Fade | Tone-weighted tint/interpolation and lift/contrast/saturation math | Designed color grades, not chemical or pigment processes. |
| Linear Posterize/Invert/Gradient Map/Solarization | Quantization, `1-c`, luminance-driven stops or threshold reflection | Linear-light behavior can differ from familiar display-encoded filters; some deliberately bound the tonal domain. |
| Imperfect Print/Color Halftone/Dither | Seeded RGB plate/paper effects, rotated dot screens, Bayer thresholds or Floyd–Steinberg error diffusion | No calibrated CMYK press/paper/ink model; error diffusion is causal and whole-frame. |
| Crosshatch/Ink Outline | Document-anchored line screens or Sobel edge darkening | Graphic stylization, without semantic drawing or ink transport. |
| Watercolor/Oil Paint | Edge-preserving smoothing plus pigment-like noise, or local luminance-bin majority color | No water/pigment fluid model, bristles, paint deposition or spectral pigment mixing. |
| Pixel Mosaic | Square/hexagonal/jittered Voronoi cells with representative point sampling | Cell color is not an area average; no semantic tessellation. |
| RGB Glitch/Displacement | Seeded band/channel offsets or data-map-driven bilinear warping | Deterministic image-space distortion, not a real transmission fault or reconstructed geometry. |
| Metal text | Glyph-derived bevel normals, RGB conductor references, GGX/Smith/Schlick response and authored studio lights | Principled 2.5D shading, without measured BRDFs, scene reflections, spectral optics or actual text extrusion. |
| Glass/plastic/emboss text | Dielectric Fresnel, lower-stack sampling/blur, screen-space offsets and inferred bevel response | No thickness-aware ray-traced text volume or photograph-derived environment. |

## What should happen next

1. Fix and regress the reproduced HDR/signed healing loss before claiming that
   local retouch preserves scene radiance across its controls.
2. Close cross-consumer correctness with a generated matrix of profile, alpha,
   resolution, crop/transform, mask/layer/effect, backend, history and reopen cases.
   Use named high-risk combinations in addition to individual feature tests.
3. Diagnose the unexplained preset baselines and qualify stress timing on the
   required Release hardware; retain failures rather than silently refreshing hashes.
4. Make execution domains and operator capabilities explicit: native/CPU support,
   scene/display/data interpretation, HDR behavior, tile/full-frame support and
   destination profile. Preserve historical recipes through versioned migration.
5. Split controller responsibilities incrementally around immutable document state,
   render scheduling/publication, persistence and tool sessions. Avoid a wholesale
   rewrite of working features.
6. Qualify one complete real-shoot workflow and representative large documents
   before expanding the feature catalog or making physical-realism claims.

## Source owners consulted

- [Canonical recipe/stages](../engine/include/photo_editor/pipeline/recipe_compiler.h)
- [Desktop compiler](../apps/desktop/src/desktop_pipeline_state_compiler.cpp)
- [Working-image conversion](../apps/desktop/src/working_image.h)
- [Render/backend routing](../apps/desktop/src/photo_render_utils.cpp)
- [Effect registry](../engine/include/photo_editor/pipeline/photo_effect_registry.h)
- [Effect execution/budgets](../engine/include/photo_editor/pipeline/photo_effects.h)
- [Optical models](filter-models-optical.md), [artistic models](filter-models-artistic.md), [numeric-reference boundaries](filter-reference-accuracy.md)
- [Text materials](text-material-emulation-model.md)
- [Timeline grade runtime](../apps/desktop/src/timeline_video_grade_runtime.cpp)
- [Scene renderer](../apps/desktop/src/gaussian_scene_renderer_metal.mm)
- [Agent capability derivation](../apps/desktop/src/agent_task_permissions.h)
- [Capability acceptance](capability-verification-plan.md), [creative qualification](creative-tools-plan.md), [color completion authority](color-engine-and-app-foundations-completion-strategy.md)
