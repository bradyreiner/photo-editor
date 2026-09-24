# Embedded local-agent runtime

The requested end state is a managed local vision model that works in the
packaged app without installing or starting a separate service. This page tracks
the work toward that state; a vendored executable alone is not completion.

## Implemented boundary

- `apps/desktop/models/agent-runtime.lock.json` pins the official macOS Ollama
  0.33.3 archive, byte size, SHA-256, MIT license, and extracted-file inventory.
  The archive is 159,236,337 bytes and contains the runtime and dependent libraries,
  not model weights. Sources: [official release](https://github.com/ollama/ollama/releases/tag/v0.33.3)
  and [license](https://github.com/ollama/ollama/blob/v0.33.3/LICENSE).
- `scripts/prepare-agent-runtime.py` verifies before extraction, limits extracted
  size, rejects unsafe archive entries, stages atomically into a new directory,
  preserves all dependency licenses, and refuses to overwrite an existing output.
  Its verifier compares the actual inventory against the checked-in lock, not
  merely a self-authored installation receipt. Extra files and modified binaries
  with forged receipts fail verification. The current pin supports macOS only.
- `PHOTO_EDITOR_AGENT_RUNTIME_DIR` enables verified bundling into
  `Framehouse.app/Contents/Resources/agent-runtime`. CMake verifies at configure,
  before copying, and after copying. Release re-signing/notarization needs its own
  packaged-artifact proof; these are pre-sign upstream-integrity checks.
- Runtime discovery honors an explicit `PHOTO_EDITOR_OLLAMA_BINARY` first,
  otherwise prefers the bundled executable, then an external PATH installation.
  An invalid explicit override fails instead of silently switching runtimes.
- Model installation validates the selected loopback HTTP endpoint and passes it
  as `OLLAMA_HOST` to the child process. Previously the CLI ignored the selected
  endpoint and inherited its destination from the shell. The child disables cloud
  features/request logging and removes HTTP/all-proxy redirection; the parent
  environment and existing service remain unchanged. Ollama's settings are
  documented in the [official FAQ](https://docs.ollama.com/faq).

## Reproduce staging

```sh
python3 scripts/prepare-agent-runtime.py --download --output /absolute/new/runtime
python3 scripts/prepare-agent-runtime.py --verify-only --output /absolute/new/runtime
cmake --preset dev-macos-debug -DPHOTO_EDITOR_AGENT_RUNTIME_DIR=/absolute/new/runtime
cmake --build --preset build-dev-macos-debug
```

Downloads are explicit. No model weights, user catalog, original photos, installed
Ollama application, or existing server are replaced by staging.

Current durable input: `.photo-editor/agent-runtime/0.33.3`, 58 files/links; the
release-macos build cache points to this workspace-local directory rather than
temporary storage. The original archive SHA-256 was checked against the official
GitHub release asset digest. `scripts/test-prepare-agent-runtime.py` covers intact
staging, links, corrupted archives, pre-existing-output preservation, archive
escape, extra payloads, and a forged receipt. `agent_runtime_paths_test` covers
endpoint validation, explicit executable authority and actual child environment.

The final app build passes upstream verification before and after bundle copy.
Verification caught CMake's `copy_directory` expanding symlinks into duplicate
binaries; the copy now preserves the original relative links with `file(COPY)`.
All six selected runtime/local-model/controller/search contract tests pass
(`43.55 s`, `/private/tmp/framehouse-agent-runtime-final-tests.log`).

The executable **inside the built app** was also started and stopped manually
on an isolated unused loopback port (11435), using an empty temporary model store.
It reported version 0.33.3, `Ollama cloud disabled: true`, and empty `/api/tags`
and `/api/ps` lists. The owned test process exited successfully after interrupt;
the separate external service on 11434 was not stopped. Log:
`/private/tmp/framehouse-agent-runtime-DwP5Hh/bundled-server.log`. This proves
bundled executable startup/readiness, not automatic app-owned lifecycle or
model inference. No model weights were downloaded or loaded by this proof.

## App-owned lifecycle (Sept 7 continuation)

`ManagedAgentRuntime` now starts the pinned bundled executable asynchronously on
an app-owned loopback port, using `AppData/agent-runtime/models` and an exclusive
model-store lock. It disables cloud execution, prunes no weights, bounds server
concurrency and retained diagnostics, and publishes readiness only after the
owned child announces its listening address and passes version/inventory checks.
It does not adopt an existing port or service. Health failures invalidate the
endpoint; explicit retry starts a new owned process without replaying requests.
Normal shutdown terminates only its child, with a bounded kill fallback.

The bundled model manifest selects this managed runtime. Explicit disk manifests
keep their external endpoints. Missing weights are unavailable with a download
instruction; they do not silently select the deterministic rulebook. Setup now
distinguishes server readiness from model installation. No weights have been
downloaded or copied by this lifecycle work.

Focused fake-server tests cover asynchronous readiness, endpoint/version checks,
port collision, private-store ownership, refusal to trust a response without an
owned listening announcement, child failure and explicit retry. The same
supervisor also passes a live empty-store test against the actual bundled binary
(`/private/tmp/managed-agent-runtime-live.log`).

The first actual packaged/AOT app proof failed: its unavailable isolated catalog
caused five consecutive five-second `catalog_export_jobs list` waits on the UI
thread. An explicit database URL was mistaken for proven readiness. The app now
checks catalog readability on the bootstrap worker before allowing that startup
fan-out; this does not make the underlying synchronous job-store APIs async.
Startup and failure also publish loading/error state without clearing assets or
switching catalog authority.

After the readiness guard, packaged proof
`/private/tmp/framehouse-managed-app-auGdJo/receipt.json` passed owned runtime
startup/readiness/shutdown with an empty private store and unavailable database.
It confirmed `ollama_reachable=true`, `selected_model_installed=false`, and no
owned process after stop. Its screenshot exposed a separate premature empty/
healthy catalog display; the explicit catalog startup/error states address that
display, with a new packaged proof required below. Eight focused regressions
passed in 48.90 seconds (`/private/tmp/managed-runtime-readiness-tests.log`), before
the final catalog-state change.

The final catalog-state packaged proof is
`/private/tmp/framehouse-managed-app-z9Aqnz/receipt.json` and `startup.png`.
Its inspected screenshot shows Library Error, the originals-preserved recovery
message and a red catalog indicator. The receipt passes independent assertions
for runtime readiness, empty model inventory, stopped ownership, visible catalog
failure, server-ready/model-missing setup checks and continued event-loop ticks.
It completed its proof in 5,829 ms with 11 timer ticks; the largest gap was
1,191 ms. That is not realtime performance and still warrants follow-up. The
baseline's repeated five-second main-thread waits are gone from this outage
path. The app exited successfully. Pre-existing QML undefined-property warnings
remain in the packaged log; this is not a clean-warning or normal-catalog proof.
An independent post-exit connection check confirmed the owned port (52718) was
closed while the separate pre-existing server on 11434 still responded.

The broader nine-test run then passed eight tests but failed the existing
managed-import receipt assertion in `catalog_asset_model_test`. The real worker
reported `backedUpCount=1` and `verificationAlgorithm=SHA-256`; the desktop mapper
discarded those fields. `JobRunner` now preserves the worker's backup count,
reused count, verification algorithm and destination instead of presenting only
the preflight intent. The unprivileged isolated rerun passed via the test's
permissive helper-failure path and is not evidence of successful managed import;
the final helper-backed rerun is required for this fix.

That helper-backed rerun passed all nine selected tests in 71.68 seconds:
`/private/tmp/managed-runtime-final-validation.log`. This includes the actual
managed-import backup receipt assertion, full agent controller and new catalog
startup-state assertions. A concurrent packaged run failed initial health checks
(`/private/tmp/framehouse-managed-app-Y4ztAX/receipt.json`); the isolated rerun
passed (`/private/tmp/framehouse-managed-app-zyEfyN/receipt.json`). Contention is
not established as the sole cause by those two runs.

A focused transient-503 regression then independently reproduced a startup
policy defect: three failed probes terminated initialization before the existing
15-second startup deadline. The supervisor now lets that deadline govern initial
readiness and retains three-failure health shutdown for an already-ready server.
The new regression failed before the fix and passes afterward with the full
managed-runtime test (3.65 seconds,
`/private/tmp/managed-runtime-warmup-fixed-test.log`). The deadline was not
increased. Health errors now retain numeric HTTP/network diagnostics without
exposing response bodies or child environment logs.

Final rebuilt/AOT proof after that fix:
`/private/tmp/framehouse-managed-app-Xq8EVC/receipt.json` and `startup.png`.
The assertions pass, the inspected screenshot shows the catalog failure, and the
app exits successfully after owned-runtime shutdown. Elapsed proof time is
5,980 ms, 11 timer ticks, maximum gap 1,180 ms. This remains empty-store lifecycle
and outage-state evidence, not loaded-model inference or realtime performance.

Reproduce the isolated packaged proof with a new absolute directory:

```sh
build/release-macos/apps/desktop/Framehouse.app/Contents/MacOS/Framehouse \
  --managed-runtime-proof-dir /absolute/new/proof
```

This QA mode isolates app data, cache, preferences, and model storage; it selects
an intentionally unavailable database, captures the real app window and setup
checks, then stops the owned runtime and writes `receipt.json`. It does not
prove normal-catalog performance or model inference.

## Required continuation

The next provisioning slice is now implemented and tracked in
`managed-model-provisioning-2026-09-07.md`: exact payload verification before
managed readiness, explicit download/storage confirmation, asynchronous CLI
pause/retry and a verified native setup-card proof. Tagged transfer, real network
resume and private-store inference remain open; this does not close every item
below.

1. Prove loaded-runner shutdown and parent hard-crash/orphan behavior, not only
   graceful empty-store shutdown. Exercise multi-instance/restart behavior with
   real loaded weights and release signing/notarization.
2. Remove remaining synchronous catalog/job/history calls from interactive paths
   and verify retry/recovery under a real catalog outage. The startup guard is
   deliberately narrower than that refactor.
3. Provision a licensed, checksum-pinned vision model through explicit download
   confirmation, with cancel/resume, disk-space checks and honest capability state.
   Do not silently replace a selected model or copy a multi-GB cache.
4. Prove a clean packaged launch, real image search/planning/review, process exit,
   offline/restart/cancellation and memory/latency behavior. Choose model sizing
   from measured responsiveness and photographic quality, not installed size alone.

First-review latency, resource contention with rendering, Gaussian reconstruction,
and the wider RAW/editing completion gates remain separate active requirements.
