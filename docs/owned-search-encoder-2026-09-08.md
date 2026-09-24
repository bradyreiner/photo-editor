# Native-owned persistent search encoder

## Status

The approved SigLIP 2 model now runs behind a reusable native-owned stdio worker.
Production compilation, controlled failure cases and real model-to-native-index
tests pass. This is **not yet connected to Find photos or background catalog
indexing**, and the Python environment/model remain in the isolated test store.
No additional model download or chat-model copy was performed.

## Implementation

- `LocalImageTextEncoder` owns exactly its QProcess child. Startup and interactive
  stop do not wait on model loading/shutdown. Stop terminates that child and kills
  it after 500 ms if necessary. Final object destruction has bounded ownership
  cleanup (up to one second); it is not a claimed nonblocking teardown path.
- The client takes explicit Python, trusted worker-script and local model paths.
  It preserves the virtualenv executable path rather than resolving its symlink
  to the system Python. It uses isolated Python mode, filters Python/hub/injection
  environment variables, and starts no network listener or external service.
- The serial protocol admits one request at a time. Busy requests are rejected,
  not silently queued. Startup is bounded to 120 s, requests to 30 s, stdout to
  128 KiB and retained stderr to 16 KiB. The worker bounds incoming lines to
  64 KiB and image byte snapshots to 64 MiB.
- Native readiness validation checks protocol, pinned revision, device, dimension
  and fingerprint format. Replies must match the current request ID, kind and
  model space, with exactly 768 finite normalized components and bounded fields.
  This trusts the configured app worker code; it is not attestation of an arbitrary
  interpreter/script selected by an untrusted caller.
- Cancel is result suppression, not preemption of an in-flight GPU kernel. It
  immediately emits cancellation but retains the occupied slot until the result
  drains or the deadline stops the worker. A canceled result cannot become a
  current result, and no request is automatically replayed after failure/restart.
- Ordinary per-request errors release the slot and retain the loaded model.
  Malformed/unsolicited replies, output overflow, startup failure, timeout and
  unexpected exit fail explicitly and stop only the owned child.
- `scripts/search_model_runtime.py` is shared by the worker and proof CLI. It
  verifies the pinned model files, disables implicit hub/network access and
  remote code, enforces six version-pinned numerical/tokenizer dependencies,
  and fingerprints the artifacts, versions, preprocessing and provider.
  The new `siglip2-photo-retrieval-v1` identity intentionally differs from the
  earlier standalone proof schema; no user index relied on that old namespace.
- One bounded source-byte snapshot feeds image decode and content hashing.
  Returned vectors therefore describe that snapshot, not a second read of a
  possibly replaced file. This alone is not current-catalog authority: the future
  coordinator must reject superseded asset/recipe/query generations and combine
  the model fingerprint with the selected catalog namespace.

The worker performs no catalog writes. Native tests explicitly pass its real
vectors through `VisualEmbeddingIndex`; that store remains a blocking worker API
for the eventual coordinator. The tiny test's synchronous index calls are not
the production background-indexing implementation.

## Evidence

All artifact paths below are under `/private/tmp/framehouse-siglip2-O2ha9I/`.

- `worker-regression.log`: initial native controlled-worker suite passes.
  It exercises readiness, repeated model use, cancellation/backpressure,
  heartbeats, request-error recovery, invalid dimensions/normalization,
  wrong request IDs, stdout overflow, unexpected exit and request/startup deadlines.
- `tests/test_search_model_worker.py` adds no-download Python protocol tests:
  malformed/oversized/incomplete frames, unknown fields and invalid IDs/payloads
  fail before inference; ordinary request errors do not replay or poison the
  following request.
- `owned-worker-live.json`: initial real MPS native proof passes with five images
  and ten queries using one process, expected first results, persistent-vector
  reuse after a different owned process starts, and both processes stopped.
- `owned-worker-pinned-final.json`: final proof additionally checks real
  cancellation, dependency identity and expected retrieval after restart.
  Startup admission and cancellation admission each measure 0 ms at millisecond
  resolution. Startup takes 4,411 ms with 440 native 10 ms heartbeat ticks.
  All five images are encoded once before ten queries, and all queries rank an
  expected fixture first. Warm native text-request plus five-image index retrieval
  takes **11, 12, 12, 12, 12 ms**. A canceled real reply is suppressed. After
  restart, the fingerprint is unchanged and all five persisted vectors remain
  usable without re-encoding. Both owned processes stop successfully.
- `shared-runtime-cpu.json` and `shared-runtime-cpu-native.json`: the refactored
  standalone proof now uses the same shared runtime and passes actual CPU
  inference, expected ranking and full native/dot-product ranking parity.
- `worker-pinned-final-regressions.log`: all eight focused suites pass in
  9.13 s (installer, worker protocol, artifact verification, managed chat runtime,
  visual search, vector index, native encoder client and search/edit workflow).
- `owned-worker-app-build.log` and `worker-pinned-final-build.log`: production
  desktop and native test compile/link successfully. All owned builds, tests
  and model-worker processes from these proofs are terminal.

The five image inputs cover four scenes, including two renderings of the same
portrait; one is an existing RAW-derived preview, not a new RAW decode. Fixture
sources and model provenance remain in `siglip-local-retrieval-proof-2026-09-08.md`.
These measurements are not full-library retrieval, frame timing or a QML workflow.
Model verification/loading/warm-up remain real startup costs. This does not
replace the fine-detail/negation verification stage or establish calibrated
photographic recall, hardware coverage or packaged offline deployment.

## Reproduce the owned-worker proof

```sh
build/dev-macos-debug/tests/local_image_text_encoder_test --live \
  /private/tmp/framehouse-siglip2-O2ha9I/venv/bin/python \
  /private/tmp/framehouse-siglip2-O2ha9I/model \
  /private/tmp/framehouse-siglip2-O2ha9I/fixtures.json mps
```

The next implementation is the background catalog coordinator: authority-scoped
indexing, source/recipe/delete/relink invalidation, bounded scheduling, shutdown
coordination and visible progress/errors. Then connect retrieval to Find photos
and verify actual native UI behavior on a larger labeled corpus. The full RAW,
loading/export, 3D/4D, managed chat/agent and professional-tool requirements remain
open wherever their independent completion evidence is missing.
