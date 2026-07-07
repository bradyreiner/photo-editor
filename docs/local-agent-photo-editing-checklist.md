# Local Agent Photo Editing Checklist

This checklist defines the requirements and goals for an in-app, locally run editing agent that can use the photo editor's color, masking, crop, retouch, render, validation, batch, and export systems. The agent should be launched from the UI, use a local model on the user's Mac, follow user-authored style rulebooks, and produce reversible, validated edits.

## Product Goals

- [x] Let users start an agent editing run from inside the desktop app.
- [x] Run the model locally on the user's Mac by default.
- [x] Let users create reusable styles that describe the desired editing outcome, not just fixed slider presets.
- [x] Let the agent use every supported color edit, masking tool, crop/transform control, layer/LUT control, retouch control, and export preflight path that is safe to automate.
  - [x] Recipe patch schemas expose validation-backed color, crop/transform, mask/local adjustment, layer/LUT, retouch, and output/export-preflight-safe fields.
  - [x] Unsafe paths remain gated by style permissions, explicit retouch permissions, export confirmations, and security review gates.
- [x] Keep originals untouched and make every agent edit reversible.
- [x] Support single-photo, selected-set, album, filtered-view, and bulk workflow runs.
  - [x] Support selected-set draft runs from the current checked asset selection.
  - [x] Expose selected-set bulk plan and draft automation commands.
  - [x] Support current-photo, selected-assets, project/album, and filtered-library scopes from the Agent dialog.
  - [x] Route project/album and filtered-library scopes through the same draft, batch validation, export preflight, artifact, and rollback paths as selected assets.
- [x] Produce visible proof for each edit: before/after preview, recipe diff, validation report, warnings, and export readiness.
  - [x] Produce an in-memory rendered proof for validation.
  - [x] Return a structured validation report and warnings for the active draft.
  - [x] Persist before/after proof images for the active draft.
  - [x] Persist grouped recipe diffs and validation reports for the active draft.
  - [x] Persist per-asset proof images, recipe diffs, and validation reports for selected-set drafts.
  - [x] Persist per-asset proof images and validation reports for selected-set batch validation.
  - [x] Return export readiness reports through the existing export preflight path.
- [x] Use the running app's existing recipe, render, catalog, mask, export, and observability paths instead of a parallel editing engine.
  - [x] Use the existing catalog recipe read/write path for draft edits.
  - [x] Use the existing render path for validation proofs.
  - [x] Use the existing mask stack recipe and render validation paths for mask/local agent drafts.
  - [x] Use the existing export preflight path for agent export readiness checks.
  - [x] Use the existing observability log for draft, commit, and rollback events.
- [x] Make agent behavior understandable: show what it changed, why it changed it, what it validated, and what it refused to do.
  - [x] Show plan summary, warnings, and refusal/error messages for the current run.
  - [x] Persist grouped recipe diffs, validation reports, preview proof policies, mask/local proof metadata, security gates, performance gates, and rollback/cancellation transaction metadata.
- [x] Support power-user automation without removing user control over final commit.
  - [x] Require explicit confirmation before automation queues selected-asset exports.
  - [x] Require explicit confirmation before automation commits an agent draft.

## Non-Goals For The First Release

- [x] Do not overwrite source image files.
  - [x] Controller workflow coverage verifies source image bytes are unchanged after analyze, draft, rollback, batch, export queueing, and commit flows.
- [x] Do not allow arbitrary code execution from styles or model output.
  - [x] Reject top-level and nested executable/script/hook/command style payload fields during style validation.
- [x] Do not let the model write unvalidated recipe JSON directly.
  - [x] Recipe patch validation explicitly rejects direct or nested full-recipe JSON payload keys before patch application.
- [x] Do not require cloud inference for core editing.
  - [x] Keep the built-in rulebook model local and available by default.
- [x] Do not auto-commit large bulk edits without explicit user approval.
  - [x] Require explicit controller-level confirmation before committing active single-photo or selected-set agent drafts.
- [x] Do not promise perfect taste. The system should optimize toward style goals and validation gates, then make review efficient.
  - [x] Draft reports and manifests mark edits as review-required candidate edits, set `qualityGuarantee: false`, and preserve validation-gate context for user approval.

## User Workflows

- [x] User selects one photo and asks the agent to edit it according to a chosen style.
- [x] User selects multiple photos and asks the agent to create a consistent batch edit.
  - [x] Selected-set drafts cluster the scope, plan from each representative, and apply each validated cluster patch to its assigned assets.
- [x] User creates a new style with natural-language goals, constraints, allowed tools, and validation thresholds.
- [x] User previews an agent edit as a draft before committing.
- [x] User compares before/after and sees recipe diffs grouped by tone, color, detail, masks, crop, layers, and retouch.
  - [x] Produce file-backed before/after proofs and grouped recipe diffs for single-photo drafts.
  - [x] Produce file-backed before/after proofs and grouped recipe diffs for every selected-set draft asset.
  - [x] Agent panel shows grouped recipe diff summaries next to before/after proof review.
- [x] User accepts, rejects, or asks the agent to revise the draft.
  - [x] Accept draft through commit.
  - [x] Reject draft through rollback.
  - [x] Ask for a revision from the Agent panel while preserving rollback to the original recipe.
- [x] User bulk-applies a style to a shoot while preserving per-lighting-group consistency.
  - [x] Multi-asset agent draft runs cluster assets by available capture metadata and recipe exposure/white-balance signals.
  - [x] Each cluster gets a representative plan and its own recipe patch before assets are synced inside the cluster.
