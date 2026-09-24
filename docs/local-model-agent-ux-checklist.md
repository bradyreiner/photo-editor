# Local Model Agent UX Checklist

## Product Principle

The agent should do more while showing less.

The user should not manage model plumbing, MCP tools, validation internals, batch clustering, prompt context, or style JSON. The app should handle that automatically and only surface what the user needs to decide:

- What should be edited?
- What style should it follow?
- Is the sample right?
- Should it apply to the rest?
- What needs review?

Everything else belongs behind smart defaults, automatic checks, and expandable detail.

## Ideal User Flow

1. User selects photos or a project.
2. User opens one `AI Edit` surface.
3. App automatically checks local model, MCP tools, selected scope, and style readiness.
4. User chooses or learns a style.
5. Agent creates a small sample set.
6. User approves, corrects, or rejects the sample.
7. Agent applies the approved direction to the full set.
8. App only interrupts for outliers, unsafe actions, or real ambiguity.
9. User commits or exports.

## UX Rules

- [x] One primary agent surface.
- [x] One primary action at a time.
- [x] No visible setup maze.
- [x] No required JSON/schema/tool knowledge.
- [x] No separate "plan", "validate", "tool", and "feedback" mental models for normal users.
- [x] Advanced diagnostics are available, but collapsed by default.
- [x] The agent asks questions only when it cannot safely infer the answer.
- [x] Every automated action produces reversible drafts until the user commits.
- [x] The app shows proof, not process. The collapsed 1440x900 flow shows the selected photos, project look, plain-language direction, sample forecast, real before/after samples, protected regions, and edit-check receipts; model, tool, memory, retouch, and run internals appear only after `Details` is opened.

## Main Interface

- [x] Replace scattered model/style/run controls with one compact `AI Edit` panel.
- [x] Expose one controller workflow state that provides the compact status line, inferred scope, readiness, and primary action for the UI.
- [x] Wire the compact workflow state into the Edit and Studio AI panels as the default status/action surface.
- [x] Show a single status line:
  - [x] `Ready`
  - [x] `Setting up local model`
  - [x] `Needs style`
  - [x] `Creating samples`
  - [x] `Waiting for your review`
  - [x] `Applying to batch`
  - [x] `Review needed`
  - [x] `Ready to commit`
- [x] Show one main button based on state:
  - [x] `Set Up`
  - [x] `Choose Style`
  - [x] `Learn This Style`
  - [x] `Create Samples`
  - [x] `Apply to All`
  - [x] `Review Issues`
  - [x] `Commit`
- [x] Keep secondary actions small:
  - [x] Change style.
  - [x] Change scope.
  - [x] Add feedback.
  - [x] Roll back.
  - [x] Show details.
- [x] Collapse model, MCP, scope, run mode, and setup diagnostics under `Details`.
- [x] Collapse tool, prompt, validation, and report details under `Details`.

## Resident Agent Trust Loop

- [x] The active resident-agent entry creates a reversible draft instead of auto-committing the edit.
- [x] Keep `Commit Edits`, `Make Changes`, and `Discard` visible beside the real edited workspace.
- [x] Route an approved draft through the editor's normal history integration so the visible result remains undoable.
- [x] Keep the resident agent visible after a failed run and offer a direct retry path.
- [x] Show a compact proof receipt on a ready draft with the actual model or fallback, rendered-proof count, and quality score when available.
- [x] Prove the first live agent update arrives within 250 ms; run model planning, retouch materialization, and proof rendering through request-owned continuations; preserve cancellability and interactive-render priority; record zero resident event-pump iterations; and surface responsive work in the draft receipt.
- [x] Make the live vision-model smoke gate fail when fallback was used, the requested model did not answer, or no image payload evidence was recorded.
- [x] Allow the configured 32B local vision model enough time to finish a structured image request instead of silently forcing fallback at 30 seconds.
- [x] Restore each project's selected style, selected local model, and recent run receipts after project switches and app/controller relaunches without leaking another project's history.
- [x] Learn only from explicitly confirmed commits, storing a bounded, path-free recipe-diff exemplar for the exact active project.
- [x] Turn `Ask for changes` into an inline correction card that creates a new reversible draft instead of merely reopening review.
- [x] Promote explicit correction language into project memory only after the revised draft is confirmed, retaining compact rejected-versus-approved recipe evidence.
- [x] Treat a plain user discard as exact-draft avoidance evidence without inventing why it was rejected.
- [x] Require the vision model to publish bounded, evidence-based visual findings with target, severity, confidence, and normalized region geometry.
- [x] Compile at most two high-confidence localized actions into clamped feathered radial masks owned by the app rather than accepting raw model-authored recipes.
- [x] Append or update agent-owned local masks by stable ID so localized drafts preserve every existing photographer-created mask.
- [x] Honor localized-only requests by suppressing global tone, color, and crop changes while preserving explicit color and framing constraints.
- [x] Route explicit quadrant-based highlight recovery through grounded local luminance analysis in milliseconds, while reserving Qwen for semantic or ambiguous requests.
- [x] Label fast deterministic vision honestly in the draft proof receipt instead of implying that the selected LLM ran.
- [x] Retrieve approved project taste as preference evidence for later local-model plans without overriding the current request or leaking taste across projects.
- [x] Keep an awaiting draft tied to its originating project if the visible project changes before commit or rollback.
- [x] Surface the active project's restored memory in the workspace character instead of making continuity invisible.
- [x] Surface the count of approved project looks so learned taste is visible and inspectable rather than hidden behavior.
- [x] Expand project continuity into an editable Taste Mirror that explains the latest learned direction and separates approved looks, approved corrections, rejected drafts, and confirmed visual referents.
  - [x] Require an explicit second click before forgetting one evidence item, remove it only from the active project, exclude it from future project planning, and leave the active draft and existing edit recipe unchanged.
  - [x] State the local privacy boundary in the card: project-only memory stores no image bytes or source paths.
- [x] Route every executable plan through one app-owned, least-invasive edit strategy before creating the reversible draft.
  - [x] Distinguish global color, localized masks, composition, protected retouch, cluster-aware batches, and one-photo outlier repair from the actual validated recipe groups and run phase.
  - [x] Show the chosen strategy, why it was chosen, and what will stay untouched beside the draft instead of exposing planning internals.
  - [x] Feed a localized-only strategy mismatch into the pre-draft stop policy, and recompute risk from the final representative cluster plan even when that photo had no prior edit recipe.
  - [x] Compile an explicit, bounded, high-confidence cleanup request into an append-only retouch operation that preserves existing photographer retouch, and stop before drafting for approval.
  - [x] Apply the exact reviewed sensitive plan after explicit approval without rerunning the model or entering an approval loop; reject the resume if its project or style changed.
  - [x] Materialize approved remove/heal geometry into a full-resolution, app-owned inpaint patch with source-mask and runtime provenance; reject unapproved, empty, unsupported, failed, or oversized artifacts instead of publishing a no-op retouch.
  - [x] Consume the reviewed approval when the rendered cleanup fails proof, clear the pending approval, preserve the original recipe, and offer `Point to repair` instead of looping over the same doomed plan.
  - [x] Turn a rejected cleanup into an actionable visual critic instead of a generic failure: name the dominant photographic problem, show the measured evidence and delivery limit, preserve the exact failed-proof region, and expose a direct `Point to repair` action. One new point tightens the reviewed region to `82%`, discards the failed artifact, rebuilds from the untouched original, and requires fresh approval before materialization (`agent-visual-critique-v1`, `agent-proof-repair-plan-v1`). Controller coverage proves the repaired geometry and no-mutation boundary; desktop visual proofs cover rejection, point mode, and renewed approval in `/private/tmp/photo-editor-agent-visual-critic-20260822-v3/`.
  - [x] Prove the model-authored plan contains no binary artifact fields, the applied recipe retains the reviewed stable retouch ID, rendered pixels change inside the bounded target and nowhere else, provenance validation passes, and rollback remains exact.
  - [x] Scale full-resolution inpaint patch coordinates and pixels into bounded proof renders so the model and photographer review the same retouch that export receives.
  - [x] Run full-resolution agent retouch materialization on a background worker while servicing the UI event loop, and publish worker duration/responsiveness evidence in the materialization report.
- [~] Add a checksum-pinned real-photo retouch corpus route with exact target, approval, full-resolution artifact, proof locality, duration, and human-review fields. The v2 machine gate runs inside the production draft transaction against a retouch-only baseline and rejects surrounding color drift, gradient discontinuity, and target texture mismatch even when materialization/locality pass; rejected proofs restore the original catalog recipe before publication. Full-resolution spot cleanup now quality-selects by photographic surface instead of blindly preferring one runtime: six-pass harmonic fill for smooth backgrounds, the checksum-verified LaMa ONNX model for moderate fabric texture, and a boundary-matched nearby-pixel exemplar for strongly textured bark. The proof artifact records the selected candidate, alternate candidate, whether the alternate ran, model/provider, and texture evidence. Smooth backgrounds retain the strict `0.004` mean-delta gate; exemplar fills get a separately reported `8/255` textured seam gate while remaining subject to the unchanged locality and `0.5–1.6` reconstruction-gradient gates. On 2026-09-01 a fresh five-asset/nine-case production-controller run passed every machine gate, including the public smooth-sky, textured-fabric, and textured-bark retouch cases; report SHA-256: `d19447960c2259cba7e0b9f90b6c4b410ebcce4b6fc5ffc735c267e10d41aedc`. Focused coverage also proves that smooth spots skip LaMa, moderate-texture spots evaluate the CPU candidate before selecting real LaMa, clean tiny-spot removal, and materialized halo rejection with exact rollback. Human photographic-quality approval remains open for all nine cases.
  - [x] Ask for a clearer target when retouch evidence is below `0.85`, and refuse safely when the active style does not permit retouching.
