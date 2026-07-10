# Current Blind Spots And Improvement Areas

Last reviewed: 2026-07-09

## Purpose

This document captures the current downsides, blind spots, and improvement areas across the app. It is intentionally broader than a feature checklist. The main question is not "does a control or subsystem exist?" but "can a photographer trust the whole workflow on real shoots, real hardware, and real releases?"

Use this document as a risk map before adding new feature surface area. When a blind spot points to a visible capability, apply the rule from [capability-verification-plan.md](capability-verification-plan.md): fix it, hide it, mark it experimental, or cut it from the primary workflow.

## Executive Summary

The app has a large amount of foundation: native Qt/QML shell, C++ render engine, Rust catalog/services, nondestructive recipes, masks, layers, local agent plumbing, benchmark tooling, and release scaffolding.

The biggest downside is that breadth can make the app look more complete than the end-to-end product reality. The near-term risk is not lack of ambition. It is that too many surfaces can appear professional before they are proven together as one reliable workflow:

`import -> cull -> batch edit -> refine -> export -> relaunch/recover`

The app should bias toward fewer, stronger, fully verified workflows until that path is boringly reliable.

## Highest-Risk Blind Spots

| Priority | Area | Blind spot | Why it matters | Best next move |
| --- | --- | --- | --- | --- |
| P0 | Core workflow | The app still needs one end-to-end professional path that is faster than fragmented alternatives. | A wide editor without strong import/cull/batch/export flow can feel like a demo, even with many tools. | Drive the roadmap through [next-level-roadmap.md](next-level-roadmap.md) and prove the full workflow with real media. |
| P0 | Capability truth | Some docs and UI surfaces imply capabilities are complete while other docs still require a capability matrix and proof audit. | Users and developers lose trust when checked boxes, visible controls, and tested behavior do not mean the same thing. | Generate the complete capability matrix and classify each row as complete, partial, placeholder, broken, or hidden. |
| P0 | Color and output trust | Color has many controls, but the roadmap still calls out structural gaps around scene/display separation, output transforms, RAW input transforms, and Delta E validation. | A photo editor is judged by final pixels, not the number of sliders. | Make the structured color pipeline and validation harness the color roadmap's primary delivery path. |
| P0 | Preview fidelity and responsiveness | Recent work improved visible feedback and full-resolution retention, but this remains a fragile cross-cutting area split between QML geometry and controller publish logic. | If edits look delayed, blurry, stale, or color-shifted, users assume the editor is broken. | Treat visible-latency, full-resolution retention, and preview/export parity as release gates for every edit workflow. |
| P1 | Mask/layer complexity | Masks, layers, local adjustments, smart selections, retouch, and target-aware routing are powerful but easy to make confusing or disconnected. | Local editing is a differentiator only if users can understand what is selected, affected, linked, and exportable. | Keep one active edit target model visible and test target routing before adding more mask/layer affordances. |
| P1 | Catalog and storage reliability | Multi-drive, managed/referenced imports, offline media, relink, recovery, and local Postgres deployment are product-critical but still high-risk. | Photographers trust editors that survive moved drives, missing files, crashes, and relaunches. | Build relink/recovery flows and clean-machine catalog tests before expanding adjacent workspaces. |
| P1 | Export and batch delivery | Export exists, but professional trust requires batch throughput, retry/recovery, color/profile fidelity, filename collision handling, and large-job stability. | The product is not complete until final files can be delivered dependably. | Treat export as part of the core workflow, not a tail-end utility. |
| P1 | Representative proof | The repo has many focused tests, but real hardware, real RAW corpora, real large libraries, and real GPU validation remain easy to under-prove. | Synthetic or skipped hardware proof can hide the failures users actually see. | Maintain representative corpora and run release-proof gates on target hardware. |
| P2 | AI/model paths | Local agent and smart-selection plumbing exists, but cloud execution is not implemented, SAM ONNX inference is not implemented in this build, and fallback states must stay explicit. | AI features create high expectations and can damage trust when model availability is unclear. | Keep AI as an accelerator with visible fallback/error states, not as a hidden dependency for core editing. |
| P2 | Scope dilution | Timeline and carousel workspaces exist beside the photo editor. | Adjacent media workflows can absorb attention before the core photo product is credible. | Keep timeline/carousel experimental or secondary until the core shoot workflow is proven. |
| P2 | Release/commercial readiness | Packaging exists, but signing, notarization, real cert validation, updater publishing, credentials storage, clean-machine QA, and commercial policy are not finished. | A downloadable app has operational requirements that local dev builds do not exercise. | Keep release-readiness tracked as a product workstream, not a last-week packaging task. |

