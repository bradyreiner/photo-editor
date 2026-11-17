# P15 SyncedTimeline waveform placement

Read-only audit and guarded native edit proposal. No app calls or project changes were made.

## Verified mismatch

The P14 native sample at frame 5985 (199.5 s) places the waveform on the page background at x≈110–946, y≈738. The FilmPolish reference frame at the same timestamp places those bars inside the A1 MUSIC lane, with the lane bounds around x=772–1695 and waveform center near y=930. Compare `native-preview-pass14-partial/frame-5985.png` against `shape-geometry-review-p6/reference-frames/landscape-t199_5.png`.

The saved P14 document contains **one** synced-timeline waveform clip, not eight waveform clips: `4fd16dec-1970-4aa7-9554-9020a67ea357` on G46 (`graphics-1b1ee3fb-43ad-43df-87d8-f141ebd4fbc8`, lane 100045). Its single native vector path contains 640 commands (128 editable bar subpaths), has no path animation frames, and is byte-for-byte the same path as P13. The eight clips added in the P14 audit are reverse-video/title overlays. Guard and move this one waveform clip only.

Saved P14 identity: project file `native-projects/looksy-polish-pass14-preview-sync.json`, SHA-256 `b5859adaa416dfa144c8b72640f040a8113e3dd9365feb34bc7a12a193083a1f`; historical native document hash `88c13f1dc7f48f11002787576b86bd031ccaeb4c6635bb112944e794f7463221`. Rebind the action to a fresh P15 task and fresh current document hash after P14 completes; do not use the historical hash as a live precondition.

## Geometry derivation

FilmPolish's landscape `Stage` is positioned at `(650,120)` with scale `1.1`. SyncedTimeline authors the waveform at stage rectangle `(110,714,840,48)`. Applying that stage mapping gives the target native frame box:

- bounds: x=`771..1695`, y=`905.4..958.2`
- center: `(1233,931.8)` on a 1920×1080 sequence
- scale: `1.1`

The native renderer translates a clip center by `targetDimension * frameOffset * 0.25`, so target offsets are `offsetX = 4*(1233/1920 - 0.5) = 0.56875` and `offsetY = 4*(931.8/1080 - 0.5) = 1.451111111111111`. Keep the existing 128-bar path, viewBox 1000×48, fill `#ffc1e3df`, frame width `0.4375`, and frame height `0.044444444444444446`; scale 1.1 expands the existing frame to the target 924×52.8 px. The visible bar paths end just inside the frame edge by design.

## Guarded action

Only apply if fresh readback still matches the target clip and lane, sequence is landscape 1920×1080 at 30 fps, track G46 is visible/unlocked with no group transform, clip timing is [196,208), and path hash is `2df841972a3ea8989ccb0096728faaf415639be596f29a997f2051bcbc437e63`. The full current geometry must match: width `.4375`, height `.044444444444444446`, offsets `-.8958333333333333/.7333333333333334`, scale `1`, stretch, opacity `1`, rotation/perspective/radius `0`, full crop, no clip keyframes. Stop on any mismatch.

Use one journaled `timeline.action.applyDraftBatch` with a new batch UUID and one typed child action. Preserve every field not listed as a target below:

```json
{
  "id": "synced-wave-stage",
  "commandId": "timeline.action.setTimelineClipFrame",
  "values": {
    "clipId": "4fd16dec-1970-4aa7-9554-9020a67ea357",
    "offsetX": 0.56875,
    "offsetY": 1.451111111111111,
    "scale": 1.1,
    "rotationDegrees": 0,
    "perspectiveX": 0,
    "perspectiveY": 0,
    "opacity": 1,
    "objectFit": "stretch",
    "width": 0.4375,
    "height": 0.044444444444444446,
    "cornerRadius": 0,
    "cropLeft": 0,
    "cropTop": 0,
    "cropRight": 1,
    "cropBottom": 1
  }
}
```

Do not change waveform coordinates, fill, audio, timing, track order, or other scene geometry. After the journaled edit, save and inspect a native frame at 199.5 s (frame 5985): confirm the waveform sits inside the A1 lane, spans the reference track width, remains centered near y=932, and preserves the exact 128-bar source path. Also inspect a frame outside 196–208 s to confirm this twelve-second overlay has no spillover.
