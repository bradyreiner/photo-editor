# P15 audio readout polish proposal

Status: prepared offline; no app calls, project mutations, builds, or worker changes.

## Scope and proof gate

Limit P15 to the two native title readouts in the six-second audio chapter. Keep all audio clips, track state, vector-path generators, paths, and other clips unchanged. The active P14 task is separate; bind the eventual mutation to a fresh P15 task and current document hash after P14 is released.

P13 saved-project identity: `native-projects/looksy-polish-pass13-audio-lanes.json`, SHA-256 `a35826ab1806e0161a830435501ba2af7c8beb7923b6a9b1861ffa60855dbd78`. Its native excerpt snapshot hash was `2322ea35da00c9c0c1fe25b705a9270fc2cf4a7d9301e8a94e7be1877d44d9b0`. The retained native frame `native-audio-lanes-pass13-after/frame-6788.png` is global frame 6788 at 30 fps, or 226.2667 s. It is before the music duck and while the location bed is at its −36 dB floor. That frame cannot verify the middle-of-section duck/fade display; inspect a freshly rendered native frame at 229 s before accepting the readouts.

## What the saved project already does

The audio mixer has a music gain curve of −3.7 dB through 226.8 s, ramps to −15.7 dB by 228 s, holds through 230.5 s, ramps back to −3.7 dB by 231.7 s, then holds to 232 s. The duck is therefore −12 dB relative to the music's −3.7 dB baseline, with an amplitude ratio of `10^(-12/20) = 0.251188643` at the duck plateau.

The location bed's native track mute flag is false throughout. Its audio gain is −36 dB through 226.8 s, ramps to +7.7 dB by 228 s, holds through 230.5 s, ramps back to −36 dB by 231.7 s, and stays at that floor through the clip end at 232 s. Label the envelope itself; do not call the track muted.

The empty `Clip.keyframes` arrays on the 16 wave clips do **not** mean their drawings are static. Each native vector-path generator has 181 local-time coordinate frames over 0–6 s, and the renderer interpolates those coordinates at clip-local time (`render_vector_path_frame`). The 8 music paths (G10–G17) have 16 bars in a 1000×64 view box, centered on y=32; their peak half-height at local 0.8 s is 25.21–28.79 units and at local 2–4.5 s is 6.33–7.23 units, a 0.2511886 ratio. The location paths (G21–G28) have the same native shape/frame structure, with a 1-unit visual floor at local 0/0.8 s and peaks of 14.82–29 units at local 2–4.5 s. This is consistent with the mixer endpoints already being encoded into the existing path frames. Do not add another amplitude/opacity transform or rewrite the paths. The actual native frame at 229 s remains the visual confirmation gate.

The vector bars’ authored center is exactly y=32/64. Their native clip placement uses `frameOffsetY` with a quarter-canvas translation (`targetHeight * frameOffsetY * 0.25`), so the canvas center is `0.5 + frameOffsetY/4` at scale 1 and with no extra group transform:

| Wave group | Clip IDs in lane order | `frameOffsetY` | Normalized center Y | Center at 1080 px |
|---|---|---:|---:|---:|
| Music, G10–G17 | `5f298fc8-b476-46b5-9e8f-8e8bcadb1a85`, `79d9c1d7-9d85-48ee-8b61-57c07f6144c7`, `824a3116-6b02-40d4-b4e8-1fc2632c09b3`, `23e0d571-47b9-4444-8309-a119d64b3a13`, `47058640-d159-4b4f-a203-511bd82a9dd8`, `09f96b88-90d6-4796-920f-cb9c969ca160`, `d8aa89a7-e29e-4314-839e-bdc7e7a2f6ba`, `da847d3b-aacd-4f9d-ab65-4052b33b0348` | 0.795185185 | 0.698796296 | 754.70 |
| Location, G21–G28 | `41218ac3-066c-4d2a-9cae-04259d4848db`, `81353691-94d6-46a5-a0eb-97f76846a5e6`, `32b502cb-c940-49d1-b227-b666860c80e8`, `a731b045-afff-4702-add0-e3825be4141f`, `be85354e-6322-491f-882c-0bf821a59ad9`, `47db446e-f015-417d-bff6-b5e31e241da3`, `5b851e48-dea1-41cd-b1a3-596a6f9b16f4`, `f2d90f94-5ade-4e41-933c-5805140ea859` | 1.304444444 | 0.826111111 | 892.20 |

