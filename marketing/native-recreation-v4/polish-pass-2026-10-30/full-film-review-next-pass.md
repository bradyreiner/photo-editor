# Full-film review: next polish pass

**Date:** 2026-10-30  
**Edit reviewed:** P3 snapshot `native-projects/looksy-polish-pass3.json`, 298 s, 1920×1080, 30 fps; resource `timeline:3db4e4fb-6737-415c-b6ee-dde766e7c237`, hash `978cd63bbb0da6d0fde8998e21fb6e76dbe98ad40b53d6c7e72747d4e3e40236`. The P3 save receipt is readback-verified in `checkpoint-pass3-result.json`.

## Evidence boundary

I reviewed the existing landscape and portrait reference contact sheets, sampled at one frame per second across 0–298 s. This is a frame-sampled review, not continuous playback; it can identify repeated sampled compositions but cannot prove that motion or transitions are absent between samples. I did not listen to either soundtrack.

There is no P3 trailer export or true P3 creative excerpt in `exports/` or `review-range-runtime-proof/` as of this review. The latest creative native export is the older 10-second v8 opening prefix. Its 1920×1080 H.264/AAC streams decode, but signal analysis measured native AAC RMS at 0.0 over those ten seconds while the reference measured −20.28 dBFS. This is a real issue in that old prefix and a reason to check the P3 mix; it does not prove the current P3 mix is silent. The adjacent 2-second, 4K H.264/AAC file in `review-range-runtime-proof/` is a generated-content range-export test (60 frames, nonzero test audio), not trailer footage. The exposed-app chapter screenshots in `native-baseline/` are from an earlier, dirty live state with frame identity marked unverified; I do not use them to claim current P3 visual defects.

## Direction

Keep the current 4:58 product-tour structure and feature coverage. The stronger opportunity is to make the existing seconds carry clearer operations and visible outcomes. The dark editorial layout, moving rainbow field, boat opener, traveler footage, careful title treatment, and consent-forward assistant sequence are recognizable strengths. These notes target specific sampled holds and feature demonstrations; they are not a recommendation for a blanket shorter cut.

## Highest-impact next-pass findings

### 1. Make the photo controls visibly change the photograph (00:22–00:38)

In the landscape samples, the boat image, left-side headline and adjustment UI repeat across 00:22–00:38. The exposure move at 00:16–00:21 is much easier to read as an operation because the displayed control value changes against the same image. The subsequent highlight/shadow, color and curve beats are individually useful, but their sampled frames spend too long showing a label and panel without a comparably obvious image response.

**Polish within this span:** preserve the highlights/shadows, color and curve features, but use each panel’s time for one clear control move and its visible before/after on the boat. Keep one clean hero frame after the adjustment. Retain the 00:16–00:21 exposure sequence and the later detail/color work at 00:39–00:57; the latter has stronger visual differentiation, especially the selection-edge detail and orange color-mix values.

**Evidence:** [landscape 00:00–00:39 samples](frames/reference-landscape-000-039.png); [landscape 00:40–01:19 samples](frames/reference-landscape-040-079.png). These show the reference only, not P3 rendered output.

### 2. Give the adaptable-canvas reveal a clearer cause-and-effect arc (02:40–03:04)

The existing 24-second sequence covers two worthwhile steps: “One photograph. Across every canvas.” runs 02:40–02:55, then “Designed together. Ready to swipe.” runs 02:55–03:04. The sampled frames show a strip growing into three campaign panels, followed by a phone-style swipe view. The repeated card compositions make the reveal feel slower than the design change itself.

**Polish within this span:** keep the three separate 4:5 outputs and the phone preview, but make the first panel visibly become the connected triptych and then show one decisive swipe through the finished set. Let the shared layout, crop and type continuity carry the “compose once” claim; avoid holding the same completed strip under multiple seconds of unchanged copy. Preserve the two-part story and its finished output.

**Evidence:** [landscape 02:40–03:19 samples](frames/reference-landscape-160-199.png); [portrait 02:40–03:19 samples](frames/reference-portrait-160-199.png). The portrait layout reflows the heading and output stack rather than cropping the landscape composition; retain that behavior.

### 3. Keep the video chapter footage-led, with one unmistakable proof for each claim (03:16–04:00)

The story opens well on travel footage at 03:04–03:16. The following 44 seconds (03:16–04:00) cover edit assembly, keyframed motion, color/effects, sound and delivery. The strongest sampled proof is the “NEPAL” title moving over the street footage with its curve, followed by the color-to-monochrome treatment. The timeline, waveform and export-panel views are useful supporting evidence, but the one-second samples show long stretches dominated by similar interface framing.

