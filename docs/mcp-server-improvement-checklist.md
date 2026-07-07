# MCP Server Improvement Checklist

This checklist is scoped to additions that should materially improve agent work in this repository: faster code understanding, safer validation, better failure triage, and stronger performance/release workflows.

## Foundation

- [x] Add tool timeouts and output caps for all command-running tools.
  - Needed because builds, tests, and perf scripts can hang or emit oversized logs.
- [x] Add structured `structuredContent` results and `outputSchema` metadata for major tools.
  - Start with `search_code`, `run_repo_checks`, `ctest_run`, `cargo_test`, `list_observability_runs`, and `read_observability_run`.
- [x] Add strict path allowlists for tools that read outside the repo.
  - In particular, constrain observability reads to known log roots or explicit repo-local paths.
- [x] Add tool annotations that distinguish read-only, destructive, idempotent, and long-running tools.
- [x] Add tests for path rejection, unknown tool errors, structured output shape, and failed command reporting.

## Discovery And Context

- [x] Add `list_cmake_presets`.
  - Parse `CMakePresets.json` and return configure/build/test presets with descriptions where available.
- [x] Add `list_cargo_packages`.
  - Parse `rust/Cargo.toml` and return workspace package names and paths.
- [x] Add `list_ctest_tests`.
  - Run `ctest -N` against a selected build directory or preset and return structured test names.
- [x] Add `repo_status`.
  - Return safe `git status --short`, changed files, and diff stat without exposing full patch by default.
- [x] Add `changed_files_context`.
  - For changed files, return likely owning subsystem, nearby tests, and recommended docs/resources.
- [x] Add `find_related_tests`.
  - Map source paths to likely native, Rust, QML, smoke, or perf tests.

## Resource Templates And Completion

- [x] Implement non-empty `resources/templates/list`.
  - Useful templates: `repo://file/{path}`, `repo://doc/{name}`, `repo://test/{name}`, `repo://artifact/{artifact_id}`, `repo://observability/{run_id}/{file}`.
- [x] Implement `completion/complete` for prompt and resource arguments.
  - Complete file paths, docs, CMake presets, Cargo packages, test names, observability runs, and perf artifact IDs.
- [x] Add resource links from tool results.
  - Example: test failures link to `repo://file/tests/...`; observability summaries link to `repo://observability/...`.

## Verification Workflows

- [x] Add `change_verification_plan`.
  - Input: changed files or a task summary. Output: smallest useful validation loop and why.
- [x] Add native desktop workflow tools that avoid accidental all-target test builds.
  - `native_dev_plan` explains launch-ready, app-only, QML lint, smoke, full, and relaunch loops.
  - `run_native_dev_checks` and `start_native_dev_task` build only the app target for normal launch prep, with smoke/full validation as explicit modes.
  - `cmake_build` accepts safe explicit targets and rejects high-risk targets such as install/package.
- [x] Add `run_focused_checks`.
  - Input: changed files. Run the narrowest relevant checks when confidence is high.
- [x] Split `run_repo_checks` into structured steps.
  - Return configure/build/test phases separately with status, command, duration, and failing phase.
- [x] Add `summarize_build_failure`.
  - Extract compiler diagnostics, warning policy failures, missing dependencies, and likely owning files.
- [x] Add `summarize_test_failure`.
  - Extract failing test names, assertions, relevant log excerpts, and likely source files.

## Performance And Proof Gates

- [x] Add `run_perf_gate`.
  - Support curated suites: `preview-pipeline`, `preview-frontier`, `preview-release-proof`, `desktop-preview-proof`, `color-adjustment`, `metal-proof`, and `mask-performance`.
- [x] Add `summarize_perf_artifact`.
  - Parse `perf-artifacts` output and return scenario, status, key metrics, regressions, and artifact paths.
- [x] Add `compare_perf_artifacts`.
  - Compare baseline and candidate artifacts using existing scripts where available.
- [x] Add `list_perf_artifacts`.
  - Return recent artifacts grouped by scenario, timestamp, and pass/fail status.
- [x] Add `validate_desktop_preview_proof`.
  - Wrap the existing desktop preview proof gate with structured pass/fail evidence.

## Observability

- [x] Add `summarize_observability_run`.
  - Return event counts, warnings/errors, session metadata, render timings, and notable state transitions.
- [x] Add `query_observability_events`.
  - Filter JSONL runs by event name, severity, category, field equality, and time bounds.
- [x] Add `list_observability_files`.
  - Show available files inside a run before reading them.
- [x] Add resource templates for log runs and JSONL files.
- [x] Add output truncation that preserves beginning, failure-focused excerpts, and tail.

## GitHub And CI Integration

- [x] Add optional GitHub API-backed tools when `GITHUB_TOKEN` is present.
  - Keep the server useful without network credentials.
- [x] Add `list_pull_requests`.
- [x] Add `summarize_pr_checks`.
- [x] Add `read_failed_ci_logs`.
- [x] Add `list_recent_workflow_runs`.
- [x] Add `summarize_release_workflow`.
  - Focus on packaging, signing, notarization, appcast, and artifact status.
- [x] Add read-only mode for GitHub tools by default.
  - Defer issue/PR mutation tools until there is a concrete need.

## Prompts

- [x] Add `plan-rust-change`.
- [x] Add `plan-engine-change`.
- [x] Add `plan-qml-change`.
- [x] Add `triage-preview-performance`.
- [x] Add `triage-observability-run`.
- [x] Add `prepare-release-validation`.
- [x] Update existing prompts to reference the new discovery and verification tools.

## Task And Progress Support

- [x] Add progress notifications for long-running commands.
  - Build, test, repo-check, perf, and release-proof tools should report step progress.
- [x] Add cancellation support for long-running child processes.
- [x] Consider MCP task support for expensive workflows.
  - Good candidates: `run_repo_checks`, `run_perf_gate`, and desktop release proof workflows.
- [x] Stream or chunk long command output instead of buffering everything until process exit.

## Toolset Modes

- [x] Add configurable toolsets to reduce tool noise.
  - Suggested sets: `context`, `code`, `build`, `test`, `perf`, `observability`, `release`, `db`, `github`.
- [x] Add environment-driven read-only mode.
  - Hide or reject tools that start/stop services, mutate databases, or run release operations.
- [x] Add a server info resource that lists enabled toolsets and unavailable optional dependencies.

## Security And Reliability

- [x] Add a central command runner with timeout, output limit, cwd policy, environment policy, and structured diagnostics.
- [x] Validate preset, suite, package, and action names against discovered allowlists instead of free strings.
- [x] Avoid exposing arbitrary command execution.
- [x] Add explicit high-risk labels for DB lifecycle, release, signing, packaging, and network-backed tools.
- [x] Add snapshot tests for tool descriptions so advertised behavior stays aligned with implementation.
- [x] Add a local smoke test that exercises `initialize`, `tools/list`, `resources/list`, `prompts/list`, and one call per critical tool.

## Suggested Implementation Order

1. Command runner hardening: timeouts, output caps, structured diagnostics.
2. Structured outputs for existing tools.
3. Discovery tools: presets, packages, tests, repo status, related tests.
4. Resource templates and completion.
5. Failure summarizers for builds/tests.
6. Perf/proof gate tools.
7. Observability query and summary tools.
8. GitHub read-only CI/PR tools.
9. Progress, cancellation, and task support.
10. Toolset modes and read-only mode.
