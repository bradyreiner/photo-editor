# P23 late chapter provenance and native proposal

Status: offline proposal only. No MCP calls, native project writes, app changes, or render jobs were made.

## Provenance and guard

- Planning project: `native-projects/looksy-polish-pass22-photo-proof.json`, SHA-256 `b6fc44862237506bb661ce69f3b5cb82e67806fe573d4274dfaa1952b0d4f37b`, native document hash `c2fb601cf1808116a7aaaa87aaecfb22c340b4f4bf719876cb9b8be932918b0c`.
- The plan builder verifies the P22 save checkpoint and compares the Gaussian label's full clip/track/title/variant/keyframe snapshot with P21. That label is unchanged; P23 changes only its color to the source lime.
- P16 native frames and the retained P16 WIP MP4 were compared with the accepted Remotion reference. P16's full render is an earlier document state, so these images identify visible differences but do not prove P22/P23 output. P20 changed a disjoint chapter. At execution, bind a fresh task and exact current document hash, then require every P22 target snapshot and free-track guard; the old P22 hash alone is not an execution grant.
- Creative sources and command schemas are hashed into `p23-late-chapter-proof.fragment.json`.

## Supported visible changes

### Gaussian chapter, 240–244s

At frame 7202 the native label `GAUSSIAN SCENE / CAMERA MOTION` is cream, while the reference source label is lime (`#d7ff48`; native ARGB `#ffd7ff48`). Clip `5d1be438-6ae9-44c2-97f7-5e6ae7aea683` remains at its existing 240–244s interval. The proposal changes only its color. The one-frame entrance difference is left untouched because the retained source proof does not justify retiming it.

### Optional agent proposal, 259–266s

The exact reference frame 7860 shows the outer panel at approximately x=716–1684, y=192–904 in a 1920×1080 frame. P22's panel begins at the same top edge but extends to about y=961. Its existing shape is reduced from 0.712963 to 712/1080 high and recentered at 548/1080, leaving its horizontal bounds, start, duration, colors, and copy intact. Both action pills move from y≈915 to the measured source center y≈835.

The third proposal line wraps as `Keep the photo, mask and type` / `separate` in the reference. Its existing subtitle moves to the source's lower line. Three thin editable dividers are proposed on free graphics lanes G43–G45, using the source border color and staggered quartic ease-out opacity curves. The row's existing photo, title animation, wording, panel fade and original source images are retained.

### Editable layer panel, 266–273s

At frame 8100 the reference reorders `Subject cutout` above `Typography`, shows four right-side visibility glyphs, keeps `Color adjustment` selected, and draws row rules. P22 shows the first two labels in the opposite order, no markers/rules, and a shorter panel. The proposal extends the outer panel to the measured 347px height, positions the existing labels and eight existing row plates to the source rows, and adds four neutral `◉` titles plus a fifth lime marker on free lanes G34–G38. A single hold-keyed `frameOffsetY` animation moves the lime marker through the selected source rows at 1.7-second steps; the row-three highlight remains the existing source-correct selected state around 270s. Four thin editable row dividers use G39–G42.

The source's reorder maps Typography (selected index 0) to row two at local time 0, then Subject to row one at 1.7s, Color adjustment to row three at 3.4s, and Original photograph to row four at 5.1s. Marker positions use the source 58px row increment at the 1.1 stage scale. No tracks are added or reordered.

The photo poster is intentionally untouched. Its source `HikerPoster` uses a shared 13-element transform group; rotating only the photo clip would diverge from the reference. P23 does not claim to fix that unsupported shared-pivot composition.

### Caption readability, 266–273s

The reference title wraps to three lines inside the 470px Caption column; the native title renderer does not reproduce that CSS auto-wrap from the current title block width. The proposal gives the existing title explicit source-equivalent line breaks: `The work` / `stays` / `editable.` and moves its block center to the reference center. It also restores the five source detail lines using the existing copy and moves the detail block to the corresponding reference center. Both existing opacity/entrance keyframe arrays remain unchanged.

### Closing, 285–295s

The reviewed P16 native/reference frames look aligned and readable. No closing changes are proposed.

## Execution and review bounds

The executable proposal has 40 typed operations in three reversible batches of 16, 16, and 8: 24 existing-title setters, 12 creates, and four animation setters. All created clip IDs must come from the authoring receipts. The plan preserves all clip timings, audio, poster elements, existing keyframes, non-landscape overrides, track metadata/order, and all non-target clips. A fresh native A/B render at 240.067, 260, 262, 270, 285, and 295 seconds is required before claiming parity.

Fragment: `p23-late-chapter-proof.fragment.json`.
