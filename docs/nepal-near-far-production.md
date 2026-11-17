# Nepal / Near & Far

Completed creative production pass, 2026-11-15, and foreground release walkthrough,
2026-11-16. The six-board work is saved and editable. Native rendering, persistence
and actual UI export passed; the agent planning check exposed a separate failure.

## Creative premise

The series alternates close human experiences with the scale of the landscape.
It uses a consistent cobalt, orange, bone and ink identity, heavy Archivo Black
headlines and quieter Space Grotesk identifiers. The opening mountain ridge
interrupts the lower title; the bridge supplies a photographic diagonal; the
close portrait receives a much larger scale than the small encounter; the quiet
mountain board provides a release; flowers, people and firelight close the story.

The six boards are **Near & Far**, **After Dark**, **On Foot**, **Up Close**,
**First Light**, and **The Days Between**. All are 1920 × 1536 pixels. They form
one saved Looksy document with independent board recipes, rather than one long
source raster that distorts when extended.

## Iteration

1. Generated a six-panel composition study using the built-in imagegen tool and
   five original photographic references. Its changed faces and scene details
   were retained only in the reference image; they do not appear in production.
2. Rebuilt the series with original camera JPEGs and native editable layers.
   The initial render exposed an inaccurate mountain mask, excess space between
   cover lines and distracting small photo frames. Native mechanics passed, but
   visual inspection rejected those rough edges.
3. Derived a finer native vector mask from the actual skyline, tightened the
   cover, removed the night photo strip and bridge inset, and selected a wider
   mountain photograph for the quiet board. Dark text improves contrast against
   its lighter sky. The first two native overview iterations are retained.
4. Applied small reversible exposure/shadow adjustments to two portrait layers.
   Inspected the final cover, portraits, closing composition, full sequence and
   a frame from the review film. Typography, subject visibility and photo/graphic
   hierarchy were reviewed at individual-board and overview scales.

## Saved artifact

- Catalog document: `4bec5106-bd3e-4172-888c-74cdff2dc031`.
- Name: **NEPAL / NEAR & FAR**, project **Nepal 2026**.
- Six boards; **41 native layers**, respectively 8, 6, 6, 7, 4 and 10.
- **Nine original photographs**, with ten app-owned backing files including the
  blank canvas. Sources were copied into `carousels/sources/<document-id>/`.
- Native text, linked photo sources, vector masks, vector planes and adjustments
  remain independently editable. Graphic layers are not flattened page imports.
- The cover silhouette is an editable vector mask over an unchanged source
  photograph. Mild portrait corrections are native adjustment parameters.
- Original RAW files, the original Nepal board and the prior Unfolded draft were
  preserved. Catalog before/after records and media hashes provide the evidence.

Private reproducible artifacts live in `.photo-editor/nepal-near-far/`:

- `near-far-overview.jpg` / `.png`: actual native renders of the finished series.
- `board-1.png` through `board-6.png`: individual native boards.
- `near-far-review.mp4`: 16-second, 1600 × 1280 review film with editorial dissolves.
- `near-far-delivery.zip`: verified archive containing six final PNGs, the native
  delivery manifest, the series overview and the review film.
- `layout.json`, `sources.json`, `create_layout.py`, the native proof helper and
  a standard preset bundle with all six editable layer recipes.
- `saved-document-receipt.json`, exact `catalog-readback.json`, authored and
  production native verification reports, and `final-verification.json`.
- `art-direction.png` and `art-direction-prompt.txt`: generated reference and
  exact prompt, clearly separate from final photographic sources.

## Verification

The native authoring run imported the standard preset bundle, exercised exact
Undo/Redo on every board, saved/reopened all six recipes, rendered them and ran
configured export with preflight. Every exported PNG has the intended dimensions
and an embedded color profile; a native delivery manifest was produced.

The catalog helper then saved a new UUID with app-owned backing media and exact
JSON readback. The exact saved record was reopened through the normal native
loader in isolated storage, using its real app-owned image paths, and rendered
and exported again. **All six saved-record renders and configured exports are
pixel-identical to the reviewed native images.** The generated concept image is
absent from every production photo source reference.

The running release's registered read-only `availablePhotoTextFonts()` command
confirmed both Archivo Black and Space Grotesk. After the Mac was unlocked, all
six boards were fitted and visually reviewed in the running release. A temporary
caption edit was undone to its exact original text; layer visibility passed
Hide → Undo → Redo → Undo. Saving, opening the prior draft and reopening this
document retained every slide and group exactly. The catalog differs only in
selection/navigation fields and save time. No external MCP permissions, launch
settings or security settings were changed. The active editor was not restarted.

The actual board export shortcut opened the native folder chooser and produced
six 1920 × 1536 PNGs with embedded profiles and an sRGB delivery manifest. All
manifest file hashes passed. Compared with the isolated native proof, the release
exports differ in small Space Grotesk text-edge regions: 563–1,780 pixels per
board, at most 0.061% of a board. Photographs, masks and large headlines match.
These two runtime outputs are therefore not claimed to be pixel-identical.
The final delivery ZIP now contains the six foreground exports and their manifest,
plus the overview and review film. All nine archive members passed CRC checks.
Evidence is in `foreground-verification.json`, `foreground-catalog-readback.json`,
`foreground-render-difference.json` and `foreground-export/`.

The review film's dimensions and duration were checked with ffprobe, and a frame
was inspected. It is a presentation of the actual native boards; it does not
claim to be a recording of the Looksy UI or real movement in the photographs.

## Agent workflow qualification

A bounded read-only request asked the in-app agent to identify the active document,
board and layer count without making changes. Planning failed: the model reported
that inspection tools were necessary but prohibited by its planning instructions.
The target chooser offered Current Photo, Filtered Photos and every photo in the
project, without naming the visible board document. This is a failed context and
planning check, not a successful agent run. Pause/notes could not be exercised
without an executable plan; external MCP editing remains unqualified. The earlier
rejected drafts remain recoverable. See `nepal-agent-workflow-review.md` for the
observed friction and proposed next product changes.