- [x] User exports edited assets after the agent passes export preflight.
  - [x] User can run export preflight on the selected agent-edited assets before exporting.
  - [x] User can queue selected-asset exports after confirmed preflight.
- [x] User can stop a running agent job and roll back partial drafts.
  - [x] Agent Cancel requests rollback-on-cancel semantics for active runs.
  - [x] Canceled draft reports and manifests record applied count, restored count, rollback status, and batch transaction state.
  - [x] Cancellation during local model planning is preserved instead of being cleared before draft application.

## UI Requirements

- [x] Add an `AgentEditController` owned by the desktop shell/controller layer.
- [x] Add an Agent panel or dialog reachable from the edit and library workspaces.
  - [x] Edit workspace inspector tab.
  - [x] Add the active `StudioWorkspace` Agent dialog to the edit canvas toolbar and library toolbar/inspector.
- [x] Show available local models and their status.
  - [x] Available local model list.
  - [x] Model status metadata exposes available, loading, busy, failed, and unavailable states.
  - [x] Display the selected local model status in the Agent panel.
  - [x] Display every configured local model status and status reason in the Agent panel.
- [x] Show selected scope: current photo, selected assets, album, filter result, or folder/import batch.
  - [x] Show current-photo and selected-assets scope in the Agent panel.
  - [x] Show album/project, filtered-library, and import-source scope rows in the active `StudioWorkspace` Agent dialog.
  - [x] Let users choose current-photo, selected-assets, project/album, or filtered-library scope before starting an Agent dialog run.
- [x] Let users choose or create a style.
  - [x] Choose a style for the current run.
- [x] Create a structured style from the Agent panel.
- [x] Let users choose run mode:
  - [x] Analyze only.
  - [x] Draft edit.
  - [x] Draft batch.
  - [x] Commit after review.
  - [x] Export after review.
  - [x] Add an Agent panel run-mode selector for analyze, draft, batch validation, and export preflight.
- [x] Let users choose tool permissions:
  - [x] Normal users choose Conservative, Balanced, Full edit, or Full edit + retouch instead of starting from raw tool toggles.
  - [x] Advanced users can still expand Details and edit exact tool permissions.
  - [x] Global tone/color.
  - [x] Curves/levels.
  - [x] HSL/color mixer/grading.
  - [x] Detail/noise/sharpening.
  - [x] Crop/transform/lens.
  - [x] Masks and local adjustments.
  - [x] Retouch/inpaint.
  - [x] Layers/LUTs.
  - [x] Export.
- [x] Show run progress with concrete stages: analyze, plan, draft recipe, render proof, validate, revise, batch sync, outlier correction, export preflight, ready for review.
  - [x] Basic status for analyzing, planning, applying draft, export preflight, ready, failed, committed, and rolled back.
  - [x] Agent panel shows a concrete stage grid for analyze, plan, draft recipe, render proof, validate, revise, batch sync, outlier correction, export preflight, and ready for review.
  - [x] Stage grid derives active, pending, complete, warning, and failed states from the current run status and latest tool-call report.
- [x] Show a before/after proof surface for the active asset.
  - [x] Agent panel displays current proof artifacts for analyze/proof runs.
  - [x] Agent panel displays before/after proof artifacts for draft runs.
- [x] Show batch status: pending, drafted, validated, warning, failed, committed.
  - [x] Return per-asset draft status in selected-set draft reports.
  - [x] Agent panel summarizes per-asset pending, drafted, validated, warning, failed, and committed states from the latest run report.
  - [x] Agent panel lists per-asset batch status with asset id plus warning, error, and revision counts.
- [x] Show validation warnings in plain language.
- [x] Show a cancel button for all long-running agent jobs.
  - [x] Add an Agent panel cancel action for active agent draft runs.
  - [x] Agent panel cancel action is enabled for any active agent run.
  - [x] Batch validation and export preflight honor cooperative cancellation and timeout checks at per-asset boundaries.
  - [x] Cancellation reports persist cancellation reason, failure reason, timing, and manifest metadata.
- [x] Show a rollback button for any uncommitted agent draft.
- [x] Keep QML declarative; controller code owns orchestration and state.

## Style System Requirements

- [x] Store styles as user-editable structured documents.
- [x] Support app-shipped styles and user-created styles.
- [x] Support style import/export.
- [x] Version the style schema.
- [x] Validate styles before use.
- [x] Support natural-language intent fields.
- [x] Support typed constraints and thresholds.
- [x] Support allowed/disallowed tools.
  - [x] Expose user-facing allowed-tool toggles in the Agent panel style builder.
- [x] Support per-genre goals such as portrait, wedding, product, landscape, real estate, street, event, filmic, clean commercial, black-and-white, and high-key.
  - [x] Ship first built-in wedding/editorial and clean commercial style examples.
  - [x] Ship built-in first-release style goals for portrait, wedding, product, landscape, real estate, street, event, filmic, clean commercial, black-and-white, and high-key.
- [x] Support batch behavior rules.
- [x] Support validation strictness per style.
- [x] Support examples or references later, but keep first release text/rule based.
  - [x] Built-in styles expose `referencePolicy.mode: text_rulebook_only` with reference-image matching deferred.
  - [x] Schema v1 style validation rejects image/example reference payloads instead of silently accepting unsupported reference matching.

Example style shape:

