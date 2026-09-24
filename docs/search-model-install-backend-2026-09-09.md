# Native search-model setup backend

## Implemented boundary

`search_model_install.h` adds a worker-only installer for the approved, fixed
SigLIP 2 revision. It supports explicitly confirmed HTTPS download and explicit
local-folder copy. It does not invoke Python/curl, adopt/move the source folder,
inspect photos, or download another model. The native SHA-256 pins for all eight
files were checked against the existing Python installer's pinned Git-blob/LFS
identities using the already approved full model.

The implementation reuses the bounded native transfer, prefix hashing,
cancellation and storage checks from the agent downloader. The shared transfer
function accepts a caller-supplied source policy; its original Ollama policy is
unchanged when no policy is supplied. Search uses explicit HTTPS/443 Hub/CDN
hosts, rejects credentials/fragments and unexpected redirects, and still checks
every file against the application pin. The current host list follows
[Hugging Face's download-host documentation](https://huggingface.co/docs/hub/models-downloading)
with the legacy LFS/bridge hosts retained. Tests cannot enable HTTP fixtures via
preferences, environment variables or a user-facing download URL.

Storage and recovery behavior:

- Only the named managed model directory beneath an existing canonical parent
  is accepted. Newly created stores are owner-only. Existing unowned populated
  directories are rejected, not implicitly adopted or repaired.
- A model/revision-specific ownership marker and exclusive process lock protect
  installer admission. The source cannot contain the destination. Source and
  destination artifact symlinks are rejected.
- SHA-named app-owned partials retain interrupted network/local copies. Retry
  hashes the prefix and resumes at its byte offset. Ignored HTTP Range responses
  restart the partial safely; unexpected size/range/encoding is rejected.
- Full SHA-256 verification precedes each final-file publication. Corrupt final
  files are retained under unique rejected-file names before replacement. A
  checksum failure discards only the app-owned partial, not the existing final.
- Admission/per-file estimates include 1 GiB headroom; actual writes recheck
  space periodically. Estimates are not substituted for full hash verification.
- Success requires a final full-model verification. Merely having a marker,
  enough bytes, some downloaded files, or an existing model directory is not
  readiness. Cancellation does not relabel a completed verified result.

This is currently a **native backend with focused test callers**, not a connected
model-manager UI or a production readiness controller. Do not call this blocking
function on the UI thread. Setup buttons, confirmation/progress/error presentation,
owned background operation lifetime, search availability gating, and startup
verification/recovery wiring remain the next integration step. Model-revision
upgrades and unowned-store migration are not silently implemented as repair.

## Verified evidence

Proof root: `/private/tmp/framehouse-siglip2-O2ha9I`.

`search_model_install_test` covers no-download-before-confirmation, zero-network
local import, verified reuse, recoverable corruption repair, exclusive locking,
truncated download resume, ignored Range restart, bad checksum, blocked redirect,
active-transfer cancellation/retry, unowned folder protection, storage failure,
source/destination symlinks and refusal to write inside the source. The initial
fixture used macOS's noncanonical temporary-directory alias; the fixture was
corrected to pass the canonical parent required by production. The guard was not
weakened. The agent downloader and artifact contracts pass alongside it.

Final focused build has no warnings. All five installer/artifact/download/search
regressions pass in 2.96 seconds (`search-install-handoff-tests.log`), with
localhost permission only for the controlled network fixtures. All owned build,
test, copy, repair and inference handles for this checkpoint exited successfully.

Real approved-model copy:

- Destination:
  `/private/tmp/framehouse-search-managed-AjTNUz/siglip2-base-patch16-224`.
- `search-managed-copy.json`: all eight files / 1,539,456,768 bytes pass, first
  worker operation takes 21,314 ms, retry reuses all eight files, and source
  fingerprints/full hashes remain unchanged. The harness observes 4,126 timer
  heartbeats over the copy and retry; this is not a full-app frame-latency proof.
- `search-managed-copy-inference.json`: the previously signed bundled runtime
  performs actual MPS inference from this managed copy. All ten queries rank an
  expected image first, restricted scope holds, five image vectors survive worker
  restart, and both owned workers stop. First query/indexing is 6,026 ms on five
  inputs/four scenes; this is not a large-library quality/performance result.
- `search-managed-network-repair.json`: the test preserves only the isolated
  store's config under a named backup, fetches the pinned 253-byte config through
  the real native HTTPS path, reuses seven files, then verifies the complete
  model again. The backup is retained. No additional weights were downloaded.
  This validates the small-file network path, not the full 1.5 GB CDN transfer.

No user catalog, model preferences, running app process, original model folder,
or chat-model weights were changed. The new managed copy is isolated test data;
it has not been silently selected as the user's production model.

## Remaining full-goal gates

Connect managed setup to the actual UI and production runtime discovery; validate
missing/corrupt model launch, operation cancellation on shutdown and recovery,
full-size real download/CDN behavior, signed packaged setup and supported OSs.
Then evaluate larger labeled libraries and real agent edits. The independent
RAW/color, loading/export, temporal 4D and professional editing requirements stay
open in `active-app-completion-goal.md`.
