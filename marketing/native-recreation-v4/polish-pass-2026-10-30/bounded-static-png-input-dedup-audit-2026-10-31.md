# Bounded render static PNG input reuse audit

Read-only audit for the P15 `[199, 200)` native excerpt. No app or worker was
called and no implementation files were changed.

## Evidence and likely repetition

- `timelineRenderVideoBoundedH264` plans small global frame windows, then calls
  `prepareNativeGeneratorFrameSequencesForRange` and
  `timelineRenderArguments` separately for each window
  (`apps/desktop/src/timeline_bounded_h264_export.cpp`, around lines 589–623
  and 695–722). Each call creates a fresh FFmpeg process and a new input list.
- `timelineRenderArguments` places clip `start/end` and every `sourceTime`
  keyframe in `cut_points`, then snaps bounded cuts to the global frame clock
  (`apps/desktop/src/timeline_document_controller.cpp`, around lines
  51169–51305). It creates each segment's color input and clip inputs inside
  the segment loop (around lines 51377–51410 and 51700–52150).
- P15's saved native document has two active retime curves crossing 199–200:
  `synced-program-arrival` ends at 199.5s and `synced-program-city` starts at
  199.5s. Their source-time keys are spaced 1/30s, producing 30 global
  one-frame segments for this one-second excerpt. The saved P15 document is
  the same canonical document as the live 4fc5… receipt; its pretty JSON bytes
  are not themselves the canonical document hash.
- There are 46 active video source clips per frame before 199.5s and 48 after
  it: roughly 37–38 title PNGs, 6 photos, 2–3 generator sequences, and one
  retimed video. A 4-frame planner window therefore repeats most source inputs
  four times because of the four retime segments. The observed 188 inputs are
  consistent with that topology; they are not 188 distinct media sources.

## Narrow safe candidate

In the bounded exporter, after `timelineRenderArguments` has produced a window
and before checking the 256-input graph cap, deduplicate only identical
complete input-option blocks whose input path is one existing literal `.png`
file. The key is the exact preceding input options plus the exact path. Do not
canonicalize path aliases. Leave every source-time cut, segment graph,
`clipVideoFilter`, title sparse-ROI filter, and global frame clock unchanged.

For each duplicate input, retain the first input block, remove later duplicate
blocks, insert one `split=N` at that retained decoded stream, and route each
original input index to a unique split output. Rewrite all numeric input
references in the filter graph (both video and audio stream labels) through
the old-to-new index map. A reused still is safe only when its entire input
block matches: the per-segment `-t`, frame rate, loop mode, and all other
options must be identical. Inputs with different duration or options remain
separate. The split branches keep the same PTS as the duplicated independent
inputs, so the already-authored segment-local `t/T` clocks remain unchanged;
each segment's filters still receive its existing global `sequence_start`.

Expected reduction for a four-frame, one-frame-segment window: retain roughly
46–48 clip source inputs once, while keeping the four color and four silent
audio segment inputs, per-segment masks, and the retimed video inputs. This
suggests roughly 55–65 inputs instead of about 188 (about two-thirds fewer),
subject to an instrumented full graph count. The helper must run before the
input-cap check, so it can also avoid needless recursive window splitting.

### Eligible input forms

- Finite one-frame title PNG input with only `-framerate` before `-i`: dedupe
  when literal path and options match. Keep the existing title filter's
  `loop=...` and rational frame clock intact; do not alter its `source_label`
  eligibility or geometry fast path.
- Looped title/photo PNG inputs with identical `-loop 1`, `-framerate`,
  `-t`, and literal path: the first demuxer stream can fan out to matching
  segment consumers. Unequal segment durations are deliberately ineligible.
- A static cached PNG generator can use the same rule. Sequence patterns,
  masks, and per-frame generator image sequences are excluded from the first
  increment even if their option blocks appear alike.

## Exclusions and correctness risks

- Do not dedupe video sources, retimed clips, source-time ramps, reverse/freeze,
  stabilization, source overrides, or any non-PNG or wildcard/sequence path.
  Their input seeks and timestamp/frame mappings can differ per segment.
- Do not dedupe by path alone: `-ss`, `-t`, `-start_number`, `-framerate`,
  loop mode, input format, and any other input option affect what frames are
  decoded and their PTS.
- A helper that removes inputs must remap *all* graph stream references after
  each removal, not just video labels, and must preserve any filtergraph
  sidecar path contract. Check that each original input consumer gets a
  distinct split output; directly reusing one FFmpeg pad twice is invalid.
- Dense graphs commonly use `-filter_complex_script`, not inline graph text.
  Rewrite that file atomically and preserve the task-owned path / cleanup
  behavior. If parsing or rewrite validation fails, use the original args and
  graph (or fail closed); never emit a partially rewritten graph.
- Identical complete `-i` blocks should normally have matching stream PTS and
  duration; assert this at the helper boundary. Check EOF/`shortest` consumers
  and branches whose filters change rate before relying on shared pads.

## Focused fixture design before app integration

1. Pure helper unit fixture: a four-segment graph with (a) repeated literal
   one-frame title PNG inputs and the current finite title loop/rational clock,
   (b) repeated `-loop 1 -framerate 30 -t 0.033333333 -i photo.png` blocks,
   (c) two retimed video blocks with different `-ss` values, (d) an image2
   `%06d.png` sequence, and (e) a photo with a different `-t`. Only (a)/(b)
   with byte-identical input-option blocks deduplicate; video, sequence, and
   mismatched duration remain distinct. Assert final `-i` count, exact
   retained input args, unique branch labels, and complete video/audio index
   remapping.
2. FFmpeg parity fixture: compare pre-helper and helper graphs at raw decoded
   RGBA output for a short 30fps clip with a static title/photo repeated across
   four one-frame source-time cuts plus animated title scale/offset/opacity.
   Assert exact frame count and per-frame `framemd5`; the retimed footage
   remains four separately sought inputs. Include the finite title sparse-ROI
   graph and confirm it stays eligible after rewriting.
3. Negative/fallback cases: equal PNG path but different `-t`, different
   `-framerate`, symlink/path alias, `%06d` sequence, non-PNG, unknown input
   option, missing/unsafe sidecar, or malformed graph. Each must stay
   undeduplicated or return the original args safely.
4. Add one controller-level fixture with a keyframed `sourceTime` video and
   multiple unaffected titles/photos. Inspect that global cuts and animated
   transforms are unchanged while the optimized graph's input count drops.

Do not use the compressed H.264 output as a bit-exact pixel oracle. Compare
before the encoder on the raw output pads; separately verify the assembled
H.264 frame count, frame rate, and clip boundaries.
