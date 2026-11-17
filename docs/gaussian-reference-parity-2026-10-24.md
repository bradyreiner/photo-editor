# Gaussian Scene: reference workflow and current boundary

The requested reference is Jörg Kahlhöfer's [Gaussian Splatting Full Pipeline From
Location Scan to Cinema 4D + Octane](https://www.youtube.com/watch?v=OzUxL_UDMTk).
Its description, chapters and transcript were inspected. It covers capture,
alignment, training, cleanup, scene scale, virtual cameras, inserted objects,
additional lights and relighting. **Full capability and visual-quality parity
with that workflow has not been achieved.**

This report retains chronological experiments and their failures. The capability
table summarizes the current state; later dated evidence supersedes earlier
implementation limits without turning those historical failures into passes.

## Implemented in this change

- Import an existing standard binary little-endian Gaussian PLY into a new
  independent project, without a trainer or capture cameras. An initial overview
  camera is generated. Degrees zero through three and at most two million
  Gaussians are supported; compressed splat formats and ordinary mesh PLYs are not.
- Remove a drawn rectangle, or retain only its contents. Selection uses projected
  splat centers at every depth, with the current focal framing and letterboxing.
  This is not surface-aware selection and does not trim individual Gaussian tails.
- Remove splats below an opacity threshold or above a chosen RMS radius.
  RMS radius is `sqrt(trace(covariance)/3)` in scene units, independent of rotation.
  These are manual cleanup tools, not an automatic geometry-quality repair.
- Undo/redo up to twenty edits during a session, restore all original splats,
  and reopen the saved visibility mask. History stores masks rather than scene
  copies. The current mask persists; the undo stack does not persist across launches.
- Export the retained original PLY rows exactly, including every SH coefficient,
  rotation, scale and extra float property. No covariance decomposition, color
  refitting or quantization is performed. Exports include an integrity receipt,
  original camera descriptions and the current camera, including measured scale
  when configured. A dense SH3 export now has a verified PlayCanvas import and
  camera/orbit/reset workflow; Cinema 4D/Octane import remains unverified.
- Freeze the active cleanup into still/film rendering and record it in output
  receipts. Background workers own immutable scene data. Failed saves do not
  replace the previous edit; exports refuse existing output paths.
- Add capture guidance and expose existing held-out training scores. No new
  quality preset or calibrated lens/film claim is introduced.

The original artifact is never edited. `cleanup.json` is an artifact-hash-bound
visibility sidecar saved atomically in the project. Corrupt, mismatched or
symlinked cleanup is rejected. Export uses a staged PLY and `.splat.json` receipt;
as with existing media publication, a process failure between the two final
renames can leave an orphan receipt. This is not a crash-atomic two-file transaction.

Two existing UI problems were corrected: scene-options content is now actually
inside its scrolling popup, and Scene uses the same unversioned self-module
import as the other workspaces, fixing an ambiguous-import failure in the bundle.

## Verification

The debug desktop bundle builds. Seven focused tests pass, including native
Metal rendering, film export, 4D workspace compatibility, controller import,
cleanup integrity/data preservation, viewport handoff and the real QML cleanup UI.
The UI test requires the macOS Cocoa/Metal platform; offscreen Qt cannot present
the required native texture.

```sh
cmake --build --preset build-dev-macos-debug --target photo_editor_app \
  gaussian_scene_cleanup_test gaussian_scene_cleanup_ui_test \
  gaussian_scene_controller_test gaussian_viewport_handoff_test \
  gaussian_scene_render_test gaussian_film_export_test \
  gaussian_spacetime_workspace_test -j 4

ctest --test-dir build/dev-macos-debug \
  -R '^gaussian_(scene_cleanup|scene_cleanup_ui|scene_controller|viewport_handoff|scene_render|film_export|spacetime_workspace)_test$' \
  --output-on-failure
```

A native SceneWorkspace proof copied the existing million-splat SUV scene into
`/private/tmp/looksy-gaussian-cleanup-real-v1/project`, then exercised an actual
reversed rectangle drag through the QML MouseArea. It retained 969,865 splats,
verified undo/redo, exported PLY and a photo with cleanup provenance, reopened the
project, applied a filter and restored all one million originals. Desktop and
1024×760 logical-size captures were inspected. Evidence:
`/private/tmp/looksy-gaussian-cleanup-real-v1/receipt.json`, `cleanup-marquee.png`,
`cleanup-after.png`, `cleanup-reopened.png` and `cleanup-compact.png`.

The rebuilt packaged app also loaded Main.qml's Scene workspace and rendered
that private real project: `state=ready`, `frameAvailable=true`, one million
splats, cleanup enabled, no saved-view error. Its broader managed-runtime proof
still failed because the separate local agent runtime timed out. This is a
confirmed Scene load/render result, not a passing whole-app proof. Receipt:
`/private/tmp/looksy-gaussian-integrated-v2/receipt.json`.

## What still separates this from the reference

| Reference capability | Current status | Required next work |
| --- | --- | --- |
| Capture, alignment and training | Local pipeline, optional sharper video-frame selection and shared-camera/strict photo matching; a full 255-photo production reconstruction passes all frozen photographic targets on 37 excluded reference views | Repeat photographic acceptance on more captures and coverage/optics conditions; the accepted Bonsai result is one dataset |
| Splat cleanup and interchange | Rectangle/freehand lasso with optional camera-depth range, opacity/size filtering, exact retained-row export and a dense SH3 PlayCanvas import proof | Brush and automatic visible-surface selection, component editing/transforms, Cinema 4D/Octane interoperability proof |
| Physical camera and depth of field | Explicit-scale thin lens, pupil response, optical vignetting, spherical distortion, astigmatism and field curvature | Matched photographic comparisons, measured-lens qualification and representative performance |
| Added objects and materials | Editable analytic primitives and project-local OBJ meshes, transforms and materials; probe or spatial diffuse/specular lighting, up to eight secondary surface hits and authored emission illuminating added surfaces plus opt-in captured colors with targeted emitter sampling | Matched photographic integration, better recovered surface properties and broader small-emitter convergence; additional maps, transmission and object animation are extensions, not demonstrated reference requirements |
| Added lights, shadows and reflections | Directional/point, rectangle/spherical emitters, finite sun size, ambient fill, geometric soft shadows, splat-cast shadows and independently selectable probe/spatial methods with bounded repeated reflections and indirect surface paths | Dense spatial diffuse and shadowed-volume responsiveness, matched photographic integration and sustained native interaction qualification |
| Reference relighting and haze | Captured appearance, approximate relighting and differential Receive shadows modes; mesh/primitive soft shadows, strength/brightness, distance haze and opt-in shadowed single scattering | Broader photographic volume integration/convergence and interaction qualification; covariance normals and baked illumination remain approximations |
| Camera post effects | Creative bloom, spectral glare with ray count/angle, cutoff, radial chromatic fringing and saturation; existing grain/halation | Matched artistic comparisons and representative performance; no spectral-renderer equivalence claimed |
| Camera tone | Exposure, creative looks, independent contrast and highlight compression | Matched artistic comparisons; recovery of clipped capture highlights and measured response profiles are not provided |
| Film response | Existing creative grain and halation | Calibrated film/sensor response is a qualification/extension beyond the demonstrated creative settings |
| Reference-level sharpness and realtime behavior | Full production Bonsai reconstruction reaches 30.02 dB / 0.9154 mean SSIM on 37 excluded views. The 960×639 combined 64-aperture/128-lighting-sample shot renders in 32.36 s with exact baseline pixels; native progressive-prefix evidence remains scenario-specific | Broader capture and integration quality, same-weight original-3DGS raster-convention audit, full-quality latency, and sustained foreground input-to-photon/resource measurements |

The existing real SUV source still has stretched and softened reconstruction
detail in the inspected native captures. Cleanup does not reconstruct that lost
information. The completed high-detail project's saved scores are 19.427057 dB /
0.56911325 SSIM at 3200-edge training; the baseline records 20.61022 dB /
0.6042293 at 1600-edge training. Different alignment/resolution domains prevent
calling this a controlled regression or improvement. More training, more splats,
larger exports and added film effects are not evidence of photographic parity.

## Objects and lighting continuation

Scene now has a **Scene composition** inspector. It supports up to sixteen
analytic spheres/ellipsoids or boxes and eight directional/point lights. The
floor template is a thin box initially aligned to the camera frame and placed
below the last object; this is not automatic scene-up or ground reconstruction.
Position, radii/half extents, rotation, sRGB material color, roughness, metallic
weight, emission, visibility, light color/intensity and ambient fill are editable.

`scene-objects.json` stores an artifact-bound composition with atomic replacement,
locking and stale-writer rejection. Fifty undo/redo states retain only the small
composition, without copying the splat arrays. Recalling a camera view leaves
the current composition intact. Still and film exports freeze the accepted
composition and record it in their receipts, including when the interactive
scene changes during film export. PLY continues to contain splats only.

The native Metal pass traces analytic intersections and direct GGX lighting,
with inverse-square point-light falloff and hard mutual object shadows. Each
aperture sample renders the objects with the same shifted ray camera as the
splats, and tiled output uses full-image ray coordinates. Focus picking includes
the nearest added solid. Object changes invalidate resolved images; film-only
adjustments can reuse the finished image.

The splat interaction is explicitly an approximation: solid silhouettes have
per-pixel intersection depths, compared against Gaussian **center depth**.
The default captured-appearance mode preserves the original lighting. The optional
captured-color mode receives added-object shadows (see below); splats do not cast
shadows onto other splats or added objects. Baked illumination remains. Optional scan reflections use a
256-pixel-per-face environment probe at the first visible object's center (transformed bounds center for meshes).
Reflection directions follow the camera and surface normal; roughness filters
the probe. A common probe approximates nearby parallax and excludes added
objects, so this is not ray-traced scene/object reflection or indirect lighting.
There are no secondary bounces or object animation yet. Finite-source lighting
and soft shadows are described in the continuation below.
OBJ geometry, base-color images and basic imported face materials are described below. Emission changes the emitting object's radiance; it does not
illuminate other objects. The existing display-referred splat compositing domain
is preserved. These are foundations for hybrid scene rendering, not Octane parity.

`gaussian_scene_objects_test` verifies codec/persistence and stale-writer checks,
front/back splat occlusion, point-light falloff, an independently arranged shadow,
focused optics, exact tile equality, solid focus picking, and object/film cache
invalidation. `gaussian_scene_objects_ui_test` exercises the actual inspector,
hide/undo, still export, camera paths, a concurrently edited film export,
reopening, floor creation, a point light and compact controls. The real-scene
run passed against a private copy of the million-splat project:
`/private/tmp/looksy-scene-objects-real-v2/receipt.json`. Its desktop and compact
native captures were inspected; originals were not edited.

An independent packaged Scene render proof can now run without requiring local
agent startup. It has its own explicitly scoped mode and does not change the
existing managed-runtime proof's requirements:

```sh
build/dev-macos-debug/apps/desktop/Looksy.app/Contents/MacOS/Looksy \
  --gaussian-scene-render-proof-dir NEW_ABSOLUTE_PROOF_DIRECTORY \
  --gaussian-workspace-proof --gaussian-project-proof PRIVATE_PROJECT_COPY
```

This mode isolates preferences/cache/catalog configuration, loads the real
Main.qml Scene workspace, opens the object inspector and records a native
framebuffer plus Scene/render state. It does not qualify local AI, catalog
availability, input latency or photographic fidelity.

The reflection probe is generated entirely on Metal from the pre-output splat
accumulation, converted to linear-light half-float faces and mipmapped there.
It therefore retains reconstructed over-white values rather than clipping them
to an 8-bit preview before reflection. This does not restore clipped input
highlights or calibrate physical radiance. Composition v2 adds the optional
reflection setting; unreflective v1 compositions retain their prior behavior.
The probe cache depends on the immutable splat scene, anchor position, capture
time and any active captured-lighting/haze composition. Camera-only movement reuses it. Tests cover all six directions, an
independently constructed off-axis mirror ray, over-white reflection, anchor
invalidation and exact tiled parity.

The packaged Main render proof passed at 1600×900 logical pixels with two
objects, two lights, an active/exposed window and visible inspector:
`/private/tmp/looksy-scene-objects-main-v1/receipt.json`. This checkpoint predates
the optional reflection probe; it proves the object/direct-light composition in
the actual shell, not reflection calibration or full reference parity.

The final reflection continuation passes all ten focused Gaussian tests, with
QML warnings treated as failures in the UI workflow. A real million-splat run
verified reflective material fields, probe rendering, undo, reopen and frozen
still/film receipts at
`/private/tmp/looksy-scene-reflections-real-v1/receipt.json`. Native captures
including `objects-reflections.png` and `objects-compact.png` were inspected.
The rebuilt packaged Main.qml render proof also passed with the reflective
composition at `/private/tmp/looksy-scene-reflections-main-v1/receipt.json`.
That window was exposed but not active, so it establishes native rendering and
control visibility, not foreground pointer/latency qualification.

The reference itself labels relighting as approximate. At
[58:10](https://www.youtube.com/watch?v=OzUxL_UDMTk&t=3490s), it switches the splat
to a power/color lighting mode; later it demonstrates receiving shadows and
acknowledges baked illumination and uneven geometry. It also demonstrates haze
near [1:02:02](https://www.youtube.com/watch?v=OzUxL_UDMTk&t=3722s). The captured-color lighting/shadow
controls and haze are implemented below. OBJ geometry import is implemented below; reconstructed-surface generation and imported materials remain separate work. Exact recovery of original lighting is a
separate photographic-fidelity requirement, not something that the reference
already achieves.


## Captured lighting and haze continuation

The Scene composition inspector adds **Captured scene** and **Haze** pages.
Captured appearance remains the default. **Light captured colors** treats the
capture's evaluated SH color as an approximate diffuse material and multiplies
its decoded linear color by ambient plus Lambert direct lighting. Power scales
this result; directional and inverse-square point lights share the existing
light controls. The covariance's shortest eigenvector estimates the surface
normal, oriented toward the camera. Nearly isotropic splats use a view-facing
fallback. Temporal scenes use their conditional covariance factors. Derived
normals are cached per immutable scene on the GPU; normals and illumination
require 32 bytes per splat when this mode is used.

Added spheres/ellipsoids and boxes cast hard shadow rays at Gaussian centers.
Shadow strength blends only the blocked direct light; ambient fill remains.
These are explicitly approximate received shadows. No original shadows are
removed, no normal field is recovered from images, and splats do not cast new
shadows. Irregular/floating geometry and baked highlights can look incorrect.
This corresponds to the reference's approximate captured-color lighting mode,
not calibrated material/light recovery.

Haze uses exponential Beer-Lambert transmittance in scene units. Each Gaussian
layer uses its center depth converted to ray distance; analytic objects use
actual ray-intersection distance. Background distance fills the remaining
transmittance. Haze color is sRGB input mixed in linear light before returning
to the trained splat compositing domain. Transparent multi-layer compositing
therefore remains an approximation; opaque surfaces follow the exponential
model. Density zero bypasses the effect exactly. Directional lights add a
Henyey-Greenstein angular scattering lobe, scaled by a fixed 0.05 creative
factor. Point-light volume integration, multiple scattering, occluder shadow
shafts and physical sky/atmosphere calibration are not implemented. Half-float
haze radiance is bounded at 65504. The scattering control is limited to +/-0.95
to avoid a singular phase function.

Both effects run per aperture ray before the existing linear aperture average,
exposure, film and output conversion. Full-canvas ray coordinates preserve tiled
output behavior. Reflection probes include captured lighting/haze and invalidate
on relevant composition changes, while still excluding inserted-object radiance.
Composition v3 persists the new settings; default v1/v2 descriptions and default
render behavior remain compatible. Still/film jobs freeze the full composition.

Verification for this continuation:

- All ten focused Gaussian tests passed in
  `/private/tmp/looksy-capture-lighting-final-ctest.log`. The final object test
  also passed after adding explicit temporal-normal and invalid-mode cases in
  `/private/tmp/looksy-capture-lighting-oracles-final.log`.
- The independent equations cover Lambert response, rotated covariance normals,
  power, back-facing light rejection, object-to-splat shadow strength, exponential
  transparent-layer fog, actual solid distance and forward-scattering orientation.
  Pinhole relighting/haze tiles are pixel-exact. Aperture tiles use the established
  one-8-bit-level tolerance: the initially strict comparison measured two pixels
  differing by one level, with no render errors. Density-zero/captured-mode
  bypasses preserve the baseline exactly.
- The native inspector workflow treats QML warnings as failures and verifies
  editable mode/power/shadow/density/distance/scattering/color, reopen, undo,
  frozen still/film export and compact layout. The million-splat private-copy run
  passed at `/private/tmp/looksy-scene-relight-real-v1/receipt.json`.
  `captured-lighting.png` and `scene-haze.png` were inspected visually. They show
  the effect and clear controls; stretched/soft source geometry remains visible.
- The rebuilt packaged Main Scene rendered the same saved composition at
  `/private/tmp/looksy-scene-relight-main-v2/receipt.json`. Its exposed window
  was not active. It is rendering/control-visibility proof, not a foreground
  interaction, steady-framerate or latency qualification. A single final frame's
  renderer timing does not include all startup/probe work or establish realtime
  behavior.

The next major capability gaps remain generating photogrammetry geometry,
soft light sources, object animation, imported materials and scene-wide indirect
light/reflections. Reference-level reconstruction quality and sustained native
interactive performance still require separate controlled evidence.


## Imported geometry continuation

**Scene composition → Objects → Import OBJ** adds a project-local triangle mesh.
The placement control either fits it in front of the current camera or keeps its
original coordinates. Source axes and units are retained; original-coordinate
placement is useful for geometry reconstructed in the same capture frame.
Import does not itself generate photogrammetry geometry or automatically align
an unrelated reconstruction. Editable position, positive nonuniform scale,
rotation, material, emission and visibility share the object inspector. Adding
a floor uses the transformed mesh bounds.

The importer accepts polygon OBJ positions, optional homogeneous position weights,
face-indexed normals, positive and relative negative indices, UV index syntax,
and concave polygons up to 256 corners. Concave faces are ear-clipped in their
dominant projection. Missing normals use triangle normals; smoothing-group
labels do not generate smooth normals. Unsupported freeform geometry fails with
a line number. Degenerate triangles are skipped and counted. Limits are 64 MiB
per source, 500,000 positions and 500,000 triangles per mesh, and one million
unique triangles per composition. Geometry/material separation follows ordinary
[OBJ face indexing](https://raw.githubusercontent.com/microsoft/DirectXMesh/main/Utilities/WaveFrontReader.h).
Geometry-only imports retain the v4 single-surface behavior. The material
continuation below adds MTL coefficients and color maps. Vertex colors remain
unapplied.

Import, parsing, BVH construction and asset publication run on a worker.
Reopening loads geometry on the scene-loading worker as well. Geometry is copied
to `geometry/<sha256>.obj` and checked against that digest on reopen. A changed,
missing or symlinked asset fails visibly. Composition v4 carries mesh hashes;
older compositions retain their schemas and behavior. Small transform/material
edits and undo history share immutable mesh data. Removing an object does not
delete its asset, so undo and already-accepted exports remain valid. Unreferenced
geometry files are retained; automatic garbage collection is not implemented.

A balanced bounding-volume hierarchy accelerates two-sided triangle intersections
on Metal. Surface normals interpolate supplied corner normals and transform by
the inverse transpose. Meshes receive and cast hard object shadows, including
self-shadows, and cast the optional approximate Gaussian-center shadows onto
captured splats. They use the same scan-probe reflections, haze, thin-lens rays,
focus picking, film/output path and tiled renderer as the analytic objects.
This does not add splat-cast shadows, indirect illumination or surface-based
scene reflections.

Mesh verification includes relative-index quads, concave faces with an empty
corner, malformed references, tiny finite normals, canceled imports and
translated polygons. Independently arranged render cases cover front/back splat
occlusion, nonuniform transform/focus depth, mesh receiving/casting object
shadows, mesh self-shadowing and mesh-to-captured-splat shadows. A 512-triangle
sloping grid exercises a multilevel BVH against its analytic plane depth; two
separate mesh assets check GPU index/root rebasing and expected colored pixels.
Haze/reflection tiles are pixel-exact in the fixture; aperture output uses the
established one-level tolerance.

The real native workflow passed on a private million-splat project copy at
`/private/tmp/looksy-scene-mesh-real-v2/receipt.json`. It imports a two-triangle
surface in capture coordinates, edits its material, uses pointer events to
undo/redo through the scrolled inspector, removes the original source OBJ,
exports a frozen still/film composition and reopens the project-local copy.
`objects-imported-mesh.png` and `objects-scroll-controls.png` were inspected.
The file dialog's selected URL and accepted signal are injected by the component
test; operating the native file picker with an OS pointer is not qualified.
This small surface plus a million splats is workflow evidence, not a performance
qualification for the maximum admitted triangle count or a reconstructed
photogrammetry surface.


Reflection probes for imported geometry use the transformed bounds center rather
than assuming the OBJ's origin is near its visible surface. Floor placement uses
the same transformed-bounds principle. Geometry parsing/BVH work participates in
project-switch cancellation. Finite tiny input normals normalize explicitly;
polygon projection uses a local anchor to preserve translated geometry precision.
The million-splat captures still show the original reconstruction's stretched
surface detail. Imported geometry does not repair those splats, and generating
reconstructed surfaces, additional material maps, soft lights and sustained
interactive qualification remain unfinished.

The final mesh continuation passes all ten focused Gaussian tests in
`/private/tmp/looksy-mesh-final4-ctest.log` (11.44 seconds). The final rebuilt
packaged Scene proof passed at
`/private/tmp/looksy-scene-mesh-main-v4/receipt.json`, rendering one million
splats and the saved mesh/analytic composition with lighting, reflections and
haze. Its native framebuffer was inspected. The window was exposed but not
active; catalog/AI and sustained foreground interaction remain outside this
proof's scope. Source/binary hashes are recorded in
`/private/tmp/looksy-scene-mesh-final-hashes.json`.


## Imported surfaces and color images continuation

OBJ import now defaults to **Geometry & materials**; **Geometry only** retains
its previous behavior when dependencies are unavailable or unwanted. Each face's
material slot and UV coordinates survive triangulation and BVH reordering. The
importer reads referenced MTL diffuse color (`Kd`), emitted RGB (`Ke`), roughness
(`Pr`), metallic weight (`Pm`) and base-color images (`map_Kd`). Legacy `Ns`
approximately maps to roughness as `1 - sqrt(Ns/1000)`, with a 0.04 floor;
explicit `Pr` takes precedence. This follows
[Blender's documented importer implementation](https://raw.githubusercontent.com/blender/blender/main/source/blender/io/wavefront_obj/importer/obj_import_mtl.cc),
not an exact conversion of arbitrary source shaders. Fixed dielectric Fresnel,
GGX direct light and the existing approximate environment probe remain the
shading model.

Referenced MTL/images must be inside the selected OBJ's directory tree. Paths
with spaces, surrounding quotes and relative subdirectories are supported;
references outside that tree fail explicitly. Color maps support UV scale,
offset, repeat and clamp. Images are limited to 8192 pixels per edge, 64 MiB per
encoded image, 32 distinct images and 256 MiB of decoded pixels per composition.
An import also limits retained encoded image sources to 128 MiB. Tagged images
convert to sRGB; untagged images assume sRGB. Image orientation follows stored
pixels rather than applying EXIF rotation. PNG is covered by the native proof;
other codecs depend on the installed Qt image readers and are not independently
qualified here.

The **Materials** page chooses an object and face-material slot. It exposes base
color, roughness, metallic, colored emission, image on/off, UV scale/offset and
image-edge mode. Base color fields display ordinary sRGB values while the stored
reflectance remains linear. Object color tints imported surfaces. The single
surface override replaces all imported face materials with the existing object
settings, and both modes support undo/redo. Missing requested dependencies or
UVs fail visibly. Actual alpha/transparency and unsupported map declarations
produce notices; normal/displacement/roughness/metallic/opacity image maps,
refraction and source shader graphs are not applied yet. Emission changes the
surface's radiance; it does not illuminate other surfaces.

Material manifests and original encoded color images are copied to
`materials/<sha256>.json` and `materials/textures/<sha256>.image`. Composition
v5 stores the material-set hash, editable per-object face materials and override
mode. Every material/image is integrity-checked on reopen and bound to the
original mesh hash. Parsing, image decode and publication stay on workers.
Decoded images share immutable storage by content hash; the weak cache retains
no unused pixel allocations. GPU images and mipmaps are reused across camera or
material-value edits. Mipmapped sRGB textures decode/filter in linear light,
with ray-derived UV gradients and up to 8x anisotropic sampling. Full-image ray
coordinates keep gradients consistent through tiling and aperture integration.
Still/film acceptance freezes all material settings and asset pointers.

A focused textured-plane check found edge rays that were numerically accepted
by some pupil samples and rejected by others. Conservative float BVH bounds,
a one-millionth barycentric edge allowance, clamped boundary barycentrics and
stable near-equal triangle ties remove the observed edge flicker. This is a
bounded float-precision correction, not a general proof of watertight geometry.
The same image checks retain their original tolerance.

Material verification uses independent expected pixels for UV orientation and
barycentric interpolation, linear `Kd`/`Ke`, texture repeat/clamp, single-surface
overrides and linear-light mipmap minification. The alternating black/white
texture converges to sRGB 188 rather than encoded-domain 128. The focused plane
includes exact geometric edge rays and retains its one-level optics tolerance;
texture/haze tile comparisons are pixel-exact in the fixtures. Tests also cover
missing dependencies/UVs, quoted image paths, legacy exponent conversion,
content-addressed image sharing, changed-asset rejection and project reopening
after removal of original OBJ/MTL/image sources. Material-only changes invalidate
resolved images while retaining the GPU color image; its upload-byte metric is
checked explicitly.

The real native workflow imports a two-face-material surface alongside a million
splats and edits both slots, color-map state, UV offset, clamp mode, roughness,
metallic and colored emission. It exercises the single-surface override, scrolled
pointer undo/redo, frozen still/film receipts and reopening without source
material files. Visual QA caught a slot-label/model-refresh mismatch despite the
rendered surface values being correct; the selector now synchronizes both its
visible name and index, with explicit checks after material edits. QML warnings
remain test failures.

The final material continuation passes all eleven focused tests in
`/private/tmp/looksy-material-final2-ctest.log` (12.72 seconds). The real workflow
passed at `/private/tmp/looksy-scene-material-real-v2/receipt.json`;
`materials-edited.png` shows the corrected **Checkered** slot label alongside its
edited values. Native captures and the exported still were inspected. The
fixture uses density 0.006 for the final real-scene demonstration; independent
haze equation tests retain stronger density cases. Original captures/projects
were not edited.

The final packaged Main Scene render proof passed at
`/private/tmp/looksy-scene-material-main-v2/receipt.json`, including one million
splats, three imported material slots and one color image. The window was exposed
but not active, so this establishes native rendering and control visibility,
not foreground latency or steady realtime performance. Texture upload metrics
confirm reuse in that final frame. Source/binary hashes are recorded in
`/private/tmp/looksy-scene-material-final-hashes.json`.

Additional maps and transparency/refraction, richer scene reflection/indirect
light, object animation, generating reconstructed geometry,
photographic-fidelity comparisons and sustained native interaction remain open.
This continuation does not establish full reference capability or visual parity.


## Finite sources and soft shadows continuation

The **Lights** page now adds one-sided rectangle and outward-emitting spherical
sources. Rectangles have position, XYZ orientation, width/height and relative
radiance; emission faces local +Z. Spheres have position, radius and relative
radiance. Larger area at fixed radiance emits more light. The directional light
adds an angular radius (0–89 degrees); its intensity stays normalized across the
sampled source cone. Angular radius zero and the original point source retain
their delta-light behavior. Dimensions are scene units, not calibrated watts,
lumens or lux. Emitting surfaces can now be shown to camera rays, as described
in the visible-source continuation below. Camera visibility does not alter their
illumination or add shadow-casting geometry.

Finite-source direct light is sampled geometrically before the GGX or captured
Lambert response, with visibility traced to each actual source sample. Rectangle
samples include emitter cosine, area and inverse-square geometry. Sphere samples
cover its visible solid angle; points inside the one-sided spherical source
receive no light from that source. Finite sun samples cover a uniform solid-angle
cone. These follow the standard
[area-light integral](https://pbr-book.org/4ed/Light_Sources/Area_Lights)
rather than applying a blur to finished hard shadows. The lighting affects
analytic and imported surfaces, including material maps and specular highlights,
and the optional approximate captured-color mode. Splats still do not cast new
shadows. Haze retains its existing approximate central-direction treatment of
sun scattering; area-light volume scattering and shadow shafts are not added.

Final light quality is selectable at 16, 32, 64, 128, 256, 512 or 1024 samples
(default 64). Navigation uses an explicitly reported eight-sample draft.
Low-discrepancy samples have deterministic per-pixel/per-Gaussian scrambling;
solid pixel seeds use full-canvas coordinates so tile seams do not change the
sample sequence. Higher quality reduces sampling noise, with more time required;
there is no denoiser or multiple-importance sampling of glossy lobes yet.

For finite-source refinement, the worker accumulates linear light in batches
of at most eight source samples.
Gaussian lighting and covariance-normal preparation use at most 65,536 points
per dispatch; solid-light batches use image bands of at most 65,536 pixels.
Cancellation is checked between committed batches. Partial integration is not
published as a completed photo. The UI reports completed lighting batches while
refining; a single GPU command still cannot be interrupted after submission.
The metrics include effective/target samples, draft status, batch count and
maximum measured batch GPU time. These bounds do not establish a wall-clock
latency or steady-framerate guarantee for every admitted scene.

Composition v6 adds finite source settings and light quality, preserving prior
composition descriptions when the new settings are unused. Accepted still/film
jobs freeze the full light configuration. Each aperture sample uses its own
surface rays and the same deterministic light sequence. Finalized radiance is
then composited with splats, haze and the existing linear aperture/film path.
Capture-light reflection probes distinguish draft and final light quality;
changing sample quality invalidates the relevant probe.

Finite-light verification includes independent closed-form sphere irradiance and
finite-sun angular averages, dense rectangle quadrature with separate segment/
box visibility, one-sided emission and emitting-surface distance limits. A
solid receiver has more than thirty partially shadowed pixels under the area
source, while the equivalent delta source has none. Exact tiled penumbra and
one-level aperture/haze parity are checked. A 65,537-point scene exposes only
its final, tilted-normal Gaussian and checks its analytically expected light,
covering both normal derivation and accumulation across a dispatch boundary.
Cancellation waits for an actual completed GPU batch before requesting a stop;
no incomplete frame/image is returned. The existing point/directional/material
and geometry checks continue to pass.

All twelve focused Gaussian tests pass in
`/private/tmp/looksy-soft-final-ctest.log` (17.59 seconds). The real million-splat
workflow passed at `/private/tmp/looksy-scene-soft-real-v1/receipt.json`, with
soft sun, sphere and rectangle controls, 16-sample quality, saved/reopened lights
and frozen still/film settings. Native `lights-rectangle.png` and
`lights-spherical.png` were inspected. Its final measured frame used 86 lighting
batches and reported a 0.501 ms maximum batch GPU time on this machine; this
single scene/frame measurement is not a worst-case latency or realtime claim.
Source stretching/softness remains visible in the original reconstruction.

The final cumulative tiled-progress correction passes all twelve focused tests
in `/private/tmp/looksy-soft-final2-ctest.log` (22.40 seconds, with other native
proof processes running). Tiled batch counts and maximum-batch timing now cover
the whole photograph; the public progress counter is cumulative across tiles.
The independent synthetic renderer receipt and native images are at
`/private/tmp/looksy-soft-light-oracles-v1/receipt.json`: rectangle clear and
shadowed samples matched 147/147 and 93/93 expected/actual; the solid row had 122
partially shadowed pixels under the rectangle versus zero under the point.
`solid-penumbra.png` and `solid-point-shadow.png` were inspected visually.

The final packaged Main Scene proof passed at
`/private/tmp/looksy-scene-soft-main-v2/receipt.json`, loading the saved
million-splat composition with textured geometry and all four light types.
Its framebuffer was inspected. The window was exposed but not active; this
proves rendering/control visibility, not foreground interaction or sustained
realtime behavior. Source/binary hashes are recorded in
`/private/tmp/looksy-scene-soft-final-hashes.json`.

Animation, additional maps/refraction, surface-based scene reflections and
indirect lighting, reconstructed-geometry generation, matched photographic
quality and sustained native interaction remain open. Full capability/visual
parity with the reference has not been established.


## Camera post effects continuation

Reinspection of the actual transcript identified camera controls that grain and
halation did not cover. At 38:00–41:01 the reference demonstrates bloom, glare,
highlight cutoff, glare ray count, spectral color, chromatic aberration and
saturation. These are also described in OTOY's primary
[Post Processing documentation](https://docs.otoy.com/cinema4d/PostProcessing.html).
The new controls are in **Photo settings → Post effects**, with Bloom, Glare
and Color sections. Numeric fields and sliders share the same saved values.

`GaussianPostSettings` stores bloom amount/spread, glare amount/length,
1–8 opposed streak pairs, rotation, spectral intensity, highlight cutoff,
radial chromatic fringing and saturation. Photo schema v4 includes the complete
lens/film/post settings. Default post settings preserve previous canonical
v1–v3 descriptions. Parsing rejects missing/extra fields, nonfinite numbers,
wrong types, fractional ray counts and out-of-range values. Camera paths require
consistent post settings across their shots, as they already require consistent
film effects. Saved views, still receipts and film keyframes retain the settings.
Editing existing Film or Lens controls preserves the post schema.

The image pipeline resolves aperture samples first. It then extracts RGB
highlights from exposed **linear float32** values, before any display clipping.
Bloom uses a normalized separable Gaussian. Glare integrates normalized colored
streak kernels with bilinear texture sampling at unit-pixel spacing; ray count
and spectral intensity do not multiply the kernel's energy. This is an RGB
creative approximation, not wavelength-traced diffraction or an Octane clone.
The existing halation is included before radial chromatic sampling. Saturation
acts in linear light before the look's highlight shoulder; grain remains after
encoding. Neutral settings keep the exact bypass. Motion uses the existing
explicit draft behavior and omits these effects until stopping.

Filter sizes and radial sampling use full-canvas coordinates, including tiles.
The tile coordinator expands each core by the largest active bloom, glare or
halation support **plus** the chromatic displacement/interpolation support.
It discards the overlap after rendering. Effects edit the retained resolved
source, so changing them does not repeat completed aperture sampling. This is
covered by exact independent-render cache comparisons. Glare cost grows with
image size, streak length and ray count; sustained realtime performance and
worst-case 8192 px camera-effect timing have not been qualified.

Verification at this checkpoint:

- `/private/tmp/looksy-camera-post-oracles-v1/receipt.json` passes independent
  double-precision image-space calculations for HDR RGB bloom, spectral streaks,
  radial chromatic interpolation and saturation. The two-byte tolerance includes
  quantization of the neutral image used by the oracle. +2 EV is applied before
  thresholding/convolution, so the HDR test cannot pass by blurring a clipped PNG.
  Cutoff, ray count/angle, native-texture/readback parity, black preservation and
  moving-draft behavior also pass. The retained proof images were inspected.
- The renderer suite additionally passes all-pixel full/tiled comparison at
  64/97 px tile sizes with maximum bloom/glare/fringing settings, rotated
  eight-pair spectral glare, halation, grain, exposure and sampled depth of field.
  Maximum allowed difference remains one byte. Existing neutral/optical/24 MP
  export and cancellation checks continue to pass.
- `/private/tmp/looksy-camera-post-focused-v1.log`: 14/14 focused tests pass in
  16.06 seconds: soft lights, materials, objects, object UI, cleanup, cleanup UI,
  controller, viewport handoff, renderer, focus picking, film export, spacetime
  workspace, saved views and camera paths. The desktop app and these targets
  rebuild. The previously recorded QString range-loop copy warning in
  `gaussian_viewport_handoff_test.cpp:307` remains unrelated.
- `/private/tmp/looksy-camera-post-real-v1/receipt.json`: private-copy million-
  splat SUV workflow passes bloom, glare and color editing, film-control edits,
  frozen still/film camera receipts, saved shots and reopen/restore. The original
  capture and project are untouched. The native captures still show stretched
  source detail; post processing does not reconstruct missing detail.
- `/private/tmp/looksy-camera-post-ui-final/receipt.json`: final component proof
  adds a real native pointer click on the glare slider at 1024 × 760, validates
  its bounds/value/rendered result, and captures `camera-post-pointer.png`.
  Other setting edits use their QML editing signals rather than claiming native
  pointer qualification for every control or OS file picker.
- `/private/tmp/looksy-camera-post-main-v1/receipt.json`: packaged Main restores
  the saved post-effect shot, displays the Glare controls and renders the effects.
  `camera-post.png` was inspected. It reports `windowExposed=true` and
  `windowActive=false`; this proves integrated rendering/control visibility,
  not foreground interaction latency, managed AI or catalog qualification.
  The optional `--gaussian-camera-post-proof` is confined to the existing
  isolated Scene render-proof path and retains its timeout.

Source and binary hashes for this checkpoint are retained in
`/private/tmp/looksy-camera-post-final-hashes.json` (18 files).

The next video-demonstrated camera gaps are the lens controls at 43:20–45:45:
aperture edge weighting, optical vignetting/cat-eye bokeh, spherical distortion,
astigmatism and field curvature. Capture-driven diffuse illumination of added
objects also remains behind the reference's emissive-scene integration. Advanced
maps, animated objects and calibrated film stocks are useful possible extensions;
they are not being treated as demonstrated requirements of this video. Source
fidelity and sustained native interaction still need qualification. **Full
capability and visual-quality parity has not been achieved.**


## Aperture edge and optical vignetting continuation

The controls demonstrated around 43:20–44:32 are now available in
**Photo settings → Aperture**: Aperture edge, Barrel distance and Barrel opening.
The opening is relative to aperture radius. Distance is stored in scene units;
when the user supplies physical scene calibration, the control displays and edits
millimetres. Recalibrating metres per scene unit retains the physical barrel
distance, rather than silently changing its millimetre value. Reset clears lens
character while preserving the configured physical focal length, focus and
f-number. Focus-pick errors now mention an incompatible barrel distance as well
as the existing aperture constraint.

This implements a geometric circular barrel opening ahead of the ideal aperture,
consistent with the type of effect described by OTOY's primary
[Universal Camera documentation](https://docs.otoy.com/HoudiniH/Houdini/UniversalCamera.htm).
For an aperture point `p`, a point `q` on the focus plane and barrel distance `d`,
the ray passes through the opening only when
`length(p + (q-p)*d/focusDistance) <= apertureRadius*barrelScale`.
The intersection clips off-axis pupil rays, changing the actual defocused
highlight shape and reducing throughput. Rejected samples contribute black to
the original sample mean; renormalizing only surviving samples would incorrectly
remove the corner light loss. This applies to the combined splat/solid/haze
sample before lens integration and camera post effects. It is not a corner-mask
post effect or a calibrated multi-element glass model.

Aperture edge importance-samples a radial response proportional to `r^edge`
(edge 0–8). The circular radial CDF is `r^(edge+2)`, and higher values move energy
toward the highlight rim. Rounded-polygon rejection retains that response
without introducing corner-density bias. Uniform scenes retain their brightness
when the barrel does not block rays. The zero-edge sequence is exactly the
previous uniform-area sequence. Quality prefixes remain deterministic and stable
through 4096 samples, including polygons at the strongest supported response.
Insufficient sampling is rejected rather than silently returning a partial set.

Lens schema v3 stores the three new fields; default character preserves canonical
v1/v2 descriptions. The parser rejects malformed, nonfinite and out-of-range
settings, and old schemas reject the new fields. Barrel distance is bounded to
one quarter of focus distance; opening is 0.5–4 times aperture radius. Camera
paths require matching character settings while still allowing focus and aperture
size changes. Saved shots and frozen photo/film keyframes include the fields.
Film/Post edits preserve the new lens schema. Character changes invalidate the
resolved-source cache; later post edits reuse the completed pupil image.
Full-canvas ray coordinates preserve optical vignetting across photo tiles.

Evidence collected:

- `/private/tmp/looksy-pupil-oracles-v1/receipt.json`: radial moments/CDFs,
  polygon completeness/prefixes, independently traced barrel-plane throughput,
  analytic two-disk overlap, rendered rim redistribution, source invalidation,
  progressive-prefix parity and pinhole bypass pass. Analytic transmission is
  0.5483596; rendered transmission is 0.5464869. The centered point's total linear
  energy is 59.92335 with uniform pupil response and 59.91130 with edge response;
  the edge outer/inner brightness ratio is 42.54. These are synthetic model
  measurements, not photographic calibration. The 513 px, 4096-sample grids
  `bokeh-grid-0.png`, `bokeh-grid-1.png`, `bokeh-grid-2.png` were inspected and show
  uniform disks, bright rims and tangential off-axis cat-eye clipping.
- The renderer regression includes exact image equality after scaling the scene,
  aperture, focus and barrel distance by ten, plus all-pixel tiled/full comparison
  with edge response, barrel clipping, depth of field and all camera post effects.
  Existing renderer and persistence checks continue to pass.
- `/private/tmp/looksy-pupil-ui-v2/receipt.json`: native component workflow passes
  numeric character edits, Film-schema preservation, frozen still/film lens
  settings, reopen/restore, a real compact-window pointer drag, and physical-mm
  display/editing. Its controls/exports deliberately use 16-sample **draft**
  quality. Those captures prove behavior and bounds, not final bokeh smoothness.
- `/private/tmp/looksy-pupil-real-v1/receipt.json`: the same draft control,
  persistence and export workflow passes on a private copy of the million-splat
  SUV project. The original artifact and project are untouched. The source's
  stretched detail is still visible; these lens controls do not reconstruct it.
- `/private/tmp/looksy-pupil-main-v1/receipt.json`: packaged Main restores the
  saved character shot, explicitly renders it at **256 aperture samples**, and
  shows the Aperture controls. `camera-pupil.png` was inspected. Its single-frame
  measurement is 8.79 seconds GPU and 11.48 seconds render time, not realtime.
  The window is exposed but inactive, so this is integrated render/control proof,
  not foreground interaction-latency qualification. The optional
  `--gaussian-camera-pupil-proof` runs only inside the isolated Scene render
  proof path and keeps the existing timeout.

The remaining demonstrated lens gaps are spherical distortion, astigmatism and
field curvature (44:45–45:45). Capture-driven diffuse illumination, reconstruction
fidelity and representative sustained interaction also remain open. The new
pupil model does not establish measured-lens or full reference-quality parity.

Final pupil checkpoint: `/private/tmp/looksy-pupil-final-ctest.log` passes 14/14
focused tests in 44.46 seconds after the scene-unit scale-invariance check and
physical recalibration preservation were added. `/private/tmp/looksy-pupil-main-v2/receipt.json`
passes against the rebuilt packaged binary, again at 256 aperture samples
(8.77 seconds GPU, 11.49 seconds render time). The earlier QString range-loop
copy warning at `gaussian_viewport_handoff_test.cpp:307` remains unrelated; no
new warnings were emitted in this checkpoint. Unrelated dirty-tree work is
preserved. Full reference capability and quality remain unproven.

`/private/tmp/looksy-pupil-ui-final/receipt.json` retains the final rebuilt native
component proof, including physical-distance preservation when metres per scene
unit changes. Source/binary hashes (21 files) are in
`/private/tmp/looksy-pupil-final-hashes.json`.


## Spherical distortion, astigmatism and field curvature continuation

The remaining controls shown at 44:45–45:45 are now in
**Photo settings → Aperture → Distortion and aberrations**. The model changes
ray directions before aperture integration. It operates on the entire composed
scene, including inserted geometry, splat/solid visibility, reflected surfaces
and haze. It does not apply a blur to the finished photograph. The reference's
distinction between distortion, perpendicular focal separation and a curved
focal surface is described in OTOY's primary
[Camera Settings and Effects guide](https://help.otoy.com/hc/en-us/articles/25808205958811-Octane-Camera-Settings-Effects).
These controls are creative strengths in an analytic model, not OTOY parameter
or pixel equivalence, a glass prescription, or measured lens calibration.

For normalized sensor field `q`, `b = dot(q,q)/(1+dot(q,q))` bounds off-axis
strength. Spherical distortion maps the chief ray to `q*(1+k*b)` and also changes
mean focal curvature. Its range is -0.5 to +0.5, preserving a monotone invertible
projection. Field curvature changes the local mean convergence; astigmatism
splits radial and tangential convergence with a trace-free field tensor. Both
range from -2 to +2. Their signs change the off-axis highlight orientation and
focus distribution. The optical axis stays unchanged. At zero aperture,
astigmatism and field curvature are exact bypasses; spherical framing still
works and remains active in the moving draft to avoid a framing jump.

Each pupil view renders into a conservatively bounded source region, including
geometry outside the original frame. The shader samples that view at the mapped
ray coordinates, interpolating decoded **linear** float32 color, then averages
aperture samples. This introduces bilinear ray-grid reconstruction, so it is not
a fully analytic per-ray evaluation of every Gaussian or a spectral ray tracer.
The barrel opening clips the modified ray directions. With all new strengths
zero, the previous pupil/integration path is retained exactly.

The source bound uses interval radial projection and a bound on the norm of the
aberration matrices. Its integer grid is independent of quality level and tile
origin. Source culling expands to cover those regions. Source allocation is
bounded to 8192 pixels per edge and, for lens-resolved views, 16,777,216 texels;
unsupported parameter/output combinations return a range/budget error rather
than silently clipping lens coverage. Large mapped views use 1024 px tile cores:
photo export assembles the color-tagged CPU image, while native previews assemble
an immutable final texture through GPU blits. Preview does not read pixels back
to the CPU, lower the requested output resolution, or publish a partial tile as
a full-frame progressive image. Tile and lens-source metrics report the actual
work. Completed tiles remain cancellable between submissions.

The shared C++ projection supports focus picking and cleanup selection. Picking
traces the displayed chief ray and converts surface depth to the corresponding
on-axis focus parameter for the curved focal surface. At a nonconverging field
position it reports an explicit focus-range error. Astigmatic picking selects
the mean convergence, not a guarantee that both split focal surfaces become
sharp simultaneously. Rectangle selection uses inverse spherical projection of
Gaussian centers; it retains its existing all-depth, center-selection semantics
and does not become a surface-aware or blurred-footprint tool.

Lens schema v4 includes all pupil controls and the three new strengths. Zero
strengths preserve the prior canonical schemas. Strict parsing, saved shots,
physical camera settings, frozen photo/film export and cache keys preserve the
settings. Camera-path shots must share character settings; post/film edits keep
the lens version. Physical scene-scale changes still retain barrel distance in
millimetres. Reset clears all six character fields while retaining the configured
physical camera. Reflection probes keep their own unprocessed scan-camera views.

Initial retained verification is in
`/private/tmp/looksy-aberration-oracles-v3/receipt.json`: inverse projection,
conservative coverage, independent double chief-ray and per-pupil image oracles,
off-frame geometry, constant-color borders, native/draft parity, focus and cleanup
all pass. The two-byte oracle tolerance includes quantization of independently
rendered neutral/pupil images. A thin Gaussian entirely outside the original
view is absent before distortion and visible after it, proving expanded source
coverage/culling. The 513 px, 2048-sample bokeh grids were inspected: changing
astigmatism sign switches tangential/radial stretching; curvature increases blur
away from the optical axis. These images demonstrate the model, not photographic
quality parity with the video.

Final aberration checkpoint:

- `/private/tmp/looksy-aberration-final-ctest.log`: 14/14 focused tests pass in
  50.27 seconds. This includes the 2049 × 257, 16-aperture-sample native-tile
  assembly comparison against photo output, with no CPU preview readback and
  no partial-tile publication. Full/tiled comparisons also cover temporal splats
  with distortion, astigmatism, curvature and camera post processing.
- `/private/tmp/looksy-aberration-ui-v1/receipt.json`: native component controls,
  frozen photo/film lens settings, saved shots, reopen/restore, compact pointer
  interaction, physical-unit conversion/recalibration and reset pass. The
  `camera-aberrations.png` capture was inspected. These control/export fixtures
  deliberately use 16-sample draft quality.
- `/private/tmp/looksy-aberration-real-v1/receipt.json`: the same workflow passes
  on a private million-splat SUV copy with distortion 0.08, astigmatism -0.5 and
  field curvature 0.15. The original project is untouched. Reconstruction detail
  remains warped and soft; these controls do not repair it.
- `/private/tmp/looksy-aberration-main-v1/receipt.json`: packaged Main restores
  the authored v4 lens, renders 256 aperture samples, and displays the new
  controls. The final `camera-aberration.png` was inspected. The reported source
  region is 1448 × 1930; the single frame took 9.73 seconds GPU / 12.81 seconds
  render time. This is not realtime. The window was exposed but inactive, so
  this proves integrated rendering/control visibility, not foreground latency,
  managed AI or catalog readiness. `--gaussian-lens-aberration-proof` is confined
  to the isolated Scene proof mode and rejects a shot without the v4 lens.

The reference also demonstrates continuous center autofocus at 41:59–42:55.
The current picker and capture-depth button are one-shot actions; they do not
establish a persistent autofocus mode during camera movement or film rendering.
That remains an explicit workflow gap. The imager's adjustable highlight
compression/contrast at 45:54–46:42 also needs a direct control/parity audit;
the current soft-highlight look is a fixed generic shoulder. Capture-driven
diffuse illumination of added objects, matched reconstruction fidelity and
representative sustained interaction remain open. **Full capability and visual
quality parity with the reference has not been achieved.**

Source/binary hashes for this checkpoint (24 files) are retained in
`/private/tmp/looksy-aberration-final-hashes.json`. The focused and desktop
builds emitted no new warnings in the final build logs. Unrelated worktree
changes are preserved.


## Continuous centre autofocus continuation (2026-10-25)

**Photo settings → Focus → Continuous centre autofocus** now stays enabled during
camera movement, scene-time changes, saved-camera playback and film rendering.
The readout shows the resolved distance or an explicit fallback reason. Manual
mode freezes the currently displayed focus when the frame is current. Clicking
Pick focus switches to a one-shot manual result; physical-camera picking uses the
resolved projection and commits a consistent manual physical lens. Manual focus
fields show the authored fallback while automatic mode is active. The reference
shows this centre-mode workflow at 41:59–42:55; OTOY's primary
[Focus documentation](https://help.otoy.com/hc/en-us/articles/25808205958811-Octane-Camera-Settings-Effects)
also describes a fixed sensor-centre target and authored-distance fallback.

Autofocus resolves the centre chief ray once per immutable pose/time/composition,
before aperture accumulation or photo tiling. It uses the existing projected
opacity median, including geometric solid/mesh intersections, with the pixel
covariance floor removed. This makes target selection independent of preview,
photo and film resolution, rather than allowing a subpixel blurred foreground
splat to change the target as output size changes. It is a geometric Gaussian
surface estimate, not sensor phase/contrast autofocus or a lens-motor simulation.
Astigmatism targets mean convergence; field curvature and distortion retain the
shared focus-parameter conversion. An empty centre or unsupported close target
uses the authored fallback, never a different previous frame's focus.

Lens schema v5 adds `focusMode=auto-centre` and retains the complete pupil and
aberration settings. Manual descriptions keep the previous canonical schemas.
Invalid modes, wrong types, missing/extra fields and malformed values are
rejected. Saved shots retain automatic mode and fallback settings. Film paths
require consistent focus mode and optical character. Physical autofocus paths
may vary fallback focus while preserving their common lens/scale; intermediate
cameras are rebuilt with physical unit-focus breathing instead of interpolating
inconsistent calibration metadata.

Authored and resolved cameras remain separate. The renderer records the authored
camera for viewport publication checks and the resolved manual camera for replay.
Progressive snapshots carry the same immutable focus decision, so refinement does
not refocus partway through an aperture sequence or fail the authored-camera
staleness guard. Identical resolved optics can reuse the existing source cache;
autofocus still checks the current target on each render. Reflection probes keep
their own scan-camera views. Autofocus work uses the worker's cancellation flag;
no UI-thread focus scan, GPU wait or pixel readback is added.

Displayed focus and manual picking now check the published image's request and
scene epoch, independently of the worker currently submitted. Starting a new
render does not make an old image current, even when the camera pose is unchanged.
The handoff test covers both a newer request and a changed composition epoch;
stale focus is hidden, picking waits, and switching to manual retains the authored
fallback rather than freezing that stale result.

Photo receipt v4 stores `camera` as the actual manual camera used for rendering,
plus `authoredCamera` and the autofocus report. Movie receipt v3 keeps authored
keyframes and records each frame's resolved focus, lock/fallback state and,
for physical cameras, exact focus in metres. These values allow deterministic
manual-camera replay instead of rerunning a future autofocus implementation.
The frame index maps to `frameIndex / fps`; temporal films retain their existing
scene-time mapping. The full trace stays in the movie sidecar. Viewport status
exposes its sample count rather than copying thousands of samples into QML.
Rendering metrics include autofocus time and the resolved-camera description.

Evidence so far:

- `/private/tmp/looksy-autofocus-oracles-v1/receipt.json`: movement, temporal
  position, inserted-object occlusion, hidden-object changes, empty-centre and
  lens-range fallback, resolution invariance, physical focus/breathing replay,
  progressive metadata/image parity, cache reuse and cancellation all pass.
  Resolved manual cameras reproduce the automatic pixels exactly. The narrow
  foreground fixture deliberately crosses opacity 0.5 only under the screen
  pixel floor, so it would expose resolution-dependent targeting.
- `/private/tmp/looksy-autofocus-film-v1.log`: twelve changing autofocus distances
  are exported and recorded. Decoded movie frames compared with independently
  rendered manual cameras from the trace have minimum RGB PSNR **40.8201 dB**
  (existing threshold 35 dB). The original cancellation, color-tagging and
  publication-collision checks also pass.
- `/private/tmp/looksy-autofocus-ui-v2/receipt.json`: native controls follow a
  dolly move, manual mode freezes the displayed result, a real pointer pick
  switches to manual, and autofocus can resume. Saved shots, frozen still/movie
  records, reopen/restore, physical manual picking, millimetre conversion,
  physical recalibration and reset pass. These fixtures use 16-sample draft
  quality; they qualify workflow behavior rather than final photographic detail.
- `/private/tmp/looksy-autofocus-final-ctest.log`: 14/14 focused tests pass in
  75.58 seconds after adding per-frame trace summarization. They cover renderer,
  focus, physical path interpolation, persistence, geometry/materials/lights,
  cleanup, native component workflow, handoff, spacetime workspace and film.
  The complete suite passes again after the publication-freshness regression:
  `/private/tmp/looksy-autofocus-freshness-ctest.log`, **14/14 in 78.70 seconds**.
- `/private/tmp/looksy-autofocus-real-v1/receipt.json`: the same continuous-focus,
  movement, native manual pick, save/reopen and still/film workflow passes on a
  private copy of the million-splat SUV scene. The original project is untouched.
- `/private/tmp/looksy-autofocus-main-v2/receipt.json` and `camera-autofocus.png`:
  the rebuilt packaged Main Scene displays continuous autofocus and a completed
  256-sample million-splat image after the publication-freshness fix. The native
  window was exposed but inactive. This is a scoped Scene result; catalog and
  managed agent runtime are not qualified. The earlier standalone v1 run measured
  117.79 ms autofocus and 12.84 s total photo rendering; v2 overlapped the test
  suite and is not a comparable performance sample. The visible source remains
  warped/soft, so neither capture establishes photographic quality parity.
- `/private/tmp/looksy-autofocus-source-binary-v1.json`: SHA-256 identities for
  the autofocus sources and rebuilt app/workflow-test executables. These identify
  evidence in the current dirty checkout, not a clean release.

The final integrated desktop rebuild retained the previous QString range-loop
copy warning in `gaussian_viewport_handoff_test.cpp:307` and an unrelated generated
`ExportView_qml.cpp` integer-conversion warning. No new autofocus-source warnings
were emitted. Autofocus adds a CPU centre scan; representative sustained latency,
focus jitter and final photographic comparison remain unqualified. Adjustable
tone controls, capture-driven object illumination, reconstruction fidelity and
performance still separate this from the complete reference workflow.


## Adjustable camera tone continuation (2026-10-25)

**Photo settings → Tone** now includes independent **Contrast** (0.25–2,
neutral 1) and **Highlight compression** (0–100%, neutral 0). Native pointer
sliders edit the shared preview, still and movie settings. Film paths smoothly
interpolate both values between saved shots, using the same eased fraction as
exposure. Lens, physical-camera, film and post edits retain the accepted tone.
Reset photo settings restores its neutral defaults.

This addresses the adjustable imager workflow noted at 45:54–46:42. OTOY's
[Camera Imager documentation](https://docs.otoy.com/cinema4d/CameraImager.html)
describes compression of bright output highlights. The model here is explicitly
`linear-luminance-contrast-peak-shoulder-v1`; no numerical Octane match or
measured film/sensor response is claimed.

The existing creative look is applied first. Contrast scales RGB together so
linear luminance Y becomes `0.18 * (Y / 0.18)^contrast`; black stays zero and
middle gray stays fixed. Compression then chooses a knee `k = 1 - 0.8*h`,
where h is its 0–1 amount. Peaks P above k become
`k + (1-k) * (1 - exp(-(P-k)/(1-k)))`; lower peaks are unchanged. All channels
are scaled together to preserve their ratios. Compression 0 bypasses the
shoulder exactly. The neutral pair preserves the old shader bypass and saved
schema; earlier shots keep their rendering behavior.

Processing happens once after pupil/light integration, exposure, spatial post
and the creative look, before sRGB encoding/clipping and grain. It operates on
float radiance, rather than trying to restore highlights from a clipped PNG.
It applies equally to splats, inserted surfaces, haze and optical/post effects.
Interactive drafts retain tone even while the existing expensive lens/film/post
effects are suspended. Tone changes are excluded from the resolved optical-source
cache key, so they do not rerasterize an unchanged pupil integration. They add no
new spatial filter, CPU image copy or preview readback.

`framehouse-gaussian-photo-settings-v5` adds a strict nested
`framehouse-camera-tone-v1` recipe. Missing/extra fields, unknown schemas, wrong
types, nonfinite values and values outside the double-precision limits are
rejected before float conversion. Non-default tone persists in saved cameras,
actual still-camera receipts and film keyframes. Explicit neutral recipes
canonicalize to the earlier applicable photo schema. The renderer reports the
model and whether tone is active, including progressive frames and tiled output.

Evidence so far:

- `/private/tmp/looksy-camera-tone-oracles-v1/receipt.json`: **2,187** independent
  scalar channel comparisons from known flat linear surface radiances, with
  **zero maximum byte error**. These cover dark/gray/colored patches, -2/0/+3 EV,
  all three looks, minimum/neutral/maximum contrast and zero/half/full compression.
  Neutral identity after edits, actual native texture pixels and interactive
  rendering agree. The images show an unclipped-radiance tone test chart, not a
  photographic or Octane comparison.
- `/private/tmp/looksy-camera-tone-core-ctest.log`: renderer, saved-view,
  camera-path and film tests pass, 4/4 in 7.36 seconds. The renderer also checks
  progressive pupil prefixes, resolved-source reuse and full/tiled pixel parity
  with tone combined with lens, bloom/glare, saturation, grain and halation.
- `/private/tmp/looksy-camera-tone-film-v1.log`: twelve changing autofocus and
  interpolated-tone frames decode and replay from the recorded settings with
  minimum RGB PSNR **41.4258 dB** (existing threshold 35 dB). Frame count, color
  tags, cancellation and publication checks remain passing.
- `/private/tmp/looksy-camera-tone-ui-v1/receipt.json`: the native workflow passes
  actual pointer edits, tone retention through film/post/lens changes, frozen
  still/movie records and save/reopen/restore. This first synthetic run preceded
  the compact-pointer and explicit tone-receipt additions.
- `/private/tmp/looksy-camera-tone-real-v1/receipt.json`: the updated workflow
  passes on a private copy of the **one-million-splat** SUV project. It records
  accepted contrast **1.5** and compression **0.72**; both appear unchanged in
  the still's actual camera and both movie keyframes. The 1024×760 logical-size
  popup retains visible controls, and an actual pointer click changes compression
  to **0.39** after reopening the saved shot. These use 16-sample draft rendering;
  the visible bokeh sampling and source reconstruction are not final-quality
  evidence. Original scene inputs are not edited.
- `/private/tmp/looksy-camera-tone-source-binary-v1.json`: SHA-256 identities
  for the tone sources, packaged executable and native workflow/render/film tests.
  This scopes evidence to the current dirty checkout, not a clean release.
- `/private/tmp/looksy-camera-tone-main-v1/receipt.json` and `camera-tone.png`:
  the rebuilt packaged Main Scene displays both tone controls and the accepted
  recipe with **256 pupil samples and one million splats**. The native window
  was exposed and inactive. The single completed photo reports **12.88 seconds**
  total render time, including **122.67 ms** centre autofocus; this does not
  establish sustained interaction or comparative performance. Catalog and
  managed-agent qualification remain outside this scoped Scene proof.
- `/private/tmp/looksy-camera-tone-final-ctest.log`: **14/14 pass in 79.02
  seconds** after the compact-pointer and receipt checks. This includes the
  updated synthetic native workflow, rendering, optics/focus/path persistence,
  film export, spacetime workspace, cleanup, materials, geometry and soft lights.
  The integrated desktop build passes; its retained QString range-loop copy
  warning in the existing handoff test is unrelated to tone. No new tone-source
  warnings or whitespace errors were found, and recorded source/binary hashes
  still match after the final runs.

This provides editable tone behavior, not lost reconstruction detail. Clipped
capture highlights cannot be recovered from a trained display-referred splat.
Capture-driven object illumination, geometric/contact integration, matched
neutral photographic detail and sustained interaction performance remain open.


## Captured environment illumination continuation (2026-10-25)

**Scene composition → Lights → Scan illumination** now lights inserted diffuse
surfaces from the captured surroundings. **Scan light strength** scales that
contribution from 0 to 10; ambient fill stays a separate additive control.
Reflection and diffuse illumination can be enabled independently. The existing
16–1024 light-sample setting also controls environment integration; navigation
uses eight samples. Defaults remain off, so earlier compositions retain their
appearance.

The renderer reuses the existing six-face 256-pixel, linear RGBA16F capture probe,
anchored at the first visible object's center (transformed mesh bounds for OBJ).
For each added-surface hit it integrates cosine-weighted hemisphere directions
against the captured radiance and traces visibility through all added primitives
and mesh BVHs. This gives directional captured-color illumination and geometric
contact shading, including concave/self occlusion where represented by the mesh.
A dielectric Fresnel weight reserves energy for reflection; fully metallic
materials receive no diffuse lobe. Input values above display white remain linear
until the common exposure/tone/output stage. The sampling follows the standard
[cosine-weighted diffuse estimator](https://pbr-book.org/4ed/Reflection_Models/Diffuse_Reflection).

This is **local-probe image-based lighting**, not full-scene multi-bounce GI.
A single shared anchor approximates spatially varying illumination; probe pixels
retain baked captured lighting and incomplete angular coverage can be dark.
The probe excludes added-object radiance. Added objects can occlude environment
light, but they do not bounce light onto each other or into the scan, and splats
do not become ray-traced shadow geometry. Emissive materials still do not light
other surfaces. These distinctions remain relevant to the reference-quality bar.

`looksy-scene-composition-v7` adds a strict `environment` group containing a
boolean `diffuse` switch and finite 0–10 `strength`. It retains all existing
material, haze, captured-light, quality and reflection settings. Earlier schemas
and neutral defaults canonicalize as before. The recipe persists atomically in
the existing artifact-bound composition, participates in undo/redo, and freezes
with still/movie exports. QML effect/light-shape/reflection edits preserve v7.
Camera recall continues to use the global composition rather than overwriting it.

The new integration uses the existing at-most-eight-sample, at-most-65,536-pixel
GPU batches, with cancellation between batches. Pupil integration, progressive
snapshots, tiled output, spatial camera effects and tone consume the same shaded
radiance. Changing diffuse strength rerenders the shaded source but reuses an
unchanged capture probe, including under captured-color lighting. Scene/time,
anchor and captured-appearance changes invalidate the probe. Rendering requires
no probe readback or new preview CPU image path.

Completed `renderMs` now measures the full renderer call, including probe
preparation and autofocus. `scanProbeMs` reports probe creation or its cache
check separately; progressive frame times include that preparation too. Earlier
phase-only timings in the preceding checkpoints excluded probe work and are not
directly comparable. `gpuMs` continues to describe the image-render phase.

Evidence so far:

- `/private/tmp/looksy-environment-oracles-v5/receipt.json`: native Metal checks
  pass constant-radiance Lambert response/strength, zero diffuse contribution for
  fully metallic surfaces, disabled legacy behavior, an independent white-furnace energy balance,
  over-white input before material response, six directional-color hemispheres,
  strict parsing and persistence. Dense polar quadrature is independent of the
  shader's hashed low-discrepancy sampler; maximum directional byte error is **1**.
- The same run measures sphere-occluded irradiance **0.73462**, compared with
  the analytic **0.73969** ratio. A mesh rectangle gives **0.75493**, compared
  with independent area-quadrature **0.75317**. Comparisons include 8-bit output
  quantization. Hidden blockers restore the previous pixels. Native texture,
  cropped/tiled output and progressive pupil-prefix pixels agree, including
  32-sample aperture integration combined with environment visibility. A live
  1024-sample integration cancels after a few GPU batches, before completion.
- `/private/tmp/looksy-environment-ui-v1/receipt.json`: the new focused native
  SceneWorkspace workflow passes actual pointer toggle, strength, undo/redo,
  other-effect schema retention, still recipe, concurrent-edit/frozen movie
  recipe, reopen/recall and compact native toggle checks. This separately scoped
  workflow keeps the broader existing object/editing regression intact.
- `/private/tmp/looksy-environment-real-v2/receipt.json`: the same workflow
  passes on a private **million-splat** SUV copy at **64 environment samples**.
  Exports freeze strength 0.7 while a concurrent strength edit to 0.2 persists
  and reopens correctly. Original scene inputs are untouched. Captures include
  `environment-off.png`, `environment-on.png`, `environment-compact.png` and
  `environment-compact-pointer.png`. The final component frame reports 81.07 ms
  including its 0.0023 ms probe cache check; this single sample is not sustained
  performance proof.

- `/private/tmp/looksy-environment-main-v2/receipt.json` and
  `environment-lighting.png`: the rebuilt packaged Main Scene displays scan
  illumination with **one million splats and 64 environment samples**. The native
  window is exposed and inactive. Its completed cold frame reports **419.85 ms**
  for the full call, including **327.28 ms** scan-probe preparation. This corrects
  the earlier omission of probe time; it is not a sustained benchmark. Catalog
  and managed-agent qualification remain outside this scoped Scene proof.
- `/private/tmp/looksy-environment-source-binary-v1.json`: affected source and
  native app/workflow/oracle executable SHA-256 identities, verified unchanged
  after the final native runs. The integrated build and whitespace checks pass.
- `/private/tmp/looksy-environment-final-ctest-v2.log`: **16/16 pass in 80.11
  seconds** with the complete-call timing correction. The existing fourteen
  Gaussian regressions remain intact, with two added environment renderer/UI
  tests. The earlier pre-timing suite also passed 16/16 in 82.18 seconds. The
  final rebuild emitted no warnings; no unrelated working-tree changes were
  reverted or committed.

The captured SUV still has warped/soft reconstruction detail. This lighting
integration cannot repair that input. Multi-bounce transport, spatially varying
scan visibility, camera-visible emitters, matched neutral photographic detail
and sustained native interaction remain open before claiming full reference
capability and quality.


## Reconstruction fidelity and convergence continuation (2026-10-25)

The installed trainer is available at `build/gaussian-runtime/brush_app`.
Two independent checks now separate neutral renderer fidelity from missing source
coverage and reconstruction detail. These do not certify complete video parity.

### Existing indoor capture and a new trained artifact

The indoor `IMG_0894 2026-09-09 223746-945` project has **110 registered views out
of 960 extracted frames**. Its saved 14-view evaluation set exactly matches every
eighth registered camera, starting at index zero, in both COLMAP file order and
name order. Its original frame grid is 873×1553; no image registration, resizing,
color correction, sharpening or film effects were applied in these comparisons.

`/private/tmp/looksy-indoor-fidelity-v1/native-summary.json` records all fourteen
native renders against the saved Brush images: minimum PSNR **77.91 dB** and
maximum channel error **2 codes**. The independent double-precision projection
oracle has maximum native error **1 code** at its 192 samples per view. Full-image
native-versus-capture results reproduce the saved training evaluation:
**31.65676 dB / 0.946771 SSIM**, versus **31.65673 / 0.946771** for Brush's saved
images. The native renderer faithfully reproduces the trained model on this
specific camera set. The mean hides a **17.78 dB** worst view (`frame_000302.png`),
whose source and inspected error crops contain motion smear and a large color
mismatch. Source blur, partial coverage and outliers remain material limits.

A new independent 10,000-step run used copied bytes for the same 110 registered
images and COLMAP calibration/points, seed 42, maximum edge 1600, splat budget
500,000, refinement every 110 steps and growth stop at 8000. It saved models and
all fourteen held-out images every 2500 steps. Training completed successfully.
Independent native-grid RGB evaluation gave:

| Same-run checkpoint | Mean PSNR | Mean SSIM | Worst-view PSNR |
| --- | ---: | ---: | ---: |
| 2500 | 29.52050 dB | 0.929108 | 17.58157 dB |
| 5000 | 30.99829 dB | 0.939879 | 18.01641 dB |
| 7500 | 32.01163 dB | 0.947985 | 17.95185 dB |
| 10000 | 33.25240 dB | 0.953804 | 17.68378 dB |

The final artifact contains **211,946 Gaussians**. Compared with the historical
5000-step artifact, mean PSNR improves **1.59564 dB** and mean SSIM improves
**0.007033**. Growth and learning-rate schedules differ from that historical run,
so this is a budget/schedule experiment rather than an isolated iteration-count
ablation. The same-run checkpoints show convergence without changing image,
pose or evaluation domains. The worst historical view does not improve; more
optimization has not fixed its source problem. No global training-default or
quality-preset change is justified from this one partial capture.

All fourteen final native renders again match the trainer outputs, with minimum
PSNR **76.95 dB** and maximum channel error **2 codes**. A reviewable project is
saved persistently under:

`build/gaussian-quality-experiments/indoor-10000/project`

Open that folder with **Scene → Open scene**. Its sibling files include
`convergence-summary.json`, `native-10000-summary.json`, fourteen neutral renders,
`quality-10000/report.json` and native-pixel error crops. The new project preserves
the partial-capture notice. It is an experimental trained result, not a clean
capture or a general-quality guarantee. The copied project is inside ignored
build output; no original project, media or trained artifact was replaced.
`original-inputs-check.json` verifies all 115 registered image/calibration input
files against preparation hashes. The historical PLY and native binary identities
are recorded in the separate fidelity receipt.

Full temporary experiment evidence remains in
`/private/tmp/looksy-indoor-convergence-v1`, including `experiment.json`, four
checkpoints, all held-out outputs and the independent reports. The historical
native/trainer/source comparison and worst-region triptychs are in
`/private/tmp/looksy-indoor-fidelity-v1`.

### Sharp imported scene and packaged native proof

For a source with sharper photographic detail, the official
[3D Gaussian Splatting project](https://repo-sam.inria.fr/fungraph/3d-gaussian-splatting/)
provides the Bonsai pretrained model, camera metadata and evaluation images.
Only its selected archive members were fetched with bounded HTTP range requests;
ZIP sizes/CRC and local SHA-256 hashes were verified. These local research assets
stay outside the repository and app bundle. Provenance is in
`/private/tmp/looksy-reference-benchmark-v1/selected-model-provenance.json` and
`selected-evaluation-provenance.json`.

The **1,244,819-Gaussian** model was rendered at every one of its **37 named
evaluation poses**, on the untouched **1559×1039** reference grid. Standard
camera-to-world JSON poses were converted to COLMAP pinhole calibration; focal
lengths were scaled by 0.5 from the 3118×2078 camera metadata. No evaluation image
was transformed. Native-versus-ground-truth results are **31.86241 dB mean PSNR**
and **0.938172 SSIM**; worst-view values are **23.50548 dB** and **0.860932**.
Per-view metrics, input/candidate hashes and worst native-pixel crops are in
`/private/tmp/looksy-reference-benchmark-v1/native-proof/native-vs-capture`.
Visible cloth texture, small object edges and background detail were inspected.
This demonstrates a sharp imported-model result at these poses, with remaining
floaters and viewpoint-specific errors.

The separate paper-era renders score **31.98058 dB / 0.938405** under the same
independent metric. The authors explicitly
[distinguish downloadable release-code models from paper-era results](https://github.com/graphdeco-inria/gaussian-splatting#evaluation).
The downloaded model and paper images therefore do **not** establish same-weight
renderer parity. Their direct image differences are labeled as an unverified
model comparison; the original driver/summary are retained alongside the clarified
summary. In particular, their roughly 0.12 dB mean gap cannot qualify the rasterizer.

A further compatibility hypothesis remains open: the current Metal path caps
splat alpha at **0.999**, matching the tested Brush contract, while the
[original INRIA rasterizer](https://github.com/graphdeco-inria/diff-gaussian-rasterization/blob/main/cuda_rasterizer/forward.cu)
uses **0.99**. Its effect on these imported weights has not yet been isolated.
Original-3DGS interchange fidelity needs that explicit convention audit rather
than inferring correctness from a similar mean ground-truth score.

`/private/tmp/looksy-bonsai-main-v2/receipt.json` and `scene-objects.png` show the
packaged Main Scene rendering the sharp model at **2700×1799**, neutral settings,
no preview readback, with the actual `DSCF5565.png` calibration. The window was
exposed and inactive. The initial v1 fixture marked the calibrated package as an
unposed PLY import and produced its expected overview; it is not held-out-view
proof. The corrected fixture and the final driver's generated project use the
calibrated loader. `/private/tmp/looksy-bonsai-driver-project-v1/receipt.json`
independently passes that generated project's app load/render. These are single
native-frame proofs; sustained latency and the entire interactive editing loop
on this new asset remain unqualified.

### Reproducible reference-package driver

`scripts/verify-gaussian-reference-package.py` validates named camera poses,
right-handed rotations, reference dimensions, focal values, exact comparator
view sets and model immutability. It renders all supplied named views and invokes
the existing native-grid quality tool. Optional `--make-project` creates an
openable calibrated Scene project with a pinned artifact hash and an explicit
external-training origin. It never transforms the input images or modifies the
input model/package. Output must be a new absolute directory.

Example with the already fetched benchmark assets:

```sh
python3 scripts/verify-gaussian-reference-package.py \
  --model /private/tmp/looksy-reference-benchmark-v1/bonsai/point_cloud/iteration_30000/point_cloud.ply \
  --cameras /private/tmp/looksy-reference-benchmark-v1/bonsai/cameras.json \
  --references /private/tmp/looksy-reference-benchmark-v1/bonsai/test/ours_30000/gt \
  --make-project \
  --output /private/tmp/looksy-bonsai-reference-check-new
```

The final driver was checked with a real view: output PNG bytes match the
37-view run exactly, its generated project loads/renders in the packaged app,
and an independent closed-form 90-degree pose agrees. Duplicate names, boolean
focal length, nonorthogonal/left-handed rotations, aspect mismatch and oversized
images, plus mismatched comparator view sets, are rejected before output creation. Receipt:
`/private/tmp/looksy-reference-benchmark-v1/driver-validation.json`.
`/private/tmp/looksy-quality-source-binary-v1.json` pins the final driver, native
renderer/evaluator/app and persisted experimental project identities. The native
runtime binaries and original model/calibration identities remained unchanged
through this audit; the source additions are the reusable driver and this report.
The final real-view PNG is byte-identical to the corresponding 37-view output.
Whitespace checks pass. No working-tree changes were reverted or committed.

This continuation improves an actual trained result and adds matched quality
evidence. Full reference capability still requires the imported-raster convention
audit, representative reconstruction from sharp complete captures, lighting/indirect
integration and sustained native interaction. The current evidence does not close
those gates.

## Opacity-convention audit and integration — October 25

The renderer now exposes **Splat opacity convention** in Scene composition →
Captured scene. Brush remains the default cap of 0.999. Original 3DGS opacity
selects 0.99, matching that part of the [original forward rasterizer](https://github.com/graphdeco-inria/diff-gaussian-rasterization/blob/main/cuda_rasterizer/forward.cu).
This setting covers the opacity cap only. It does not assert equivalence of
CUDA tile admission, clipping, projection, filtering or the complete renderer.

The choice is a global scene setting in artifact-bound composition v8. Older
recipes and default descriptions keep their existing forms. Unknown or malformed
profiles are rejected. The same immutable setting reaches preview, photographic
and tiled rendering, progressive samples, focus picking, reflection/environment
captures and frozen photo/film exports. Resolved-image and probe caches include
the profile. Saved camera recall retains the current scene convention.

Nondefault photo receipts use v5 and film receipts v4, preserving their existing
camera, tone and autofocus data. PLY exports still contain the retained original
rows exactly; their companion receipt records the profile. Reimport restores
the convention only from a recognized receipt matching the copied PLY hash.
It does not reapply the old cleanup mask. Import remains Core-only and publishes
the composition beside the staged artifact, before publishing the new project.

### Measured effect and limits

A paired prototype ablation used identical weights, named poses and pixel grids
for each cap. All cases agreed with the independent scalar calculation within
one channel value at the sampled pixels. PSNR below is against captured images,
not a same-weight CUDA render:

| View | Brush cap | Original cap |
| --- | ---: | ---: |
| Bonsai DSCF5565 | 24.153617 dB | 24.155034 dB |
| Bonsai DSCF5837 | 23.505484 dB | 23.504936 dB |
| Bonsai DSCF5829 | 36.252455 dB | 36.258171 dB |
| Indoor frame_000262 | 36.753798 dB | 36.753798 dB |

Evidence: `/private/tmp/looksy-alpha-ablation-v1/summary.json`. These are small
changes and do not explain the larger public-model reconstruction errors.
No global default or training preset was changed. This is a compatibility
control, not evidence of a general photographic-quality improvement.

The final production integration independently rechecked DSCF5565 with both
conventions. The Brush PNG is byte-identical to the prior benchmark image
(`22e4d5ad993b3b03ba7e3ec798610340db8197c01c706f47aac830e15d1911ae`).
The Original-cap result is 24.155034 dB against capture, with maximum scalar/native
sample error 1 across 192 pixels. Final receipts:
`/private/tmp/looksy-alpha-default-driver-v1/summary.json`,
`/private/tmp/looksy-alpha-driver-v1/summary.json` and
`/private/tmp/looksy-alpha-scalar-final.json`.

The reference driver accepts `--alpha-model brush|original-3dgs`, records the
requested profile, checks the returned profile and includes its artifact-bound
composition in projects made with `--make-project`. The generated Original-cap
project at `/private/tmp/looksy-alpha-driver-v1/project` loads in the packaged app.

### Verification

The desktop bundle and all affected targets build. **17/17 focused tests pass**
in 87.44 seconds, including the new native opacity workflow, the existing
environment and object workflows, legacy/default rendering, controller import,
cleanup integrity, focus, camera paths, materials, lighting and film tests.
Log: `/private/tmp/looksy-alpha-final-ctest-v1.log`.

The independent two-layer oracle produces center `#e50000` for Brush and
`#e51900` for the Original cap because the existing termination threshold treats
the near-opaque second contribution differently. Both caps choose the same
opacity-median focus depth in the focus fixture, while accumulated opacity differs.
Cache invalidation, strict recipe persistence, reflection-probe propagation,
progressive prefix replay and native/tiled output are checked. Tiled versus full
output has maximum code error 1; native texture readback equals the full output.
Receipt: `/private/tmp/looksy-alpha-oracles-v1/receipt.json`.

Native UI verification exercises pointer selection of the actual popup options,
desktop and 1024×760 compact geometry, undo/redo, edits across effect groups,
photo receipts, unchanged PLY rows, a concurrent profile change during film
export and reopen/shot recall. A separate decode of that frozen 12-frame film
agrees with receipt-driven native replay at minimum 46.2098 dB RGB PSNR.
Evidence: `/private/tmp/looksy-alpha-ui-v4/receipt.json` and
`/private/tmp/looksy-alpha-replay-v2.log`. The final focused CTest reruns the
same workflow with its final receipt/check labels. Default and Original-cap
autofocus/tone films independently pass 12 decoded frames each at minimum
41.4258 and 41.4764 dB, respectively. Mixed-cap paths are rejected before encoding.

The packaged Main proof renders all 1,244,819 Bonsai splats with Original opacity
selected and the Captured scene controls visible. Its 3200×1800 native window
capture was visually inspected. The window is exposed but inactive; this is a
single-render/control proof, not an active-interaction or sustained-performance
qualification. Receipt and image:
`/private/tmp/looksy-alpha-main-v1/receipt.json`, `alpha-model.png`.

The final opacity UI workflow also passes on a separate calibrated copy of all
1,244,819 public-model points, including native compact dropdown selection,
reopen, unchanged PLY rows and concurrent edits during film export. Evidence:
`/private/tmp/looksy-alpha-ui-public-v2/receipt.json`, `alpha-compact.png`.

### Dense-scene film compression correction

The first dense-scene film failed the existing 35 dB minimum RGB replay gate:
its worst decoded frame was 33.907256 dB and its mean 34.972467 dB at 256×170.
Saving all twelve expected native frames and encoding them independently with
the existing settings produced **identical decoded bytes** to the exported film.
This isolated the discrepancy to encoding rather than scene, camera, profile
or frozen-composition changes. The failed diagnostic is retained at
`/private/tmp/looksy-alpha-codec-diagnostic-v1/receipt.json`.

The encoder now uses CRF 14 rather than 18, retaining H.264, medium preset,
BT.709 limited-range YUV420 and the existing sRGB transfer tags. Receipts record
the encoder, CRF, preset and pixel format. In the controlled identical-RGB
experiment, worst-frame PSNR rises to **35.433547 dB** and mean to **36.238000 dB**.
The sample movie grows from 24,941 to 39,305 bytes (1.576×). This ratio and the
35 dB pass apply to this sample; neither a universal file-size ratio nor a
perceptual-quality guarantee is implied.

The actual native workflow's new movie is byte-identical to the isolated CRF-14
encode, SHA256 `9253865e35a9a324215562f560035db431c3da92b376b4b28c425dd090bdd25c`.
All twelve decoded frames pass receipt-driven native replay. Evidence:
`/private/tmp/looksy-alpha-codec-isolation-v1/receipt.json` and
`/private/tmp/looksy-alpha-public-replay-v2/receipt.json`.
The replay tool accepts `PROJECT MOVIE --replay-proof-dir NEW_ABSOLUTE_DIRECTORY`
to save expected/decoded PNGs and its per-frame diagnostic without weakening
the existing threshold. This helper does not resolve mesh assets or apply frozen
cleanup masks; the recorded replay runs use untrimmed static scenes with analytic
objects. The final Brush and Original-cap autofocus/tone film
tests retain their changing-focus/contrast coverage and decode above 41.52 dB.

After the encoding change, the rebuilt bundle and all **17/17 focused tests pass
again**, in 83.26 seconds: `/private/tmp/looksy-alpha-final-ctest-v2.log`.
The final bundle's corresponding packaged Main proof is
`/private/tmp/looksy-alpha-main-v2/receipt.json`, `alpha-model.png`.
`/private/tmp/looksy-alpha-source-binary-v1.json` records the Gaussian source,
test, driver, binary, model and evidence identities for this dirty-tree snapshot.

The remaining reference-level gates still include representative source
reconstruction, the rest of the same-weight original-renderer audit, lighting
integration (including splat-cast shadows and spatial indirect illumination),
matched photographic comparisons and sustained native interaction. Full video
capability and quality parity remains open.

## Visible finite-light sources — October 25 continuation

Rectangle and spherical lights now offer **Show source in camera** on the
Lights page. Their one-sided emitting surfaces participate in primary camera
rays, source-to-source and solid depth ordering, front-splat transmission,
depth of field, lens mapping, haze, photographic post processing, focus picking
and centre autofocus. Rectangle emission faces local +Z; a shown back face is
opaque black. Spheres emit outwards, with an opaque black interior. A shown
zero-power source remains a black surface. Hiding the whole light disables its
illumination and primary visibility; hiding only its source preserves lighting.
Existing sources retain their previous camera-hidden default.

The emitting surface is traced directly from the same position, orientation,
dimensions, color and radiance used by the finite-light sampler. It is not a
duplicate scene object, so the existing sixteen-object/eight-light budgets are
unchanged. Camera visibility does not turn the light into a new shadow caster.
The existing splat interaction remains centre-depth compositing, not recovered
surface geometry. Emissive object materials still do not illuminate other objects;
splat-cast shadows and spatial indirect illumination also remain open.

Composition v9 carries an optional strict boolean `cameraVisible` on rectangle
and sphere lights, plus all v8 settings. Unknown fields, wrong types, old versions
with the new field and visibility on point/directional lights are rejected. False
canonicalizes back to the applicable older recipe. Default descriptions remain
unchanged. Undo/redo and artifact-bound project persistence preserve the field.
All QML effect, alpha, environment and shape edits retain the newer recipe.

Photo and film receipts now retain nondefault compositions even when the scene
contains lights without added objects. Both exports freeze source visibility;
changing it during encoding cannot change an accepted film. Saved camera recall
keeps the current global visibility rather than restoring a stale lighting state.

Only actual receivers require stochastic light integration. Visible sources
alone, with captured-color relighting disabled, now trace once per pupil sample;
they neither prepare an unused environment probe nor report fictitious refinement
samples. Added surfaces and captured-color receivers retain the selected lighting
quality. Before/after optimization, both the dense-scene photo and MP4 are
byte-identical:

- PNG: `77129b6530d5759354cf229d8129340907f4d7797248feed0840ad83bde20203`
- MP4: `862a01b0471eeda832c46716ed43c65e4fb55b550508c5cf807f6f65f6757862`

This demonstrates removed work without a pixel change in that workflow; it is
not a sustained-latency or resource qualification.

### Verification and remaining export gate

The debug bundle and affected targets build. **18/18 focused Gaussian tests
pass**, in 102.32 seconds: `/private/tmp/looksy-visible-emitters-ctest-v1.log`.
The new primary-source oracles independently check projected rectangle/sphere
silhouettes, radiance, one-sidedness, zero power, near clipping, nearest-source
ordering, solid occlusion, splat transmission and focus depths. Haze and focused/
defocused optics, cache invalidation, native texture readback, tiled output and
progressive prefix replay also pass. Existing finite-light irradiance and
geometric shadow oracles remain green; showing a source behind the camera leaves
their pixels unchanged. Evidence:
`/private/tmp/looksy-visible-emitters-core-v4/visible-sources/receipt.json`.

Native workflow tests exercise actual pointer toggles for both light types,
whole-light hiding, undo/redo, v9 retention during other edits, source autofocus,
photo receipts, concurrent film edits, project reopen, saved views and compact
controls. They pass with the small fixture and all 1,244,819 calibrated Bonsai
points. Receipts:
`/private/tmp/looksy-visible-emitters-ui-v2/receipt.json`,
`/private/tmp/looksy-visible-emitters-public-v2/receipt.json`.
The small fixture's twelve-frame film passes independent frozen-receipt replay
at minimum 42.4011 dB. The packaged Main proof restores saved autofocus and bloom,
displays the source and controls, and presents the native texture without preview
readback. Its exposed, inactive window capture was visually inspected:
`/private/tmp/looksy-visible-emitters-main-v1/receipt.json`, `visible-emitters.png`.

**The dense bright-source MP4 does not pass the separate 35 dB decoded-frame
gate.** It records minimum 34.416217 dB and mean 35.353531 dB. The failed receipt
and all expected/decoded frames are retained at
`/private/tmp/looksy-visible-emitters-public-replay-v1/receipt.json`.
Re-encoding identical native replay RGB frames with the product's settings
reproduces the exported movie's decoded bytes exactly, isolating this loss to
encoding rather than visibility, focus, camera interpolation or the immutable
composition. Controlled encoder cases:

| Identical native RGB input | Minimum RGB PSNR | Mean RGB PSNR | File bytes |
| --- | ---: | ---: | ---: |
| Current MP4, CRF 14, YUV420 | 34.416217 dB | 35.353531 dB | 41,931 |
| CRF 10, YUV420 | 35.495233 dB | 36.209350 dB | 64,385 |
| CRF 0, YUV420 | 36.883091 dB | 37.138492 dB | 233,422 |
| CRF 14, YUV444 | 37.169859 dB | 38.571946 dB | 42,017 |

Receipt: `/private/tmp/looksy-visible-emitters-codec-v1/receipt.json`.
The existing threshold was not weakened and the compatibility MP4 format was
not changed again for this fixture. A master-export path that preserves rendered
color detail is the next export requirement; the compatibility MP4 cannot be
treated as lossless rendering proof. The 18 passing focused tests do not close
this dense-scene export-quality gate or full reference-quality qualification.

`/private/tmp/looksy-visible-emitters-source-binary-v1.json` pins the source,
binaries, unchanged model and successful and failed evidence for this snapshot.

## Lossless rendered-frame sequences — October 25 continuation

**Export film → PNG frame sequence** now writes a new folder of numbered PNGs
and `camera-path.json`. The format preserves the renderer's existing **8-bit
sRGB RGB pixels**, including output effects, without video quantization or chroma
subsampling. This is a lossless master of that rendered output; it does not add
floating-point HDR, unbaked passes or information already clipped in capture.
MP4 remains available for playback/sharing; its quality is raised to CRF 10 as
verified below.

The export dialog offers the format, a parent folder and a new folder name.
Invalid names are disabled; existing destinations are rejected by the worker.
PNG sequences support 256–8192 pixels on the long edge and preserve odd image
dimensions. MP4 retains its 4096 limit and even raster dimensions. Native tests
exercise the actual format menu and render action, parent/name validation, the
different 4K/8K admission rules and compact pointer/menu bounds. The OS folder
chooser itself is preconfigured by the fixture rather than driven interactively.

Both formats use the **same frozen render loop**: path interpolation, explicit
capture-time mapping, autofocus, physical/lens settings, cleanup, objects,
materials, light-source visibility, tone, film and post processing. Frame time
remains `frameIndex / fps`, beginning at zero and excluding the nominal endpoint.
Sequence export does not require FFmpeg. A timed 4D sequence independently
replays all twelve frozen camera/time samples exactly, despite concurrent cursor
and camera changes. GUI status retains counts rather than copying the complete
per-frame integrity list into every QML snapshot.

The sequence receipt uses `framehouse-gaussian-frame-sequence-v1`, with FPS,
dimensions, zero-based `frame-%06d.png`, cameras, composition, scene-time mapping,
resolved focus trace and per-frame encoded-file and unpadded-RGB SHA256 hashes.
Frames and receipt are closed inside a private sibling staging directory, then
one exclusive directory rename publishes the complete folder. Ordinary failure,
cancellation and destination races expose no partial sequence and preserve other
files/directories. The writer checks cancellation during PNG output as well as
rendering. This is atomic publication, not a power-loss durability or resumable-job
claim.

### Verified quality and interoperability

The previously failing dense bright-source film recipe now exports twelve PNGs
at the **same 256×170 grid**. Every decoded channel equals independent native
replay, with **maximum channel error zero**. The encoded PNG bytes also match
the twelve native reference PNGs retained during the earlier MP4 diagnosis.
The MP4's 34.416217 dB failure remains recorded; the lossless path closes the
rendered-color master requirement without weakening that separate MP4 gate.
Receipts:
`/private/tmp/looksy-frame-sequence-dense-direct-v1/camera-path.json`,
`/private/tmp/looksy-frame-sequence-dense-replay-v1/receipt.json`.

FFmpeg independently admits these files as an image sequence, reads all twelve
at 24 fps, and decodes RGB hashes identical to the pre-encoded render hashes.
Evidence: `/private/tmp/looksy-frame-sequence-interchange-v1.json`.
For an editor/import tool, use the FPS in `camera-path.json` and the zero-based
`frame-%06d.png` pattern. No third-party graphical editor import is claimed.

The native UI workflow also exports all twelve frames from the calibrated
1,244,819-point Bonsai project at 800×533. All decoded pixels and file/RGB hashes
match independent native replay exactly. It verifies a concurrent visibility
change cannot alter the accepted sequence, then reopens the project and recalls
shots with the correct current global state. Evidence:
`/private/tmp/looksy-frame-sequence-public-v1/receipt.json`,
`/private/tmp/looksy-frame-sequence-public-replay-v1/receipt.json`.

The core test covers a 257×193 odd grid, sRGB profile, changing manually replayed
focus, saturated sources, grain/bloom/tone, no encoder dependency, invalid paths,
preflight and observed-frame cancellation, late file/empty-directory collisions
and staging cleanup. **20/20 focused Gaussian tests pass** in 101.86 seconds,
including the existing MP4, scene, materials, lighting and temporal workflows.
Log: `/private/tmp/looksy-frame-sequence-ctest-v1.log`.

The final packaged Main proof displays the sequence dialog over the calibrated
scene with saved autofocus and bloom restored. Its native capture was visually
inspected; the window is exposed and inactive. This is a render/control proof,
with actual export covered by the separate workflow above, rather than a complete
app/catalog/agent or sustained-interaction qualification. Evidence:
`/private/tmp/looksy-frame-sequence-main-v1/receipt.json`, `frame-sequence.png`.

### Maximum-resolution check

The same dense recipe, including its emitters, autofocus and bloom, successfully
renders and publishes all twelve frames at **8192×5459**. The PNGs total
237,961,947 bytes and all encoded-file hashes match their receipt. Publication
was observed only after the full sequence completed. Export receipt:
`/private/tmp/looksy-frame-sequence-8k-v1/master/camera-path.json`.
Full-resolution independent decoded/native replay also passes all twelve frames,
with maximum channel error zero, including every file/RGB hash. Receipt:
`/private/tmp/looksy-frame-sequence-8k-v1/replay/receipt.json`.

The 8K producer's source/binary identity is retained at
`/private/tmp/looksy-frame-sequence-8k-v1/producer-snapshot.json`. A hash check
against the final source proves the PNG implementation and renderer/shader are
unchanged; the production worker's only later difference is the MP4 CRF constant.

### MP4 quality follow-through

The measured bright-source case also justifies raising the standard MP4 export
from CRF 14 to **CRF 10**, retaining libx264/medium, YUV420 and the existing color
tags. The actual product exporter reproduces the controlled CRF-10 movie byte
for byte. All twelve decoded frames now pass the unchanged 35 dB gate:
minimum **35.495233 dB**, mean **36.209350 dB**, versus 34.416217/35.353531 dB.
The sample grows from 41,931 to 64,385 bytes. MP4 remains lossy; maximum local
channel error is 76 in this case, while the PNG path preserves every rendered
channel. Neither a universal size ratio nor a universal MP4 quality bound is
claimed. Evidence: `/private/tmp/looksy-frame-sequence-dense-crf10-replay/receipt.json`
and `/private/tmp/looksy-frame-sequence-dense-crf10.mp4.camera-path.json`.

After that adjustment, **20/20 focused tests pass again**, in 108.16 seconds:
`/private/tmp/looksy-frame-sequence-ctest-v2.log`. The corresponding final
packaged-app dialog/render proof is `/private/tmp/looksy-frame-sequence-main-v2/receipt.json`.
`/private/tmp/looksy-frame-sequence-source-binary-v1.json` pins the final sources,
binaries, unchanged model and qualification evidence, including the earlier
8K producer snapshot.

The broader reference-quality goal remains open for source reconstruction,
lighting/indirect integration, the remaining original-rasterizer audit, matched
photographic comparisons and sustained native interaction.

## Scan shadows and secondary visibility — 2026-10-25

The Lights page now exposes **Scan shadows**, off by default. When enabled,
Gaussian scan particles attenuate direct-light samples and environment-diffuse
samples on inserted analytic/OBJ surfaces. The control is global, undoable and
artifact-bound; composition v10 adds a strictly boolean `scanShadows` field to
the complete v9 settings. Disabling it canonicalizes to the applicable older
schema. Existing descriptions and render behavior remain unchanged by default.
Effect, alpha, finite-source and reflection edits retain the new setting. Photo
and film exports freeze it with the rest of the composition.

Visibility evaluates the maximum anisotropic Gaussian response over the finite
receiver-to-light segment, then multiplies `(1-alpha)` across particles. It
uses the selected .999/.99 alpha cap, a 1/255 per-particle cutoff and 1e-4
transmittance termination. Conservative opacity-dependent bounds accelerate
queries; no screen-space covariance floor or billboard shadow proxy is used.
The analytic maximum-response approach follows the formulation in
[3DGRT, sections 3.2 and 4.3](https://arxiv.org/html/2407.07090v1).
This implementation uses a software Metal BVH, independently of that project's
OptiX renderer, training changes and generalized kernels.

Static loading retains the original scale/rotation square root alongside the
existing raster covariance. This matters for very thin real splats whose float
covariance can become singular. Cleanup preserves the aligned factors. Temporal
queries use the retained conditional factor, translated mean and temporal
opacity at the explicitly selected instant. The index is replaced with the
scene or time and reused across camera, material, light and exposure changes.

These are appearance-opacity shadows. They do not recover calibrated extinction
or remove baked capture lighting. Captured-color relighting still lacks
scan-to-scan self-shadow rays; ambient fill, emission and probe reflections are
unchanged. Environment visibility multiplies the existing single-anchor probe
estimate, so it is not spatial radiance reconstruction or multibounce transport.
Shadowed volumes and photographic reference parity remain open.

### Verification and limits

**22/22 focused tests pass**, in 108.20 seconds:
`/private/tmp/looksy-scan-shadows-ctest-v1.log`. The new Metal test checks
independent single-opacity, overlap, correlated-covariance, finite light-distance,
thin-factor, temporal motion/fading and both alpha-cap oracles. It also checks
environment attenuation, area-source partial visibility, exact tiled output,
observed GPU-batch cancellation without partial publication, empty-index
replacement and factor alignment after cleanup. A native SceneWorkspace test
checks pointer toggles, undo/redo, effect retention, frozen photo/film recipes,
reopen/shot recall and compact-window interaction. Its ordinary fixture does not
establish dense-scene performance.

The dense reference run exposes a remaining responsiveness limit. At a
2700×1799 Retina preview, combining scan shadows with 16-sample environment
illumination exceeds the existing 20-second per-action wait. Three attempts
retain their failure evidence under
`/private/tmp/looksy-scan-shadows-public-v1` through `public-v3`.
Centroid-based partitions, nearer-first traversal and adaptive submissions do
not close this gate. Adaptive submissions preserve an upper bound of 65,536
pixels and four visibility samples per command; the measured-time target is
8 ms, not a guaranteed GPU-latency bound. Export resolution and sample counts
have not been lowered to make the workflow pass.

`/private/tmp/looksy-scan-shadows-source-binary-v1.json` pins the relevant
Gaussian sources, native test binaries and packaged application. It explicitly
records that the dense Retina 20-second gate and full video parity are open.

With an explicitly extended **120-second per-action offline wait**, the dense
native workflow completes, including photo/film export, a concurrent global
lighting edit, reopening, shot recall and compact pointer toggles:
`/private/tmp/looksy-scan-shadows-public-v4/receipt.json`.
The retained final 2048×1364 frame takes **50,745.77 ms** (50,265.90 ms GPU),
despite reusing the visibility index; its largest lighting command is 36.37 ms.
This is completion evidence, not a responsiveness pass. The exported film has
twelve 256×170 frames at 24 fps with the expected color tags, retaining scan
shadows and environment strength .7 despite the concurrent edit to .2.
Independent metadata receipt: `/private/tmp/looksy-scan-shadows-interchange-v1.json`.
The 800-pixel photo visibly shows the captured bonsai's shadow on the inserted
sphere; no matched photographic-quality improvement is inferred from that
demonstration.

The final-source direct-light benchmark and independent oracles are retained at
`/private/tmp/looksy-scan-shadows-dense-v5/receipt.json`. All oracles pass; maximum
absolute transmittance-ratio error is **0.006393**. The full 1,244,819-point
artifact produces an index with 1,127,439 admitted particles, 419,869 nodes and
92,309,808 bytes. Cold construction/upload takes 2645.73 ms. Twelve 800×533
native camera renders reuse it, with median **64.50 ms**, maximum **73.15 ms**.
25,180 pixels change between the paired direct-light off/on images. The separate
environment workflow above demonstrates why these bounded direct-light timings
do not qualify interactive global illumination.
The complete diagnostic process peaks at 1,030,930,432 bytes resident, including
loaded model/factors, GPU resources and temporary construction allocations;
this is not steady-state index memory. Timing log:
`/private/tmp/looksy-scan-shadows-dense-v5-time.log`.

The final packaged Main application also renders the dense scan and its cast
shadow at 2700×1799 with direct lighting (environment disabled in a separate
private input project). Its exposed window and object inspector were visually
checked; no preview readback occurs. The recorded render call is 3505.99 ms,
including cold setup. Receipt and screenshot:
`/private/tmp/looksy-scan-shadows-main-v1/receipt.json`, `scene-objects.png`.
This scoped Scene check leaves catalog/agent and sustained-interaction
qualification false. The window is exposed but inactive.

The earlier frozen bright-source lossless master also still replays all twelve
frames with maximum channel error **zero** after the renderer/data changes:
`/private/tmp/looksy-scan-shadows-legacy-replay-v1/receipt.json`.

A lossless sequence exported from the dense frozen shadow-enabled recipe also
passes independent decode/native replay for **all twelve frames**, with maximum
channel error **zero** and matching file/RGB integrity hashes. It retains the
frozen .7 environment strength while the reopened project's global value is .2.
Master: `/private/tmp/looksy-scan-shadows-sequence-v1/camera-path.json`;
replay: `/private/tmp/looksy-scan-shadows-sequence-replay-v1/receipt.json`.
This 256×170 check verifies integration and lossless publication; it does not
extend the earlier 8K proof to shadow-enabled 8K rendering.

## Native visibility acceleration — 2026-10-25

The dense environment-light performance limit above is now closed on the
measured M4 Pro through native Metal acceleration structures. The unchanged
2048×1364, 16-sample recipe improves from **50,539.27 ms to 365.41 ms** warm
rendering, with maximum RGB difference 1/255 in a single channel of one pixel
across three paired images. Peak process RSS rises 3.71%; the retained index
grows from 92.31 MB to 173.05 MB. The software path remains available and tested.

**23/23 focused tests pass**. The actual dense native workflow also passes its
original **20-second action wait**, including export/reopen/recall and compact
pointer interactions, without lowering resolution or samples. Its retained
final frame takes 375.68 ms. This closes that action gate on this host, not
sustained interactive or photographic reference qualification.
See the [controlled comparison and remaining limits](gaussian-scan-visibility-performance-2026-10-25.md).

## Spatial reflections — 2026-10-25

Added surfaces now have an optional **Spatial rays** reflection method. Rays
start at each shaded surface, preserve anisotropic captured opacity and
distance/source ordering, evaluate reflected SH/temporal appearance and sample
rough GGX reflections. Reflected analytic/OBJ materials and UV color textures,
finite emitters and distance haze are supported. Composition v11 persists the
method and freezes it into photo/film exports; the probe remains the default.

**28/28 focused tests pass**. Native and software traversal match all pixels of
27 independent rendered cases, with zero channel error. The full dense native
workflow passes with an explicitly offline **120-second action wait**, including
exports, a concurrent global mode edit, reopening, recall and compact pointer
selection. New and retained legacy lossless masters replay all twelve frames
exactly. These checks do not establish photographic reference parity.

Controlled traversal changes reduce the unchanged 2700×1799, 16-sample spatial
recipe from **88,969.23 ms to 34,764.35 ms** at the measured warm pose, with all
three paired images exactly equal. **The 20-second spatial action gate remains
open.** The prior native visibility recipe stays at 364.16 ms warm with identical
pixels and stable peak RSS. Spatial diffuse radiance, recursive mirrors,
emissive indirect illumination, shadowed volumes, reconstruction quality and
sustained native interaction remain incomplete. See the
[implementation, evidence and physical limits](gaussian-spatial-reflections-2026-10-25.md).
The final packaged Main application also misses its unchanged 30-second Scene
render/control proof while the initial spatial frame is refining; this is not
a passing integrated Main render. Evidence and source/binary identities are
retained in `/private/tmp/looksy-spatial-source-binary-final-v1.json`.

## Spatial reflection heap and render activity — 2026-10-25

The later bounded max-heap change preserves every pixel of the frozen dense
recipe while reducing measured warm rendering from **35,174.30 ms to
16,815.92 ms** at unchanged 2700×1799 resolution and 16 samples. Peak RSS rises
0.55%; the legacy visibility recipe stays at 364.50 ms with exact pixels.
**28/28 focused tests pass**; all pixels of 28 paired native/software oracle
images match, including shuffled-depth retained-buffer replacements. New and
retained twelve-frame lossless masters replay exactly.

The full native dense workflow now passes its original **20-second action
wait**, including frozen exports, concurrent edits, reopen/recall and compact
pointer controls. Its final 2048×1364 frame takes 8879.74 ms. The packaged Main
app also passes its unchanged **30-second proof** in active and explicitly
background-launched exposed windows, with 2700×1799 spatial frames, after a
scoped user-initiated render activity is added. Idle system sleep remains
allowed; the activity ends with the render. These close the earlier measured
action/render gates on this host, not sustained interaction or full video parity.

See [controlled measurements, power-activity scope and remaining limits](gaussian-reflection-performance-2026-10-25.md).
Final source/binary/evidence identities:
`/private/tmp/looksy-spatial-heap-source-binary-final-v1.json`.
Spatial diffuse radiance, recursive/multiple-bounce transport, emissive indirect
illumination, shadowed volumes, reconstruction quality, matched photographic
comparisons and sustained native interaction remain open.

## Spatial diffuse illumination and authored emission — 2026-10-25

The next lighting stage adds **Lights → Scan illumination → Spatial rays**.
Added surfaces gather captured Gaussian radiance from their actual positions,
including anisotropic opacity, distance/source ordering, SH, temporal appearance
and haze. Rays can reach one added surface and receive its direct light, ambient
fill and material emission. Authored emissive objects can therefore illuminate
other added surfaces. They still do not relight the captured scan. Further
indirect bounces, recursive mirrors and refraction remain open.

Illumination and reflection methods are independently selectable. Composition
v12 persists both and freezes them into photos/films; probe remains the default.
Finite analytic emitters use the direct-light estimator, while material emission
uses cosine rays, avoiding duplicate emission. Secondary surfaces do not silently
use the shared diffuse probe in this mode.

**31/31 focused tests pass**. Native and software traversal each pass twenty
independent rendered lighting cases with maximum channel error zero, including
both alpha models, spatial/mixed methods, SH/time, emission, a directly lit
secondary surface, haze and finite-source double-counting. Tiling and observed
GPU-batch cancellation also pass. A native A/B visibly transfers blue light from
an unchanged emissive object onto a gray sphere and floor; its editable recipe is
retained. These are mathematical and integration checks, not matched reference
photographic quality.

A bounded dispatch optimization reduces the unchanged **2700×1799, 16-sample**
dense diffuse recipe from **45,162.56 ms to 32,006.59 ms** warm, with all paired
images exactly equal. Peak RSS rises 3.77%; a second shader-specialization
experiment misses its incremental improvement gate and is reverted. Existing
spatial reflections retain exact images at 17,097.96 ms warm, within their 10%
guard. Legacy visibility retains exact images at 375.69 ms warm, also within its
guard.

**The dense diffuse 20-second action target and packaged Main 30-second check
remain open.** An explicitly labeled offline Main check completes in 42,345 ms,
with a 32,636.67 ms full-resolution render and no preview readback. It records
`timingGateQualified=false`; ordinary proof keeps its 30-second limit.
Higher-sample and combined dense methods, shadowed volumes, emission lighting
the captured scan, reconstruction/sharpness, matched photographic integration
and sustained interaction still require work. Full video parity remains false.

See [implementation, evidence and timing limits](gaussian-spatial-diffuse-2026-10-25.md).
The accepted build completes the dense native workflow with the explicit
120-second offline wait, including photo/film freezing, global edits,
reopen/recall and compact pointer controls. A new lossless twelve-frame master
and three retained masters replay exactly. The existing spatial-reflection
recipe also retains its ordinary packaged Main 30-second pass. These do not
close the new diffuse responsiveness gates. Final source/binary/evidence
identities: `/private/tmp/looksy-spatial-diffuse-source-binary-final-v1.json`.

## Repeated surface paths — 2026-10-25

**Surface bounces** now selects one through eight secondary surfaces for the
spatial methods. Added surfaces can reflect other reflective surfaces and receive
indirect light through chains of added surfaces. Each reached vertex shades
direct light, ambient fill and emission, including OBJ materials/UV color maps.
Depth one retains the previous behavior; higher depths use a distinct linear
radiance/opacity path pipeline. Captured lighting through translucent regions can
therefore also change when selecting multiple bounces. Primary rasterization and
legacy one-surface descriptions/compositing remain unchanged.

The path chooses one GGX/cosine lobe per vertex, compensating its selection
probability, with distinct sample stratification at each bounce. Finite source
emission is partitioned between direct and continuation estimates. Composition
v13 persists the strict 1–8 depth and freezes it into photo/film recipes. Shot
recall retains current global depth. The ordinary native fixture passes pointer
selection, undo/redo, effect retention, concurrent global edits, export freezing,
reopen/recall and compact depth selection with its original 20-second action wait.

Native and software traversal each pass **19 independent rendered checks**,
with maximum channel error **1/255**. These include analytic diffuse/mirror
cavities through eight surfaces, mixed BRDF angular quadrature, per-segment haze,
captured opacity and finite-source partition. All nineteen retained case/demo
image pairs match exactly across backends. A native depth-one/four A/B visibly
adds further blue emission transfer to the sphere/floor. This is mathematical
and integration evidence, not photographic reference parity or dense interaction
qualification.

The depth limit is finite, with no transmission, MIS, Russian roulette or
denoising. Captured Gaussians remain a radiance/visibility field; added emission
does not relight the scan or remove its baked lighting. Shadowed volumes,
reconstruction detail/coverage, photographic integration and sustained native
interaction still require work. Full video parity remains false.
See [transport, compatibility and evidence](gaussian-surface-bounces-2026-10-25.md).
The desktop bundle builds and **34/34 focused tests pass**. The dense native
workflow completes with its explicit 120-second offline action wait, and the
exposed/inactive packaged Main app renders four bounces at 2700×1799 and 16 samples
in 34,890.04 ms, without preview readback. This retains
`timingGateQualified=false`; dense diffuse responsiveness remains open.
One new v13 and three retained twelve-frame lossless masters replay exactly.
All retained default comparison images remain identical and diffuse/reflection
cold/warm/RSS guards pass. Legacy visibility warm/RSS guards pass, but one of
three cold-start measurements misses its 10% guard; that miss is retained and
cold-start stability remains unqualified. Evidence identities:
`/private/tmp/looksy-surface-bounces-source-binary-final-v1.json`.

## Progressive lighting feedback — 2026-10-25

Scene now publishes immutable completed lighting prefixes while the unchanged
full-quality solution refines. The status shows the displayed sample count.
During navigation it can publish a useful live prefix before canceling for a
newer camera pose. Photo and film exports retain the complete frozen recipe.

For the identical 2700×1799 four-bounce, 16-sample dense recipe, first GPU queue
availability improves from **32.46 s to 1.83 s**. Two unchanged eight-sample
720×480 live poses improve from 844/815 ms to **154/127 ms**. All three final
images match the baseline exactly. Full completion is **32.98 s**; this does
not close the dense 20/30-second full-quality gates. Process RSS passes its
guard, while broader macOS footprint increases **17.85%**.

These are queue measurements. The final focused suite passes 37/37 tests. A
retained small-fixture foreground trace records 123 ms first visible prefix and
245 ms first held live frame with 20 changing poses. Earlier unavailable and
slow observations are retained; sustained native input-to-photon qualification
remains open. A provisional frame is not a final
lighting solution or proof of photographic equivalence. Full video parity
remains false. See [implementation and measured limits](gaussian-progressive-lighting-2026-10-25.md).

The dense functional workflow passes with the explicit offline action wait,
showing a first prefix at 795 ms and 11 changing live partial frames in an
inactive window. Packaged Main captures a prefix at 3.60 s in its ordinary
run but still fails full completion at 30 s. Its explicit offline check passes
at 40.95 s elapsed / 32.28 s render, retaining `timingGateQualified=false`.
The legacy Main repeat passes at 26.67 s; an earlier run times out while still
navigating, so repeatability remains unqualified. One new and four retained
lossless masters replay all **60 frames exactly**. Final evidence identities:
`/private/tmp/looksy-lighting-progressive-source-binary-final-v1.json`.

## Optional sharper video-frame selection — 2026-10-25

Scene's video setup now offers **Prefer sharper frames**, off by default. It
ranks three 320-edge proxies per interval, then extracts the chosen full-size
source frames with the unchanged SDR color conversion. Every interval contributes
one frame. The new provenance receipt retains all scores/proxy hashes, selected
indices, approximate times, final PNG hashes and the source hash. Uniform sampling
retains its previous behavior. The selected mode persists through the controller
and saved project.

Independent synthetic source-pixel checks, deterministic ties, cancellation after
proxy writing begins, and actual native checkbox/Create pointer input pass. The
compact capture form also fits correctly after hiding the current scene's editing
rows during new-capture setup. **6/6 focused checks pass**, and the desktop builds.

The existing indoor clip yields all 160 requested frames in both modes at a
1600-pixel edge, with equal source hashes; selection changes 107 intervals.
Inspected frames still show motion blur and viewpoint/content changes. Focus
scores do not establish improved geometry, alignment or photographic quality.
No new trained-model quality gain is claimed; the option remains off by default.
See [selection behavior, evidence and remaining experiment](gaussian-video-frame-selection-2026-10-25.md).

A separate three-pose original-raster CPU interpretation audit finds only 1–2
code differences at its 192 sampled pixels per pose. It is not CUDA execution or
complete original-renderer parity. Product rasterization remains unchanged.
Evidence identities: `/private/tmp/looksy-sharp-video-source-binary-final-v1.json`.

## Frame-selection alignment validation — 2026-10-25

The first paired production preparation/COLMAP run fails the selection-improvement
hypothesis: uniform connects **10/160 views**, selected connects **3/160**. Both
fail the unchanged 80-view coverage gate; training never starts. The selected set
has more features and verified correspondences, which does not establish useful
geometry. Recovered calibration and interval coverage differ, so no cross-pose
photographic metric is reported.

The option remains off by default and is labeled experimental. Its low-coverage
message now suggests comparing uniform sampling. Three focused controller/native
UI checks pass and the desktop builds. The new diagnostic replay and comparison
tools retain hashes and component/coverage evidence. Source fidelity, useful
capture coverage, trained detail and full video parity remain unproven.
See [the failed hypothesis and evidence](gaussian-video-alignment-2026-10-25.md).

## Object emission received by captured colors — 2026-10-25

**Captured scene → Receive object emission** now lets authored glowing spheres,
boxes and imported OBJ material emission illuminate the scan in **Light captured
colors** mode. The option is off by default. Cosine-sampled authored emission is
added to existing captured-color lighting, with authored-geometry blocking,
optional captured-opacity visibility, stable receiving-particle exclusion,
shadow-strength blending and emitter-segment haze. Explicit lights retain their
separate estimator. Composition v14 freezes this setting into still/film recipes;
disabled recipes retain the existing applicable schema.

Independent native/software checks pass 17 rendered cases each, with maximum
channel error 0/255; all retained native/software PNGs are byte-identical. Native
SceneWorkspace pointer input, undo/redo, mode/sample/effect retention, concurrent
export edits, global reopen/shot recall and compact controls pass on both the
small fixture and the calibrated 1,244,819-point Bonsai scene. A separate dense
workflow enables scan shadows with an off-camera source, isolating received
lighting in its on/off photographs. Both keep the original 20-second action wait.

The focused suite has **41 passes, no failures and one foreground-activation
skip** out of 42 checks. The frozen packaged Main app renders the new dense
scan-shadow/emission recipe at 2700×1799, 64 samples, in 2418.34 ms; its unchanged
30-second Scene render/control proof passes in 11,321 ms with an active/exposed
window. A new full-resolution twelve-frame PNG master independently replays
exactly. This qualifies that recipe and workflow, not sustained interaction or
full photographic integration.

Captured normals/materials remain approximate and original lighting stays baked
in. Small sources can need more samples; light importance sampling, MIS,
denoising, captured indirect surface paths and shadowed scattering are absent.
Capture alignment/detail, matched photographic integration, broader dense
lighting performance and sustained foreground interaction remain open. Full
reference-video parity remains false. See [transport, evidence and qualification
limits](gaussian-capture-emission-2026-10-25.md).

The new 2700-edge and 256-edge shadowed masters plus five retained lighting
masters replay **84/84 frames exactly**. Scoped source/binary and evidence
identities: `/private/tmp/looksy-capture-emission-source-binary-final-v1.json`.

## Small-source emission convergence and probe quality — 2026-10-25

**Object emission sampling → Target emitters** now combines conservative emitter
bounds with cosine rays, accounting for proposal overlap and concentrating seven
of eight strata on small bounds. New reception enables use this method; explicit
method preferences persist in composition v15. Existing v14 recipes keep their
cosine behavior. New v15 probes honor final sample quality and invalidate stale
draft probes; the old rule used one sample without an explicit soft light.

Twenty analytic/quadrature cases pass on native/software traversal, maximum
channel error 1/255. Across sixteen receiver seeds at 8/16/64 samples, a small
sphere matches its analytic brightness at every budget; a small imported panel
meets the two-code target at default 64 samples while retaining noisier drafts.
All 32 retained traversal-backend PNGs match byte-for-byte. Native selection of
both methods, history, frozen exports and reopening pass. The focused suite
passes 44/44 checks without skips; its existing foreground trace is a separate
small older recipe, not a dense targeted-lighting latency proof.

The dense scan-shadowed film misses its original 20-second action wait at four
of twelve frames. A separate 120-second offline workflow passes and retains
`timingGateQualified=false`. The frozen packaged Main app passes its unchanged
30-second Scene render/control gate in 17,838 ms, rendering 2700×1799 at 64 samples
in 8547.17 ms with an active/exposed window. This improves small-source
convergence, with a measured dense visibility cost; it does not close responsive
film, combined-method performance, capture-quality or photographic-integration
gates. See [estimator, negative observations and evidence](gaussian-emission-sampling-2026-10-25.md).

Both new v15 masters (2700-edge and UI 256-edge) plus seven retained masters
replay **108/108 frames exactly**. Final scoped identities:
`/private/tmp/looksy-emission-importance-source-binary-final-v1.json`.

## Capture-lighting reuse across probe faces — 2026-10-25

Probe cube faces now reuse one completed captured-lighting field at their shared
observer position. A conservative scene/composition/position/time/alpha/quality
and batch-grouping key protects reuse; canceled partial writes clear validity.
The existing illumination buffer is retained, with no shader, sample-count,
recipe-version or scene-sized allocation change.

The first bounded hypothesis passes its declared timing, memory and exact-pixel
gates on the identical 1,244,819-point reflective-object/targeted-emission/scan-
shadow recipe. Full completion improves **31,272.58 → 9383.56 ms**; first live
completion improves **4035.07 → 1347.55 ms**. The second live pose stays at about
571 ms. All three final and three progressive PNGs are byte-identical; RSS and
Darwin peak footprint remain within their guards. These are renderer queue
observations, not input-to-photon latency.

Native/software tests pass 34 exact fresh-renderer state comparisons, including
observer hemisphere and isotropic fallback, quality/batch changes, materials,
scene/time and interrupted updates. The combined dense native workflow passes
with its explicit offline export wait. The pre-cache packaged app misses its
30-second combined-scene gate; the optimized frozen app passes in **23,360 ms**,
2700×1799 at 64 samples, exposed but inactive. This closes that Scene deadline,
not foreground interaction or the prior dense-film timing gate.

Capture fidelity, photographic integration, broader dense lighting and sustained
interaction remain open. See [controlled loop and qualification scope](gaussian-capture-lighting-cache-2026-10-25.md).

The final suite has **46 passes, no failures and one foreground-activation skip**
out of 47 checks. A new combined master and nine retained masters replay
**120/120 frames exactly**. Final scoped identities:
`/private/tmp/looksy-capture-lighting-cache-source-binary-final-v1.json`.

## Photo calibration and ambiguous matches — 2026-10-25

Photo capture now offers separate, default-off experimental choices for shared
estimated camera calibration and stricter feature matching. Contradictory camera,
lens, focal, format and pixel-dimension evidence rejects the shared option.
The ordinary EXIF grouping also fixes a defect that split consecutive shots by
their capture timestamps. Existing camera/lens/zoom/format separation remains.

On a frozen training-only 32-photo subset, the production candidate retains
31 registrations and improves median focal error **13.77% → 0.90%**, median
rotation error **9.79° → 2.54°**, and median normalized center error
**0.578 → 0.036**. It still misses the unchanged 2° pose target and retains one
badly constrained camera. A separate direct CLI run passes the median gates but
also retains position outliers; repeatable quality acceptance remains open.
Guided matching and a tighter absolute-pose threshold failed their experiments
and are not enabled for the photo option. No new model was trained, and no
held-out photographic-quality or private-video gain is claimed.

The desktop builds, 10/10 focused checks pass, and native pointer/Create proofs
at desktop and compact sizes pass with inspected captures. The next quality
task remains weak-view reconstruction, followed by training and independent
held-out comparison. Full video parity remains false. See
[photo calibration experiment and retained failures](gaussian-photo-alignment-2026-10-25.md).

Further source-quality experiments identify ambiguous planar-surface matches in
the sparse photo set. Affine features and planar-link/bootstrap trials fail the
combined geometry and coverage requirements and remain outside product defaults.
A denser 96-photo production capture test now excludes all 37 named reference
photos from reconstruction and passes all frozen camera-quality gates: 96/96
views, 0.218° median / 0.385° maximum rotation error and 0.0908% median focal
error. The first 5000-step reconstruction fails the external photographic
targets (17.39477 dB / 0.413328 mean SSIM). Investigation identifies an additional
photographic develop applied to already-rendered capture inputs. Preparation now
preserves processed-image appearance through ICC-to-sRGB conversion, and a
controlled corrected-color retraining improves the same external views to
22.02049 dB / 0.649668 mean SSIM. It still fails the absolute photographic
targets. Increasing the fixed-dataset budget to 15,000 steps / one million
splats does not resolve this: 22.07054 dB / 0.645735 mean SSIM, with worst-view
PSNR regressing to 18.54281 dB. That schedule is not promoted; fresh corrected
alignment is a separate ongoing experiment. Finished-scene qualification remains
separate from the alignment result.
See [source-capture experiments and current limits](gaussian-source-capture-quality-2026-10-25.md).

## Freehand cleanup and capture color — October 25 continuation

The Clean scene panel now supports freehand lasso removal and keep operations.
The native dense 1,244,819-splat workflow passes shape selection, concave strokes,
Escape cancellation, undo/redo, PLY/photo export, reopening, compact-window keep
input and restore-all. Both shapes additionally support explicit camera-forward
Near/Far intervals, with native layer identity, undo/reopen, compact input and
export checks passing. All depths remains the default. Automatic visible-surface
selection and component transforms remain open. See
[lasso behavior and native evidence](gaussian-lasso-cleanup-2026-10-25.md).

The capture-color regression covers sRGB and Display P3 PNG/JPEG/TIFF sources,
and timed-capture validation retains the earlier dataset contract. The corrected
96-photo training preserves the accepted sparse geometry and source identities;
it is a color-only experiment rather than a fresh integrated capture proof. See
[capture color preservation and quality evidence](gaussian-capture-color-2026-10-25.md).

Fresh production reconstruction from corrected photos retains 96/96 cameras,
improves median rotation error from 0.21763° to 0.09036°, and improves all 37
external views at the same 5000/500k schedule. Mean quality reaches 24.94575 dB /
0.775294 SSIM, with 20.01140 dB worst-view PSNR; all absolute photo targets still
fail. A complete 255-photo production run uses frozen tool copies and excludes
the same external reference photos. That run has now completed with 255/255
cameras and all three frozen photographic targets passing: mean 30.02280 dB /
0.915408 SSIM and worst-view 22.83792 dB. Every external view improves in both
metrics. This is a one-dataset integrated result; broader capture quality and
finished-scene parity remain open. See [fresh reconstruction and full source coverage](gaussian-fresh-capture-quality-2026-10-25.md).

## Shadowed haze and independent PLY import

Opt-in shadowed haze now integrates explicit source visibility along captured,
solid, background and reflected ray segments. Eighteen independent numeric cases
pass with native Metal and software traversal, together with the real native
checkbox, persistence and frozen photo/film workflow. The dense 830,842-splat
diagnostic initially takes 46.82 s at 640-pixel width with scan occlusion. A
bounded follow-up reduces a fresh 46.80 s baseline to **574.40 ms** with all image
guards passing. The dense native editing/export workflow passes, and twelve
film frames replay exactly with the frozen sequential renderer. Sustained
foreground interaction and broader photographic integration remain unqualified.
See [haze model and evidence](gaussian-shadowed-haze-2026-10-25.md).

The [haze performance report](gaussian-haze-performance-2026-10-25.md) retains all
three attempts, including the failed dense UI timing checks before the final
parallel field implementation. Export sample counts and grid spacing are
preserved; native angular sampling has its own cache identity.

The 1,203,946-point cleaned SH3 PLY also renders in an independent local
PlayCanvas viewer with unchanged sampled attributes and exact camera-reset PNGs.
This closes the demonstrated PlayCanvas interchange case, while C4D/Octane
compatibility remains unverified. See [independent PLY import evidence](gaussian-ply-interop-2026-10-25.md).

## Captured-color shadow receiver

Receive shadows now lets authored objects shadow the scan while retaining its
unoccluded photographed colors. It uses the ratio of received to unoccluded
explicit lighting, with separate finite-source accumulation before division.
The native synthetic and dense workflows pass mode selection, persistence,
undo/redo and frozen film export. Both twelve-frame sequences replay with zero
RGB error, and all four prior staged captured/relit images remain byte-identical.
The real staged scene's unoccluded background is also byte-identical to its
neutral capture. The control is present in the rebuilt desktop target.

These are integration and correctness results. The diagnostic object placement
still needs artistic refinement, and the proof windows were exposed but inactive.
Finished-scene quality, active-foreground performance and full reference parity
remain open. See [shadow receiver contract, evidence and limits](gaussian-shadow-receiver-2026-10-25.md).

## Imported-object finished-shot integration

The real reconstruction now has a tabletop copper-torus/chrome-sphere composition,
an inspected camera move, a combined depth-of-field/haze/tone/halation still, and
an actual packaged Scene capture with editable object controls. Film replay now
supports SHA-bound imported geometry/materials and reproduces all twelve mesh
frames exactly, including after the current recipe is deliberately changed and
then restored. Ordinary project opening and frozen replay share the same asset
integrity checks and memory limits.

The combined still takes approximately 75.30 seconds at 960x639 in this session;
it is an export-quality result, not realtime qualification. Its reflections use
the environment probe; spatial integration is inspected separately. The full
combined film, broader motion/contact/convergence and active-foreground gates
remain open. See [finished-scene artifacts, replay evidence and limits](gaussian-finished-scene-2026-10-25.md).

## Combined optical haze scheduling

A frozen 960x639 render of the combined object/lens/haze/finishing scene improves
from 76.62 seconds to 32.36 seconds with a byte-identical PNG. The accepted change
only groups independent work into larger bounded GPU submissions; 64 aperture
samples, 128 lighting samples, field spacing, depth strata and tile layout remain
unchanged. The initial 19.9% attempt misses the fixed 20% target and is retained
as rejected; the second attempt and confirmation pass all image, GPU and memory
guards. Eleven focused tests and both native workflows pass unchanged. The full
combined twelve-frame film also exports and replays with zero RGB error, retaining
all lens, atmosphere, geometry and finishing settings; its first frame matches
the baseline still exactly. The tested path is 0.5 seconds and 1.5 degrees. This
is an export/integration result, not larger-motion/convergence or sustained
foreground-interaction qualification.
See [optical haze experiment and evidence](gaussian-optical-haze-performance-2026-10-25.md).

## Real private-capture reconstruction check

The older 64-photo SUV/location capture confirms the legacy preparation issue:
current source-preserving inputs differ from the old prepared images by 31.25
codes on average. Old native versus saved trainer rendering agrees at 71.85 dB,
locating the large visible softness upstream of native display at that pose.
A strict shared-camera alignment loses coverage (53/64) and is not trained.
Standard matching retains all 64 views with one estimated camera and completes
a matched-budget 10,000-step/one-million-splat production reconstruction in a new
temporary project.

The rebuilt capture still fails every frozen photographic target: 19.30561 dB /
0.588564 mean SSIM, 14.87833 dB worst-view PSNR. Its native/trainer agreement is
70.55 dB at the inspected camera, and foreground streaking/ghosting remains in
both. All pinned original files remain unchanged. This failed quality result is
retained alongside the public Bonsai pass, not replaced by it. See
[private-capture baseline, full run and rejected quality gates](gaussian-private-capture-quality-2026-10-25.md).

## Wider Bonsai finishing move — October 26

Following the user's direction to deprioritize the sparse SUV capture as the
finishing benchmark, the accepted 255-photo Bonsai composition now has a separate
three-second, twelve-degree camera move. It retains 960x639 pixels, 64 aperture
samples, 128 lighting samples, imported geometry/materials, received shadows,
probe reflections, shadowed haze and finishing settings. All 72 production PNGs
export successfully; their file/RGB hashes verify, and the opening/midpoint/ending
frames match separate native renders exactly. Selected additional viewpoints
were inspected. The editable project, PNG master and MP4 viewing copy are retained
under `build/scene-deliverables/bonsai-motion-2026-10-26/`.

Six focused native tests pass. The packaged Scene workspace loads the new project
and its saved path, with the window exposed but inactive. A combined spatial
reflection/diffuse experiment at the same lens/haze settings is canceled after
9 minutes 48 seconds without a completed first image; it is not a photographic
quality failure or a completed performance benchmark. Wider probe-lit motion is
demonstrated, while combined spatial/lens performance and convergence,
photographic integration and sustained foreground interaction remain open.
See [wider motion, full-sequence verification and retained deliverable](gaussian-bonsai-motion-2026-10-26.md).
