# Native trailer source readiness and Timeline replacement gap

Audit date: 2026-10-30. This is a read-only source/asset audit. No app was launched, no MCP mutation was invoked, no worker was changed, and no C++/QML implementation was edited.

## Document and capture identity

- The inspected worker receipt is `marketing/native-recreation-v4/evidence/dense-timeline-ui-review-worker.json`. Its current candidate is `/private/tmp/looksy-dense-timeline-v7iaxqgh/data/timelines/documents/3db4e4fb-6737-415c-b6ee-dde766e7c237.json.autosave`.
- The protected live document is 1920x1080, 30 fps, 298 seconds, 192 tracks, dirty and unsaved. The captured document hash is `ecc3f49c507c877d1d008d674b8b1b101ec8b9cbbe28bcfcbb2a4c1920dbe273`; the receipt's saved v30 source hash is `20cebcdbfd4eb5d6bd18f70e3245992c1ebbfa9252b2ead883dbd4c2f4cc2082`. These refer to different states; keep the live autosave and v30 source distinct.
- The native baseline captures are [t00005.000.png](native-baseline/t00005.000.png) (SHA-256 `d3088307861f76e73bf763e1342b8db46b680b801643042d48f47f5c446f4320`) and [t00289.000.png](native-baseline/t00289.000.png) (SHA-256 `3a1c03bdffd07c7c3955ae06aba67d34c2ef1712480b05edf9eadc72e899a5e0`). Receipt SHA-256: `fef5789be267e8a4266805187c03be0f03cd278fe4877ee7b1f503a48f72ddc3`.
- Both requested seeks report `framePresentationStatus: unverified` and `decodedFrameIdentityVerified: false`. At t=5, the seek snapshot says `mediaReady: false`, while the later performance sample says `mediaReady: true`, `playbackHealth: stalled`, 49 frame samples, and measured key `95a0c114-a4dc-41d4-8c72-4fca4b9acc32|1329x748|60cf6694cded598e3a0f2bfbcab4d3ff1c487317d1980ea40ea6bdf49af3073d`. The prefix is the selected G17 compound clip ID, not the G3 still-photo ID. Treat those conversion metrics as evidence about that video source; they do not identify the rendered multi-layer composition.
- At t=289 the selected Program clip is a photo, so `hasVideoSource: false`, `mediaReady: false`, and `effectivePreviewQuality: none` are expected. The same `measuredSourceKey` and 49-sample count are carried forward from t=5. They are stale for the closing frame.

## Opening hero: source file is valid; rendered discrepancy remains

The live autosave's G3 track is a graphics track serialized as `kind: video`, `role: graphics`, ID `graphics-4169b6c8-8422-4fce-bc4c-7bd04678047b`. Its `hero-photo` clip ID is `6dcf29a7-0bbd-4975-a0cb-4ebe7b09e666`, on 0–12 seconds. At sequence time 5 its evaluated state is opacity 1, frame offset X 0.48, scale 1.04, width 0.75, height 1, fill mode, with no mask and no effects. Those values match the intended right-side photo treatment.

The current clip path is `/Users/bradyreiner/projects/PersonalProjects/photo-editor/marketing/animated-trailer/public/media/creative-polish-v2/curve/curve-16.png`. It exists, is a readable 1440x960 PNG, and its current SHA-256 `b23ed6d62aae81036e3dfc9c305c1498d545da9ff48a5958be930a8815efee30` matches both the candidate's `sourceContentSha256` and `marketing/native-recreation-v4/source-manifest.json`. Direct image inspection shows the boat photograph, not an application screenshot. Do not replace or overwrite this asset based on the magenta t=5 screenshot alone.

The 0–12 second opening keys are internally coherent: G3 hero opacity is 0 through t=2, 1 from t=3 through t=8, then fades to 0 by t=10; its frame eases from X 0.46/scale 1.08 toward X 0.50/scale 1.00 by t=8. The opening wordmark/title layers are visible through t=8 and fade with the hero; selected G17 is opacity 0 through t=8 and rises to 1 by t=10. G3 has no clip mask/effect at t=5. This timing agrees with the reference's boat entrance and G17 handoff.