The readout text is stale: music clip `5f7067a0-88d9-4867-9946-fc802d4cd6a8` says `0.0 dB` although the saved mix is −3.7 dB at the beginning and −15.7 dB at the duck plateau; location clip `934fe5b7-b800-4615-98fb-b7f370f5cf12` says `MUTED / ON` for the entire interval, hiding its timed gain states.

## Bounded native edit

Use one journaled `timeline.action.applyDraftBatch` with a fresh batch UUID after the P15 task is admitted. The typed child actions are already in the synchronous batch allowlist. Operate only after a fresh read confirms the same timeline ID, sequence frame rate 30 fps, both target clip IDs/tracks/text/start/duration, no overlapping clip on G20 in [226,232), and unchanged source audio IDs/keyframes/mute flags. Hard-stop on any mismatch; use the fresh `currentHash` from that read, not the P13 snapshot hash.

1. Keep music clip `5f7067a0-88d9-4867-9946-fc802d4cd6a8` at its current timing/style and set only `values.text` to `DUCK / −12 dB`.
2. Keep location title clip `934fe5b7-b800-4615-98fb-b7f370f5cf12`, set only its text to `−36.0 dB`, then shorten that same clip to [226.0,226.8).
3. Create four titles on the existing G20 track for the remaining intervals: `FADE IN` on [226.8,228.0), `+7.7 dB` on [228.0,230.5), `FADE OUT` on [230.5,231.7), and `−36.0 dB` on [231.7,232.0).

All boundaries are exact 30 fps frame boundaries: [6780,6804), [6804,6840), [6840,6915), [6915,6951), [6951,6960). The four new clips must copy the current location title style: Space Mono, 16.5 px, weight 400, regular, zero letter spacing, 1.01 line height, `#fff1efdf`, right aligned, position X `0.89140625`, Y `0.7798449074074075`, transparent background, shape width `0.55`, shape height `0.16`, zero shape radius, white shape stroke, stroke width 0, opacity 1. Preserve the title lane, and verify style equality after creation.

Typed batch operation payloads (create operations also return `createdClipId`; store those returned IDs in the task receipt and use them for post-edit readback):