## Product And Workflow Blind Spots

### 1. Core workflow is still the real product test

The app should not be evaluated by isolated feature count. The product test is whether a photographer can import a shoot, rapidly cull it, batch edit keepers, refine with masks/crop/detail, export deliverables, relaunch, and recover from storage problems.

Current downside:

- The codebase has many strong subsystems, but the product risk is fragmentation.
- Culling and batch editing are still the highest-leverage workflow gaps called out by the roadmap.
- Workspaces like timeline and carousel can distract from the photo workflow unless deliberately deprioritized.

Improvement direction:

- Define one "professional shoot" acceptance script with real media and run it repeatedly.
- Make culling, batch sync, masks, crop, export, relaunch, and recovery part of one proof artifact.
- Avoid adding new primary UI until that script is smooth.

### 2. Capability meaning is not yet strict enough

There are multiple kinds of "done" in the repo: a control exists, a test exists, a checklist item is checked, a smoke test passes, a render path works, or a real workflow is trustworthy. Those cannot be treated as equivalent.

Current downside:

- Broad aspirational docs use many checked boxes, while verification docs still call for a capability matrix and classification.
- Some capabilities may exist in narrow form but still lack product behavior, export parity, reset/undo semantics, failure handling, or manual QA.
- A visible control without deterministic proof creates a trust problem.

Improvement direction:

- Build the capability matrix from QML controls, controller properties, recipe fields, render stages, Rust APIs, and docs.
- Classify each capability as `complete`, `partial`, `placeholder`, `broken`, or `hidden`.
- For every `partial` or `broken` row, decide: fix, hide, mark experimental, or cut.

### 3. Real photographer throughput needs stronger ownership

The roadmap correctly centers culling and batch editing. Those workflows need as much attention as rendering quality because they decide whether the app is useful on a full shoot.

Current downside:

- Many editing tools are deep, but a slow or incomplete library/cull/batch path will make the app feel single-image oriented.
- Keyboard-first review, compare/survey, focus check, ratings, rejects, filters, and sync groups need to operate as one fast loop.
- Batch edit must include selective sync, conflict preview, preset application, recipe persistence, thumbnail invalidation, and export queue consequences.

Improvement direction:

- Treat culling and batch sync as first-class product features.
- Add workflow-level tests for multi-selection edits and thumbnail/export consequences.
- Make keyboard throughput measurable and visible in performance artifacts.

## UI And Interaction Blind Spots

### 4. UI breadth can obscure the active editing model

The Studio surface is powerful but large. A user needs to always know the active target: base image, photo layer, adjustment layer, mask, retouch operation, crop, compare state, or transient brush stroke.

Current downside:

- Mask/layer routing has historically been easy to get wrong because global controls, layer-local controls, and selection-scoped controls can look similar.
- Tool state spans QML mode, inspector tab, active target, controller selection, render intent, and recipe state.
- If more controls are added without a stricter active-target contract, local edits can appear inert or unlinked.

Improvement direction:

- Keep one visible active-target model and make all edit commands route through it.
- Add source-scan or smoke guards for every new entry point that can create layers, masks, or local adjustments.
- Prefer fewer visible states over more parallel controls.

### 5. Advanced controls need a disclosure standard

The app has advanced color, diagnostic, AI, layer, mask, and proofing controls. These are useful, but they should not compete with core edits by default.

Current downside:

