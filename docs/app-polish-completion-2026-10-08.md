# Looksy polish completion — October 8, 2026

This report maps the seven requested product priorities to concrete fixes and acceptance evidence. It supports the existing canonical checklists; it does not redefine their completion thresholds. An implemented fix can be complete while its broader product qualification remains open.

## Work in this pass

| Priority | Concrete work | Acceptance state |
| --- | --- | --- |
| 1. Smooth editing | Move recovery discovery, verification and outgoing checkpoint persistence off the GUI path; drain failed/deferred media queues. | Focused foreground-admission and registered queue-terminal checks pass; native painted brush and large-project budgets remain open. |
| 2. Trustworthy saving/recovery | Prevent a late Save from marking a different same-ID recovered session clean; preserve unrelated Recent recovery rows and exact outgoing checkpoints. | Current complete focused lifecycle and native Timeline Save/reopen/Recents recovery pass. The broad recovery suite timed out; large-project timing remains open. |
| 3. Clear tools/targets | Route Open selected photo in Edit only to a photo; distinguish selection from row focus; enlarge actual menu targets; expose color-domain help in accessible header tooltips. | Catalog model and full native accessibility workflow pass. |
| 4. Photographic/output fidelity | Preserve DPI, profile, text and physical-pixel coverage during flattened JPEG/scene-linear export. | Focused queued/direct JPEG and 48 lossless PNG cases pass. Seven rebuilt color/output regression tests pass; independent real-camera/human acceptance remains open. |
| 5. Shoot-to-delivery workflow | Integrate current Library work with photo-only Edit routing and durable delivery. | Native routing, real keyboard export, delivery preflight and offline-recovery surfaces pass. Complete real-shoot acceptance remains open. |
| 6. Dependable AI completion | Account for scale/rotation in composition review, remove duplicated repair schemas, and retain native font constraints after grammar normalization. | Focused model regressions pass. The matched live attempt ended before producing a plan; successful autonomous completion remains open. |
| 7. First launch/release experience | Increase Quick Start choice targets, correct macOS primary shortcut mapping, and repair the compatibility-aware release brand gate. | Complete native first-use/shortcut workflow, brand regressions and candidate structural preflight pass. Clean-machine and external public-release gates remain open. |

## Checked off in this pass

- [x] Preserve flattened-export metadata and full physical-pixel coverage, with a failing-before/passing-after regression.
- [x] Rebuild and pass seven focused color/output regressions: DCP, camera-linear RAW ownership, ICC LUT, ICC concurrency, preview ICC identity, render goldens and export encoding.
- [x] Repair the compatibility-aware brand gate and add four fail-closed regression checks to CI.
- [x] Pass candidate structural preflight and 16 commercial-release configuration tests; retain actual public-release blockers.
- [x] Reconfirm the complete Timeline lifecycle regression after isolating the cumulative test fixtures, alongside native Save/reopen/Recents proof.
- [x] Close and verify native photo-only Edit routing and its first-use/shortcut presentation.
- [x] Verify transformed AI composition review through focused model and controller regressions; retain the failed autonomous sample as evidence.
- [x] Remove duplicated local-model repair context and preserve font constraints, with production-planner regressions.
- [x] Verify the macOS Command mapping in Cocoa: open Command Palette, search, dismiss, and restore focus.
- [x] Verify the complete native first-use/shortcut workflow after the macOS Command mapping correction.
- [x] Verify media queues drain after process-start failures and critical-to-warning cache pressure.
- [x] Verify real selection-menu target sizing and keyboard/pointer opening at 130% interface scale.
- [x] Verify delivery preflight and offline-recovery surfaces in Cocoa; preserve a real keyboard-export artifact.

The prompt/schema checkoff covers the reproduced defects. Autonomous creative completion still requires a successful live-model result and broader production-job qualification below.

## Integration verification

The app and fourteen selected test targets built successfully in `/private/tmp/looksy-library-collections-build`. The initial sandboxed service checks failed to start the isolated managed catalog; an elevated run passed `agent_edit_controller_test` in 46.04 seconds. `agent_local_model_test` passes after aligning two stale prompt/schema expectations without changing their native semantic assertions. `catalog_asset_model_test` passes in 119.84 seconds, including cached photo targeting, actual value-change notifications, and same-count photo-to-video replacement. A stale thumbnail-key fixture was updated to the existing production v7 policy.