```yaml
schema_version: 1
name: Warm Editorial Wedding
goal: Natural warm editorial wedding edit with protected skin tones and soft highlight rolloff.
allowed_tools:
  global_color: true
  curves: true
  hsl: true
  color_grading: true
  detail: true
  crop: conservative
  masks: true
  retouch: minor_cleanup_only
  export: false
constraints:
  preserve_skin_texture: true
  avoid_clipped_highlights: true
  avoid_crushed_blacks: true
  max_exposure_shift: 1.2
  max_temperature_shift: 1800
  max_saturation_shift: 18
validation:
  highlight_clip_limit_percent: 0.5
  shadow_clip_limit_percent: 1.0
  require_skin_tone_check: true
  require_before_after_proof: true
batch_behavior:
  cluster_by_lighting: true
  maintain_sequence_consistency: true
  correct_outliers_individually: true
```

## Local Model Requirements

- [x] Support at least one local model backend for the first implementation.
- [x] Keep the model runner outside the UI thread.
  - [x] Draft planning dispatches local model planning through a worker runner and records runner metadata in reports/manifests.
- [x] Let the app discover configured local models.
  - [x] Built-in deterministic local model registry.
  - [x] Discover local model metadata from an app-data manifest.
- [x] Show whether a model is available, loading, busy, or failed.
  - [x] Model availability is exposed in the backend model registry.
  - [x] Configured local model status is exposed in the backend model registry.
  - [x] Agent panel shows each model status and reason without requiring logs or config-file inspection.
- [x] Use structured tool calls rather than free-form recipe writes.
- [x] Require the model to cite the active style constraints when proposing edits.
- [x] Require the model to produce a plan before applying a draft.
- [x] Require the model to use render/validation feedback before finalizing.
- [x] Keep prompts and rulebooks local.
- [x] Log model/tool decisions to the run artifact without storing private images in text logs.
  - [x] Persist planned/applied recipe patch and validation outputs in single-photo draft report artifacts.
  - [x] Persist model plan, style citations, structured tool calls, and tool outcomes in run manifests.
- [x] Support cancellation and timeout limits.
  - [x] Add controller-level draft run timeout limits with timeout cancellation metadata.
  - [x] Add UI and bridge cancellation request entry points for active agent runs.
  - [x] Add rollback-on-cancel reporting for user-stopped draft jobs.
- [x] Support deterministic or low-temperature mode for repeatable batch runs.

## MCP And Automation Architecture

- [x] Extend the existing `photo-editor-mcp` server with a `photo` toolset.
  - [x] Add first bridge-backed `photo` MCP commands for live status, models, selection, selected recipe, recipe patch validation/draft apply, run draft, commit, and rollback.
  - [x] Add bridge-backed mask draft commands for linear/radial mask creation and local mask adjustment.
- [x] Add a local-only desktop automation bridge in the running Qt app.
- [x] Prefer `QLocalServer` or another local authenticated IPC mechanism over a public network port.
  - [x] Try `QLocalServer` first and fall back to authenticated localhost-only TCP when local sockets are unavailable.
- [x] Generate a per-session automation token or nonce.
- [x] Expose only typed domain commands.
- [x] Reject unknown fields and out-of-range values.
- [x] Return structured results for all tools.
- [x] Link tool results to observability runs, preview proofs, recipe diffs, and export artifacts.
  - [x] Return proof, recipe diff, and validation artifact paths from draft tool results.
  - [x] Return read-only proof artifact and validation report paths from `photo_render_proof`.
  - [x] Return manifest, preflight report, and export batch report paths from agent run results.
  - [x] Return manifest, batch validation report, and per-asset validation artifact paths from selected-set validation results.
  - [x] Expose agent run artifact files through MCP resource URIs.
  - [x] Return MCP resource links for agent run JSON artifacts from bridge-backed photo tools.
- [x] Keep MCP useful when the app is not running by reporting live status clearly.
- [x] Keep destructive or bulk operations behind explicit user confirmation.
  - [x] Require `confirmed: true` for automation export queueing.
  - [x] Require `confirmed: true` for automation draft commits.
  - [x] Require `confirmed: true` for selected-set automation draft commits.
  - [x] Add an explicit confirmation affordance before queueing exports from the Agent panel.
  - [x] Require `confirmed: true` for automation retouch patch drafts.
  - [x] Expose no deletion-like photo automation command in the first release toolset.

Suggested MCP tools:

- [x] `photo_live_status`
- [x] `photo_list_models`
- [x] `photo_selected_assets`
- [x] `photo_read_asset`
- [x] `photo_read_recipe`
- [x] `photo_validate_recipe_patch`
- [x] `photo_apply_recipe_patch_draft`
- [x] `photo_render_proof`
- [x] `photo_validate_edit`
- [x] `photo_create_mask`
- [x] `photo_apply_mask_adjustment`
- [x] `photo_plan_bulk_edit`
- [x] `photo_apply_bulk_draft`
- [x] `photo_validate_batch`
- [x] `photo_commit_agent_draft`
- [x] `photo_rollback_agent_draft`
- [x] `photo_export_preflight`
- [x] `photo_export_batch`

## Editing Tool Coverage

- [x] Expose all global tone controls.
- [x] Expose all RAW input controls.
  - [x] Expose RAW exposure, boost, shadow boost, detail, chroma noise reduction, moire reduction, and white balance.
  - [x] Expose RAW cleanup, false-color suppression, recovery/mapping/lens toggles, demosaic quality, camera look, black/white levels, and calibration primary controls.
- [x] Expose white balance controls.
- [x] Expose contrast, brightness, HDR/local tone, filmic shoulder/toe, adaptive contrast, levels, and zones.
- [x] Expose curves: master, luma, red, green, blue.
  - [x] Expose tonal curve band controls for shadows, darks, lights, and highlights.
  - [x] Expose typed master, luma, red, green, and blue point-curve controls.
- [x] Expose color controls: temperature, tint, vibrance, saturation.
- [x] Expose HSL/channel mixer/selective color controls.
  - [x] Expose first HSL hue, saturation, and luminance adjustment controls.
  - [x] Expose typed mixer model, active channel, per-channel HSL/selective CMYK controls, and RGB channel mixer controls.
