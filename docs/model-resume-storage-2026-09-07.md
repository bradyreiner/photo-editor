# Cache-aware model download storage

The managed setup plan previously required the full pinned artifact plus 1 GiB
for every attempt. A user who had already downloaded most of the 20.9 GB model
could therefore be prevented from resuming, despite having enough space for the
remaining bytes.

## Changed

- Setup now reports estimated cached bytes, estimated remaining bytes, the
  1 GiB storage reserve, and the resulting additional free-space requirement.
  It credits bounded partial files or correctly sized complete blobs only at
  canonical private-store paths. Symlinks/path escapes are not credited.
- These are explicitly storage estimates, not integrity results. The worker
  still hashes cached data. A corrupt complete blob's replacement space is
  checked using its actual missing/partial bytes before any replacement request.
- The worker checks storage at admission, before each new/replacement payload,
  after restarting a server-ignored range, during streaming (at 64 MiB or
  one-second checkpoints), and before committing the model manifest.
- The atomic manifest needs a fresh allocation even when all blobs are cached.
- The setup card shows cached bytes and the estimated space needed for remaining
  data, and says cached files will be checked before reuse. No model availability
  is granted by these estimates.

## Evidence

`agent_model_download_test` now also covers:

- full-artifact cold admission and rejection before network activity when short
  of the required budget;
- a valid partial download completing with a free-space budget below the old
  full-model requirement;
- a correctly sized but corrupt complete cache passing only the size estimate,
  then being rejected for insufficient replacement space after hashing, before
  any blob request or manifest publication;
- storage becoming unavailable before the manifest commit, with verified blobs
  retained and no manifest published.

These disk budgets are injected into small native HTTP fixtures. Tests did not
fill the user's disk or download production weights; they are not physical
disk-full, unplugged-volume or 20.9 GB transfer benchmarks.

The desktop and focused targets build:
`/private/tmp/model-resume-storage-build.log`.
Seven focused tests pass in 44.05 seconds, including the complete agent-controller
test: `/private/tmp/model-resume-storage-tests.log`.
The isolated controller confirmation/pause/retry handoff passes:
`/private/tmp/model-resume-storage-controller.log`.
The setup-card runtime test covers cached and cold estimates and preserves its
error/retry assertions. Its inspected screenshot is
`/private/tmp/model-resume-storage-card.png`; focused `qmllint` is clean.

## Remaining boundaries

Cached metadata cannot predict corruption before hashing. A repair may therefore
require more space than the initial estimate; the worker reports that explicitly.
Other applications can consume space between checks, so file-write failures still
retain partial data and require retry. Physical disk loss, full multi-GB transfers,
verification/editing resource contention, managed inference, and the remaining
RAW/loading/3D/4D/editing requirements are not established by this work.

The request to reuse the existing external model payload remains unanswered.
No model weights were copied, downloaded or loaded in this continuation.
