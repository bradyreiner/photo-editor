# New Gods native soundtrack plan (P9)

Status: plan only. No Timeline, app, MCP, autosave, or source project was mutated. The user selected **New Gods — HoliznaCC0**; the goal here is an editable native score and cue layout that preserves the current 298-second visual timing.

## P9 guard and current audio

- Timeline resource: `timeline:3db4e4fb-6737-415c-b6ee-dde766e7c237`.
- P9 document hash: `4d8157be5d32d94ec6ad7c01b2705175adb09a637afedc6d151148f8e2e72a48`.
- Recovery-copy file: [looksy-polish-pass9.json](../native-projects/looksy-polish-pass9.json), SHA-256 `51531c69e912a50908cb99ce5a795d697c7fb22e24a2dcf90cbc886b9a35f0b4`.
- The original source project remains SHA-256 `20cebcdbfd4eb5d6bd18f70e3245992c1ebbfa9252b2ead883dbd4c2f4cc2082`.
- Current Music 1 track `music-754faa1f-2e2e-43b4-af95-a77bbef7c3c1` contains one 0–298 s baked clip `7ec6c2d2-a845-414a-8856-a186344dd3ce` (`creative-polish-v4-master.wav`). It is unmuted. Empty A1 `audio-1` is a dialogue/main-bus lane, so the plan leaves it untouched.
- A future fresh task must compare the live document hash and those exact IDs/timings to P9 before any operation. A mismatch cancels this binding and requires a new readback.

## Musical trim and levels

The original MP3 is 315.828 s, stereo 48 kHz, 320 kb/s, SHA-256 `645d3a2b54aee7a2f2de6a886015f7fdb220490cecba2913fab31e1cde67b2f5`. FFmpeg EBU R128 reports −13.2 LUFS integrated, 8.1 LU LRA, and +0.1 dBFS true peak. At −3.7 dB native clip gain the proposed 298-second music-only excerpt measures −16.8 LUFS, 7.9 LU LRA, and −3.6 dBFS true peak. The old baked master measures −16.8 LUFS and −3.7 dBFS true peak, so the proposed music-bed level is close; the LRA differs and this is not a full-mix or human mix approval.

**Primary trim candidate: source 17.828–315.828 s, sequence 0–298 s.** It keeps the whole source ending and exactly fills the picture without looping or time stretch. A 20 ms RMS envelope autocorrelation peaks at a 0.500 s period (~120 BPM, correlation 0.893). Positive RMS-flux phase is estimated near 0.48 s from the decoded origin; with the MP3 stream start at 0.023 s, the next likely pulse after 17.828 s is about 17.98 s. The planned 0.18 s fade-in settles near that pulse. This is a signal-based alignment hypothesis, not verified bar/downbeat analysis. Numeric analysis found only 1.040 s of leading silence at a −50 dB/100 ms detector threshold and no detected end silence; the last source second still measures about −27.75 dBFS RMS. A 2-second native ending fade is therefore needed.

A 15-second audition pair for the 17.828 s and 18.0 s starts, plus a pair of ending windows for the full-tail and half-second-grid trims, is in [music-candidates/auditions](../music-candidates/auditions/). The files were generated from the original source but not listened to by the agent. The 18.0–315.828 s alternative preserves the full tail but ends 0.172 s before the sequence; 17.5–315.5 s fits 298 s on a half-second grid but cuts the final 0.328 s of non-silent source content.

## Native track and clip plan

The existing baked mix should stay intact for rollback, muted by a future stable-ID, desired-state track adapter. Do not use role-wide `setAudioRoleSetting("music", "muted", true)`: it would also mute the replacement music track. The plan creates four role-correct lanes: **New Gods** (`music`), **Motion FX** (`effects`), **Interface FX** (`effects`), and **Ambience** (`ambience`). Two effect lanes preserve the existing overlap between the woosh at 252 s and click at 253 s (0.510794 s overlap). The location clip at 226–232 s and click at 227 s are on separate role tracks. Two generated swells at 123.9–125.7 and 134.4–137.3 s share the ambience lane with location because those ambience intervals do not overlap.

The complete score has 55 clips: one New Gods score clip, 51 source-backed cue clips (all 14 `quick-air-woosh.mp3` events and 37 `interface-click.mp3` events), one location clip, and two generated swell clips. All 51 source-backed cue events retain their current starts. The standalone files and their hashes are in the cue manifest. Source peaks are −0.268 dBFS for the woosh and −0.267 dBFS for the click. To preserve the old builder's 0.03/0.045 linear peak targets plus its +1 dB final gain, use approximately −29.19 dB and −25.67 dB clip gain respectively. These are peak-derived values; native full-mix export still needs a true-peak check.

The original location WAV remains a separate 6-second clip at sequence 226 s. Its measured stereo RMS is about −29.21 dBFS; the prior builder normalized to 0.075 linear RMS then added +1 dB final gain. Use about +7.7 dB clip gain with a native gain curve approximating the original smooth envelope: local 0–0.8 s at −36 dB; 0.8–2.0 s to +7.7 dB; hold through 4.5 s; return to −36 dB by 5.7 s and hold to 6 s. The −36 dB keyframe floor is near-silence, not exact zero.

