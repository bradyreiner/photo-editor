# Dense scenes and maximum-resolution film study

Delivery: `build/scene-deliverables/dense-film-lab-8k-2026-10-26/`.
Temporary native evidence: `/private/tmp/looksy-dense-film-v1/`.

The study adds actual near-field occluders, a crowded interior, reflective
mechanical detail and controlled HDR emitters. The three photographic cases use
the current 8192-pixel per-axis export ceiling. The bokeh diagnostic separately
uses the maximum 4096 pupil samples. No production renderer approximation or
film defaults were changed during this study.

| Case | Gaussians | Native output | Pupil samples | Renderer time |
| --- | ---: | --- | ---: | ---: |
| Foreground portrait | 2,789,855 | 8192 × 8192 | 256 | 220.3 s |
| Dense office | 1,105,897 | 8192 × 5461 | 128 | 19.3 s |
| Motorcycle / forest | 2,916,295 | 8192 × 5461 | 128 | 39.1 s |
| Practical-light convergence | 2,789,855 | 2048 × 2048 | 4096 | 322.9 s |

Times exclude source import and PNG encoding. They are observations from this
local native study, not an isolated performance comparison or realtime claim.
The forest square uses 16 tiles and peaks around 2.8 GB process memory footprint;
the 3:2 photographs use 12 tiles. The original photo pixels are not enlarged or
AI-generated. Native output dimensions do not certify source resolving power.

## Scenes and physical authoring

The foreground portrait combines the existing person and forest with two
separately transformed, clipped foliage slices at roughly 0.35–1.4 authored
metres. Focus is picked on a face surface at 1.971920 m. The 100 mm f/2 camera
uses a 24 × 24 mm square crop of its 36 × 24 mm sensor. Source scale remains an
explicit staging assumption. The scene preserves full view-dependent SH and
covariance; the layers are merged into one native capture, not a backdrop image.

The office loads both complete LOD-zero chunks of
[Seeget3D's public capture](https://superspl.at/scene/70953d8c), whose author reports
555 source images. The coarse LOD is not substituted. Nearby chair edges, wooden
tables, bright windows and ceiling fixtures expose depth, antialiasing and source
highlight limitations. The mechanical case retains captured metal reflections;
it does not reconstruct a new forest reflection or dielectric glass model.

The practical-light scene adds three editable emissive spheres: two behind the
person and one in front. They exercise per-aperture visibility, RGB light return
and round versus hexagonal irises. Captured illumination is retained rather than
claiming that these sources relight the person's skin.

All four project folders contain validated saved views. The three photographic
projects also have valid short focus/zoom paths using shared gates and f-stops.
Native project reload validates artifact hashes, camera calibration, paths and
composition. The delivered forest project was also opened in packaged Main:
`ok=true`, camera controls visible, 2,789,855 points, four saved views and a
two-second path. The existing offline-catalog banner remains outside this Scene
qualification.

## Grain and halation checks

The new `gaussian_film_resolution_test` renders a 67,108,864-pixel synthetic
recording with fixed grain and HDR features crossing tile seams. Comparing 2048
and 1024 tile edges gives 16 versus 64 tiles, with a maximum one-code channel
difference in 13 pixels. This protects global grain phase, halation overlap and
bounded tile assembly at the actual export ceiling.

Measured grain-density correlation at the same 11.71875 µm separation is
0.3485 at 8K and 0.3736 at 4K. The check permits finite-spectrum and pixel
quantization error; it detects grain tied incorrectly to output pixels. It is
not a film-stock calibration. Existing independent film-plane references remain
the small-image RGB transport oracle.

Each real scene has four 4K variants with the same optical source: no film,
grain only, halation only and combined. The helper verifies that only film
settings changed and that all later variants reused the resolved optical image
with zero rasterized pupil samples. The forest comparison is square; the office
is wide; the motorcycle comparison deliberately uses a closer 50 mm f/1.4 view
than its 28 mm 8K original. The reviewer labels the two camera domains separately.

With the optical source cached, real-scene film variants took roughly 26–88 ms
in the renderer. Full optical accumulation remains the expensive stage.

## Bokeh sampling finding

The HDR practical-light case showed visible sample footprints at 256 pupils.
Repeating the exact 2K scene at 1024 and 4096 pupils, with film and seed fixed,
produced substantially smoother discs. In a fixed 48 × 48 patch inside the blue
disc, measured linear-sRGB blue variation divided by its mean was:

| Pupil samples | Relative variation | Mean linear sRGB blue |
| ---: | ---: | ---: |
| 256 | 0.2120 | 0.34186 |
| 1024 | 0.0988 | 0.34025 |
| 4096 | 0.0547 | 0.34143 |

The patch statistic includes film grain and background variation. It establishes
a strong convergence trend, not a noise-free reference or commercial-lens match.
The 4096-sample photograph retains residual grain/sampling variation and takes
over five minutes at 2K. This makes adaptive optical accumulation a higher-impact
next performance target than simplifying the film pass.

## Implementation and reproduction

`gaussian_scene_staging_driver` now accepts persistent authored composition,
reports tile/sample progress, exports unresampled detail crops, measures stored
RGB patch statistics and validates delivered projects without rendering. The
review builder produces a local page comparing saved images; it does not pretend
to be a live 3D renderer. The native projects provide the actual 3D controls.

```sh
cmake --build --preset build-dev-macos-debug --target gaussian_scene_staging_driver gaussian_film_resolution_test
ctest --test-dir build/dev-macos-debug -R '^gaussian_film_resolution_test$' --output-on-failure
QT_QPA_PLATFORM=offscreen build/dev-macos-debug/tests/gaussian_scene_staging_driver --validate-project /absolute/project
python3 scripts/build-gaussian-film-review.py build/scene-deliverables/dense-film-lab-8k-2026-10-26
```

The evidence folder records source/binary hashes, all optical/film settings,
native renderer durations, exact crop rectangles and project reload receipts.
The five focused film/camera/saved-view/path tests passed. Browser review covers
case switching, film variants, pixel view, the comparison wipe and loaded detail
images; it does not substitute for native scene rendering.

Sources and CC BY 4.0 attribution for the person, forest, motorcycle and office
are included in the delivery README. Conversion uses the pinned official
PlayCanvas splat-transform 3.10.3, retaining all source points except explicitly
authored world-box clipping. Source captures are preserved.

## Remaining fidelity limits

The source scans already contain display-referred color, compression, clipped
highlights and captured reflections. More export pixels do not restore missing
capture detail or film exposure latitude. Kodak's current
[5219 technical document](https://www.kodak.com/content/products-brochures/motion-picture/KODAK-VISION3-5219-7219-technical-information.pdf)
describes highlight latitude, anti-halation construction and scanning as parts
of a recording pipeline; it does not validate this approximation or its presets.

The current photographs use the ideal thin lens. Compound-glass scene integration,
diffraction, absolute exposure, measured sensitometry, dye-layer transport and
scanner profiles remain outstanding. This study improves qualification and
provides more demanding examples; it does not declare photographic parity.