Other concurrent work is changing shared files during this pass. Build/test receipts record the tested artifacts; older source hashes are not asserted to describe the final whole tree. The original failures remain in [the focused log](evidence/app-polish-2026-10-08/integrated-focused-tests.log), and catalog-service verification is in [the elevated log](evidence/app-polish-2026-10-08/integrated-service-tests.log). Later native/source provenance is recorded below, with concurrent source drift kept explicit.

The separately registered `timeline_recovery_lifecycle_test` passed in 39.68 seconds. It covers the complete recovery segment without concealing the later media-analysis failure in the original broad test. With the 38,019,421-byte, 128-clip/40-checkpoint fixture, discovery admission was 3.46329 ms and selected-open admission was 0.6345 ms during this run. These measure entry into asynchronous work; they do not measure total recovery time or a painted frame. [Registered test log](evidence/app-polish-2026-10-08/recovery-lifecycle-registered.log), [behavior and limits](evidence/app-polish-2026-10-08/performance-recovery.md).

The rebuilt production planner regression passed after both repair-context deduplication and the post-normalization native font constraint fix. The synthetic rejected-call case preserves exact calls/errors, approved copy, successful layer IDs and font facts, while removing repeated schemas. [AI implementation and evidence](evidence/app-polish-2026-10-08/agent-completion.md).

The matched `native-model-10` attempt was externally terminated with SIGTERM before producing a plan. Its partial receipt contains only the initial inspection; there is no generated output or completed workflow claim. The source and binary remained unchanged, and the signal sender is undetermined. The earlier `native-model-09` sample and this failed attempt are preserved. [Model10 provenance](evidence/agent-workmanship-2026-10-08/native-model-10/provenance.json).

The temporary build directory was removed during the runtime interruption. Final verification moved to the validated `build/dev-macos-debug` preset tree; the app and eight selected test targets built successfully. The fourteen tracked source files remained unchanged throughout this build. Artifact hashes and the interrupted-build history are recorded in [final development provenance](evidence/app-polish-2026-10-08/final-dev-artifact-provenance.json). The current `agent_local_model_test` passed again in 1.91 seconds, including the concurrent outer-response font constraint guard. [Final planner test](evidence/app-polish-2026-10-08/final-dev-agent-model.log).

A later coordinated build of the app, delivery/offline UI shards and both broad Timeline test targets passed. Its 22 selected source hashes were stable except `agent_local_model.cpp`, which another ongoing task changed during and after the build. The 1.91-second planner result qualifies the earlier `bc72a99c` source and recorded binary, not that later drift. Current read-only inspection finds the repair-context and font-constraint paths still present; no successful real-model claim follows. [Later integration provenance](evidence/app-polish-2026-10-08/final-native-integrated-provenance.json).

The complete Cocoa `desktop_shell_accessibility_test` passed in 17.61 seconds. It covers Quick Start choices at 130% scale, Command Palette/Search/Escape/focus restoration, a named exposure field and typed `0.50` commit, and native keyboard/pointer opening of the selection menu with its 42 px inner target. The empty-selection route now checks actual selection count rather than a retained focused row; selected video and Timeline rows remain ineligible, while an empty selection opens the first visible photo. A real keyboard-driven PNG export completed with one finished job and a preserved 41,702-byte artifact. Fresh output preflight was ready before capture. [Workflow evidence](evidence/app-polish-2026-10-08/workflow-ux.md), [manifest](evidence/app-polish-2026-10-08/native-workflow-cocoa-exec/accessibility-manifest.json), [delivered PNG](evidence/app-polish-2026-10-08/native-workflow-cocoa-exec/first-run-keyboard-export.png).

Earlier native failures were retained and diagnosed separately: the exposure query ran before asynchronous workspace creation, the oversized capture exceeded this screen's usable area, and the CLI proof did not enter the main Qt application loop. The native test now uses `QApplication::exec()`; offscreen behavior is preserved. The 1600×1000 request is explicitly recorded as constrained to 1512×917, rather than claimed at the requested dimensions. The accepted run has a non-failing `CustomerAccountPanel.qml:166` null `busy` warning; this pass does not qualify that account surface.

