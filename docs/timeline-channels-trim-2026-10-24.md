# Animation channels and precision trim feedback

This continues the [Timeline polish](timeline-polish-2026-10-24.md) with a multi-channel animation view and a more informative trim preview.

## Animation channels

Open a clip's Graph, then choose Channels beside Value and Speed. Motion channels and other animated parameters appear as stacked curves on one time ruler. The channel chooser can include or hide individual parameters; its reset restores Motion and animated channels. This is a viewing preference and does not edit the project.

Each lane shows its name, evaluated value, key count, an add-at-playhead action, and a shortcut to its detailed curve. Click a channel to target its keys. Drag a diamond to change time and value; Shift constrains the gesture to its dominant axis. Live updates retain their pointer owner, snap timing to authored frames, and form one Undo operation. Changes stay within the selected parameter. Existing graph selection, nudge, clipboard, easing, and navigation actions apply to the active channel.

Ordinary wheel motion scrolls the channel list. Command/Control-wheel zooms the common time range, and Shift-wheel pans it. The Fit values action refits the lanes. Short panels use smaller lanes and show numeric and interpolation controls when keys are selected. Larger panels retain the detail controls. Long document names fit the header while preserving the full accessible name and rename tooltip.

The channel chooser and comparison mode are session UI state; this pass does not add serialized comparison presets or cross-channel grouped key selection. Saved key values and timing retain the existing document format.

## Trim feedback

Dragging an In or Out handle opens a two-up source comparison in Program. Isolated clips show Before and After. Adjoining clips can compare both sides of the cut or switch to Before/After. The preview reports the frame delta, boundary timecode, before/after duration, and available head/tail handles. A source-range rail shows the retained interval. Retime preview samples follow the existing source-time curve, and the rail includes sampled interior source positions.

Pointer candidates are snapped to sequence frames and constrained by source handles, minimum duration, sequence start, and the adjoining clip when rolling. A visible explanation appears at a limit. A trailing update publishes the final pointer position even when motion ends inside the preview throttle. The document remains unchanged during the preview; release commits one Undo operation, and cancellation restores Program without changing the clip.

Preview source positions account for playback rate: the outgoing sample is one sequence frame before the boundary, mapped through the clip's source-time relationship. Decoding uses the existing bounded frame queue and revision-keyed disk cache at up to 960 x 540, without upscaling small sources. Pending or failed frames have explicit loading/unavailable states. Late results for a different clip, side, or requested source time are ignored. Hover scrubbing keeps its existing 384 x 216 request size.

## Verification and remaining qualification

Evidence is retained in [timeline-channels-trim-2026-10-24](evidence/timeline-channels-trim-2026-10-24/). The receipt distinguishes functional assertions from native visual qualification: a software-rendered run cannot mark the native gate passed.

The production Main workflow checks aligned lane times, pointer-held live editing, isolation of the other channel, grouped Undo, one-frame nudging, channel chooser behavior, save/reopen, 2x-rate trim mapping, exact frame counts and limits, release/Undo, cancellation, and compact channel/header geometry. A moving source fixture is decoded independently to compare the actual trim preview pixels against the requested source frame, with a mean absolute RGB error limit of 8 on a 0–255 scale.

The final Looksy app, driver, controller test target, and contract test target built successfully. All four focused registrations passed: the channel/trim functional workflow, UI consistency contract, shared keyframe curve workflow, and keyframe parameter coverage. The style audit passed across 296 QML files with no violations. The measured trim-image error was 2.281 against the limit of 8. Both before/after consumers received a complete shared source frame before the edge moved. No new compiler warning was reported; the existing generated ExportView integer-conversion warning appeared in the full rebuild.

The [verification manifest](evidence/timeline-channels-trim-2026-10-24/verification.json) records source, executable, and capture hashes. The final software-rendered views are [aligned channels](evidence/timeline-channels-trim-2026-10-24/functional-verified/channels-aligned-curves.png), [compact animated channels](evidence/timeline-channels-trim-2026-10-24/functional-verified/channels-compact-workspace.png), [rate-aware trim comparison](evidence/timeline-channels-trim-2026-10-24/functional-verified/trim-before-after-rate-aware.png), and [source-limit feedback](evidence/timeline-channels-trim-2026-10-24/functional-verified/trim-source-limit.png).

Native foreground verification was pending at the end of this pass because attempts could not acquire an active macOS test window. That gate subsequently passed in the [October 25 native context follow-up](timeline-context-2026-10-25.md), including native channel editing, trim preview, persistence, and compact geometry. The original software-only receipt is retained as historical evidence. The prior audio, realtime playback, render/export, and broader Timeline readiness limits continue to apply.

## Manual native review

1. Open a video clip, animate Scale and Horizontal Position, and open Graph → Channels. Confirm equal key times line up; scroll, zoom, pan, and switch into either detailed curve.
2. Drag a key while watching Program. Confirm the other channel is unchanged, release, then Undo once. Repeat with frame nudging and numeric entry.
3. Resize to a laptop window and narrow the dock. Confirm the channel chooser, lane actions, values, and full-name tooltip remain usable. Save, close, and reopen; verify authored keys.
4. Trim ordinary and 2x-rate clips. Compare the Before/After source frames and duration/handle counts, then drag beyond the source end. Verify the limit explanation, commit/Undo, and cancel.
5. Try adjoining clips with Roll and toggle the cut comparison. Check reverse/nonlinear retiming and source gaps separately before claiming those paths qualified.

The focused registrations are `timeline_channels_trim_functional_test` (software renderer) and `timeline_channels_trim_native_test` (macOS foreground and native pointer). The native test requires an unlocked, available desktop.
