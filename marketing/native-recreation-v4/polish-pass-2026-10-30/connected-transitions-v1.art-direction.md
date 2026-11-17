# Connected transition proposal — RAW → Library → Canvas → Carousel

Status: native authoring proposal, unbound and not rendered. The saved pass2 JSON is used only as a read-only planning snapshot. The fragment deliberately contains an invalid starting-document hash sentinel, so `author_native.py` must reject it until the root binds the exact fresh native hash and rechecks all clip preconditions.

## RAW → Library (57–63s): the same boats photograph lands in its grid cell

Hold the broad outgoing boats preview through 60 seconds. Over the last two seconds, ease the actual image frame up and right while shrinking and tilting it to the pose of the existing `IMG_4969` grid thumbnail. Start that existing thumbnail 0.2 seconds early with its already-authored entrance curve. Its existing first frame begins transparent at −18 degrees; the outgoing image reaches that exact pose at 61.8 seconds, then fades over 5 frames while the incoming thumbnail becomes visible. The outgoing image window contracts with the photo, so the boats scene occupies the same real grid cell at the cut.

No alternate photograph or new card is introduced. The preview and thumbnail use different existing renditions/crops of the same boats scene; the native full-resolution frames must confirm that the content reads as one match. The preview crop and incoming card aspect differ slightly, so review for a crop jump and adjust the pose only after seeing rendered output.

## Canvas → Carousel (157–161.4s): the shoe window becomes a three-panel panorama

Keep the current poster artwork and copy readable through 157 seconds. Then move the existing orange shoe cutout leftward and down into the existing first Carousel panel. Ease its rotation from −23 degrees to upright, scale its frame to the panel width, and slide its real sequence-space rectangular mask toward the left-panel bounds. Keep the shoe at full opacity until the existing 159.5–160.5 second veil begins, then fade it beneath that veil.

The next artwork is intentionally different: the existing mountain photo appears on three already-created clips sourced from the same `IMG_5071` photo. Patch only those clips' local-0 and first-1.3-second mask edges. Each panel begins as a narrow vertical slice, then opens left-to-right in a staggered half-second ease. The middle and right panels follow the left; after 161.4 seconds, their current native curves continue unchanged. This is a designed window match-cut from shoe poster to panorama, not a same-image morph.

This motion uses native 2D per-clip frame offsets, uniform scale, rotation, and rectangle masks. The inspected MCP catalog has no composition group-transform or 2.5D camera action, so the proposal does not claim a camera move. It adds no decorative light overlay, photo, track, source swap, or title fade.

## Scope and review

`connected-transitions-v1.commands-fragment.json` lists the stable clip IDs, source-snapshot preconditions, native actions, and stale-hash requirement. It changes the outgoing RAW image, the existing matching Library thumbnail timing, the shoe cutout's nearly static geometry curves, and the opening left/right mask edges on the three existing panorama photos. The three panorama clips' later motion, vertical mask bounds, images, phone panels, captions, and track structure remain outside this proposal.

`connected-transitions-v1.storyboard.json` requests 22 native Program frames around both transitions. Root should review those moving intervals and adjacent-frame pairs after native execution; the storyboard itself is not render evidence. Reject or revise the transition if media is not ready, a frame is stale, the match-cut obscures the image, the triptych seams break, or any existing text becomes hard to read.