Native `desktop_shell_delivery_export_test` passed in 3.64 seconds and `desktop_shell_offline_recovery_test` passed in 5.32 seconds. The delivery capture verifies 20-file preflight, one missing original and numbered collision handling; its completed-job display uses a seeded record/artifact, so it is not full batch-execution proof. Offline recovery verifies cached preview/edit preservation and visible recovery actions in Library, Edit and Export; it does not execute a full relink. These synthetic cases supplement the actual keyboard-export result above. Current source and native artifact receipts are recorded in [native UI provenance](evidence/app-polish-2026-10-08/native-ui-final-provenance.json). [Delivery log](evidence/app-polish-2026-10-08/native-delivery-final.log), [delivery captures](evidence/app-polish-2026-10-08/native-delivery-final/delivery-export-manifest.json), [offline log](evidence/app-polish-2026-10-08/native-offline-final.log), [offline captures](evidence/app-polish-2026-10-08/native-offline-final/offline-recovery-manifest.json).

The registered `timeline_media_queue_terminal_paths_test` passed in 5.12 seconds. It preflights invalid-shebang executables, then verifies failed ffprobe requests and ffmpeg poster work leave no stuck pending state, and verifies queued work drains after critical-to-warning pressure. These deterministic cases establish the two fixed terminal paths; they do not alone attribute earlier broad media timeouts. The initial focused test had an ordering/failure-return bug that was corrected before accepting the registered result. [Detailed queue evidence](evidence/app-polish-2026-10-08/performance-recovery.md).

The final queue rerun passed in 0.71 seconds after explicitly establishing eligible normal pressure for the two process-start failure fixtures; the separate critical-to-warning transition case remains unchanged. The initial rerun after cache/settings isolation had correctly deferred work under critical pressure before attempting process startup, so it could not test `FailedToStart`. [Current queue result](evidence/app-polish-2026-10-08/timeline-media-queue-terminal-current.log), [retained deferred run](evidence/app-polish-2026-10-08/queue-terminal-final.log).

The isolated full `timeline_document_controller_test` passed in 101.21 seconds after restoring test-owned canonical files that earlier backup/corruption cases deliberately removed or damaged. Before those restorations, the later cache manager correctly refused cleanup of an index containing unreadable projects. Settings/cache isolation and fixture restoration do not weaken production cleanup or media assertions. Earlier media-analysis stalls did not recur; that observation does not establish which fixed queue terminal path caused them. [Broad controller result](evidence/app-polish-2026-10-08/timeline-document-controller-restored-all-index-elevated.log).

The isolated recovery diagnostic preserved eight journal snapshots. It found the outgoing two-marker document followed by recovered one-marker documents; only the asynchronously populated `sourceContentSha256` differed from the prematurely captured expected fixture. The fixture now waits for verified source integrity before taking its expected snapshot and keeps exact whole-document comparisons. It also follows the actual surfaced Recent candidate and checks an exact ordered pair in append-only history, allowing later deliberate same-ID reloads. The current complete lifecycle test passed in 30.08 seconds. The earlier failure remains inspectable in [the field diff](evidence/app-polish-2026-10-08/recovery-fingerprint-diagnostic.json) and [raw journal](evidence/app-polish-2026-10-08/recovery-fingerprint-journal.jsonl). The subsequent no-argument broad recovery suite reached its 300.02-second CTest timeout without stdout or a terminal assertion. Its broader confirmation remains open; this timeout does not invalidate the separate focused pass or establish a diagnosed production defect. [Bounded broad recovery result](evidence/app-polish-2026-10-08/timeline-recovery-broad-current-elevated.log).

