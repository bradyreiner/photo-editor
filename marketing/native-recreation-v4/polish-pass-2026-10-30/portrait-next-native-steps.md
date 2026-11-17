# Portrait continuation: native handoff

## Exact source and suffix

The source checkpoint is `marketing/native-recreation-v4/projects/looksy-native-portrait-in-progress-checkpoint-1.json`, SHA-256 `5c5ac69035e789c4a9d5917594d718fcf035bcb619cbdd080f0c231c5364f564`, 58,652,459 bytes. It is a 1080×1920, 30 fps Timeline with 413 tracks and 502 clips. The offline binder's checkpoint proof also pins 413 stable track IDs and 502 stable clip IDs.

Use `full-portrait-v9-schema-corrected-parent.commands.json` as the corrected future plan: it has 3,643 steps, so after the proved 1,637-action prefix it leaves 2,006 steps. The rejected first remaining action is `library-carousel/p-lib-62-IMG_4788-image` (`timeline.action.createTimelineClip`); the native scope guard rejected it before journaling. The older v8 plan has 3,642 steps and would incorrectly report only 2,005 remaining.

The approved parent currently has 104 `assetPaths`; all 104 paths exist. That scope includes the prior `creative-polish-v4-master.wav`, but it does **not** include the newer New Gods audio-plan inputs. The portrait binder pins this exact 104-path scope (`bind_reviewed_continuation.py` rejects task scope that differs from the frozen plan), so do not simply append audio paths at admission. If the portrait must carry the P9 cue plan, it needs a separately re-proven continuation/admission; otherwise keep New Gods for a later task. The P9 cue plan uses these six additional files (110 paths in the union):

- `/Users/bradyreiner/projects/PersonalProjects/photo-editor/marketing/native-recreation-v4/polish-pass-2026-10-30/music-candidates/downloads/public-candidates/HoliznaCC0 - New Gods.mp3`
- `/Users/bradyreiner/projects/PersonalProjects/photo-editor/marketing/animated-trailer/public/audio/foley/quick-air-woosh.mp3`
- `/Users/bradyreiner/projects/PersonalProjects/photo-editor/marketing/animated-trailer/public/audio/foley/interface-click.mp3`
- `/Users/bradyreiner/projects/PersonalProjects/photo-editor/marketing/animated-trailer/public/audio/creative-polish-location.wav`
- `/Users/bradyreiner/projects/PersonalProjects/photo-editor/marketing/native-recreation-v4/polish-pass-2026-10-30/native-audio-plan-p9/generated-assets/new-gods-swell-123.9.wav`
- `/Users/bradyreiner/projects/PersonalProjects/photo-editor/marketing/native-recreation-v4/polish-pass-2026-10-30/native-audio-plan-p9/generated-assets/new-gods-swell-134.4.wav`

All six files exist and their SHA-256 values match the P9 audio manifest. The audio manifest's master WAV is already among the 104. This is task `assetPaths` scope; it is separate from the startup project-input grant below. The exact P9 audio cue manifest is `polish-pass-2026-10-30/native-audio-plan-p9/new-gods-native-audio-cue-manifest-p9.json`. The 104-file existence check does not replace fresh native per-source integrity verification.

## Receipts: present and missing

The v8 source proof set already exists: the checkpoint document, ready-document-2, save, stop review, final reviewed-task, and release results. The saved project SHA in that evidence matches the 5c5ac… checkpoint.

The only valid fresh-copy/admission receipts required by the default portrait binder that are currently missing are:

1. `evidence/portrait-v9-copy-result.json`
2. `evidence/portrait-v9-document-result.json`
3. `evidence/portrait-v9-admit-result.json`

The existing `portrait-v10-inputs-result.json` is from an earlier startup and its opaque input ID is not reusable. `portrait-v10-copy-result.json` is **not** a valid import proof: it reports `ok:false`, `mutationApplied:true`, `recoveryRequired:true`, and source/copy graph hashes differ. Do not replay that import or use its receipt in the binder. Resolve the current Timeline state by inspection before any new attempt.

