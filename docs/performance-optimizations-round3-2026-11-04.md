# Performance optimizations, round 3 — 2026-11-04

This pass used three Luna xhigh agents plus integration/review in the parent. Existing changes and concurrent RAW/color-management work were preserved. The Release application and focused targets build through the shared build lock; validation uses temporary data.

Evidence root: `perf-artifacts/luna-performance-round3-2026-11-04/`.

## Accepted changes

| Area | Change | Focused evidence |
|---|---|---|
| Brush input | Create the component-source job/signature only when the brush actually samples component contents. | With a stored 16 MiB mask, dispatch was about 101 ms before and 0.43 ms after; two alternating pairs produced the same alpha hashes. Empty-mask dispatch also improved. |
| Brush projection | For an inactive Geometry stage, evaluate the existing sampler once per axis and retain the exact interpolation arithmetic. | 1800×1200 → 2560×1707 projection: 107.162 → 52.856 ms; seven fixtures have identical RGBA pixels, including crop, ROI, singleton axes and the active-geometry fallback. |
| Recovery checkpoints | Append small checksummed, fsynced edit records between atomic compactions. Retain immediate durability before helper debounce. | See `root/checkpoint-quiet-*.json` and the final comparison below. Recovery, queue, close and asynchronous recipe integration tests pass. |
| Library search | Cache owned searchable fields by canonical metadata identity/revision, bound additional storage to 44 MiB, preserve field boundaries/Unicode matching, and cache stable-query results. Admission avoids cyclic-scan eviction. | Matched 100k typed-query p95: 412.684 → 147.876 ms; repeated query/filter p95: 405.444 → 24.290 ms. RSS +3.8%; protected filters improved. |
| Retouch blur | Fuse RGB horizontal/vertical passes, retaining sum order, float rounding, alpha and cancellation behavior. | 1024×768 radius 2: 4.048 → 2.302 ms; bitwise RGBA parity. |
| Board previews | Build request-local first-ID/component lookups, deduplicate component work and cache board rectangles. | 263.225 → 2.628 ms for 128 selected / 6,400 linked items / 64 boards; affected sets and document restoration match; RSS +0.9%. |
| CPU-readable Metal | Reuse one bounded 16 MiB synchronous readback buffer with a try-lock lease through completion; concurrent/oversized calls use temporary allocation. | Actual-device 606×404 test: 0.969 → 0.924 ms median; 8 allocations → 1, with 7/7 warm reuse hits, unchanged transfer volume and ACES2 parity; RSS decreased 0.3%. |
| Hidden Timeline content | Release closed dock panels after a short grace period with session snapshots; deactivate 25 closed popup content loaders. | Behavior tests cover source/effect/caption drafts and source/color/audio suspension. A 26-popup audit identified local speed-field drafts, so that popup retains its original content lifetime. |

Library/Board evidence: `catalog-board/summary.md` and its baseline/candidate receipts. Engine evidence: `engine/ENGINE_RESULTS.md`. Timeline implementation/audit: `root/qml-round3-implementation-report.md`.

## Logged QML failures

`activeTimelineView()` now rejects a stale Edit workspace during a route change, and render-action method calls have capability checks. Timeline waveform visibility always produces a Boolean when clip metadata is absent. Timeline route readiness waits for its actual editor page. Generic title-font fallback resolves to the system UI family; a Qt font probe confirmed the same physical family and advance as the previous generic family on this host.

The completed offscreen brush workflow now applies its selection authoring transaction before navigating to the next fixture. The fixture selects by identity rather than relying on rows staying fixed. Its final two-iteration diagnostic completed: first feedback 80 ms, later feedback 39 ms. These source-QML/offscreen figures are diagnostics, not a native displayed-frame speedup claim.

## Recovery format and failure behavior

The existing authority-scoped `.json` file starts with the conventional checksummed v3 envelope. Small edits append a typed JSON tree edit and a digest-chain link, followed by a commit fence. The body is flushed/fsynced before the fence is written and fsynced. Atomic `QSaveFile` compaction also syncs the containing directory on Unix.

