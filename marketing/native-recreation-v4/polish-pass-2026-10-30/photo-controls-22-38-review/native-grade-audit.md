# Photo control chapter audit: 22–38 seconds

Read-only review of P6 (`looksy-polish-pass6.json`; document hash `167bf753c47f812c4da8d16ab75607a51a54f4ada94b850e9cf9409ad696b423`; saved-file SHA-256 `43bc28e826c913e5ace1da497ced74e8fb617f668c997cb825f9aaff65e79263`). No app, timeline, source-photo, or source-code mutation was made.

## What is already real

The boat photo changes come from actual `IMG_4969.CR2` renders through LibRaw and Looksy’s native CPU color pipeline. `studio-color.cpp` decodes 16-bit camera-WB data, uses linear sRGB, applies the photo-engine adjustments, then writes through ACES2 Hellwig JMh and sRGB publication. The asset receipt is explicit that these are engine-generated plates for an animated UI, not native UI recording or complete performance/accuracy qualification.

P6 then uses static photo clips and opacity blends: nine H/S plates, nine color plates, and seventeen curve plates, each with a masked “before” plate. The clips keep timing, position, mask, and opacity editable, but the H/S, temperature, and tone-curve parameters are baked into the image pixels. Those clips have empty effect stacks and no color-grade recipe.

The reference source uses one progress curve for its readout and plate blend. P6’s H/S and color plate transitions line up with that progress within about 33 ms (one frame at 30 fps). The curve sequence also aligns within about one frame, including the hold while the shadow point finishes and the highlight/midtone point begins. This audit found no timing correction justified by the saved P6 data.

## Image response

I compared the authored engine plates directly, at their 1440×960 source size. The H/S endpoint pair has SSIM 0.994877, PSNR 38.37 dB, and mean luma difference 2.13/255. The photo visibly changes, though the whole-frame difference is modest. Temperature change is more chromatic than luminance (SSIM 0.991775, PSNR 37.42 dB). The tone-curve endpoints produce the strongest change (SSIM 0.859898, PSNR 28.70 dB, mean luma difference 6.98/255).

These are source-plate comparisons, not native preview or export pixel checks. The full-resolution reference contact sheets and plate endpoint pairs are in this directory.

## Native-grade route

The MCP catalogue exposes a photo-engine grade recipe and keyframes for `highlights` and `shadows`. The current H/S target clips are `mediaType=photo`, however. `TimelineDocumentController::setClipColorGradeRecipe` accepts only `video` or `adjustment`, so the shared grade route cannot target these saved still-photo clips.

A new adjustment clip is created at the top eligible picture lane, where it applies to the composite below. That would grade the photo along with the background and graphics. The ordinary clip effect list includes Exposure, Contrast, and Saturation but not the matching highlights/shadows controls; applying it to P6’s `1-0.png` would also grade an already-developed image. Neither is an honest substitute.

The 28 MB CR2 is present in `marketing/animated-trailer/capture/desktop-source/IMG_4969.CR2`. `ffprobe` recognizes its TIFF wrapper, but a local FFmpeg decode fails with `rps 0 invalid`; the timeline controller/playback path does not call the LibRaw camera-linear decoder. A direct RAW timeline source is therefore not verified.

## Recommended next improvement

The defensible improvement is a native still-photo grade path backed by LibRaw, so the existing boat photo can remain one editable RAW source with native `grade:highlights` and `grade:shadows` keys. Keep the before-side duplicate ungraded at exposure +1.4 EV, mask it to the existing comparison region, and animate the after-side source from highlights 0→−35 and shadows 0→+35 over 22.7–25.8s. Preserve the copy, timing, image framing, fixed divider, and established color/curve beats.

That needs an isolated source/preview/export parity fixture first. The minimum fixture and frame assertions are in `native-grade-audit.json`. I’m providing no P6 command fragment: the current native contract rejects the target clip type, while the available adjustment-clip path has the wrong scope.
