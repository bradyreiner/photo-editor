# Native Timeline batch acceptance

This is a guarded acceptance run for the new native draft batch capability. It uses a fresh disposable Timeline and a novel canonical copy of one admitted manifest PNG. It does not edit the Remotion trailer project, admit or recover a task automatically, or grant export/commit access. Do not run the mutating commands until the root agent has opened the fresh Timeline and admitted its exact resource, source path, output directory, and style/action plan.

The Python runner writes per-RPC timing and request/response byte counts to an append-only NDJSON file. It persists each mutation intent before calling native MCP and refuses to replay a call whose response receipt is missing. The offline tests use fake MCP responses only; the live phases below have not been run.

## Preconditions

Root should create and save a new, disposable native Timeline with no clips, retain its canonical saved project path, and record its `resourceId`. Before task admission, create a fresh canonical input copy of the manifest PNG and admit the exact copied path plus an existing canonical output directory. Include the task categories for media, layers, text, framing, masking, effects, and delivery. The task must retain the existing edit/style gates and the read/draft separation; export and commit remain unavailable.

The untouched manifest source is:

- Path: `/Users/bradyreiner/projects/PersonalProjects/photo-editor/marketing/animated-trailer/public/media/creative-polish-v2/detail/detail-0.png`
- Size: `1296872` bytes
- SHA-256: `6109f298f60e5f938f8c5ac8bfd426a03bf84ede460a8c522cf2d6a9686ca47f`

Make a unique copy before admitting the task. The runner leaves the original unchanged and verifies that the copy has the same byte count and SHA-256. The copy path is `marketing/native-recreation-v4/acceptance-input/<copy-id>/detail-0.png`; root must admit that exact printed canonical path so the new copy exercises source hashing instead of reusing a previously observed hero file.

```sh
python3 marketing/native-recreation-v4/batch_acceptance.py prepare-source \
  --copy-id '<new unique short ID>'
```

The plan's `canonicalDocumentPath` is the saved empty baseline used for the release/reopen/recovery test. `outputDirectory` must be the same exact directory admitted by the task. The runner saves one separate task-owned native project copy within it.

## Prepare and preflight

After root supplies the task and Timeline identifiers, make a local plan. This phase reads the source manifest and writes a JSON file; it makes no MCP calls.

```sh
python3 marketing/native-recreation-v4/batch_acceptance.py prepare \
  --task-id '<exact admitted task ID>' \
  --resource-id 'timeline:<exact stable ID>' \
  --source-copy '<exact canonical path printed by prepare-source and admitted by root>' \
  --output-directory '<exact admitted canonical output directory>' \
  --canonical-document '<canonical saved empty Timeline path>' \
  --plan marketing/native-recreation-v4/evidence/batch-acceptance-<task>.json
```

Then perform read-only native checks. They verify the active task, exact Timeline target, source allowlist, output-directory scope, approved categories, empty baseline, and required typed actions including `applyDraftBatch`, mask, blend, and save.

```sh
python3 marketing/native-recreation-v4/batch_acceptance.py preflight \
  --plan marketing/native-recreation-v4/evidence/batch-acceptance-<task>.json
```

## Draft authoring and idempotency

The `author` phase requires both `--execute` and exact task/resource confirmation. It creates three video tracks in one 3-operation batch, imports the admitted photo as a separate non-batch media action, then performs an 8-operation batch: backplate graphic and frame, headline title and title update, photo frame, rounded sequence-space mask, screen blend, and opacity keyframes. It reads back stable native IDs and saves one task-owned project copy. No path or file is selected by an unrestricted caller.

```sh
python3 marketing/native-recreation-v4/batch_acceptance.py author \
  --plan marketing/native-recreation-v4/evidence/batch-acceptance-<task>.json \
  --execute --confirm-task-id '<exact task ID>' \
  --confirm-resource-id 'timeline:<exact stable ID>'
```

Replay the same 8-operation batch with the exact body and ID; verify the native document hash and clip ID list stay unchanged. Then change one valid title string while reusing the batch ID; the runner expects an MCP error and verifies no document or ID change. A rejection is considered a passing negative assertion only when the MCP `result.isError` envelope is present.

```sh
python3 marketing/native-recreation-v4/batch_acceptance.py replay \
  --plan marketing/native-recreation-v4/evidence/batch-acceptance-<task>.json \
  --execute --confirm-task-id '<exact task ID>' \
  --confirm-resource-id 'timeline:<exact stable ID>'
```

