# Background startup recovery and dispatch barrier

The preceding idle-queue pass was verified progress, not completion. Inspection
confirmed that `JobRunner::initializeFromStore` still synchronously recovered
history and that notifications could wake an uninitialized runner. This pass
moves recovery off the foreground thread and closes dispatch until it succeeds.

## Contract

- Notifications before initialization cannot launch queue scans or jobs.
- One background recovery pass runs at a time. Repeated initialization while
  recovering or already ready cannot recover current-session work again.
- Startup recovery requires successful reads and commits in the selected
  catalog. Failed/malformed reads cannot become an empty successful recovery;
  a legacy fallback write cannot confirm a catalog commit.
- Failures keep dispatch closed and retry recovery after one second. A successful
  retry publishes readiness; it does not replay interrupted exports.
- The app initializes history/catalog views after recovery finishes. Ordinary
  job-history readers no longer migrate legacy files in the background.
- The worker retains no runner pointer. Destruction does not block or publish
  a late completion; an already running recovery may finish its durable writes.

Strict catalog behavior is explicit through `require_catalog` on store recovery
and upsert. Existing non-startup callers retain their legacy compatibility.
This is not a conversion of all job-store operations to strict catalog authority.

## Performance loop

The performance-loop skill used a three-candidate maximum, sub-100 ms admission,
at least 20 heartbeat ticks in 300 ms with a 250 ms helper delay, and protected
recovery/dispatch behavior. Native guards require exact outputs, save/undo/final
outline, matching active/exposed state and at most 10% time/memory regression.
The first implementation passes; subsequent test expansion did not change it.

Matched empty-history recovery:

- Baseline: 445 ms admission and one delayed tick,
  `/private/tmp/job-startup-baseline.log`.
- Candidate: below the millisecond timer resolution and 29 ticks,
  `/private/tmp/job-startup-final-tests.log`.

The deterministic test additionally holds dispatch closed across three delayed
recovery commits, checks exactly-once recovery, preserves paused/canceled states,
rejects malformed/read/write failures, retries without new notifications and
checks nonblocking destruction. Its fake-helper tests do not claim real catalog
durability; the following independent helper-backed run supplies that evidence.

## Real PostgreSQL and native app proof

`/private/tmp/job-startup-real-4siQIe` contains a fresh catalog seeded with three
interrupted jobs. Real helper readback verifies export `Paused`, pausing import
`Paused`, and canceling timeline `Canceled`, with recovery metadata and no render
files. Recovery admission is below 1 ms, completion takes 1201 ms, and 120 UI
heartbeat ticks fire. Log: `/private/tmp/job-startup-real-proof.log`.

Reproduce only with an empty, newly created directory matching the test prefix:

```sh
build/dev-macos-debug/tests/job_runner_startup_test \
  --real-catalog /private/tmp/job-startup-real-NEW_UNIQUE_DIRECTORY
```

Native Scene/import/Edit proof: `/private/tmp/job-startup-native-trgs4b`.
All four output PNG files are byte-identical to the preceding accepted
`/private/tmp/job-scan-native-ylv8v1` run. Neutral pixels/profile, masked exposure
and P3 autosave, final outline readiness and three-step undo pass.
`scene-edit-mask.png` was visually inspected, but does not prove pointer/stylus
or marching-ants interaction. The existing Scene proof command is in
`scene-photo-edit-handoff-2026-09-08.md`.

| Native guard metric | Before | Candidate |
| --- | ---: | ---: |
| Whole proof | 16003 ms | 16243 ms |
| Peak footprint | 2120829040 bytes | 2124286040 bytes |
| Add-mask / masked-exposure admission | 70 / 54 ms | 71 / 53 ms |
| Maximum observation-only timer gap | 1196 ms | 1206 ms |

Time increases 1.50% and peak footprint 0.16%, within the guards. These single
observations do not establish whole-app speedup, p95 or input-to-photon latency.
The whole-proof 1710 ms maximum belongs to after-capture diagnostics, including
1482 ms in the harness's AI self-check. Machine-readable guards and image hashes
are retained in the candidate's `comparison.json`.

Six focused tests pass in 21.81 seconds: startup, scan, wake and cancellation
runner tests; photo-export recovery; Scene-to-Edit handoff. App and test builds
have no warning/error diagnostics. Three separate history/model tests still fail
at the same assertions as the preceding turn: the post-resume export summary
and two uncontrolled-outage minimum-tick checks. Their results are retained in
`/private/tmp/job-startup-history-checks.log`, not counted as passes.

Both temporary proof servers were stopped after checking their exact data
directories; their databases and artifacts are retained. No user catalog changed.

## Remaining scope

Job start/completion persistence and catalog/photo publication still block the
foreground thread. Concurrent legacy migration/user mutations and crash-during-
recovery need dedicated stress/atomicity proof before extending background
writes. Startup failure/retry UI needs explicit native coverage. Diverse-camera
RAW/color calibration, real 4D training, actual embedded-model quality and the
professional editing-tool inventory remain part of the unchanged full goal.
