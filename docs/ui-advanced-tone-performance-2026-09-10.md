# UI identity reuse and prepared advanced layer tone, 2026-09-10

This pass targets repeated main-thread recipe hashing and per-pixel advanced
layer setup. The acceptance target is at least 20% improvement on each focused
scenario, with exact pixels and unchanged recipe identities. The identity cache
keeps its 32 MiB serialized-payload accounting limit. End-to-end RAW latency and
peak memory are checked separately with a 10% regression guard. Local timing is
subject to host load; these are individual runs, not population percentiles.

## Changes

- The large-entry recipe identity cache admits up to 32 entries instead of four.
  The measured recipe contains seven stack masks and four mask sets, including
  five large entries. The old limit repeatedly evicted unchanged entries. A
  native main-thread sample traced viewport property reads through
  `edit_state_signature` into repeated Qt SHA-256 hashing. Concurrent misses now
  reuse an entry inserted by another thread instead of charging for duplicates.
  Digest bytes, source equality, mutation invalidation and persistence behavior
  are unchanged. The byte limit accounts for serialized payload size, not total
  process RSS or all QVariant/container overhead.
- Standard adjustment layers prepare their advanced tone settings once per
  layer. Both normal pixels and spatial preparation share that immutable state.
  Brightness, filmic shoulder/toe, adaptive contrast, levels and five tonal zones
  retain the same numerical operations. The custom callback path retains scalar
  preparation and supplies the comparison oracle.
- The actual-QML interaction driver accepts an asset-bound recipe file and
  records an 8 ms GUI heartbeat, allowing large-mask stalls to be reproduced.

## Focused results

Five samples per process, three alternating old/new process pairs, 1024 × 683
pixels with advanced tone enabled:

| Advanced layer time | Before | After |
| --- | ---: | ---: |
| Pair 1 median | 36.71 ms | 9.91 ms |
| Pair 2 median | 40.96 ms | 9.94 ms |
| Pair 3 median | 38.86 ms | 10.30 ms |
| Median of process medians | 38.86 ms | 9.94 ms |

The measured reduction is 74.4%. Every run checks exact production/scalar pixel
equality. An initial 191 ms baseline overlapped compilation and is excluded.

The native Cocoa/Metal QML workflow uses the same 1800 × 1200 synthetic source,
large-mask recipe, one curve change and 30 coalesced curve updates in both builds.

| Metric | First before → after | Clean repeat before → after |
| --- | ---: | ---: |
| Edit setter | 1097 → 69 ms | 1369 → 111 ms |
| First native publication | 3082 → 501 ms | 3696 → 835 ms |
| First frame after publication | 3260 → 585 ms | 3960 → 966 ms |
| All render work idle | 3462 → 768 ms | 4126 → 1265 ms |
| Worst setter during 30 updates | 229 → 14 ms | 206 → 20 ms |
| Burst through settled result | 5351 → 1491 ms | 5218 → 2124 ms |
| Maximum GUI heartbeat gap | 1673 → 490 ms | 2337 → 525 ms |

The first baseline included sampling; the repeat did not. Both clear the target.
Before/after native screenshots were inspected: the image, controls and layout
remain consistent. The fixture intentionally lacks catalog helpers, so its save
warning/retry behavior is expected and does not describe the user's catalog.

The remaining 490–525 ms heartbeat gap is still visible jank. There are zero
native publications during the input portion of this coalesced curve burst in
both builds. This pass improves setter and completion latency; it does not prove
continuous rendered feedback throughout dragging. Save preparation/checkpoints
remain synchronous and are a separate follow-up.

## Correctness and reproduction

The six native CR2 runs completed (before/after, then two after/before pairs).
The host was busy, with substantial per-run variability; the first pair alone
would have incorrectly suggested a regression. No builds ran during these
measurements. `raw-comparison.json` records every sample and output digest.

| RAW metric | Before median | After median | Change |
| --- | ---: | ---: | ---: |
| First live draft render | 319 ms | 217 ms | −32.0% |
| Full curve-edit render | 3196 ms | 3037 ms | −5.0% |
| Flattened export | 23041 ms | 19921 ms | −13.5% |
| Peak RSS, two measured runs per build | 5,830,246,400 bytes | 5,831,860,224 bytes | +0.03% |

Full render samples are 1805/3196/4231 ms before and 3037/1488/3570 ms after.
Export samples are 16891/23041/23495 ms before and 19921/18384/22930 ms after.
These medians pass the 10% latency/memory guards, but do not establish a reliable
whole-workflow speedup on a broader corpus. The 217 ms draft still misses the
100 ms budget. Peak RSS is for the entire heavy preview/export fixture, not the
identity cache alone.

All six runs produced byte-identical `draft.png`, `full.png` and
`matched-reference.png` across builds. Draft/reference channel delta is zero;
native full-preview/flattened-export maximum channel delta remains 1/255.
Every cached viewport comparison also reports zero difference. The real RAW
output was visually inspected. Both focused optimizations are accepted on this
evidence; save-path jank and continuous feedback remain follow-ups.

- `neutral_layer_elision_test`: exact scalar/production comparisons, including
  72 advanced field/sign/spatial cases, masks, blend modes, clipping, HDR/alpha,
  mixed controls and parallel rows.
- `edit_recipe_state_test`: portable SHA-256 identity compatibility for ten
  large masks, four concurrent cold requests, mutation invalidation and unchanged
  original-state reuse, alongside existing recipe regressions.
- Real CR2 proof uses the existing large-mask recipe with advanced controls
  enabled on an adjustment layer, a 1024-pixel draft minimum, native viewport
  cache checks and flattened export. Results are recorded below.

Artifacts are under `/private/tmp/framehouse-ui-tone/`: archived before binaries,
source snapshots, `tone-*-paired.log`, `burst-*.log`, full telemetry directories,
screenshots, `advanced-recipe.json` and `raw-*.log`/image directories. Temporary
artifacts and the local source photograph are not committed fixtures.

The Release app and focused tests rebuilt successfully; both regression tests
passed again from that build. `./scripts/run-native.sh --no-build` relaunched the
app successfully (PID 6670, startup-ready event at 20:06:52 UTC). The existing
save journal was backed up before launch and contained no pending or active
entries. Build and launch logs are in the same artifact directory.

```sh
build/release-macos/tests/neutral_layer_elision_test
build/release-macos/tests/edit_recipe_state_test
build/release-macos/tests/neutral_layer_elision_test --advanced-benchmark

QT_QPA_PLATFORM=cocoa \
PHOTO_EDITOR_OBSERVABILITY_RUN_DIR=/private/tmp/ui-tone-telemetry \
build/release-macos/tests/actual_app_workflow_benchmark_driver \
  --color-interaction-proof --color-control curve --iterations 1 \
  --timeout-ms 120000 --recipe-file /path/to/large-mask-recipe.json \
  --capture-dir /private/tmp/ui-tone-captures
```
