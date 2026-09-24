# Lossless display-cache recovery

## Finding

The broad controller retry fixture initially exposed a missing image-provider
preview. Native-only publication was a possible explanation, but the failure
persisted with native delivery explicitly disabled. Focused cold and warm tests
then separated two actual defects, using real Metal readback and CPU provider
pixels rather than the existence of one particular representation.

Warm-cache selection-start changed mask/layer revisions after the sole cached
full render was scheduled. That result was correctly rejected as stale, but
the lightweight tool-start path scheduled no replacement. Both native/provider
paths settled without usable pixels. Reproductions:
/private/tmp/initial-selection-warm-native-before.log and
/private/tmp/initial-selection-warm-provider-before.log.

Selection-start now refreshes queued/in-flight photograph work or an initial
missing preview with a mask-commit request. Already-loaded, idle tool-start
retains its lightweight behavior. Stale-result rejection is not relaxed.

The broad retry fixture now explicitly tests provider delivery; separate native
tests cover actual textures. Both require initial publication, injected failure,
last-good pixel/tool preservation, retry, cleared errors and current full quality.

The other defect was fidelity: the disk preview cache was JPEG.
A no-edit retry preserved dimensions and tool/error state but changed channel
values by up to 4/255 on the generated gradient fixture. Both native/provider
recovery probes failed their exact pixel check before the cache change. Evidence:
/private/tmp/initial-selection-native-diagnostics.log and
/private/tmp/initial-selection-provider-before.log.

## Implementation

The display-image cache and comparison-reference cache now use atomic lossless
PNG writes, with a new pipeline-v12-lossless-display-cache identity and .png
extension. Old JPEG caches are not read under this identity and are not deleted.
ICC tagging and cancellation-before-commit remain intact. Header scheduling and
worker decoding require PNG, expected dimensions, and successful full decode.

Telemetry reports disk_png_cache and png_encode_requested. The legacy
jpeg_encode_requested field remains false for compatibility. The repeat-cache
performance gate now checks the new path and no encoding on a hit; its latency
thresholds were not relaxed. Historical JPEG artifacts remain historical proof,
not validation of the new format's performance.

This changes the existing display-image cache, whose render calls request
ARGB32. RAW decoding, working-surface precision, native high-precision surfaces,
display transforms and export encoding are not changed. It does not establish
HDR mastering or independent colorimetric correctness.

The corruption test now damages PNG IDAT while retaining a readable IHDR header.
It continues to require cancellation without mutation, repair without a false
cache-hit claim, exact repaired pixels/ICC, warm reuse and wrong-size repair.

## Verification

The first two focused recovery tests pass, with byte-identical displayed pixels
before and after retry on native and provider paths:
/private/tmp/lossless-preview-retry-tests.log (3.65 seconds).

Further photographic, corruption and app checks are recorded below as completed.

Cold native RAW and JPEG checks both preserve exact 4208x3120 displayed pixels
across failure/retry (max channel delta 0): /private/tmp/lossless-raw-native-retry.log
and /private/tmp/lossless-jpeg-native-retry.log. The warm-cache RAW run also passes:
/private/tmp/lossless-recovery-real-proof/
20260908T085505.395862Z-perf-interaction-workflow-4729448a (max delta 0,
current full generation 3).

Five focused cold/warm/native/provider recovery cases and the corruption test
pass in /private/tmp/lossless-warm-recovery-tests.log. The broad controller suite
now passes the old retry fixture and stops later at its first-slider settled-full
scheduling expectation. That unrelated assertion was not relaxed. PNG corruption
coverage includes eight cases across legacy/ACES2 and ordinary/compare modes.

The performance-loop skill bounds any follow-up tuning to three attempts, with
exact recovery pixels protected, >=15 percent target latency improvement and
peak RSS within 10 percent of a fresh PNG baseline. Admission first uses the
existing 30-sample repeat-cache gate: sample p95/p99/max <=150ms and filtered
render <=50ms, with cache hits and zero source-decode/edit/encode work. If the
initial lossless baseline passes, no speculative optimization is needed. This is
not a comparison with the incorrect lossy JPEG output, nor an app-wide realtime
claim.

The fresh lossless baseline passes the existing repeat-cache gate without tuning:
/private/tmp/lossless-preview-cache-gate/
20260908T085704.550489Z-perf-preview-raster-b3adb620. Thirty samples have retry
p50/p95/max 11ms and cached-render p50/p95/max 8ms. All thirty are disk_png_cache
hits with no source decode, edit, encoding or GPU readback. This is the gate's
960x640 synthetic fixture, not the real RAW corpus. Thresholds were unchanged;
the bounded loop stopped at admission with no optimization candidate.

## Cold-cache encoding follow-up

