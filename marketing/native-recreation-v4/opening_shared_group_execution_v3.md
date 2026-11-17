# Opening shared-group execution: v3 safety recipe

This is an offline execution recipe for the existing v2 landscape and portrait
fragments. It does not contain an execution receipt and has not been run. Keep
the v2 fragments and their prior evidence unchanged.

## Action manifest checked against the current source

The phase-1 `applyDraftBatch` contains 12 operations, within the runner's
16-operation limit: six `timeline.action.clearTimelineClipMask` operations and
six `timeline.action.setTimelineClipAnimation` operations. The animation calls
replace only each child's `opacity` channel with a held value at local time
zero. `author_native.py`'s batch allowlist includes both actions.

The async producer is a separate task-owned command family:

- `timeline.action.createTimelineCompound`: `requestId`, `childTimelineId`,
  `label`, and stable `clipIds` only; it accepts no caller paths.
- `timeline.action.startTimelineCompound`,
  `timeline.action.inspectTimelineCompound`,
  `timeline.action.cancelTimelineCompound`,
  `timeline.action.settleTimelineCompound`, and
  `timeline.action.reconcileTimelineCompound`: `requestId` only.

The parent reveal uses `timeline.action.setTimelineClipMask` and
`timeline.action.setTimelineClipAnimation`, both typed native actions. The
compound adapter derives all five output siblings from the active task's sealed
`outputDirectory` and journals them before starting a producer.

The saved v15 task snapshot includes `layers_luts`, `masks`, and `graphics`,
which cover these actions under the current authorization mapping. Reuse that
task only if a fresh `timeline.inspect_task` still reports `status: running`,
the exact current Timeline target, and the required capabilities. If it is
reviewed, released, or otherwise inactive, finish/release its lifecycle first
and admit a new correction task for the exact Timeline with those capabilities,
the same approved canonical source asset paths, and a task-owned output root.
Never copy the old task ID or assume its permissions remain active.

## 1. Preconditions before the first correction

Use the ready native Timeline after the complete standard landscape suffix and
other agreed opening edits. Read `timeline.inspect_document` and
`timeline.inspect_task` and capture the current document hash. Require:

- landscape is exactly 1920×1080 at 30 fps; portrait is exactly 1080×1920 at
  30 fps;
- `ready` is true and the task targets the inspected resource;
- the active task status is `running` and has `layers_luts`, `masks`, and
  `graphics` in its approved capability snapshot;
- all six fragment clip IDs exist in the current copied Timeline and cover
  global 0–12 seconds;
- no earlier journal step already uses the fragment's batch or compound
  request ID.

If any check fails, stop before mutation. In particular, do not assume the
historic fragment IDs survived a copy/import; verify the current document.

## 2. Reset the six children, then prepare the compound

Build a new precompound plan with the current resource, running task, and exact
starting document hash. Use a fresh output filename so prior evidence cannot
be overwritten:

```sh
python3 marketing/native-recreation-v4/build_opening_group_correction_v2.py \
  --orientation landscape \
  --phase precompound \
  --resource-id 'timeline:<current-uuid>' \
  --task-id '<current-running-task-uuid>' \
  --starting-document-hash '<current-document-sha256>' \
  --output marketing/native-recreation-v4/opening-group-landscape-precompound-<checkpoint>.json

python3 marketing/native-recreation-v4/author_native.py \
  marketing/native-recreation-v4/opening-group-landscape-precompound-<checkpoint>.json \
  --receipt marketing/native-recreation-v4/evidence/opening-group-landscape-precompound-<checkpoint>.receipt.json \
  --ndjson marketing/native-recreation-v4/evidence/opening-group-landscape-precompound-<checkpoint>.ndjson
```

Use separate portrait filenames and `--orientation portrait` for portrait. Do
not edit any clip while the compound operation is pending or unresolved.

Only after the exact 12-operation phase-1 receipt succeeds, inspect the current
document again and create the compound with the fixed values in the matching
fragment. Send that action through `app.invoke_command`; never through
`author_native.py`:

```json
{
  "commandId": "timeline.action.createTimelineCompound",
  "values": {
    "requestId": "<fragment requestId>",
    "childTimelineId": "<fragment childTimelineId>",
    "label": "<fragment label>",
    "clipIds": ["<the six verified IDs, in fragment order>"]
  }
}
```

Do not add output paths. Before calling, verify the sealed task output directory
is present and that the five derived sibling names do not already belong to
another task operation.

## 3. Bounded async polling and restart recovery

Use one request ID for the entire operation. Never submit a second create call
with a new ID to work around a timeout.

1. Give the initial create call a bounded RPC timeout. If its reply is lost,
   inspect the same `requestId`; do not create again until the native task
   journal proves whether the request was prepared, accepted, completed, or
   failed.
2. If the result is `prepared: true`, `accepted: false`, and `retryable: true`,
   leave the prepared child and parent in place. Inspect the same ID, then call
   `startTimelineCompound` for that same request only after inspection still
   reports the exact prepared step. Use bounded backoff (2, 4, 8, 16, then 30
   seconds) for at most two minutes. If it remains deferred, stop and leave the
   task unresolved for a later resume.
3. Once accepted, poll `inspectTimelineCompound` with a two-second interval,
   increasing to ten seconds, for at most 15 minutes. A polling timeout is not
   a producer failure: stop polling, preserve the task-owned files, and resume
   later by inspecting the same request ID. Do not cancel, delete files, or
   revert merely because the deadline expired.
4. On a lost `start` or `settle` reply, inspect the same request and consult its
   task-journal step. The producer helper verifies its persisted executable,
   arguments, uid, boot/session, start identity, and exact output paths. If
   identity is missing, ambiguous, or mismatched, fail closed and preserve all
   five files.
5. Call `settleTimelineCompound` only after inspection reports terminal state.
   Require `settled: true`, `status: ready`, and decoded
   `transparencyVerified: true`. A container/codec label alone is insufficient.
   If the settlement reply is uncertain, inspect the same ID and journal step;
   do not repeat create or start.
6. Use cancel/reconcile only for an explicitly chosen cancellation or a
   recoverable interrupted operation. Require verified producer stop or
   verified absence for the exact persisted launch spec before task-journal
   reconciliation. Any unknown writer state blocks cleanup and task revert.

The current MCP `inspectTimelineCompound` result exposes process/status and
task-owned paths, but does not return the child's complete editable clip
document. The fragment's child-document editability condition therefore still
needs a separate native child readback proof before claiming it passed. Do not
infer that proof from `ready` or alpha verification.

## 4. Apply the shared parent reveal after settlement

After the child readback proof and successful compound settlement, inspect the
document and active task again. Use the new document hash for a fresh parent
reveal plan. Supply the exact phase-1 plan/receipt and compound settlement
receipt to `build_opening_group_correction_v2.py --phase parent-reveal`. The
builder validates receipt hashes and substitutes the returned parent clip ID.
Run the resulting two-action plan through `author_native.py` with new receipt
and NDJSON paths.

The parent gets the shared circle at 58%/55% and the 8–9.6 second opacity and
mask curves. Its edges are scaled independently by output width and height so
the reveal remains circular in pixels. The desktop light child remains at
opacity 0.24; other children are reset to 1.0. Do not reapply those reveal
channels to the children.

This plan remains unexecuted until its current resource/task/hash preflight
passes. It does not prove visible-frame parity by itself.