- [x] Expose black-and-white controls.
  - [x] Expose typed black-and-white enable and per-channel mixer controls.
- [x] Expose color replacement and skin-tone protection controls.
  - [x] Expose typed color replacement enable, source/target hue, range, softness, strength, and preservation controls.
  - [x] Expose typed skin-tone protection enable, line hue, range, and strength controls.
- [x] Expose color harmony and color grading controls.
  - [x] Expose typed color harmony enable, mode, base hue, and strength controls.
  - [x] Expose typed color grading shadows, midtones, highlights, amount, and balance controls.
- [x] Expose texture, clarity, dehaze, sharpening, and noise reduction controls.
- [x] Expose lens, perspective, transform, crop, and upright controls.
  - [x] Expose lens distortion, vignette, chromatic aberration, perspective, transform, and crop zoom controls.
  - [x] Expose typed crop preset, crop rectangle, upright mode/guide, and content-aware crop fill controls.
- [x] Expose output color space and soft proof controls where safe.
  - [x] Expose output headroom through the typed recipe patch.
  - [x] Expose typed output color space, soft-proof enable/profile, and render intent controls.
- [x] Expose linear and radial masks.
- [x] Expose brush/selection masks.
  - [x] Expose typed brush points, lasso polygons, mask set metadata, combine operations, and per-mask opacity/density.
- [x] Expose subject/sky/background/depth/AI-generated masks when available.
  - [x] Expose typed generated mask kinds and depth/range mask definitions without allowing agent-supplied binary matte payloads.
- [x] Expose mask refinement controls.
  - [x] Expose typed selection cleanup, range refinement, and generated-mask hole/island cleanup thresholds.
- [x] Expose local mask adjustments for tone, color, detail, and density/opacity where supported.
  - [x] Expose linear/radial local exposure, temperature, feather, position, radius, center, and rotation controls.
  - [x] Expose mask-stack local tone, color, density, opacity, blend, and colorize controls.
  - [x] Expose mask-stack local texture, clarity, dehaze, sharpening radius/detail/masking, and noise reduction controls.
- [x] Expose layers and LUT stacks where supported.
  - [x] Expose typed adjustment-layer stacks with bounded layer metadata and layer-local LUT stack payloads.
- [x] Expose retouch/inpaint only under explicit style permissions.
  - [x] Expose typed clone, heal, patch, and remove retouch stack entries only through the explicit `retouch` tool permission.
  - [x] Reject agent-supplied inpaint patch bytes, source masks, candidate payloads, and unsupported inpaint worker metadata from recipe patches.
- [x] Expose export presets and export preflight.
  - [x] Expose export preflight with caller-selected preset and destination.
  - [x] Expose confirmed export queueing with caller-selected preset and destination.

## Agent Decision Loop

- [x] Load style and validate it.
- [x] Inspect asset metadata and current recipe.
  - [x] Analyze-only report includes selected asset metadata and current recipe summary.
- [x] Analyze rendered source preview.
  - [x] Render the draft image for deterministic validation.
  - [x] Render the current image for analyze-only validation.
- [x] Identify edit goals and risks.
  - [x] Report validation risks for the rendered current image in analyze-only mode.
  - [x] Persist explicit `editGoals` and `riskAssessment` fields in analyze, plan, draft, and manifest outputs.
- [x] Build an edit plan using only allowed tools.
- [x] Apply a draft recipe patch.
- [x] Render a proof preview.
- [x] Validate the edit.
- [x] Revise if validation fails.
- [x] Stop after a configured iteration limit.
- [x] Produce final draft report.
- [x] Wait for commit approval when required.
  - [x] Unconfirmed controller commit attempts leave the draft active and report `confirmationRequired`.

## Image Validation Requirements

- [x] Validate highlight clipping.
- [x] Validate shadow clipping.
- [x] Validate histogram shape and major channel imbalance.
  - [x] Validate major average channel imbalance.
- [x] Validate white balance plausibility.
- [x] Validate saturation extremes.
- [x] Validate skin-tone plausibility when faces/skin are detected.
  - [x] When a style requires skin-tone validation, detect skin-tone candidate pixels and warn on implausible hue, saturation, luma, or red/blue balance.
- [x] Validate crop bounds and horizon/rotation where possible.
  - [x] Warn when recipe crop rectangles are out of normalized bounds, too small, over-rotated, or missing guided upright angles.
- [x] Validate mask coverage and mask leakage.
  - [x] Estimate coverage for known mask geometry and warn on tiny masks or broad leakage-prone masks.
- [x] Validate retouch/inpaint provenance before export.
  - [x] Warn from both agent recipe validation and export preflight when clone, heal, patch, or remove retouch entries lack required source/provenance metadata.
- [x] Validate preview/export parity for high-risk edits.
  - [x] Export preflight flags crop/transform/optics, masks/local adjustments, retouch/inpaint, layers/LUTs, and output-profile proofing as high-risk groups requiring preview/export parity proof.
  - [x] Export jobs publish preview/export diff metadata and verify final encoded pixels against the export preview within preset-specific tolerance.
  - [x] Agent export preflight surfaces the per-asset parity requirement through the existing export preflight path.
- [x] Validate export preflight.
- [x] Validate performance budget for interactive runs.
  - [x] Finalized agent reports include `performanceBudget` with budget, duration, status, and asset count.
  - [x] Persist performance budget metadata in run manifests and user-facing run history.
  - [x] Add a warning when an interactive agent run exceeds the configured budget.
