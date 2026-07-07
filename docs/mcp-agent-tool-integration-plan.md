# MCP Agent Tool Integration Plan

## Goal

Create a local agent runtime that can attach to the photo editor's MCP server, discover the app's tools, reason with a local model such as Ollama-hosted Qwen, call those tools under a rulebook, validate every edit, and save or export completed images without bypassing the existing editor pipeline.

The agent must never directly mutate image files or recipes. It plans and calls tools; the app validates, renders, drafts, commits, rolls back, and exports.

## Architecture

1. **Photo editor MCP server**
   - Runs beside the desktop app, bound to localhost only.
   - Exposes JSON-RPC MCP tools for reading assets, rendering proofs, applying recipe patches, validating drafts, committing drafts, rolling back drafts, and exporting.
   - Uses the same controller APIs already used by the UI and automation bridge.
   - Returns structured outputs and proof artifacts so agent runs are auditable.

2. **In-app MCP client**
   - Lives inside the agent runtime.
   - Connects to the local MCP server by stdio for bundled/in-process development or Streamable HTTP for a running app.
   - Calls `tools/list` at run start and caches the tool schemas for the selected run.
   - Enforces app-level allowlists before exposing tools to the model.

3. **Local model backend**
   - Adds an `ollama` backend to `AgentLocalModel`.
   - Calls `http://127.0.0.1:11434/api/chat` with a vision-capable model such as `qwen2.5vl:7b`.
   - Sends the current proof image, style rulebook, user instructions, available tool schemas, and current run state.
   - Requires structured JSON responses: either a direct recipe patch or an explicit tool call plan.

4. **Agent run orchestrator**
   - Owns the loop: observe, plan, call tool, inspect result, revise, validate, commit/export.
   - Maintains a strict tool budget, timeout budget, cancellation support, and rollback checkpoints.
   - Records every prompt, selected tool, arguments, result summary, proof path, validation warning, and final output path in the run manifest.

## Tool Surface

### Read-only tools

- `photo.live_status`
  - Returns selected asset count, active draft state, model status, manual-edit pause state, and queue state.
- `photo.list_assets`
  - Returns selected/current/filtered asset ids and metadata.
- `photo.read_asset`
  - Returns one asset's metadata, recipe summary, current style, and available proof paths.
- `photo.get_recipe_patch_schema`
  - Returns the typed color, crop, mask, retouch, layer, and export tool schema.
- `photo.get_style_rulebook`
  - Returns selected style goal, constraints, allowed tools, protected hues, batch policy, and export policy.
- `photo.render_current_proof`
  - Renders a current proof image and can return an MCP image content item or a file/resource link.
- `photo.inspect_recipe_diff`
  - Returns the current draft diff, risk assessment, and validation warnings.

### Draft tools

- `photo.plan_recipe_patch`
  - Optional helper that lets the app generate a deterministic baseline patch from the current rulebook.
- `photo.apply_recipe_patch_draft`
  - Applies a validated recipe patch as a reversible draft.
  - Requires confirmation for retouch, inpaint, destructive, or large batch changes.
- `photo.create_mask`
  - Creates supported masks through the same mask schema used by the editor.
- `photo.apply_mask_adjustment`
  - Applies local adjustments to an existing or newly created mask.
- `photo.render_draft_proof`
  - Renders before/after and validation artifacts for the active draft.
- `photo.rollback_agent_draft`
  - Restores the original recipes for every asset in the active draft transaction.

### Commit and export tools

- `photo.validate_current_draft`
  - Runs recipe and image validation before commit.
- `photo.validate_batch`
  - Validates all target assets and returns per-asset warnings.
- `photo.commit_agent_draft`
  - Requires explicit app-side approval unless the run policy allows auto-commit.
- `photo.export_preflight`
  - Checks preset, output path, color space, dimensions, naming, and overwrite risk.
- `photo.export_batch`
  - Queues export jobs after validation and confirmation.

## Agent Loop

1. **Start run**
   - User chooses scope: current photo, selected assets, filtered set, or collection.
   - User chooses style and model.
   - App snapshots current recipes and creates a run id.
   - MCP client lists tools and filters them by run policy.

2. **Observe**
   - Render a proof image for each representative asset or cluster.
   - Read recipe summary, histogram/validation signals, style constraints, and batch metadata.