- [x] Reject a second draft request while an agent run is active so nested UI event processing cannot start overlapping edits.
- [x] Separate clarification, sensitive approval, visible-object selection, and safe-stop states in the resident overlay.
  - [x] Show the model's actual clarification question with a compact free-text answer card instead of mislabeling it as an approval.
  - [x] Keep approval as an explicit exact-plan action, and give safe stops a dismiss action that cannot accidentally rerun the request.
- [x] Let the photographer ask for two genuinely different directions without losing the first draft.
  - [x] Snapshot Direction A exactly, restore its original recipe before planning Direction B, and render/validate Direction B through the normal request-owned draft path.
  - [x] Show the two actual rendered proofs side by side with direct `Choose A` / `Choose B` actions; block batch continuation and commit until one is chosen.
  - [x] Keep the choice reversible and out of durable memory until the selected direction is explicitly committed, then store only a compact project-local choice receipt.

## Smart Setup

- [x] On app launch or first agent open, silently check:
  - [x] Ollama installed.
  - [x] Ollama reachable.
  - [x] Selected local model installed.
  - [x] Model can respond.
  - [x] Model can inspect a proof image.
  - [x] Professional cleanup runtime availability is reported separately from core planning readiness; a missing model is labeled as a proof-gated fallback rather than full image-editing capability.
  - [x] MCP tools are reachable.
  - [x] MCP tool discovery is retried briefly before setup and draft planning so late local endpoints can recover without user action.
  - [x] App can render before/after proofs.
- [x] If setup is incomplete, show one `Set Up Local AI` action.
- [x] `Set Up Local AI` should handle as much as possible automatically:
  - [x] Start or detect Ollama.
  - [x] Download the selected recommended model.
  - [x] Test model response.
  - [x] Test image proof path.
  - [x] Test MCP tool access.
- [x] Install the checksum-pinned, license-reviewed local inpaint bundle and its pinned ONNX Runtime SDK. The production lock now fixes OpenCV Zoo LaMa revision `aee6d22f0a13e5e35af1c9a1c3afd62841fc6f3f` and Microsoft ONNX Runtime 1.29.0 macOS arm64 by exact byte count and SHA-256. `install-ai-runtime.py` safely downloads/extracts, records an installed-SDK file inventory, and re-verifies both artifacts; inference-enabled CMake rejects anything outside that verified paired install. The packaged app contains the verified 92,591,623-byte model and the required `libonnxruntime.1.dylib`, and a real ONNX pixel test passed with `meanMaskedChannelDifference=51.0417`, 3,111 ms cold, and 1,485 ms warm against 6,000/2,500 ms gates on the supported host. CoreML is optional and fails over to this same real ONNX graph; it remains off by default because macOS 26.3 rejected its temporary compile workspace in current validation.
  - [x] Revalidate the installed or environment-overridden bundle inside the app before readiness or inference; damaged bytes, path traversal, missing provenance, and tensor-contract drift now fail closed to the proof-gated fallback with a concrete diagnostic.
- [x] Only show low-level errors when automatic recovery fails.
- [x] Recommend one default model instead of making the user choose from a list first.
- [x] Move model switching to advanced settings.

Acceptance criteria:

- [x] A non-technical user can get to `Ready` without knowing what Ollama or MCP means.
- [x] A technical user can still inspect the backend details.

## Scope Handling

- [x] Infer scope from selection:
  - [x] One selected photo -> current photo.
  - [x] Multiple selected photos -> selected set.
  - [x] No selection but project active -> project.
  - [x] Filter active -> filtered set.
- [x] Show scope as a compact phrase: `24 selected photos`, `Current project`, `1 photo`.
- [x] Let user change scope with one small dropdown.
- [x] Warn only for meaningful risks:
  - [x] No editable photos. The compact preflight replaces forecast copy with an exact empty-scope stop instead of presenting an enabled batch promise.
  - [x] Huge batch without sample approval.
  - [x] Mixed edited/unedited references when learning style. `agent-style-reference-preflight-v1` distinguishes explicit edited examples, their legitimate paired originals, and unrelated originals; the look picker keeps its selected asset IDs synchronized, warns on mixed sets, and excludes unrelated originals from learned-style evidence.
  - [x] Missing source files. Scope and style-reference preflights stop with a direct reconnect/read-source explanation before planning or style learning.
- [x] Do not expose batch clustering until there are outliers or review results. The pre-edit surface now says how many photos are likely ready or may need the user's eye and promises samples first; cluster language stays internal until the result/exception phase.

Acceptance criteria:

- [x] The app chooses the likely scope correctly most of the time. Controller coverage proves current photo, multi-selection, active project, and filtered-library inference; `agent-filtered-scope-inference-v1` additionally renders 20 visible filtered photos from a 40-photo library and verifies all 20 IDs are targeted instead of falling through to an empty selection.
- [x] The user can override scope without entering a configuration flow.

## Style Handling

- [x] Always require a style, but make style creation simple. `reloadStyles()` selects a safe built-in when no style has been chosen, planning still stops if no valid style exists, and the compact `YOUR LOOK` card keeps the active look visible without exposing its schema.
- [x] Default to the last used style for the current project. Project continuity persists and restores `styleId` independently for each project and across controller relaunches; focused controller coverage proves Alpha and Beta restore different styles without leakage.
- [x] If edited references are selected, offer `Learn This Style`.
- [x] If no style exists, offer three starting choices:
  - [x] Learn from selected edits. The compact look card opens an edited-photo-only picker, excludes the active edit target, and requires an explicit example choice before it creates a learned project style.
  - [x] Describe a style. `Make this my look` turns the user's plain-language direction into a real project-sticky style in one action.
  - [x] Use a built-in style. `Built-in looks` opens only bundled safe starting points and preserves the current project choice.
  - [x] The 1440x900 `ai-edit-compact-entry` proof shows all three direct starts, while `agent-described-project-look` proves the visible saved-look receipt after the one-click description path.
- [x] Hide full style schema from normal users.
- [x] Show learned style as a taste card:
  - [x] Name.
  - [x] Reference count.
  - [x] Confidence: low / medium / high.
  - [x] Short style summary.
  - [x] Example thumbnails resolve approved catalog asset IDs at render time; project memory stores neither image bytes nor source paths.
- [x] Let user remove bad references directly from the taste card.
  - [x] The workspace Taste Mirror exposes confirmation-gated `Forget` controls for individual approved looks, corrections, rejected directions, and visual referents without changing existing edits.
- [x] Let user rename the style inline.
  - [x] The Taste Mirror stores a project-local display name without mutating built-in/global style definitions or the active draft.
- [x] Save advanced style fields in the background. `createStyleFromDescription(...)` generates bounded tool permissions, highlight/shadow and skin protections, before/after proof requirements, sample-first clustering, consistent batches, and outlier correction without presenting those fields in the primary UI; controller coverage inspects the persisted style.

Acceptance criteria:

- [x] Learning a style feels like selecting examples, not configuring an agent.
- [x] The user can see enough to trust the learned style without reading technical metadata.

## Agent Intelligence

- [x] The agent should automatically gather context before editing:
  - [x] Current recipe.
  - [x] Current proof image.
  - [x] Style profile.
  - [x] Relevant tool permissions.
  - [x] Validation thresholds.
  - [x] Batch similarities.
  - [x] Prior user feedback.
  - [x] Exact copy and source-canvas bounds for agent-owned graphic text layers when reviewing a rendered graphic.
- [x] The proof critic compares expected graphic copy with the rendered image, checks clipping, legibility, contrast, hierarchy, safe margins, collisions, and subject occlusion, and fails closed to photographer review when proof resolution is insufficient.
- [x] The agent should automatically choose the right edit path:
  - [x] Color-only correction when masks are unnecessary.
  - [x] Local masks when global edits cannot solve the issue.
  - [x] Crop/straighten only when composition benefits.
  - [x] Retouch only when allowed, explicitly requested, and grounded in bounded high-confidence evidence.
  - [x] Per-cluster batch edits when lighting differs.
  - [x] Per-image correction for outliers.
- [x] The agent should ask before:
  - [x] Major retouching.
  - [x] Large crop/composition changes.
  - [x] Destructive-looking edits. `agent-destructive-intent-seatbelt-v1` detects explicit overwrite, replace-original, discard/reset-existing-work, flatten, and bake-in wording while honoring negation such as `do not overwrite`; it pauses before recipe mutation, promises source/current-edit preservation, and resumes only as the exact reviewed reversible draft.
  - [x] Exporting or overwriting files.
  - [x] Ambiguous style intent.
