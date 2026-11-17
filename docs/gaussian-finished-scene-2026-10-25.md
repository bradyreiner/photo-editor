# Imported-object composition and frozen film replay

The real 830,842-Gaussian reconstruction now has an editable composition with
an imported copper torus and chrome sphere placed on upper cloth patches. A
combined shot adds a tighter camera, depth of field, received shadows, captured
environment reflections, shadowed haze, tone and subtle halation. This provides
an integrated visual result; full reference-video parity remains open.

## Placement and visual inspection

Evidence root: `/private/tmp/looksy-finished-scene-v1`.

The initial composition placed objects on the hanging front cloth. Surface
probes now report opacity-median depth at candidate image coordinates and estimate
a local plane from neighboring picks. Some candidates select the distant bicycle
at the table edge, so successful depth picking alone is not sufficient placement
evidence. The accepted anchors are `(0.44, 0.60)` for the ring and `(0.72, 0.56)`
for the sphere in the first captured camera. Their estimated normals point
upward in the image, with dot products 0.977 and 0.947 against camera up.

The sphere center is offset from its anchor by its radius along the local normal.
The torus translation uses its lowest transformed mesh vertex against that plane.
These are approximate supporting patches, not reconstructed collision surfaces.
The tabletop still, last film frame and actual packaged application capture were
inspected. The objects retain their placement over the tested 1.5-degree move.
Larger moves and additional surfaces remain unqualified.

Important artifacts:

- `surface-probe/surfaces.json` and `surface-reference.png`: inspected candidates.
- `tabletop-v1/finished.png`: pinhole composition with received shadows.
- `tabletop-spatial-v1/finished.png`: separate spatial-reflection/diffuse rendering.
- `tabletop-film-v1/frames/`: twelve 960x639 frames from the camera move.
- `beauty-v1/finished.png`: combined optical/atmosphere shot.
- `beauty-v1/project/`: editable geometry, composition and saved cameras/path.
- `packaged-tabletop/scene-objects.png`: actual desktop Scene workspace with the
  torus selected and editable transform/material controls visible.

The combined shot uses 64 aperture samples and 128 lighting samples, manual focus
between the object anchors, aperture radius 0.012 times focus distance, 1.4 focal
scale, shifted principal point, haze density 0.02 divided by scene focus distance,
contrast 1.04, highlight compression 0.08 and halation amount 0.04. The exact
camera and composition were written to `beauty-v1/plan-before.json` before rendering.
That shot uses environment-probe reflections; spatial rendering was inspected in
the separate pinhole case. It is not a combined spatial/lens convergence proof.

## Frozen geometry and material resources

Film replay previously rejected all mesh objects. The composition loader now
shares `resolve_gaussian_composition_resources`, which resolves the already-parsed
composition's SHA-bound geometry, material sets and textures without reading the
current `scene-objects.json`. The existing symlink/integrity checks and limits of
one million unique triangles, 32 images and 256 MiB decoded image data remain.
Ordinary project opening retains its artifact binding and strict recipe parser.

The film verifier resolves the composition recorded in the export receipt. All
twelve imported-torus frames replay with zero RGB error in
`mesh-film-replay/receipt.json`. A second replay hides the torus and changes the
sphere color in the current recipe, still reproduces the original twelve frames
exactly, then restores the current recipe byte-for-byte. See
`frozen-mesh-replay-audit.json` and `edited-recipe-mesh-replay/receipt.json`.

Material tests independently retain frozen overrides after a current recipe edit,
resolve with the current recipe absent, and reject missing mesh/corrupt texture
assets. The final four material/object/film/sequence tests pass in
`final-resource-regression.log`. The desktop target also builds successfully.

The staging driver additionally supports read-only replay of an explicit plan:

```sh
build/dev-macos-debug/tests/gaussian_scene_staging_driver --render-plan PROJECT PLAN_JSON NEW_OUTPUT
```

It validates the pinned artifact SHA, parses the saved camera/composition and uses
the shared resource resolver. `plan-replay/render.png` is byte-identical to the
original `tabletop-v1/finished.png`. No completed film receipt is fabricated to
drive a still render.

## Remaining qualification

The packaged application proof passes visible object controls and rendering,
with the window exposed but inactive. It is not foreground interaction proof.
The combined shot records approximately 75.30 seconds at 960x639; its uncomposed
optical source takes 0.80 seconds, while the atmosphere-only background records
57.74 seconds. These are descriptive export timings with other verification work
in the session, not an isolated performance experiment. Combined lens/atmosphere
performance needs investigation before claiming a smooth workflow at this quality.

The [subsequent bounded scheduling experiment](gaussian-optical-haze-performance-2026-10-25.md)
reduces the same combined shot to 32.36 seconds with byte-identical PNGs and
completes its twelve-frame film with zero-error frozen replay. Broader
camera motion, contact on more captures, spatial/lens sampling convergence,
active-foreground performance, the original-rasterizer audit and C4D/Octane
interchange remain open. This result does not establish complete video parity.
