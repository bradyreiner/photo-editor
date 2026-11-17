# Opening shared desktop group: v2 execution recipe

The landscape and portrait correction fragments are offline, unexecuted plans. The v2 native batch payload has exactly 12 operations: six `clearTimelineClipMask` actions and six opacity-only `setTimelineClipAnimation` actions. The operation notes are descriptive metadata outside the native payload. The batch clears child mask curves and preserves every other animated channel.

The compound producer is asynchronous and must stay outside `author_native.py`. The runner treats a successful RPC as a completed step; that does not prove a background compound render settled. Use the native task-owned compound actions for the producer lifecycle, then use `author_native.py` for the two synchronous edit phases.

## 1. Preflight and run the child cleanup plan

Wait until the matching full chapter plan and the three opening parity corrections are complete. Read the current native document and active task. Require a ready 30 fps Timeline with the fragment’s exact dimensions, all six listed stable IDs at 0–12 seconds, and an active task that targets the inspected Timeline resource. Use the exact current document hash.

Build a new scoped plan file. Choose a new filename for each resource/hash checkpoint; the builder refuses to overwrite existing files.

```sh
python3 marketing/native-recreation-v4/build_opening_group_correction_v2.py \
  --orientation landscape \
  --phase precompound \
  --resource-id 'timeline:<current-timeline-uuid>' \
  --task-id '<current-running-task-uuid>' \
  --starting-document-hash '<current-64-character-document-sha256>' \
  --output marketing/native-recreation-v4/opening-group-landscape-precompound-<checkpoint>.json

python3 marketing/native-recreation-v4/author_native.py \
  marketing/native-recreation-v4/opening-group-landscape-precompound-<checkpoint>.json \
  --receipt marketing/native-recreation-v4/evidence/opening-group-landscape-precompound-<checkpoint>.receipt.json \
  --ndjson marketing/native-recreation-v4/evidence/opening-group-landscape-precompound-<checkpoint>.ndjson
```

Use `--orientation portrait` and a separate receipt/plan name for portrait. The runner validates the active resource, task, composition, readiness, and exact starting document hash before mutation. It writes intent before the native batch and records the result after it. Stop and inspect if the receipt or reply is incomplete; do not replay an unresolved batch.

## 2. Create the editable compound

Inspect the document again after phase 1 and confirm the same resource/task. Send the phase 2 values from the orientation fragment through `app.invoke_command` as `timeline.action.createTimelineCompound`:

```json
{
  "commandId": "timeline.action.createTimelineCompound",
  "values": {
    "requestId": "<fragment requestId>",
    "childTimelineId": "<fragment childTimelineId>",
    "label": "<fragment label>",
    "clipIds": ["<the six fragment clip IDs, in order>"]
  }
}
```

Pass no filesystem paths. The native adapter derives its five output paths from the admitted task’s sealed output directory and journals the operation before producer launch. Use the exact fixed request ID from the fragment. Never create a replacement request after an uncertain response.

If create returns `accepted: true` and `pending: true`, inspect that same request ID with `timeline.action.inspectTimelineCompound`. Poll the native status with bounded waits. Once the producer reaches terminal state, call `timeline.action.settleTimelineCompound` for that request and require `ok: true`, `settled: true`, `status: "ready"`, and `transparencyVerified: true`. Then verify the returned `compoundClipId` and child ID. The child document must retain all six stable clip IDs and their editable non-mask/non-opacity properties.

If create returns `prepared: true` without acceptance, do not repeat create. Inspect the same request. Once the native scheduler permits work, call `timeline.action.startTimelineCompound` for that same request ID. If the producer fails, is interrupted, or cannot be identified, use the task-owned inspect/cancel/reconcile path and preserve all outputs until its journal reconciliation proves cleanup is safe. Never edit the parent while the compound step is pending or unresolved.

## 3. Apply the parent reveal after settlement

After settlement, read the current document and task again. Use the new current document hash; compound creation changed the parent Timeline. Build a post-settlement plan using the phase 1 plan/receipt and the exact saved settlement response:

```sh
python3 marketing/native-recreation-v4/build_opening_group_correction_v2.py \
  --orientation landscape \
  --phase parent-reveal \
  --resource-id 'timeline:<same-current-timeline-uuid>' \
  --task-id '<same-current-running-task-uuid>' \
  --starting-document-hash '<post-settlement-64-character-document-sha256>' \
  --phase1-plan marketing/native-recreation-v4/opening-group-landscape-precompound-<checkpoint>.json \
  --phase1-receipt marketing/native-recreation-v4/evidence/opening-group-landscape-precompound-<checkpoint>.receipt.json \
  --settlement-receipt marketing/native-recreation-v4/evidence/opening-group-landscape-settlement-<checkpoint>.json \
  --output marketing/native-recreation-v4/opening-group-landscape-parent-reveal-<checkpoint>.json

python3 marketing/native-recreation-v4/author_native.py \
  marketing/native-recreation-v4/opening-group-landscape-parent-reveal-<checkpoint>.json \
  --receipt marketing/native-recreation-v4/evidence/opening-group-landscape-parent-reveal-<checkpoint>.receipt.json \
  --ndjson marketing/native-recreation-v4/evidence/opening-group-landscape-parent-reveal-<checkpoint>.ndjson
```

Use separate portrait filenames and `--orientation portrait` for the portrait branch. The builder refuses to produce a parent plan unless the saved phase 1 receipt proves all 12 exact synchronous operations succeeded and the settlement receipt matches the fragment request, child ID, parent ID, Timeline resource, ready status, and decoded transparency receipt.

The parent plan applies the source circle center (58%, 55%) and sampled 8–9.6 second quintic opacity/mask curves. Its edge coordinates scale separately by output width and height so the mask remains circular in pixels. Do not pass the placeholder parent ID from the fragment directly; the builder substitutes the compound clip ID returned by settlement.

## Recovery and proof limits

- Use distinct plan, receipt, and NDJSON paths for every new resource/hash checkpoint; preserve prior evidence.
- Do not run either native runner against a different orientation, resource, task, or hash than the plan records.
- An async `accepted/pending` response is not settlement. A file or codec label is not decoded-alpha verification.
- If a call’s outcome is uncertain, inspect the same batch/request before retrying. Never use raw project JSON edits or caller-supplied compound output paths.
- These plans do not prove visible-frame parity. Review the resulting native preview/render after the rest of the trailer is complete.