3. **Plan**
   - Send the local model the proof image, rulebook, user instructions, available tool schemas, and current state.
   - Model must return structured JSON only.
   - Accepted output types:
     - `recipe_patch`
     - `tool_call_plan`
     - `needs_user_input`
     - `cannot_complete_safely`

4. **Execute tools**
   - App validates the requested tool name and arguments against the MCP schema.
   - Destructive or sensitive tools require confirmation.
   - Tool results are returned as structured content plus concise text for model context.

5. **Validate**
   - Validate recipe patch schema.
   - Render draft proof.
   - Run image validation and rulebook checks.
   - If warnings are repairable, send a compact validation report back to the model for one or two revision attempts.

6. **Apply**
   - Save edits as a reversible draft first.
   - For batch runs, apply per-cluster or per-asset patches, not unsafe global mask/retouch edits unless style policy allows it.

7. **Review or auto-commit**
   - Show before/after proof and tool log in the UI.
   - Commit only when the user approves or when the run policy explicitly allows unattended commit.

8. **Export**
   - Run export preflight.
   - Queue exports.
   - Persist manifest with source asset ids, output paths, model id, style id, tool calls, validation reports, and proof images.

## Model Prompt Contract

The model receives:

- Role: local photo-editing agent.
- Hard rule: do not invent tools or edit files directly.
- User instructions.
- Style rulebook.
- Allowed tool schemas.
- Current asset/proof context.
- Validation warnings from prior attempts.
- Required JSON output schema.

The model must return:

```json
{
  "decision": "recipe_patch",
  "summary": "Short edit intent.",
  "styleConstraintCitations": [
    {
      "section": "allowedTools",
      "key": "color",
      "reason": "Color edits are allowed by the selected style."
    }
  ],
  "recipePatch": {},
  "confidence": 0.82,
  "requiresConfirmation": false
}
```

For tool calling:

```json
{
  "decision": "tool_call_plan",
  "toolCalls": [
    {
      "name": "photo.render_current_proof",
      "arguments": {
        "assetId": "asset-1"
      },
      "reason": "Need current image proof before local adjustments."
    }
  ]
}
```

## MCP Server Implementation Plan

### Phase 1: Wrap Existing Automation Commands

- Reuse `PhotoAutomationBridge` command handlers as the first MCP tool implementation layer.
- Add a `PhotoMcpServer` class that maps MCP `tools/list` and `tools/call` to existing controller methods.
- Start with in-memory tests and stdio transport.
- Add tool schemas matching current automation commands.
- Return both `structuredContent` and a short text summary for each call.

Implementation note:

- Added `PhotoMcpServer`, a local MCP JSON-RPC adapter over `PhotoAutomationBridge`.
- `tools/list` now exposes photo tools with input schemas and safety annotations such as read-only, destructive, confirmation-required, and local-only.
- `tools/call` delegates to the existing bridge/controller commands and returns MCP-style `content`, `structuredContent`, and `isError` for tool execution failures.
- Added read-only bridge commands for `photo_get_recipe_patch_schema` and `photo_get_style_rulebook`.
- Verified with `photo_automation_bridge_test`, which now covers MCP tool listing, structured live-status calls, validation-backed recipe schema calls, unknown-tool errors, and unsupported-method JSON-RPC errors.
- Verified the desktop target builds with the adapter linked into `photo_editor_app`.

### Phase 2: Add Streamable HTTP for Running App Attachment

- Bind only to `127.0.0.1`.
- Use a random session token written to the app data runtime directory.
- Validate `Origin` on HTTP requests.
- Require token or local app session handshake before tool calls.
- Add UI indicator showing MCP server status, connected clients, exposed tool count, and last tool call.

Implementation note:

