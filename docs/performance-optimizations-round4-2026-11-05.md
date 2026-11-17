# Performance optimizations, round 4 — 2026-11-05

This bounded pass retains the large-paint checkpoint improvement and more accurate preview timing telemetry. Three Luna xhigh agents investigated preview admission, catalog filtering and QML activation; the parent handled checkpoint conversion and integration. Existing and concurrent edits, including external font-property work, were preserved.

Evidence root: `perf-artifacts/performance-round4-2026-11-05/`. Gates: at most three attempts per target, at least 10% target improvement, no more than 5% protected-path regression and no more than 10% peak RSS growth. Performance windows run sequentially.

## Accepted: one canonical layer conversion

The desktop recipe contains the same layer data under both `photoLayers` and `layers` for compatibility. It previously converted the complete QVariant layer list to JSON twice. It now constructs one QJsonArray and shares it between the two keys. No new cache or recovery format is introduced; immediate durable writes, compaction, exact recovery and old format support remain unchanged.

The matched baseline overrides only the pre-edit codec object, linking the same dependencies and benchmark object as the candidate. Its link map confirms the archive codec object is not pulled. Sources and executable/archive hashes are recorded under `checkpoint/`.

| Large-paint checkpoint fixture | Baseline | Candidate |
|---|---:|---:|
| Warm median | 300.611 ms | 158.221 ms |
| Cold write | 426.395 ms | 277.823 ms |
| Peak process RSS | 3,564,634,112 B | 3,547,136,000 B |

Warm latency improves 47.37%, cold latency 34.84%, and peak process RSS decreases 0.49%. The 384-tile fixture produces the identical 196,825,837-byte journal and SHA-256. Exact reopen and paint tile decoding pass. Both journal variants pass after the change. The matched nine-case mask/tone/rejected guards pass: medians stay within +1.20%, p95 within +3.06%, and peak RSS is effectively unchanged. These measurements cover a synthetic large-paint checkpoint, not whole-app interaction latency.

## Preview wait interpretation

Inspection found that the existing `queue_wait_ms` starts at the GUI request timestamp and ends before submitting the QtConcurrent future. It includes GUI scheduling/debounce delays and does not measure waiting inside the global thread pool. The round 3 144–374 ms values therefore cannot alone justify changing pool priorities. Fresh instrumented runs measured 0 ms of actual pool wait on the reproduced successful previews. Retained telemetry labels request-to-GUI submission separately, measures pool wait, includes it in worker-completion totals, and logs request-to-provider/texture publication after previewChanged. Publication events include source asset, path, generation and asynchronous job identity. These are controller publication measurements, not native displayed pixels.

## Remaining qualification

The large-paint checkpoint still takes about 158 ms; changed 16 MiB mask checkpoints still take about 75 ms in the initial guard rerun. This accepted change does not introduce changed-tile mask persistence. Cold QML/source/offscreen diagnostic timings are distinct from compiled packaged native displayed-frame qualification.

## Private mask candidates not retained

Attempt 2 reused canonical brush/raster JSON payloads and retouch aliases. All 32 codec fixtures were byte-identical; 16 MiB brush writes improved about 20% and 4 MiB retouch about 30%, but the confirming 1 MiB tone median rose 6.62% and small-tone p95 9.16%. It was not applied to production.

Attempt 3 narrowed this to reusing only the exact retouch object already in the compatibility stack. Its 32 codec fixtures were also identical. Under the held build lock, 4 MiB retouch improved 18.759 → 12.855 ms (-31.47%), but the protected 1 MiB brush p95 rose 7.10% (its median +0.34%). The tail result may include measurement noise; it still misses the declared 5% guard. Peak RSS was effectively unchanged. The loop stops without applying this candidate. Full frozen link inputs and comparison files remain under `checkpoint/`.

The [performance-loop skill](/Users/bradyreiner/.codex/memories/skills/photo-editor-perf-loop/SKILL.md) says, “Revert if the target misses badly or violates a protected guardrail.” The only retained codec change is the accepted shared layer array; no mask persistence format or durability behavior changed.