After the three new receipts pass the binder's graph checks, run `bind_portrait_scope_recovery.py` offline. It writes `full-portrait-v9-reviewed-suffix.commands.json` and `projects/looksy-native-full-portrait-v9-in-progress.json`; it does not call MCP or mutate the Timeline.

## Startup grant and safe lifecycle order

`main.cpp` passes `qgetenv("PHOTO_EDITOR_AUTOMATION_PROJECT_INPUTS")` to `TimelineDraftLifecycle` at startup. The parser accepts a JSON **array** of at most 8 objects, each with exactly `path` and `sha256`; paths must be absolute canonical readable regular files (not symlinks), SHA-256 must be lowercase 64-hex, and each file is limited to 256 MiB. The complete configuration is limited to 64 KiB. The file is hashed during startup validation and rechecked on import. Changing the environment after launch has no effect.

For this checkpoint, the exact one-item grant is:

```json
[{"path":"/Users/bradyreiner/projects/PersonalProjects/photo-editor/marketing/native-recreation-v4/projects/looksy-native-portrait-in-progress-checkpoint-1.json","sha256":"5c5ac69035e789c4a9d5917594d718fcf035bcb619cbdd080f0c231c5364f564"}]
```

Safe order after the landscape is durably saved and its active render is terminal:

1. Finish/review and release any landscape task. Confirm no task remains active or review-held, and that the landscape save is listed as a saved draft.
2. Keep any queued/running landscape export intact. Once it is terminal, retain and verify the complete output, create the authorized complete worker backup, and restart only that isolated worker. There is no close operation in `TimelineDraftLifecycle`; `timeline.import_project_input` rejects while a Timeline is active. Do not automatically reopen landscape recovery after this restart; confirm `timeline.inspect_document` reports `active:false` before importing.
3. Launch only after the prior worker PID is terminal, with the startup grant above in its environment. Call `timeline.list_project_inputs`; use the newly returned opaque `inputId` (IDs are generated at startup and must not be carried over from v10 or the landscape worker).
4. Call `timeline.import_project_input` exactly once with `{ "inputId": "<returned id>", "confirmed": true }`. The tool creates a managed Library copy with a fresh Timeline ID and preserves the source bytes. If a reply is uncertain, inspect the active document and `timeline.list_saved_drafts`; never replay the import.
5. Capture the import result, then inspect the imported document. Continue only if it has a fresh resource ID and the exact 413-track/502-clip stable-ID graph, portrait dimensions, 30 fps, and expected source hash. The previous v10 copy failed this gate.
6. Admit one new task against that returned resource ID using `timeline.admit_task`, the approved objective/actions, the exact frozen 104-path asset scope required by the existing binder (apply the selected New Gods arrangement in a separate subsequent task), the canonical existing project output directory, and `confirmed:true`. Save the admission receipt and imported-document inspection; verify the fresh task starts with an empty journal before binding/running the suffix.

The lifecycle tools involved are `timeline.list_saved_drafts`, `timeline.inspect_document`, `timeline.list_project_inputs`, `timeline.import_project_input`, and `timeline.admit_task`. `timeline.inspect_document` is compact; retain the rich imported-document result required by the binder for stable-ID graph comparison.

## Finish and qualify the portrait independently

After the frozen suffix is complete, run native graphics-track compaction and
verify clip IDs and overlapping-layer order. Apply the landscape P9–P16 repairs
through portrait semantic targets and its own layout; do not copy landscape IDs
or frame offsets. Add the accepted continuous New Gods arrangement in a separate
task with its six additional source grants.

The old portrait checkpoint is 1080×1920 but records `exportPreset: instagram`.
That preset's native preflight expects a 4:5 still/social canvas, as the bridge
fixture demonstrated. After the frozen continuation, choose `youtube_shorts`
through the native delivery action and verify that 1080×1920, 30 fps and 298 seconds
remain unchanged. Do not resize the film to satisfy the old preset. Retain the
native save/readback receipt before full export and review this format separately.