- Advanced controls can make the app feel technical before it feels fluent.
- Diagnostics and fallback states are necessary, but they should stay compact unless the user asks for detail.
- Complex AI/model/model-recovery UI can make simple edits feel heavier than they should.

Improvement direction:

- Keep the primary editing surface photographer-first.
- Put diagnostics, backend status, fallback explanations, and model internals behind explicit details affordances.
- Require every exposed advanced control to pass the capability verification gate.

## Render, Color, And Image Quality Blind Spots

### 6. Color architecture remains the deepest quality risk

The color roadmap already names the structural issue: the app needs a clean route from input profile to scene-linear working space to display/export transform. Many controls exist, but professional trust depends on architecture and validation.

Current downside:

- Working-space and output-transform assumptions can drift between preview, export, thumbnails, scopes, and presets.
- RAW normalization and white balance still need a first-class input transform model.
- Delta E/reference-chart validation needs to be a continuous harness, not ad hoc visual review.

Improvement direction:

- Make `colorPipeline` canonical and reduce duplicated output/render fields.
- Separate input transform, grading, and output transform in recipe and engine state.
- Expand chart, neutral-ramp, saturated-patch, and real-photo validation.

### 7. Preview/export parity must stay a hard invariant

The preview path has native texture publishing, lower-resolution drafts, visible tiles, full-resolution restore, compare/mask modes, and export. That gives good performance options, but it also creates many places for drift.

Current downside:

- A fast preview can still be wrong if it replaces a valid higher-resolution texture or skips a needed edit.
- A correct full render can still feel broken if the first visible feedback is delayed.
- GPU and CPU fallback reasons can make parity host-dependent.

Improvement direction:

- Pair every preview optimization with parity, stale-result, and full-resolution retention tests.
- Track first-visible feedback separately from settled/full-resolution render.
- Treat hardware skips as incomplete release evidence, not success.

### 8. RAW and detail quality need real corpus proof

RAW, shadows/highlights, detail, denoise, sharpening, profile handling, and lens correction should be judged on representative files.

Current downside:

- Synthetic tests are necessary but not enough for real camera behavior.
- Unsupported or partially supported camera/lens paths can look like bad editing math.
- Clean output requires more than preview correctness: export bit depth, profiles, metadata, and sharpening all matter.

Improvement direction:

- Maintain a small licensed RAW/reference corpus with dark, high dynamic range, skin, landscape, night, and saturated-color cases.
- Add per-camera support status and clear fallback/error states.
- Make export fidelity part of every image-quality milestone.

## Masks, Layers, Retouch, And Local Editing Blind Spots

### 9. Local editing is high-value but high-complexity

Masks and layers are the app's strongest differentiator only if the routing and mental model stay clear.

Current downside:

- Multiple mask sources, operations, adjustment layers, layer masks, retouch masks, AI masks, and brush states can easily create "why did nothing change?" moments.
- Smart selection and inpaint rely on model availability or fallback behavior.
- Some retouch/model modes are intentionally hidden or fallback-based.

Improvement direction:

- Keep mask/layer operations inspectable through a compact target/status strip.
- Prove every command path affects the intended target and no other target.
- Keep fallback selection and local inpaint states visible, editable, and recoverable.

### 10. Pixel-accurate interaction needs ongoing regression guards

Coordinate mapping, outline tracing, pixel-grid proof, visible-tile rendering, mask overlays, and canvas transforms are fragile because they combine QML geometry with source pixel math.

Current downside:

- Small layout changes can break pointer-to-pixel mapping.
- Fast live outlines can be mistaken for final alpha if state is not explicit.
- Pixel-grid and source-resolution inspection depend on full-resolution backing, not just zoom level.

Improvement direction:

- Keep source-scan gates around coordinate normalization and overlay contracts.
- Require final-alpha vs live/cached/fallback state metadata for every outline path.
- Keep visual proof artifacts for pixel-grid and mask-outline regressions.

## Catalog, Persistence, And Storage Blind Spots

### 11. Local Postgres is powerful but operationally heavy

The architecture uses PostgreSQL-backed local catalog services and managed local database bootstrap. That can be a strength, but it raises shipping and recovery complexity.

