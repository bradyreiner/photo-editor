# Recipe checkpoint performance: measured, format-compatible recovery

## Scope and gates

This is a bounded optimization of the synchronous local checkpoint introduced in
`recipe-crash-recovery-2026-09-07.md`. It does not defer durability, change catalog
authority, weaken checksums, alter recipe semantics, or change a color transform.

The performance-loop gates were: at most three attempts; at least 50% less p95
checkpoint time for large recipes; small-recipe p95 no more than 1 ms worse;
at most 20% peak-RSS growth; and exact recipe recovery plus the existing real
JPEG/RAW crash/retry/canonical-pixel tests. Optimization stopped after two attempts
when the timing and memory gates passed. Correctness validation remains separate
from timing eligibility.

`editor_recipe_save_benchmark` writes real atomic checkpoint files in fresh
temporary directories. Each scenario has one cold write and 24 measured writes.
Every measured write changes exposure; the brush scenario also changes alpha
bytes. Raster fixtures use deterministic high-entropy 1 MiB/4 MiB alpha arrays,
are normalized through the recipe codec, and have a minimum encoded-size check.
Fresh journal reopen must produce exactly the final serialized recipe. No GUI,
catalog server, model runtime or photographic renderer runs in this benchmark.

## Baseline and diagnosis

Before artifacts: `/private/tmp/framehouse-checkpoint-baseline-ZLGLIH/`.
Two unprofiled runs contain `run-1.json`, `run-2.json` and matching `.time` files
from macOS `/usr/bin/time -l`. Separate profiled runs split the write stages;
those runs are diagnostic and are not substituted for the unprofiled comparisons.

For a roughly 2.8 MB checkpoint, split profiling measured about 7.5 ms encoding,
2.5 ms payload serialization, 11.1 ms SHA-256, 2.3 ms envelope serialization and
0.7 ms atomic writing in that diagnostic run. Filesystem timings vary. The data
identified hashing and repeated serialization, not a need to postpone the save.

## Changes

1. On macOS, checkpoint payload SHA-256 uses CommonCrypto, reducing the measured
   digest stage from about 11.1 ms to 0.85 ms. Other platforms retain Qt SHA-256;
   a separately compiled portable-path test exercises it on this host too.
   Namespace/authority identity and SHA-256 output are unchanged.
2. Outbox v2 stores the recipe as a JSON object instead of serialized JSON inside
   a JSON string. It builds the existing recipe object directly. The already
   encoded payload and ASCII hex digest are embedded once in the final envelope,
   avoiding another traversal of all raster strings. Integrity still covers the
   entire canonical JSON payload. The atomic write, owner permissions and
   direct-write-fallback prohibition are unchanged.

The reader accepts v1 and v2 outboxes, requires the current recipe version and
exact object roundtrip, and rejects malformed/unsupported formats without
rewriting them. A v1 checkpoint is rewritten as v2 only on a normal successful
atomic checkpoint. Older application builds that only understand v1 cannot read
v2 pending state; use the current build for recovery before downgrading.

The first v2 test failed closed because the adjustment-object API deliberately
rejects a `recipeVersion` field. The corrected reader validates that version at
the outbox boundary, then calls the unversioned adjustment parser and verifies
the complete rebuilt recipe object. The failed run at
`/private/tmp/framehouse-checkpoint-candidate2-XRFzWa/` is not accepted evidence.

## Accepted comparison

Candidate 1 (digest only):
`/private/tmp/framehouse-checkpoint-candidate1-4b0HIy/`.
Final candidate:
`/private/tmp/framehouse-checkpoint-candidate2-fixed-cy64AY/`.

The comparison below deliberately uses the fastest baseline p95 and slowest
candidate p95 across the two runs, not cherry-picked matching trials.