## Rejected preview scheduling candidate

Immediate interactive dispatch at the end of asset selection reduced request-to-publication from 630 to 330 ms and 247 to 162 ms in the two fixtures. It also regressed brush feedback p95 from 44 to 67 ms and first-use controller-to-QML frame time from 72 to 96 ms. It was reverted. The first candidate generation initially carried different mask state and later published the current mask via mask_commit. Those publication times alone do not establish equivalent presented-frame behavior.

Both matched offscreen workflows reached their target events, while both retained pre-existing brush HUD/first-use budget failures. The 373-file QML tree and font sources matched across the pair. See `preview-queue/attempt-report.md`. Production scheduling retains its previous behavior; only timing telemetry remains.

## Rejected Library candidates

All Library changes were restored to the frozen Round 4 baseline, including the original 44 MiB searchable-field cache limit.

1. Increasing that bounded cache to 96 MiB improved the initial 100k typed-query p95 by 9.61%, below the 10% target.
2. Adding a POSIX basename view avoided QFileInfo construction while retaining platform/edge fallbacks. A fresh matched pair improved typed-query p95 by 5.24%, also below target.
3. A bounded source sorting projection removed repeated filename/capture-time work for broad filters. Hardening added owned string keys, exact input/locale/time-zone validation, bounded admission before allocation and old-generation release. It left text searches and small results on the original path. Filter p95 improved 60–89%, but the median repeated-search p95 across two process pairs regressed 5.14%, above the declared 5% guard. Worst candidate versus best baseline RSS rose 9.13%, within its cap. Semantics, QML checks, stable projection/selection contracts and exact rollback hashes are recorded under `catalog/candidate-attempt3/`.

The large filter gain is preserved as an unapplied candidate. A future experiment could release the projection when a nonempty query makes it unused, then remeasure retained memory and first-query/repeated-query costs. No additional attempt was made after the bound.

## QML investigation

No production QML was edited. In the current offscreen baseline, brush activation was 256 ms cold and 53 ms warm. Mask-change timestamps are cumulative: 8 ms after maskStateChanged, 246 ms after controlsChanged, 247 ms after maskOverlayChanged and 248 ms total. Thus the synchronous controlsChanged segment cost about 238 ms; overlay added about 1 ms. Existing HUD objects were already instantiated. The source-supported mask-set projection candidate was not proven to own that interval, so no speculative deferral was applied. See `qml/round4-investigation.md`.

## Final verification

- Final Release app and focused targets build successfully (`final-confirmation-build.log`, exit 0).
- Eleven of fourteen focused CTests passed inside the sandbox. The TCP fixture in `app_bootstrap_test` could not bind there; its native recheck passed. Thus twelve of the fourteen focused tests pass after that environment recheck.
- The dedicated held-pool regression fails with the pre-instrumentation logic and passes with the retained telemetry (`baseline-check/held-pool-red-green.json`): dispatch 1 ms, actual pool wait 266 ms, render 4 ms, request-to-completion 272 ms. It exercises the normal controller submission path and restores pool configuration.
- Two default tests remain failing in both the current binary and private links using pre-task controller/codec logic: `editor_preview_feedback_test` and `editor_preview_layer_mask_history_test`. The private baseline keeps the same current header layout and external font signal; its new timing fields are unused. Project headers remained unchanged throughout the comparison.
- Both tests assume that adding a linear selection creates an adjustment layer. A direct probe reports exactly `maskCount=1`, `photoLayerCount=0`, `canUndo=true` for both baseline and current code. The feedback test fails its layer-count precondition before entering the first-frame timing call. This existing selection/layer contract mismatch was not changed by this performance pass. See `baseline-check/comparison.json`.
- The new regression uses isolated temporary data; no native displayed-frame qualification is claimed. Native recheck and baseline receipts are retained rather than marking the entire suite green.