- [x] Return warnings instead of silently accepting questionable edits.

## Batch Editing Requirements

- [x] Support selected assets.
  - [x] Expose selected-set plan and draft through bridge/MCP bulk commands.
- [x] Support current album/project scope.
  - [x] Validate and export-preflight every asset in the current project/album scope.
  - [x] Use project/album assets without requiring the user to rebuild the checked selection.
- [x] Support filtered library scope.
  - [x] Use the current filtered gallery result as the agent draft, validation, and preflight target set.
  - [x] Roll back filtered-library draft changes as one scoped transaction.
- [x] Cluster assets by capture time, camera/lens, exposure, histogram, white balance, and visual similarity where available.
  - [x] Use capture date, camera, lens, ISO, shutter, aperture, recipe exposure, and recipe white-balance buckets for deterministic first-release clusters.
  - [x] Report histogram and visual-similarity signals as unavailable when no analyzed signal exists instead of inventing confidence.
- [x] Choose representative assets per cluster.
  - [x] Select and report the first available asset in each metadata/recipe cluster as the representative.
- [x] Draft a recipe per cluster.
  - [x] Agent-planned multi-asset runs call the local planner once per cluster representative and persist `clusterPlanning`.
- [x] Sync appropriate recipe groups across each cluster.
  - [x] Apply each cluster recipe patch only to assets assigned to that cluster and report cluster sync groups.
- [x] Correct outliers individually.
  - [x] Per-asset proof validation can revise a cluster patch independently before draft apply and reports outlier correction metadata.
- [x] Preserve virtual-copy independence.
  - [x] Agent draft reports include virtual-copy/source-asset metadata and a `target-asset-id-only` policy.
  - [x] Controller coverage verifies drafting a virtual copy changes only the variant recipe and leaves the source asset recipe unchanged.
- [x] Avoid applying crop/retouch/mask edits globally unless explicitly allowed.
  - [x] Selected-set drafts reject global crop/optics, mask, and retouch patch groups unless the style `batchBehavior` explicitly opts in.
- [x] Show batch summary before commit.
  - [x] Return selected-set draft summaries with asset ids, per-asset status, warnings, proof paths, and recipe diffs.
- [x] Create representative samples before applying large batches.
  - [x] For selected sets with three or more photos and fewer representative clusters than targets, draft only the representative sample assets first.
  - [x] Preserve `fullTargetAssetIds`, `remainingAssetIds`, and planned per-asset patches in the draft report for review and continuation.
  - [x] Confirmed sample-review Apply to All drafts only the remaining photos inside the same rollbackable draft transaction.
  - [x] Sample-review feedback can revise the representative samples immediately by rolling back the sample draft, rerunning sample creation with the feedback, and leaving remaining photos untouched.
  - [x] Deferred sample feedback is written to durable style memory only after confirmed Apply to All approval.
  - [x] Batch summaries include simple review counts and an issue-first review list so successful assets can stay hidden by default.
  - [x] Review issues support accepting an issue without pixel changes and rolling back one photo while keeping the remaining draft active.
  - [x] Review issues support fixing one photo by reusing the bounded validation repair path and merging the repaired asset report back into the current batch review.
  - [x] Review issues support fixing all safe outliers by looping through current non-failed issue rows and preserving the active draft transaction.
  - [x] Review issue rows carry and display rendered proof thumbnails, validation/style reasons, and suggested actions.
  - [x] Review issue rows keep detailed validation evidence collapsed under a `Details` toggle.
  - [x] Return selected-set validation summaries with asset ids, per-asset status, warnings, proof paths, and report paths.
  - [x] Persist `batchSummary` with status counts, proof/diff counts, commit readiness, and confirmation requirement in draft reports and manifests.
- [x] Commit as a reversible batch transaction.
  - [x] Keep selected-set drafts in one reversible uncommitted draft run.
  - [x] Report selected-set draft commits with committed asset ids, scope, and count.
  - [x] Persist `batchTransaction` metadata across draft, commit-confirmation, and committed reports/manifests.
- [x] Allow rollback of the entire batch.
  - [x] Roll back all recipes changed by a selected-set draft.
  - [x] Rollback reports include the restored selected-set transaction id, asset ids, scope, and restored count.

## Recipe And Data Safety

- [x] Store agent edits as recipe diffs before full recipe snapshots.
  - [x] Record grouped recipe diffs for drafted patch fields.
  - [x] Persist diff-first agent provenance on draft reports, manifests, and agent-created snapshot metadata.
- [x] Keep a recipe snapshot before each agent run.
- [x] Tag agent-created recipe snapshots with run id, model id, style id, and timestamp.
- [x] Never mutate original image files.
- [x] Never delete user edits without explicit approval.
  - [x] Reject empty layer, mask, and retouch replacement stacks because they would clear existing user edits without a confirmation path.
- [x] Preserve manual edits unless the user chooses to replace them.
  - [x] Current-base draft mode preserves current recipe fields outside the validated patch and reports `manualEditPolicy`.
- [x] Support "agent edits on top of current edit" and "agent starts from neutral" modes.
  - [x] Add `recipeBaseMode` with `current` and `neutral` options in the controller and Agent panel.
  - [x] Neutral-base drafts snapshot the current recipe for rollback, then apply the validated patch on a neutral base.
- [x] Validate recipe schema before applying.
- [x] Reject values outside known safe ranges.
- [x] Keep binary mask data bounded and validated.
  - [x] Bound segmentation/confidence/depth/source-mask payload dimensions and total bytes before accepting recipe data.
  - [x] Validate persisted recipe mask payload dimensions before apply, and reject mismatched or over-limit payloads.
  - [x] Reject direct agent-supplied binary mask/retouch payload injection in recipe patches.

