# Native pinned model download

This replaces the managed runtime's mutable-tag CLI pull with the app's own
bounded, resumable transport. Explicitly configured external Ollama servers
retain their existing CLI install path. This advances embedded-model setup;
it does not prove installed-model inference or complete the active app goal.

## Protocol and implementation

The registry's digest-addressed manifest URL returned HTTP 404 during this work.
The pinned runtime's [name parser](https://github.com/ollama/ollama/blob/v0.33.3/types/model/name.go)
also does not implement digest-based names, and its
[pull implementation](https://github.com/ollama/ollama/blob/v0.33.3/server/images.go)
fetches the named tag. Appending a digest to the CLI command is therefore not a
verified solution for this bundled version.

The new `agent_model_download.h` transport instead:

1. Fetches only the small named manifest, bounded to its exact pinned byte count.
2. Checks its SHA-256 against the application pin and validates that every
   config/layer reference agrees with the pinned file list. No blob request
   precedes this check; repeated references to a single physical blob are valid.
3. Requests only pinned `/blobs/sha256:<digest>` resources. Production transport
   uses HTTPS, normal TLS verification, no proxy/cookies/credentials, and only
   same-registry or HTTPS R2 storage redirects, with a bounded redirect count.
4. Streams bounded chunks on a worker, with pinned size and full SHA-256 checks.
   Unknown-length responses still cannot exceed the pinned byte budget.
5. Rehashes existing complete/partial files on the worker. Valid complete blobs
   need no network transfer. Partial files resume using an exact byte range;
   if a server returns the full object instead, the partial/hash restart safely.
6. Publishes each completed blob and atomically publishes the named manifest
   last. The managed runtime still performs its independent full-file verification
   before exposing the model as available.

Pause aborts the actual owned network request. Retry retains incomplete cache
data and obtains fresh registry responses; no signed redirect URL is persisted.
The worker has a 30-second inactivity timeout. UI progress is coalesced at 100 ms,
and QObject destruction cancels without waiting for network completion. A pause
after the manifest commit point does not relabel an installed artifact as failed.

Download ownership uses a private-store lock. Existing symlink paths and traversal
are rejected. A replacement corrupt final file is preserved with a unique
`.rejected-<uuid>` suffix before its verified replacement is published. Only a
failed-hash app-owned `.part` cache is discarded for an explicit retry. This is
not protection against an attacker concurrently controlling the local filesystem.

The controller retains explicit confirmation, excludes concurrent installs,
balances managed readiness invalidation across completion/pause, and no longer
requires a CLI executable for the managed download. The setup card now shows the
actual failed-download reason, clears it on retry, and excludes failures belonging
to a different selected model.

## Evidence

- Final rebuilt desktop and focused targets pass:
  `/private/tmp/pinned-model-final-accepted-build.log`. Seven focused tests pass
  in 43.68 seconds, including the full agent controller and rendered setup card:
  `/private/tmp/pinned-model-final-accepted-tests.log`. Final transport case output
  is in `/private/tmp/pinned-model-transport-final.log`.
- Synthetic HTTP fixture tests cover changed manifests before blob transfer,
  exact full-artifact verification, interruption/resume, ignored and incorrect
  ranges, corrupt hashes, declared and chunked oversize responses, encoded
  payload rejection, unsafe/looping redirects, inactivity timeout, existing-blob
  reuse, corrupt-file preservation, symlink/traversal rejection, store ownership,
  pre-start pause, actual request cancellation, and nonblocking destruction.
- Real-registry preflight used the production native transport on a worker:
  `/private/tmp/pinned-model-registry-preflight.json`. It verifies the manifest
  SHA-256 plus config, license and parameters, and retries the parameters blob
  from byte five. **12,881 public bytes** were read. No weight blob, installation,
  private model store, image, or inference request was involved.
- An isolated controller handoff proof verifies native transport selection,
  confirmation, pre-start pause, explicit retry and balanced readiness gates,
  with an intentionally unavailable CLI executable. Its private model store
  remains empty: `/private/tmp/pinned-model-controller-handoff.log`.
- The setup-card runtime test checks the visible failure reason, retry action and
  cross-model error exclusion. Its rendered screenshot was inspected:
  `/private/tmp/pinned-model-download-error.png`. This is an isolated QML fixture,
  not foreground performance evidence. Focused `qmllint` is clean.

Reproduce the read-only network preflight (small public metadata only):

```sh
build/release-macos/tests/agent_model_download_test --registry-preflight
```

Reproduce the controller admission/pause proof (no network or weights; requires
the normal approximately 22 GB free-space admission check to pass):

```sh
build/release-macos/tests/agent_edit_controller_test --managed-download-handoff
```

## Remaining gates

- The full 20.9 GB managed model is still not installed. Real multi-GB interrupted
  transfer and loaded inference are not proven by small HTTP fixtures/metadata.
- Cache-aware admission and worker-side replacement/storage checks were added
  in `model-resume-storage-2026-09-07.md`. Cache sizes are estimates, not integrity
  proof; corrupt cache may need more repair space. Physical disk-full/volume-loss
  and multi-GB resume evidence remain open.
- Measure hashing, transfer, startup and editing contention on target hardware.
  Background work alone is not proof of responsive foreground editing.
- Full managed inference/search/edit quality, loaded-runner shutdown, release
  packaging/signing, RAW/color, loading, 3D/4D and professional editing gates remain
  in `active-app-completion-goal.md`.
