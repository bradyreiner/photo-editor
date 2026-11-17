# Connected transition v3 review: preserve the Canvas chapter wipe

Status: review-only recommendation. No native actions were applied. The safe v3 decision for Canvas → Carousel is to preserve the existing P9 composition, transition veil, and panorama-mask timing; do not run the shoe-only transform or early panel-reveal actions from v2.

## Evidence set and target identity

- P9 saved project: `native-projects/looksy-polish-pass9.json`, SHA-256 `51531c69e912a50908cb99ce5a795d697c7fb22e24a2dcf90cbc886b9a35f0b4`.
- P9 checkpoint document hash: `4d8157be5d32d94ec6ad7c01b2705175adb09a637afedc6d151148f8e2e72a48`.
- P3 planning project SHA-256: `1c3eac4733991206cd41c661cd15c716513bfca520e2c52e2a6fb388b97e4234`.
- All six v2 target clips are present and their full clip objects, keyframes, track indices, and track IDs match P3 exactly in P9: outgoing RAW preview `fa5c794e-6855-4b35-bd89-301b19a218ef`; incoming Library card `d1b30d81-d03c-46d9-993a-431bc4d70711`; shoe `32248f5d-6503-4a90-96a4-3cb6b8b3a2ce`; panorama panels `27e510ac-6602-47a7-a09f-014aebefcd95`, `b8905213-e33b-411a-aec1-a54dd1cc66a0`, and `6550e260-d20c-4b66-a399-514eab2bdf53`.
- Reference evidence is the saved one-frame-per-second landscape set, especially `frames/reference-one-per-second/landscape-159.png`, `landscape-160.png`, `landscape-164.png`, `landscape-165.png`, and `landscape-166.png`. These are sampled source frames, not a continuous-motion review.
- This is not proof of the current P9 native render. No native render or live app call was made for this review.

## Exact source-to-project boundary

The source reference at 159s is the complete shoe poster with the copy “A poster today. A thumbnail next.” and “OUT THERE.” At 160s the film has changed to the mountain poster under “One photograph. Across every canvas.” That poster remains intact at 164–165s; the first reference frame showing the three-panel split is 166s.

P9 has a full-composition transition veil, clip ID `00103e11-12b9-4835-9b98-9c5c4dbd3de0`, active from 159.5 through 160.5. The native renderer uses a quintic smootherstep path and clips an opaque `#0b0d12` fill plus light field to it. At exactly 160.0s, its path spans x=-0.20W to 1.20W (with a further ±0.06W curved edge), so it covers the whole canvas during the chapter cut. This transition already moves the entire composition through a nonblank branded wipe.

The P9 outgoing poster at 159.9667s has 17 active clips: 12 title/shape layers, 3 motion-generator layers, the shoe photo, and the continuous audio clip. Among them are the chapter label, headline/body, “OUT THERE.” artwork, shoe, desktop light field, vector path, and the veil. At 160.0s, 74 clips are active: 60 title/shape layers, 7 motion-generator layers, 6 photo layers (three panorama clips and three phone panels), and audio. The incoming chapter also carries the mountain text/artwork, export labels, gradients, light field, vector path, and the same veil.

The relevant boundaries are therefore scene-level. Moving only the shoe layer cannot carry its poster background, geometric artwork, copy, light field, and labels as a connected object.

## Why v2 fails this boundary

The v2 shoe operation starts changing the shoe at local 8.5s = global 158.5s while the rest of the outgoing poster remains still. At that start pose, the saved shoe values are X=.6875, Y=.044444, scale=1.363636, rotation=-23°, and mask left/top/right/bottom=.109375/.083333/.890625/.863889. The proposal then moves it to X=-.9675, Y=.537963, scale=.586970, rotation=0° by local 9.5s = global 159.5s, while the poster's copy and artwork are unchanged.

The renderer positions a clip center at canvas center plus one quarter of the canvas times its frame offset. That puts the proposed endpoint shoe center at about (496, 685) in the 1920×1080 sequence. Its frame is about 465×349 px at the proposed scale; its PNG remains fully opaque as a foreground shoe cutout because the existing opacity curve holds at 1 through the end. The panel-1 photo mask is a 464×580 px sequence-space window beginning at global 160, while the shoe mask is slightly narrower (about 450×580 px) and the shoe clip ends exactly at global 160. Thus the proposal does not turn the shoe into a hole or a panorama image; it puts a visible shoe over the future panel window, then removes that shoe layer at the chapter boundary. The existing whole-scene veil conceals the exact cut at 160, but it does not conceal the isolated shoe motion during 158.5–159.5.

The 18 v2 panel edits create a second definite regression. They collapse each current panel mask to zero width at local 0, hold it closed for 0.5–0.833s, then reopen by local 1.0–1.333s. P9 instead has the three panorama masks already open at local 0 and held through local 6 (global 166): panel 1 `.1371875–.3790625`, panel 2 `.3790625–.6209375`, panel 3 `.6209375–.8628125`. Adjacent edges match exactly, producing a continuous held panorama before the authored split. V2 would make the panel split appear about five seconds earlier than the source and remove the panorama during the start of the incoming scene.

The renderer math supports the same caution for the other v2 transition: sequence masks are applied after frame scaling/rotation/cropping (`timeline_document_controller.cpp:55085–55164, 55663–55682). The RAW proposal uses a fixed axis-aligned 255×190 px output-space mask around a -18° rotated incoming card. It clips corners that remain part of the unmasked incoming tile. The matched center is (914, 490) px, but matching center and rotation alone does not prove matching silhouette or crop. P9 also has an existing 61.5–62.5s full-composition veil and many caption/export layers changing at that cut, so a two-photo-only transition should not be called a connected whole-scene move.

## V3 recommendation

For Canvas → Carousel, make no per-clip pose or mask change in this pass. Keep the shoe poster assembled as authored through global 159.5s, preserve the existing 159.5–160.5s full-canvas veil, and retain the three panorama-mask hold through global 166s. That preserves the source's chapter timing and avoids an isolated shoe pop or early triptych reveal.

A true connected movement from the shoe poster into the Carousel scene needs a group transform that includes the background/artwork, copy, shoe, labels, and other active layers. The inspected native action set has per-clip geometry and masks but no whole-composition group transform. Until a native A/B review shows a specific flaw in the existing veil or panel split, preserve the current cut and do not simulate a group move by shifting the shoe alone.

Review the next native baseline at 159–161s and 164–167s before considering a change. The source-frame evidence says the poster holds through 165s and the three-panel split is visible by 166s; the native output is still required to qualify the exact transition timing.

## Implementation guardrails for a future proposal

- Keep P9's `Transition Veil` clip, timing, and renderer parameters unchanged.
- Keep the shoe, poster copy/artwork, three panorama photos, phone panels, and panel titles on their current timings and tracks.
- Do not add a fade to black or fade the shoe against an empty background.
- If the native baseline differs from the sampled source at 159–167s, propose only the smallest stable-ID changes that account for every visible layer and keep the P9 source untouched.
- Keep the RAW → Library v2 group on hold pending its own native A/B review; do not rebind the old v2 fragment as safe.
