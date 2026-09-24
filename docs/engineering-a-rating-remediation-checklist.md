# Engineering A-Rating Remediation Checklist

Audit baseline: 2026-09-02

This document is the engineering completion contract for moving Framehouse from
the current audit result to an A rating in every category. It covers repository
hygiene, build reproducibility, data integrity, security, persistence,
concurrency, performance, architecture, UI quality, testing, operations, and
release engineering.

An item is not complete because code was written. It is complete only when the
required automated, runtime, failure-path, and human evidence exists.

## Status legend

- `[x]` Verified in the current repository or by the 2026-09-02 audit.
- `[~]` Partially implemented or verified, with a material gap still open.
- `[ ]` Not implemented, not verified, or blocked by external evidence.

## A-rating rules

Every category receives an A only when all of the following are true:

- [ ] Every category-specific release blocker is closed.
- [ ] Every required automated gate runs from a clean checkout in CI.
- [ ] Required failure-injection and recovery tests pass.
- [ ] Required performance budgets pass on declared hardware tiers.
- [ ] Required visual, accessibility, color, and photographer reviews are recorded.
- [ ] There are no unexplained skips, quarantined failures, or ignored warnings.
- [ ] Evidence identifies the commit, platform, toolchain, command, result, and artifact.
- [ ] Any temporary exception has an owner, issue, expiry date, and bounded risk statement.

Passing a broad suite does not override a failed focused gate. A category with an
open critical or high-severity item cannot score above C. A release candidate
with an unreproducible clean checkout cannot receive an overall passing rating.

## Checklist authority and routing

This checklist is the engineering roll-up and release-quality contract. It must
link to existing domain evidence instead of copying detailed product backlogs or
creating a second implementation plan for the same capability.

| Concern | Canonical detail source | This checklist owns |
| --- | --- | --- |
| One-time launch, credentials, legal, and clean machines | [production-launch-checklist.md](production-launch-checklist.md) | Engineering readiness roll-up |
| Per-version candidate and publishing sequence | [release-checklist.md](release-checklist.md) | Quality gates required before release |
| Performance measurement expansion | [performance-measurement-expansion-checklist.md](performance-measurement-expansion-checklist.md) | A-rating budgets and final evidence |
| Product UX and human acceptance | [user-experience-improvement-checklist.md](user-experience-improvement-checklist.md) | Engineering and accessibility gates |
| Local agent experience and calibration | [local-model-agent-ux-checklist.md](local-model-agent-ux-checklist.md) | Security, reliability, and proof roll-up |
| Color implementation and validation | [color-engine-and-app-foundations-completion-strategy.md](color-engine-and-app-foundations-completion-strategy.md) | Color A-rating exit decision |

- [~] Add a stable item ID to work tracked across more than one checklist. The ownership validator now recognizes
  `<!-- work-id: AREA-NNN -->` markers and rejects duplicate claims; existing repeated items still need migration.
- [~] Give each item exactly one canonical owner document; other documents link to it. All 44 top-level tracking
  documents now resolve to one active scope owner, but copied item-level status prose still needs replacement.
- [ ] Replace copied status prose with links to immutable reports or the canonical item.
- [~] Audit all existing checklists for duplicate, contradictory, obsolete, or orphaned work. The complete 44-file
  inventory is classified as active, supporting, historical, parked, or complete; item-level semantic consolidation
  remains open.
- [x] Publish a checklist index mapping each product/engineering area to its canonical document. The human routing
  table and machine-readable 44-document registry are `docs/checklist-index.md` and `docs/checklist-index.json`.
- [x] Fail documentation validation when two active items claim ownership of the same stable ID. CI now rejects
  duplicate document IDs, duplicate work IDs, unindexed trackers, missing owners, and non-active canonical owners.

## Mandatory reuse-before-build gate

Complete this gate before implementing any item below. It applies to code,
functionality, UI, schemas, services, tests, scripts, documentation, and planned
work. Literal copy detection alone is insufficient; two differently written
paths that perform the same user operation are also duplication.

- [ ] Search existing symbols, files, QML actions, routes, commands, schemas, migrations,
  tests, scripts, and checklists for the requested capability.
- [ ] Identify the current canonical owner from UI command through controller, domain service,
  persistence, render/export consumer, and tests.
- [ ] Record one decision: `reuse`, `extend`, `consolidate`, `replace`, or `new capability`.
- [ ] Explain why an existing abstraction cannot own the work before adding a parallel implementation.
- [ ] Identify all current consumers before changing or replacing shared behavior.
- [ ] Check whether the proposed work duplicates a feature under a different label, workspace,
  controller, command, recipe field, or storage representation.
- [ ] Check active issues, branches, pull requests, and checklist IDs for overlapping in-progress work.
- [ ] Assign one implementation owner and one canonical issue before work begins.
- [ ] Reuse existing fixtures, generators, adapters, design components, and test helpers where their
  semantics match; do not create local near-copies for convenience.
- [ ] Keep intentional platform/backend variants behind one interface and one conformance suite.
- [ ] Add characterization tests before consolidating paths whose behavior is not already explicit.
- [ ] Delete the replaced path, migrate its consumers, and remove stale tests/docs in the same bounded
  change or track a dated removal issue.
- [ ] Verify consolidation does not create a generic abstraction that mixes unrelated semantics.
- [ ] Attach search evidence, ownership decision, affected consumers, and deletion count to review.

### No-duplication A exit gate

- [ ] A versioned capability inventory maps every shipped user action to one canonical domain owner.
- [ ] No shipped capability has competing controllers, services, schemas, or persistence authorities.
- [ ] Intentional backend/platform implementations pass the same contract tests and are not divergent forks.
- [ ] Duplicate-code, dead-code, and dependency reports contain no unexplained production finding.
- [ ] Active engineering work has one owner and cannot be unknowingly implemented twice.

## Current baseline

- [x] Desktop application target builds on the audited macOS host.
- [x] Headless native suite passes on the audited host.
- [x] Rust formatting passes in the audited working tree.
- [x] Rust workspace tests pass.
- [x] Rust Clippy passes with `-D warnings` for the production workspace. (2026-09-02:
  `cargo clippy --workspace --all-targets --locked -- -D warnings` passed after excluding local-only MCP tooling.)
- [~] A `git archive HEAD` checkout loads and builds every declared workspace member. A tracked-only
  481-file export resolves locked Cargo metadata after the manifest repair; hosted full-build proof remains open.
- [ ] The non-monolithic macOS desktop suite passes without failures or crashes.
- [ ] The supported LaMa inference latency gate passes on the audited hardware tier.
- [ ] The 10,000-clip timeline projection completes within its 1.5-second budget.
- [x] Architecture boundaries pass on the current working tree. Histogram ownership was extracted from
  `editor_preview_controller.cpp` (39,106 lines plus a focused 199-line implementation), crop math was extracted from
  `photo_render_utils.cpp` (3,262 lines), local fast-path policy was extracted from
  `color_local_layer_runtime.cpp` (754 lines), and input white-balance math was extracted from
  `color_input_runtime.cpp` (593 lines). The gate enforces dependency direction and canonical ownership, not file length.
- [~] QML lint runs, but Qt 6.10.2 currently reports 3,706 warnings after enabling bound component
  behavior in the largest delegate-heavy files and removing every layout-positioning warning.
- [~] The working tree contains 1,109 changed entries (327 tracked changes and 782 untracked paths) and is not a
  release-candidate baseline.

# Phase 0: Stop-the-line release blockers

Do not publish a production candidate until every item in this phase is complete.

## 0.1 Protect originals during import

- [x] Define one canonical `ImportDestinationPlan` owned by the import domain. (2026-09-02:
  Rust import planning now publishes the canonical source, relative, managed, backup, and metadata plan.)
- [ ] Use that same planner in C++ preflight, Rust execution, UI summaries, and tests.
- [x] Include organization template, normalized source-relative path, sanitized camera/date
  components, collision suffix, managed root, and backup root in the plan.
- [x] Reject selected source paths that resolve outside the canonical import root.
- [x] Add duplicate policy to the Rust `ImportRequest` as a typed enum.
- [x] Enforce `skip`, `reuse-identical`, and `replace` in the Rust worker rather than only in UI preflight.
- [x] Make `skip` incapable of writing, replacing, truncating, or deleting an existing destination.
- [x] Make `reuse-identical` compare verified content identity before catalog publication.
- [~] Make `replace` preserve the old destination until the verified replacement can be atomically published.
  POSIX uses atomic replacement; the Windows recovery path preserves bytes but still needs true atomic replace proof.