- `PhotoAutomationBridge` now starts a second localhost-only MCP HTTP endpoint at `/mcp` alongside the existing automation bridge.
- The endpoint accepts JSON-RPC MCP requests over HTTP POST and routes them through `PhotoMcpServer`.
- The bridge validates local `Origin` headers, requires `Content-Length`, rejects malformed/non-JSON bodies, and returns JSON HTTP responses.
- The existing session file now advertises `mcpProtocol`, `mcpEndpointKind`, `mcpEndpoint`, and `mcpTransport`.
- App startup logging now includes the MCP endpoint and endpoint kind.
- `AgentEditController` now exposes `mcpEndpoint`, `mcpEndpointKind`, `mcpStatus`, `mcpToolCount`, `mcpToolNames`, `mcpAvailable`, and `refreshMcpTools()`.
- The Studio agent dialog and EditView agent panel show a compact local MCP status/endpoint row.
- `photo_automation_bridge_test` verifies the session metadata, makes real localhost HTTP calls for MCP `tools/list` and `tools/call`, and checks the controller-visible MCP status/tool cache.
- The same test now verifies `GET /mcp` returns a localhost-only `text/event-stream` discovery stream with endpoint metadata and advertised tool names for external Streamable HTTP clients.
- `desktop_shell_smoke_test` has source guards for the Studio/Edit MCP status indicators.
- The bridge now tracks active/total MCP HTTP client counts and the last `tools/call` name/status/timestamp. `photo.live_status`, the session file, `AgentEditController`, StudioWorkspace, and EditView expose that telemetry.

### Phase 3: Add In-App MCP Client

- Add `AgentMcpClient`.
- Support `tools/list`, `tools/call`, timeouts, cancellation, and structured errors.
- Cache tool schemas per run id.
- Enforce app-side allowlist:
  - read-only
  - draft edit
  - retouch/inpaint
  - commit
  - export
- Never expose commit/export tools to the model unless run policy allows them.

Implementation note:

- Added `AgentMcpClient`, a localhost-only MCP client for the app-side agent runtime.
- The client supports `tools/list` through `refreshToolCache`, caches tool schemas per run id, exposes cached tool names, and rejects tool calls missing from the cached schema.
- The client supports `tools/call` with structured content extraction, timeout handling, cancellation flags, JSON-RPC protocol error handling, and MCP tool execution errors.
- The client enforces a per-run allowlist before making network calls, so sensitive tools such as commit/export can be hidden from a model even when the MCP server exposes them.
- Active run cancellation is now wired into MCP discovery, observation, and model-selected tool-call clients through a controller cancellation predicate. The HTTP wait loop aborts in-flight local MCP requests and reports `canceled: true`.
- `photo_automation_bridge_test` verifies schema caching, allowed read-only tool calls, allowlist blocking, missing-cache rejection, and active-request cancellation against the running localhost MCP endpoint.
- Verified the desktop target builds with `AgentMcpClient` linked into `photo_editor_app`.

### Phase 4: Add Ollama/Qwen Backend

- Extend model manifest support:

```json
{
  "id": "qwen2.5vl-7b-ollama",
  "name": "Qwen2.5-VL 7B via Ollama",
  "backend": "ollama",
  "endpoint": "http://127.0.0.1:11434",
  "model": "qwen2.5vl:7b",
  "vision": true,
  "available": true
}
```

- Probe `/api/tags` and model availability.
- Add install UI:
  - detect Ollama
  - install instructions
  - pull model
  - test model
  - select as active agent model
- Send proof PNGs as base64 images.
- Use JSON schema or strict JSON mode.
- Reject non-JSON, unknown tools, unsupported fields, or missing style citations.

Implementation note:

- `AgentLocalModel` now supports configured `backend: "ollama"` models in `agent-models/models.json`.
- Ollama model metadata is treated as local-only, supports `endpoint`, `model`, `vision`, `status`, and `available`, and requires a localhost `http://127.0.0.1` or `http://localhost` endpoint.
- `plan_agent_recipe_patch` dispatches available Ollama models to a local `/api/chat` runner instead of rejecting them as unsupported.
- The Ollama runner sends:
  - selected model name
  - non-streaming chat request
  - structured JSON schema for the response
  - style rulebook JSON
  - validation-backed recipe patch schema
  - user instructions
  - current recipe JSON
  - base64 rendered proof bytes when the model manifest has `vision: true`; the source file is only a fallback if no proof artifact is available
