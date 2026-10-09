# Recipe checkpoint serialization verification — 2026-09-28

## Result and limits

The second implementation attempt passed this isolated comparison's gates.
With a fixed 4 MiB raster recipe in rejected recovery and a changing small pending
edit, conservative checkpoint p95 decreased **31.484 → 19.537 ms (37.9%)**.
Peak process RSS increased **224,821,248 → 243,548,160 bytes (8.3%)**.
This is synthetic journal performance, not native application frame latency.
Full recipe JSON construction, payload hashing, and whole-file writes remain;
finding 7 is not complete.

## Method

Artifact directory: `/private/tmp/looksy-checkpoint-comparison-20260928/`.
The reference executable reconstructs the prior v3 writer body in an isolated
source file, using the same current headers, codec, libraries, build flags, and
benchmark fixture as the candidate. It retains the unused cache member in the
class layout. It is not a historical production binary or a clean Git checkout.
`journal-reference.cpp`, its object/build logs, and `accepted-source/manifest.json`
record the inputs. Each run records the executable SHA-256 in `runs.json`.

Each scenario performs one cold write and 24 measured writes, changing a real edit
on every iteration. The added `4mib_rejected` case keeps its raster-heavy rejected
record unchanged while changing an unrelated small pending edit. Each run closes
and reopens the journal and checks exact serialized recipes for both pending and
rejected records. All files are synthetic and reside in temporary directories.
No live catalog, user recovery journal, photos, or UI was opened.

Two alternating pairs ran per attempt: reference, candidate, candidate, reference.
Tables compare the faster reference p95 with the slower candidate p95. RSS compares
the smaller reference peak with the larger candidate peak. Measurements ran on the
host because the sandbox denied `/usr/bin/time -l` access to `kern.clockrate`;
the initial sandbox run is retained separately and excluded from acceptance.

Gates were recorded in `method.md` before the comparisons: at least 20% lower p95
for the rejected-record case; at most 10% regression for large changed-edit cases;
at most 1 ms regression for small recipes; at most 20% peak-RSS increase; exact
reopen in every case. Maximum three implementation attempts; two were used.

## Attempts

Attempt 1 improved the target case by 33.2%, but the 4 MiB changed-tone case
regressed 10.7%, exceeding the guard. It was not accepted. Its evidence is under
`attempt-1/`.

Attempt 2 adds direct whole-payload serialization when only pending edits exist,
avoiding intermediate per-record byte buffers on that common path. Checkpoints
with active/rejected/migration-retry records retain exact-value serialization
reuse. Both paths write envelope chunks through one atomic `QSaveFile` commit.
Evidence: `attempt-2/comparison.json`, four run JSON files, four `.time` files,
and `attempt-2/runs.json`.

| Scenario | Reference p95 | Candidate p95 | Change |
| --- | ---: | ---: | ---: |
| Small changed tone | 0.389 ms | 0.383 ms | -1.5% |
| 1 MiB changed tone | 7.799 ms | 7.721 ms | -1.0% |
| 1 MiB changed brush | 7.113 ms | 7.532 ms | +5.9% |
| 4 MiB changed tone | 31.548 ms | 33.161 ms | +5.1% |
| 4 MiB rejected record + small changed edit | 31.484 ms | 19.537 ms | -37.9% |

Every scenario's exact-reopen check passed in all four accepted runs.
The measured regressions and higher memory are retained in this report; the
optimization is most useful when large records remain unchanged across writes.

## Correctness coverage

The current and portable SHA-256 journal suites both passed after the final change:
`/private/tmp/looksy-performance-fix-7-attempt2-tests.log` (2/2).
The tests now compare complete bytes against an independent QJsonDocument-based
v3 envelope, including Unicode/quotes/backslashes/NUL, repeated cache use, a
sub-tolerance numeric change, changed file paths and migration evidence, active
to migration-retry transitions, rejected-record preservation, failed writes and
retry, exact reopening, and empty-checkpoint recovery. Existing killed-writer,
authority isolation, exclusive lock, corruption retention, and supported recipe
version checks also ran.

The legacy fixture was corrected to remove the v3-only rejected field before
constructing a v1/v2 payload. Both old-format upgrades are now checked; the reader
was not weakened to accept a mislabeled v3 fixture.

Remaining acceptance: cache-budget and 16 MiB scenarios, live controller/UI timing,
real rejected-recipe recovery, and a broader durable incremental-storage design.
These results do not establish the entire performance goal or all recovery paths.