- [~] Eliminate the remove-then-rename gap for existing destinations. The destructive POSIX gap is removed;
  Windows still needs a platform-native atomic replacement implementation and failure proof.
- [ ] Preserve metadata, permissions, and durability guarantees required by each supported platform.
- [x] Ensure backup source identity refers to the original source, not an already replaced managed copy.
- [ ] Reject configurations where managed and backup roots alias through symlinks, case folding, or volume aliases.
- [ ] Reserve destination names atomically so concurrent imports cannot select the same output path.
- [ ] Record source hash, destination hash, policy, planner version, and outcome in the import receipt.
- [~] Add tests for source-folder and date-camera organization under every duplicate policy. Source-folder
  reuse and date-camera skip/replace are covered; the complete cross-product remains open.
- [~] Add same-name/same-content and same-name/different-content collision tests. Focused worker cases exist;
  catalog/preflight, case-folded, and cross-platform cases remain open.
- [ ] Add RAW+JPEG, case-insensitive filename, Unicode-normalization, and symlink collision tests.
- [ ] Inject read failure, disk-full, short copy, hash mismatch, rename failure, process crash, and app shutdown.
- [ ] Prove every injected failure leaves the previous original byte-for-byte intact.
- [ ] Prove retry is idempotent and does not create duplicate catalog records.
- [ ] Run a removable-drive end-to-end import, disconnect, reconnect, backup, and recovery proof.

### Import A exit gate

- [ ] No supported import mode can destroy or silently replace an original without explicit replace intent.
- [ ] Preflight and worker destination plans are byte-for-byte identical in contract tests.
- [ ] The failure matrix passes on APFS, a case-insensitive volume, and supported Windows storage.
- [ ] A restore drill recovers a deliberately interrupted managed import without manual database repair.

## 0.2 Restore clean-checkout reproducibility

- [x] Decide whether `rust/crates/mcp` is production source or local-only tooling. (2026-09-02:
  it is repository-development tooling; the production app owns automation in the tracked C++ bridge/server.)
- [x] If production source, track the full crate and remove its `.gitignore` and CI rejection rules.
  Not applicable after the explicit local-only ownership decision.
- [x] If local-only, remove it from the committed Cargo workspace and gate all integrations behind an explicit option.
  It is now an excluded standalone crate, and the tracked production tree has no integration reference.
- [x] Apply the same source-versus-local decision to `scripts/ai` and `plugins/photo-editor-codex`.
  Both remain ignored local-only tools with no tracked build, packaging, or runtime reference.
- [x] Add a CI job that exports `HEAD` with `git archive` into an empty directory.
- [~] Run Cargo metadata, locked Rust build/test, CMake configure/build/test, and packaging inventory in that export.
  The clean-source job now runs both build/test graphs and records the CMake target inventory; final
  platform-package content inventory remains owned by the macOS and Windows release jobs.
- [x] Fail CI when a tracked manifest references ignored or untracked source.
- [~] Fail CI when generated source is required but its generator and inputs are not tracked. Clean-source
  configure/build catches required missing generated source; a complete generator/input registry audit remains open.
- [x] Document every generated artifact, generator, source input, and regeneration command in
  `docs/source-reproducibility.md`.
- [~] Prove no developer-global configuration, Homebrew-only path, untracked model, or prior build output is required.
  The Ubuntu empty-export job is isolated from the developer tree; the new workflow still needs a hosted CI run.

### Reproducibility A exit gate

- [ ] A clean, network-controlled checkout builds twice with the locked dependencies and identical source inputs.
- [ ] All declared targets work without copying files from an existing developer tree.
- [~] CI publishes the source inventory and dependency lock evidence for each release candidate. The workflow and
  SHA-256 inventory are implemented; release-candidate artifact retention and a hosted run still require proof.

## 0.3 Secure the automation and MCP boundary

- [x] Disable the MCP HTTP listener unless automation is explicitly enabled by the user or launch policy.
  The default launch keeps HTTP off; `--enable-automation` or `PHOTO_EDITOR_ENABLE_AUTOMATION` opts in.
- [~] Require a high-entropy capability token on GET, SSE, POST, discovery, and tool calls. A new 256-bit
  bearer is required by every accepted HTTP route; the network assertions need a non-sandboxed CI run.
- [x] Use constant-time credential comparison where practical for both bearer and socket requests.
- [x] Never place tokens in logs, error messages, telemetry, process arguments, or shared URLs.
- [x] Create separate read-only, draft-mutation, export, and commit capabilities through one canonical
  `automation_capability_policy.h` used by discovery, MCP invocation, and the direct automation bridge.
- [x] Default new sessions to the minimum read-only capability.
- [ ] Require a real app-owned approval receipt for commit, export, destructive repair, and other sensitive actions.
- [ ] Reject caller-supplied `confirmed: true` without a matching unexpired approval receipt.
- [ ] Bind approval to session, project, asset scope, exact operation, exact arguments, and expiry time.
- [ ] Consume one-shot approvals atomically and reject replay.
- [~] Rotate the session token on restart, disable, permission change, and suspected compromise. Start/stop
  rotation is implemented; interactive permission changes and compromise recovery remain open.
- [x] Apply strict Origin and Host validation as defense in depth, not as authentication.
- [~] Add connection, request-rate, concurrent-call, header-size, body-size, JSON-depth, and execution-time limits.
  All listed limits except a hard execution deadline are enforced at the HTTP boundary.
- [~] Ensure session files and local sockets are owner-only from creation. Owner-only directory, `QSaveFile`,
  final-file, and `QLocalServer::UserAccessOption` policy are implemented; Windows ACL proof remains open.
- [x] Document the local threat model, including browsers, sandboxed applications,
  other user processes, and remote helpers.
- [~] Add tests for missing/invalid/expired/replayed tokens and insufficient capabilities. Missing/invalid and
  insufficient-capability cases exist; expiry/restart and replay cases remain open.
- [~] Add tests for omitted, null, forged, local, opaque, and hostile Origin headers. Cases are implemented
  with strict Host variants but require a listener-permitted CI host to execute.
- [~] Add tests proving unauthenticated callers cannot list private state or invoke any tool. Both GET discovery
  and POST calls are covered when the test host permits loopback listeners.
- [x] Add tests proving read-only callers cannot draft, export, commit, rollback, or delete. The canonical
  capability-policy test proves mutation tools are neither listed nor callable by a read-only session.
- [ ] Add an audit record for every permission change and sensitive tool decision without recording secrets.
- [ ] Perform a focused local endpoint penetration review before release.

### Security A exit gate

- [ ] No unauthenticated local or remote client can observe private state or mutate the app.
- [ ] Sensitive operations require narrow, visible, non-replayable user authorization.
- [ ] Security tests run in CI and the threat model is reviewed for every new automation transport.

## 0.4 Make catalog decoding total and non-panicking

- [x] Inventory nullability and constraints for every selected catalog column. The canonical table is in
  `docs/catalog-integrity.md` and the reviewed post-migration schema is `docs/postgres-schema.sql`.
- [x] Replace fallible `row.get` calls with `try_get` and typed error propagation at persistence boundaries.
  Every PostgreSQL row mapper now returns `Result`; no persistence `Row::get` remains.
- [x] Decode nullable schema fields into `Option<T>` or backfill and migrate them to `NOT NULL`. Optional recipes,
  approval time, exposure values, and legacy-nullable strings/dates are decoded explicitly; required dates reject null.
- [x] Add database constraints for month, day, rating, aperture, ISO, and other bounded values. Migration 0013
  constrains new writes for capture dates, exposure, review state, export progress, durations, slide counts, and JSON shape.
- [x] Stop silently clamping corrupt persisted values into plausible defaults. Checked Gregorian date validation
  replaces the former unsigned `unwrap_or_default`, and unknown import modes now fail closed.
- [x] Return an actionable data-integrity error with affected record identity and recovery guidance.
- [x] Add migration fixtures containing null, legacy, out-of-range, malformed JSON, and unknown enum values.
  The isolated PostgreSQL runtime proof injects each class after temporarily removing forward-write constraints.
