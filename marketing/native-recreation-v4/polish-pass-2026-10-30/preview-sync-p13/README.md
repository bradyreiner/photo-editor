# P13 preview follows the edit

This proposal repairs the video-preview chapter at project seconds 196–208. It uses the protected P13 native checkpoint, the existing G12 preview layer, the existing G13/G14 title layers, and the original four footage sources. It does not edit the serialized project file.

The reference's `FilmPolish.tsx` clock and `State.ts` source selection produce eight adjacent source windows at 30 fps. The native plan stores one `sourceTime` point for every rendered frame (360 points total), plus one valid, non-rendered endpoint per native clip (368 points total). The six visible text spans follow the selected footage; the five-frame city and six-frame ridge reverse windows are intentionally short and require readability review in the native render.

The bounded operation list has 49 steps: it stages 12 existing clips outside the chapter, changes the stale clock string to `PROGRAM / SEQUENCE 01`, reuses four current preview clips, imports four additional instances of existing footage, adds four native text clips to the existing title layers, and restores the existing clips to the final 196–208 ranges. New clip IDs are read from `author_native` mutation receipts with `{result, field: "createdClipId"}` references. They are never predicted or copied by hand.

## Local plan verification

From the repository root:

```sh
python3 marketing/native-recreation-v4/polish-pass-2026-10-30/preview-sync-p13/verify_preview_sync_p13_plan.py
python3 marketing/native-recreation-v4/polish-pass-2026-10-30/preview-sync-p13/run_preview_sync_p13.py
```

These commands read the fixed P13 snapshot, source assets, the plan template, and Remotion source hashes. They do not call the app or write a Timeline project. The template and storyboard are frozen review artifacts; if a guard fails, stop and prepare a newly reviewed plan instead of regenerating the old one from changed inputs.

## Native execution handoff

After a fresh native inspection confirms the target clips/tracks still match this P13 review and the task has access to the four exact source video paths, bind the current task ID and the exact `timeline.inspect_document` hash:

```sh
python3 marketing/native-recreation-v4/polish-pass-2026-10-30/preview-sync-p13/run_preview_sync_p13.py \
  --apply --task-id '<fresh-task-id>' --document-hash '<fresh-64-hex-documentHash>'
```

The wrapper verifies the active task owns the timeline and the four imported paths, then delegates to `author_native.py` using the compact exact-hash inspector. Receipts and append-only intents are written under `preview-sync-p13/receipts/<task-id>/`. A later retry must use that same task receipt directory. `author_native.py` refuses to replay a `createTimelineClip` with an unresolved intent; inspect the native task and journal before any recovery.

## Expected native readback

The final document keeps the existing 196 tracks, adds exactly four video clips and four title clips, retains all existing clip IDs, leaves all clips outside the named chapter IDs untouched, and returns sequence duration to 298 seconds. No new track or source media is created. Root must compare the listed native sample frames against the Remotion reference for shot identity and title alignment, then audit the whole document for the eight expected additions and unchanged outside timing.

The transient staging positions are 300–335 seconds so no target clip overlaps during the transaction. If execution stops partway, the journal is authoritative; do not release or continue the task until the receipt has been reconciled.
