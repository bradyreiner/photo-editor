# P22 Photo proof rail and comparison insets

Status: guarded native proposal only; no app/MCP calls or native project writes.

## Baseline and evidence

- P21 checkpoint: `marketing/native-recreation-v4/polish-pass-2026-10-30/native-projects/looksy-polish-pass21-film-polish.json` (project SHA-256 `7a99aa8d2fe790a58a20ebced6ff04f0e912dc584e2f34b1bdcfef7a1cbc0ef9`, native document hash `8c125de95668c44c2a578721ae5ece46bad59b155391f9bf7655efca677a3c86`).
- P21 readback audit: `marketing/native-recreation-v4/polish-pass-2026-10-30/p21-film-layout/pass21-film-polish-native-saved-audit.json`; saved copy verified, 196 tracks, 1,414 clips, 1,398 unchanged original clips. Render parity remains a separate review.
- Closed P16/reference pairs reviewed: 20s (frame 600), 24s (720), 30s (900), 38s (1140). P16 was rendered against the earlier P16 document hash; the P21 guard uses its own saved hash.

## Source-correct findings

1. At 20s, the boat-photo comparison is composed cleanly in both images, but native BEFORE/AFTER copy floats over the plate. Reference uses dark inset cards; the After label is lime and reads “AFTER / THIS EDIT.” P21 still has transparent backgrounds, zero padding, and a white “AFTER” title. The fragment styles the eight existing label clips across 16–39s, reusing their existing opacity curves.
2. At 24s, the numeric HIGHLIGHTS −12 / SHADOWS +12 cards are part of the accepted photo UI work. They are outside this fragment; all those clips remain unchanged.
3. At 30s, the image and copy remain legible but the reference-only proof rail is missing. The source shows “CAMERA RAW → LINEAR LIGHT → LOOKSY COLOR → sRGB” below the image, with Looksy Color and arrows in lime. The proposal creates seven short editable title clips, staggered and faded over the exact 27–32s phase, using G20–G26, whose P21 clips have no overlap in [27,32).
4. At 38s, the curve graph is the correct phase-specific tool. The comparison labels remain but no proof rail should persist after 32s.

Badge coordinates use the source Stage (650,120) at scale 1.1. The measured reference text glyph center is y≈209.5px; set native `positionY=.194`. Before is left anchored at x=672 (`.35`), After right anchored at x=1728 (`.9`). Source CSS `#0b1324dc` is stored as native ARGB `#dc0b1324`; `backgroundPadding=14` and radius5.5 reproduce the source inset without adding shape tracks.

The seven rail items are native title clips. Their positions are derived from the source flex row and reference pixel runs (label starts 662, 845, 1054, 1261; y glyph rows 919–931). Sparse native easing approximates the source quartic/quintic opacity curves; review entry and exit frames in a native render before accepting.

## Native edit bounds

- 22 operations, split into 15 existing setters/title creates and 7 receipt-bound opacity setters.
- 8 existing comparison labels get only source text/style/background updates. Their opacity and other keyframes must retain the stored SHA-256.
- 7 new native title clips; 0 new tracks. No bitmap overlays, masks, generated images, or global reorders.
- The plan binds exact P21 hash and track/clip snapshots. Rebind from a fresh document if the live hash changes.

## Limitation

This rail is a native editable explanation of the color path. It does not close the live photo-grade/RAW engine proof gap. P16 comparison states 22–39 are static image plates, and the native photo grade implementation needs its own engine/pixel proof.

Executable fragment: `marketing/native-recreation-v4/polish-pass-2026-10-30/p22-photo-proof/p22-photo-proof.fragment.json`.
