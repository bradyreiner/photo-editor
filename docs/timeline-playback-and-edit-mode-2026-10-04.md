# Timeline playback and visible edit mode

This continuation addresses the paused-seek failure recorded in
[timeline-workflow-polish-2026-10-04.md](timeline-workflow-polish-2026-10-04.md)
and makes clip movement easier to predict.

## Changes

The sequence clock updates every 15 ms. Previously, its normal forward-playback
updates also corrected the decoder position whenever the clocks differed by more
than 20 ms. The native trace repeatedly decoded the first picture despite the
playhead advancing. An isolated Qt probe reproduced that behavior and the later
paused seek returning to position zero when it made repeated playing-position
corrections. The same probe without those corrections showed changing pictures
and a changed picture at the paused seek target.

Normal forward playback now lets the decoder run between source changes. Paused
seeking and reverse shuttle still request decoder positions; source changes keep
their existing load/position handshake. The stronger native workflow checks that
Play advances actual picture content, then seeks while paused to 0.6, 1.2, and
0.3 seconds, requiring a changed picture and matching player position each time.
This does not establish frame-accurate presentation timestamps or audio sync.

A persistent **Insert / Overwrite** selector sits beside Select in the ordinary
Timeline toolbar. It uses the existing document edit mode and existing command
handler. The two old advanced-only mode buttons are replaced by this selector.
Its menu and tooltip explain whether moving opens space or replaces occupied
timing. When Ripple is active, the tooltip explains its different move behavior.
The existing tool dropdown still offers the same commands.

Source controls expose an interactive scrollbar whenever they overflow, so lower
mark/range and Insert/Append/Overwrite controls are discoverable. The source title
has an accessible name, and its media-player handlers use explicit signal
parameters.

## Evidence

Final outcomes, logs, source/binary hashes, and capture manifests are recorded in
[verification.json](evidence/timeline-playback-edit-mode-2026-10-04/verification.json).
The production build, 15 QML tests, three focused controller tests, native pointer
workflow, and both 49-capture layout matrices passed. The workflow records 12
captures and verifies actual picture changes during forward playback and each
paused seek. A sample of native captures was visually reviewed; all recorded PNG
dimensions were checked against their manifest and display scale.

Native pointer checks select both edit modes, play/pause video, trim and move
linked clips, cancel gestures, add media, preview Source without sequence
mutation, play/pause Source, and save/reopen the document.

The layout matrix checks Comfortable, Balanced, and Compact in Edit and the
Edit/Color/Audio/Finish Timeline layouts. The edit-mode selector must remain
visible beside Select at each size. Native captures use 1180×768, 1440×900, and
1920×900; offscreen captures also exercise 1920×1080. Image backing dimensions
must match each capture's logical size and display scale.

The earlier delivery-queue source-contract failure remains separate: its expected
`queueOrder` literal is absent from pre-existing `job_runner.cpp` changes. This
continuation does not change that subsystem or claim a passing full test suite.

The fixture is a generated two-second 640×480, 30 fps H.264/AAC moving test
pattern. Long projects, representative camera formats, reverse-playback accuracy,
audio synchronization, realtime grading throughput, full-resolution color
fidelity, export parity, physical trackpad gestures, and VoiceOver are still
separate qualification tasks.

## Next usability work

Keep the next pass centered on complete edits: visible destination/track targeting
when adding media, precise keyboard trims, snap feedback on real gaps and locked
tracks, multiselection moves, and ripple outcomes. Qualify these existing tools
before adding another toolbar or inspector section. Preserve the same Looksy
control states and essential actions across all three densities.
