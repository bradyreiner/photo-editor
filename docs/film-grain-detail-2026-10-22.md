# Film grain and detail refinement

Film grain is available through **Layers → Effects → Film grain**. New effects
start with Portrait negative 400. The stock picker changes structure and useful
starting settings together; subsequent sliders remain editable. Grain stocks do
not recolor or desaturate the photograph.

| Profile | Reference direction | Texture behavior |
| --- | --- | --- |
| Color negative 100 | Ektar | Tight dye clouds; low clumping |
| Portrait negative 400 | Portra | Softer overlapping clouds; gentle highlight grain |
| Cinema negative 500T | Vision3 | Different dye-layer scales; quieter deep shadows |
| Tabular silver 100 | T-Max | Fine monochrome texture; minimal clumping |
| Classic silver 400 | Tri-X | Irregular clusters; stronger midtone texture |
| Pushed silver 3200 | High-speed / pushed silver | Coarse clusters; stronger shadow texture |

These are artistic stock-inspired profiles, not calibrated emulsion simulations.
The names describe families rather than promising exact reproduction of a stock,
scanner, developer, exposure, or film format. The renderer combines seeded,
randomly positioned compact particles at fine and cluster scales. Stock
coefficients determine scale mixture, cluster size, tonal response, and relative
dye-layer scales. Clumping, color variation, shadow grain, highlight grain, size,
strength, and seed can be adjusted independently. Changing size and strength to
the same values across stocks still leaves structural differences.

The field is anchored to document coordinates and repeats exactly for a saved
seed, including when split across tiles. Raster-footprint broadening and variance
attenuation approximate unresolved grain at smaller previews. This is not an
exact integration of each grain over every output pixel. Inspect at 1:1 when
choosing final texture. Original four-value grain payloads retain the original
renderer and byte-identical parameter encoding; the stock extension is optional.
Selecting “Original grain (legacy)” is an explicit return to that renderer.

Noise reduction now runs before sharpening within each Detail pass. Luminance
noise reduction compares smoothed scales to avoid classifying individual noisy
pixels as edges. Color reduction attenuates its blend at strong chroma boundaries.
Sharpening interpolates neighboring blur scales continuously, responds below
one pixel, and softly bounds its high-frequency signal to reduce strong halos.
The Amount UI now matches the persisted 0–100 range, removing a dead 100–150
range. Global and layer CPU detail share a single implementation; compiled CPU
and retained Metal programs use the same equations and processing order.

Effect-stack ordering is still editable. Base Detail is an existing global pass
after layer composition. To preserve added grain when using heavy cleanup, use
Detail controls on an adjustment layer below the grain layer (or within the grain
layer, where its Detail pass precedes its effects). This change does not silently
reorder saved layer stacks or change the rendering of legacy grain itself.

Reference material used for artistic direction:

- [Kodak TRI-X technical data F-4017](https://www.kodakprofessional.com/sites/default/files/wysiwyg/pro/resources/f4017_TriX.pdf): granularity, sensitometry, and processing context.
- [Kodak VISION3 500T technical data](https://www.kodak.com/content/products-brochures/Film/VISION3_5219_7219_Technical-data.pdf): layered dye technology and reduced shadow grain.
- [Kodak Professional film brochure](https://www.kodak.com/global/plugins/acrobat/en/professional/products/films/2012Brochure.pdf): comparative stock families.

Validation is recorded under `docs/evidence/film-grain-detail-2026-10-22`.
