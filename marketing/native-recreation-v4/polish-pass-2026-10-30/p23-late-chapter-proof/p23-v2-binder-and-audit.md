# P23 v2 binder and saved-state audit

P23 v2 supersedes the initial offline fragment for binding. The original fragment remains unchanged as the first proposal record.

## Offline preflight completed

`prepare_p23_v2.py` ran `validate_clip_references` and `verify_placement` against the exact P22 planning baseline and latest P20 candidate:

- P22 file SHA-256: `b6fc44862237506bb661ce69f3b5cb82e67806fe573d4274dfaa1952b0d4f37b`; document hash `c2fb601cf1808116a7aaaa87aaecfb22c340b4f4bf719876cb9b8be932918b0c`.
- P20 file SHA-256: `2728aaec37df78003811f0aab81ac54ffc4142b986c0fa503826eb79b2dc23b1`; document hash `801fea1560cd6ecd77a940d7926e63948d367598d8ec81b79eef1ffa319ae799`; 196 tracks and 1,453 clips.
- All 24 P23 target clips retain their P22 field/keyframe/source-track snapshots in P20. All 12 proposed G34–G45 lanes are clear over `[259,273)`. The 196-track order is unchanged.
- `validate_clip_references`: 24 stable existing targets and four valid receipt-bound create references.
- `verify_placement`: 24 target source tracks, 32 compositor tracks, and all planned intervals validated; global track order unchanged.

The executor must still bind a fresh active native task and recheck the current document hash, target fields, keyframes, and destination lanes before applying. A later live change invalidates this preflight.

## After-save audit command

After the native task saves and produces the planned receipts, run:

```sh
python3 marketing/native-recreation-v4/polish-pass-2026-10-30/p23-late-chapter-proof/audit_p23_saved.py \
  --bound-plan marketing/native-recreation-v4/polish-pass-2026-10-30/pass23-late-bound.json \
  --saved-project marketing/native-recreation-v4/polish-pass-2026-10-30/native-projects/looksy-polish-pass23-late-proof.json \
  --checkpoint marketing/native-recreation-v4/polish-pass-2026-10-30/checkpoint-pass23-late-proof-result.json \
  --execution-receipt marketing/native-recreation-v4/polish-pass-2026-10-30/pass23-late.execution.json \
  --output marketing/native-recreation-v4/polish-pass-2026-10-30/p23-late-chapter-proof/p23-late-native-saved-audit.json
```

The audit compares the saved P23 result with the full P20 baseline; checks exact bound-plan, save-checkpoint and operation-receipt hashes; verifies all 24 existing target changes and keyframe/source/timing preservation; binds all 12 created clip IDs to successful receipts; checks all four curves; and requires the other 1,429 original clips, track metadata/order, audio, and project fields to remain unchanged. It permits only subpixel geometry tolerances recorded in its output, including a bounded minimum-pixel rule normalization. It does not claim render parity; a fresh native A/B render review remains separate.
