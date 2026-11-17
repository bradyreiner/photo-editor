# P9 photo chapter timing review

**Scope:** read-only review of the exact recovered P9 project, its staged photo assets and opacity curves, the Remotion source, and one-second reference frames. No app calls or project edits. There is no current P9 native render for 22–38 seconds, so visual conclusions about the native composite remain open.

## Snapshot and project evidence

- Recovered project file SHA-256: `c9bd178371a9f97535db20904749373995a40e2b862a5d289d097f6a9b8c336f`.
- Recovered document hash: `f61f46a891e1224d480498367fddd78ad25ca33887c8618fc5095d3cb1df2456`.
- The P9 project contains nine highlights/shadows plates, nine temperature plates, and seventeen curve plates, with separate masked before plates. These clips are still photos blended by editable opacity curves; the adjustment values are baked into the plate pixels.
- The 35 adjustment plates and three before plates for 22–39 seconds have the same relevant source, timing, keyframes, framing, opacity, masks, and effects fields as P6. Their grade recipes and effect stacks are empty. P9 changes to the nine RAW detail plates begin at 39 seconds, outside this chapter.
- The plates derive from the actual `IMG_4969.CR2` through LibRaw and Looksy’s native CPU color pipeline, then publish as sRGB. The engine receipt explicitly calls them UI plates, not a native UI recording or a complete accuracy qualification.

## Priority 1: render the existing 22–38s photo controls before changing them

The reference source runs highlights/shadows from 22.7–25.8 seconds, temperature from 27.7–30.8, and the two curve moves from 32.9–34.6 and 35.5–37.4. Each sequence then holds its result before handing off. The P6 opacity audit found the plate blends track those source controls within one 30 fps frame; the corresponding P9 clips are unchanged from P6. There is no evidence for a timing edit.

The plate endpoints do change the photo. Existing source-image comparison reports highlights/shadows SSIM 0.994877 and mean luma difference 2.13/255, temperature change mostly in chroma, and a stronger curve response (SSIM 0.859898, mean luma difference 6.98/255). These describe the generated plates, not P9 preview/export pixels. The subtle highlights/shadows response could read weakly in the final composite; verify it in the native render before considering any creative change.

Recommended native review range: **22.4–37.4 seconds** (15 seconds, 450 frames at 30 fps). Compare the photo, control labels/readouts, before divider, and blend at 22.7, 24.0, 25.8, 27.7, 30.8, 32.9, 34.6, 35.5, and 37.4 seconds. Check for a visible result at each control move and clean holds at the handoffs. This range captures all three feature demonstrations and their endpoints.

## Grade-route constraint

The current still-photo grade path accepts static pointwise recipes, but the RAW plates are already rendered with those adjustments, and the shared grade path rejects RAW camera files. Adding a grade to the current 1-x, 2-x, or curve-x clips would post-process already-developed images and double-apply part of the look. The route does not currently provide a safe replacement for these animated plate blends. Keep the authored plates and timing until the native excerpt is reviewed; a live-grade rewrite would need a LibRaw-backed source and preview/export parity for the original CR2.

## Evidence boundary

The source contact sheets are sampled once per second and show the reference presentation only. They cannot establish P9 motion, current native image response, or audio quality. The report makes no continuous-playback or listening claim. The next decision is a native 22.4–37.4s excerpt review, then return to the rest of the film only after that evidence exists.
