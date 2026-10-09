# Creative finishes expansion — October 7, 2026

This collection adds 44 selectable filters, looks, and text finishes across Edit and Timeline. Counts distinguish new filter algorithms from named recipes built from editable operators.

| Location | Additions | Access |
| --- | ---: | --- |
| Edit filters | 6 new operators | Selected layer → Filters → Add filter |
| Edit looks | 16 editable recipes | Selected layer → Filters → Add filter → search |
| Timeline | 12 built-in effect-stack recipes | Selected video clip → Effects → stack presets |
| Text | 10 metal finishes | Text layer → Typography → Material / style presets |

## Edit filters and looks

New operators: Film Grain, Light Leak, Split Tone, Vignette, Duotone, and Soft Fade. Every look expands into ordinary editable effect-stack entries with amount, bypass, ordering, and effect-mask support. Grain and leaks use seeded document coordinates. They are procedural treatments, not emulations of named film stocks or scanned physical leaks.

| Look | Treatment |
| --- | --- |
| Fine 35 Grain | Fine monochromatic grain |
| Color 400 Grain | Colored grain and subtle fade |
| Silver Grain Study | Nearly monochrome, coarser grain |
| Dusk Emulsion | Amber highlights and fine grain |
| Amber Edge Leak | Warm left-edge exposure |
| Rose Corner Leak | Warm corner wash with fine grain |
| Blue Edge Leak | Cool right-edge exposure |
| Window Flare Veil | Broad warm edge veil with slight diffusion |
| Teal Amber Balance | Teal shadows and amber highlights |
| Lavender Cream | Lavender shadows, cream highlights, matte lift |
| Sepia Ink | Warm two-tone ink and fine grain |
| Cyan Ink Study | Blue-to-cyan two-tone print |
| Indigo Coral Ink | Indigo-to-coral ink and matte lift |
| Matte Black Finish | Lifted blacks, restrained color, fine grain |
| Linen Color Fade | Airy color fade with slight paper variation |
| Quiet Lens Falloff | Broad feathered vignette |

## Timeline looks

Soft Matte Print, Bright Editorial, Clean Pop, Quiet Monochrome, Silver Monochrome, Airy Portrait, Soft Detail, Low-Key Story, Faded Color Study, Pastel Print, Rich Color, and Natural Contrast use Timeline's existing exposure, contrast, saturation, monochrome, fade, punch, and Gaussian blur operators. They are video-clip recipes, with individually editable stages, rather than twelve new rendering algorithms. Built-ins remain available beside custom presets and are excluded from custom preset storage. Applying a look is undoable in one step; the popup supports merging selected stages or replacing the stack.

## Metal typography

Polished Chrome, Brushed Silver, Warm Gold, Copper, Dark Steel, Champagne Gold, Rose Gold, Antique Brass, Titanium, and Black Chrome are rendered material profiles with studio reflection bands and glyph-edge lighting. Brushed profiles include directional surface variation. Tint, reflectivity, bevel softness, depth, and light direction remain editable. Metal text uses text-style schema version 9 and the CPU compositor; existing glass/emboss documents retain version 8. These materials are available in Edit typography, not Timeline titles.

## Evidence

The creative look workflow test uses the production controller, QML picker, undo history, recipe codec, and PNG exporter. The operator tests check raster contracts. Render and native UI captures, test results, and known limits are recorded after integration in `docs/evidence/creative-finishes-2026-10-07/README.md`.

These checks establish rendering and editing behavior. They do not constitute full photographic qualification of every treatment at every resolution, nor a sustained playback-performance claim.