- [x] The agent should refuse or pause when validation says the edit is unsafe.
- [x] Independently prove that rendered pixels moved in the explicitly requested direction before a draft can pass.
  - [x] Compare before/after luminance, contrast, saturation, and signed red-blue balance with deterministic thresholds and exact failure reasons.
  - [x] Cover brighter, darker, stronger/softer contrast, warmer, cooler, muted, monochrome, quieter, and explicit tone/color preservation in a varied generated regression corpus.
  - [x] Do not invent global requirements for vague requests or localized wording such as `warm the skin`.
  - [x] Reject and restore a controller draft that moves opposite explicit intent, while allowing requested soft/quiet histogram compression instead of repairing the intended effect away.
  - [x] Keep Direction B authoritative over conflicting Direction A adjectives, and compile explicit cool/quiet intent into a recipe that passes its rendered-pixel proof.
- [ ] Calibrate the deterministic pixel thresholds against a human-rated, real-photo corpus spanning cameras, lighting, skin tones, and difficult high-dynamic-range scenes.
  - [x] Define a versioned, license/checksum-pinned manifest contract for agent instructions, expected pixel checks, optional protected regions, and explicit human-review state.
  - [x] Run the actual deterministic planner, reversible controller, renderer, retouch materializer, and intent validator over the five-photo public RAW/JPEG corpus; a fresh current-tree run passed all nine machine cases on 2026-09-01, including quality-selected smooth-sky, real-LaMa textured-fabric, and local-exemplar textured-bark retouch (`agent_real_photo_corpus_machine_pass=9/9`, `agent_real_photo_corpus_human_pending=9`; report SHA-256 `d19447960c2259cba7e0b9f90b6c4b410ebcce4b6fc5ffc735c267e10d41aedc`).
  - [x] Keep repair iterations from reversing explicit brighter/darker, contrast, saturation, temperature, monochrome, or preservation intent relative to the current recipe.
  - [x] Build an auditable local [photographer-review workflow](agent-human-review-workflow.md) that copies and hashes the exact nine before/after proofs, draws protected/retouch regions, requires Pass/Fail plus a 1–5 rating and note for every case, exports digest-bound ratings, and refuses stale, altered, incomplete, or source-overwriting ingestion. The refreshed 2026-09-01 packet with the clipboard finish handoff is durable at `/Users/bradyreiner/Library/Application Support/PhotoEditor/PhotoEditor/agent-calibration/review-packet-20260901-copy-handoff/index.html`; packet digest `17c6e93aa2c3d8908079d80399be658547ef4e53eaa436134b5b39478aaab291`, contract-file SHA-256 `074a6a4405d6bf026adf7b99c841060de88310da15efec9376eb792995a8abd3`, and index SHA-256 `a07c6f1c35152e93d75e47367ea5433534299291c2d0431057f28fcd1ce57b3c`. The packet self-test and production desktop-controller verification of the durable copy both pass; the original signed packet remains unchanged.
  - [ ] Complete and record human ratings for every case; the durable blind-review packet is ready, but all nine public-seed reviews remain `pending`, so this parent gate is not complete and the Agent must not rate its own work.

Acceptance criteria:

- [x] The user sees fewer controls because the agent chooses tools intelligently. The default surface contains one direction field, three project-look starts, one collapsed `Details` affordance, and one state-driven primary action.
- [x] The app still guards sensitive or irreversible decisions. Major retouch, composition, destructive-looking intent, export/overwrite, ambiguity, and failed proof each have an explicit approval, clarification, or safe-stop contract.

## Obsidian-Style Memory

- [x] Store user-facing agent memory as local, readable documents, not only opaque database rows.
- [x] Treat memory like an internal Obsidian-style vault:
  - [x] Plain Markdown for human-readable notes.
  - [x] YAML front matter for structured fields.
  - [x] Stable links between styles, projects, shoots, feedback, and run reports.
  - [x] Tags for retrieval such as `#style/wedding`, `#lighting/reception`, `#feedback/skin-tone`, `#tool/masks`.
  - [x] Backlinks from a style to every project/run that trained or changed it.
- [x] Keep the vault local-first inside app data by default.
- [x] Let advanced users inspect, export, or redact the vault.
- [x] Add a direct `Open Vault` UI action for advanced users.
- [x] Treat Obsidian as an optional power-user surface, not a dependency in the normal editing flow.
- [x] Keep Obsidian actions behind Details/advanced UI so the main workflow stays `Select photos -> Choose style -> Make samples -> Apply to all -> Review outliers`.
- [x] Expose vault health/status to the app: local-first, Obsidian-compatible, schema version, note counts, and active style note.
- [x] Do not require users to manage the vault during normal editing.
- [x] Never store source image bytes in memory notes by default.
- [x] Store references as asset ids, recipe summaries, fingerprints, and user-approved notes.
- [x] Add thumbnail/proof links to durable memory only when safe/redacted.
  - [x] Run memory notes include relative links to app-owned generated proofs, validation reports, recipe diffs, and manifests.
  - [x] Durable memory still rejects source image paths, absolute app-data paths, base64 payloads, and embedded image bytes.
- [x] Add conflict detection when a user edits memory notes in Obsidian while the app is running.
  - [x] Memory status compares the current Markdown vault fingerprint against the last app-owned snapshot and reports outside added/modified/deleted notes.
  - [x] Advanced memory status shows the outside-change count without exposing hashes or raw file metadata.
- [x] Add an optional `Open in Obsidian` deep link when Obsidian is installed, with the current folder-open behavior as the fallback.
  - [x] Advanced memory controls expose `Open Obsidian` while preserving `Open Vault`.
  - [x] The controller reports the Obsidian URL, dry-run readiness, and folder-open fallback policy for tests and diagnostics.
- [x] Show a compact "style changed because..." summary after approved feedback writes durable memory.
  - [x] Direct style feedback and approved sample-review feedback carry `styleChangeSummary` into the run report.
  - [x] The default agent surface shows the latest style-change summary without requiring the user to open the Markdown vault.

Memory types:

- [x] `Style memory`: what the user's edit taste is.
- [x] `Project memory`: what this wedding/shoot needs.
- [x] `Feedback memory`: corrections the user gave during sample review.
- [x] `Tool memory`: which editor tools solve which kinds of photo problems.
- [x] `Run memory`: what happened in a previous agent run.
- [x] `Issue memory`: recurring validation failures and how they were fixed.

Suggested vault structure:

```text
agent-memory/
  styles/
    brady-wedding-editorial.md
  projects/
    2026-06-28-smith-wedding.md
  feedback/
    2026-06-28-sample-feedback.md
  runs/
    run-2026-06-28-001.md
  tools/
    masks.md
    color-grading.md
    skin-tone-protection.md
```

Acceptance criteria:

- [x] The agent can learn across shoots without hiding memory from the user.
- [x] The user can understand why a style changed without opening Obsidian.
- [x] Memory can be backed up, redacted, or reset.
- [x] Add explicit memory migration/versioning checks.

## Memory Retrieval Strategy

- [x] Do not dump the full memory vault into the model context.
- [x] Retrieve only the smallest useful packet for the current phase.
- [x] Use a staged retrieval plan:
  - [x] Before setup: no style memory needed.
  - [x] Before sample creation: active style card, project brief, latest feedback, representative examples.
  - [x] Before revision: current sample result, user feedback, failed validation checks.
  - [x] Before batch: approved sample recipe, cluster summaries, style constraints.
  - [x] Before outlier repair: only the outlier photo, its validation report, relevant fixes from prior runs.
- [x] Make the project brief a real, photographer-authored `Project North Star` instead of an implicit placeholder. It is editable and forgettable in the resident Taste Mirror, persists only for the exact project, enters future planning and proof critique as qualitative guidance, and cannot override the current request, style constraints, validation, or safety. Saving and forgetting leave an active draft and existing edits untouched; it stores no asset IDs, image bytes, or source paths and grants no automatic recipe, culling, retouch, or destructive authority. Controller coverage proves relaunch persistence, project isolation, two-step forgetting, and draft invariance; local-model coverage proves conflicting current-request color, palette, and framing instructions win. Rendered proof: `/private/tmp/photo-editor-agent-north-star-20260826/agent-project-north-star-1440x900.png`; manifest: `/private/tmp/photo-editor-agent-north-star-20260826/ai-edit-ux-manifest.json`.
- [x] Make North Star use visible on the draft instead of hiding it inside model prompts. `agent-project-north-star-draft-receipt-v1` is emitted only when the exact saved digest reached planning, counts actual rendered-proof validation and model-critique use separately, and labels the result as an `Assignment Compass`: the current request led, there is no direct numeric recipe/culling/retouch/destructive authority, no alignment is claimed, and final fit remains the photographer's judgment. The receipt survives `Apply to All` without rereading changed memory. Controller coverage proves the deterministic-proof-only path; rendered local-model + proof-review state: `/private/tmp/photo-editor-agent-north-star-compass-20260826/agent-project-north-star-1440x900.png`; manifest: `/private/tmp/photo-editor-agent-north-star-compass-20260826/ai-edit-ux-manifest.json`.
- [x] Use deterministic app logic for first-pass retrieval.
- [x] Use embeddings or similarity search only as a secondary ranking layer.
- [x] Summarize run history into compact, editable memory notes after runs.
- [x] Keep raw run artifacts separate from durable memory.
- [x] Require user-visible events when durable style memory changes.

Acceptance criteria:

- [x] The model sees enough context to be smart.
- [x] Context stays small enough for local models.
- [x] Old or irrelevant memories do not distract the model.

## MCP Tool Strategy