## Observability And Artifacts

- [x] Create an agent run artifact directory.
- [x] Record style id and style schema version.
  - [x] Include style id in the current in-memory run report and observability events.
- [x] Persist style id and schema version in the run manifest artifact.
- [x] Record model id and backend.
  - [x] Include model id in the current in-memory run report.
  - [x] Include model backend in the current in-memory run report.
  - [x] Persist model id and backend in the run manifest artifact.
- [x] Record asset ids and batch scope.
  - [x] Include active asset id in the current in-memory run report.
  - [x] Include selected-set asset ids and `selected-assets` scope in plan and draft reports.
  - [x] Include project/album and filtered-library scope names plus asset ids in scoped validation, draft, and export preflight reports.
- [x] Record tool calls and structured outcomes.
  - [x] Include planned/applied structured recipe patch in the current in-memory run report.
  - [x] Include style constraint citations in the current plan and draft reports.
  - [x] Persist structured tool calls and outcomes in run manifests and run report artifacts.
- [x] Record recipe diffs.
  - [x] Persist recipe diff JSON for the active draft.
- [x] Record validation reports.
  - [x] Include validation report in the current in-memory run report.
  - [x] Persist validation report JSON for the active draft.
  - [x] Persist analysis report JSON for analyze-only runs.
  - [x] Persist selected-set batch validation report JSON.
- [x] Record before/after proof paths.
  - [x] Record current proof path for analyze-only runs.
- [x] Record export preflight reports.
  - [x] Include selected asset export preflight reports in the current agent run report.
  - [x] Persist selected asset export preflight reports as run artifacts.
- [x] Record failures and cancellation reason.
  - [x] Record failure reasons, errors, and timing in failed run reports and manifests.
  - [x] Persist timeout cancellation reasons in failed draft run reports and manifests.
- [x] Add MCP resources for agent run artifacts.
- [x] Add observability events for agent run start, tool call, render proof, validation failure, draft applied, commit, rollback, and export.
  - [x] Add run start, per-tool-call, and validation failure events.
  - [x] Draft apply, commit, and rollback events.
  - [x] Proof artifact write event.
  - [x] Analyze-only event.
  - [x] Read-only proof render event.
  - [x] Export preflight event.

## Security And Privacy

- [x] Keep local inference as the default.
- [x] Make any cloud model use explicit and opt-in.
  - [x] Require configured non-local models to opt in through both manifest metadata and `PHOTO_EDITOR_ENABLE_CLOUD_MODELS`.
  - [x] Keep explicitly opted-in cloud models unavailable until a dedicated cloud runner exists.
- [x] Do not send images, thumbnails, recipes, metadata, or paths to network services without explicit user approval.
- [x] Keep IPC local and authenticated.
- [x] Do not expose arbitrary shell commands through the photo automation bridge.
- [x] Do not allow styles to execute code.
- [x] Add read-only/analyze-only mode.
- [x] Add confirmation requirements for bulk commit, export, retouch, and deletion-like actions.
  - [x] Add explicit confirmation requirement for automation export queueing.
  - [x] Add explicit confirmation requirement for automation draft commits.
  - [x] Add explicit confirmation requirement for automation retouch patch drafts.
  - [x] Keep deletion-like actions out of the photo automation bridge until a confirmed, reversible workflow exists.
- [x] Redact sensitive paths in shareable reports where possible.
  - [x] Persist a separate `shareable-manifest.json` with app-data, home, temp, source, and absolute artifact paths redacted while preserving the internal manifest for app use.

## Performance Requirements

- [x] Keep model inference off the UI thread.
  - [x] Draft planning uses the worker model runner and pumps UI events while awaiting the local plan.
- [x] Keep render proof jobs cancellable.
  - [x] Route draft proof rendering through controller cancellation and timeout checks before decode, before each render, before saving, and before validation.
  - [x] Persist proof render cancellation policy and checkpoint metadata in validation reports so manifests show that proof jobs remain cancellable.
- [x] Reuse existing preview caches.
  - [x] Agent analysis and draft proof renders use `decode_source_image` with a nonnegative preview edge, which routes through the shared decoded-image cache.
  - [x] Reports expose `proofRenderSource.usesDecodedImageCache` so cache reuse is visible in artifacts and manifests.
- [x] Avoid full-resolution renders unless required for validation or export.
  - [x] Agent proof renders use a named bounded preview decode edge instead of full-resolution source decoding.
  - [x] Reports expose `proofRenderSource.sourceResolutionPolicy`, `sourceDecodeMaxEdge`, and `fullResolutionRequired=false`.
- [x] Use draft previews during iterative planning.
  - [x] Draft reports and manifests record preview proof policy and per-asset draft preview counts from validation iterations.
- [x] Use settled previews for final proof.
  - [x] Draft reports and manifests record settled final proof readiness and settled preview counts.
- [x] Bound batch concurrency.
  - [x] Run selected-set validation, draft, preflight, and export queue loops with an explicit controller concurrency limit of `1`.
  - [x] Include `batchConcurrencyLimit` in batch reports and manifests, and mirror it inside `batchSummary`.
- [x] Pause or deprioritize background agent work during active manual editing.
  - [x] `AgentEditController.manualEditingActive` pauses draft automation with a structured `backgroundWorkPolicy` report instead of mutating recipes.
  - [x] `StudioWorkspace` binds manual editing state from active crop/mask/sampling/brush tools and disables Agent runs while that state is active.
- [x] Record run timings and queue wait times.
  - [x] Record run start, finish, and duration for analyze, draft, preflight, and export runs.
  - [x] Persist `queueWaitMs` in finalized agent reports and run manifests, using `0` for immediate controller runs.