The manifest also contains the full-size boat source `/Users/bradyreiner/projects/PersonalProjects/photo-editor/marketing/animated-trailer/public/media/IMG_4969.jpg` (1800x1200, 485098 bytes, SHA-256 `73f1dfaf468aa80e27a70b5e1616233032ab338c0b36408ecc05aedeac230271`) and a 720x480 copy at `creative-polish-v2/IMG_4969.jpg` (SHA-256 `4e7f39de837800bbe6ed3657661497941b7e202c343df98895233a245595c373`). Both show the same boat scene. These are valid immutable replacement candidates if a later review confirms the G3 source itself needs replacement; current evidence does not establish that.

The t=5 screenshot does show the magenta title composition without the expected boat image. The source is present and hashes match, so a source-path or missing-file repair is not supported. In `TimelineView.qml`, a non-primary photo layer is loaded through the normal QtQuick `Image`: `programSceneImageSource()` returns `safePosterSource()` and the background photo `Image` uses the file URL asynchronously. It does not use `image://library-original` or `image://timeline-playback`. The custom timeline playback image provider is for the primary video frame.

At t=5 the selected primary is G17 (`95a0c114-a4dc-41d4-8c72-4fca4b9acc32`), a 12-second ProRes 4444 1920x1080 compound movie at `marketing/native-recreation-v4/projects/compound-a1702a1c-e127-562a-a62f-f0db01f6a1ee.mov`, SHA-256 `c6e4db2694a096c4a96ab2c0abaa438c0786850321370d4320de5ef0a0de33bb`. Its G17 opacity is keyed to 0 at local time 5. The movie and its cached poster are not the expected multi-layer composition: an offline frame at t=5 shows the separate “Your work. From start to finish.” composition. The cached poster `/private/tmp/looksy-dense-timeline-v7iaxqgh/cache/timeline-posters/451bfcd58c8d6ce4c1ee00b004af91ff7dc9ed1d.jpg` is 960x540, SHA-256 `187e84a2f8fce8f8e7318aeb413c7edd1b5525a6349ee5138236c2019e3a0ac8`, and also shows that separate composition.

The concrete next check is the G3 background photo Image's runtime `status`, `source`, `paintedWidth`/`paintedHeight`, effective visibility, and opacity after it reaches `Image.Ready`, paired with a fresh Program screenshot at the same document hash. The existing QML wrapper object name is `timelineProgramBackgroundImage-6dcf29a7-0bbd-4975-a0cb-4ebe7b09e666`; it reports painted dimensions but not the child Image's load status. Current MCP video metrics cannot establish that the still-photo layer loaded or was composited.

## Closing: valid assets, entrance state at t=289 differs from reference

The current autosave's closing sequence spans 280–298 seconds. At local time 9 (sequence time 289):

- G7 `closing-desktop-image` uses `desktop-launch/raw-editor.png` and is opacity 0, which is consistent with the reference end card replacing the app screenshot at this point. The asset is a valid Looksy editor screenshot with the boat image, and its manifest hash is `8ebb266571865d16535c0c0e2b9bb5ca5cb272293bd73206d3e1c8a09c3e3398`.
- G8 `closing-app-wordmark` uses the valid `looksy-logo-dark.png` asset (manifest SHA-256 `17199f363f1168d68bd9ac1f1c9a7a8d3401ab04982175021be3c8dedda811b9`). Its evaluated opacity is 0.209876543 and frame offset Y is -0.450667186.
- G9 tagline and G10 CTA are also at opacity 0.209876543. G10 CTA frame offset Y is 0.73115191. G13/G14 footer labels are opacity 1.
- G2 Desktop Light Field is opacity 0.65. G12 Fractal Noise is opacity 1 with soft-light blending. Its parameters are valid in the document: 1920x1080, base frequency 0.78/0.78, 3 octaves, seed 19, stitched tiles.

The reference montage `frames/reference-landscape-280-297.png` shows the full Looksy wordmark, tagline, CTA, and colorful end-card background by t=289 (4:49). The native screenshot at that time is almost black with a tiny white mark. The authored wordmark/tagline/CTA entrance state is concretely behind the reference at t=289; use the existing keyframes/positions as the repair target. The still-photo source is valid, and the video-readiness fields at t=289 are not applicable. The capture alone does not prove whether the missing colorful background is a procedural-generator load/render issue; inspect the G2/G12 QML generator Image/Canvas readiness before changing their creative parameters.