Current downside:

- Bundling/managing Postgres is more operationally complex than embedded storage.
- Generated DB credentials, service startup, migration failures, backups, clean shutdown, and repair flows need product-grade UX.
- The architecture doc still calls out the decision between bundled local service, synced backend, or hybrid offline cache.

Improvement direction:

- Prove managed local DB bootstrap on clean macOS and Windows machines.
- Add backup, repair, and migration failure recovery workflows.
- Decide and document the long-term catalog deployment model before beta.

### 12. Multi-drive and offline media flows must be first-class

The product direction includes referenced libraries, managed imports, and multi-drive awareness. That requires more than storing paths.

Current downside:

- Missing files, moved folders, disconnected drives, duplicate imports, and relinks are common real-world cases.
- If these states are not visible and recoverable, users will distrust the catalog.

Improvement direction:

- Build visible offline-drive/missing-file states in Library and Export.
- Add relink workflows for folders and drives.
- Test import/relink/export across referenced and managed modes.

## Export, Jobs, And Delivery Blind Spots

### 13. Export is the trust endpoint

Export needs the same quality standard as preview. It should handle large jobs, retries, cancellation, profile embedding, metadata, naming, collisions, and recovery.

Current downside:

- A good editor session can still fail the product if final delivery is slow, wrong, or unrecoverable.
- Export throughput and recovery are still called out as areas needing explicit gates.
- Professional output formats and profile-aware transforms are still part of the color/output roadmap.

Improvement direction:

- Add release-level export batch throughput gates.
- Test cancel/resume/retry and file collision behavior.
- Tie export settings to the structured color/output transform work.

### 14. Jobs need user-facing recovery, not just internal state

The app has job queues and telemetry. Users still need compact, actionable states.

Current downside:

- Import, thumbnail, export, model, timeline, and carousel jobs can fail for different reasons.
- Internal logs are not enough for a product user.

Improvement direction:

- Standardize job states: queued, running, retryable failure, blocked by missing input, cancelled, complete.
- Keep retry controls local to the failed work.
- Include enough diagnostics for support without exposing implementation noise by default.

## Performance And Operability Blind Spots

### 15. Instrumentation is strong, representative evidence is the risk

The performance and operability docs define useful metrics and suite artifacts. The blind spot is not whether metrics exist; it is whether the right workloads, hardware, and baselines are being used consistently.

Current downside:

- Local runs may skip hardware-bound GPU proof.
- Synthetic corpora may not expose large-library, real-RAW, or slow-drive behavior.
- A metric improvement is not enough if visual parity or user-visible latency regresses.

Improvement direction:

- Keep accepted baselines per profile and hardware class.
- Run release-proof on machines with actual target GPU support.
- Pair performance gates with visual/parity gates.

### 16. Visible latency and compute latency should stay separate

Users experience whether the app responds immediately. That is not the same as total render throughput.

Current downside:

- A render can finish quickly on a worker while result delivery or QML frames still lag.
- A full-resolution settle render can be correct while the first visible feedback feels late.

Improvement direction:

- Keep measuring first feedback, draft preview, settled preview, full-resolution restore, texture upload, and GUI delivery separately.
- Regressions in any of those should fail the relevant workflow gate.

## AI, Agent, And MCP Blind Spots

### 17. AI should accelerate work, not define core reliability

The local agent path, MCP integration, smart selection, and inpaint support are valuable, but they must stay optional and understandable.

Current downside:

- Cloud model execution is explicitly not implemented by the local agent runner.
- SAM ONNX inference is explicitly not implemented in this build.
- AI-dependent UI can feel broken if model setup, fallback, or unavailable states are not clear.

Improvement direction:

- Keep core editing usable without models.
- Show local model availability, fallback mode, and failure recovery in plain language.
- Require deterministic tests for model missing/invalid/valid states.

### 18. Agent UI should remain action-first

The agent can expose prompts, learned style, tool calls, audit/proof, and local-model status. That can overwhelm users if internals leak into the main flow.

Current downside:

- Too much model/process detail can make the feature feel like setup instead of editing.
- Generated edits need clear before/after, recipe diff, undo, and target routing.

Improvement direction:

- Keep the default agent surface focused on direct actions on the current photo or selected assets.
- Keep audit, tool calls, and detailed proof behind Details.
- Treat every agent recipe patch like a normal edit: reversible, inspectable, and exportable.

## Testing And Verification Blind Spots

### 19. Test volume is not the same as product proof

The repo has many tests across controller, math, render parity, smoke, catalog, export, timeline, and agent paths. That is good. The blind spot is the difference between focused proof and end-to-end confidence.

Current downside:

- A focused test can pass while the full workflow still feels fragmented.
- Some important evidence is host-dependent or skipped under headless/CI conditions.
- UI smoke tests can prove object names and state signatures but not all subjective interaction quality.

Improvement direction:

- Maintain focused tests for invariants, but add scenario tests for full-shoot workflows.
- Add manual QA scripts where visual quality and interaction polish cannot be fully automated.
- Keep proof statements tied to exact commands, artifacts, and host limitations.

### 20. Documentation can drift from implementation

The repo has many docs, checklists, plans, and status notes. That helps execution, but it also creates drift risk.

Current downside:

- Checked status can become stale after later changes.
- Some docs are aspirational, some are operational, and some are proof logs.
- New contributors may not know which docs are canonical.

Improvement direction:

- Add status labels to major docs: `canonical`, `roadmap`, `proof log`, `archived`, or `aspirational`.
- Keep one canonical source for priority order.
- Reference proof commands and artifact paths when marking items complete.

## Release, Security, And Commercial Blind Spots

### 21. Release engineering is scaffolded, not fully proven

The release guide lays out packaging paths, but several production tasks remain incomplete.

Current downside:

- Apple signing/notarization automation is not fully finished.
- Windows signing with a real cert still needs validation.
- Updater publish pipelines are not fully tested.
- Production icons/assets for Windows branding are not complete.
- Generated database credentials need Keychain/Credential Manager storage.

Improvement direction:

- Run clean-machine install tests for macOS and Windows before any alpha.
- Treat signing, notarization, updater feed, and DB runtime bundling as their own release milestone.
- Add release smoke tests for startup, DB bootstrap, import, edit, relaunch, and export.

### 22. Product policy and support systems are still needed

A real app also needs privacy, telemetry, licensing, update channels, diagnostics, and support workflows.

Current downside:

- Telemetry and diagnostics are technically useful, but user-facing policy and support flow need product decisions.
- Licensing/activation and update-channel strategy are still future product work.
- Support for rendering/catalog bugs needs reproducible reports and safe log collection.

Improvement direction:

- Write privacy/telemetry and diagnostics policy before broad testing.
- Define alpha/beta/stable update channels.
- Add a support bundle export that redacts secrets and includes relevant logs, system info, recipe, catalog state, and render diagnostics.

## Recommended Next Sequence

1. Create the capability matrix and classify every exposed feature.
2. Pick the single professional workflow script and make it the top-level acceptance test.
3. Close culling and batch-edit gaps before broadening adjacent workspaces.
4. Make structured color pipeline and output transform work the next image-quality foundation.
5. Prove local Postgres bootstrap, catalog recovery, and clean-machine relaunch.
6. Run release-proof performance and render gates on target hardware.
7. Hide or mark experimental anything that cannot pass the capability gate.

## Good Signals To Track

- A user can import 500+ real photos, cull with keyboard, sync a base grade, refine several keepers, export, relaunch, and recover missing media without developer intervention.
- Every visible edit control has product behavior, state transitions, reset/undo semantics, preview/export contract, and automated proof.
- Preview first feedback, settled preview, and full-resolution restore are separately measured and within budget.
- Color validation includes synthetic invariants, chart patches, real photos, and CPU/GPU parity where hardware supports it.
- Release artifacts install and run on clean macOS and Windows machines without developer tools.
- Docs no longer rely on ambiguous checked boxes; each complete claim links to current proof.

