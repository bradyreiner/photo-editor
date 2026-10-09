# Preset library and creative expansion — October 5, 2026

The photo library now contains 82 presets, and the filter picker contains 36 editable effect looks. This pass adds 30 photo presets and 12 effect looks to the [first creative collection](creative-collection-2026-10-05.md).

## Preset workflow

- Browse All, Favorites, or My presets with search, category, and pack filters.
- Virtualized cards use a bundled photographic reference rendered through the canonical thumbnail pipeline. Hover previews the look on the open photo.
- Select a look, adjust its intensity, and apply it to the current editing target.
- Create a preset in a focused dialog with a name, pack, capture source, and adjustment scopes.
- Save from the base image or the selected adjustment layer. Layer presets capture that layer's own tone, color, and detail settings.
- Save as new protects existing names; Update selected is an explicit custom-preset action.
- A successful save opens My Presets and clears filters so the new entry is immediately visible. Favorites refresh without reopening the panel.
- Duplicate a stored look into a custom preset without applying it to the open photo.
- Save failures report an error and restore the previous in-memory preset. Presets retain the existing file format and version metadata.

## Added photo presets

### Food and Product

- Fresh Table
- Citrus Menu
- Dark Chocolate
- Porcelain Product
- Glass and Chrome
- Artisan Bakery

### Architecture and Urban

- Concrete Geometry
- Warm Facade
- Blue Hour Architecture
- Night Grid
- Urban Monochrome
- Clean Interior

### Dreamy Color

- Rose and Teal Duotone
- Indigo and Amber Duotone
- Lilac Dream
- Mint Reverie
- Peach Highlights
- Candy Neon

### Cool Editorial

- Glacier Editorial
- Slate Portrait
- Cyan Window
- Clean Steel
- Winter Linen
- Moonlit Editorial

### Muted Minimal and Print

- Quiet Canvas
- Taupe Still Life
- Stone Pastel
- Faded Poster
- Graphic Ink
- Soft Letterpress

## Added effect looks

| Look | Category | Treatment |
| --- | --- | --- |
| Market Glow | Food | A warm, restrained bloom that keeps food texture crisp. |
| Glass Catchlight | Product | Fine highlight scatter and a clean edge lift for reflective objects. |
| Architectural Detail | Architecture | A controlled fine-detail lift for stone, brick, and clean lines. |
| Cool Editorial Veil | Optical | A cool, subtle highlight veil with protected bright whites. |
| Quiet Diffusion | Optical | Low-strength neutral diffusion for a calm, minimal finish. |
| Pastel Plate | Print | Soft paper texture with restrained color plate drift. |
| Limited Edition | Print | A reduced palette and gentle registration offset for a graphic print. |
| City Pan | Motion | A short, restrained horizontal exposure trail for moving street scenes. |
| Kinetic Sweep | Motion | A diagonal shutter drag with moderate center falloff and highlight weighting. |
| Portrait Gauze | Portrait | Soft highlight diffusion with just enough detail to retain eyes and fabric. |
| Moon Halo | Optical | A cool, gentle halo around bright points and night highlights. |
| Matte Paper | Print | Fine paper texture with minimal ink spread and no strong color shift. |

## Validation

The desktop app and focused test targets build successfully. The preset registry, effect raster checks, preset service, and custom authoring tests pass. Inventory validation and its adversarial checks also pass.

The authoring test covers scope exclusion, layer-local capture, built-in protection, duplicate-name rejection, explicit update versioning, disk round trips, and rollback when saving fails. The UI test loads the real Main.qml editor with an isolated photo and library, checks immediate save/favorite refresh, and exercises duplicate-name feedback and explicit update.

Native macOS screenshots are reviewed at 1280×800 and 900×700. Evidence and exact commands are in [the UI evidence directory](evidence/preset-library-2026-10-05/README.md).

This pass provides basic checks for the added looks and rendered review of the preset UI. It does not claim full photographic visual qualification for every look.
