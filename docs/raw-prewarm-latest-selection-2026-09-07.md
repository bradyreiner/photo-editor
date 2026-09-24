# Latest-selection RAW decode prewarming

## Defect and scope

The previous process-wide prewarm helper deduplicated identical keys in a
`QSet`, but submitted a separate low-priority pool task for every distinct RAW
selection. It supplied no cancellation token to
`prewarm_source_image_decode_pyramid`. Rapid browsing could therefore accumulate
obsolete decode work and retained recipe snapshots. Selecting a JPEG or an
already-resident RAW returned without canceling that work.

`RawDecodePrewarmQueue` replaces that scheduling policy. It permits one active
speculative task and one latest pending replacement. New keys cancel the active
task cooperatively and replace the pending task before it reaches the thread
pool. Identical active/pending keys deduplicate; A -> B -> A correctly schedules
a fresh A after the first A was canceled. JPEG and already-resident RAW selection
cancel speculative RAW work.

The worker passes its cancellation token through the existing pyramid builder
and decoder. LibRaw's existing progress callback and stage checks consume that
token. There is no decoder-quality, color-math, recipe or foreground-preview
change, and no JPEG/embedded-camera-image substitute for editable RAW pixels.

The queue remains process-wide because prewarming and the decoded image cache
are shared. The latest request wins across controllers only for this optional
speculation; their normal rendering requests are unchanged. The bound is on
task/recipe count, not a new whole-application RSS ceiling.

## Lifetime and failure behavior

One low-priority pool closure drains the latest pending job. It owns shared queue
state rather than a controller QObject. Cancel and destruction discard pending
payloads and signal active cancellation without joining a worker. Decoder
exceptions cannot escape the worker or strand subsequent requests. A synchronous
pool-submission exception clears the scheduled flag and records failure.

The queue does not forcibly interrupt LibRaw header opening, arbitrary decoder
calls or filesystem operations. Global thread-pool shutdown can still wait for
uninterruptible work. This is not a complete application shutdown guarantee.

## Executable proof

`raw_decode_prewarm_queue_test` holds one active callback, submits 100 newer keys
and a duplicate of the last key, and verifies:

- Exactly one task remains pending; 99 pending recipes are replaced.
- The first callback sees cancellation; only replacement 99 executes.
- Two callbacks start overall, including the deliberately held callback.
- Cancel before pool dispatch executes none of the 100 queued requests.
- Returning to a canceled key, exception recovery, active destruction and
  immediate release of a destroyed queue's pending payload pass.

Log: `/private/tmp/raw-prewarm-queue-test.log`.

The real RAW variant uses the Nexus 6 DNG for the final replacement callback;
it successfully populates both 1600-edge and 768-edge decoded-cache entries.
The held first callback is a scheduling fixture, not a forced pause inside
LibRaw. Receipt: `/private/tmp/raw-prewarm-queue-real-raw.json`.

The real controller test at 4208x3120 passes all twelve cache-hit pixel checks,
asynchronous admission and latest-selection publication checks. Its twelve
ordered RGBA hashes exactly match the pre-change receipt
`/private/tmp/warm-loading-raw-after.json`. Current receipt:
`/private/tmp/raw-prewarm-controller-raw.json`. This is an output/behavior check,
not a paired performance benchmark or a measured browsing FPS improvement.

Seven regressions pass in 3.39s: queue policy/lifetime, warm loading/retry,
corrupt-cache recovery, future drain, preview math, and legacy/ACES2 reference
workflows. Log: `/private/tmp/raw-prewarm-regressions.log`.

The native editing smoke passes with a RAW base and JPEG source layer: QML
placement, preview publication and undo/redo succeed. Its isolated-profile
receipt is `/private/tmp/raw-prewarm-app-QKXJjA/receipt.json`; the screenshot was
visually inspected. The unavailable-catalog banner is expected for this test
profile, not a finding about the user's catalog. No model weights were fetched.

App/driver builds pass; changed C++ compiles without warnings. Logs:
`/private/tmp/raw-prewarm-queue-build.log` and
`/private/tmp/raw-prewarm-regression-build.log`.

## Reproduce and remaining goal

```sh
build/release-macos/tests/raw_decode_prewarm_queue_test
build/release-macos/tests/raw_decode_prewarm_queue_test SOURCE_RAW NEW_RECEIPT.json
```

The optional receipt path must not exist. The test uses an isolated temporary
cache and does not modify the input RAW.

Cold selection still performs synchronous recipe restoration, RAW metadata
opening and cache/header checks. Foreground latency, diverse-camera color truth,
broader memory pressure, Gaussian 4D/film, embedded inference/search and the full
professional editing inventory remain open. The complete app goal remains active.