- [x] Prove snapshot, list, export, relink, and migration commands return errors rather than panic. The 2026-09-02
  isolated PostgreSQL proof passed every corrupt-decode case plus the existing backup/recovery matrix.
- [x] Add a top-level panic boundary for Rust helper processes that preserves diagnostics without exposing secrets.
  All catalog binaries use the shared `run_catalog_helper`; PostgreSQL URL credentials are redacted.

### Catalog mapping A exit gate

- [x] Every row allowed by every supported migration decodes deterministically or returns a structured error.
- [x] No user-controlled catalog value can panic a helper process.
- [x] Corrupt records can be identified, repaired, or restored without hiding unaffected records. The independent
  `catalog_integrity` report scans every record, emits all findings plus the scanned count, and has a fail-closed
  `--require-clean` release mode; the isolated proof covers both clean validated constraints and multiple findings.

# Phase 1: Reliability and lifecycle correctness

## 1.1 Concurrency ownership

- [~] Establish and document the owning thread for every QObject, controller, watcher, and mutable service.
  `docs/concurrency-lifecycle.md` records the major owners and rules; per-operation inventory remains open.
- [~] Prohibit background lambdas from dereferencing UI/controller QObjects. Catalog deletion no longer does so;
  the remaining `QtConcurrent`/watcher inventory still needs path-by-path closure.
- [x] Snapshot immutable request data before dispatching catalog delete work. Program, arguments, working directory,
  environment, target IDs, generation, and cancellation state are captured on the owning thread.
- [~] Move catalog command execution behind a thread-safe worker/service with no UI affinity. Deletion now uses a
  value-only process boundary. Job, timeline-asset, and carousel catalog helpers now share
  `AppBootstrap::runCatalogHelper`; remaining UI-thread synchronous callers still need async dispatch.
- [~] Return results through queued signals or future continuations guarded by request generation. Catalog deletion
  now has both; every other async surface still needs conformance evidence.
- [~] Give every asynchronous request an ID, owner, cancellation token, and terminal state. Catalog deletion now
  conforms through its generation, model-owned watcher, shared token, and single watcher result.
- [ ] Reject stale results after project, asset, recipe, document, or generation changes.
- [x] Cancel and bounded-wait for deletion work during controller destruction. Destruction invalidates the generation
  and signals the token; the worker terminates an active helper with bounded terminate/kill waits and owns no model data.
- [ ] Audit every `QtConcurrent::run`, `QFutureWatcher`, raw callback, and captured `this`/`QPointer`.
- [ ] Add destruction-at-each-await-point tests for catalog, editor, timeline, carousel, export, and agent work.
- [ ] Run TSan coverage over representative controller and cancellation scenarios, not only headless engine tests.

## 1.2 Remove nested event-loop polling

- [x] Inventory every `QCoreApplication::processEvents`, blocking socket wait, process wait, and sleep loop.
  The current production owners and event-pump counts are recorded in `docs/concurrency-lifecycle.md`.
- [ ] Replace agent-edit polling with explicit asynchronous workflow states.
- [ ] Replace local-model synchronous transport with a bounded asynchronous transport service.
- [ ] Replace MCP client synchronous transport with a bounded asynchronous transport service.
- [ ] Replace thumbnail wait pumping with a completion signal/future and cancellable dependency.
- [ ] Prevent a second command from entering any non-reentrant workflow during callbacks.
- [ ] Add invariants for exactly one terminal result per request.
- [ ] Add tests that trigger project switch, close, undo, cancel, and new requests during every active phase.
- [x] Add a static architecture check that rejects new production `processEvents` calls outside an approved adapter.
  Architecture validation freezes the four legacy files at their current counts and rejects every new owner/increase.

## 1.3 Bounded shutdown

- [ ] Introduce one task/process supervisor for timeline analysis, proxies, caches, transcription, and rendering.
- [~] Cancel all active tasks concurrently during shutdown. Timeline-owned media processes now receive kill together;
  remaining controller task families still require the same supervisor contract.
- [~] Set one global graceful-shutdown budget rather than sequential per-process budgets. Timeline media processes
  now share one 1000 ms budget; the app-wide supervisor remains open.
- [x] Remove unbounded `future().waitForFinished()` calls from UI-owned destructors. The program-scope watcher captured
  only immutable values, so destruction now cancels/disconnects it without waiting.
- [~] Define escalation from cooperative cancel to terminate to kill. Catalog deletion follows this escalation and
  timeline destruction kills process-isolated work immediately; one app-wide policy remains open.
- [ ] Preserve recoverable journals and partial-output cleanup after forced termination.
- [ ] Add hung-worker, hung-process, blocked-I/O, and never-finishing-future shutdown tests.
- [ ] Measure app-close p50, p95, and max with idle, moderate, and worst-case active workloads.
- [ ] Prove shutdown remains responsive and bounded on macOS and Windows.

## 1.4 Integrity sidecar lifecycle

- [ ] Replace fire-and-forget sidecar writes with an owned integrity queue.
- [ ] Bind each sidecar task to artifact identity, size, modification state, and generation.
- [ ] Cancel or discard results when the artifact changes or is deleted.
- [ ] Surface sidecar failures to operability state without failing unrelated UI.
- [ ] Retry only transient failures with bounded backoff.
- [ ] Prove stale checksums are never published for replaced artifacts.
- [ ] Drain or safely cancel the queue during shutdown.

### Reliability A exit gate

- [ ] No worker accesses an object with incompatible thread affinity.
- [ ] No user workflow depends on nested event-loop pumping.
- [ ] Every asynchronous operation has cancellation, stale-result rejection, and a bounded terminal path.
- [ ] App shutdown meets its published latency budget under fault injection.

# Phase 2: Persistence, recovery, and durable state

## 2.1 Atomic secret and authority files

- [ ] Create PostgreSQL state and credential files with owner-only permissions at open time.
- [ ] Write to a unique same-directory temporary file without following symlinks.
- [ ] Flush and fsync file contents before publication.
- [ ] Atomically rename and sync the parent directory.
- [ ] Preserve the last-known-good authority file until the replacement is durable.
- [ ] Apply an equivalent ACL and atomic-publication contract on Windows.
- [ ] Test default and permissive umasks, symlink attacks, interruption, disk-full, and concurrent writers.
- [ ] Verify backups, logs, support bundles, and crash reports never contain the password.

## 2.2 JSON and document corruption handling

- [ ] Replace silent `QJsonDocument::fromJson` failures with typed parse results and source locations.
- [ ] Distinguish missing, empty, unsupported-version, malformed, truncated, and semantically invalid state.
- [ ] Never clear in-memory user state before a replacement has parsed and validated successfully.
- [ ] Quarantine corrupt files with a timestamp and checksum.
- [ ] Attempt a verified last-known-good backup where the format supports it.
- [ ] Show a user-facing recovery explanation rather than presenting an empty library/project/job list.
- [ ] Add schema versions and migrations to every durable JSON format.
- [ ] Reject unknown destructive fields while preserving forward-compatible optional fields where intentional.
- [ ] Test truncation at every byte boundary for small representative state files.
- [ ] Test malformed UTF-8, duplicate keys, excessive nesting, huge arrays, unknown versions, and partial writes.

## 2.3 Database backup and restore

- [x] PostgreSQL backup publication uses partial archives and validation in existing tests.
- [x] Existing tests cover corrupt dump rejection and pre-restore safety behavior.
- [ ] Run a full backup/restore drill against a representative populated catalog.
- [ ] Verify row counts, foreign keys, recipes, projects, jobs, thumbnails, and canonical recipe payloads after restore.
- [ ] Test restore after interrupted migration and interrupted app upgrade.
- [ ] Test insufficient disk, denied permissions, PostgreSQL version mismatch, and corrupt authority state.
- [ ] Record recovery point objective and recovery time objective.
- [ ] Add clean-machine restoration instructions and automated verification output.
- [ ] Prove external PostgreSQL configurations are never reset or replaced by managed-runtime recovery.

## 2.4 Stable persistent identities

- [ ] Replace new uses of Rust `DefaultHasher` for durable IDs with a documented versioned algorithm.
- [ ] Define canonical input normalization for drive IDs, paths, Unicode, case, and separators.
- [ ] Preserve existing asset IDs through a compatibility or migration layer.
- [ ] Detect and report collisions instead of silently suffixing incompatible identities.
- [ ] Add cross-Rust-version and cross-platform golden vectors.
- [ ] Document which IDs are stable, session-local, content-derived, or replaceable cache keys.

