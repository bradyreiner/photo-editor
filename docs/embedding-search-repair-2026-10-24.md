# Local embedding search repair

Library AI Search now defaults to the existing local SigLIP 2 image/text
embedding pipeline. The desktop app compiles and exposes the model setup
controller, verifies the bundled worker, verifies the model off the UI thread,
and configures search only after verification succeeds. Embedding search is
independent of the chat model's busy state.

Search model settings provides download, local-folder import, file verification,
progress and cancellation. Find photos stays disabled until search is configured.
The legacy detail-search mode remains available through the mode toggle.

The pinned `google/siglip2-base-patch16-224` model (revision
`75de2d55ec2d0b4efc50b3e9ad70dba96a7b2fa2`, 768 dimensions) is installed in
`~/Library/Application Support/PhotoEditor/search-models/siglip2-base-patch16-224`.
The native installer verified all eight files and recorded managed ownership;
re-import reused the verified files. The source copy and photographs were not
modified. The release cache previously omitted the search runtime; both local
app builds now include the verified `build/search-runtime-input` bundle.

## Verification

Six focused CTest checks pass: model installer, worker protocol, vector index,
local similarity search, catalog scope contract, and setup controller. The setup
check covers verification off the UI thread, missing-file rejection, recovery,
and cancellation without admitting search.

Real inference with the frozen worker ranked the expected photograph first for
three descriptions: rocky mountain peaks, a sunglasses portrait, and a person
beside a lake and skyline. Warm queries took 268–538 ms in this small fixture;
all three vectors were reused after a worker restart. Scope restriction and
owned-worker shutdown also passed.

The isolated native Cocoa workflow loads production Main.qml with real search
controllers and three local JPEG previews. Qt pointer events open AI Search,
open model settings, click Find photos, and clear the results. It checks default
embedding mode, expected first result, cached repeat queries, filter invalidation
and recovery, worker shutdown, and wide/compact layouts. Its final receipt
records 6,134 ms for initial indexing/search and 527/529 ms for cached searches.
Setup, results and compact screenshots were visually inspected. These are native
Qt pointer checks, not an OS input latency measurement.

Evidence is under `build/search-fix-2026-10-24/`: `contracts.log`, `setup-test.log`,
`managed-model-install.json`, `live-search.log`, `native/receipt.json`, screenshots,
build logs, bundled-runtime verification, and source/binary SHA-256 snapshots.
Both Debug and Release app builds pass. Restart Looksy to load the rebuilt app;
the existing session was not closed.

This qualifies the model and search path on the three-photo fixture. It does not
establish large-library indexing speed, exhaustive query accuracy, or RAW search
quality. Similarity ranks candidates; it does not verify every detail in a query.
