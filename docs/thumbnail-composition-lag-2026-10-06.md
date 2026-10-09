# Thumbnail composition interaction lag

The user's repository-launched Looksy session (PID 20466, started October 6 at 18:12 local time) provides direct evidence of blocking work when composing a graphic with text, an adjustment layer and subject cutouts. The initial investigation was read-only; subsequent component fixes and their qualification limits are recorded below.

## Observed costs

| Operation | Controller callback | Recovery stage | Render-intent stage | Normalization |
| --- | ---: | ---: | ---: | ---: |
| Text style change, three layers | 1,090 ms | 339 ms | 748 ms | 1.52 ms |
| Text style change, three layers | 1,406 ms | 649 ms | 756 ms | 0.46 ms |
| Layer transform, three layers | 375 ms | 0 ms | 374 ms | 0.72 ms |
| Layer-stack change, five layers | 1,942 ms | 1,147 ms | 793 ms | 1.18 ms |

The generic layer-stack event does not identify its command, so it is not labeled as a confirmed reorder measurement. These are individual logged slow callbacks, not population percentiles. The render-intent stage includes synchronous signals, request preparation and scheduling; it is not the background renderer's execution time. The 375 ms transform cannot be attributed to saving, which recorded zero time for that invocation.

Successful recovery checkpoints reached 243,832,871 bytes (approximately 244 MB decimal), with adjacent writes taking 667 and 674 ms. One nearby checkpoint failed after 1,146 ms and was followed by successful writes. The failure's cause was not established. Logged layer payloads progressed from 1,536 to 3,072 paint tiles. This makes large raster recipe handling a priority; it does not establish a malformed record or justify discarding one.

Edit-route frame telemetry also records multi-second intervals. Frame intervals can include idle periods and do not independently measure input-to-painted-frame latency. The controller callback timings directly establish foreground blocking work.

## Current source and priorities

`LayerTransformOverlay` calls `setPhotoLayerTransform()` during pointer movement. `setPhotoLayerTransform()` and `movePhotoLayer()` both reach `applyPhotoLayerStackChange()`. That method normalizes the stack, captures undo, invalidates thumbnails, requests persistence, publishes controls and submits a composite edit intent. Existing coalesced transactions already group undo, defer persistence until completion and schedule interactive renders. Avoid proposing these existing mechanisms as new fixes.

1. Profile and reduce the foreground recovery checkpoint for raster-heavy compositions. Store immutable cutout/paint payloads once and journal small metadata revisions, preserving recovery, exact reopen and undo/redo. Simply removing the immediate checkpoint would change the durability contract.
2. Break down synchronous render-intent work, including signal subscribers and request preparation, using the same raster-heavy fixture. Keep expensive encoding, hashing, decoding and preparation on workers; retain immutable request capture and stale-result checks. The existing background renderer alone does not ensure a cheap foreground submission.
3. Publish layer structure incrementally, preserve selection by identity and reuse unchanged thumbnails. Keep selection, handles and row movement responsive while rendering is pending.
4. For smooth dragging, explore cached layer textures and compositor transforms with a final canonical render at release. Validate masks, blend modes, adjustment layers and export parity before accepting an approximation.
5. Keep the last complete canvas visible, with localized rendering/thumbnail feedback. Loading indicators require a responsive UI thread to animate and cannot compensate for blocking callbacks.

Use the actual composition size and raster payload scale. Target 16.7 ms frames for 60 Hz interaction, record input-to-painted-frame latency separately from final render and durable save completion, and compare matching before/after runs without concurrent compilation. Do not accept a fix based only on a build or a small empty-layer fixture.

## Provenance and next gate

The active process path is `build/release-macos/apps/desktop/Looksy.app/Contents/MacOS/Looksy`. Its log session records the same PID. However, the binary file on disk was rebuilt at 18:18:35, after this process started. The exact loaded build has not been mapped to the current source, and no speedup is claimed. First reproduce these operations in an isolated session of the newest binary, preserving the user's running document, then compare one narrow change against that baseline.

[Selected session timings and provenance](evidence/thumbnail-composition-lag-2026-10-06/session-timings.json) retain the initial log snapshot hash, timestamps and stage fields. No application patch or restart was performed during that initial investigation.

## Memory and zoom follow-up