**Polish within this span:** retain the full feature coverage and the travel shots. Make the edit claim show a single trim/ripple operation and the resulting cut in playback; keep the animated title as the motion proof; keep one readable color/B&W before-and-after; pair the waveform with an audible soundtrack excerpt; and finish on the chosen delivery settings/output. This can make the existing 44 seconds feel like a sequence of results without dropping the tools the tour promises.

**Evidence:** [landscape 03:20–03:59 samples](frames/reference-landscape-200-239.png); [portrait 03:20–03:59 samples](frames/reference-portrait-200-239.png). This describes the reference’s sampled presentation. It is not a finding about P3 native playback or export, which is not available for this chapter.

### 4. Stage the optional assistant as a visible, user-controlled workflow (04:12–04:40)

The 28-second section has a good trust arc: connect a local model (04:12–04:19), describe a request and show the plan (04:19–04:26), show the editable result (04:26–04:33), then show review choices (04:33–04:40). Keep “if you want it,” the proposed-step review, the editable poster and the user’s final choice. In the sampled landscape and portrait sheets, several adjacent seconds repeat the same connection or plan card; the causal steps are less prominent than the persistent headline and panel.

**Polish within this span:** make the connection state resolve, reveal or highlight proposed steps in order, then show one concrete edit appearing in the poster before the approval controls arrive. Leave the final choice visible long enough to read. Use the existing 28 seconds to show these state changes clearly; do not remove the optionality or approval step to chase a shorter runtime.

**Evidence:** [landscape 04:00–04:39 samples](frames/reference-landscape-240-279.png); [portrait 04:00–04:39 samples](frames/reference-portrait-240-279.png). Small text legibility should be assessed on a full-resolution native excerpt before adjusting type size; the contact sheets alone are too reduced for that judgment.

### 5. Re-qualify the P3 soundtrack and close (03:46–03:52; 04:49–04:58)

“Picture meets sound” at 03:46–03:52 visibly promises a mixed timeline, and the reference finishes with a Looksy mark and website CTA at 04:49–04:58. The older native prefix’s silent AAC samples and the earlier unverified black close capture make these important checks, not established current failures. The source’s stereo AAC metadata and the synthetic 4K test do not establish that the P3 edit has an audible, well-balanced soundtrack or the correct final tail.

**Next proof:** review a real P3 excerpt covering 03:46–03:52 with decoded audio samples and human listening, then verify the 04:49–04:58 picture and audio tail in the actual render. Keep the moving mark, concise CTA and deliberate end hold if the native output matches the reference.

**Evidence:** [old native v8 prefix review report](technical-v8-prefix10/review-report.json); [old native v8 00:00–00:10 decoded frames](frames/native-v8-landscape-prefix-000-009.png); [generated-content range-test verification](review-range-runtime-proof/verification.md). These artifacts establish only the stated historical prefix and infrastructure-test facts.

## Retain

- **00:00–00:10:** brand light field into the boat/editor opener. The old native v8 prefix is visually close to its reference at sampled frames.
- **00:16–00:21:** clear exposure-control response; use it as the model for other photo-operation proofs.
- **03:04–03:16:** travel footage creates a strong shift from image/design work into filmmaking.
- **03:28–03:46:** keyframed title and color/effect result give the video section memorable visual payoffs.
- **04:12–04:40:** optional plan, editable output and user choice are valuable product differentiators; preserve the whole arc.
- **04:49–04:58:** retain the full mark and CTA as the intended close; confirm it in the P3 native render.

## Review artifacts referenced

- Full landscape and portrait reference sheets: [`frames/`](frames/), especially `reference-landscape-000-039.png` through `reference-landscape-280-297.png` and corresponding portrait sheets.
- Earlier creative/technical review and native screenshot limitations: [`review.md`](review.md) and [`native-baseline/`](native-baseline/).
- Current P3 checkpoint and receipt: [`checkpoint-pass3-intent.json`](checkpoint-pass3-intent.json) and [`checkpoint-pass3-result.json`](checkpoint-pass3-result.json).
- Latest creative native export reviewed here: [`native-v8-bounded-thread-opening-prefix-10s.mp4`](../exports/native-v8-bounded-thread-opening-prefix-10s.mp4).
- Range-render implementation proof, explicitly not trailer content: [`review-range-runtime-proof/`](review-range-runtime-proof/).