- [x] Treat MCP as the agent's controlled hands, not its brain.
- [x] The app remains the source of truth for recipes, validation, rendering, commits, rollback, and exports.
- [x] The model proposes intent and structured actions.
- [x] The orchestrator decides which tools are available at each phase.
- [x] Expose tools progressively:
  - [x] Setup phase: status and readiness tools only.
  - [x] Style phase: read style, learn style, summarize references.
  - [x] Sample phase: read asset, render proof, inspect schemas, and validate candidate patches.
  - [x] Revision phase: inspect current proof, style, schema, and validation state before repair.
  - [x] Batch phase: plan bulk context and validate batch without handing commit/export authority to the model.
  - [x] Outlier phase: inspect one issue, fix one issue, accept/rollback one issue.
  - [x] Commit/export phase: commit draft, export preflight, queue export with confirmation.
- [x] Do not expose every MCP tool to the model at once.
- [x] Cache tool schemas, but re-check for tool list changes.
- [x] Retry MCP discovery before local-model planning and treat `mcpAttached` as true only when usable tools are ready.
- [x] Use strict input and output schemas for every tool.
- [x] Prefer structured tool output over long text output.
- [x] Trim tool output before returning it to the model.
- [x] Add timeouts, retry limits, and cancellation to every tool call.
- [x] Require approval for:
  - [x] Commit.
  - [x] Export.
  - [x] Retouch.
  - [x] Large crop.
  - [x] File output.
  - [x] Any operation outside the active project scope.
- [x] Log every tool call into the run report.
- [x] Show only user-relevant tool events by default.

Acceptance criteria:

- [x] The model cannot accidentally discover a dangerous path just because a tool exists.
- [x] Tool use is auditable.
- [x] The UI remains simple because the orchestrator manages tool choice.

## Agent Strategy Loop

- [x] Implement the agent as a deterministic orchestrator around a local model.
- [x] Use this loop for normal editing:
  - [x] `Observe`: gather selected scope, active style, proof images, recipe summaries, and relevant memory.
  - [x] `Plan`: ask the model for a structured edit strategy or clarification question.
  - [x] `Constrain`: app filters proposed actions through style rules, permissions, and tool availability.
  - [x] `Act`: app calls MCP/editor tools.
  - [x] `Validate`: app renders proof and checks image/recipe/style quality.
  - [x] `Repair`: model gets compact validation feedback for a limited number of retries.
  - [x] `Review`: user sees samples or outliers.
  - [x] `Remember`: app saves approved feedback and run lessons into local memory.
- [x] Keep planning low temperature/deterministic for batch consistency.
- [x] Use separate strategy prompts by phase instead of one giant prompt.
- [x] Use compact few-shot examples from prior successful edits only when relevant.
- [x] Add a hard retry budget for model repair loops.
- [x] Add a hard tool budget per run phase.
- [x] Stop and ask the user when confidence is low or risk is high.

Acceptance criteria:

- [x] The agent feels smart because it follows a reliable loop. The live path resolves scope and project look, creates representative samples, accepts direct natural-language or point-grounded correction, transfers approved feedback, applies only to the remaining photos, collapses good results, and stops on real ambiguity or failed proof.
- [x] The model is never the only safety layer.
- [x] Prove the installed local model instead of relying only on fake transport. The opt-in `agent-live-local-runtime-smoke-v1` route probes the configured Ollama model, sends a real image-grounded planning request, requires a non-fallback reversible recipe, then sends the proof image through the real vision critique path. On 2026-08-26 the warm `qwen3-vl:32b` run stayed local-only and passed without automatic commit: plan `99,250 ms`, proof critique `71,693 ms`. Receipt: `/private/tmp/photo-editor-agent-live-ollama-20260826/live-runtime-receipt-warm.json`.
- [x] Keep long local inference honest and cancellable in the visible workspace. `agent-local-vision-planning-heartbeat-v1` names the local review stage, advances a real elapsed timer, states `NO PIXELS CHANGED` and `CANCEL SAFE`, and runs while request-owned planning leaves the UI event loop responsive. A controller regression verifies the local-only/cancel-safe/no-pixel-change event evidence. Full 1440×900 proof: `/private/tmp/photo-editor-agent-full-shell-20260826-v18/agent-local-vision-planning-heartbeat-1440x900.png`; manifest: `/private/tmp/photo-editor-agent-full-shell-20260826-v18/ai-edit-ux-manifest.json`.
- [~] Make deep local vision feel interactive on target Macs.
  - [x] Publish a conservative reversible first preview within 2 seconds while deep local vision continues. `agent-two-speed-planning-preview-v1` runs the deterministic project-look planner only for a single low-risk global edit, publishes through the transient catalog path, leaves Commit locked, preserves the exact original recipe, and restores it immediately on cancel, clarification, approval, safe stop, model failure, or proof failure. The validated deep result replaces the fast preview only after the existing bounded repair and proof-critique loop passes. Focused proof measured `6 ms` first publication against a `2,000 ms` budget and five publications through deep validation; baseline/candidate receipts: `/private/tmp/photo-editor-agent-two-speed-20260826/{baseline,candidate}.json`. Full 1440x900 proof: `/private/tmp/photo-editor-agent-two-speed-20260826-v1/agent-two-speed-fast-preview-1440x900.png`; manifest: `/private/tmp/photo-editor-agent-two-speed-20260826-v1/ai-edit-ux-manifest.json`.
  - [x] Validate the fast draft independently, define hardware-tiered deep-model targets, and let the photographer deliberately use that validated fast draft without waiting. `agent-fast-draft-independent-proof-v1` renders the deterministic candidate at a bounded 512 px edge and runs the same image, style, and pixel-intent checks concurrently with deep planning; rejection withdraws the preview and restores the exact original transient recipe while deep planning remains cancelable. `agent-fast-draft-hardware-deadline-v1` retains explicit 20 s performance (64+ GiB), 30 s balanced (32+ GiB), and 45 s constrained deep-review targets. `agent-proved-fast-choice-v1` no longer treats that target as an action gate: as soon as independent proof passes, the speech card, receipt, heartbeat, accessibility description, and **Use Proved Draft** action offer an immediate choice to use the reversible result or keep waiting for deep vision. Commit remains locked. `agent-validated-fast-draft-v1` then cancels only deep planning, converts the exact visible recipe into a review-required reversible draft, never commits automatically, ignores the late deep callback, and retains byte-for-byte rollback. Focused controller coverage measured a 26 ms independent proof and accepted the deliberate choice before a 5,000 ms test target, with exact rollback and late-callback rejection; forced proof rejection still restores the original and leaves deep planning cancelable. The controller, QML contract, and full AI Edit shard pass. Current 1440×900 immediate-choice proof: `/private/tmp/photo-editor-agent-proved-fast-choice-20260901/agent-validated-fast-draft-countdown-1440x900.png`; target-arrived proof: `/private/tmp/photo-editor-agent-proved-fast-choice-20260901/agent-validated-fast-draft-choice-1440x900.png`; manifest SHA-256 `0225f1615e6031584c8c8c901176a7f633e5c95aa7b45af3f317f0f98cce6700`.
  - [x] Keep the independently proved first look available after deep vision finishes instead of silently making refinement a one-way replacement. `agent-two-speed-draft-choice-v1` retains the exact fast and deep adjustment snapshots plus each version's recipe and rendered proof for the active single-photo draft. The photographer can switch **First Look ↔ Deep Review** without rerunning a model; every selection is explicit, project-bound, reversible, and uncommitted. Invalid or stale choices fail closed, direction comparisons cannot overlap, and Discard always restores the one exact pre-agent recipe rather than the previously viewed version. Focused controller coverage switched `deep → first look → deep`, rejected an invalid automatic choice without changing pixels, and then proved byte-for-byte original rollback. The QML contract and AI Edit visual shard pass. Current 1440×900 proof: `/private/tmp/photo-editor-agent-two-speed-choice-20260901/agent-two-speed-proved-draft-choice-1440x900.png`; manifest: `/private/tmp/photo-editor-agent-two-speed-choice-20260901/ai-edit-ux-manifest.json`; manifest SHA-256 `1c6eea2b2e9fc9e99a250d6df8a7fd42ef4110d71ea1d346022b19d754c98b86`.
  - [x] Let repeated proved-version choices teach restraint versus refinement without turning taste into hidden automation. `agent-two-speed-taste-signal-v1` stores only compact project-local choice metadata after a confirmed commit; switching previews, abandoning a draft, and a single choice cannot influence planning. Two or more consistent confirmed choices expose aggregate **First Look / Deep Review** counts and bounded qualitative guidance in the Taste mirror. The current request, visible photo, active style, and safety policy always win; both proved versions remain available and neither selection nor commit can happen automatically. `agent-two-speed-taste-forget-v1` requires a second confirmation and removes only this preference signal while retaining approved-look evidence and existing edits. Focused controller coverage proves the pre-commit boundary, two-commit threshold, future memory packet, exact original-recipe receipt, and narrow forget; the local-model request, QML contract, and AI Edit visual shard pass. Current 1440×900 proof: `/private/tmp/photo-editor-agent-two-speed-taste-20260901-final3/agent-project-taste-mirror-1440x900.png`; manifest: `/private/tmp/photo-editor-agent-two-speed-taste-20260901-final3/ai-edit-ux-manifest.json`; manifest SHA-256 `eafa7609f98056ce67d58555f5f9106d3e7f256fc57170ef5382e9ec8407253c`.
  - [x] Turn repeated confirmed choices into an honest end-of-assignment conversation instead of silently growing another memory channel. `agent-assignment-taste-reflection-v1` derives a compact project-local reflection only after the proved-version preference threshold is met and no Project North Star already exists. It shows the exact aggregate counts, labels the result **NOTHING REMEMBERED YET**, and can be dismissed for the session. **Shape North Star** only pre-fills the existing editable North Star control; the reflection is intentionally excluded from model memory packets, has no recipe authority, and writes nothing until the photographer explicitly presses **Save North Star**. Controller coverage proves no pre-commit or one-choice reflection, no automatic or model-packet influence, no image/path/snapshot storage, and removal with the underlying preference; the QML contract and full AI Edit shard prove prefill, cancel, and zero durable write. Current 1440×900 reflection proof: `/private/tmp/photo-editor-agent-assignment-reflection-20260901-v2/agent-project-taste-mirror-1440x900.png`; unsaved proposal proof: `/private/tmp/photo-editor-agent-assignment-reflection-20260901-v2/agent-assignment-reflection-north-star-draft-1440x900.png`; manifest SHA-256 `247b8cbc6feff81b734f1a1836c6200aaa19d6eb28a98fb746c26cf7e272831f`.
  - [~] Re-run the installed `qwen3-vl:32b` cold and warm paths on each supported Mac tier to verify the deep-review target with real transport and record p50/p95 evidence. The 48 GiB M4 Pro (`Mac16,8`) balanced tier is now directly proved by five unload-before-plan cold runs and five unchanged resident-model warm runs. All ten image-grounded plan/proof requests passed locally without fallback or automatic commit. Cold planning p50/p95 was `84,377/93,115 ms`; cold full-path p50/p95 was `116,398/128,920 ms`. Warm planning p50/p95 was `39,172/41,449 ms`; warm full-path p50/p95 was `47,221/50,120 ms`. The 30,000 ms balanced deep-review target precedes warm planning p50/p95 by `9,172/11,449 ms` and cold planning by `54,377/63,115 ms`, while cold planning p95 remains inside the 120,000 ms per-request guard. This target now communicates when deep review is taking longer than expected; it no longer withholds the independently proved reversible choice. Checksum-bound receipt: `/private/tmp/photo-editor-agent-live-tier-benchmark-20260827/aggregate-receipt.json` (SHA-256 `255038ee99d58e014a394fe2bb0f07366feb7cba292a66e372918c3f2cb518ff`). `scripts/perf/run-agent-live-tier-benchmark.py` now reproduces the five-cold/five-warm method, unloads the model for every cold sample, validates every local/image/approval invariant, rejects fallback receipts, verifies the declared tier against installed memory, and writes nearest-rank p50/p95 plus sample hashes. Performance and constrained Mac tiers remain open; the existing one-run warm receipt above is historical evidence, not a substitute for their five-sample distributions.
    - [x] Re-establish the current-tree warm and cold paths after the semantic vision contract grew. `agent-compact-semantic-intent-generation-v1` sends Ollama a bounded 1,138-byte generation schema while retaining the detailed app-owned normalization, deterministic recipe compiler, permission checks, retouch evidence checks, and fail-closed parsing. `agent-explicit-scope-authority-v1` also prevents the model from turning an explicitly requested overall look into a local-only edit merely because framing or highlights must be protected. On 2026-09-01 all five balanced-tier warm plan/proof runs passed locally with images, no fallback, no cloud, no automatic commit, and mandatory photographer approval. Planning p50/p95 was `68,266/69,159 ms`; full-path p50/p95 was `89,918/144,771 ms`; durable receipt: `/Users/bradyreiner/Library/Application Support/PhotoEditor/PhotoEditor/agent-calibration/performance/balanced-compact-warm-20260901/aggregate-receipt.json` (SHA-256 `ace81a6f23b2d02c9c40b0f87b542c66ccd2fc95e38669979e2f834d9514acad`). A fresh five-sample unload-before-plan distribution now also passes the unchanged current tree: cold planning p50/p95 is `105,917/112,462 ms`, proof-critique p50/p95 is `48,250/50,048 ms`, and full-path p50/p95 is `155,965/160,712 ms`. All five cold receipts remain local and image-grounded with no fallback or automatic commit, and every individual request stays inside the existing 120,000 ms guard. The 30,000 ms balanced deep-review marker precedes cold planning p50/p95 by `75,917/82,462 ms`; this correctly communicates a longer-than-expected deep review while the independently proved reversible First Look remains available, so no policy change is required. Cold durable receipt: `/Users/bradyreiner/Library/Application Support/PhotoEditor/PhotoEditor/agent-calibration/performance/balanced-compact-cold-20260901/aggregate-receipt.json` (SHA-256 `d09090f4b4fc97207c556e89fb111db6539bcb9440e39a516a973cc441f01854`). Increasing the guard to 180,000 ms, adding an empty-request preload, and a short-key adaptive global route were measured and rejected; the accepted V1 path remains intact. Performance and constrained tiers remain open.