A live host sample reported Looksy RSS of 6,934,688 KiB (6.61 GiB) on a 48 GiB machine, with about 20.82 GiB of system-wide swap allocated. A separate three-second counter sample recorded zero swap-ins and zero swap-outs, 533 decompressions and zero compressions. Existing swap allocation alone does not establish active swap thrashing or attribute pressure to Looksy. This was not a synchronized reproduction of a zoom freeze, and RSS does not capture all GPU allocations or peak transient use.

The session's source image is 6,024 × 4,020 pixels, approximately 24.2 MP. Recent renderer events report about 852 MB of managed render caches; that figure is not the app's complete allocation inventory. Recorded zoom requests plan 3–8 visible tiles out of 96, so the logs do not support claiming that every zoom always rerenders the entire image. Current source reuses covered native pixels and requests visible tiles when coverage is missing. Full-resolution coverage getters also query edit identity; current identity code already compacts and caches immutable large payloads, so do not describe it as unconditionally hashing the entire raster recipe anew.

High resolution increases the cost of intermediates, copies and serialization and can amplify the measured foreground stalls. The observed save/submission stalls already explain unresponsive controls without requiring active swapping. The next qualification should separately measure foreground zoom callbacks, input-to-painted-frame latency, render dimensions, transient/retained memory and paging activity on a reproducible zoom gesture.

## First accepted component fix

The ordinary edit-identity cache admits entries only up to its 32 MiB byte budget. Large cutouts exceed that limit, so repeated queries serialize and hash them again. Position changes also invalidate whole-entry identity even when every pixel is unchanged. This affects signatures queried by full-resolution coverage and other UI/render preparation paths.

`edit_adjustment_patch.cpp` now fingerprints immutable `paintTiles` separately from layer metadata. Exact source-map comparisons reuse fingerprints across unchanged queries and transforms. Pixel changes, paint metadata, layer order, text, tone and undo snapshots still affect identity. A mutex protects concurrent GUI/worker queries. The additional payload cache retains shared source values, never serialized JSON, with at most 16 entries and a conservative 256 MiB source-storage ceiling. The ordinary 32 MiB entry-cache limit remains. Derived identity values change; saved recipe fields and pixels are unchanged.

Acceptance required at least 20% improvement in warm and transform queries, no more than 20% cold/peak-memory regression (2 ms timing allowance), and exact correctness checks. One candidate passed three alternating before/after process pairs, without overlapping task builds.

| Isolated identity workload | Before | After |
| --- | ---: | ---: |
| Repeated query, median of process medians | 661.67 ms | 0.091 ms |
| Transform revision, median of process medians | 659.75 ms | 0.097 ms |
| Cold identity construction | 665.53 ms | 663.35 ms |
| Peak memory footprint, median | 3.431 GB | 0.703 GB |

The fixture contains two 48 MiB ASCII paint payloads (768 tile strings each) plus a text row. It isolates the production identity helper; the strings are not a real decoded photo/composition fixture. Each process takes seven warm and seven transform samples. Both versions use the same frozen benchmark object and matching remaining link inputs. Memory is the probe-process peak, not Looksy's retained memory or GPU footprint. No input-to-painted-frame speedup is established here. Cold first-time hashing and synchronous recovery writes remain open.

The production app and guard targets built. Four registered checks passed: identity, canonical v13 persistence, coalesced undo, and layer payload reuse. The identity test also covers concurrent GUI/worker queries. Separate `--resident-zoom` and `--zoom-out-coverage` controller checks passed. The full `editor_preview_pixel_zoom_test` failed two unrelated source-string expectations in unchanged QML: `basePaintedContentX: contentMargin` and `function boundedCanvasCoordinate(`. The full focused suite is not claimed to pass.

Direct native-window observation was attempted but blocked: Computer Use startup failed, and its retry reported a server/client version mismatch requiring a ChatGPT client relaunch. The user's Looksy process and graphic were left open; no restart or document mutation was performed. The rebuilt app must be relaunched after saving to exercise this change. Native click-through qualification and recovery-write improvements remain outstanding.

[Matched comparison](evidence/thumbnail-composition-lag-2026-10-06/identity-fix/comparison.json), [build provenance](evidence/thumbnail-composition-lag-2026-10-06/identity-fix/current-build-provenance.json), and logs in that directory record the component gate and its limits.