The subsequent 13MP app smoke exceeded its unchanged 45-second proof deadline
and produced no receipt. It ran concurrently with functional tests, so this is
not a controlled whole-app timing comparison. However, default PNG encoding took
4.6 seconds in an isolated RAW recovery and 7.1 seconds in that app run. The warm
960x640 cache-hit gate above does not cover this cold-write cost.

A separate bounded codec comparison uses the same saved 4208x3120 display image,
ARGB32 input, one warmup and five measured encode/decode iterations. Compare the
default encoder with Qt compression 11 (zlib level 1), preserving exact pixels
and ICC including synthetic alpha/P3 coverage. Admission requires encode median
improvement >=15 percent, decode p95 regression <=5 percent, peak RSS growth
<=10 percent and encoded size growth <=20 percent. At most three candidates;
stop or revert on unstable results or failed guards. No app deadline changes.

Candidate 1 (compression 11) was rejected: encode median 981ms, but decode p95
207.4ms versus 186.0ms baseline exceeded the protected 5 percent regression.
Candidate 2 (compression 46, zlib level 4) initially contained an unexplained
63.6-second wall-time encode outlier, despite 16.8 seconds total process CPU time.
No change was accepted from that noisy run alone. Serial confirmation of the
same baseline and candidate produced stable samples:

| Metric | Default PNG | Compression 46 |
| --- | ---: | ---: |
| Encode median | 4649.2ms | 2567.0ms |
| Encode p95 | 4693.3ms | 2581.1ms |
| Decode p95 | 184.69ms | 184.81ms |
| Encoded bytes | 29,227,160 | 29,167,226 |
| Peak RSS, preceding host runs | 299,532,288 | 299,237,376 |

Both runs have identical input SHA256 and require exact decoded pixels and ICC
on every iteration. The second candidate passes the declared gates (44.8 percent
encode improvement, 0.06 percent decode p95 growth). The production atomic writer
now selects compression 46. This is codec cost on one 13MP photographic display
image, not total app latency or all-camera realtime proof. Two candidates were
tested; the loop stops after this acceptance.

Evidence: /private/tmp/png-codec-baseline.json,
/private/tmp/png-codec-candidate.json, /private/tmp/png-codec-candidate2.json,
/private/tmp/png-codec-confirm--1.json and /private/tmp/png-codec-confirm-46.json.
Host RSS is in the baseline/candidate2 `-resource.log` files; confirmation ran
inside the sandbox after permission-review timeout and did not produce fresh RSS
(the time wrapper reported a denied kern.clockrate query). Reported confirmation
timings come from the benchmark itself, which completed its exactness checks.
The Qt 6.10.2 [PNG handler source](https://raw.githubusercontent.com/qt/qtbase/v6.10.2/src/gui/image/qpnghandler.cpp)
defines the compression mapping; PNG remains lossless at both settings.

## Final integration checks

The rebuilt release targets pass 14/15 regressions in 47.32 seconds:
/private/tmp/png-cache-final-regressions.log. This includes five recovery cases
(with loaded-idle tool-start preserving pixels/generation), four native local-mask
cases, corruption repair, render policy, compiled adjustments, mask coverage and
the new PNG codec alpha/P3 test. The sole broad-controller failure remains its
first-slider full-render-timer expectation. Source inspection shows a pending
full request can replace that timer in draft mode; the assertion alone does not
prove final rendering is absent. Actual global-slider draft/final fidelity needs
its own focused check before changing production behavior or that assertion.

The rebuilt desktop app passes its isolated real RAW + placed JPEG source-layer
workflow with the unchanged 45-second deadline. Receipt and inspected screenshot:
/private/tmp/lossless-fast-png-app-proof-6AZsXw/receipt.json and startup.png.
The receipt reports ok, packaged QML, visible preview, source-layer PNG export and
undo/redo, with elapsedMs 36185. The catalog-recovery banner is expected because
this proof deliberately uses a closed database port; no user catalog is changed.
No model weights were downloaded. This serial smoke run is not a controlled
speed comparison with the earlier concurrent failed run. Its 4656ms observation
event-loop gap explicitly prevents a realtime claim.

The unchanged thirty-sample repeat-cache gate also passes after compression
tuning: /private/tmp/lossless-fast-png-cache-gate/
20260908T133108.854720Z-perf-preview-raster-d340c823. Retry p50/p95/max are
11/11.5/13ms; cached-render p50/p95/max are 8/8/9ms. Every sample is a PNG cache
hit with no source decode, editing, encoding or GPU readback. This remains the
960x640 synthetic cache-read scenario, not full-resolution photographic realtime.

Final optimized-writer real-RAW warm recovery also passes native Metal readback:
/private/tmp/fast-png-real-raw-warm-retry.log. It preserves 4208x3120 pixels with
max delta 0 across injected failure/retry, keeps the selected tool, clears the
error and reaches full generation 3/current generation 3 (10.061 seconds total
workflow, not per-frame latency).