- [ ] Batch output is consistent and reproducible enough for professional work.
  - [x] Compute `agent-professional-consistency-map-v1` from the real before/after render statistics for every drafted photo. It compares relative tone, contrast, warmth, green-cast, saturation, and available skin response against the representative inside each lighting group, so unlike scenes are not judged by an invalid global average.
  - [x] Make the consistency result deterministic: stable asset/group ordering, fixed versioned thresholds, no stochastic sampling, and a SHA-256 digest. Focused proof verifies the same eight-photo batch produces the same digest in reverse input order and isolates a deliberately divergent eighth photo.
  - [x] Show the result as a compact photographer-facing Consistency Map above exception review, with lighting-group cards and drift counts feeding the existing bounded Fix/Keep/Revert queue. Rendered proof: `/private/tmp/photo-editor-agent-consistency-map-layout-20260822/agent-professional-consistency-map-1440x900.png`.
  - [x] Turn measured drift into an actual finishing decision instead of leaving it as a chart. `agent-consistency-exception-routing-v1` merges every unresolved comparable drift into the commit-blocking exception queue, augments an existing validation issue instead of duplicating it, and keeps an explicitly accepted or reverted difference out of the queue. `agent-consistency-drift-repair-v1` names the divergent responses, shows measured evidence, offers a bounded `Match group` tone/color delta when safe, and refuses automatic skin-sensitive repair. `agent-consistency-repair-application-v1` preserves crop, framing, local masks, retouch, geometry, detail, and the source patch while requiring a fresh proof; focused contracts cover routing, safe repair, exact unrelated-edit preservation, and manual-review refusal. Desktop proof: `/private/tmp/photo-editor-agent-consistency-director-20260822-v2/agent-exception-first-batch-review-1440x900.png`.
  - [x] Keep the completion claim honest: the report explicitly sets `professionalReadinessClaimed: false` and `humanCalibrationRequired: true`; a coherent machine regression can pass `consistencyPassed` without claiming the product is professionally calibrated.
  - [x] Run repeated full real-photo deliveries and fail closed on stable-evidence drift. Two serial deliveries covered nine cases across five assets (18 executions total), passed all machine gates, produced exact matching proof/recipe/quality/runtime evidence with delivery digest `eb22c2a7f82a268daca1726a119d75c05dded1f4e5f93f50a1d1ba7bbfc36ffe`, and reported zero unexplained drift. Receipt: `/private/tmp/photo-editor-agent-delivery-rehearsal-20260822/reproducibility-receipt-v1.json` (`agent-professional-delivery-reproducibility-v1`, receipt digest `3c7f1b5347f8509771172ee961493453752bdd5ea64816f624ca9ad094303bf7`).
  - [x] Keep rendered-proof deadlines reliable across Mac sleep or host/VM clock discontinuities without weakening genuine active-work timeouts. `agent-proof-worker-active-clock-v1` now samples a 5 ms request-owned event-loop heartbeat while proof work runs outside the controller call; it counts every ordinary interval through 5,000 ms as active work and excludes only the suspended portion of a larger discontinuity. Proof reports publish the active clock contract, suspension count, suspended duration, active duration, and zero explicit event-pump iterations. `agent_suspend_resilience_test` proves the exact boundary and a 60,005 ms discontinuity, while the full controller regression proves cancellation restores the exact original recipe across a continuation.
  - [x] Make every local-model decision stage request-owned and replay-safe. `agent-request-owned-planning-continuation-v2` resumes primary planning, one bounded MCP tool-follow-up, per-lighting-group planning, and validation repair without controller-side planning waits; it reuses completed logical stages and per-asset proof critiques, freezes the original project/look/model/scope/photo set, and safe-stops on mid-run context drift. The automation/MCP surface now also exposes a confirmed exception-resolution action so external agent runs can clear a legitimate commit block instead of reaching a dead end.
  - [x] Make expensive app-owned materialization request-owned too. `agent-request-owned-worker-continuation-v1` runs approved retouch artifact creation and each proof render as cached per-asset/iteration stages, preserves the original progressive-preview recipe across every resume, forwards cancellation into the render loop, and retains background-stage evidence when the 64-event activity stream is compacted.
  - [x] Turn the final photographer gate into a real taste-calibration instrument instead of a machine-anchored pass/fail form. `agent-human-review-packet-v2` hides machine verdicts during judgment, requires deliverability plus four photographic dimensions and structured failure reasons, and signs the exact reviewed pixels. `agent-photographer-calibration-profile-v1` then records taste tendencies, machine/human disagreements, and threshold-specific tighten/relax signals while explicitly forbidding automatic threshold mutation. Nine-case rendered proof: `/private/tmp/photo-editor-agent-human-review-20260825/quicklook/index.html.png`; signed packet digest: `af5d9d7bf0e97ed536a524003aab2245572357ff86cbc68955a04f6c94142140`.
  - [x] Make the nine-case judgment session practical enough to finish without weakening its blind-review contract. `agent-blind-light-table-v1` adds side-by-side, movable split, and After-only inspection; hold-to-Before and keyboard split nudging; first-unfinished navigation; completion marking; and digest-scoped local autosave for decisions, ratings, notes, and photographer name. The saved draft explicitly contains no proof pixels, cannot cross into a different packet, and remains optional when browser storage is unavailable. Machine verdicts, machine failures, and source paths remain in the signed contract but are removed entirely from the review page payload. The export stays bound to `agent-human-review-ratings-v2`, exact proof hashes, and the hidden machine report. Nine-case render: `/private/tmp/photo-editor-agent-blind-light-table-v2-20260826/quicklook/index.html.png`; packet digest: `896055780538fc9eaff30a25c7ba0743f0cfb0f09d609807c3eb6cd810f10dc7`.
  - [x] Close the product loop from Agent to photographer judgment instead of leaving the review session discoverable only through documentation. Taste now presents one compact human-only calibration card with **Review proofs** and **Import profile**. `agent-photographer-calibration-review-launch-v1` accepts only a packet `index.html`, `review-contract.json`, or their directory; verifies the v2 packet digest, exact case counts, safe packet-local paths, every copied before/after SHA-256, light-table safety flags, and HTML redaction before opening it; and fails closed on a changed proof. The handoff writes no memory, imports no ratings, and explicitly records that the Agent cannot complete photographer judgment. Focused coverage includes a tampered-proof rejection and byte-for-byte desktop verification of the real Python-built nine-case packet (18 proof hashes). Rendered Taste handoff: `/private/tmp/photo-editor-agent-calibration-handoff-20260826/agent-project-taste-mirror-1440x900.png`; manifest: `/private/tmp/photo-editor-agent-calibration-handoff-20260826/ai-edit-ux-manifest.json`.
  - [x] Close the post-review desktop gap instead of making the photographer run two Python commands after exporting ratings. **Finish ratings** now selects the human export and signed packet, then `agent-photographer-calibration-native-finalize-v1` re-verifies the packet, proof identities, immutable source evidence, exact case set, timezone-aware signature, strict integer ratings, dimensions, tags, and notes. It writes idempotent reviewed-manifest and profile artifacts without overwriting the source, imports only the compact project-local profile, and preserves the no-auto-rating, no-threshold-mutation, and no-professional-readiness boundaries. Focused controller coverage includes source preservation, idempotent replay, and string-coercion, timezone, and proof-tamper rejection. Full AI Edit runtime render: `/private/tmp/photo-editor-agent-calibration-finish-full-20260826/agent-project-taste-mirror-1440x900.png`; proof manifest: `/private/tmp/photo-editor-agent-calibration-finish-full-20260826/ai-edit-ux-manifest.json`. The same run also proves the Story Map's role-evidence receipt remains visible at 1440×900 instead of collapsing out of the review card.
  - [x] Remove the redundant second packet choice from the normal calibration finish. After **Review proofs** verifies and opens a signed packet, `agent-photographer-calibration-session-handoff-v1` retains that packet reference only for the current UI session and exact project. **Finish ratings** then needs only the exported ratings file, reuses the exact verified packet, and clears the session reference after successful native verification or when the active project changes. Relaunch, a project change, or a missing session reference falls back to an explicit packet choice; no packet path is written to durable memory. Repeated full AI Edit shards pass after also making visual capture wait for stable non-zero Qt Quick layout. One-file handoff proof: `/private/tmp/photo-editor-agent-full-shell-20260826-v16/agent-calibration-packet-ready-1440x900.png`; manifest: `/private/tmp/photo-editor-agent-full-shell-20260826-v16/ai-edit-ux-manifest.json`.
  - [x] Remove the Downloads/file-picker round trip from the normal photographer finish path. The blind packet now copies a signed ratings-and-notes payload with a local-file-safe fallback; **Finish copied ratings** reuses the exact session/project-bound packet, re-runs every existing validation, writes no photo bytes or source paths, clears the clipboard only after success, and keeps **Download JSON** plus the file finalizer as recovery. The machine still cannot supply a rating, and a missing, stale, malformed, incomplete, or wrong-packet clipboard payload fails closed. The refreshed durable nine-case packet is `/Users/bradyreiner/Library/Application Support/PhotoEditor/PhotoEditor/agent-calibration/review-packet-20260901-copy-handoff/index.html` (unchanged packet digest `17c6e93aa2c3d8908079d80399be658547ef4e53eaa436134b5b39478aaab291`, HTML SHA-256 `a07c6f1c35152e93d75e47367ea5433534299291c2d0431057f28fcd1ce57b3c`). Desktop proof: `/private/tmp/photo-editor-agent-calibration-clipboard-20260901-v2/agent-calibration-packet-ready-1440x900.png`; manifest SHA-256: `04f841e59fb144b9535ad81456764eab60122696683faeddb3495d5ced77260a`.
  - [x] Make calibration feel like teaching a trusted assistant instead of importing a statistical report. `agent-photographer-calibration-coach-v1` deterministically ranks signed machine/photographer disagreements, then Taste shows one compact first-person lesson naming whether the Agent was too confident or cautious, the photographer's actual deliverability decision, weakest photographic dimension, issue tags, and a bounded next move. It remains project-local across relaunch, disappears after confirmed calibration forget, stores no pixels or paths, writes no new memory, and cannot self-rate, auto-train, or change a threshold. Focused controller and QML contract coverage pass. The Coach now also passes inside repeated full desktop AI Edit shards; current render: `/private/tmp/photo-editor-agent-full-shell-20260826-v16/agent-project-taste-mirror-1440x900.png`; manifest: `/private/tmp/photo-editor-agent-full-shell-20260826-v16/ai-edit-ux-manifest.json`.
  - [x] Consume that signed profile in the actual desktop Agent instead of leaving calibration as an offline report. The Taste mirror imports or replaces a digest-verified profile for the exact active project, exposes its blind-case summary, feeds qualitative restraint into deterministic/local-model planning and proof critique, and forgets it only after confirmation without changing existing edits. Cross-runtime fixture coverage proves Python-to-Qt signature compatibility, project isolation, relaunch persistence, prompt provenance, tamper rejection, and the invariant that numeric proof thresholds and professional-readiness claims remain untouched. Rendered desktop proof: `/private/tmp/photo-editor-agent-calibration-ui-20260825/agent-project-taste-mirror-1440x900.png`; evidence manifest: `/private/tmp/photo-editor-agent-calibration-ui-20260825/ai-edit-ux-manifest.json`.
  - [x] Let the photographer deliberately carry verified taste between projects without creating silent global memory. `agent-photographer-taste-passport-v1` exports only the compact blind-review calibration behind nested SHA-256 digests, hashes rather than names the source project, and excludes pixels, source paths, automatic training, threshold mutation, and professional-readiness claims. Import is an explicit file action for the active project only: `agent-photographer-taste-passport-preview-v1` first verifies the entire passport without writing memory, shows the signed lesson summary and whether current calibration will be replaced, and requires **Use for this project**. Confirmation pins the reviewed passport digest, re-reads and revalidates the file, refuses a preview/import race, records visible passport provenance, leaves every other project and active draft untouched, and fails closed after outer-envelope or embedded-profile tampering. Focused controller coverage proves cross-project transfer, source isolation, project-name redaction, relaunch persistence, tamper rejection, and changed-after-preview rejection. 1440×900 verified-preview proof: `/private/tmp/photo-editor-agent-taste-passport-preview-20260827/agent-taste-passport-preview-1440x900.png`; confirmed state: `/private/tmp/photo-editor-agent-taste-passport-preview-20260827/agent-taste-passport-ready-1440x900.png`; manifest: `/private/tmp/photo-editor-agent-taste-passport-preview-20260827/ai-edit-ux-manifest.json`.
  - [x] Turn calibration uncertainty into a direct taste-teaching moment instead of silently biasing the plan. When a rendered draft touches a profile concern such as warmth, `agent-photographer-taste-check-v1` names the uncertainty and offers one deliberate action that renders the current direction beside a more restrained alternative. Both panes are real draft proofs; commit stays blocked until the photographer chooses one, and the calibrated choice becomes project-local taste only after `Commit Edits`. The comparison carries its profile digest and concern provenance while retaining the no-threshold-mutation and no-professional-readiness invariants. Workspace proof: `/private/tmp/photo-editor-agent-taste-check-20260825/agent-photographer-taste-check-1440x900.png`; two-direction proof: `/private/tmp/photo-editor-agent-taste-check-20260825/agent-photographer-taste-directions-1440x900.png`; manifest: `/private/tmp/photo-editor-agent-taste-check-20260825/ai-edit-ux-manifest.json`.
  - [ ] Calibrate the fixed response thresholds against photographer ratings for the nine review-pending cases before setting this parent gate complete. The fresh 2026-09-01 machine run and signed packet are complete, but no human rating is inferred from machine evidence. The receipt must keep `humanCalibrationRequired: true` and `professionalReadinessClaimed: false`, and `--require-human-calibration` must continue to fail until all nine ratings are signed, ingested, and followed by any required threshold rerun.

