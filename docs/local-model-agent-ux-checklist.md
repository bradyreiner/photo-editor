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

- [ ] One primary agent surface.
- [x] One primary action at a time.
- [ ] No visible setup maze.
- [ ] No required JSON/schema/tool knowledge.
- [ ] No separate "plan", "validate", "tool", and "feedback" mental models for normal users.
- [x] Advanced diagnostics are available, but collapsed by default.
- [ ] The agent asks questions only when it cannot safely infer the answer.
- [ ] Every automated action produces reversible drafts until the user commits.
- [ ] The app shows proof, not process.

## Main Interface

- [ ] Replace scattered model/style/run controls with one compact `AI Edit` panel.
- [x] Expose one controller workflow state that provides the compact status line, inferred scope, readiness, and primary action for the UI.
- [x] Wire the compact workflow state into the Edit and Studio AI panels as the default status/action surface.
- [ ] Show a single status line:
  - [x] `Ready`
  - [x] `Setting up local model`
  - [x] `Needs style`
  - [x] `Creating samples`
  - [x] `Waiting for your review`
  - [x] `Applying to batch`
  - [x] `Review needed`
  - [x] `Ready to commit`
- [ ] Show one main button based on state:
  - [ ] `Set Up`
  - [ ] `Choose Style`
  - [x] `Learn This Style`
  - [x] `Create Samples`
  - [x] `Apply to All`
  - [x] `Review Issues`
  - [x] `Commit`
- [ ] Keep secondary actions small:
  - [x] Change style.
  - [x] Change scope.
  - [x] Add feedback.
  - [x] Roll back.
  - [x] Show details.
- [x] Collapse model, MCP, scope, run mode, and setup diagnostics under `Details`.
- [x] Collapse tool, prompt, validation, and report details under `Details`.

## Smart Setup

- [ ] On app launch or first agent open, silently check:
  - [x] Ollama installed.
  - [x] Ollama reachable.
  - [x] Selected local model installed.
  - [x] Model can respond.
  - [x] Model can inspect a proof image.
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
- [ ] Only show low-level errors when automatic recovery fails.
- [ ] Recommend one default model instead of making the user choose from a list first.
- [ ] Move model switching to advanced settings.

Acceptance criteria:

- [ ] A non-technical user can get to `Ready` without knowing what Ollama or MCP means.
- [ ] A technical user can still inspect the backend details.

## Scope Handling

- [ ] Infer scope from selection:
  - [x] One selected photo -> current photo.
  - [x] Multiple selected photos -> selected set.
  - [x] No selection but project active -> project.
  - [x] Filter active -> filtered set.
- [ ] Show scope as a compact phrase: `24 selected photos`, `Current project`, `1 photo`.
- [ ] Let user change scope with one small dropdown.
- [ ] Warn only for meaningful risks:
  - [ ] No editable photos.
  - [ ] Huge batch without sample approval.
  - [ ] Mixed edited/unedited references when learning style.
  - [ ] Missing source files.
- [ ] Do not expose batch clustering until there are outliers or review results.

Acceptance criteria:

- [ ] The app chooses the likely scope correctly most of the time.
- [ ] The user can override scope without entering a configuration flow.

## Style Handling

- [ ] Always require a style, but make style creation simple.
- [ ] Default to the last used style for the current project.
- [x] If edited references are selected, offer `Learn This Style`.
- [ ] If no style exists, offer three starting choices:
  - [ ] Learn from selected edits.
  - [ ] Describe a style.
  - [ ] Use a built-in style.
- [ ] Hide full style schema from normal users.
- [ ] Show learned style as a taste card:
  - [x] Name.
  - [x] Reference count.
  - [x] Confidence: low / medium / high.
  - [x] Short style summary.
  - [ ] Example thumbnails.
- [ ] Let user remove bad references directly from the taste card.
- [ ] Let user rename the style inline.
- [ ] Save advanced style fields in the background.

Acceptance criteria:

- [ ] Learning a style feels like selecting examples, not configuring an agent.
- [ ] The user can see enough to trust the learned style without reading technical metadata.

## Agent Intelligence

- [ ] The agent should automatically gather context before editing:
  - [ ] Current recipe.
  - [ ] Current proof image.
  - [ ] Style profile.
  - [ ] Relevant tool permissions.
  - [ ] Validation thresholds.
  - [ ] Batch similarities.
  - [ ] Prior user feedback.
- [ ] The agent should automatically choose the right edit path:
  - [ ] Color-only correction when masks are unnecessary.
  - [ ] Local masks when global edits cannot solve the issue.
  - [ ] Crop/straighten only when composition benefits.
  - [ ] Retouch only when allowed and clearly needed.
  - [ ] Per-cluster batch edits when lighting differs.
  - [ ] Per-image correction for outliers.
- [ ] The agent should ask before:
  - [ ] Major retouching.
  - [ ] Large crop/composition changes.
  - [ ] Destructive-looking edits.
  - [ ] Exporting or overwriting files.
  - [ ] Ambiguous style intent.
- [ ] The agent should refuse or pause when validation says the edit is unsafe.

Acceptance criteria:

- [ ] The user sees fewer controls because the agent chooses tools intelligently.
- [ ] The app still guards sensitive or irreversible decisions.

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

- [ ] Treat MCP as the agent's controlled hands, not its brain.
- [ ] The app remains the source of truth for recipes, validation, rendering, commits, rollback, and exports.
- [ ] The model proposes intent and structured actions.
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
- [ ] Require approval for:
  - [x] Commit.
  - [x] Export.
  - [x] Retouch.
  - [x] Large crop.
  - [x] File output.
  - [x] Any operation outside the active project scope.
- [x] Log every tool call into the run report.
- [x] Show only user-relevant tool events by default.

