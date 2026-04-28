# AI Agent Operations

This repository now has one shared agent surface for Codex and Claude:

- `.mcp.json`
  - Project-scoped MCP server for Claude Code.
- `plugins/photo-editor-codex/`
  - Repo-local Codex plugin with the same MCP server plus shared hook guardrails.
- `.claude/settings.json`
  - Shared project hook configuration for Claude Code.
- `.claude/rules/`
  - Repo rules that load unconditionally or by path.
- `.claude/agents/`
  - Project subagents for review, desktop UI work, Rust/MCP/telemetry work, and release safety.
- `scripts/ai/`
  - Shared hook scripts used by both Codex and Claude.

## Claude

Claude Code reads `.mcp.json`, `.claude/settings.json`, `.claude/rules/`, and `.claude/agents/` directly from the repo.

- MCP server name: `photo-editor`
- Recommended verification loops:
  - Rust: `cd rust && cargo test --workspace`
  - Engine/headless native: `cmake --preset ci-headless && cmake --build --preset build-ci-headless && ctest --preset test-ci-headless --output-on-failure`
  - macOS desktop smoke: configure `dev-macos-debug`, build `app_bootstrap_test`, `app_shell_controller_test`, and `desktop_shell_smoke_test`, then run those tests with `ctest`

## Codex

The repo-local plugin lives at `plugins/photo-editor-codex`. The marketplace entry is in `.agents/plugins/marketplace.json`.

The plugin bundles:

- repo-safe `PreToolUse` guardrails for destructive Bash commands and secret-bearing file edits
- `PostToolUse` guidance after file writes so Codex stays on the smallest useful verification loop
- the same repository MCP server used by Claude

## OpenTelemetry

The Rust-side telemetry path is opt-in and layered on top of the existing JSONL observability.

Set either:

- `OTEL_EXPORTER_OTLP_ENDPOINT`
- `OTEL_EXPORTER_OTLP_TRACES_ENDPOINT`

Optional labels:

- `OTEL_SERVICE_NAMESPACE`
- `OTEL_DEPLOYMENT_ENVIRONMENT`

Current OTLP emitters:

- `photo-editor-mcp`
- Rust helper processes that initialize the catalog observability logger

The Qt/C++ desktop shell still relies on JSONL diagnostics for now.