### Persistence A exit gate

- [ ] Every durable write is atomic or journaled, versioned, validated, and recoverable.
- [ ] Corruption never masquerades as deletion or empty state.
- [ ] Backup and restore are proven on real representative catalogs and clean machines.

# Phase 3: Input, memory, and transport hardening

## 3.1 Checked image and mask allocation

- [ ] Create shared checked helpers for pixel area, row bytes, channel count, and total allocation bytes.
- [ ] Perform multiplication in a wide unsigned type only after validating positive dimensions.
- [ ] Define operation-specific maximum width, height, pixels, frames, layers, masks, and bytes.
- [ ] Apply limits to QImage decode, working images, masks, thumbnails, crops, AI tensors, Metal buffers, and exports.
- [ ] Verify `QImage`, `QByteArray`, and vector allocations succeeded before indexing.
- [ ] Use checked row-offset arithmetic for every raster loop.
- [ ] Reject malformed model output dimensions before copying tensors.
- [ ] Return actionable errors rather than attempting unbounded allocation.
- [ ] Fuzz image headers, dimensions, mask payloads, crop rectangles, and tensor shapes under sanitizers.

## 3.2 Bounded local HTTP

- [ ] Define maximum request and response header sizes.
- [ ] Define maximum encoded body, decoded body, JSON depth, string size, and collection count.
- [ ] Reject oversized or negative `Content-Length` before buffering the body.
- [ ] Bound chunked-response accumulation and validate chunk framing.
- [ ] Handle conflicting content-length, premature close, slowloris, invalid status, and malformed headers.
- [ ] Use one shared tested HTTP framing implementation rather than parallel custom parsers.
- [ ] Keep connect, write, first-byte, idle, total, and cancellation timeouts separate.
- [ ] Apply backpressure to streaming responses and tool output.
- [ ] Fuzz the parser and run memory-growth tests against adversarial localhost servers.

## 3.3 Process execution

- [x] Production helper calls generally pass program arguments separately rather than invoking a shell.
- [~] Centralize helper resolution, environment filtering, timeout, cancellation, and output limits.
  `AppBootstrap::runCatalogHelper` now owns resolution, catalog authority, deadlines, terminate/kill cleanup,
  and merged output for job, timeline-asset, and carousel persistence; cancellation and output caps remain open.
- [ ] Cap stdout/stderr bytes and preserve the most useful diagnostic tail.
- [ ] Redact sensitive arguments and output before logging.
- [ ] Validate executable provenance for bundled helpers.
- [ ] Reject writable or unexpected helper paths in production builds.
- [ ] Add tests for hung helpers, output floods, crashes, signals, non-UTF-8 output, and missing executables.

### Input-hardening A exit gate

- [ ] All externally influenced sizes and dimensions are checked before allocation or arithmetic.
- [ ] Local services and helper processes cannot exhaust memory, disk, descriptors,
  or UI time without a bounded failure.
- [ ] Fuzz targets run continuously and have a documented zero-crash corpus baseline.

# Phase 4: Architecture and maintainability

## 4.1 Decompose god controllers

- [ ] Publish a responsibility map for editor, timeline, carousel, catalog, and agent controllers.
- [ ] Define dependency direction and stable interfaces for each extracted domain.
- [ ] Extract editor document state and edit transactions.
- [ ] Extract render scheduling, cancellation, dirty-domain compilation, and publication.
- [ ] Extract mask/selection lifecycle and raster services.
- [ ] Extract layer lifecycle, thumbnails, history, and persistence adapters.
- [ ] Extract catalog import planning, execution, deletion, relinking, and project membership.
- [ ] Extract agent planning, permissions, execution, proof, review, and memory services.
- [ ] Extract timeline media analysis, process supervision, cache, recovery, and serialization.
- [ ] Extract carousel document, layout, asset, and export services.
- [ ] Keep QML-facing controllers as thin orchestration/view-model layers.
- [ ] Prevent new cross-domain private-field access with interface and dependency tests.
- [ ] Keep each production translation unit focused on a cohesive responsibility with isolated tests and dependencies.
- [ ] Reduce each public controller API to cohesive use-case interfaces rather than hundreds of invokables.
- [x] Remove raw file- and method-line ceilings from architecture validation; they reward formatting tricks and do not
  measure cohesion, coupling, or correctness.
- [ ] Delete compatibility shims after their documented migration window.

Architecture quality criteria:

- [ ] Production C++ files have one named responsibility and do not bypass domain owners.
- [ ] Public headers expose minimal cohesive interfaces and hide implementation details.
- [ ] QML screens compose focused components and keep domain policy outside presentation code.
- [ ] Test files are organized around one behavior family with reusable fixtures where semantics match.
- [ ] Controllers expose cohesive use-case interfaces rather than unrelated property and invokable collections.

## 4.2 Replace dynamic contracts with typed boundaries

- [ ] Inventory every durable or cross-component `QVariantMap` and stringly typed enum.
- [ ] Define typed DTOs for import plans/results, agent tools/results, render
  requests/results, jobs, and recovery state.
- [ ] Register focused immutable value types for QML where needed.
- [ ] Replace string comparisons with scoped enums and explicit conversion functions.
- [ ] Version every serialized DTO and reject unsupported versions clearly.
- [ ] Generate or centrally define field names shared between Rust, C++, JSON, and QML.
- [ ] Add round-trip, unknown-field, version-migration, and canonical-serialization tests.
- [ ] Keep generic maps only at explicitly documented extensibility boundaries.

## 4.3 Dependency boundaries

- [x] The core engine has a tested Qt-free boundary.
- [ ] Add forbidden-dependency checks between UI, application, domain, persistence, platform, and infrastructure layers.
- [ ] Prevent controllers from directly implementing storage, networking, and process orchestration.
- [ ] Prevent QML from encoding domain policy that is absent from application/domain code.
- [ ] Add architecture diagrams and ADRs for ownership, threading, persistence,
  render publication, and agent permissions.
- [ ] Detect dependency cycles in CI.

### Architecture A exit gate

- [ ] Major domains are independently testable without instantiating the full desktop shell.
- [ ] Public APIs are cohesive, typed, versioned, and small enough for human review.
- [ ] Complexity and line-count trends decrease for at least two release cycles.
- [ ] Architecture checks enforce direction and ownership rather than preserving current file sizes.

# Phase 5: Code cleanliness and static quality

## 5.1 Formatting and linting

- [ ] Keep `cargo fmt --all --check` green in clean-checkout CI.
- [ ] Fix the current MCP `unnecessary_filter_map` Clippy failure.
- [ ] Keep `cargo clippy --workspace --all-targets --locked -- -D warnings` green in CI.
- [ ] Add a repository `.clang-format` matching the established C++ style.
- [ ] Add a check-only C++ formatting job.
- [ ] Add `.editorconfig` for common whitespace, encoding, and newline rules.
- [ ] Configure `clang-tidy` for bug-prone, performance, concurrency, modernize, and readability checks.
- [ ] Establish a reviewed baseline, then fail CI on new findings.
- [ ] Add platform-appropriate static analysis on macOS and Windows where compiler behavior differs.
- [ ] Treat production compiler warnings as errors after establishing a clean baseline.
- [x] Remove the duplicate `-lc++` linkage warning. The Apple pkg-config fallback now removes LibRaw's retired
  `stdc++` compatibility entry while retaining its imported include/library contract; app and test links are clean.
- [x] Repair or regenerate the Ninja log/state so normal incremental builds do not report premature EOF.
  `ninja -t recompact` repaired the dependency state and two follow-up builds converged to `no work to do`.

## 5.2 Complexity and duplication

- [~] Measure function length, cyclomatic complexity, nesting, parameter count, and duplication. Exact normalized
  clone length/count is now measured across 919 files; function complexity, nesting, and parameter metrics remain.
- [~] Set ratcheting budgets for production code and tests. Clean-source CI now rejects growth above the audited
  732 total and 505 production clone counts; CI rejects regressions while each consolidation drives those counts toward zero, and complexity,
  test-only, and language-specific budgets remain.
