# Native MCP mutation cost audit

Date: 2026-10-31  
Scope: read-only/offline inspection of the P15/P16 receipts and the current task-mutation code. No native worker calls, app actions, builds, or source edits were made. The full native film render was active during this audit, so I did not start a competing fixture benchmark.

## Measured evidence

The P16 one-title edit spent **11,378.5 ms in one native RPC**. Its request was 338 bytes and response 1,146 bytes. The immediately preceding `timeline.inspect_document` was **3,303.8 ms** for a 2,814-byte response. The MCP/schema lookups and receipt writes around that call were each about 1 ms or less.

P15 applied 13 child operations in one `timeline.action.applyDraftBatch` RPC: **12,416.4 ms**, 5,801-byte request, 4,866-byte response. This is one transaction, not 13 network calls. Its time is close to P16’s single-action transaction, which points to fixed per-transaction document work as the dominant cost; the child setter count is not the main driver.

The matching pre-P16 P15 snapshot is **49,707,655 bytes** (`documentHash=4fc5b756…79fc`). The P16 post-edit snapshot is **49,707,652 bytes** (`documentHash=438df6c8…24a`). The saved project JSON is 139,269,517 bytes for P15 and 139,269,514 bytes for P16 because those files are indented. Thus the action is guarded by a roughly 49.7 MB canonical task state, even though the RPC payload is only a few KB.

The released P16 task journal is 3,944 bytes with three historical steps. That rules out journal size as the explanation for this particular 11.4-second transaction. Repeated conversion and whole-file journal saves can become cumulative cost as portrait work adds hundreds of steps, but that is a separate, source-derived concern rather than a measured P16 bottleneck.

### Receipt identities

- `pass16-source-label-v2.execution.json` SHA-256 `99a2295c9b70fdd120d99d55c786ae84b33555ac13b2eb01a0540faaf9267131`
- `pass16-source-label-v2.execution.ndjson` SHA-256 `a3ea11922f0f2283dfce921366224e7838f8aaa1dfb0337ed7843481768daedf`
- `pass15-combined-polish.execution.json` SHA-256 `af2d7dcd4d77cb472d52ad3b33c097dc6c981da3431fef3391938372974b58ff`
- `pass15-combined-polish.execution.ndjson` SHA-256 `f472817bff1966ff270a0c6e5c8a9c085e6329ab10e1d721ad2c1933a59d84a2`
- P15/P16 native project files: [P15](native-projects/looksy-polish-pass15-final-graphics.json) SHA-256 `5c6c9df7ead0133cbebe58740200cccf4cfab868ac4bc0f60a5410c62b74851c`; [P16](native-projects/looksy-polish-pass16-source-label.json) SHA-256 `cf34f9c4e035ceb86d5ad0e9c3a5ea6d1c3f00d97b7e21175d383939fb21f26c`.

## Call path and repeated work

The registered non-creating `setTimelineClipTitle` route skips created-clip ID discovery; it does not scan `owner->clips()` before and after. Task admission already enrolled and captured the Timeline baseline, so the repeated per-action `enroll()` sees a checkpoint and does not take another baseline read.

For a task-owned synchronous batch, the current path is:

1. `AgentTaskCoordinator::invokeWithPreimage()` calls the adapter’s `read()` for the exact preimage. This is `agentTaskDocumentState()`, which runs `documentObject()`, removes `updatedAt`, and compact-serializes the whole document.
2. `AgentTaskJournal::prepare()` hashes the current bytes, durably saves the prepared step, and returns a report.
3. `runAgentTaskMutationBatchImpl()` uses those trusted preimage bytes, takes its Undo snapshot, applies the typed operations, then builds a second full `documentObject()`. It parses the 49.7 MB preimage into a `QJsonObject` and compares the complete objects to decide `changed`.
4. After the controller returns, the coordinator calls adapter `read()` again, generating the full task state a third time. It compares the exact before/after byte arrays and passes the after bytes to `journal.complete()` for the checkpoint hash and durable completion record.