The two swells are now standalone float32 PCM WAV files in `generated-assets/`, produced with `marketing/.venv/bin/python` (NumPy 2.5.3) from the original score builder equations, seed 28, at 48 kHz stereo. The manifest records exact file hashes and recipe. The 1.8 s centered swell is `5a4c52c36bd9b99fa37abf6ab3edc1141c7121ba7813b64d5c8fcdcf60eebb06`; the 2.9 s left-to-right swell is `71b5f514e5f441418f371429b1a0791327282d9f788d84007a3fdafc89fa4c97`. Both are now ready as individual native audio clips with +1 dB gain. The [reproducible stem-preparation script](prepare-new-gods-native-swells.py) writes only under `native-audio-plan-p9/generated-assets`; it does not touch the baked master or project file.

## Actual command boundary

- `timeline.action.createTimelineTrack` accepts an audio role and is in the reversible `applyDraftBatch` allowlist. The JSON fragment contains a valid planning-only 4-track batch with a generated batch ID; resolve returned track IDs from receipts.
- `timeline.action.createTimelineClip` is registered and task-owned as an individual native action. It requires an admitted `import_media` path and typed source trim. It is deliberately excluded from `applyDraftBatch`; create each clip individually and record its returned clip ID before continuing.
- `timeline.action.setTimelineClipAnimation` and `timeline.action.editTimelineClipKeyframe` support the audio clip `audioGainDb` channel and are batchable. Clip keyframe range is −36 to +24 dB. Create clips first, then send audio-gain channel updates in `applyDraftBatch` groups of at most 16 operations.
- The source patch adds task-owned, stable-ID actions `timeline.action.setTimelineTrackMuted` and `timeline.action.setTimelineTrackAudioRole`; each requires an expected prior state, rejects stale/locked/non-audio targets, and is idempotent when the desired state already holds. `setTimelineTrackMuted` routes through the existing undoable track-setting controller path. These actions require the focused source build and matching app binary before use. No action replaces or deletes a clip. The prepared runner uses `author_native.py` to fsync a durable intent before each native call, record every returned `createdClipId`, and refuse to replay an unresolved call. `createTimelineClip` remains an individual call; track creation can batch, and gain curves can batch after clip IDs are read from receipts. The runner will bind only the latest P9 document hash and verify that all six exact asset paths are admitted before it sends any edit.

## Guarded native authoring runner

`build_new_gods_native_authoring_plan.py` writes an `author_native.py` plan containing 127 typed native steps: four audio tracks, role readbacks, temporary mute on the four new lanes, 55 individual source clip imports, 55 audioGainDb edits, then the final mute/unmute cutover. Every import step resolves its track ID from a native track receipt; every gain step resolves its clip ID from that exact `createTimelineClip` receipt. The existing `author_native.py` runner fsyncs an intent before each mutating call, keeps the append-only NDJSON result log, and refuses to replay a source import whose result receipt is unresolved. It batches the safe track/animation operations in groups of at most 16.

The wrapper verifies the protected P9 snapshot hash, all six source-file hashes, and exact equality between those six canonical paths and the admitted task paths before any native edit. `--apply` also requires the exact active task ID. The first plan start must still match document hash `4d8157be5d32d94ec6ad7c01b2705175adb09a637afedc6d151148f8e2e72a48`. The generated template is [new-gods-native-authoring-plan-p9.template.json](new-gods-native-authoring-plan-p9.template.json).

Local preflight, with no app/MCP call:

```sh
python3 marketing/native-recreation-v4/polish-pass-2026-10-30/native-audio-plan-p9/run_new_gods_native_authoring.py
```

Apply only after the matching Release review binary is running, P9 has been read back at the guarded hash, a fresh task owns that Timeline, and the task has admitted the exact six source paths. Use the exact task ID returned by task admission:

```sh
python3 marketing/native-recreation-v4/polish-pass-2026-10-30/native-audio-plan-p9/run_new_gods_native_authoring.py \
  --apply --task-id YOUR_FRESH_TASK_ID
```

The default receipt root is `native-audio-plan-p9/receipts/<task-id>/`, containing the task-bound plan, human-readable `receipt.json`, and append-only, fsynced `receipt.ndjson`. A completed step resumes only when its exact plan digest, step ID, command, values, and native result receipt match; clip IDs are resolved only from `createdClipId` results.

**Uncertain import behavior:** if the process dies after writing `invoke_started` for a `createTimelineClip` but before writing `invoke_completed`, the next run reads the native task, then refuses the unresolved NDJSON entry before sending another mutation. It will not retry that import. A recorded `invoke_failed` also blocks resume. This is a safe stop, not automatic reconciliation: inspect/recover the task, read back whether the clip exists and its exact track, source, start, trim, and duration, then author a new continuation from the reconciled document hash and native `clipId`. Keep the old task receipt/log intact; do not edit or delete it to force replay. If native readback cannot establish whether the import committed, stop and preserve the task for review.

Local-only validation (no MCP call) is `python3 marketing/native-recreation-v4/polish-pass-2026-10-30/native-audio-plan-p9/run_new_gods_native_authoring.py`. The latest run verified 127 steps, 55 audio clips, the P9 snapshot, and all six paths. Native execution remains pending the matching staged app binary and a fresh audio task whose exact path scope and task ID have been admitted.

## Review after application

The native export should confirm that Music 1 remains intact but muted, the new New Gods lane is unmuted, all 55 clip IDs came from native receipts, and the audio picture remains exactly 298 seconds. Check the opening pulse and fade, 123–138 s transition swells, 226–232 s location duck, and final tail. Measure full-mix EBU R128 and true peak after all clips are present, compare it with the old master, and have a person listen before calling the soundtrack approved.