- The runner accepts Ollama chat `message.content` or generate-style `response`, requires valid JSON, and rejects missing summary, missing style citations, or missing recipe patches.
- Prompt audit records whether an image was included, byte count, image source kind, and source path. Full manifests keep this for local audit; shareable manifests redact absolute paths and prompt text.
- `agent_local_model_test` verifies Ollama manifest discovery, localhost-only metadata, request payload shape, rendered proof image preference, schema inclusion, structured JSON parsing, warnings propagation, and recipe patch validation.
- `AgentEditController` exposes `prepareSelectedModelInstall()` and `installSelectedModel(confirmed)` for Ollama-backed models. The install method starts a local `ollama pull <model>` process only after confirmation and reports running/status/output through model-install properties.
- `probe_agent_local_model()` and `AgentEditController::probeSelectedModel()` call the local Ollama `/api/show` endpoint for the selected model and update runtime status/details such as model family, parameter size, and quantization level.
- The EditView model panel shows the Ollama pull command, a Test button, a Pull button, and install/probe status for the selected Ollama model.
- Ollama pull output is parsed as newline-delimited JSON progress. `AgentEditController` exposes `modelInstallProgress` with status, digest, bytes, and percent while preserving raw output for audit/debugging.
- The draft planner renders a current proof artifact for the representative asset before model planning, injects that proof path into the local-model request, and records the planning proof report/path in the run report.
- `agent_local_model_test` includes a dedicated fake Ollama HTTP server path for deterministic `/api/show` and `/api/chat` request/response coverage. If the current test runtime refuses localhost socket binding, the test falls back to the existing fake-response hook while preserving the same payload assertions.

### Phase 5: Agent Tool-Reasoning Loop

- Add `AgentRunOrchestrator`.
- Inputs:
  - scope
  - selected style
  - selected model
  - run policy
  - user instructions
- Output:
  - draft transaction
  - proof artifacts
  - validation report
  - tool call manifest
  - export manifest if requested
- Support:
  - pause
  - cancel
  - rollback
  - retry with validation warnings
  - resume failed batch from last clean asset

Implementation note:

- `AgentEditController::runDraftForScope` now performs MCP tool discovery before local model planning and records it as `mcpDiscovery`, `mcpAttached`, `mcpToolCount`, `mcpToolNames`, and `mcpEndpointKind` in the draft report.
- Draft runs call read-only MCP observation tools before local model planning: `photo.live_status`, `photo.get_style_rulebook`, and `photo.get_recipe_patch_schema`. Results are stored as `mcpObservation` and compact `mcp.tools/call ...` audit entries.
- The Ollama response contract now accepts `decision: "recipe_patch"` or `decision: "tool_call_plan"`. Tool-call plans are bounded to a read-only allowlist, executed through `AgentMcpClient`, stored as `modelToolPlan`, and followed by one required final `recipe_patch` model turn.
- The Ollama response contract also accepts `needs_user_input` and `cannot_complete_safely` as safe non-draft decisions. The controller surfaces these with `needsUserInput`, `cannotCompleteSafely`, `userQuestions`, and `safeStopReason` instead of applying a patch.
- Draft `toolCalls` now include `mcp.tools/list`, fixed read-only MCP observation calls, model-selected `mcp.model_tool_call ...` entries, and `plan_agent_recipe_patch`, so run manifests show whether the agent had a live local tool surface, what context was read, and which model-requested tools ran.
- Local-model planning now returns `modelPromptAudit` with the system/user prompt text, schema mode, tool/result counts, and image byte counts. Full local manifests persist this audit data, while shareable manifests redact prompt text.
- The planner now renders a representative current-proof image before local-model planning, sends that rendered proof to vision-capable Ollama models, and records the planning proof report/path for audit.
- Model-planned draft runs now attempt one model-backed repair turn when validation returns warnings. The repair request includes the current patch, validation report, and proof artifact context, records `modelRepairReports`, and then falls back to the procedural revision helper if the model cannot return a valid replacement patch.
- Shareable manifests redact prompt-audit-shaped maps wherever they appear, including nested model repair reports, so validation/proof paths embedded inside repair prompts are not leaked.
- Failed batch draft reports now include a `resumePlan` with clean asset reports, per-asset patches, cluster metadata, and the source run id. `resumeLastFailedBatchDraft()` reuses clean validation reports only when the original recipe still matches, continues from the pending assets, and drafts the full selection through the same validation/apply pipeline.
- The run still applies only validated recipe patches through the existing draft pipeline; model-selected tools are currently limited to read-only observation tools and one tool-call round before final patch planning.
- `agent_local_model_test` verifies tool-call-plan parsing, `agent_edit_controller_test` verifies graceful no-endpoint reporting, model-backed validation repair, and resumable failed batch drafts, and `photo_automation_bridge_test` verifies live MCP-attached draft runs execute a model-selected `photo.live_status` call before final planning and remain rollbackable.