- [x] Generate a structural duplicate-code report for C++, Objective-C++, QML, Rust, Python, CMake, shell,
  PowerShell, and test code. The
  maximal normalized-clone reporter is self-tested and runs against the clean archived source in CI; its JSON is
  retained with source-closure evidence. The current 919-file working-tree audit found 732 clones at the 12-line/320-character
  threshold (505 production, 218 test-only, and 9 production/test). The pass removed the leading 180-line recipe
  serializer clone, the 103/79-line menu popup copies, the 73-line timeline clip loader, the 51-line timeline
  undo/redo state copier, the 43-line thumbnail presentation setup, the 42-line job telemetry copy, and the 47-line
  CPU/Metal white-balance implementation, the shared preview/export camera-profile metadata projections, the
  repeated batch-cancellation path in the agent controller, duplicated preview queue telemetry, the shared
  mask-row action paths, mask search aggregation, and retouch before/after controls. The largest remaining mixed finding is a 55-line production/test validation copy; the
  largest remaining production C++ finding is a 38-line internal `working_image.h` clone and the largest remaining
  production QML finding is a 32-line mask source-row interaction clone.
- [ ] Supplement textual clone detection with a capability/behavior inventory to find semantic duplication.
- [ ] Inventory repeated UI actions that reach different controllers or persistence paths.
- [~] Inventory competing helpers for JSON, hashing, file publication, paths, process execution,
  HTTP framing, image allocation, cancellation, and error translation.
- [~] Designate one canonical implementation for each cross-cutting concern and document its contract.
  Catalog helper execution, packaged helper dispatch, and bounded process waiting now have canonical owners and guards;
  JSON, hashing, publication, HTTP, allocation, cancellation, and error translation remain open.
- [~] Extract repeated JSON parsing, process management, HTTP framing, persistence, and image-area logic.
  Three duplicated catalog-helper runners now delegate to one process boundary, and 13 macOS helper executables now
  reuse one multicall binary; the other concern families remain.
- [ ] Keep platform-specific implementations behind shared interfaces and conformance tests.
- [ ] Delete obsolete implementations and prove production binaries no longer reference them.
- [ ] Remove dead feature flags, stale compatibility branches, unused properties, and unreachable QML states.
- [ ] Require tests before deleting apparently unused stateful paths.
- [ ] Add a dependency and dead-code report to release evidence.

## 5.3 Repository hygiene

- [ ] Classify every current modified, deleted, and untracked file as source,
  intended deletion, generated artifact, or disposable output.
- [ ] Break the current work into narrowly reviewable commits or pull requests.
- [ ] Remove or relocate root artifacts such as `--report`, `--report-artifacts`,
  `:memory:.ses`, and stray `CMakeFiles`.
- [ ] Store performance evidence under one documented artifact root outside source directories.
- [ ] Enforce artifact retention by age, total bytes, and protected release evidence.
- [ ] Add preflight checks for suspicious filenames and large untracked build outputs.
- [ ] Keep secrets, databases, model binaries, source photos, logs, and support bundles out of Git.
- [ ] Require a clean worktree before release tagging and source archive generation.

### Cleanliness A exit gate

- [ ] Formatting, compiler warnings, Clippy, clang-tidy, QML lint, and repository
  hygiene gates pass with no unexplained suppression.
- [ ] Files and APIs meet the agreed complexity budgets or carry a time-bounded exception.
- [ ] A reviewer can identify all source required for a build from tracked manifests alone.

# Phase 6: QML, UX implementation quality, and accessibility

## 6.1 QML correctness

- [ ] Fix all 142 missing-property warnings. The attempted direct `StudioWorkspace`/`StudioImageViewport` typing
  correctly failed runtime validation because it creates a QML type cycle; keep the current findings visible until
  the input surface is moved behind a real acyclic contract rather than hiding them with `var`.
- [x] Fix all layout-positioning warnings. The 10 remaining layout-owned width/height assignments were replaced
  with layout preferences or implicit dimensions; the current lint report contains zero layout-positioning warnings.
- [x] Fix runtime invalid-anchor warnings in diagnostic overlays. The three diagnostic labels now use explicit
  sibling-coordinate placement; a rebuilt desktop launch reached `desktop.startup.ready` with no anchor warnings.
- [~] Fix undefined-to-color, undefined-to-bool, and undefined-to-string assignments. The timeline runtime proof no
  longer dereferences absent keyframe/transition/error fields; the repository-wide warning set remains.
- [~] Fix attached accessibility properties used on unsupported object types. The Program Comparison semantics now
  attach to its `Rectangle` content item instead of `Window`; repository-wide audit remains.
- [ ] Replace missing default fonts with explicit available application typography.
- [ ] Reduce 3,557 unqualified-access warnings through typed required properties, IDs, and explicit scope.
  `ToolRail.qml` now uses bound nested components and signal-based menu closure, eliminating all 43 of its
  unqualified-access findings and both unsafe dynamic `itemAt()` method lookups. `TimelineClipContent.qml` now
  binds its repeated badge delegates explicitly, removing 28 more. `StudioDiagnosticOverlayStack.qml` now accepts
  typed geometry, visibility, color, and source inputs instead of dynamic workspace/viewport/controller facades,
  removing all 93 warnings from that component. `StudioViewportBrushCursors.qml` now receives explicit cursor
  geometry, visibility, color, and label inputs, removing all 45 warnings and an unused pressure-text property.
  `StudioViewportLoupe.qml` now receives explicit presentation geometry, display state, and preview inputs instead of
  reaching through workspace/viewport objects, removing all 47 of its warnings.
- [~] Add a warning baseline that immediately fails on new correctness-category warnings. The macOS desktop CI
  now rejects any `Quick.layout-positioning` or comma-expression warning through a self-tested category-budget
  parser; ratchets for the remaining version-sensitive categories and the final zero-warning gate remain open.
- [ ] Reach zero QML warnings, then make the complete lint output fatal in CI.

## 6.2 QML architecture

- [ ] Split Studio, Timeline, and Carousel workspaces into bounded feature components.
- [ ] Move business rules, permission decisions, data normalization, and persistence out of QML.
- [ ] Keep components explicit about required inputs and emitted commands.
- [ ] Prevent deep `parent.parent` traversal and implicit context-property dependencies.
- [ ] Centralize design tokens, focus visuals, spacing, typography, motion, and reduced-motion behavior.
- [ ] Add component-level fixtures for empty, loading, ready, error, offline, disabled, and overflow states.

## 6.3 Accessibility

- [ ] Give every interactive element an accurate role, name, description, state, and keyboard action.
- [ ] Establish logical focus order for every workspace and modal surface.
- [ ] Prove all workflows are operable without a pointer.
- [ ] Support visible focus, screen-reader announcements, reduced motion, high contrast, and text scaling.
- [ ] Validate minimum target sizes and contrast in every theme/state.
- [ ] Test VoiceOver on macOS and Narrator on Windows with representative workflows.
- [ ] Record human accessibility review findings and remediation evidence.

## 6.4 Visual and interaction proof

- [ ] Capture deterministic baseline images for representative window sizes and display scales.
- [ ] Test narrow, standard, wide, Retina/high-DPI, and multi-monitor layouts.
- [ ] Test long text, localization, RTL, missing media, offline drives, and error banners.
- [ ] Store perceptual diffs with reviewed thresholds rather than only source-string contracts.
- [ ] Keep generated proof artifacts bounded and attributable to a commit.
- [ ] Conduct photographer workflow review for import, cull, edit, masks, AI review, export, and recovery.

### UI/accessibility A exit gate

- [ ] QML produces zero static and runtime warnings in representative workflows.
- [ ] Automated visual and accessibility suites pass on macOS and Windows.
- [ ] Human keyboard, screen-reader, localization, and photographer reviews are recorded with no critical findings.

# Phase 7: Test engineering and verification quality

## 7.1 Test topology

- [ ] Split 20,000-line and 10,000-line test executables by behavior and fixture.
- [ ] Use a consistent native test framework or shared harness with isolated setup, teardown, and diagnostics.
- [ ] Make each test independently runnable and deterministic.
- [ ] Eliminate hidden ordering, global-state, shared-cache, and shared-port dependencies.
- [ ] Replace giant custom-main failure chains with named assertions and scoped fixtures.
- [ ] Keep test registration generated or data-driven enough to avoid a 4,000-line CMake registry.
- [ ] Add stable labels for unit, integration, database, visual, performance, hardware, network, and packaging tests.

## 7.2 Separate correctness from performance