| Changed-edit scenario | Before p95 | After p95 | Reduction |
| --- | ---: | ---: | ---: |
| Small tone recipe | 0.428 ms | 0.384 ms | 10.3% |
| Tone edit with 1 MiB raster mask | 30.310 ms | 8.628 ms | 71.5% |
| Brush edit with 1 MiB raster mask | 27.906 ms | 8.759 ms | 68.6% |
| Tone edit with 4 MiB raster mask | 110.914 ms | 37.651 ms | 66.1% |

Peak process RSS in the same benchmark decreased from at least 541,622,272 bytes
to at most 202,981,376 bytes (62.5% lower). This is the isolated benchmark's peak,
not whole-app photographic rendering memory. All four fresh-reopen equality
checks pass in every accepted run. All large scenarios clear the 50% gate,
small-recipe timing improves, and the memory guard passes.

Reproduce after building `editor_recipe_save_benchmark`:

```sh
/usr/bin/time -l build/release-macos/tests/editor_recipe_save_benchmark
PHOTO_EDITOR_PROFILE_RECIPE_CHECKPOINTS=1 build/release-macos/tests/editor_recipe_save_benchmark
```

The optional profile switch emits only per-stage timing JSON, not recipes,
filenames, credentials or image data. Leave it unset for timing comparisons.

## Correctness verification

- Seven focused tests pass in 16.08 seconds:
  `/private/tmp/recipe-checkpoint-final-tests.log`. These cover queue ordering,
  killed-writer recovery, current/portable digest paths, exact Qt envelope/digest
  compatibility, v1-to-v2 recovery, Unicode/escaped large evidence, corruption
  rejection, write failure retention, save-status QML, stdin transport, canonical
  persistence and linked-source relink.
- Large JPEG PostgreSQL crash/retry/migration/pixel proof passes:
  `/private/tmp/recipe-checkpoint-v2-jpeg.log`. Actual controller checkpoint writes
  measured 7, 6 and 7 ms for 2,812,639 bytes. Private port 56245 was stopped and
  independently verified closed.
- The same real DNG/JPEG/linked-RAW and large-mask proof passes:
  `/private/tmp/recipe-checkpoint-v2-raw.log`. Three actual controller checkpoints
  each measured 7 ms for 2,812,741 bytes (the prior format measured 25–26 ms).
  Kill-before-debounce recovery, two canonical reopens, exact state/render pixels,
  injected database failure/retry and migration replay safeguards pass. Private
  port 56536 stopped and independently reported Connection refused.
- App and test builds pass without compiler warnings/errors in
  `/private/tmp/recipe-checkpoint-final-build.log`; scoped `git diff --check`
  passes.
- The packaged-app check passes in 21,353 ms at
  `/private/tmp/framehouse-checkpoint-v2-native-rp9AyH/receipt.json`. It renders,
  exports and undo/redoes the placed RAW layer while the deliberately unavailable
  private catalog leaves a visible `Saving edits` chip. The screenshot was
  visually inspected; the post-exit outbox is v2, contains the pending asset and
  stores its recipe as an object. The private model runtime stopped, relinquished
  ownership and independently closed port 56894. The existing shutdown warning
  about a running save helper was not fixed by this optimization; the subsequent
  `editor-close-coordination-2026-09-07.md` records that fix and its proof.
  All window samples were inactive, and a fresh read-only
  macOS session check still reported screen lock. This is runtime correctness,
  not foreground interaction evidence.

## Remaining limits

These are synchronous checkpoint measurements, not input-to-photon or complete
slider/brush latency. Larger recipes still require work; 4 MiB raster checkpoints
can take about 38 ms at p95. Incremental/content-addressed raster storage, bounded
large-document memory, multiwriter conflict detection and recovery repair remain
open. Editor checkpoint close coordination is addressed in the subsequent
`editor-close-coordination-2026-09-07.md`; universal background-job shutdown is
not. The previously documented full
RAW/color, Gaussian/4D, embedded-model and professional-editing completion gates
are unchanged. This optimization does not establish foreground realtime rendering.