### Phase 6: Bulk Editing

- Cluster assets by camera, recipe, lighting, rating, and metadata.
- Run the model on representative assets first.
- Apply synchronized color/global patches to the cluster.
- Require per-asset review for masks, retouch, crop, and inpaint unless style policy explicitly allows global application.
- Validate and export each asset independently.

Implementation status:

- Batch draft planning clusters scoped assets by capture/camera/lens/exposure and recipe signals, records representative asset ids, and stores cluster planning metadata in run reports and manifests.
- Multi-asset draft runs plan representative patches per cluster, synchronize validated recipe patches to the cluster assets, and persist per-asset cluster ids, representative ids, sync groups, proof artifacts, validation reports, and outlier correction flags.
- Project and filtered-library scopes are supported for validation, export preflight, and scoped draft application without mutating unrelated hidden assets.
- Commit, rollback, validation, and export preflight reports include `batchSummary`, `assetReports`, proof paths, and manifest paths so bulk runs remain auditable.
- `agent_edit_controller_test` covers selected-asset clustering, representative planning, per-asset draft reports, project/filtered scopes, rollback, commit confirmation, virtual-copy scoping, timeouts, missing sources, and export preflight.

### Phase 7: UI

- Add an Agent Runs panel:
  - model selector
  - MCP status
  - style selector
  - scope selector
  - allowed tools toggles
  - run/stop/pause/rollback controls
  - live tool call log
  - before/after proof strip
  - validation warnings
  - approve commit/export buttons
- Show privacy/offline status clearly:
  - local model
  - local MCP server
  - no cloud calls
  - cloud disabled unless explicitly opted in

Implementation status:

- The agent UI exposes local model setup/probe controls, MCP endpoint/status, style/scope/run controls, draft commit/export/rollback controls, proof thumbnails, and run history.
- EditView and StudioWorkspace now expose compact run audit panels with proof artifact paths, bounded tool-call logs, and merged run/validation warnings from the last run report.
- `desktop_shell_smoke_test` source guards cover the MCP status indicators, Ollama pull/probe controls, and run audit proof/tool/warning UI contracts.

## Safety Rules

- The model cannot write files.
- The model cannot call shell commands.
- The model cannot bypass recipe patch validation.
- MCP tools validate all inputs.
- Commit/export requires app-side confirmation unless a named unattended policy is selected.
- Retouch, inpaint, destructive crop, overwrite export, and bulk mask operations require stronger confirmation.
- Every run has rollback checkpoints.
- Every run writes an audit manifest.
- Tool results are sanitized before they are sent back to the model.
- Tool names and descriptions are stable, compact, and unambiguous.

## Testing Plan

- Unit test MCP tool listing and schemas.
- Unit test each tool call maps to the correct controller method.
- Test invalid tool arguments return structured tool errors.
- Test unauthorized commit/export/retouch calls are rejected.
- Test local model bad JSON is rejected.
- Test local model unsupported tool calls are rejected.
- Test validation warning repair loop.
- Test cancel/rollback during active MCP tool calls. Current coverage verifies active MCP client cancellation; draft rollback-on-cancel coverage remains in `agent_edit_controller_test`.
- Test bulk run applies per-asset edits without mutating unrelated selected assets.
- Test run manifest contains model id, tool calls, proof paths, validation reports, and export paths.
- Test Ollama request/response behavior through the dedicated fake HTTP server when localhost socket binding is available, with fake-response fallback in runtimes that reject test listeners.

## Definition Of Done

- A user can install/select a local Qwen vision model.
- The running app exposes a local MCP server.
- The in-app agent can discover and call app tools through MCP.
- The model sees images and rulebooks, then returns structured patch/tool plans.
- The app validates and applies drafts through existing edit pipeline only.
- User can review, commit, rollback, and export.
- Bulk runs produce saved/exported images plus a complete audit manifest.
- Focused tests cover MCP tools, local model backend, validation, rollback, and bulk export.
