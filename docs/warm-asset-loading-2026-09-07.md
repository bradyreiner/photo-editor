# Asynchronous warm asset loading

## Defect and implementation

Warm asset selection and preview retry bypass debounce when a fresh full-preview
disk cache exists. Previously `startRenderStage` then called the entire preview
renderer inline on the GUI thread. Even a hit reads/decodes the cached JPEG and
prepares its full-resolution image; a vanished cache can fall through into a
render. The real RAW baseline blocked inside `loadAsset` for a median 122ms.

The inline branch is removed. Warm selection and retry still dispatch
immediately, skip the draft/debounce path, and reuse the same disk cache, but the
renderer runs through the existing QtConcurrent worker and generation-checked
publication path. Cache encoding, decode settings, recipe identities, color
transforms and pixel math are unchanged. No new render queue or controller wait
is introduced.

This does not remove all synchronous work from `loadAsset`: recipe restoration,
RAW metadata lookup, cache-header checks and state/publication bookkeeping still
need their own cold/large-recipe/slow-storage evidence.

## Accepted bounded loop

One candidate, within the three-attempt ceiling. Gates established before the
production change: improve median warm-selection admission time at least 15%;
keep median and p95 preview-ready time within 20%, peak RSS within 10%, and all
twelve ordered cached-image hashes identical. The changed run must also pass
the held-queue/latest-selection check.

Both real fixtures are 4208x3120. Each host process uses an isolated temporary
runtime/cache, seeds the cache, establishes the warm decoded image, then performs
twelve warm selections. Image hashing is outside the timed section. Admission
measures the synchronous `loadAsset` call; ready time includes that call and
processing events until the preview controller settles. This is a controller
latency measure, not display input-to-photon or FPS. Host `/usr/bin/time -l`
captures peak RSS, including initial cold seeding.

| Metric | RAW before | RAW after | JPEG before | JPEG after |
| --- | ---: | ---: | ---: | ---: |
| Admission median | 121.68ms | 2.37ms | 60.24ms | 0.86ms |
| Preview-ready median | 121.97ms | 125.20ms | 60.43ms | 60.26ms |
| Preview-ready p95 | 127.92ms | 125.98ms | 64.60ms | 63.64ms |
| Peak RSS bytes | 2,369,470,464 | 2,366,652,416 | 1,921,433,600 | 1,921,761,280 |

Admission improves **98.05% RAW / 98.57% JPEG**. All gates pass. The same ordered
RGBA hashes are preserved across the change; each run also checks the cached
image's color-space tag stays unchanged. This is not an independent colorimetric
reference or a lossless-cache claim: the existing disk cache uses JPEG.

Artifacts:

- `/private/tmp/warm-loading-raw-before.json` and `warm-loading-raw-after.json`
- `/private/tmp/warm-loading-jpeg-before.json` and `warm-loading-jpeg-after.json`
- Corresponding `.log` files hold host memory/process measurements.
- `/private/tmp/warm-loading-raw-comparison.json`
- `/private/tmp/warm-loading-jpeg-comparison.json`

`scripts/perf/compare-warm-asset-loading.py` validates sample counts, timings,
cache hits, hashes, dimensions, memory and the gates. After-to-itself comparison
correctly exits 1 for zero improvement.

## Executable regression

`editor_warm_asset_loading_test` covers cold cache seeding and twelve repeated
warm loads, then holds the worker queue while selecting the first image and
replacing it with a second image. During the 100ms hold, GUI event processing
continues, no image may publish, and no disk hit may complete inline. After
release, only the second image may publish.

Before the fix the queue test reports `asyncPassed: false`,
`queuedWhileHeld: false`, `noEarlyPublication: false`, and `newestOnly: false`.
After the fix all four are true for the synthetic, real RAW and real JPEG runs.
Baseline mode records the failure without changing production behavior.

The default synthetic regression additionally covers a completed old worker
whose notification is still queued when the user switches images, and retry
after evicting the test-only cache between scheduling and worker execution.
These checks are not included in the paired host benchmark measurements.

Final CTest run: **7/7 pass in 5.46s** (warm loading, future drain, preview math,
before/after recipe invariance, color cache identity, and legacy/ACES2 reference
workflows). Log: `/private/tmp/warm-loading-final-tests.log`. App and regression
builds pass; changed C++ builds without warnings. Logs:
`/private/tmp/warm-loading-candidate-build.log` and
`/private/tmp/warm-loading-regression-build.log`.

Native app smoke: `/private/tmp/warm-loading-app-nODlXt/receipt.json` reports
`ok: true`, isolated catalog, successful JPEG placement over RAW through QML,
preview publication and undo/redo, and no model download. The screenshot was
visually inspected. Its catalog-recovery banner is expected for the deliberately
unavailable-catalog profile, not a finding about the user's catalog. This smoke
is an integration check, not foreground performance measurement.

## Reproduce

Fixtures live under
`/private/tmp/framehouse-six-raw-source-v3/hdrplus-nexus6-skin-0218/`:
`hdrplus-nexus6-skin-0218.dng` and `hdrplus-nexus6-skin-0218-reference.jpg`.

```sh
QT_QPA_PLATFORM=offscreen /usr/bin/time -l \
  build/release-macos/tests/editor_warm_asset_loading_test \
  SOURCE_IMAGE NEW_RECEIPT.json > NEW_RECEIPT.log 2>&1
python3 -B scripts/perf/compare-warm-asset-loading.py BEFORE.json AFTER.json
```

The receipt path must not already exist. Before-patch runs use the test-only
`--baseline` flag to permit recording the expected asynchronous-contract failure;
this flag does not alter controller behavior. Use serial host runs for comparable
memory/timing measurements. Running without paths uses temporary synthetic images
and additionally exercises the recovery cases.

## Remaining scope

This repairs warm-load UI blocking; it does not accelerate cached JPEG decoding
itself. End-to-end warm preview readiness remains about 125ms RAW / 60ms JPEG in
these runs. Cold import/decode, heavy recipe admission, real catalog switching,
native interaction frame timing, all-camera color truth, Gaussian 4D/film,
managed embedded inference/search and the full professional editing inventory
remain open. The broad completion goal remains active.
