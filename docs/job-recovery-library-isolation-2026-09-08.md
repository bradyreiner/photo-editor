# Job recovery does not make a healthy Library unavailable

## Defect and change

Startup routed a job-recovery failure through the catalog startup failure
channel, and only refreshed Library assets after successful job recovery. A
preserved legacy/catalog history conflict therefore hid otherwise readable
photos and offered catalog repair for a job-specific problem.

Library refresh now starts after successful catalog bootstrap independently of
the runner's recovery barrier. Dispatch remains paused until recovery succeeds.
`JobQueueModel.startupRecoveryError` is separate from history-read errors and
normal queue status; successful history refresh cannot erase it. Identical
automatic recovery failures do not repeatedly reload job history.

The recovery panel identifies paused jobs, explains that Library remains usable,
and routes Review jobs to Export. It does not expose catalog repair or a Done
action for unresolved job recovery. Genuine catalog outages retain their own
recovery/repair surface and take precedence.

Native visual inspection caught a second defect: first-run onboarding covered
the recovery panel after navigation. Onboarding now defers while recovery is
visible, without marking the guide skipped or complete. The proof waits for
settled navigation and verifies onboarding is hidden and still pending.

## Verification

- Six focused tests passed (23.87 s): `job_recovery_status_test`,
  `job_store_concurrency_test`, `job_runner_startup_test`,
  `job_runner_scan_test`, `job_runner_wake_test`, and
  `job_runner_cancellation_test`. Log:
  `/private/tmp/job-recovery-isolation-tests.log`.
- Desktop build passed. Final log:
  `/private/tmp/job-recovery-isolation-accepted-build.log`; no compiler
  warning/error diagnostics found there. This is not a clean whole-app QML
  runtime warning claim.
- Real isolated PostgreSQL conflict fixture plus a referenced photo:
  `/private/tmp/job-store-real-ui-final-iMCqtn`.
  `receipt.json` reports `jobRecoveryIsolationProof.ok=true`, one Library asset,
  three readable jobs, no catalog error, hidden catalog repair/Done, successful
  Review jobs navigation, settled navigation, and deferred/pending onboarding.
  Visually inspected `job-recovery-library.png` and `startup.png` show the photo
  and unobscured job-recovery panel respectively.
- `jobs-before.json` and `jobs-after.json` are byte-identical. The saved SHA
  check for `runtime/jobs/index.json` passes. Neither version was discarded.
- Genuine catalog-outage native proof:
  `/private/tmp/job-recovery-outage-mlQ4lO`. Receipt passes and inspected screenshot
  shows Catalog recovery, the proper repair controls, and unavailable job
  history rather than incorrectly claiming the Library is available.
- Normal trained-Scene to catalog import to Edit regression:
  `/private/tmp/job-recovery-scene-w6c64o`. Receipt, foreground/exposed state,
  final mask outline, saved mask/color edit, neutral fidelity and three undos
  pass. All four PNG outputs match `/private/tmp/job-import-native-qNC9Zv`
  byte-for-byte. Inspected Edit screenshot has no recovery/onboarding overlay.
  The existing comparison script's `read_proof` fidelity checks were reused;
  its performance promotion gates were not applied or claimed.

The earlier `/private/tmp/job-store-real-ui-zycQk5` receipt passed its original
checks but its Export screenshot was covered by onboarding/transition overlays.
It is retained as diagnostic evidence, not accepted final visual proof.

Both conflict-test servers (ports 52455 and 53472) and the Scene regression
server (58312) were stopped after exact test data-directory ownership checks;
post-stop status reports unhealthy/stopped. Data and artifacts are retained.
No user catalog, originals, or model weights were changed.

## Remaining boundaries

Review jobs opens existing history; it is not a version comparison/resolution
workflow. Explicit conflict reconciliation, stale ordinary worker writes,
deletion/recreation protection, and safe mutation during unresolved recovery
remain open. The native logs also expose Export preflight delegate
`modelData`/undefined-property warnings and shutdown warnings, not repaired here.

The normal Scene proof still records a 1,629 ms maximum whole-proof event-loop
gap; this work does not establish realtime behavior. Large-catalog loading,
independent RAW/color calibration, full 3D/4D capture quality, packaged model
inference, and the professional editing-tool inventory remain part of the
active goal, not completed by this recovery fix.