## Testing Requirements

- [x] Unit test style parsing and validation.
- [x] Test controller-backed user style creation and persistence.
- [x] Unit test recipe patch validation.
- [x] Unit test range clamping/rejection behavior.
- [x] Unit test agent draft snapshot/rollback behavior.
- [x] Add controller tests for single-photo agent run state transitions.
- [x] Add controller tests for analyze-only read-only behavior and artifacts.
- [x] Add tests for bulk draft apply and rollback.
  - [x] Add selected-set draft apply and rollback controller coverage.
  - [x] Add selected-set draft commit controller and bridge coverage.
  - [x] Add controller coverage for project-scope validation/preflight and filtered-library draft/rollback.
  - [x] Add catalog model coverage for visible-assets and project-assets scope helpers.
  - [x] Add controller coverage for clustered selected-set planning, representatives, sync groups, and manifest persistence.
  - [x] Add controller coverage for virtual-copy agent drafts and source-recipe independence.
- [x] Add tests for model/tool call schema handling with a fake local model.
  - [x] Add local model registry tests for default-local behavior and explicit cloud opt-in metadata.
- [x] Add tests for automation bridge authentication and malformed requests.
- [x] Add render proof tests for representative edits.
  - [x] Verify the single-photo controller writes before/after proof artifacts.
  - [x] Verify representative color, crop, and mask draft patches render readable proof artifacts and grouped recipe diffs through the controller proof path.
- [x] Add validation tests for clipping, masks, crop, and export preflight warnings.
  - [x] Add validation tests for clipping, saturation extremes, histogram shape, white balance, channel imbalance, and empty images.
  - [x] Add validation tests for recipe crop risk, mask coverage/leakage, and retouch provenance warnings.
  - [x] Add agent/bridge tests for export preflight warning reports.
- [x] Add MCP smoke tests for new `photo` tools.
  - [x] Add MCP missing-session/status error coverage.
  - [x] Add MCP tool metadata coverage for `photo_read_asset` and `photo_render_proof`.
  - [x] Add MCP `photo_render_proof` bridge-session smoke coverage with proof resource links.
  - [x] Add MCP tool metadata coverage for `photo_validate_edit`.
  - [x] Add MCP tool metadata coverage for `photo_export_preflight`.
  - [x] Add MCP tool metadata coverage for `photo_export_batch`.
  - [x] Add bridge and MCP metadata coverage for mask creation and mask adjustment tools.
  - [x] Add bridge and MCP metadata coverage for selected-set bulk plan and draft tools.
  - [x] Add controller, bridge, and MCP metadata coverage for selected-set batch validation.
  - [x] Add MCP resource read and resource-link coverage for agent run artifacts.
- [x] Add observability tests for agent run events.

## Rollout Plan

### Phase 1: Single-Photo Global Edit

- [x] Add style schema and local style storage.
- [x] Add `AgentEditController` skeleton.
- [x] Add fake local model runner for deterministic tests.
- [x] Add single-photo draft recipe patch support.
- [x] Support global tone/color/curve/detail tools.
- [x] Render before/after proof.
- [x] Validate clipping and recipe ranges.
- [x] Support commit and rollback.

### Phase 2: Live MCP/App Bridge

- [x] Add local automation bridge in the running app.
- [x] Add `photo` MCP toolset.
  - [x] Add first bridge-backed `photo` toolset commands.
- [x] Add app-running status and selected-asset tools.
- [x] Add draft apply, render proof, validate, commit, and rollback tools.
  - [x] Add draft apply, render proof, validate recipe patch, commit, and rollback tools.
  - [x] Add bridge-backed agent run cancellation command.
- [x] Add resource links for proof artifacts.
  - [x] Return proof artifact paths from bridge-backed draft/run tool results.
  - [x] Expose MCP resource links for JSON proof reports and PNG proof images from agent run artifacts.
  - [x] Read proof PNG artifacts as `image/png` MCP blob resources.
- [x] Add smoke tests for bridge and MCP integration.
  - [x] Add MCP photo tool smoke coverage for bridge-shaped responses and resource links.

### Phase 3: Full Color Tool Coverage

- [x] Expose every color edit group through typed tool schemas.
  - [x] `agent_recipe_patch_tool_schema()` exposes validation-backed typed schema metadata for raw, global tone, color, curves, HSL, color mixer, color grading, detail, and output groups.
  - [x] Expose first typed color harmony and color grading patch controls.
  - [x] Expose typed HSL, selective color, and RGB channel mixer patch controls.
- [x] Add recipe patch validation for every exposed field.
  - [x] `agent_recipe_patch_validation_coverage()` reports every exposed schema field as validated and recipe-patch-applied from the shared field tables.
  - [x] Add recipe patch validation for RAW numeric, boolean, and enum fields.
  - [x] Add recipe patch validation for master/luma/RGB point-curve arrays.
  - [x] Add recipe patch validation for crop rectangle bounds, crop preset strings, upright mode, and crop fill.
  - [x] Add recipe patch validation for output profile, soft-proof, render intent, and headroom fields.
  - [x] Add recipe patch validation for black-and-white channel mixer fields.
  - [x] Add recipe patch validation for color replacement and skin-tone protection fields.
  - [x] Add recipe patch validation for color harmony and grading fields.
  - [x] Add recipe patch validation for mixer model, HSL channel, per-channel mixer, and RGB channel mixer fields.
  - [x] Add recipe patch validation for adjustment-layer stacks and layer-local LUT entries.
  - [x] Add recipe patch validation for brush, lasso, range, generated, and depth mask stack entries.
  - [x] Add recipe patch validation for permission-gated retouch stack entries.
