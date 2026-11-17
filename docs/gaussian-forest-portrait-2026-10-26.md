# Native forest portrait and physical camera study — 26 October 2026

The human portrait is now composed inside a genuine 3D forest capture. The
editable project and native photographs are in
`build/scene-deliverables/forest-portrait-4k-2026-10-26/`.
This replaces the Bonsai-only photographic demonstration with a face, hair,
clothing, real foliage depth and independently adjustable background focus.

## Scene and provenance

- Human: [Seeget3D, 3D Portrait](https://superspl.at/scene/3c052a7e),
  705,803 Gaussians, CC BY 4.0. The author reports 52 source photographs and
  links the [Kaggle reconstruction dataset](https://www.kaggle.com/datasets/yogeshkulkarni/3d-reconstruction).
- Forest: [draftmode, Forest Scan](https://superspl.at/scene/259c0051),
  2,142,439 original Gaussians, CC BY 4.0. The staged forest retains 1,481,727
  after clipping its near region in world depth; the original scan is preserved.
- Combined native scene: 2,187,530 Gaussians, full three-band SH. It is a merged
  point scene with covariance, orientation and view-dependent color, rather than
  a background photograph. The person and forest are baked into one imported
  capture; this does not yet create separate editable capture-layer objects.

`scripts/compose-gaussian-camera-study.mjs` invokes the pinned official
[PlayCanvas splat-transform](https://github.com/playcanvas/splat-transform)
3.10.3 converter. The converter rotates SH and covariance as well as positions.
The script accounts for its canonical/PLY 180-degree Z rotation. The delivered
recipe and receipt record every transform and argument.

The source captures have no measured scale. The person's 0.107 metres per
source unit and the forest's 10 metres per source unit are explicit authoring
assumptions. Once composed, one native scene unit is one authored metre. Camera
reports are internally physical for this scale; these are not independent
measurements of the photographed subject or forest.

The static import/view/focus/visibility limit is now a shared four-million-point
constant, allowing this scene. Training and 4D admission budgets retain their
existing limits. This study qualifies 2.19 million points, not every possible
four-million-point workload.

## Camera and usable controls

Five primary saved views render at native 3840 x 2160 with 128 aperture samples:

| Saved view | Lens | Focus | Purpose |
| --- | --- | --- | --- |
| portrait-100mm | 100 mm f/2.8 | 2 m | Main photograph |
| wide-85mm | 85 mm f/2.8 | 2 m | Wider shoulders and surroundings |
| zoom-135mm | 135 mm f/2.8 | 2 m | Tighter framing |
| forest-focus | 100 mm f/2.8 | 8 m | Background focal plane |
| deep-focus-f8 | 100 mm f/8 | 2 m | Smaller iris / deeper focus |

A 36 x 24 mm sensor uses a 36 x 20.25 mm active gate for 16:9 output. Projection
uses finite-focus image distance, not a fixed infinity-focus approximation.
The iris is nine rounded blades. Film-plane settings use 6-micron grain,
60-micron RGB halation diffusion, amounts 0.08 / 0.10 and seed 42.

The Camera inspector now has a continuous focal-length zoom slider. Changing
physical focal length or focus after orbiting preserves the actual world camera
pose. Focus picking also retains that pose. Navigation pivots around the optical
focus distance rather than the merged scene's distant depth median.

Saved physical paths may vary focal length and manual focus distance, while
sharing scale, sensor, active gate, f-stop, pupil shape, film and sampling.
Each sampled camera recomputes finite-focus projection and pupil diameter, so a
zoom keeps the specified f-number instead of interpolating incompatible pixel
focal lengths and aperture radii. The delivered path runs
85 -> 100 -> 135 mm -> forest focus, 1.5 seconds per transition, 24 fps.

The delivered three-second 1280 x 720 movie shows the zoom section, rendered
with an explicitly separate 32-sample preview project. The editable delivered
project and 4K photographs retain 128 samples. The full 108-frame preview path
was also rendered into the temporary evidence directory; its strongly defocused
32-sample face shows sampling texture, so that portion was excluded from the
presentation movie. The 128-sample 4K background-focus photograph is included.

## Extreme aperture controls

The camera now accepts f/0.1–f/128; the new logarithmic aperture slider spans
f/0.1–f/22 and preserves typed values above f/22. Its logarithmic coordinate gives
consistent travel per stop. The focal-length slider and numeric field remain
independent: changing zoom recalculates pupil diameter D = f/N for the selected
f-number and retains camera pose. Two endpoint presets are included in the
project: `extreme-f0.1` and `deep-focus-f22`.

The ideal pupil's scene-unit bound was expanded to support the requested
experimental f/0.1, including zoom and close focus. This is bounded geometric
transport, not a realizable compound lens at f/0.1. Synthetic native tests check
blur spread independently from pupil-ray variance at f/0.1, f/0.2 and f/22;
conversion and persistence also cover 14–2000 mm focal lengths.

Stopping down currently changes geometric depth of field; diffraction is not
implemented. Real f/22 resolution also depends on wavelength and diffraction,
as described by [Edmund Optics](https://www.edmundoptics.com/knowledge-center/application-notes/imaging/mtf-curves-and-lens-performance/).
This remains an explicit fidelity boundary rather than a commercial-lens match.

## Rendering changes and evidence

The retained renderer uses four-vertex triangle strips and three eleven-bit
stable radix passes. An exact primary-visibility experiment reduced the number
of submitted Gaussians but its initial apparent 30% speedup did not reproduce:
an isolated paired run measured 22.43 -> 21.73 seconds, only 3.1%, below the
predeclared 20% gate. The CPU bounds, their extra cached data and experimental
property test were removed. This experiment is rejected, not a retained speedup.

All five frozen 4K forest photographs were decoded and compared across that
experiment: maximum channel-code error 0, SSIM 1. The baseline source was restored
after rejection. PNG container hashes differ, so acceptance uses decoded pixels.
The native Metal suite passed ordering, opacity, SH, clipping, lens, progressive
sampling, exposure, film, autofocus, cache isolation and tiled export regressions.

Foreground native gesture runs during this work measured roughly 10.6–14.2 fps,
86–115 ms p95 publication gap, 615–714 ms to a styled prefix and 2.69–5.50 seconds
to the complete 1704 x 958 viewport photograph after release. These are Qt pointer
events observed at frame swaps, not physical input-to-photon measurements. They
cover the full-point draft and the experiment's photograph path; final packaged
proof is kept separately. The absolute 30 fps / 50 ms p95 / 500 ms first-style gate
remains unmet. 4K photographs ranged from about 15 to 22 seconds in repeated full
runs on this host. The initial 10.69-second run is not a stable promise.

A bounded point-subset draft was also rejected: native screenshots showed the
skin becoming partly transparent. The full-point draft is retained. Faster large
scenes need opacity-preserving spatial LOD or a different sort path, rather than
simply dropping points. The earlier 705k-point strip/radix experiment improved
observed FPS by about 16% but missed its 20% interaction target; this does not
establish reference-video timing parity.

Evidence root: `/private/tmp/looksy-human-4k-v1/`. The final delivery includes
selected native receipts, screenshots, decoded-pixel comparisons, movie probes
and a source/binary hash manifest. Temporary failed variants and timing runs are
kept distinct from retained results.

## Scope boundaries

Captured illumination is retained. This composition does not recover forest
illumination on skin, material BRDFs or missing scan geometry. The source face
and clothing limit actual resolving power; 4K output is not a measured 4K lens
or sensor match, and no AI enlargement or invented detail was used.

The photo renderer still uses the ideal thin lens. The separately validated
compound-glass ray generator is not yet connected to photographic splat capture.
Grain/halation remain generic physical-scale statistical approximations, not a
measured commercial film stock. These are remaining fidelity gates, alongside
large-scene interaction performance.

## Final qualification

The final packaged app loaded all seven saved views and the 4.5-second path,
restored the 100 mm photograph and captured `camera-studio.png` with both physical
sliders visible (`ok=true`, `cameraControlsVisible=true`, native 3200 x 1800
window capture). Its isolated offline catalog is intentionally unqualified; the
capture establishes Scene behavior rather than Library or agent readiness.

The complete native studio workflow passed f/0.1, f/22, 200 mm zoom, aperture
slider input, shot save/recall, compact panel bounds, film controls and workspace
recreation. The blur oracle measured 26.969 pixels squared against 27.024 predicted
at f/0.1; the f/0.2 and f/22 checks passed their pixel-quantization-aware tolerance.
Four focused unit suites and the full native render regression passed.

Actual 1280 x 720 forest examples at f/0.1 and f/22 are delivered separately from
the five 4K photographs. An additional f/0.1 preview uses a picked face surface
at 1.952862 m and 256 aperture samples. Its thin sharp plane illustrates why
focus precision matters at this experimental pupil size.

A one-line missing `QImage` include in `timeline_document_controller.h` was
required to complete the packaged build after concurrent Timeline changes. No
Timeline behavior was changed as part of this camera work.
