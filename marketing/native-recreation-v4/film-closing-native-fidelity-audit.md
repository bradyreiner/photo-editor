# Film and closing native fidelity audit

The source film places `FilmChapter` at global 184–252 seconds and `BuyerClosing` at 280–298 seconds in `CreativeFilm.tsx` (sequences at lines 51–70). The native Film/Closing v3 plan covers those same ranges. This audit concerns two source treatments that were omitted from that plan.

## 244–252 seconds: framing cards

`FilmPolish.tsx` phase 7 starts at local chapter second 60, so its 8-second framing scene is global 244–252. It uses `creative-polish-v2/arrival-12.mp4` in a rounded 830×467 landscape card and a separately framed 260×462 phone card. The landscape card has `perspective(1500px) rotateY(-7deg)` and shadow `0 30px 70px #0008`; the phone has `rotate(5deg)` and shadow `0 25px 70px #0009` (`FilmPolish.tsx:908–952`). The native v3 plan already uses the same source, start, duration, rounded cards, and phone rotation (`film_closing_v3_plan.py:1488–1516`), but has no perspective or frame shadows.

The current native `frameShadow` contract can express the two static shadows. It is optional on `setTimelineClipFrame`, persists on the clip, and reaches preview/export; the export helper scales sequence-canvas pixel values and maps CSS blur radius to approximately half-radius Gaussian sigma (`agent_timeline_commands.h:435–475`, `timeline_frame_shadow.cpp`). The new additive fragments provide:

| Output | Card | Shadow color | Offset | Blur radius |
| --- | --- | --- | --- | --- |
| Landscape | Landscape film | `#88000000` | `(0, 33 px)` | `77 px` |
| Landscape | Phone | `#99000000` | `(0, 27.5 px)` | `77 px` |
| Portrait | Landscape film | `#88000000` | `(0, 30 px)` | `70 px` |
| Portrait | Phone | `#99000000` | `(0, 25 px)` | `70 px` |

Landscape values include the source Stage's 1.1 scale; portrait Stage scale is 1.0. The fragments preserve all existing frame values and are marked unexecuted. They refer to the existing chapter result IDs and must be bound to a completed authoring receipt before execution. Shadow pixel parity has not yet been visually verified.

The existing native `framePerspectiveX/Y` does not reproduce the source 3D rotation. Its homography moves only the two top corners while fixing both bottom corners. CSS `perspective(1500px) rotateY(-7deg)` changes all four corners because depth changes both horizontal and vertical projected coordinates. Setting native perspective values would create a different keystone. The typed four-corner action now persists editable corners. New landscape and portrait correction fragments map the card video and separate title overlay to the source transform; both are prepared and unexecuted, and their native pixel parity is not yet verified. Export parity also needs a focused check: the Program QML projection receives each clip's local item dimensions, while the current FFmpeg path applies the quad after constructing a full-canvas layer.

## 280–298 seconds: paper grain

`BuyerClosing` adds a full-canvas `PaperGrain opacity={0.12}` after the hero, desktop card, and CTA (`BuyerScenes.tsx:463–565`). The source implementation is a desaturated SVG `feTurbulence` overlay using `fractalNoise`, `baseFrequency=.78`, three octaves, seed 19, and stitched tiles, composited with `soft-light` (`rebuild/Kit.tsx:63–90`). Native clip blend modes include soft-light, but the admitted generators are desktop-light, transition-veil, linear-gradient, and vector-path; none exposes SVG turbulence settings (`timeline_document_controller.cpp:6285–6355`). Desktop-light is an animated caustics field with different math and is not a faithful grain substitute. This feature remains unsupported until a deterministic editable turbulence generator is available in both preview and export.

## Artifacts

- `film-closing-shadow-corrections-v1-landscape.commands-fragment.json`
- `film-closing-shadow-corrections-v1-portrait.commands-fragment.json`
- `film_closing_native_corrections.py` (offline fragment builder)
- `film-closing-perspective-quad-corrections-v1-landscape.commands-fragment.json`
- `film-closing-perspective-quad-corrections-v1-portrait.commands-fragment.json`
- `film_closing_perspective_quad_corrections.py` (offline fragment builder with independent matrix cross-check)
- `tests/timeline_perspective_shader_layout_test.cpp` (source-layout regression for the perspective shader uniform block)

No live app edits, native actions, or builds were performed for this audit.