- [ ] Remove wall-clock performance assertions from the ordinary parallel functional lane.
- [ ] Run latency tests serially with controlled CPU, thermal, cache, and background-load conditions.
- [ ] Classify the machine and select only budgets approved for that hardware tier.
- [ ] Record cold, warm, p50, p95, max, memory, backend, model hash, and cache state.
- [ ] Require multiple samples and a statistically defensible regression rule.
- [ ] Preserve functional assertions even when a host is not eligible for performance certification.
- [ ] Investigate and eliminate the timeline UI proof bus error under parallel execution.
- [ ] Prove visual tests use unique app-data, cache, database, socket, and artifact directories.

## 7.3 Coverage

- [ ] Add LLVM source coverage for C++ and `cargo llvm-cov` or equivalent for Rust.
- [ ] Report line, branch, function, and changed-line coverage by domain.
- [ ] Establish an honest baseline without excluding difficult production code merely to improve the number.
- [ ] Set increasing changed-line and critical-domain thresholds.
- [ ] Require near-complete branch coverage for import replacement, migrations,
  permissions, recovery, and destructive actions.
- [ ] Publish machine-readable and human-readable coverage artifacts.
- [ ] Review uncovered critical paths during every release candidate.

## 7.4 Fuzzing, properties, and mutation testing

- [ ] Add libFuzzer/cargo-fuzz targets for images, masks, recipes, JSON, HTTP, manifests, and migration inputs.
- [ ] Add property tests for geometry mappings, crop/rotate transforms, serialization, undo/redo, and import planning.
- [ ] Seed fuzz corpora with prior regressions and supported format edge cases.
- [ ] Run short sanitizer-backed fuzz smoke in CI and longer scheduled campaigns.
- [ ] Add mutation testing for import policy, permission checks, recovery, and recipe validation.
- [ ] Establish acceptable mutation scores for critical decision logic.
- [ ] Turn every discovered crash or escaped mutant into a permanent regression test.

## 7.5 Failure injection and chaos

- [ ] Create deterministic fault points for filesystem, database, process, network,
  GPU, model, and cancellation failures.
- [ ] Test disk-full and permission loss during import, save, backup, export, and update.
- [ ] Test PostgreSQL disconnect/restart during read and transaction boundaries.
- [ ] Test GPU loss/fallback and corrupt shader/model assets.
- [ ] Test sleep/wake, drive disconnect, project switch, and shutdown during active work.
- [ ] Verify failures are contained, explained, retryable where safe, and never silently destructive.

### Testing A exit gate

- [ ] All required suites pass from a clean checkout with no crashes and no unexplained skips.
- [ ] Critical domains meet coverage and mutation thresholds.
- [ ] Fuzzing and failure-injection campaigns have zero unresolved high-severity findings.
- [ ] Performance and visual results are stable, attributable, and isolated from functional tests.

# Phase 8: Performance and resource efficiency

## 8.1 Close current failed gates

- [ ] Profile the supported LaMa inference path with cache access fixed and controlled warmup.
- [ ] Identify preprocessing, runtime initialization, model execution, and postprocessing costs separately.
- [ ] Meet the declared 6,000 ms cold and 2,500 ms warm limits on the supported macOS arm64 tier.
- [ ] Profile the 10,000-clip projection and reduce isolated time below 1.5 seconds with margin.
- [ ] Prove the timeline optimization preserves clips, tracks, effects, captions,
  markers, transcripts, and offline state exactly.
- [ ] Re-run both gates across repeated samples and store distributions.

## 8.2 Interactive budgets

- [ ] Define p50/p95/max budgets for launch, library load, import preflight,
  thumbnail first paint, preview update, brush latency, mask creation, undo/redo,
  project switch, timeline scrub, agent first feedback, export start, and shutdown.
- [ ] Measure UI-thread blocked time and frame misses during each interaction.
- [ ] Measure CPU, GPU, memory, allocations, I/O, cache hit rates, and worker queue delay.
- [ ] Prevent background AI, thumbnail, indexing, or timeline work from starving direct manipulation.
- [ ] Add cancellation-latency and stale-work-waste budgets.
- [ ] Test small, representative, large, and stress-scale catalogs/documents.

## 8.3 Resource lifecycle

- [ ] Bound preview, thumbnail, model, waveform, render, and disk caches.
- [ ] Define eviction policy and behavior under memory/disk pressure.
- [ ] Prove long editing sessions do not leak memory, file descriptors, sockets,
  processes, GPU resources, or temporary files.
- [ ] Add soak tests for repeated open/edit/export/close and project switching.
- [ ] Keep performance artifact storage below a configured quota with protected release evidence exempted.

## 8.4 Release scope and feature consolidation

- [ ] Publish a release capability matrix with `ship`, `preview`, `internal`, `disabled`, and `remove` states.
- [ ] Map every shipped capability to its single UI entry, domain owner, persistence contract, tests,
  documentation, and support status.
- [ ] Consolidate duplicate user actions that perform the same operation from different workspaces.
- [ ] Consolidate overlapping editor, mask, timeline, carousel, agent, import, and export terminology.
- [ ] Hide or compile out incomplete internal tools rather than shipping unsupported visible paths.
- [ ] Remove abandoned experiments, obsolete compatibility modes, dead feature flags, and unreachable screens.
- [ ] Ensure disabled features do not initialize services, allocate caches, open ports, start processes,
  register commands, or enlarge the package unnecessarily.
- [ ] Require product and engineering approval before expanding release scope after feature freeze.
- [ ] Give every preview feature an owner, warning, telemetry/privacy classification, rollback, and removal plan.
- [ ] Prove each shipped feature has an end-to-end packaged-build test and user-facing recovery path.

## 8.5 Production binary, startup, and package optimization

- [~] Establish size budgets for the application, Qt modules, Rust helpers, PostgreSQL runtime,
  AI models/runtimes, codecs, symbols, resources, and installers. macOS CI now enforces the multicall catalog helper at
  10,000,000 bytes, PostgreSQL at 70,000,000 bytes, and both non-Basic Qt Quick Controls styles and excluded Qt
  runtime branches at zero; signed total, component, installer, and Windows baselines remain.
- [x] Produce a package composition report sorted by compressed and installed bytes. The canonical staged-tree
  reporter records installed and deterministic per-file DEFLATE bytes, both rankings, category totals, hashes,
  repeated names, duplicate waste, prior-report growth, and exact final-installer bytes/hashes. Both packaging jobs
  run it after creating the platform installer and retain the report.
- [~] Remove unused Qt modules, QML resources, plugins, helpers, models, assets, and duplicate libraries. macOS now
  packages one ONNX Runtime dylib; all 135 Controls imports select Basic at compile time; and deployment removes
  non-Basic styles, Qt PDF import, unsupported legacy image readers, Qt Virtual Keyboard, the redundant GLib
  network backend, and the parallel OpenSSL TLS plug-in while preserving PDF export, GIF, native Apple networking/TLS,
  QtNetwork's directly linked OpenSSL libraries, and required LibRaw dependencies.
  PostgreSQL includes only its six used executables and excludes static/development client payload, falling from
  80,130,923 to 64,370,855 installed bytes. A canonical manifest corrected a release defect that had omitted
  `catalog_integrity` from the bundle and its budget. The 13 separately linked macOS catalog helpers are now one
  7,379,584-byte multicall binary behind 13 relative compatibility symlinks, down 52,614,848 bytes; its exact packaged
  stage passes all 12 lifecycle markers. The latest measured packaging-scope stage is 239,355,893 installed bytes and
  85,542,735 raw-DEFLATE bytes, has no unexplained identical-content group, and passes 212-Mach-O/1,198-edge dependency
  closure. Remaining Qt/module review, AI-enabled scope, final installer measurement, and Windows parity remain open.
- [x] Detect duplicate bundled frameworks, dylibs/DLLs, model files, license files, and resources. The canonical
  SHA-256 content inventory detects every byte-identical regular file and fails unexplained groups with exact,
  owned, expiring exceptions plus visible structural explanations for required Qt privacy manifests and PostgreSQL
  timezone aliases. It also inventories symlinks and rejects broken or package-escaping targets. Both macOS and
  Windows final staging jobs run and retain the report; the latest measured 1,187-regular-file/119-symlink local macOS
  packaging-scope stage has no unexplained duplicate group and every symlink resolves within the package root. A fresh
  clean-tree rebuild is still required before treating those measurements as release-candidate evidence.
