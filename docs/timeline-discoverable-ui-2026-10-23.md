# Discoverable Timeline workspace — 23 October 2026

This extends the earlier [Timeline UI redesign](timeline-ui-ux-redesign-2026-10-23.md)
with visible primary tools and independent panels. Three Luna xhigh specialists
worked on docks/editorial controls, direct editing tools, and Color/Audio consoles;
the integrated shell, pointer routing, and verification were handled together.
The existing dirty checkout and unrelated work were preserved.

## User-visible behavior

The permanent panel strip exposes Media, Source, Effects, Inspector, Color,
Mixer, Keyframes, Titles, Captions, Transcript, and Scopes. Each panel can be
opened, focused, closed, moved between docks, or floated. Opening Color or Mixer
does not close Effects. The Window menu exposes the same panels and the task
arrangements. On smaller windows the side docks share a scrolling tab strip;
the permanent panel buttons remain available.

Panel visibility, placements, active tabs, floating geometry, preferred dock
widths, and timeline scroll survive workspace recreation. Editing, Color, Audio,
and Finishing retain independent arrangements. Reset affects the current
arrangement. Factory layouts normalize without silently changing tab order.

The timeline palette exposes Select, both Track Select directions, Blade, Trim,
Ripple Trim, Roll, Slip, Slide, Rate Stretch, Pen, Hand, Zoom, Text, and Shape.
Labels reflow at smaller widths. Link, Snap, Ripple, Fit Sequence, and Fit
Selection remain directly visible. Source, Insert, Overwrite, Replace,
Fit to Fill, Append, and Match Frame have a separate source-edit command row.
Sequence resolution remains under Sequence Settings.

Rate Stretch operates on the exact clip and its linked audio, maintains the
source range and start, validates locks/neighbours/rate limits, and groups the
gesture into one Undo. Curved retimes have an explicit disabled reason. Pen
authors clip opacity/audio gain or track envelopes. Stale gestures cannot edit a
replacement document. Escape cancels active gestures. Hand and Zoom change the
viewport; choosing a tool preserves a saved document's clean state and history.

Effects discovery and applied controls have separate retained surfaces. The
library remains browsable without selection. Inspector tabs keep complete
labels. Find routes to the focused Effects/Animate search or the timeline,
without duplicate shortcuts when panels coexist. Floating panels retain their
own search shortcut.

Source Preview, Match Frame, and Reveal in Source focus the actual Source dock,
including when it was previously closed. The lower graph reserves enough height
for its controls while exposed. Compact side docks preserve the Inspector when
a bottom panel receives focus, and narrower landscape layouts cap side widths
without overwriting saved preferences. Shared FilterChip sizing now accounts for
its actual padding, fixing label clipping at the primitive rather than shortening
Timeline labels.

Color offers Primaries, Wheels, Curves, Input/LUT, and Program comparison through
the existing controller and grade recipe. Controls read back the selected clip
and respect selection and locks. The Mixer has real track faders, meters,
Mute/Solo/Pan, and processing controls. In a small dock, clip controls open one
click deeper in a dialog sized to the main window. The master strip shows real
meters; there is no simulated master gain control.

Titles, Captions, and Transcript expose their existing authoring operations in
the dock system. A transcript word correction now detaches its live nested
storage after snapshot capture, so one Undo restores the previous word and timed
segment rather than a modified snapshot.

## Verification and limits

Evidence and final receipts are recorded in
`docs/evidence/timeline-discoverable-ui-2026-10-23/verification.json`.
Failed and diagnostic runs are retained with the accepted runs. No existing
test timeout, recovery memory limit, or workspace latency budget is raised.
Contracts that referenced removed wrappers are migrated to the actual visible
controls and retained state, keeping pointer and state assertions.

Final results on this host:

| Check | Result |
| --- | --- |
| Release desktop build | Passed; app linked with the latest Timeline QML |
| Final focused UI suite | 13/13 passed |
| Controller, recovery, playback, scheduling, caption regressions | 5/5 passed; core source unchanged since these runs |
| Final discoverable native interaction proof | Passed in 13.309 s; seven native captures, 11 panels and 15 tools |
| Inspector/graph native workflow | Passed in 17.802 s |
| Full edit, decoded Source playback, save/reopen native workflow | Passed in 39.184 s |
| Keyboard, effect discovery, keyframe and curve native workflow | Passed in 10.420 s |
| Workspace functional diagnostic | All layout, persistence, splitter, input and detached-window assertions passed; overall result remains failed due to first-open latency |

The unchanged workspace budgets are 1500 ms for first Timeline, 1000 ms for first
Color, and 250 ms for warm switches. The final diagnostic measured **7072 ms for
first Timeline (failed)**, 468 ms for first Color, 136.4 ms warm Color p95, and
92.2 ms warm Editing p95. Diagnostic mode continues through functional checks and
still exits nonzero for the failed budget; the default proof remains fail-fast.
These source-QML measurements do not qualify production AOT or consistently
foreground switch latency. Earlier failed runs remain in the evidence bundle.

The final 13-test run supersedes an earlier Mixer label-layout failure; the
original failure log is retained. The fix supplies a compact Pan control and
budgets gain-label width for its actual -60 to +24 dB range, with pointer,
keyboard, readback and Undo checks. No assertion or existing budget was removed.
Native smoke readiness now requires QML incubation to finish within its existing
750 ms bound before style auditing, and waits for the tracking popup to actually
close before ordinary captures. Both readiness conditions fail on timeout.

The [local evidence gallery](evidence/timeline-discoverable-ui-2026-10-23/index.html)
contains actual captures and a manifest with source/binary hashes. The final
compact Timeline, simultaneous Effects/Color/Mixer, and landscape laptop Color
captures were visually reviewed. Inspector/graph captures were also reviewed in
their corresponding earlier accepted run. Native source-QML runs span the
incremental fixes listed here; each run retains its own receipt and binary hash.
The current app and final discoverable run reflect the final responsive changes.

Known diagnostics retained in logs include an unrelated generated ExportView
integer-conversion build warning, ffmpeg QProcess teardown warnings, and two
in-flight QML items at diagnostic engine teardown. These checks do not qualify
warning-free application teardown or app-wide release readiness.

This work qualifies the named UI workflows on the tested fixtures and host.
It does not establish full Premiere/Resolve parity, sustained 4K playback,
sample-level audio editing, plugin hosting, a master gain bus, automation
Read/Write/Touch/Latch modes, or additional interchange formats. The native
workflow harness loads source QML with the ordinary Qt cache; a separate
production build does not by itself establish bundled AOT runtime parity.

## Manual acceptance

1. Open all eleven panel buttons, then use tab Close, Move, Float, and Dock.
   Keep Effects, Color, and Mixer open together. Resize and return from Library.
2. Compare Media grid/list and thumbnail sizes with a real mixed shoot. Preview
   and mark Source, insert/overwrite, and use Match Frame on an existing cut.
3. Use every palette tool on video and linked audio. Check locked tracks,
   neighbours, invalid rates, one-step Undo/Redo, and Escape cancellation.
4. Open Animate and Effects together. Find in each, filter channels, open Graph,
   pin a clip, select another clip, and edit/undo the pinned curve.
5. Drag Mixer faders, then open Clip, channel mapping, routing, and processing
   from a short dock. Confirm complete labels, scrolling, live readback, and Undo.
6. Switch Editing → Color → Audio → Editing. Reset one arrangement, relaunch
   with floating panels, and verify selection, playhead, preferred sizes, and
   scroll without changing the edit.