## Accepted recovery serialization fix

The existing journal's fast JSON encoder recognized large `segmentationData` and `depthData` strings, but cutout `paintTiles` took the general Qt serializer. Extend detection to large strings in nested maps and arrays. For ASCII strings that require no JSON escaping, borrow Qt's encoded string storage and append it directly after checking every character. Quotes, backslashes, control characters and non-ASCII strings use Qt's canonical escaping. Qt 6.10 supports borrowed JSON string views; older Qt versions retain an owned-string fallback. The older-version branch was not measured on this host.

This leaves journal versions, checksum calculation, atomic `QSaveFile` commit, failure retention and cache limits intact. Three candidates tested the same hypothesis. The first two probes improved by approximately 13% and 17%, below the 20% acceptance target. The third reduced conversion/copy overhead and passed three alternating before/after process pairs, with no concurrent task builds.

| Durable recovery write fixture | Before | After |
| --- | ---: | ---: |
| Warm write, median of process medians | 517.18 ms | 390.64 ms |
| Cold write, median | 534.43 ms | 410.53 ms |
| Peak process footprint, median | 2.712 GB | 2.712 GB |

Warm writes improved 24.47%. The fixture contains two pixel cutout layers, each with 192 valid compressed RGBA float tiles, plus text on a 6,024 × 4,020 canvas. Each write changes a layer transform. All six final processes wrote exactly 196,822,048 bytes with SHA-256 `b5d0a23df6a72736266c658c09971a9dad5f1bd1edaabab8f0f3c15d4ac2304d`, reopened the exact layer state, and decoded a valid tile. The fixture repeats one deterministic tile and exercises journal scale and encoding; it does not reproduce the user's photo or measure pointer-to-frame latency. Peak footprint covers the entire probe, including setup and reopen, rather than just the timed writes.

One matched pair of the nine existing tone/brush/retouch/rejected-record scenarios passed the protected cold, p50 and p95 timing gates (+20% plus 2 ms), peak footprint gate (+20%), exact reopen and identical output hashes. The 32-entry codec matrix also matched. New regression coverage compares long nested strings and changed tile data against an independent Qt compact-JSON envelope, including quotes, backslashes, newline, NUL and Unicode. Registered native journal, portable-digest journal and queue tests passed (3/3), covering recovery after writer death, catalog isolation, exclusive ownership, atomic failure retention and corrupt-file preservation. Test journals were disposable; no live recovery record was edited.

This remains a foreground checkpoint of approximately 391 ms. It reduces one pause but cannot establish smooth 60 Hz interaction. The next larger persistence change should write immutable raster content once and commit small metadata revisions while maintaining durable recovery and undo. Native reordering, dragging, text edits and zoom still require qualification in the rebuilt app.

During the follow-up, Computer Use briefly listed apps, then inspecting Looksy by application path timed out and a subsequent request failed native-pipe startup. A new startup log at 00:50 UTC (PID 45701) coincided with the inspection attempt and reported `AgentSessionTerminal is not a type`; the current checkout already registers that component, so the final rebuild includes that independent registration. No existing Looksy process was closed, and no graphic was changed by the tests. Direct native click-through is still unverified.

The final production app build passed, with unrelated existing agent/generated-QML warnings. A software/offscreen launch using `--managed-runtime-proof-dir` isolated catalog, runtime data and preferences. It emitted `desktop.startup.ready` and its receipt reports `packagedQmlLoaded: true`, with no missing-type error. The broader runtime proof exited 1 (`ok: false`) because the local AI runtime port was already occupied; its receipt confirms `ownsProcess: false` and that no existing server was adopted or stopped. The startup observation establishes QML loading only; full runtime, native interaction and GPU proof remain open. See [startup receipt](evidence/thumbnail-composition-lag-2026-10-06/recovery-fix/startup-receipt.json).

[Recovery comparison](evidence/thumbnail-composition-lag-2026-10-06/recovery-fix/comparison.json), [protected cases](evidence/thumbnail-composition-lag-2026-10-06/recovery-fix/guard-comparison.json), [provenance](evidence/thumbnail-composition-lag-2026-10-06/recovery-fix/provenance.json), receipts and narrow patches record the acceptance evidence.