- [~] Verify runtime dependency scanning includes dynamically loaded production paths. The macOS gate now walks
  every packaged Mach-O, including QML, Qt, and PostgreSQL plugins, and resolves loader, executable, and run-path
  dependencies; the latest measured packaging-scope stage passed 212 Mach-O files and 1,198 dependency edges. Windows DLL delay-load/plugin proof
  remains open.
- [~] Evaluate release-only optimization, dead stripping, link-time optimization, and section garbage
  collection per platform; enable only with correctness and startup evidence. The Rust release profile now uses
  Thin LTO and one codegen unit after an isolated optimized build reduced the 12 shipped helpers from 71,604,016 to
  56,155,440 bytes (15,448,576 bytes, or 21.6%) and all 34 optimized catalog tests passed. Symbol stripping remains
  disabled until matching protected symbols and crash-stack symbolication exist; native macOS/Windows optimization
  and packaged startup/correctness parity remain open.
- [ ] Split and retain debug symbols in protected symbol artifacts while keeping production packages lean.
- [ ] Preserve actionable crash stacks for the exact released binaries.
- [ ] Eliminate the fresh macOS deploy warnings `QProcess: Destroyed while process (install_name_tool/strip) is still
  running` or fix/pin the Qt deployment toolchain version that emits them. The resulting bundle currently passes
  strict signing and complete dependency closure, but an A gate cannot silently normalize subprocess-lifecycle warnings.
- [ ] Build a startup dependency trace from process entry to first usable library/editor frame.
- [ ] Defer model loading, heavy workspace creation, indexing, diagnostics, and optional services until needed.
- [ ] Keep the database, updater, automation listener, and background workers from delaying first usable UI
  unless their readiness is required for the initial workflow.
- [ ] Remove development overlays, test hooks, verbose logging defaults, synthetic fixtures, and benchmark
  assets from production packages.
- [ ] Keep safety validation, error handling, recovery, integrity checks, and permission enforcement enabled
  in optimized builds.
- [ ] Set production defaults for log level, cache quota, worker concurrency, model loading, telemetry,
  automation, updater channel, and diagnostic retention.
- [ ] Compare debug and release behavior with a shared correctness and golden-output suite.
- [~] Measure cold/warm launch, first frame, first interactive frame, library ready, memory at idle,
  package size, installed size, and first-edit latency for every release candidate. The newest package has exact size
  evidence, but startup is currently skipped because this host session cannot register an AppKit application. The
  aggregator now reports local skips as `skipped` and makes any release-proof skip `incomplete` with a nonzero exit;
  usable-frame, idle-memory, first-edit, and real GUI-session evidence remain open.
- [ ] Fail release qualification on unexplained size or startup regressions against the previous release.
- [ ] Verify release optimization does not alter color, render, export, recipe, migration, or recovery results.
- [ ] Run the optimized packaged binary through sanitizer-tested inputs and clean-machine workflows.

### Release optimization A exit gate

- [ ] Every shipped feature is supported, owned, tested, documented, and represented once in the capability map.
- [ ] Disabled/internal features contribute no unintended runtime work or externally reachable surface.
- [ ] Package contents contain no duplicate or unused production dependency without an approved exception.
- [ ] Startup, idle memory, package size, and core interaction budgets pass with margin in release builds.
- [ ] Release optimizations preserve byte-level or tolerance-defined correctness across critical workflows.

### Performance A exit gate

- [ ] Every published budget passes with margin on every supported hardware tier.
- [ ] Direct manipulation remains responsive during worst-case background activity.
- [ ] Long-running and stress tests stay within memory, disk, process, descriptor, and thermal budgets.
- [ ] Results come from representative assets and actual application workflows, not synthetic-only proxies.

# Phase 9: Privacy, observability, and supportability

## 9.1 Data classification and redaction

- [ ] Classify source paths, project names, prompts, recipes, database URLs, export destinations, image metadata,
  model input/output, account values, tokens, and helper output.
- [ ] Implement one centralized redaction/filtering layer for C++, Rust, Qt messages, and support bundles.
- [ ] Redact before serialization or OTLP export, not only at display time.
- [ ] Hash identifiers only when correlation is necessary and document the scope/lifetime of the hash.
- [ ] Add automated tests with seeded secrets and personally identifying paths.
- [ ] Fail support-bundle creation if required redaction validation cannot complete.

## 9.2 Telemetry controls

- [ ] Keep network telemetry disabled by default unless the user or managed deployment explicitly opts in.
- [ ] Show what categories are collected, destination, retention, and how to disable/delete them.
- [ ] Separate local performance diagnostics from remotely exportable telemetry.
- [ ] Restrict OTLP endpoints and transport security in production builds.
- [ ] Apply bounded queues, retry, shutdown, and disk retention to telemetry.
- [ ] Ensure telemetry failure never blocks editing or leaks unbounded resources.

## 9.3 Operational signals

- [x] Structured local observability and performance summarization exist.
- [ ] Define stable event schemas, versions, severity, ownership, and cardinality budgets.
- [ ] Add correlation IDs across UI command, worker, helper, database, render, and export boundaries.
- [ ] Emit actionable failure classes instead of only free-form messages.
- [ ] Add health summaries for database, storage, cache, model, GPU/backend, worker queues, and updater.
- [ ] Document diagnostic collection and recovery playbooks for support staff.

### Privacy/observability A exit gate

- [ ] Automated redaction tests prove sensitive fields cannot reach logs, telemetry, or support bundles.
- [ ] Remote telemetry is transparent, minimal, secure, bounded, and explicitly controlled.
- [ ] Production failures can be diagnosed from structured, privacy-safe evidence.

# Phase 10: Dependencies and supply-chain security

- [x] Cargo dependencies are locked.
- [x] LittleCMS source is versioned and SHA-256 pinned.
- [x] CI runs Cargo Audit and Gitleaks.
- [ ] Generate an SBOM for Rust, C++, Qt, bundled PostgreSQL, codecs, AI runtimes, models, fonts, and assets.
- [ ] Record source, version, hash, license, redistribution rights, and update policy for every shipped component.
- [ ] Verify every network-fetched archive with a committed digest or signature.
- [ ] Pin CI actions by immutable commit SHA and maintain them through reviewed updates.
- [ ] Add dependency-review and license-policy gates to pull requests.
- [ ] Scan packaged binaries and archives rather than only source manifests.
- [ ] Scan model formats and bundled runtimes with the available security tooling.
- [ ] Sign release provenance/attestations and publish checksums from the trusted release job.
- [ ] Define critical vulnerability response and dependency update SLAs.
- [ ] Document accepted advisories with feature-reachability evidence, owner, and expiry.

### Supply-chain A exit gate

- [ ] Every shipped byte has known provenance, integrity verification, and an approved redistribution basis.
- [ ] Release artifacts have verifiable SBOM, checksum, signature, and build provenance.
- [ ] No unresolved reachable critical/high advisory remains at release time.

# Phase 11: Cross-platform build, package, update, and recovery

## 11.1 Build matrix

- [ ] Build and test declared minimum macOS Apple-silicon, macOS Intel, Windows 10, and Windows 11 configurations.
- [ ] Pin and publish compiler, SDK, Qt, Rust, CMake, Ninja, PostgreSQL, and packaging tool versions.
- [ ] Treat platform-specific warnings as failures after baselining.
- [ ] Verify paths, Unicode, case handling, long paths, permissions, and process behavior on every platform.
- [ ] Prove CPU fallback where GPU/Metal functionality is unavailable or unsupported.

## 11.2 Package integrity

- [x] Release automation includes macOS signing/notarization and Windows signature verification.
- [x] Package verification checks required licenses and major runtime dependencies.
- [ ] Install each package on a clean machine without developer tools.
- [ ] Validate first-run database bootstrap, import, edit, masks, agent-disabled baseline, export, quit, and relaunch.
- [ ] Verify no absolute developer paths or untracked files are embedded.
- [ ] Verify application, helpers, libraries, models, resources, and update metadata are signed or integrity checked.
- [ ] Validate quarantine/Gatekeeper and Windows reputation/security behavior.

## 11.3 Update and rollback

- [ ] Prove macOS updates on both supported architectures from the previous public version.
- [ ] Prove Windows App Installer updates without changing package identity.
- [ ] Back up and validate catalog state before schema-changing upgrades.
- [ ] Test update interruption, bad signature, unavailable feed, insufficient disk, and post-update launch failure.
- [ ] Define application rollback behavior when the database schema is not backward compatible.
- [ ] Verify uninstall/reinstall preserves user data unless the user explicitly requests removal.