## Sample-First Review

- [x] Always create samples before applying to a large set.
  - [x] Large selected-set drafts now apply only representative cluster samples first, preserve the full target list, and keep remaining photos unchanged until confirmed `Apply to All`.
- [x] Pick samples automatically from representative lighting/composition groups.
  - [x] Add assignment-level story intelligence without pretending to recognize narrative semantics. `agent-assignment-story-map-v1` deterministically suggests atmosphere, hero, detail, transition, and support roles from explicit curation, source order, and lighting-group boundaries; its anchors join the real sample review, while source order, culling state, recipes, and durable memory remain unchanged until the photographer acts. The visible receipt says exactly that there is no auto-cull, and controller coverage proves stable role/digest output plus `sample-review-coverage-only` planning influence. Rendered proof: `/private/tmp/photo-editor-agent-story-map-20260825/ai-edit-sample-review-1440x900.png`; manifest: `/private/tmp/photo-editor-agent-story-map-20260825/ai-edit-ux-manifest.json`.
  - [x] Let the photographer direct the suggested story without silently changing the full assignment. `agent-assignment-story-role-revision-v1` gives the shown sample a compact Atmosphere, Hero, Detail, Transition, or Support assignment; every choice deterministically swaps the prior role holder, rolls back the live samples, and rebuilds real proofs while every remaining photo and the source order stay untouched. Repeated role changes remain transient to the exact project and photo set; a controller regression proves Atmosphere → Hero → Detail and verifies that the memory-vault fingerprint and note count do not move. Five-role menu proof: `/private/tmp/photo-editor-agent-story-director-full-v2-20260825/agent-story-director-role-menu-1440x900.png`; repeated non-Hero proof: `/private/tmp/photo-editor-agent-story-director-full-v2-20260825/agent-story-director-detail-steered-1440x900.png`; manifest: `/private/tmp/photo-editor-agent-story-director-full-v2-20260825/ai-edit-ux-manifest.json`.
  - [x] Direct every real sample instead of silently targeting sample one. The bounded story navigator now switches the active before/after proof pair, position receipt, asset ID, current role, and role-revision target together; previous/next stop at the real sample boundaries, and a rebuild preserves the active sample. Desktop smoke navigates from `asset-1` to `asset-2`, waits for its distinct real proof pair, assigns Support, and proves `2 / 2` remains active with the remaining batch and memory untouched. Rendered proof: `/private/tmp/photo-editor-agent-story-navigator-v3-20260825/agent-story-director-second-sample-steered-1440x900.png`; manifest: `/private/tmp/photo-editor-agent-story-navigator-v3-20260825/ai-edit-ux-manifest.json`.
  - [x] Explain every suggested or photographer-directed role without inventing scene understanding. `agent-assignment-story-role-evidence-v1` records a deterministic basis, exact curation/order/lighting signals, sequence and cluster provenance, whether the photographer assigned the role, and `semanticRecognitionUsed: false` for every asset. The active sample shows one compact `WHY <ROLE>` receipt that changes from evidence such as `SOURCE ORDER` to `YOUR DIRECTION`, plus an explicit `NO SCENE CLAIM` boundary. Controller coverage verifies exact explicit-curation and photographer-direction evidence; rendered proofs: `/private/tmp/photo-editor-agent-story-evidence-20260825/ai-edit-sample-review-1440x900.png` and `/private/tmp/photo-editor-agent-story-evidence-20260825/agent-story-director-second-sample-steered-1440x900.png`; manifest: `/private/tmp/photo-editor-agent-story-evidence-20260825/ai-edit-ux-manifest.json`.
  - [x] Carry the photographer-approved arc through `Apply to All` and `Commit Edits` as an inspectable delivery contract. `agent-approved-assignment-story-receipt-v1` binds the approved story-map digest, role-evidence contract, approval source, full-set and newly drafted counts, and exact safety boundaries into both the run report and `batchSummary`, including the committed report. The final review changes from a steering surface to `STORY DIRECTION · APPROVED FOR DELIVERY`, hides stale sample controls, and states that source order is kept with no auto-cull, semantic scene claim, role-based recipe variation, or story-direction memory write. Controller coverage proves the planned recipe patches are unchanged across approval and the exact receipt digest survives commit. Rendered proof: `/private/tmp/photo-editor-agent-story-approval-20260825/agent-approved-story-delivery-receipt-1440x900.png`; manifest: `/private/tmp/photo-editor-agent-story-approval-20260825/ai-edit-ux-manifest.json`.
  - [x] Make story taste teachable only when the photographer explicitly asks. After a committed story delivery, one compact `Remember this story` action writes `agent-story-preference-memory-v1` to the exact project with normalized source-order role anchors, a visible memory event, and a two-click forget path. Future sets in that project can use it only for role-anchor suggestions and still require fresh sample review; it stores no asset IDs, image bytes, or source paths and has no recipe, culling, or semantic authority. Controller coverage proves unconfirmed actions are no-ops, another project cannot see it, future recipes remain byte-identical, and confirmed forget removes its influence without changing edits. Rendered proof: `/private/tmp/photo-editor-agent-story-memory-20260825/agent-story-preference-remembered-1440x900.png`; manifest: `/private/tmp/photo-editor-agent-story-memory-20260825/ai-edit-ux-manifest.json`.