That is three full live-document projections in one mutation (before, inner after, outer after), one full preimage JSON parse, exact byte comparison, and SHA-256 work. It explains why a 13-operation batch is still roughly as slow as one operation. There are no internal phase timers in the receipts, so assigning an exact number of milliseconds to each stage would be an inference. The measured 3.30-second `inspect_document` on the same state and ~11–12.4-second mutation receipts are consistent with repeated full-model projections being the primary cost.

The undo snapshot is copied using Qt implicit sharing and the current code reuses equal immutable clip arrays where possible. It may still scan/compare many clips; no transaction-specific timing isolates that cost. `tests/timeline_recovery_test.cpp` has a 10,000-clip incremental-load/edit stress fixture with latency thresholds, but no retained log from that test was found for comparison here.

The task journal adds a second, growing cost: `prepare()` and `complete()` each call `report()`, and `report()` converts the entire journal object—including all prior steps—to `QVariantMap`. The mutation coordinator consumes only `ok`, `stepId` from prepare and `ok` from complete. Each path also rewrites the durable journal JSON; those writes should not be removed or weakened casually because they bracket the native operation for crash recovery.

## Minimal safe optimization proposal

The largest safe reduction is to reuse one exact postimage rather than build the live document twice after the edit. Do **not** replace final equality with a simple `setDirty()`/touched flag: a batch can do A→B→A and must remain net unchanged, with no new undo unit, revision, or notification. A touched flag also cannot prove equality after rollback.

Recommended design for review, not an implementation performed in this audit:

1. Keep the trusted exact preimage and all current stale-check, rollback, baseline, and journal SHA-256 guards.
2. At the end of the synchronous controller batch, create one canonical compact task postimage from the already-built final document object, excluding `updatedAt` exactly as `agentTaskDocumentState()` does. Byte-compare it to the trusted preimage for net-zero detection; this preserves A→B→A semantics without parsing the preimage back into a second full JSON tree.
3. Pass the postimage through an internal typed result/out-parameter to `AgentTaskCoordinator`; do not put a 50 MB byte array into a `QVariantMap` or MCP response. Use these same bytes for `journal.complete()` instead of calling `adapter.read()` a second time. This preserves exact checkpoint hashes while reducing live-document projection count from three to two.
4. Keep the existing full after-read on failure/rollback paths unless an independently verified exact restored postimage is available. Capture the success postimage after controller notifications, or otherwise demonstrate that synchronous callbacks cannot reenter and mutate the model between capture and return. Preserve the current task lock/owner-thread constraints.
5. Give internal journal callers narrow prepare/complete acknowledgment results rather than `report().toVariantMap()`, while retaining the complete report for explicit task inspection/recovery and retaining both durable `save()` calls.

Validation for a future patch should include a production-shaped 1,289-clip / ~49.7 MB fixture and separate timers for `documentObject`, compact serialization, preimage parse, SHA-256, journal report conversion/save, and Undo capture. Behavioral tests should prove:

- A real edit produces a returned postimage whose independent `agentTaskDocumentState()` bytes and hash match exactly.
- A no-op and an A→B→A batch are net unchanged: no Undo addition, document revision change, task hash change, or authored-state signal.
- A failed multi-operation batch restores the exact preimage and its prior undo/redo/revision state.
- A changed external document is still rejected before invocation, and `journal.complete()` records the exact final postimage hash.
- Reentrant signal handling cannot make the returned postimage stale; if this cannot be guaranteed, do not use postimage handoff.

## Bounds and caveats

- The 11.4-second P16 and 12.4-second P15 values are measured whole-RPC durations. The per-stage split above is source inspection plus a timing correlation, not a profiler trace.
- The P16 per-action re-enrollment does not recapture the baseline because task admission already enrolled the resource.
- `document_edit_revision_` is not a valid in-batch change flag: `setDirty()` intentionally defers revision increments while `agent_task_mutation_batch_active_` is true, and commit increments once afterward.
- No source changes, app interaction, worker mutation, or competing build were made. A private C++ phase benchmark was skipped to avoid contention with the active full-film render; this report records the existing native measurements instead.
