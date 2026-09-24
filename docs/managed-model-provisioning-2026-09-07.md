# Managed vision model provisioning

This continuation advances the embedded-model requirement of
`active-app-completion-goal.md`; it does not complete the RAW, loading, Gaussian,
editing-tool or calibrated agent-quality requirements.

This document records the initial provisioning implementation. The later native
transport and UI error handling supersede its managed CLI download path; see
`pinned-model-download-2026-09-07.md` and the updated remaining gates below.
Cache-aware storage admission was added afterward in
`model-resume-storage-2026-09-07.md`; the full-size-only admission description
below is historical.

## Implemented

- `agent_model_artifact.h` is the single application pin for Qwen3-VL 32B:
  exact raw manifest SHA-256
  `ff2e46876908853b8d0872595047c46d599d970056277e4864c8a1f0fd93fd3b`,
  plus weights, config, license and parameters with exact sizes and hashes.
  There are five unique files totaling 20,910,287,340 bytes. The registry repeats
  the same license layer; the pin verifies its one physical blob once.
- The public [Ollama manifest](https://registry.ollama.ai/v2/library/qwen3-vl/manifests/32b)
  and license hashes were checked directly. The
  [Qwen model card](https://huggingface.co/Qwen/Qwen3-VL-32B-Instruct) and
  [Ollama package page](https://ollama.com/library/qwen3-vl:32b) declare Apache-2.0.
- Local verification streams bounded chunks on a worker, verifies every file,
  checks sizes and path containment, rejects symlinks, detects file changes and
  supports cancellation. A successful result is cached only in memory with a
  size/mtime/change-time fingerprint. Every app session rehashes; a forged disk
  receipt is not trusted. This is not a defense against an attacker controlling
  the application's process or filesystem metadata.
- The managed server becoming ready and advertising a model name no longer
  makes the model available. The supervisor publishes `verifiedModels` only
  after local payload verification. Managed model discovery and probes fail
  closed while missing or unverified. Explicit external-server manifests retain
  their prior authority and are not represented as managed integrity proof.
- Managed installs explicitly invalidate model readiness before starting and
  require re-verification after completion or cancellation. This prevents the
  old ready flag from authorizing use while model files are replaced.
- Install plans expose artifact size, license, exact manifest digest, private
  destination and free space. Download requires the pinned size plus 1 GiB of
  headroom. This deliberately conservative check does not yet credit partial
  or already-cached blobs against required free space.
- The desktop launch is asynchronous and retains at most 64 KiB of installer
  output. Pause terminates only the owned CLI, including a pre-start cancellation
  race, with a bounded kill fallback. Explicit retry starts another request.
- The local-AI primary action now opens a focused confirmation card rather than
  immediately invoking confirmed setup. It shows size/license/storage, offers
  runtime retry, and supports download progress/pause. Closing and reopening the
  card during download remains possible. The confirmation is tied to the model
  shown on the card.

## Evidence

- `agent_model_artifact_test` checks all five payload roles, same-size corruption,
  missing license, symlink escape, traversal, cancellation and the production
  pin's byte count. No weights are required for this synthetic regression.
- The same verifier hashed the user's existing local model **read-only**:
  `/private/tmp/agent-model-existing-artifact-verification.json` reports `ok=true`,
  five files and all 20,910,287,340 bytes verified. No model was copied, downloaded,
  deleted or loaded by that test.
- Seven focused tests passed in 47.58 seconds before the final explicit
  install-state gate: artifact integrity, managed lifecycle, model discovery and
  probe gating, full agent controller (including cancellation/retry), runtime
  endpoint handling, and connected search/edit contracts.
  Log: `/private/tmp/agent-model-provisioning-tests.log`.
- Initial packaged setup proof:
  `/private/tmp/framehouse-model-setup-SE3lr1/receipt.json` confirms a required
  download confirmation, 20.9 GB pinned payload, approximately 22.0 GB required
  space, an owned loopback endpoint, and no installed model. Its screenshot
  caught the popup during its entry animation. The final proof must wait for
  the popup's `opened` state before judging its appearance.
- Final focused suite after the install/readiness exclusion gate: **7/7 pass**,
  47.01 seconds, `/private/tmp/agent-model-provisioning-final-tests.log`.
  The new setup component also passes focused `qmllint` with no output.
- Final rebuilt/AOT proof:
  `/private/tmp/framehouse-model-setup-KP56H7/receipt.json` and `startup.png`.
  Independent assertions pass for owned runtime shutdown, no verified model,
  explicit confirmation, exact byte count and an installable storage plan. The
  inspected screenshot shows the fully open, unobstructed confirmation card.
  Only this isolated QA profile marks onboarding complete; the user's preferences
  are unchanged. The test does not press Download or exercise inference.

Reproduce the isolated native/AOT setup-card proof (does not press Download):

```sh
build/release-macos/apps/desktop/Framehouse.app/Contents/MacOS/Framehouse \
  --managed-runtime-proof-dir /absolute/new/proof \
  --managed-runtime-setup-proof
```

## Remaining gates

1. Real managed-store provisioning and packaged inference/search/planning/review
   are not yet proven. Permission was requested to reuse the verified local
   payload using private copy-on-write clones; no copy was made in this work.
2. The managed transport was subsequently replaced by the native pinned-manifest
   and digest-blob downloader in `pinned-model-download-2026-09-07.md`. It rejects
   a changed manifest before any blob transfer; the external-server CLI path is
   unchanged. The manifest endpoint itself remains tagged, with exact content
   verification, because the registry's digest endpoint returned 404.
3. Native cancellation/resume now has synthetic HTTP and small real-registry
   evidence, recorded in that follow-up. A real interrupted multi-GB transfer
   and inference after full provisioning remain unproven.
4. Measure full-hash verification latency/progress and resource contention while
   editing. Background hashing is not itself a proof of responsive startup.
5. Loaded-runner shutdown, parent hard-crash recovery, release packaging/signing,
   model sizing and photographic quality/latency remain open.