- [x] Show only the sample outputs first.
- [x] For each sample, show:
  - [x] Before/after.
  - [x] Short explanation.
  - [x] Pass/warn/fail badge.
- [x] Primary review actions:
  - [x] `Looks good`
  - [x] `Make changes`
  - [x] `Reject`
- [x] `Make changes` opens one natural-language feedback box.
  - [x] The live sample review replaces its approval action with one inline `Make Changes` composer and one outcome-named revision action.
  - [x] Reopening an active reversible draft returns directly to review instead of resetting the user to the prompt.
- [x] Feedback should immediately revise the sample, not just save notes.
  - [x] Sample feedback rolls back the current sample draft, reruns sample creation with the feedback in context, leaves remaining photos untouched, and defers durable style-memory writes.
  - [x] After revision, show an approval-gated transfer receipt generated from the revised batch plan: the exact lesson, updated sample count, remaining-photo count, lighting-group policy, untouched-batch guarantee, and project-memory boundary (`agent-correction-transfer-preview-v1`).
- [x] After approval, offer `Apply to all`.
  - [x] Confirmed sample review continuation drafts only the remaining target photos in the same rollbackable draft transaction.

Acceptance criteria:

- [x] The user does not need to understand planning or validation to approve direction.
- [x] Feedback has an immediate visible effect.

## Review And Outliers

- [x] Forecast review effort before a batch without pretending uncertainty is certainty.
  - [x] `agent-batch-review-forecast-v1` reports exact representative-sample and known-blocker counts immediately; it withholds a numeric exception promise and says `forecast calibrating` until the active project/style has real outcomes.
  - [x] Once evidence exists, the preflight estimates likely automatic versus review counts, shows a bounded review range and low/medium/high confidence, and uses at most five project-local prior runs.
  - [x] Run history stores only `agent-batch-review-evidence-v1` counts—never paths or image bytes—and completed batch reports evaluate the prior estimate against the actual exception count.
  - [x] The compact entry proof shows `18 likely automatic · 2 likely review` from 120 prior project photos; controller coverage proves both the honest calibrating state and a learned bounded estimate.
- [x] After batch application, show a simple result summary:
  - [x] Edited count.
  - [x] Passed count.
  - [x] Needs review count.
  - [x] Failed count.
  - [x] `batchSummary.reviewSummary` now provides these counts, and the compact batch status line uses them when available.
- [x] Hide successfully edited images by default.
  - [x] The Edit view prefers `batchSummary.reviewIssues` for the visible batch rows and only falls back to all asset reports when no issues exist.
- [x] Show only outliers and failures first.
  - [x] `batchSummary.reviewIssues` is ordered from the validation/review pass and carries only failures, warnings, style misses, or independently corrected outliers.
- [x] For each issue, show:
  - [x] Thumbnail.
  - [x] Reason.
  - [x] Suggested fix.
  - [x] `Fix`
  - [x] `Accept anyway`
  - [x] `Rollback this photo`
  - [x] Issue rows now show the rendered proof thumbnail, the validation/style reason, and the suggested action from `batchSummary.reviewIssues`.
  - [x] Issue rows expose working fix, accept, and rollback actions; fix re-drafts that one photo with a bounded validation repair, accept resolves the issue without changing pixels, and rollback restores that photo's pre-agent recipe while keeping the remaining draft active.
- [x] Offer `Fix all outliers` when safe.
  - [x] Bulk repair is shown only when `reviewSummary.fixAllOutliersAvailable` is true, then reuses the one-photo bounded repair path for each current review issue.
- [x] Keep detailed validation scores under expansion.
  - [x] Each issue row has a collapsed `Details` toggle for style score, warning/error counts, cluster id, outlier correction, and proof path.

Acceptance criteria:

- [x] The review workload scales with problems, not batch size.
  - [x] `agent-exception-review-v1` emits one batch receipt plus an exception-only queue, records `reviewWorkItemCount`, and explicitly guarantees `queueContainsOnlyExceptions` and `workloadScalesWithExceptions`. The conversation review page binds its `ListView` only to `batchSummary.reviewIssues`; verified photos are represented by the collapsed receipt count instead of row delegates.
- [x] A batch of 500 good edits does not create 500 things to inspect.
  - [x] The focused controller contract test proves both boundary cases: 500 verified photos produce zero review work items, while 498 verified photos plus two exceptions produce exactly two work items. The desktop smoke test captures the latter as `agent-exception-first-batch-review-1440x900.png`, verifies the `498 verified · 2 need review` receipt, and proves Apply stays disabled until the two exceptions are resolved.

## Tool Permissions Without Tool Management

- [x] Do not show a wall of tool toggles by default.
- [x] Use permission modes:
  - [x] `Protect`
  - [x] `Balanced`
  - [x] `Transform`
- [x] Translate modes into allowed tools internally.
  - [x] The active run policy maps `Protect` to global color/tone/curves only, `Balanced` to the saved project look's permissions, and `Transform` to bounded color, crop, detail, local-mask, photo-layer, and editable graphic-text authority. Retouch, destructive actions, and export remain approval-gated in every mode.
- [x] Show a plain summary:
  - [x] Each mode explains its real edit envelope in one sentence next to the choice; technical permission fields stay out of the default surface.
- [x] Let advanced users expand and edit exact tool permissions.
  - [x] Exact per-tool checkboxes are hidden unless the Agent `Details` drawer is open.

Acceptance criteria:

- [x] Normal users choose intent and risk level.
  - [x] The compact Agent entry exposes one-click `Protect`, `Balanced`, and `Transform` choices before the direction field. `agent-editing-freedom-v1` is persisted per project, injected into planning and validation, and cannot be changed underneath an active draft. Focused controller proof verifies that `Protect` rejects crop and blocks graphics, `Balanced` honors the project look, and `Transform` permits bounded crop, color grading, photo layers, and graphic text while approval invariants remain fixed. Desktop runtime proof captures both the default and selected Transform states (`/private/tmp/photo-editor-agent-editing-freedom-20260822/ai-edit-compact-entry-1440x900.png`, `ai-edit-editing-freedom-transform-1440x900.png`).
- [x] Advanced users can still control exact automation boundaries.
  - [x] Per-tool permissions remain editable in `Details`, and each localized draft intent now has an enforced `Keep` lock that blocks both mask correction and whole-draft replanning until the user explicitly unlocks it.

## Conversation Model

- [x] Treat model clarification as conversation, not failure.
- [x] If the model returns `needs_user_input`, show the question inline.
- [x] If the model returns `cannot_complete_safely`, show a safe-stop explanation and suggested next action.
- [x] Preserve run context when the user answers.
- [x] Continue the same run after the answer.
- [x] Keep chat short and task-specific.
- [x] Show an editable intent ledger from the real draft plan:
  - [x] Separate what the agent observed from what it will change.
  - [x] Show exact bounded adjustment values and what remains protected.
  - [x] Let `Show` focus the linked region and `Change` open a correction scoped to that mask.
  - [x] Let `Keep` protect the exact current-draft mask; visibly disable both targeted correction and broad replanning while the lock is active, then restore them only after explicit unlock.
  - [x] Route finding-scoped `lock this change`, `keep this change exactly`, and `unlock this change` language to the same enforced boundary before ordinary adjustment parsing.
- [x] Let the user point directly at the rendered photo and describe a bounded local correction.
  - [x] Map the click through the crop/rotation-aware image coordinate mapper, show the exact seed, attempt an evidence-gated local-content matte, keep the visible feathered radial boundary when center/coverage/contrast evidence is unclear, disclose which boundary won, focus the exact resulting mask, preserve all existing masks and everything outside the resolved target, reject outside/ambiguous targets without mutation, and defer project memory until confirmed commit (`agent-point-grounded-correction-v2`).
- [x] Do not turn the app into a general chatbot.

Acceptance criteria:

- [x] The user can talk to the agent when needed.
- [x] The conversation stays anchored to the selected photos and style.

## Details Drawer

- [x] Add a collapsed `Details` drawer for advanced inspection.
- [x] Include:
  - [x] Model name and status.
  - [x] MCP status.
  - [x] Memory vault status.
  - [x] Open/export/reset memory vault actions.
  - [x] Tool calls.
  - [x] Recipe diff.
  - [x] Validation report.
  - [x] Style citations.
  - [x] Run artifacts.
  - [x] Shareable report export.
- [x] Keep this drawer out of the default workflow.

Acceptance criteria:

- [x] The UI stays simple. Rendered proof covers a collapsed default with technical process hidden, a one-click expanded Details state, an outcome-first sample review, and a workspace review whose normal receipt contains no model, semantic-selection, or quality-check jargon.
- [x] Debugging and trust evidence remain available.

## Copy

- [x] Prefer outcome labels over technical labels.
- [x] Use:
  - [x] `AI Edit`
  - [x] `Set Up Local AI`
  - [x] `Learn This Style`
  - [x] `Create Samples`
  - [x] `Make Changes`
  - [x] `Apply to All`
  - [x] `Review Issues`
  - [x] `Commit Edits`
- [x] Avoid default-surface labels like:
  - [x] MCP
  - [x] Pull
  - [x] Probe
  - [x] Tool schema
  - [x] Plan run
  - [x] Style JSON

Acceptance criteria:

- [x] Button text maps to what the user wants, not what the system is doing internally.

## Build Priority

1. [x] Collapse current controls into one `AI Edit` panel with state-based primary actions.
   - [x] Make the live resident entry default to resolved target, plain-language direction, and one outcome-specific `Create Edit` or `Create Samples` action.
   - [x] Keep scope override, optional inspiration, selected look, local model, local tools, and project taste behind one `Details` disclosure.
   - [x] Prove both the collapsed default and expanded Details state at 1440x900 with the real walkthrough component.
   - [x] Remove the retired, unreferenced legacy run-dialog component and migrate its remaining source-contract checks to the live surface.
   - [x] Preserve local-tool health, memory-vault actions, learned-style changes, proof artifacts, tool-call summaries, validation, and warnings inside the live Details disclosure.
   - [x] Prove `Set Up Local AI`, `Choose Style`, and the outcome-specific create action route through the state-driven primary action instead of becoming dead buttons.
2. [x] Add automatic local AI readiness checks and one setup action.
3. [x] Add the local Markdown memory vault and style/project/run note schema.
4. [x] Add staged memory retrieval for sample, revision, batch, and outlier phases.
5. [x] Add phase-based MCP tool exposure instead of exposing every tool at once.
6. [x] Replace raw learned-style controls with `Learn This Style` and a taste card.
   - [x] Workflow state offers `Learn This Style` when selected assets include edited virtual-copy references.
   - [x] Workflow state exposes a compact taste card with style name, reference count, confidence, and summary.
   - [x] Edit and Studio AI surfaces keep raw style chooser/builder controls behind `Details`.
7. [x] Make sample-first review the default path for batches.
   - [x] Controller coverage proves three selected photos draft two representative samples first, then confirmed `Apply to All` drafts the remaining photo without double-applying the reviewed samples.
8. [x] Route feedback into immediate sample revision and durable style memory only when approved.
   - [x] Sample feedback immediately revises the samples and reports `feedbackMemoryDeferred` until the user approves the direction.
   - [x] Confirmed `Apply to All` saves deferred sample feedback into durable style memory and exposes the approved feedback memory note/event in the run report.
9. [x] Render model questions inline.
10. [x] Show only outliers after batch runs.
    - [x] The visible issue list is driven by `batchSummary.reviewIssues`, and each row now supports `Fix`, `Accept anyway`, and `Rollback this photo`.
11. [x] Move model/MCP/tool/validation internals into a details drawer.
12. [x] Add smart permission modes instead of default tool toggles.
13. [x] Polish copy around user outcomes.
14. [x] Add a reversible `Show me two directions` decision inside review.
    - [x] Controller coverage proves both rendered proof files exist, Direction B differs from A, commit is blocked before choice, and choosing A restores the exact serialized recipe.
    - [x] Project-taste coverage proves no approval is written while comparing and the explicit choice is recorded only after confirmed commit.
    - [x] The 1440x900 AI Edit visual shard captures the compact two-proof choice surface and verifies the visible post-choice memory receipt.

## Definition Of Done

- [x] The default UI has fewer visible controls than today.
- [x] The agent has more context than today.
- [x] The user can complete a batch edit without understanding model setup, MCP, tool schemas, or validation internals.
- [x] The user still sees enough proof to trust the result.
- [x] Durable agent memory is inspectable, local, editable, and redacted for sharing. The one-click Details proof exposes the local note count, vault, Obsidian, and safe-copy actions; the taste mirror supports project-local rename/forget controls without storing image bytes or source paths.
- [x] The model receives retrieved memory packets, not the entire memory vault.
- [x] MCP tools are exposed by run phase with strict schemas, approvals, timeouts, and audit logs.
- [x] Advanced details are one click away, not always in the way.
- [x] The product feels like an editing assistant, not an automation console. The visible loop is now choose a look, describe the outcome, review real samples, make changes, apply to all, inspect exceptions, and commit; diagnostics remain available without defining the normal workflow.
