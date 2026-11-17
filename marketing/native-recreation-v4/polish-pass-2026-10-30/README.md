# Looksy opening and RAW chapter polish pass

This is a bounded native continuation plan for the existing trailer. It preserves the current copy, typography, source art, images, split comparisons, detail scene, and overall look. It adds no generated media and creates no new graphics track. Native output review remains with the root owner.

## What is weak in the current source

- The opening `hero-photo` clip has 181 sampled keys each for `frameOffsetX` and `frameScale`, plus 85 opacity keys. Those arrays approximate a slow curve with 30 fps linear segments. The image starts at opacity zero, reaches its strongest value around 8.23 seconds, then fades by 12 seconds, so the opening reveal is late.
- The desktop-light generator has 93 sampled `generator:gather` keys, and the gradient has 85 sampled opacity keys. Opening title and support copy also use 36–85 sampled keys. The stored authoring is frame-by-frame linear motion even where the values describe a smooth ease.
- The existing 39–50 second detail composition carries two overlapping copy groups. Old title/body/section label clips start at 39 and end at 50; replacement clips start at 44 and end at 49. They overlap for five seconds. The `ADD` label (45–49.7) and `ERASE` label (48.5–49.5) overlap for one second.
- The 12–39 second RAW comparison already uses existing before/after photos and curves. Keep those images and their framing; this pass does not introduce replacement photos. At 12–16, the original shot is a `photo` clip. The shared timeline grade API lacks a shared-photo-grade adapter for this target; the adjustment-clip workaround is deferred because its effect would include the existing composited text and background.
- The `app-wordmark` clip has a saved transform far outside the canvas. Leave it alone until native frames at 0 and 2 seconds establish whether it is intended to be offscreen.

## Applied title-overlap correction and next native fragment

The root applied [raw12-62-title-overlap-v1.commands-fragment.json](raw12-62-title-overlap-v1.commands-fragment.json) through native Timeline actions. Its receipt records all four operations as successful. The root's 45.733s capture now shows one replacement heading, while the detail photo still covers the left copy.

[raw39-50-detail-rightcolumn-v1.commands-fragment.json](raw39-50-detail-rightcolumn-v1.commands-fragment.json) plans native frame and animation setters for the existing detail base plus nine stable photo-detail plate IDs. It applies the agreed X/Y/scale inset map, compresses the transform/radius samples into sparse eased native keys, and leaves all plate opacity crossfades, text, pixel-grid/readout clips, media, and track order alone. Its stored live-document hash is deliberately stale after the applied title correction; the root must rebind it from fresh native inspection and check the exact stable-ID preconditions before execution. The geometry proposal bounds the plate expansion to x=.385–.805 and keeps a measured .085 minimum gap from the reference copy bound near x=.30. Root owns native execution and rendered review.

Sequence-time boundaries are on the 30 fps grid:

- Old copy fade: frame 1307 (43.5667s) through frame 1319 (43.9667s).
- Replacement copy begins: frame 1320 (44.0s).
- ADD label fade: frame 1444 (48.1333s) through frame 1454 (48.4667s).
- ERASE label begins: frame 1455 (48.5s).

## Opening motion proposal (held)

[opening-motion-v1.commands-fragment.json](opening-motion-v1.commands-fragment.json) is a proposed edit for six existing clips through `timeline.action.setTimelineClipAnimation`, but the root observed a magenta/no-hero frame near 5 seconds and suspects the source is not ready. Do not execute this proposal until native media readiness and the intended photo are understood:

- Give the existing hero image a restrained left-to-center drift and 1.08-to-1.0 scale over roughly eleven seconds, with sparse eased keys. Reveal the image early and hold it for the main title.
- Let the existing desktop-light gather resolve over the image entrance, with a low-opacity peak and a controlled tail.
- Keep the existing dark gradient behind the copy at a low, steady opacity while the copy is on screen.
- Reveal the primary headline first, then the existing support line; hold both long enough to read and clear them before the chapter cut. Keep the meta label through the opening and fade it before 12 seconds.

