# Portrait v9 continuation: P11–P13 semantic map

Offline crosswalk only. It does not bind landscape UUIDs into the portrait document or alter the frozen portrait plan.

## Identity and binding rule

- Portrait checkpoint: `projects/looksy-native-portrait-in-progress-checkpoint-1.json`, SHA-256 `5c5ac69035e789c4a9d5917594d718fcf035bcb619cbdd080f0c231c5364f564`; 1080×1920 at 30 fps, 413 tracks and 502 clips.
- Corrected parent plan: `full-portrait-v9-schema-corrected-parent.commands.json`, 3,643 logical steps. Its proved prefix ends at step 1,636; the next (rejected) action is step 1,637, `library-carousel/p-lib-62-IMG_4788-image`. The P11–P13 counterparts below are all suffix creations, not clips in the 502-clip prefix.
- Landscape UUIDs are evidence references only. Bind each portrait target from the result of its own `film-closing/...` creation step (`createdClipId`) after that step succeeds. Keep all portrait-specific positions, masks, trims, animation, and composition from the portrait plan.

## P11: the 196-second timeline shell

The landscape P11 audit reports nine changed clips and eleven actions at 196 seconds, preserving source, timing, title styling, and track order. The portrait parent has corresponding future native objects. The role mapping is based on the authored object identity and UI label, not on copying landscape coordinates.

| Landscape P11 reference UUID | Portrait suffix creation step | Portrait role |
| --- | --- | --- |
| `f3d2a426-fc08-46f6-8230-6998a4f753a7` | `film-closing/synced-timeline-shell` (3032) | Main timeline shell |
| `7dbaf3e9-4480-4bf6-acbb-adb6ffcd9b0b` | `film-closing/synced-program-viewer-backdrop` (3049) | Program viewer backdrop |
| `3b382f34-cc96-42a0-919b-cddfd33bae04` | `film-closing/synced-inspector-background` (3085) | Inspector background |
| `61fbf265-a79b-4b25-94b2-3b6430cc3424` | `film-closing/synced-title-clip` (3133) | Timeline title clip bar |
| `b1999a32-ed8a-4b27-b616-aeb8d7a41344` | `film-closing/synced-thumbnail-label-backdrop-arrival` (3142) | Arrival thumbnail label backdrop |
| `7e65b9d6-78d3-406b-93e9-4345588586a5` | `film-closing/synced-thumbnail-label-backdrop-ridge` (3151) | Ridgeline thumbnail label backdrop |
| `87b6d3f6-2617-4186-99f3-502ed9adf236` | `film-closing/synced-thumbnail-label-backdrop-city` (3160) | Kathmandu thumbnail label backdrop |
| `2ed8d2cf-f3f8-43d0-9e64-bc593afeb122` | `film-closing/synced-thumbnail-label-backdrop-night` (3172) | Night thumbnail label backdrop |
| `e2e8a20a-73b6-4694-b35e-3938a88d6b2e` | `film-closing/synced-playhead` (3181) | Animated timeline playhead |

The landscape correction neutralized the outer frame on these objects and cleared/restored the playhead animation around its frame edit. The portrait parent already authors the equivalent objects with its own normalized layout. Review the portrait render before considering a similar correction; do not paste any P11 transform or landscape clip ID into it.

Evidence: `pass11-timeline-shell-native-audit.json` and `pass11-timeline-shell-196.execution.json`.

## P12: city comparison on the original source

P12 converted two landscape comparison clips from preprocessed media back to the original `creative-polish-v2/city-10.mp4`, then authored a shared grade on both and Black & White on the monochrome clip. The portrait plan already has the matching logical objects, but currently points its two comparison creations at processed variants.

| Landscape target (reference only) | Portrait suffix step to retarget to original source | Preserved portrait follow-up steps |
| --- | --- | --- |
| `c41c417a-963b-47a8-85fa-cac5108ddb60` `grade-city-graded` | `film-closing/grade-city-graded` (3272) | `grade-city-graded-frame` (3273), `grade-city-graded-mask` (3274), `grade-city-graded-wipe` (3279) |
| `d8d47522-2f58-4a32-8e8b-402b091fe657` `grade-city-monochrome` | `film-closing/grade-city-monochrome` (3276) | `grade-city-monochrome-frame` (3277), `grade-city-monochrome-mask` (3278), `grade-city-monochrome-wipe` (3280) |

Use the admitted original source at `marketing/animated-trailer/public/media/creative-polish-v2/city-10.mp4` (`assetId`: `native-trailer:creative-polish-v2/city-10.mp4`). The portrait plan's source reference `film-closing/grade-original-city` (3269) already uses that file. P12 native readback shows source SHA-256 `2cd86e83c922dce29d30662e77de114454730abc73ef958a188774e4244e6b03` on both repaired landscape targets.

After creating both portrait comparison clips, add journaled native setter steps bound to each step's `createdClipId`:

