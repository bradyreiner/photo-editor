# Bounded title PNG loop: P13 scope and proof

The bounded H.264 renderer now decodes a title PNG once, then repeats that decoded input for the exact number of frames in its bounded segment. The loop sits before the title's frame, scale, position, effect, and opacity filters. Those filters still process every output frame, preserving authored animation. Caption word timing stays on the existing input path.

## P13 scene scope

Read-only inspection of `native-projects/looksy-polish-pass13-audio-lanes.json` (SHA-256 `a35826ab1806e0161a830435501ba2af7c8beb7923b6a9b1861ffa60855dbd78`) found 114 title clips active from 44.0 to 45.0 seconds. Of those, 111 have clip keyframes and 3 do not; no active title has track keyframes, an enabled source matte or cutout, effects, clip mask, color input transform, frame corner, or transition. The source loop is upstream of those per-frame filters, so its eligibility does not depend on static geometry or opacity. Each segment must still align to the sequence frame clock and contain at most 16 frames; all other inputs retain the previous `-loop 1` image demux path.

## Focused renderer proof

`timeline_sparse_title_filter_workflow_test` passed in Release. Its fixture checks the production argument builder omits `-loop 1` for the bounded title PNG, the filter graph contains a finite `loop=loop=14:size=1:start=0` with an exact 30 fps timebase, and a dynamic title with scale, offsets, and opacity animation still uses that same upstream loop.

The fixture renders 15 frames against black through both the old image2 loop input and the new single-PNG input with production filters. It requires exact equality of all 15 `framemd5` records, including pixel hashes and PTS. The focused CTest receipt is `static-title-loop-focused-release-LastTest.log`.

## Performance evidence limits

An earlier standalone P8 title-only probe with 108 static title layers measured 1.034 s and 427,032,576 bytes peak RSS for repeated image2 looping, versus 0.432 s and 156,254,208 bytes for decode-once looping, with exact 30-frame pixel and PTS matches. That probe used the P8 fixture and does not establish the P13 scene's render time or whole-film speedup. P13's native throughput still needs measurement with the staged binary.