Compaction occurs for large edits, changed/unreusable mask stacks, empty checkpoints, or after 128 records. The file stays inside the existing 256 MiB bound. Recovery materializes the queue without retaining another full JSON tree beside decoded raster data. Legacy v1/v2/v3 envelopes remain readable. Existing older executables that only parse a single envelope cannot consume a pending delta journal; a successful queue drain produces a conventional empty checkpoint.

Incomplete trailing bodies/fences recover the preceding committed head without altering the file. A torn final fence whose delimiter reached storage is covered. A corrupt complete body or a broken middle chain remains a visible failure and preserves the original file. Before retrying over an unacknowledged tail, its truncation is persisted before any replacement bytes are issued. Deep evidence edits fall back to a full atomic checkpoint when a typed patch would exceed the JSON recovery parser depth limit. Tests cover both cases, killed-writer recovery after an appended edit, exact tiny scalar changes, queue-role transitions, atomic failure retention, ownership/retry, rejected records, legacy migration and compaction.

## Checkpoint comparison

Two sequential alternating baseline/candidate process pairs used the same 24-edit fixture matrix. Median checkpoint times across the two runs:

| Fixture | Before | After |
|---|---:|---:|
| Small tone | 0.298 ms | 0.134 ms |
| 1 MiB mask, tone edit | 1.865 ms | 0.172 ms |
| 4 MiB mask, tone edit | 6.016 ms | 0.289 ms |
| 16 MiB mask, tone edit | 21.341 ms | 0.766 ms |
| 4 MiB retained rejected record | 17.638 ms | 12.076 ms |

The changing-raster protected fixtures stayed within 2.2% in each paired run. Peak RSS maxima rose 4.8%; the worst individual paired RSS increase was below 6%. Every fixture reopens to the exact final recipe. `root/checkpoint-final-comparison.json` records the comparison.

A separate large-paint guard used two cutouts, text, 384 encoded tiles and a 196.8 MB checkpoint: median 366.370 → 266.979 ms (-27.1%); cold write +1.3%; RSS +5.5%. Reopen and tile decoding passed. Small edits append roughly 0.5 KiB records in this case, but constructing/comparing its large JSON state still costs hundreds of milliseconds. This is not a 16 ms interaction qualification.

## Rejected candidates and remaining scope

Sparse-mask ROI growth was reverted after three attempts: sparse growth improved around 80%, but repeated dense-case measurements regressed 6.2–6.5%, exceeding the 5% guard. The original radius-1/2 stencil and caller are restored exactly. The rejected implementation and evidence remain in `engine/`.

Always-asynchronous Timeline loaders were also reverted. A complete native source-QML comparison showed route readiness 16.208 → 14.804 s and initial input dispatch 9.836 → 5.748 ms, but the dominant Timeline frame gap remained 1.677 → 1.656 s. That did not meet the stall-reduction target. This diagnostic comparison also spans concurrent StudioWorkspace color-management changes, so it does not isolate loader effects. These source-QML cold-load timings are not packaged application timings; the live log's long route/render-dispatch delay is not declared eliminated.

Photo-layer composition still uses the existing CPU fallback; these tests do not qualify the logged 100 ms live-render budget.

The repository performance-loop skill requires reverting candidates that miss the target or violate protected guards. The QML correctness, readiness and hidden-content fixes remain accepted; the two loading expressions retain their prior behavior.

## Verification

- Release `photo_editor_app` and the focused benchmark/test targets build successfully.
- 16 controller/engine/UI tests pass in `final-focused-ctest.log`.
- Two recovery-journal variants plus the final removal/blur test pass in `final-recovery-engine-ctest.log` (19 focused tests total).
- Both journal variants pass again after the final deep-evidence and retained-tail hardening (`final-journal-edge-ctest.log`, `final-journal-tail-ctest.log`); final Release build confirmation exits 0.
- Library benchmark `--verify` passes after correcting its assertion scope: membership changes intentionally reset a model and invalidate persistent indexes; unchanged projections preserve them.
- Actual Metal-device ACES2 parity and reuse tests passed. The sandbox alone could not enumerate Metal, so hardware tests used the approved native execution path.
- Native brush foreground qualification was unavailable for the raw test executable; no native brush input-to-pixel claim is made.
- `git diff --check` passes for task-owned changes. Initial/final dirty-tree inventories and source/binary receipts are retained in the evidence root.