Acceptance criteria:

- [ ] The model cannot accidentally discover a dangerous path just because a tool exists.
- [ ] Tool use is auditable.
- [ ] The UI remains simple because the orchestrator manages tool choice.

## Agent Strategy Loop

- [ ] Implement the agent as a deterministic orchestrator around a local model.
- [ ] Use this loop for normal editing:
  - [ ] `Observe`: gather selected scope, active style, proof images, recipe summaries, and relevant memory.
  - [ ] `Plan`: ask the model for a structured edit strategy or clarification question.
  - [ ] `Constrain`: app filters proposed actions through style rules, permissions, and tool availability.
  - [ ] `Act`: app calls MCP/editor tools.
  - [ ] `Validate`: app renders proof and checks image/recipe/style quality.
  - [ ] `Repair`: model gets compact validation feedback for a limited number of retries.
  - [ ] `Review`: user sees samples or outliers.
  - [ ] `Remember`: app saves approved feedback and run lessons into local memory.
- [x] Keep planning low temperature/deterministic for batch consistency.
- [x] Use separate strategy prompts by phase instead of one giant prompt.
- [x] Use compact few-shot examples from prior successful edits only when relevant.
- [x] Add a hard retry budget for model repair loops.
- [x] Add a hard tool budget per run phase.
- [x] Stop and ask the user when confidence is low or risk is high.

Acceptance criteria:

- [ ] The agent feels smart because it follows a reliable loop.
- [ ] The model is never the only safety layer.
- [ ] Batch output is consistent and reproducible enough for professional work.

## Sample-First Review

- [x] Always create samples before applying to a large set.
  - [x] Large selected-set drafts now apply only representative cluster samples first, preserve the full target list, and keep remaining photos unchanged until confirmed `Apply to All`.
- [x] Pick samples automatically from representative lighting/composition groups.
- [x] Show only the sample outputs first.
- [ ] For each sample, show:
  - [x] Before/after.
  - [x] Short explanation.
  - [x] Pass/warn/fail badge.
- [ ] Primary review actions:
  - [x] `Looks good`
  - [x] `Make changes`
  - [x] `Reject`
- [x] `Make changes` opens one natural-language feedback box.
  - [x] The existing AI notes field becomes the feedback entry point during sample review, with a visible `Make Changes` action.
- [x] Feedback should immediately revise the sample, not just save notes.
  - [x] Sample feedback rolls back the current sample draft, reruns sample creation with the feedback in context, leaves remaining photos untouched, and defers durable style-memory writes.
- [x] After approval, offer `Apply to all`.
  - [x] Confirmed sample review continuation drafts only the remaining target photos in the same rollbackable draft transaction.

Acceptance criteria:

- [ ] The user does not need to understand planning or validation to approve direction.
- [ ] Feedback has an immediate visible effect.

## Review And Outliers

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
- [ ] For each issue, show:
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

- [ ] The review workload scales with problems, not batch size.
- [ ] A batch of 500 good edits does not create 500 things to inspect.

## Tool Permissions Without Tool Management

- [x] Do not show a wall of tool toggles by default.
- [x] Use permission modes:
  - [x] `Conservative`
  - [x] `Balanced`
  - [x] `Full edit`
  - [x] `Full edit + retouch`
- [x] Translate modes into allowed tools internally.
  - [x] The style builder maps each permission mode into the existing `allowed_tools` fields before saving the style.
- [x] Show a plain summary:
  - [x] `Can adjust color, tone, crop, and local masks. Retouch requires approval.`
- [x] Let advanced users expand and edit exact tool permissions.
  - [x] Exact per-tool checkboxes are hidden unless the Agent `Details` drawer is open.

Acceptance criteria:

- [ ] Normal users choose intent and risk level.
- [ ] Advanced users can still control exact automation boundaries.

## Conversation Model

- [x] Treat model clarification as conversation, not failure.
- [x] If the model returns `needs_user_input`, show the question inline.
- [x] If the model returns `cannot_complete_safely`, show a safe-stop explanation and suggested next action.
- [x] Preserve run context when the user answers.
- [x] Continue the same run after the answer.
- [x] Keep chat short and task-specific.
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

- [ ] The UI stays simple.
- [ ] Debugging and trust evidence remain available.

## Copy

- [ ] Prefer outcome labels over technical labels.
- [ ] Use:
  - [ ] `AI Edit`
  - [ ] `Set Up Local AI`
  - [x] `Learn This Style`
  - [ ] `Create Samples`
  - [ ] `Make Changes`
  - [ ] `Apply to All`
  - [ ] `Review Issues`
  - [ ] `Commit Edits`
- [ ] Avoid default-surface labels like:
  - [ ] MCP
  - [ ] Pull
  - [ ] Probe
  - [ ] Tool schema
  - [ ] Plan run
  - [ ] Style JSON

Acceptance criteria:

- [ ] Button text maps to what the user wants, not what the system is doing internally.

## Build Priority

1. [ ] Collapse current controls into one `AI Edit` panel with state-based primary actions.
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
13. [ ] Polish copy around user outcomes.

## Definition Of Done

- [ ] The default UI has fewer visible controls than today.
- [ ] The agent has more context than today.
- [ ] The user can complete a batch edit without understanding model setup, MCP, tool schemas, or validation internals.
- [ ] The user still sees enough proof to trust the result.
- [ ] Durable agent memory is inspectable, local, editable, and redacted for sharing.
- [x] The model receives retrieved memory packets, not the entire memory vault.
- [ ] MCP tools are exposed by run phase with strict schemas, approvals, timeouts, and audit logs.
- [ ] Advanced details are one click away, not always in the way.
- [ ] The product feels like an editing assistant, not an automation console.