| Proposed suffix operation ID | Target | Native operation |
| --- | --- | --- |
| `film-closing/grade-city-graded-exposure` | `grade-city-graded` | exposure `0.25` |
| `film-closing/grade-city-graded-contrast` | `grade-city-graded` | contrast `14` |
| `film-closing/grade-city-graded-vibrance` | `grade-city-graded` | vibrance `12` |
| `film-closing/grade-city-monochrome-exposure` | `grade-city-monochrome` | exposure `0.25` |
| `film-closing/grade-city-monochrome-contrast` | `grade-city-monochrome` | contrast `14` |
| `film-closing/grade-city-monochrome-vibrance` | `grade-city-monochrome` | vibrance `12` |
| `film-closing/grade-city-monochrome-black-and-white` | `grade-city-monochrome` | add enabled `Black & White`, amount `1` |

These are proposed IDs for a later reviewed suffix extension; they are not appended to the frozen 3,643-step parent here. Keep the parent’s portrait frame, mask, wipe keyframes, timing, and ordering. Do not grade the separate `grade-original-city` reference clip. P12’s approved values are visible in `pass12-city-grade-task-reviewed.json` and the saved P13 native project.

## P13: visual audio-mixer shapes, not audio-track edits

The P13 task targets three landscape title shapes at 226 seconds. Its native audit shows no track/clip order change. It normalizes two rounded-rectangle outer frames and clears/reapplies the animated playhead frame; it does not author audio-track mute, gain, or role state.

| Landscape P13 reference UUID | Portrait suffix creation step | Portrait role |
| --- | --- | --- |
| `1c4d634c-0db6-4f74-8f9d-7070e7285364` | `film-closing/audio-mixer-panel-0` (3355) | Blue music lane panel |
| `96f04d43-a20b-4832-af5e-18afd950bc4f` | `film-closing/audio-mixer-panel-1` (3388) | Green location lane panel |
| `9838556a-610a-417a-ad6e-f44bf54fbd90` | `film-closing/audio-mixer-playhead` (3421) | Yellow animated playhead |

The portrait suffix also creates their labels and waveform-generator chunks under `film-closing/audio-mixer-*`. Keep those suffix shapes and waveforms in their portrait-authored geometry. This visual mockup is separate from the actual New Gods soundtrack work in P9.

Evidence: `pass13-audio-lanes-native-audit.json`, `pass13-audio-lanes.execution.json`, and the saved `native-projects/looksy-polish-pass13-audio-lanes.json` (file SHA-256 `a35826ab1806e0161a830435501ba2af7c8beb7923b6a9b1861ffa60855dbd78`). The audit marks post-render review pending.

## P9 reference and scope

Use `pass9-native-readback-audit.json` plus `pass9-photo-timing-review.md` as the existing P9 audit; do not remap that chapter from landscape UUIDs. The readback audit covers 1,222 clips, 114 targets, and verifies non-target and source/timing preservation. The 22–38 second photo-control visuals are editable opacity blends over developed UI plates, not a safe live-grade rewrite; render the native 22.4–37.4 second range before changing them.

The New Gods audio arrangement is a separate task from the P13 mixer mockup. `portrait-next-native-steps.md` records that its additional source inputs are outside the frozen 104-path portrait admission scope. Keep it in a separately re-proved task rather than silently expanding the current portrait grant.

## Remaining gates

1. No P11–P13 target is in the proved portrait prefix: every mapped target is a future suffix object, so every portrait ID must come from its own successful creation receipt.
2. The portrait continuation still lacks fresh `portrait-v9-copy-result.json`, `portrait-v9-document-result.json`, and `portrait-v9-admit-result.json`. The previous v10 copy receipt is a failed import and cannot be reused. Follow the safe lifecycle in `portrait-next-native-steps.md`; require a fresh imported document with the 413-track/502-clip graph and fresh resource ID before binding or executing the suffix.
3. Render review remains separate evidence. The landscape P13 audit has `nativePostRenderPending: true`; neither the source audit nor this crosswalk proves portrait pixels, animation, or audio.

## Post-suffix native compaction gate

The frozen portrait parent creates 950 Timeline tracks total; 539 `createTimelineTrack` steps remain in the 2,006-step suffix. Finish the suffix under its exact frozen 104-path scope first. Before retime polish and before applying P11–P14 carryovers, while the portrait task is still owned, generate a **new** `compactTimelineGraphicsTracks` plan from the exact post-suffix native document/hash, apply that plan through the owned action, then save and audit its readback. Do not bind to a pre-compaction plan or use landscape IDs.

Landscape v30 is precedent for this action, not a portrait target geometry: `evidence/native-v30-compact-plan-independent-check.json` checked 33,342 overlap-order pairs, reduced 912 graphics tracks to 164 (192 total tracks), and confirmed all graphics clip IDs were retained. Compaction can change track IDs. Use the fresh plan/apply receipts to rebuild the portrait track-ID map before any later P11–P14 edit that targets a track; retain clip IDs only where the native compaction receipt confirms they survived.
