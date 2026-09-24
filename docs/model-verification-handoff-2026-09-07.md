# Managed model verification handoff

This fixes one embedded-model readiness defect under the active app completion
goal. It does not establish installed-model inference or complete that goal.

## Defect and change

The verifier checks the model files on its worker before returning. Previously,
the UI completion handler trusted that result without checking whether the
files had changed while delivery was queued. It could publish a stale verified
model until a later inventory poll. A finished `QFuture` also ceased to count as
running before its completion handler consumed it.

The completion handler now checks the current pinned artifact fingerprint
before any successful readiness publication. Missing or changed files invalidate
the result and trigger another inventory verification on the next event-loop
turn. This remains a cheap metadata invalidation check, not another full hash
on the UI thread. It is not a defense against hostile filesystem mutation or
forged metadata; the existing full-file SHA-256 and containment checks remain
the integrity authority.

An explicit active flag covers both worker execution and queued completion, so
the watcher cannot be reused before its previous result is consumed. Starting
the worker before publishing its status also lets reentrant stop/install actions
cancel that actual worker. Old-generation completions remain excluded, and
pending replacement verification resumes without waiting for the next poll.

## Evidence

- A synthetic completed-worker result with now-missing pinned files reproduced
  the prior erroneous readiness publication. The test failed before the fix:
  `/private/tmp/model-readiness-before-test.log`.
- The runtime regression now passes and checks every readiness publication,
  not just final state. It also covers a completed-but-undelivered worker,
  installation invalidation, retry after installation, and stop invalidation.
  It uses no production model weights.
- The desktop app and focused targets built successfully:
  `/private/tmp/model-readiness-final-build.log`.
- Five focused tests passed in 3.54 seconds: managed runtime, artifact integrity,
  runtime paths, local model discovery, and model runtime bundle.
  `/private/tmp/model-readiness-final-tests.log`.
- The actual packaged Ollama binary passed asynchronous startup, empty private
  store readiness, and owned shutdown using a temporary store:
  `/private/tmp/model-readiness-live-runtime.log`.
- Initial sandboxed tests could not start loopback servers. The final runtime
  tests were rerun with loopback permission; no external model store was adopted.

## Remaining work

The packaged runtime still reports `modelsInstalled: false`. No weights were
copied, downloaded, or loaded in this work. Real provisioning, immutable download
transport, interrupted-transfer recovery, inference/search/edit quality and
latency, loaded-runner shutdown, and foreground editing under model load remain
open. See `managed-model-provisioning-2026-09-07.md` and
`active-app-completion-goal.md` for the broader gates.