```json
[
  {"id":"music-label","commandId":"timeline.action.setTimelineClipTitle","values":{"clipId":"5f7067a0-88d9-4867-9946-fc802d4cd6a8","values":{"text":"DUCK / −12 dB"}}},
  {"id":"location-head-text","commandId":"timeline.action.setTimelineClipTitle","values":{"clipId":"934fe5b7-b800-4615-98fb-b7f370f5cf12","values":{"text":"−36.0 dB"}}},
  {"id":"location-head-time","commandId":"timeline.action.setTimelineClipTiming","values":{"clipId":"934fe5b7-b800-4615-98fb-b7f370f5cf12","trackId":"graphics-c046e23c-67d8-4e1f-8daa-7d373186a716","startSeconds":226.0,"durationSeconds":0.8}},
  {"id":"location-fade-in","commandId":"timeline.action.createTimelineTitle","values":{"trackId":"graphics-c046e23c-67d8-4e1f-8daa-7d373186a716","templateKey":"basic","startSeconds":226.8,"durationSeconds":1.2,"values":{"text":"FADE IN","fontFamily":"Space Mono","fontSize":16.5,"fontWeight":400,"italic":false,"letterSpacing":0,"lineHeight":1.01,"color":"#fff1efdf","alignment":"right","positionX":0.89140625,"positionY":0.7798449074074075,"backgroundColor":"#00000000","shapeWidth":0.55,"shapeHeight":0.16,"shapeCornerRadius":0,"shapeStrokeColor":"#ffffffff","shapeStrokeWidth":0}}},
  {"id":"location-peak","commandId":"timeline.action.createTimelineTitle","values":{"trackId":"graphics-c046e23c-67d8-4e1f-8daa-7d373186a716","templateKey":"basic","startSeconds":228.0,"durationSeconds":2.5,"values":{"text":"+7.7 dB","fontFamily":"Space Mono","fontSize":16.5,"fontWeight":400,"italic":false,"letterSpacing":0,"lineHeight":1.01,"color":"#fff1efdf","alignment":"right","positionX":0.89140625,"positionY":0.7798449074074075,"backgroundColor":"#00000000","shapeWidth":0.55,"shapeHeight":0.16,"shapeCornerRadius":0,"shapeStrokeColor":"#ffffffff","shapeStrokeWidth":0}}},
  {"id":"location-fade-out","commandId":"timeline.action.createTimelineTitle","values":{"trackId":"graphics-c046e23c-67d8-4e1f-8daa-7d373186a716","templateKey":"basic","startSeconds":230.5,"durationSeconds":1.2,"values":{"text":"FADE OUT","fontFamily":"Space Mono","fontSize":16.5,"fontWeight":400,"italic":false,"letterSpacing":0,"lineHeight":1.01,"color":"#fff1efdf","alignment":"right","positionX":0.89140625,"positionY":0.7798449074074075,"backgroundColor":"#00000000","shapeWidth":0.55,"shapeHeight":0.16,"shapeCornerRadius":0,"shapeStrokeColor":"#ffffffff","shapeStrokeWidth":0}}},
  {"id":"location-tail","commandId":"timeline.action.createTimelineTitle","values":{"trackId":"graphics-c046e23c-67d8-4e1f-8daa-7d373186a716","templateKey":"basic","startSeconds":231.7,"durationSeconds":0.3,"values":{"text":"−36.0 dB","fontFamily":"Space Mono","fontSize":16.5,"fontWeight":400,"italic":false,"letterSpacing":0,"lineHeight":1.01,"color":"#fff1efdf","alignment":"right","positionX":0.89140625,"positionY":0.7798449074074075,"backgroundColor":"#00000000","shapeWidth":0.55,"shapeHeight":0.16,"shapeCornerRadius":0,"shapeStrokeColor":"#ffffffff","shapeStrokeWidth":0}}}
]
```

The normal task journal and rollback must wrap the full batch. Do not issue these child actions directly through an unjournaled controller route.

## Readback and acceptance

- Confirm exactly five location readout segments on the original G20 track with no gaps/overlaps and IDs/timings/text matching the plan. Capture all four created IDs from the native batch receipt.
- Confirm the music title kept its original ID, timing, lane, and style. Confirm the source music/location clips and their exact audio automation/mute state are unchanged.
- Confirm all 16 vector-path clips, generator parameters, coordinate frames, style, and timing are unchanged.
- Render and inspect native frames 6795 (226.5 s), 6825 (227.5 s), 6870 (229 s), 6930 (231 s), and 6954 (231.8 s). Frame 6870 is the required midpoint check: music at −15.7 dB, location at +7.7 dB, with the existing vector paths showing the corresponding smaller/larger waveform shapes. Do not accept based only on the earlier frame 6788 at 226.2667 s.

## API limits

`setTimelineClipTitle`, `setTimelineClipTiming`, and `createTimelineTitle` are the typed actions needed for this readout-only edit. `setTimelineClipAnimation` does not expose dynamic `titleText` or `frameHeight`; the controller's valid clip-keyframe list likewise omits `frameHeight`. No generator-parameter update action was found, and none is required for this proposal because the native vector paths already contain local-time samples. Keep waveform edits out of P15.
