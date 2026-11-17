# Timeline follow-up: cleanup, filter delivery, and startup

This follows the [Timeline interaction pass](timeline-depth-2026-10-24.md).
It fixes failures found while checking the editing workflow through export,
workspace suspension, and application startup. It is scoped evidence from an
already dirty checkout, not overall release qualification.

## Changes

- The inactive-workspace resource-release timer now uses a precise timer. The
  existing two-second grace period is a minimum; a coarse timer could fire
  early, cause the release guard to reject the attempt, and leave resources
  retained. Generation cancellation and the grace duration are unchanged.
  The regression check also asserts that release cannot happen before grace.
- Spatial filter export no longer combines an explicit output frame rate with
  passthrough frame timing, a combination rejected by FFmpeg 8. The full-frame
  stream now uses constant frame timing at the authored rate. Other export
  paths retain their existing timing behavior.
- The shared Edit-filter panel displays pending export validation and actual
  unsupported-placement reasons beside its stack. Controller teardown also
  clears the panel safely. This exposes limitations before the export dialog.
- Fixed a source-QML startup error in SearchModelSetupDialog: Flow's read-only
  implicit height was assigned directly. Layout sizing now owns that value.
- Removed two StudioPanel color overrides so those panels use their shared
  palette. The source style audit is now clear across 294 QML files.

## Verified behavior

The app and focused test targets built successfully. Nine focused tests passed:
Timeline UI consistency, media browser, panel docking, Color Console, shared
photo filters, transforms, frame scheduling, playback controller, and runtime
style policy. Both source inventory/style regression checks passed. Build and
check logs are in [checks](evidence/timeline-readiness-2026-10-24/checks).
The generated ExportView integer-conversion build warning remains.

Cleanup passed five consecutive focused runs with the new minimum-grace
assertion. The final native Cocoa decoder run passed suspend, resource release,
state preservation, late-frame rejection, and resume in 14.071 seconds;
[receipt](evidence/timeline-readiness-2026-10-24/native-playback-final.json).
The prior intermittent cleanup failure is retained in the earlier pass's
records rather than being retroactively described as a pass.

The actual Main workspace passed the strict active-window interaction proof
with ten native captures. It checks live keyframe dragging while the pointer is
held, gesture Undo, graph zoom and percent units, source-crop pointer mapping,
exclusive transform/crop tools, Color and shared-filter changes to decoded
Program pixels, panel drag round trips, context menus, and the visible export
restriction. See the [receipt](evidence/timeline-readiness-2026-10-24/native-controls/timeline-depth-proof.json),
[keyframe capture](evidence/timeline-readiness-2026-10-24/native-controls/timeline-keyframe-live-edit.png),
[crop capture](evidence/timeline-readiness-2026-10-24/native-controls/timeline-crop-tool.png),
and [filter status capture](evidence/timeline-readiness-2026-10-24/native-controls/timeline-filter-export-restriction.png).
The status capture reports unavailable source dimensions in this rapidly
executed fixture. The separate analyzed-video test checks rejection of a
scaled source placement.

The expanded filter test saves and reopens projects, renders baseline and
filtered MP4 files through the production export path, decodes them, and
compares them with the shared preview engine. The fixture is a 128 x 96
checkerboard at 12 fps for half a second. It tests Invert plus Gaussian blur on
a video clip, and Invert plus Posterize plus Gaussian blur on an adjustment
clip. Both exports contain all six authored frames at 12 fps.

| Placement | Mean absolute RGB preview/export error | Required maximum | Actual filter change |
| --- | ---: | ---: | ---: |
| Adjustment clip | 2.315 | 8 | 98.305 |
| Video clip | 2.423 | 8 | 93.091 |

Errors use 0–255 RGB channel values. The comparison includes ordinary encoded
video loss; the reference uses the decoded baseline and the same recipe in the
shared preview engine. This is a real export-path check for these fixtures,
not every filter or codec. Evidence includes the
[receipt](evidence/timeline-readiness-2026-10-24/filter-export/filter-export-proof.json),
[editable adjustment project](evidence/timeline-readiness-2026-10-24/filter-export/adjustment-filter-project.json),
[editable video project](evidence/timeline-readiness-2026-10-24/filter-export/video-filter-project.json),
[preview reference](evidence/timeline-readiness-2026-10-24/filter-export/video-clip-preview-reference.png),
and [decoded export](evidence/timeline-readiness-2026-10-24/filter-export/video-clip-decoded-export.png).
Their source PNG and MOV remain beside the projects. A separate minimal raw
frame-stage reproduction retains FFmpeg's rejected passthrough case and the
successful six-frame constant-rate case in
[ffmpeg-frame-mode-proof.json](evidence/timeline-readiness-2026-10-24/ffmpeg-frame-mode-proof.json).

## Open gates

Spatial filters on source clips still require analyzed dimensions, matching
source/sequence aspect, fit or stretch without crop, and a full-frame source
without placement, opacity, or animation changes. The UI reports the actual
restriction. Adjustment clips provide a supported composite-filter route.
Displacement-map asset binding and comprehensive all-filter delivery remain
outside this qualification. Sustained 4K playback and audible Program output
are also unqualified here.

Cold source-QML Timeline loading still fails the existing 1,500 ms budget:
8,136 ms, including 6,906 ms route compilation. The source load error was fixed;
no startup speedup is claimed. The packaged-app benchmark with an isolated
working PostgreSQL catalog lost foreground eligibility before its first usable
Timeline timing sample. Its unrun phases and missing memory samples do not
establish a latency or memory regression. Failed receipts and logs are retained
in [packaged-media-baseline](evidence/timeline-readiness-2026-10-24/packaged-media-baseline).
Earlier setup failures are retained separately. No focus requirement, timing
budget, or memory guard was relaxed. The performance loop stopped because it
could not produce a valid gate metric. The disposable PostgreSQL cluster was
stopped successfully; the user's catalog was not used for these runs.

Current source, executable, receipt, capture, and export hashes are recorded in
[verification.json](evidence/timeline-readiness-2026-10-24/verification.json).
The follow-up patch records differences against the pre-turn snapshots for
files that had snapshots; it does not assign ownership to unrelated changes
already present in this checkout.