### Release engineering A exit gate

- [ ] Signed production packages install, update, recover, and uninstall correctly on all supported clean machines.
- [ ] The packaged runtime passes the same core workflows as the developer build.
- [ ] Release provenance is traceable from source commit to public download checksum.

# Phase 12: Color, image-quality, and product acceptance

Code quality alone cannot establish an A rating for a professional photo editor.

## 12.1 Color and render correctness

- [x] The repository contains broad color pipeline, render parity, profile, and golden tests.
- [ ] Run all non-skipped CPU/GPU/Metal parity tests on eligible hardware.
- [ ] Resolve every skipped parity path or classify it as inapplicable through explicit platform detection.
- [ ] Validate RAW decode, DCP, ICC, OCIO/ACES, HDR, soft proof, export, and display conversion against references.
- [ ] Measure Delta E, clipping, monotonicity, gradient smoothness, highlight rolloff, and round-trip error.
- [ ] Validate representative camera, profile, monitor, HDR, and export combinations.
- [ ] Prove preview and export use equivalent intended color decisions at full resolution.

## 12.2 Photographer and human review

- [ ] Complete the outstanding nine-case photographer review for agent retouch quality.
- [ ] Complete representative human calibration for supported local-model tiers.
- [ ] Run professional reviews for skin, foliage, skies, mixed lighting, high ISO,
  deep shadows, highlights, and textured retouch.
- [ ] Record reviewer role, calibrated display, environment, source checksum, result, defect, and disposition.
- [ ] Require unanimous or explicitly defined acceptance thresholds for destructive
  or irreversible image-quality claims.
- [ ] Keep machine proof and human aesthetic approval separate in all completion claims.

## 12.3 End-to-end product workflows

- [ ] Prove import-to-export workflows with managed and referenced originals.
- [ ] Prove offline/relink, missing media, backup, recovery, virtual copy, project, timeline, and carousel workflows.
- [ ] Prove undo/redo and rollback across editor, masks, agent drafts, timeline, and failure recovery.
- [ ] Verify exports with independent viewers and metadata tools.
- [ ] Run multi-hour production sessions with representative photographers and capture blocking usability defects.

### Product-quality A exit gate

- [ ] Objective color/render thresholds pass on all supported paths and hardware.
- [ ] Required photographer reviews pass with traceable evidence.
- [ ] Representative end-to-end workflows pass in packaged builds without data
  loss, unexplained visual drift, or blocking UX defects.

# Phase 13: Documentation and engineering governance

- [ ] Publish an architecture overview showing domain, process, thread, persistence,
  and data-flow boundaries.
- [ ] Add ADRs for import atomicity, catalog authority, durable IDs, MCP permissions,
  threading, telemetry, and update rollback.
- [ ] Document supported platforms, hardware tiers, file formats, color paths,
  model/runtime requirements, and limitations.
- [ ] Keep setup, build, test, package, recovery, and incident commands executable and CI-verified.
- [ ] Define code ownership for import/data safety, catalog/migrations,
  rendering/color, agent/security, timeline, release, and privacy.
- [ ] Require two-person review for destructive storage changes, permission changes,
  migrations, signing, and release promotion.
- [ ] Add pull-request templates requiring risk, user-visible behavior,
  schema/environment changes, tests, screenshots, and rollback.
- [ ] Define severity, incident response, disclosure, hotfix, rollback, and postmortem processes.
- [ ] Track every temporary suppression, skipped test, ignored advisory, and architecture exception centrally.
- [ ] Review and expire exceptions at every release candidate.
- [ ] Keep the production launch checklist linked to this engineering checklist
  without duplicating contradictory status.

### Governance A exit gate

- [ ] High-risk systems have explicit owners, reviewed ADRs, tested runbooks, and required reviewers.
- [ ] Documentation matches the packaged product and is checked as part of release qualification.
- [ ] No critical operational knowledge exists only in one developer's local environment.

# Final A-rating verification matrix

The overall rating becomes A only after every row is complete.

| Category | Required final evidence | Status |
| --- | --- | --- |
| Functional uniqueness | Capability inventory, ownership map, clone/dead-code report | [ ] |
| Data integrity | Import collision/failure matrix and restore drill | [ ] |
| Reproducibility | Clean archived checkout build/test/package inventory | [ ] |
| Security | Auth/capability suite and focused penetration review | [ ] |
| Catalog correctness | Nullable/corrupt migration fixtures with zero panics | [ ] |
| Concurrency | Lifecycle/race tests plus representative TSan run | [ ] |
| Persistence | Atomic-write faults and full catalog restore | [ ] |
| Input hardening | Sanitizer fuzzing and bounded resource proofs | [ ] |
| Architecture | Domain extraction, API budgets, dependency gates | [ ] |
| Code cleanliness | Format/lint/static analysis at zero unexplained findings | [ ] |
| QML quality | Zero lint/runtime warnings and visual suite | [ ] |
| Accessibility | Automated plus VoiceOver/Narrator human proof | [ ] |
| Testing | Coverage, mutation, fuzz, failure injection, stable suites | [ ] |
| Performance | All budgets passing with margin by hardware tier | [ ] |
| Release optimization | Scope, package composition, startup, size, and release parity proof | [ ] |
| Privacy | Redaction tests, telemetry controls, safe support bundle | [ ] |
| Supply chain | SBOM, provenance, signatures, license approval | [ ] |
| Cross-platform release | Clean install/update/recovery on every supported OS | [ ] |
| Color and image quality | Objective parity plus photographer approval | [ ] |
| Governance | Owners, ADRs, runbooks, exception control | [ ] |

# Required final commands and artifacts

Exact commands may be wrapped by CI, but the final evidence must include the
equivalent of all applicable checks below.

- [ ] `git diff --check`
- [ ] Clean-worktree and tracked-source inventory check
- [ ] Capability inventory and reuse/consolidate/replace decision report
- [ ] Cross-language duplicate-code, duplicate-resource, and dead-code report
- [ ] `git archive HEAD` clean-checkout verification
- [ ] `cmake --preset ci-headless && cmake --build --preset build-ci-headless`
- [ ] `ctest --preset test-ci-headless --output-on-failure`
- [ ] ASan/UBSan headless and representative desktop/controller tests
- [ ] TSan concurrency and lifecycle suite
- [ ] `cmake --preset dev-macos-debug && cmake --build --preset build-dev-macos-debug`
- [ ] Complete desktop functional suite with no unexplained skips
- [ ] Serialized visual proof suite
- [ ] Serialized performance certification suite by hardware tier
- [ ] Release package composition and duplicate-binary/resource scan
- [ ] Release-versus-debug correctness and golden-output comparison
- [ ] Cold/warm startup, idle memory, package size, and first-edit regression report
- [ ] `cargo fmt --all --check`
- [ ] `cargo clippy --workspace --all-targets --locked -- -D warnings`
- [ ] `cargo test --workspace --locked`
- [ ] C++ format and clang-tidy gates
- [ ] QML lint with zero warnings
- [ ] C++ and Rust coverage reports
- [ ] Fuzz smoke and scheduled-campaign summaries
- [ ] Mutation report for critical policy code
- [ ] Cargo Audit, secret scan, dependency review, package scan, and SBOM
- [ ] macOS and Windows clean-machine install/update/recovery evidence
- [ ] Representative RAW/color/render corpus report
- [ ] Photographer and accessibility review reports
- [ ] Signed release manifest linking commit, binaries, hashes, SBOM, tests, and approvals

# Completion record

Do not fill this section until every category in the matrix is complete.

- [ ] Release commit:
- [ ] CI run:
- [ ] Capability inventory and consolidation report:
- [ ] Release optimization and package composition report:
- [ ] macOS Apple-silicon package proof:
- [ ] macOS Intel package proof:
- [ ] Windows package proof:
- [ ] Security review:
- [ ] Backup/restore drill:
- [ ] Performance certification:
- [ ] Color and photographer approval:
- [ ] Accessibility approval:
- [ ] SBOM and provenance:
- [ ] Final engineering approval:

Final declaration:

- [ ] Framehouse has an A rating in every engineering category with no hidden,
  skipped, local-only, or unverified completion claim.