- [x] Add per-tool validation warnings.
  - [x] Recipe patch validation emits tool-specific review warnings for RAW, crop/optics, masks, retouch, layers/LUTs, and output edits.
- [x] Add style constraints for color-specific behavior.
  - [x] Add schema-v1 `constraints.color_constraints` for temperature bias, protected hue families, HSL channel bounds, color-grading saturation bounds, and harmony permission.
  - [x] Built-in styles publish color-specific constraints in their rulebooks.
  - [x] The deterministic local planner cites color-specific constraints and bounds generated `colorMixer` and `colorGrading` patch fields to those constraints.
- [x] Add validation for skin tone, saturation, color cast, and histogram balance.
  - [x] Image validation reports skin-tone plausibility, saturation extremes, white-balance/channel cast, and compressed histogram risks.

### Phase 4: Masking And Local Adjustments

- [x] Expose mask creation tools.
  - [x] Expose first linear/radial mask draft creation tools.
  - [x] `agent_recipe_patch_tool_schema()` exposes mask-stack creation kinds for linear, radial, brush, polygon/lasso, luminance, color, depth, subject, sky, and object masks.
- [x] Expose mask refinement tools.
- [x] Expose local adjustment tools.
  - [x] Expose first linear/radial local exposure and temperature adjustment tools.
  - [x] Expose mask-stack local tone, color, density, opacity, blend, and colorize recipe patch controls.
  - [x] `agent_recipe_patch_tool_schema()` exposes validation-backed local tone, color, detail, opacity, blend, and colorize adjustment fields for mask-stack entries.
- [x] Add mask coverage validation.
- [x] Add mask leakage/outlier warnings.
- [x] Add UI proof for masks and local edits.
  - [x] Agent draft reports and manifests expose `maskLocalProof` metadata with before/after proof paths, mask counts, local-adjustment counts, and the Agent dialog proof surface contract.
  - [x] The Agent dialog renders a proof strip from `agentEdit.lastRunReport` and shows mask/local proof readiness for mask-local drafts.

### Phase 5: Batch Editing

- [x] Add batch scope selection.
  - [x] Reuse checked selected assets as the first batch scope.
  - [x] Expose selected-set bulk planning and draft application over bridge/MCP.
  - [x] Add Agent dialog scope controls for current photo, selected assets, current project/album, and filtered library.
  - [x] Add controller `ForScope` entry points for validation, export preflight, planning, draft application, and patch application.
- [x] Add clustering.
  - [x] Add deterministic batch cluster reports with available/unavailable signal metadata.
- [x] Add representative-image editing.
  - [x] Plan each cluster from its representative asset and persist representative asset ids.
- [x] Add cluster-level recipe sync.
  - [x] Persist cluster sync groups in asset reports, batch summaries, and manifests.
- [x] Add outlier correction.
  - [x] Mark assets revised by validation feedback as cluster outlier corrections.
- [x] Add reversible batch transaction support.
  - [x] Restore every selected recipe when rolling back a selected-set draft.
- [x] Add batch validation report.
  - [x] Return per-asset validation reports for selected-set drafts.
  - [x] Return read-only selected-set batch validation reports with per-asset proofs.

### Phase 6: Retouch, Export, And Production Hardening

- [x] Add explicit retouch/inpaint permissions.
  - [x] Add explicit retouch recipe patch permission enforcement.
  - [x] Keep inpaint payload fields unavailable to agent recipe patches without a dedicated safe tool path.
- [x] Add retouch provenance validation.
- [x] Add export preflight and export tools.
  - [x] Add agent and bridge export preflight support.
  - [x] Add confirmed agent and bridge export queueing support.
- [x] Add stricter security review.
  - [x] `agent_recipe_patch_validation_coverage()` exposes a `securityReviewGate` proving direct/nested recipe payloads, agent-supplied binary mask/inpaint data, tool-permission bypasses, and retouch permission bypasses are rejected.
- [x] Add performance proof gates.
  - [x] Agent reports and manifests persist `performanceProofGate` with pass/fail status, budget, duration, queue wait, asset count, tool-call count, and proof-count evidence.
- [x] Add user-facing run history.
  - [x] Agent controller exposes a bounded `runHistory` list with recent mode, status, scope, asset count, timing, and artifact paths.
  - [x] Agent panel renders the five most recent run history entries.
- [x] Add style import/export polish.
  - [x] Add first user-facing style creation flow.
  - [x] Add controller-level validated style export/import JSON round trip with collision-safe imported style ids, reload, selection, and persisted file paths.

## Open Decisions

- [x] Choose first supported local model backend.
- [x] Decide whether the first agent worker is launched by the app or by MCP.
  - [x] First worker is app-owned in `AgentEditController`; MCP calls the running app through the local automation bridge.
- [x] Decide the style storage location and sync/backup behavior.
  - [x] User styles are stored under app data in `agent-styles`; sync/backup is deferred to normal app-data backup for the first release.
- [x] Decide whether agent-created masks are visible as normal user masks immediately or only after commit.
  - [x] Agent-created masks live in the draft recipe immediately for proof/review and are removed by rollback if not committed.
- [x] Decide bulk auto-commit limits.
  - [x] Bulk auto-commit limit is zero for first release; selected-set drafts always require explicit confirmed commit.
- [x] Decide how much image analysis is classical CV versus model-driven.
  - [x] First release uses model planning for recipe patches and deterministic render/classical image validation for proof gates.
- [x] Decide whether reference-image style matching is in scope after the first text/rulebook release.
  - [x] Reference-image style matching is deferred until after the first text/rulebook release.
