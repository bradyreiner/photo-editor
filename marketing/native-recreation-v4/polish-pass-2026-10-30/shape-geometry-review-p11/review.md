# P11 geometry candidates: 226s, 232s, 244s

## Baseline and target-state audit

P11 is `native-projects/looksy-polish-pass11-timeline-shell.json`, canonical document hash `bbf04be03ea2c7848adfc4b549fbacd60dd99adbf006faa29f3006ee4147553d`, file SHA-256 `0816fe71e26c30ffb7b8a02f831c7643455782f3fc9c5a0f29d8518ea4fa656f`. It contains 196 tracks, 1,277 clips, and uses the landscape output variant. The prior P11 task `22e3198f-f7a0-49fc-8b20-efb097876d1e` is released; do not reuse it.

I compared all seven targets in the three P10 fragments against P11: stable clip and parent track IDs, asset IDs, timing, trim, opacity, inner shape/text styling, main outer-frame values, keyframe channels, and landscape geometry/framing curves. Every target matches its P10 precondition at `1e-9`. The 181-key playhead curve and 181-key landscape framing curve both match after normalizing away default handle metadata. The only whole-document changes recorded by the P11 audit are the 196s editor-shell group, so these 226/232/244 targets remain untouched.

No P11 native-render frames for 226, 232, or 244 were found in the current evidence tree. The correction remains conditional on capturing and reviewing the native baseline first. P6 source-reference stills exist at the three chapter starts.

## Fresh P11 baseline frame list

| Chapter | Native capture times (seconds) | Stable targets | Source reference | Expected visible defect if the saved duplicate transform is present |
| --- | --- | --- | --- | --- |
| 226 audio lanes | 226.000, 229.000, 231.966667, 232.000 | `1c4d634c-0db6-4f74-8f9d-7070e7285364`, `96f04d43-a20b-4832-af5e-18afd950bc4f`, `9838556a-610a-417a-ad6e-f44bf54fbd90` | `shape-geometry-review-p6/reference-frames/landscape-t226.png` | The blue MUSIC and green LOCATION plates are likely much shorter and narrower than their reference lanes. The green playhead has a 2.2px × 253px inner shape, but the repeated outer size predicts a subpixel-wide, roughly 59px-high line. Inspect all four frames to verify the full left-to-right movement and the exact 232s cut. |
| 232 source label | 232.000, 236.000, 239.966667, 240.000 | `6f7ec77a-cb87-47ae-8ef7-2cfa5df59943` | `shape-geometry-review-p6/reference-frames/landscape-t232.png` | The inner plate is about 420px × 48px. Reapplying the same normalized outer size predicts about 92px × 2px, making the separate Rec.709 label appear unbacked or clipped. The 240s frame checks that the fix does not persist after the clip. |
| 244 delivery pills | 244.000, 248.000, 251.966667, 252.000 | `dedebc89-8705-4920-8952-bfac8d0aff92`, `87f51e8f-c8bc-49b4-b306-db6219209102`, `f345a85b-123a-415b-af89-81062f81ff8d` | `shape-geometry-review-p6/reference-frames/landscape-t244.png` | The repeated outer size predicts pill plates only about 30px, 16px, and 60px wide, each about 3px high, against the reference row of readable outlined pills. Their separate text should remain unchanged. The 252s frame checks clip-end isolation. |

These are baseline requests, not proof that the native renderer currently exhibits the predicted failure. Compare the P11 native frames to the cited references before applying any fragment; if the native frame is already correct, leave that group unchanged.

## Exact-fragment binding against P11

Use the existing source fragments:

- `shape-geometry-review-p10/chapter-226-audio-lanes.p10.commands-fragment.json` (5 actions)
- `shape-geometry-review-p10/chapter-232-source-label.p10.commands-fragment.json` (1 action)
- `shape-geometry-review-p10/chapter-244-delivery-pills.p10.commands-fragment.json` (3 actions)

Their P10 whole-document hashes and task placeholders are stale. After the P11 baseline capture, admit a **new** task on resource `timeline:3db4e4fb-6737-415c-b6ee-dde766e7c237`, inspect its exact live hash and task ID, then run `bind_native_fragment.py` separately for the group to produce a fresh bound fragment. Do not reuse P11’s released task ID or edit the saved P11 project file. The binder’s P10 target snapshot is still useful because every selected target field matches P11; its target-state comparison must pass in full before it binds the current P11 document hash and new task ID. Abort the entire group on any target mismatch.

Apply each reviewed group as its own bounded native batch. Preserve the inner title shape, positions, colors, stroke, text, opacity, placement, source, effects, masks, and track order. The 226s playhead proposal clears and restores only its existing `frameOffsetX` channel, preserving all 181 linear samples rebased to the neutral outer frame. `setTimelineClipFrame` must update both main and landscape geometry. No extra tracks or held candidates are included.

## Copy/story review

The current chapter copy reads coherently across these beats: 226 demonstrates MUSIC and LOCATION lanes with a moving playhead; 232 labels the image as a Rec.709 source under “From camera color to final delivery”; 244 shows the three delivery choices. No text rewrite is justified from these stills. Keep the source label and the “FULL PRODUCTIONS / SOCIAL CUTS / HIGH-RESOLUTION DELIVERY” text as authored; the proposed changes target only their associated geometry plates.
