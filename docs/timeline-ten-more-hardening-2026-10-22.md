# Ten more Timeline defects

This follow-up continues the [professional hardening work](timeline-professional-hardening-2026-10-22.md).
The ten findings below are additional source-backed defects. Implementation,
focused regression results, and native visual proof are separate acceptance
steps; this is not a claim of Resolve or Premiere parity.

| # | Defect and user impact | Change | Required proof |
| --- | --- | --- | --- |
| 1 | Custom keymaps accepted Save, Quit, Delete, and clipboard collisions because fixed actions were outside the editable registry. Chord prefixes and modifier aliases also bypassed the dialog's string comparison. | Shared canonical, context-aware conflict checking includes fixed actions; the dialog names the conflicting action above the scrolling list and preserves settings. Preset availability, Custom state, navigation labels, redo help, and legacy Timeline modifier spelling use the actual runtime bindings. | Registry and controller rejection, actual QML feedback, offered preset validity, valid mapping persistence. Native keyboard dispatch remains separately unqualified. |
| 2 | Output QC scanned only the first video and audio streams while reporting signal scanning complete. A silent secondary audio stream could be missed. | Scan each supported stream with bounded work; label findings by stream and expose counts and incomplete coverage in the panel. | Encoded dual-audio fixture, stream-specific silence, visible coverage/incomplete labels. |
| 3 | OTIO Clip.2 alternate media references were silently discarded. | Include inactive references in the explicit import loss report. | Clip.2 fixture reports loss before import approval. |
| 4 | OTIO bounded individual clip durations but allowed their cumulative track cursor to exceed the supported range. | Bound cumulative clip and gap positions. | Individually valid clips whose combined duration exceeds the limit are rejected. |
| 5 | Trimming a timed caption's head restarted its word clock. Removing words to compensate would make extending the head destructive. | Retain authored words and persist a visible word-clock offset through trim, split, overwrite, save, transcript navigation, preview, and export. Preserve authored line breaks. | Head trim, extension without Undo, Undo/Redo, reopen, overwrite fragments, visible segmentation and sidecar timing. |
| 6 | Masked Blur on lower video and still layers used the full-layer blur path. Native checks also exposed stale cached detail and loss of a paused decoder when editing/reselecting a secondary layer. | Refresh edited layer detail, keep background playback and mask delegates keyed by clip ID, retain a used decoder across primary/secondary selection, and composite the blur through its spatial mask. | Native lower-layer video/still pixel comparisons inside and outside the mask, a strong unmasked control, and an assertion that the same decoded MediaPlayer survives edits. |
| 7 | Caption padding, corner radius, and safe margins used truthy fallbacks, so valid zero values displayed or rendered as defaults. | Use explicit missing-value fallbacks for zero-valid caption style fields. | Actual zero-valued controls and caption layout checks. |
| 8 | Live multichannel monitoring retained the first pair instead of performing the selected stereo downmix; center and surround material could disappear. | Decode the source layout and route it into stereo before the live processors and sink, with explicit LFE policy. | Center/surround routing, actual stereo output format, malformed samples, headroom before gain. Audible/export equivalence remains open. |
| 9 | Cancel preserved a recoverable partial voiceover WAV but closed the panel that showed its location. | Keep the panel, recovery path, and status visible when a partial take exists. | Instantiated panel cancellation with and without a recovery path. |
| 10 | Voiceover started sequence playback before asynchronous microphone permission completed, advancing through the permission prompt and potentially stopping playback it did not own. | Start only on actual pre-roll/recording and stop only playback requested for that capture. | Pending, denied, canceled, pre-roll, and recording state transitions with pre-existing playback. |

## Verification status

**28/28 scoped regression checks pass**, with a separate empty PostgreSQL database
for each invocation and isolated profiles where applicable. This includes the
complete Timeline controller, trim and caption workflows, audio DSP and native
buffer routing, playback/frame scheduling, recovery and its 10,000-clip scale
fixture, crop/render/animation workflows, packaging, OTIO, shortcut settings, and
the instantiated shortcut, delivery, and voiceover panels.

The Release application and native proof driver build successfully. A new
integer-conversion warning was corrected; the subsequent build was warning-free.
The old playback test required the decoder to discard multichannel layout by
requesting stereo in advance. Its replacement requires native-layout decoding
and injects an actual 5.1 buffer through the connected processing path, checking
exact center/surround contributions and LFE omission in the stereo meters. The
existing stereo, phase, peak, and RMS checks remain. No timeouts or performance
budgets were raised.

The native mask proof also tests its own stimulus against the unmasked render
before accepting a masked result. Its inside contrast threshold remains 55,
outside change is bounded at 24, and the inside result must match the unmasked
reference within 24. Probe selection uses only the baseline and control images.
It does not select a probe from the masked result.

The final Cocoa/Metal run passed for secondary stills and decoded video, with
the same secondary MediaPlayer surviving amount/mask edits and foreground
reselection. Outside-mask differences were 0/0 for the still and 6/2 for video;
inside differences were 58/61 and matched the unmasked reference within 10/9.
The unmasked controls changed outside pixels by 57–69, so they demonstrate that
the fixture detects a whole-layer blur. The actual zero-valued inspector and
fully opened Styles preview were rendered and visually reviewed. Narrow caption
controls now stack instead of overlapping their labels and values.

The native crop workflow also passed Program dragging, Undo, exact percentage
entry and Reset, fresh primary pixels, secondary video/still crop pixels, and
Fit letterboxes. This ran before the final caption-only layout and popup-capture
refinements; its exact source/binary snapshot is recorded separately.

The [verification receipt](evidence/timeline-ten-more-2026-10-22/verification.json)
binds 1,674 source files, test/application binaries, logs, screenshots, and the
fresh native receipt. The source snapshot is checked for drift after the final
build. The crop result has its own source context. The shortcut, QC, permission,
and recoverable-take panels were also instantiated and visually reviewed;
microphone callbacks in the panel test are simulated.

The 10,000-clip regression projected the complete model in 175 ms against its
existing 1,500 ms limit. Seek/selection/edit/Undo p95 were 4/1/15/3 ms, and retained
RSS growth was 364 MiB within the existing 640 MiB limit. These are synthetic
model measurements, not decoded-media playback qualification.

Physical microphone capture, native keyboard dispatch, audible live/export audio
equivalence, long-form 4K/HDR playback, and broad interchange remain open product
qualification gates.