The native schema accepts `frameOffsetX`, `frameScale`, `frameOffsetY`, `opacity`, and `generator:gather` for these clip types, and supports `ease-out`, `ease-in-out`, and `hold`. This contract does not expose tangent values for Bezier easing, so the fragment does not claim direct tangent authoring. The root must resolve the t=5 no-hero/magenta frame and confirm `mediaReady` before any opening mutation or opening render review.

Typography stays with the current Anton headline, Space Grotesk support copy, and Space Mono labels. No new graphic, font, or track is introduced.

## Photo-grade path (deferred)

[raw12-16-grade-mcp-sequence.json](raw12-16-grade-mcp-sequence.json) is an exploratory schema note only, not a production edit. The root deferred the adjustment-clip approach because it could grade the text, UI, background, and already-processed comparison asset. Any later grade must first be inspected in the native context and prove a visible change on the ordinary photo clip with a matching native exposure readout, or document that the shared-photo-grade adapter is missing. Do not execute the adjustment-clip sequence in this pass. Earlier schema exploration considered:

1. Add one four-second native adjustment clip at 12 seconds. The protected project’s top graphics lane is empty over this range, so the supported add operation should reuse that lane and keep the track count at 192; abort if live inspection shows an overlap or different top lane.
2. Attach a pointwise shared photo grade to the returned adjustment clip ID, starting at 0 EV.
3. Animate native `grade:exposure` from 0 EV to +0.35 EV by local 1.2 seconds (sequence 13.2 seconds), then hold through 16 seconds.
4. Inspect the grade receipt for shared-engine preview and pointwise sequence-export support. Compare frames at 12, 13.2, and 15.9667 seconds to confirm the image pixels change and the existing type remains legible.

This grade is deliberately confined to the original camera shot. Applying it across the later before/after comparison sequence would change both halves and weaken that demonstration. The adjustment clip sits over the existing composite, so its effect on the image and overlaid type must pass the native pixel review before acceptance.

## Contact sheet proposal

[preview-contact-sheet-proposal.json](preview-contact-sheet-proposal.json) lists exact 30 fps frame indices for four root-owned native review sheets: opening and chapter boundary, RAW comparisons, detail-copy transitions, and color/delivery. They are requested native Program frames, not generated images or existing evidence. The adjacent 43.9667/44.0 and 48.4667/48.5 pairs are required to verify the two copy fixes.

## Stale guard and execution

The protected v30 source has SHA-256 `20cebcdbfd4eb5d6bd18f70e3245992c1ebbfa9252b2ead883dbd4c2f4cc2082` and deterministic source-document hash `6f6bcbce033152a7d7401a075e18ab8f6c622109b8f0f7a1a0dd24c6b5cdbdb2`. The active worker is a hydrated native document, whose current hash supplied by the root is `ecc3f49c507c877d1d008d674b8b1b101ec8b9cbbe28bcfcbb2a4c1920dbe273` under task `9b086e79-262f-4f46-a1c6-9cbc99769d1b`.

The title-overlap fragment was initially bound to that live hash. The detail geometry fragment contains the same pre-overlap hash as a deliberate stale guard; root must rebind it from fresh native `timeline.inspectAgentDocument` output after the title receipt and before execution. Confirm stable-ID clip metadata, active landscape frame fields and opacity/keyframe state against current native readback. If a call times out or its result is uncertain, inspect the task journal and document before continuing; do not resend the operation blindly. The geometry fragment uses reversible native batches capped at 16 operations.

## Not qualified here

This subtask did not edit the protected project JSON, app source, running app, or MCP session. The root applied the four title-overlap actions in the live native Timeline and captured the shown 45.733s frame; review established that the replacement copy is single but still covered by the detail image. The new geometry plan and contact-sheet proposal are not native evidence. The root owns fresh-hash binding, execution, full-render review, and any next decisions.
