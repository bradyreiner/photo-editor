# RAW output and catalog readiness performance — September 10, 2026

The legacy display/export batch rebuilt its color-transform matrix for each
pixel. Out-of-gamut pixels also rebuilt the output color context. The batch now
compiles those constants once and processes pixels with the existing bounded
engine scheduler. The scalar transform remains an independent reference; the
per-pixel operations and camera-LUT fidelity fallback are unchanged.

## Matched native RAW measurement

Release build, native Metal texture publication, the same local CR2 and recovered
5.7 MB legacy recipe, and a global curve edit. The driver uses temporary settings
and cache directories, disables persistence, and does not write to the catalog.
It checks the draft against a matched-resolution reference and the native full
preview against the exported image.

| Measurement | Before | Accepted output batch |
| --- | ---: | ---: |
| Full curve-edit render | 6,278 ms | 4,723 ms |
| Display conversion within that render | 2,118 ms | 531 ms |
| Full photo-selection render | 8,387 ms | 6,652 ms |
| First curve feedback render | 207 ms | 201 ms |
| Peak process RSS | 5,377,818,624 B | 5,405,458,432 B |

Full edit rendering improves **24.77%**, display conversion **74.93%**, and full
photo-selection rendering **20.69%**. Peak RSS increases **0.51%**. These are
individual host observations, not corpus percentiles or input-to-photon latency.
First feedback still exceeds the 100 ms budget on this large legacy recipe.

All three generated PNGs (draft, matched reference, full) are byte-identical to
the baseline. Draft/reference maximum channel difference remains 0/255 and
native full/export difference remains 1/255.

## Bounded decision and rejected experiment

The loop allowed at most three code attempts. Acceptance requires at least 20%
less full edit render time, no more than 10% regression in full selection, first
edit feedback or peak RSS, successful parity checks, and identical output files.
The first attempt passes all gates.

A subsequent sampling profile identified selection-mask rasterization as a large
remaining cost. Parallelizing its independent rows preserved exact alpha bytes
in 30 cases, including nested worker calls, but missed the performance gate in
two native runs: full edit time was 9,393 and 8,963 ms. Unchanged stages also slowed
and substantial competing host CPU load was observed. That experiment was
reverted exactly; it is not shipped as an optimization. Final launch verification
then exposed a separate readiness bottleneck, addressed in the third attempt
below. No further optimization attempts were made.

## Reproduction and evidence

Private local evidence is under `/private/tmp/framehouse-perf-next/`:

- `command.json` records the exact source/recipe/driver invocation.
- `baseline.log`, `candidate-1.log`, their `*-images/` directories, and
  `gate-1.json` hold the accepted comparison.
- `gate.py baseline candidate-1` performs the numerical and file-equality gates.
- `render.sample.txt` records the mask investigation; `candidate-2*.log` and the
  saved rejected test document the reverted experiment.

Run the benchmark command from the repository with
`PHOTO_EDITOR_BENCHMARK_NATIVE_PREVIEW=1` and wrap it in `/usr/bin/time -l` for
host memory measurements. Use a fresh output directory for each run. The source
photo and recovered recipe are private fixtures and are not checked in.

The permanent `color_output_batch_test` compares 150,912 RGBA outputs bit for bit
against the scalar transform across working/output profiles, gamut policies,
render intents, headroom, black-point compensation and chromatic adaptation.
It includes signed/HDR pixels, alpha outside [0, 1], small batches and empty input.

## Final verification and limits

The Release app and focused test targets built successfully. Five checks passed:
`color_pipeline_test`, `color_output_batch_test`, `color_curve_headroom_test`,
`render_parity_test` with host Metal access, and `mask_thumbnail_color_test`.
The final RAW output was also visually inspected.

`editor_preview_feedback_test` failed its immediate mask-feedback check, which
includes a 33 ms admission budget. Its isolated repeat subsequently timed out at
126 seconds. This remains unresolved; neither its threshold nor its assertions
were weakened. The accepted color batch has exact pixel and focused pipeline
proof, but these results do not establish app-wide responsiveness under load.
See `final-build.log`, `final-tests.log` and `feedback-repeat.log` in the evidence
directory. The live save outbox was empty and backed up before relaunch.

## Startup readiness repair

The ordinary launcher initially failed its managed-catalog startup probe twice. The server
was independently verified running at the stored data directory and port,
`pg_isready` accepted connections, and an authenticated `SELECT 1` succeeded
(743 ms including the client process). The helper uses a 750 ms connection
timeout and incorrectly attempts a second server start when that check fails.
No server was stopped or catalog reset. For an intermediate run, the launcher was
given `PHOTO_EDITOR_DATABASE_URL` constructed from the same managed state; its
normal migration check then succeeded and it launched the rebuilt Release app.
This selected the same database and credentials, not a new catalog.

That exposed a second readiness failure: `main.cpp` used `catalog_export_jobs
list` to check connectivity with a five-second deadline. Serializing the existing
7.56 MB job took 21.7 seconds in the diagnostic run, preventing Library admission.
The third candidate adds `catalog_export_jobs health`, checks its small JSON
response on the existing background worker, and leaves full history reads to
job recovery after admission. The managed helper now checks `pg_ctl status`
after a failed quick SQL probe and does not start a second live postmaster;
the normal catalog connection must still validate credentials and readiness.
Neither startup nor UI timing limits were increased.

The matched readiness comparison passed the 20% latency and 10% memory gates:

| Measurement | Previous history probe | Readiness query |
| --- | ---: | ---: |
| Elapsed | 7,056 ms | 2,595 ms |
| Peak RSS | 47,628,288 B | 11,288,576 B |
| Response size | 7,563,996 B | 15 B |

Readiness time falls 63.22% and memory falls 76.30%. The original job-history
payload is byte-identical. Evidence: `measure-readiness.py`, `readiness-gate.json`
and `readiness-{list,health}.{json,log}`. These are individual host observations
with other system activity, not latency percentiles.

`cargo test --workspace --locked` passed. The new crate integration test confirms
that `health` rejects invalid authority without reporting ready or creating a
replacement catalog. `app_bootstrap_test` passed. `job_runner_startup_test`
reported immediate recovery admission (0 ms), but its heartbeat assertion failed
with only nine ticks. That and the preview-feedback timing failure above remain
visible limitations; their assertions were not relaxed. See `rust-tests.log`,
`readiness-test.log` and `startup-tests.log`.

The final **normal** `./scripts/run-native.sh --no-build` launch succeeded without
a database URL override. The existing PostgreSQL service and catalog were
retained, Library thumbnail work started, and the outbox had zero pending or
active saves. `relaunch-final.log` records the run. Startup was still slow under
the observed host load (about 20 seconds in the launcher and 18.7 seconds in the
app's managed bootstrap), and thumbnail queue waits reached 14.6 seconds. Those
remaining stalls are not claimed fixed by the measured output/readiness gains.
