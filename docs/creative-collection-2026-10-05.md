# Creative collection — October 5, 2026

38 new photo presets, 24 new editable effect looks, 21 new typography styles, and 8 bundled font families. The existing 14 photo presets and nine effect operators remain available.

Implementation uses the existing photo adjustment, effect-stack, and typography renderers. Effect looks append ordinary editable operators to the selected layer in one history action. Typography styles use installed or bundled font families and reset decoration and variable axes while preserving text content, size, and layout paths.

## Photo presets

Available in the photo preset picker. The registry now contains 52 presets; the additions are listed below.

### Cinematic (8)

- Teal and Amber Cinema
- Neon Afterglow
- Blue Hour Glow
- Tungsten Noir
- Cinema Fade
- Copper Shadow Drama
- Soft Spotlight
- Night Market Lights

### Soft Editorial (8)

- Airy Linen
- Peach Window Light
- Lavender Haze
- Coastal Pastel
- Rose Tint Editorial
- Porcelain Bloom
- Buttercream Morning
- Muted Flower Market

### Analog Color (7)

- Warm Negative
- Faded Seventies
- Instant Cream
- Cross Process Color
- Dusty Rose Matte
- Soft Chrome
- Expired Film Shift

### Monochrome (3)

- Silver Gelatin
- Foggy Monochrome
- Noir Spotlight

### Travel and Nature (12)

- Alpine Clarity
- Tropical Jade
- Desert Amber
- Ocean Depth
- Autumn Trail
- Wildflower Field
- Nordic Mist
- Rainforest Rich
- Canyon Glow
- Alpine Sunrise
- City Walk
- Coastal Sun

## Effect looks

Open Add filter and choose an entry from Looks. Each recipe becomes one or more editable stack entries; amount, masks, parameters, enable/disable, and ordinary layer history remain available.

| Look | Category | Treatment |
| --- | --- | --- |
| Analog Fade | Film | Soft lifted print tones with a gentle highlight veil. |
| Warm 35mm | Film | Warm highlight bloom with subtle film plate texture. |
| Portrait Glow | Film | A restrained warm glow that keeps the subject readable. |
| Cinema Bloom | Film | Warm highlight scatter with textured print detail. |
| Instant Print | Film | Paper texture and slight color plate drift. |
| Plate Shift | Film | Offset color plates with a crisp, graphic print feel. |
| Lo-Fi Print | Film | Paper texture with visible color plate offsets. |
| Newsprint | Print | Coarse ink texture with a compact tonal palette. |
| Dream Haze | Optical | Diffuse bright details while keeping shadows calm. |
| Soft Focus | Optical | A broad gentle diffusion with low blur strength. |
| Mist Filter | Optical | Subtle highlight fog with a neutral tint. |
| Golden Bloom | Optical | Warm highlight bloom for late sun and practical lights. |
| Cool Bloom | Optical | Cool tinted highlight scatter with protected whites. |
| Glassy Diffusion | Optical | A crisp highlight halo with light texture underneath. |
| Panning Trail | Motion | A directional exposure streak for moving scenes. |
| Slow Shutter | Motion | Long shutter trails with a soft center-weighted falloff. |
| Diagonal Drift | Motion | A softer diagonal motion trail with highlight bias. |
| Kinetic Echo | Motion | Layered directional blur for a graphic movement feel. |
| Soft Shutter | Motion | A gentle directional trail with center-weighted falloff. |
| Poster Pop | Graphic | A bold stepped-color treatment with crisp edges. |
| Color Negative | Graphic | Invert the image and simplify it into a punchy palette. |
| Block Color | Graphic | Reduced palette with softened small detail. |
| Risograph | Print | Misregistered ink plates with textured, stepped color. |
| Soft Detail | Portrait | Mild smoothing with a light detail lift. |

## Typography styles

Select a text layer, enable full typography, then choose a style and press Apply. A fallback notice identifies the resolved installed font when the preferred family is unavailable.

| Style | Preferred family |
| --- | --- |
| Quiet Editorial | Cormorant Garamond |
| Modern Magazine | DM Sans |
| Soft Contrast Serif | Fraunces |
| Gallery Italic | Cormorant Garamond |
| Warm Caption | DM Sans |
| Swiss Grid | Space Grotesk |
| Geometric Bold | Outfit |
| Mono Label | Space Mono |
| Technical Mono | Space Mono |
| Wide Geometry | Space Grotesk |
| Cinema Title | Bebas Neue |
| Poster Condensed | Bebas Neue |
| Indie Zine | Archivo Black |
| Retro Display | Fraunces |
| Newsprint | Cormorant Garamond |
| Outline Neon | Space Grotesk |
| Electric Sign | Outfit |
| Frosted Glass | DM Sans |
| Smoked Lettering | Space Grotesk |
| Soft Emboss | Fraunces |
| Soft Italic Mood | Fraunces |

## Bundled fonts

These fonts are embedded for offline use in photo text and Carousel designs. Each family retains its original SIL Open Font License 1.1 and copyright notices. Sources and exact hashes are in [the font manifest](../apps/desktop/assets/fonts/manifest.json); see [font details](../apps/desktop/assets/fonts/README.md).

- Space Grotesk
- Space Mono
- DM Sans
- Cormorant Garamond
- Bebas Neue
- Archivo Black
- Fraunces
- Outfit

Upstream: [official Google Fonts repository](https://github.com/google/fonts), pinned at `7085eb89a950e85db5b166b7a58d414544b4140c`.

## Basic validation

The macOS Debug desktop build and seven focused tests passed. The preset inventory validator and its adversarial checks also passed. See [build evidence](evidence/creative-collection-2026-10-05/build.txt) and [CTest output](evidence/creative-collection-2026-10-05/ctest.txt).

- Font resource checks cover all eight families, glyph availability, license bytes, and SHA-256 checksums.
- Effect recipe checks cover all 24 looks, supported operators, serialization, deterministic rendering, finite output, and a changed raster. The picker test checks search and successful/rejected applications; the workflow checks undo/redo and locked-layer rejection.
- Typography checks instantiate the actual panel, apply all 21 styles to a controller fixture, resolve installed fonts, reset stale decoration/axes, preserve text geometry, and reject locked edits. The existing typography workflow also passes.
- Photo preset checks cover all 52 registry entries, metadata, numeric bounds, and representative recipe semantics.

This collection has not undergone full photographic visual review, full-resolution performance qualification, or manual font/style review.

The preset inventory manifest describes executable entries; it does not grant visual approval to new presets. Previously captured visual approval evidence is not reused for the expanded source registry.