## Visible mask/blend and Undo/Redo proof

At approximately 2 seconds, root should inspect the actual native Program image with the photo frame, rounded mask, and screen blend visible over the backplate. The saved screenshot needs to show the Program and enough Timeline context to identify the active clip. The runner verifies native mask/blend readback and the screenshot hash but does not judge pixels. The reviewer label defaults to `assistant`; `--reviewer-type user` is optional.

```sh
python3 marketing/native-recreation-v4/batch_acceptance.py verify-visible \
  --plan marketing/native-recreation-v4/evidence/batch-acceptance-<task>.json \
  --confirm-task-id '<exact task ID>' \
  --confirm-resource-id 'timeline:<exact stable ID>' \
  --evidence-visible '<screenshot captured from native Looksy>'
```

With the Timeline focused, use one native Undo. The runner must read back the exact document hash captured after the separate photo import but before batch B. Capture a screenshot and record it:

```sh
python3 marketing/native-recreation-v4/batch_acceptance.py verify-undo \
  --plan marketing/native-recreation-v4/evidence/batch-acceptance-<task>.json \
  --confirm-task-id '<exact task ID>' \
  --confirm-resource-id 'timeline:<exact stable ID>' \
  --evidence-after-undo '<screenshot captured after one native Undo>'
```

Use one native Redo and capture a second screenshot. The runner requires the complete post-batch hash to match again before allowing review/release:

```sh
python3 marketing/native-recreation-v4/batch_acceptance.py verify-redo \
  --plan marketing/native-recreation-v4/evidence/batch-acceptance-<task>.json \
  --confirm-task-id '<exact task ID>' \
  --confirm-resource-id 'timeline:<exact stable ID>' \
  --evidence-after-redo '<screenshot captured after one native Redo>'
```

## Review, source identity drain, recovery, and revert

Only after native visual and Undo/Redo evidence passes may the task enter review and release its Timeline ownership:

```sh
python3 marketing/native-recreation-v4/batch_acceptance.py review-release \
  --plan marketing/native-recreation-v4/evidence/batch-acceptance-<task>.json \
  --execute --confirm-task-id '<exact task ID>' \
  --confirm-resource-id 'timeline:<exact stable ID>'
```

The runner then polls read-only Timeline and clip details for up to 90 seconds. It requires the exact novel admitted copy path, byte size, and full SHA-256 to settle; it records intermediate `ready:false` periods rather than treating them as a source change. Any unexpected path, size, or digest stops the phase and preserves the task evidence.

```sh
python3 marketing/native-recreation-v4/batch_acceptance.py drain \
  --plan marketing/native-recreation-v4/evidence/batch-acceptance-<task>.json \
  --drain-timeout 90
```

Root should close and reopen the *same canonical empty-baseline document path* in Looksy. The runner only reattaches the released task if the open Timeline exposes the same stable `resourceId`. It also verifies the imported clip still has the admitted source's canonical path, byte size, and full digest. A failure is an explicit recovery conflict; the runner does not normalize or migrate old hashes.

```sh
python3 marketing/native-recreation-v4/batch_acceptance.py recover \
  --plan marketing/native-recreation-v4/evidence/batch-acceptance-<task>.json \
  --execute --confirm-task-id '<exact task ID>' \
  --confirm-resource-id 'timeline:<exact stable ID>'
```

After reviewing the recovered state, the final phase requires separate `--confirm-revert`. A passing revert must restore the fresh Timeline baseline and remove the enrolled project copy. If native conflict checks block it, retain the journal and output for investigation.

```sh
python3 marketing/native-recreation-v4/batch_acceptance.py revert \
  --plan marketing/native-recreation-v4/evidence/batch-acceptance-<task>.json \
  --execute --confirm-revert --confirm-task-id '<exact task ID>' \
  --confirm-resource-id 'timeline:<exact stable ID>'
```

## Evidence and limits

The `<plan>.ndjson` file contains each RPC's tool, request and result, local wall time, request/response size, and durable mutation intent/outcome. `<plan>.receipt.json` carries stable track/clip IDs, source identity at import and after derived work, document hashes, screenshot hashes, replay outcomes, release/recovery state, and final revert outcome.

The source SHA transition after a real imported photo remains a key observation: the run must expose whether post-release derived metadata changes the task checkpoint and whether canonical reopen/recovery/revert still succeeds. A source identity mismatch remains fail-closed. This plan does not enable compound renders, exports, commits, or any broader filesystem operation.