The native Cocoa `desktop_shell_timeline_workflow_test` passed in 30.80 seconds, with 25 captured states. Pointer trim/move, paired clip timing, gesture cancellation/Undo/Redo, decoded Play/Pause/seek, Source preview isolation, visible Save-versus-Render, durable save/reopen, and unsaved recovery from Recents passed. Switching to Edit left playback stopped with zero hidden frame/playhead change. The fixture is synthetic 640×480 video with silent AAC; paired timing does not establish audible synchronization or sustained 4K performance. Its standalone catalog shows an unavailable-catalog banner, so this is Timeline workflow evidence rather than healthy full-app startup qualification. [Native test](evidence/app-polish-2026-10-08/native-timeline-final.log), [workflow receipt](evidence/app-polish-2026-10-08/native-timeline-final/workflow.json), [captures](evidence/app-polish-2026-10-08/native-timeline-final/manifest.json).

The existing native P27 brush driver is stale for current QML, and the checked-in timing gate lacks the independent foreground/key-window/occlusion sampler used by historical receipts. Those old aggregate receipts cannot qualify this build. Native brush targets remain open; controller, event-only, or offscreen checks are not substitutes.

The [bounded performance skill](/Users/bradyreiner/.codex/memories/skills/photo-editor-perf-loop/SKILL.md) requires: "Stop immediately when a run cannot produce the metric needed for gating." With no current independent foreground sampler, another timing run would not establish the native brush acceptance metric. That qualification is left open explicitly.

## Verified output fix

The export preparation function creates a fresh raster for alpha flattening. It previously retained only the color profile, losing DPI, text, offset and device-pixel-ratio metadata. Its implicit painter coordinates also used display sizing rather than covering every physical source pixel for a 2× image.

The fix explicitly composites the entire source rectangle and preserves source metadata after painting. It does not alter color-transform selection or promote a new color pipeline. The new regression failed on the original helper, then passed after the change. JPEG round-trip DPI permits the format's integer-DPI quantization; in-memory preparation remains exact. Existing 48 PNG cases continue to prove lossless pixel/profile/metadata equality for queued and atomic exports.

Evidence: [export result](evidence/app-polish-2026-10-08/export-result.json), [before failure](evidence/app-polish-2026-10-08/export-before.log), [after result](evidence/app-polish-2026-10-08/export-after.log), [build](evidence/app-polish-2026-10-08/export-final-build.log).

Rebuilt color/output tests passed 7/7 in 9.63 seconds. No hardware skips were reported by this focused set. [Test log](evidence/app-polish-2026-10-08/color-regressions.log), [build log](evidence/app-polish-2026-10-08/color-regression-build.log). The real-RAW render-set and source-inventory contract self-tests also passed; they validate evidence handling, not independent camera fidelity.

## Verified release gate fix

The branding scanner rejected three legacy `PhotoEditor` directory-reader statements in the catalog model. These are compatibility reads for existing data, not public product copy. The scanner now permits only those exact statements in that file. Regression checks still reject displayed `PhotoEditor`, an allowed statement followed by displayed copy, and obsolete public names. CI runs these checks before scanning the real release surface.

Candidate preflight passes with `--allow-dirty`, which establishes structural readiness only. Public preflight remains failed for missing hosted checkout/prices/billing portal, legal publisher, public release repository, website/support address, Sparkle feed/public key and distribution archive/URL. This pass does not configure credentials, publish, notarize or claim clean-machine installation.

Evidence: [release results](evidence/app-polish-2026-10-08/release-checks.json), [public blockers](evidence/app-polish-2026-10-08/public-preflight.log).

## Product gates that require more evidence

- Broad recovery-suite completion, native brush feedback and representative large-document interaction budgets.
- A complete real-camera shoot through import, cull, selective batch edit, masks, export retry, relink and crash/relaunch recovery.
- Rights-cleared independent camera/profile/reference inputs and human blinded color acceptance, as required by the active color strategy.
- Complete supported creative jobs through the production model session with revision, cancellation, exact reversal, save/reopen and deliverable receipt; human photographic/design review.
- Clean-machine managed-service/model setup, trusted signing/notarization, installed update/rollback and configured public commerce/support.

Canonical owners: [capability verification](capability-verification-plan.md), [performance](performance-measurement-expansion-checklist.md), [masks/layers](mask-layer-selection-checklist.md), [UX](user-experience-improvement-checklist.md), [color strategy](color-engine-and-app-foundations-completion-strategy.md), [agent UX](local-model-agent-ux-checklist.md), [release](release-checklist.md), and [launch](production-launch-checklist.md).