Across 280–298, G7's editor screenshot is fully visible through t=287 and fades out by t=288.5. G8/G9/G10 begin fading in at t=288.5, are only about 0.21 opacity at t=289, and reach full opacity by t=290. The screenshot at t=289 is therefore one second earlier in the entrance than the reference montage. If the reference timing is the target, move that entrance earlier; separately inspect the QML photo and generator image readiness because the screenshot is much darker than the keyed layers predict.

## Native source-replacement capability audit

`TimelineDocumentController::replaceSelectedClipFromSourceImpl()` accepts a target whose `TrackKind` is `Video`. Graphics tracks are also `TrackKind::Video` (with a separate `role == "graphics"`), so this selected-clip UI action is not restricted to the `clips_`/media lanes. It accepts photo sources and rejects audio-only sources. The current implementation uses the selected `ClipRef`, so it is not a stable-ID, task-owned automation adapter.

The MCP Timeline action registry registers `timeline.action.createTimelineClip`, which authorizes `filePath` with `authorize_path("import_media", ...)`, but registers no source-replacement action. `loadSourceClipFromGallery()` is a UI Q_INVOKABLE and is not exposed as a stable-ID task action. Creating a new clip and deleting the old one would lose the target's identity and is not an acceptable replacement seam.

There is also a still-duration limitation in the selected UI route: `kDefaultStillDurationSeconds` is 4 seconds; loading a photo with duration 0 produces a 4-second Source range. The replacement method requires the marked source range to be at least as long as the target edit. A 12-second photo hero therefore cannot be replaced from a normally loaded photo Source unless the still is first given a longer synthetic duration.

The current replacement copies the target clip ID and retains its effects, grade/LUT, keyframes, transitions, output overrides, mask/matte/blend, crop, opacity, and much of its frame transform. It does **not** copy `frame_width`, `frame_height`, `frame_corner_radius`, or `frame_shadow`; `buildGalleryClip()` initializes those to defaults. G3 currently uses width 0.75, so the existing route would change its visual geometry even if the target/source range were accepted. For source-derived data, update identity to the new source rather than retaining the old media's hash/signature.

Recommended native seam for the next implementation increment:

1. Add a task-owned `replaceTimelineClipSource` action with required `clipId`, `assetId`, `filePath`, and `displayLabel`; optional source in/out, `posterSource`, and fit-to-fill flag. Resolve `clipId` directly to a controller `ClipRef`, then reuse the same replacement core as the selected UI action. Keep the current audio rejection, target track lock checks, source validation, one-undo behavior, and stale/owned-task guards.
2. Reuse `buildGalleryClip()` and the controller's source metadata path. For a photo, set a synthetic source duration at least equal to the target's preserved timeline duration before validating marks; still frames have no media-time limit. For a video, require completed native duration analysis before applying trims.
3. Carry the new source's asset ID, canonical path, source size, identity signature and poster while preserving the existing clip ID, group, sequence start/duration, style, and all keyframes. Add the omitted frame width/height/corner radius/shadow to the shared replacement core.
4. Register the path-policy callback as `authorize_path("import_media", filePath)` and route through `mutation_owner` so the action remains owned by the current Timeline task. Return the unchanged target `clipId` and a clear recoverable error without partial mutation when resolution, authorization, analysis, or validation fails.

Focused regression coverage for that increment:

- Replace a 12-second photo on a graphics track by stable clip ID; assert one existing clip remains, the ID/track/start/duration are unchanged, the source ID/path/identity change to the imported image, and still duration does not reject the replacement.
- Seed width/height/corner radius/shadow, opacity, mask/matte/blend, crop, effects/grade, transitions, output overrides, and representative keyframes; assert they survive replacement exactly. Assert one Undo restores the old source and one Redo restores the replacement.
- Reject missing/locked IDs, audio-only media, invalid/unreadable files, and video sources whose analysis is still pending without changing the document hash.
- Invoke the registered action through the task command registry; assert `import_media` authorization is requested for the exact file path, missing authorization rejects before mutation, and the owned-task guard still rejects an unowned invocation.
