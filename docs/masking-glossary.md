# Masking Glossary

This glossary defines the vocabulary used by masking, selection, brush, smart selection, and retouch UI. Use these terms consistently in code comments, QML object names, tests, and user-facing tooltips.

| Term | Definition |
| --- | --- |
| Selection | A non-destructive matte used to target local adjustments. A selection does not bake pixels into the source image. |
| Retouch | A repair workflow that stages, applies, or stores a retouch patch in the edit recipe. The original file remains unchanged. |
| Retouch patch | The recipe entry and pixels/metadata produced by Remove, Heal, Patch, Clone, Dust, or Red Eye. |
| Mask set | A group of selection sources that compose into one editable selection result. |
| Source | One component inside a mask set, such as brush, object, polygon, lasso, luminance, color, depth, alpha, sky, or subject. |
| Operation | The way a source changes the mask set result: Add, Subtract, Intersect, Replace, Exclude, or Union. |
| Overlay | The canvas visualization of a selection or retouch target; it is not the image data itself. |
| Generated mask | A selection source produced by Vision, SAM, or a clearly labeled local heuristic/fallback runtime. |
| Heuristic mask | A generated mask from local image rules, such as sky color/position or skin-tone color estimates, rather than a semantic model. |
| Stale generated mask | A generated mask whose source image, crop, rotation, preview resolution, or runtime/model metadata no longer matches the current edit state. |
