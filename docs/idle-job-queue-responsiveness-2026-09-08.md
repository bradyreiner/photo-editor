# Idle job queue responsiveness

This is verified progress on loading, not completion of the active app goal.
The diagnostic sample at `/private/tmp/scene-mask-sample-jrC1Ig/app-sample.txt`
shows foreground job-store helpers in queue scans, startup recovery and job
commits. Each can also resolve the managed database URL synchronously.
This change removes only the idle queue observation from that foreground path.

## Bounded loop and implementation

The performance-loop skill was used with at most three candidates. The target
was admission below 100 ms and at least 20 UI heartbeat ticks during a 300 ms
window with a helper deliberately delayed by 250 ms. The first candidate passes.

`JobRunner` now submits one read-only queue observation to a worker, with one
coalesced follow-up for notifications received during that read. A superseded
snapshot is discarded before dispatch. Invalid/failed reads cannot dispatch
legacy fallback work; a one-second retry observes recovery without another user
action. Workers retain no runner pointer, and destruction does not wait for them.
Existing priority selection and per-job start revalidation remain unchanged.
Startup recovery still owns legacy migration; background scans do not migrate.

## Measurements and native correctness

The baseline test output is `/private/tmp/job-scan-baseline.log`: 464 ms admission
and one delayed heartbeat. Candidate observations return below the millisecond
timer resolution, with 28 and 29 ticks. This measures controller admission,
not input-to-photon latency or faster database completion.

The matched native debug-app proof uses the real 753,038-splat static project
and a fresh persistent catalog: `/private/tmp/job-scan-native-ylv8v1`.
All recorded samples are active/exposed. Comparison against the preceding
accepted `/private/tmp/scene-mask-accepted-YI9HYp` proof gives:

| Metric | Before | Candidate |
| --- | ---: | ---: |
| Whole proof | 16,256 ms | 16,003 ms |
| Peak footprint | 2,124,089,408 bytes | 2,120,829,040 bytes |
| Add mask / masked exposure admission | 74 / 53 ms | 70 / 54 ms |
| Maximum observation-only timer gap | 1,466 ms | 1,196 ms |

The predeclared native guards (exact images, save/undo/final outline, matching
foreground state, at most 10% time/memory regression) pass; `comparison.json`
retains both measurements and gate results. All four exported PNG files are
byte-identical. Neutral pixels/profile, masked exposure/P3 catalog save and
three-step undo pass. `scene-edit-mask.png` was visually inspected; it is not
proof of pointer/stylus interaction or visible marching-ants behavior.

The whole-proof maximum is 1,560 ms, but its phase is `after-capture`: the
harness's synchronous local-AI self-check takes 1,362 ms. The preceding 1,565 ms
maximum has the same attribution. Neither should be presented as an editing
action's latency. The observation-only stalls remain real, and the small overall
runtime change is not an app-wide realtime claim.

Reproduce the focused test with `job_runner_scan_test`; its temporary helper
captures response/status before sleeping so supersession is exercised. It also
covers malformed history, outage retry, read-only legacy fallback, an obsolete
queued snapshot, 101-to-2 read coalescing, and nonblocking destruction.
The native command is documented in `scene-photo-edit-handoff-2026-09-08.md`;
always use a fresh proof directory and the same trained static project.

## Verification boundary

The app and focused targets build without warning/error diagnostics. The three
runner tests pass twice. The broader nine-test run passes six, including the
runner trio, timeline recovery, photo-export recovery and Scene-to-Edit handoff.
`job_queue_model_test` fails its post-resume export summary assertion;
`export_job_loading_test` and `job_queue_loading_test` fail their initial outage
checks. These tests do not instantiate the changed `JobRunner`; they are not
counted as passing. Logs: `/private/tmp/job-scan-final-regressions.log`.
Added failure-only diagnostics without changing assertions. The two outage
checks report `loading=0`, a catalog-service error, and zero heartbeat ticks;
their minimum-tick gate depends on the duration of an uncontrolled real failure.
Those results remain failures, not evidence of passing UI responsiveness.
See `/private/tmp/job-scan-outage-diagnostics.log`. The model summary test's
post-resume count failure still needs diagnosis. A later source check corrected
the initial asynchronous-reload hypothesis: `resumeJob` calls a synchronous
`reloadJobs`, so that hypothesis does not explain this failure.

The proof-owned PostgreSQL server on port 57717 was stopped after verifying its
exact temporary data directory; post-stop status reports unhealthy/stopped.
Its files and receipts remain. No user catalog was modified.

Next loading work is startup recovery and job start/completion persistence,
followed by catalog publication and photo opening. Those synchronous paths
remain. Diverse-camera RAW/color calibration, genuine temporal reconstruction,
actual embedded-model weights/quality, and the professional editing-tool
inventory remain separate completion requirements.
