# Ten more high-impact Timeline findings

Read-only product audit, 2026-10-23. Three Luna xhigh specialists inspected
audio/playback, compositing, and delivery/portability; the parent reviewed the
selected code paths. These are additional findings beyond the previous
[ten fixes](timeline-ten-more-hardening-2026-10-22.md).

The inspected working tree is dirty, based on commit
`b4cda21d6b92cca24aefe8371861d6601a8c1d9b`. Findings refer to the current
working-tree source, not that commit alone. Product code was not changed.
Confidence below describes source evidence. Except for the two isolated FFmpeg
checks recorded below, integrated runtime reproductions have not been run.

## 1. Saving a package copy can overwrite the original local project

**Priority: critical project-safety risk. Confidence: high.**

Package collection rewrites resources but retains the original `timelineId`.
Opening its extracted `Project.json` adopts that identity and file path. Save
then redirects to the canonical local `{timelineId}.json` and updates the
existing library record by the same identity. An edited package copy opened on
the originating installation can therefore replace the original project's
canonical document and media references. The extracted file is no longer the
save destination.

Evidence: [package rewrite](../apps/desktop/src/timeline_delivery.cpp#L610),
[identity adoption](../apps/desktop/src/timeline_document_controller.cpp#L15346),
[loaded path](../apps/desktop/src/timeline_document_controller.cpp#L15396),
[save destination](../apps/desktop/src/timeline_document_controller.cpp#L15563).

Required behavior and proof: distinguish opening the existing project from
importing a copy. On an identity collision, provide an explicit copy/replace
choice or preserve the opened document's destination. Package, extract, open,
edit, save and relaunch with the original present; prove the intended project
alone changes, including library identity and resource paths.

## 2. Publishing an updated timeline collides with its previous render

**Priority: high; blocks ordinary revision delivery. Confidence: high.**

Publish uses a stable timeline render path. The worker stages the new render,
but its publication transaction refuses any existing target. After one
successful Publish, publishing another edit to the same container can fail
with “A delivery file already exists. Its contents were preserved.” The project
can be updated while the library render remains old.

Evidence: [stable job/output](../apps/desktop/src/timeline_document_controller.cpp#L4052),
[stable render path](../apps/desktop/src/timeline_document_controller.cpp#L15479),
[worker publication](../apps/desktop/src/job_runner.cpp#L2484),
[existing-file rejection](../apps/desktop/src/job_output_transaction.h#L62).

Required behavior and proof: version render destinations or safely replace the
previous delivery transactionally. Publish A, change a visible and audible
element, Publish B, reopen the library render and verify B. Exercise failure
and cancellation during replacement and retain the last successful render.

## 3. A short source audio stream can silence the rest of the mix

**Priority: high; missing delivered audio. Confidence: high, filter behavior reproduced.**

Every multi-input bus, including the final bus mix, uses
`amix=duration=shortest`. Inputs are trimmed but not padded to the segment's
duration. A video with six seconds of picture and only two seconds of embedded
audio can end a six-second music mix after two seconds. Concat may pad the
remaining video interval with silence, but cannot restore the lost music.

Evidence: [trim without padding](../apps/desktop/src/timeline_document_controller.cpp#L42491),
[shared shortest-input mix](../apps/desktop/src/timeline_document_controller.cpp#L42527).

Required behavior and proof: preserve every active input for its valid span and
pad/bound the segment explicitly. Render a six-second video with two-second
embedded audio and a six-second bed; verify the bed after two seconds and
through the final frame. Repeat across different buses, stems and silence gaps.

## 4. Inspector selection can change the Program video's sound

**Priority: high; monitoring cannot be trusted. Confidence: high.**

The Program decoder resolves `programClip()`, but `updateAudioVolume()` reads
`currentClip()`, which is the inspector selection. It takes gain, fade,
audio-track processors and playback eligibility from that selection. Selecting
a standalone voiceover while a video remains on Program can apply the
voiceover's settings to the video's embedded audio. The separate QML audio
layer still plays the voiceover.

Evidence: [selected versus Program clips](../apps/desktop/src/timeline_document_controller.cpp#L5481),
[Program decode/color identity](../apps/desktop/src/timeline_playback_controller.cpp#L4354),
[selected-clip audio settings](../apps/desktop/src/timeline_playback_controller.cpp#L5012),
[separate audio layers](../apps/desktop/qml/workspaces/TimelineView.qml#L5916).

Required behavior and proof: bind processing to the source being decoded.
Play distinct embedded-video and voiceover tones; change selection, mute,
gain and processor settings. Measure each routed output and prove selection
alone does not change the mix, including cache/proxy transitions.

## 5. Exported fades restart at unrelated timeline boundaries

**Priority: high; audible delivery discontinuities. Confidence: high, filter behavior reproduced.**

The exporter divides the timeline at clip edges, caption-word boundaries,
retime samples and processor keyframes. It creates an audio filter chain for
each segment. A clip fade-in is rebuilt from zero over the remaining segment
duration; fade-out is similarly rebuilt in segment-local time. An unrelated
picture or caption edit can therefore change the music envelope and introduce
repeated fades or sudden jumps.

Evidence: [global segmentation](../apps/desktop/src/timeline_document_controller.cpp#L41773),
[segment-local fades](../apps/desktop/src/timeline_document_controller.cpp#L42465).

Required behavior and proof: evaluate fades against uninterrupted clip-local
time. Compare a tone with a one-second fade against the same timeline with
unrelated cuts at 0.25 and 0.6 seconds. The resulting audio envelopes must agree
within an explicit sample/encoding tolerance. Include fade-out and cache ranges.

## 6. OTIO imports lose the true source duration and trim handles

**Priority: high; imported edits become difficult or unsafe to revise. Confidence: high.**

For a source range starting at 20 seconds and lasting five seconds, OTIO import
writes native `durationSeconds=5`, `trimStartSeconds=20`, `trimEndSeconds=25`.
It does not mark duration provisional, so media analysis does not repair it.
Trim-end editing subsequently bounds the requested source out against the
incorrect five-second duration. Source ranges and source duration become
internally inconsistent; extending into genuine source handles cannot work
correctly.

Evidence: [import fields](../apps/desktop/src/timeline_interchange.cpp#L529),
[provisional default](../apps/desktop/src/timeline_document_controller.cpp#L14154),
[conditional duration repair](../apps/desktop/src/timeline_document_controller.cpp#L40075),
[trim bound](../apps/desktop/src/timeline_document_controller.cpp#L27163).

Required behavior and proof: keep edit duration separate from source duration;
resolve/validate available source range, including offline imports. Import
20–25 seconds of a 60-second source, extend both handles, Undo/Redo, relink,
save/reopen and compare decoded first/last frames.

## 7. HDR input has no complete, consistent delivery transform

**Priority: high for phone/camera HDR footage. Confidence: high.**

The source resolver deliberately retains HDR transfer and primaries for PQ/HLG
sources. The FFmpeg input adapter can therefore retain that signal, while both
ordinary and spatial-frame-stream delivery hardcode Rec.709 transfer,
primaries, matrix and limited-range output tags. There is no corresponding
explicit HDR-to-SDR tone-map in these paths, nor a selectable HDR output signal.
Tags alone cannot convert the retained signal. The visible result needs
measurement, but the pipeline mismatch is explicit in source.

Evidence: [HDR output-signal preservation](../engine/src/pipeline/video_color_transform.cpp#L333),
[source adapter](../apps/desktop/src/timeline_document_controller.cpp#L44109),
[delivery tags](../apps/desktop/src/timeline_document_controller.cpp#L42710),
[frame-stream delivery tags](../apps/desktop/src/timeline_document_controller.cpp#L43254).

Required behavior and proof: offer explicit SDR conversion and HDR delivery
targets, with consistent display/output transforms and UI labels. Use known
PQ/HLG ramps and camera clips; measure transfer, gamut, highlights and output
metadata through preview, cache, export and external playback.

## 8. A 10-bit ProRes master still passes through 8-bit processing

**Priority: high for grading, gradients and graphics. Confidence: high.**

Layer processing uses `format=rgba`, an 8-bit packed format. The normal
composited segment is then forced to `yuv420p` before the encoder is selected.
Choosing ProRes later requests `yuv422p10le`, but expanding an already
quantized, chroma-subsampled frame cannot recover the lost information. Alpha
and spatial-frame-stream routes also use 8-bit RGBA intermediates.

Evidence: [input conversion](../apps/desktop/src/timeline_document_controller.cpp#L44116),
[segment quantization](../apps/desktop/src/timeline_document_controller.cpp#L42195),
[ProRes output format](../apps/desktop/src/timeline_document_controller.cpp#L42717),
[raw frame-stream input](../apps/desktop/src/timeline_document_controller.cpp#L43223).

Required behavior and proof: preserve sufficient bit depth and chroma precision
through compositing and grading, with intentional conversion at delivery.
Render a 10/16-bit gradient and fine colored graphics to ProRes; measure retained
levels, banding and chroma detail against a high-precision reference. Probe the
encoded format as well as the actual pixels.

## 9. Timecode sync interprets camera clocks using the sequence rate

**Priority: high for multicam and dual-system sound. Confidence: high.**

The probe does not request source frame rate and parses source timecode using
`framesPerSecond()` from the sequence. A 30 fps source label ending in 15
frames means 0.5 seconds; in a 24 fps sequence it becomes 0.625 seconds, a
125 ms error. The parser also treats `:` and `;` identically and computes
wall-clock fields plus `frames/fps`, rather than applying DF/NDF frame-count
semantics. Invalid drop-frame labels are accepted. BWF sample references use
their audio sample rate correctly, making mixed clock representations a
particularly important regression case.

Evidence: [parser](../apps/desktop/src/timeline_document_controller.cpp#L2287),
[probe fields](../apps/desktop/src/timeline_document_controller.cpp#L2333),
[external-audio sync](../apps/desktop/src/timeline_document_controller.cpp#L38879),
[multicam sync](../apps/desktop/src/timeline_document_controller.cpp#L39084).

Required behavior and proof: retain source timecode rate and format, parse
SMPTE frame counts, and expose those values in sync review. Test source rates
24/25/30 and 29.97/59.94 DF/NDF, unequal source/sequence rates, minute/hour
boundaries, invalid DF labels and sample-based BWF alignment.

## 10. Cropping can move an exported effect mask away from its preview

**Priority: high for selective corrections. Confidence: high.**

Program computes its displayed source aspect after crop. Export also crops
the source before Fit/Fill sizing, but mask mapping still calculates content
fractions from the original `clip.source_size`. Cropping a 16:9 source to
square and applying an off-center masked Blur/Exposure therefore uses
different geometry for the rendered image and its mask. The delivered
correction can affect the wrong area.

Evidence: [cropped preview aspect](../apps/desktop/qml/workspaces/TimelineView.qml#L1286),
[crop before sizing](../apps/desktop/src/timeline_document_controller.cpp#L44138),
[uncropped export-mask aspect](../apps/desktop/src/timeline_document_controller.cpp#L44272).

Required behavior and proof: share the crop/fit/fill/transform mapping between
preview, overlay and export. Compare native preview and decoded export pixels
inside/outside an off-center mask on cropped video and stills, including Fit,
Fill, rotation, feathering and Undo/Redo.

## Evidence and limits

Two isolated FFmpeg filter reproductions were executed during this audit:

- For a constant-amplitude signal with a one-second fade-in, splitting at
  0.25 seconds and applying the exporter's segment-local fade formulas changed
  the mean amplitude just before the boundary from 0.122495 to 0.489979, and
  just after it from 0.132495 to 0.009993. This demonstrates the envelope reset;
  it does not claim an integrated app export was exercised.
- Mixing two- and six-second 48 kHz tones using
  `amix=inputs=2:duration=shortest:normalize=0` yielded exactly two seconds of
  samples. The integrated app's subsequent concat/mux behavior still needs
  the acceptance scenario above.

No app builds, native UI runs or repository test suites were run for this
assessment. No tests were modified. The acceptance tests above are required
work for implementation, not completed verification. Existing recovery and
transactional safeguards may preserve recoverable prior data; they do not
remove the project-identity or delivery-routing defects described here.
