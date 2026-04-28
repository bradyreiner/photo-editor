use std::fs;
use std::path::{Component, Path, PathBuf};
use std::process::Command;

use serde_json::{json, Value};
use tracing::{error, info, instrument, warn};

pub const MCP_PROTOCOL_VERSION: &str = "2025-11-25";

#[derive(Debug, Clone)]
pub struct PhotoEditorMcpServer {
    repo_root: PathBuf,
}

impl PhotoEditorMcpServer {
    pub fn discover() -> Result<Self, String> {
        let repo_root = discover_repo_root(std::env::current_dir().map_err(io_error)?)
            .ok_or_else(|| "Unable to locate the photo-editor repository root.".to_string())?;
        Ok(Self { repo_root })
    }

    pub fn repo_root(&self) -> &Path {
        &self.repo_root
    }

    pub fn handle_input_line(&self, line: &str) -> Vec<Value> {
        if line.trim().is_empty() {
            return Vec::new();
        }

        match serde_json::from_str::<Value>(line) {
            Ok(Value::Array(messages)) => messages
                .into_iter()
                .filter_map(|message| self.handle_message(message))
                .collect(),
            Ok(message) => self.handle_message(message).into_iter().collect(),
            Err(error) => vec![jsonrpc_error(
                Value::Null,
                -32700,
                format!("Invalid JSON: {error}"),
            )],
        }
    }

    #[instrument(skip_all, fields(mcp.method = %method_name(message.as_object())))]
    pub fn handle_message(&self, message: Value) -> Option<Value> {
        let request = message.as_object()?;
        let id = request.get("id").cloned();
        let method = request.get("method")?.as_str()?;
        let params = request.get("params").cloned().unwrap_or_else(|| json!({}));

        match method {
            "initialize" => {
                id.map(|request_id| jsonrpc_result(request_id, self.initialize_result()))
            }
            "notifications/initialized" => None,
            "ping" => id.map(|request_id| jsonrpc_result(request_id, json!({}))),
            "logging/setLevel" => id.map(|request_id| jsonrpc_result(request_id, json!({}))),
            "tools/list" => {
                id.map(|request_id| jsonrpc_result(request_id, json!({ "tools": self.tools() })))
            }
            "tools/call" => id.map(|request_id| self.call_tool(request_id, params)),
            "resources/list" => id.map(|request_id| {
                jsonrpc_result(request_id, json!({ "resources": self.resources() }))
            }),
            "resources/templates/list" => {
                id.map(|request_id| jsonrpc_result(request_id, json!({ "resourceTemplates": [] })))
            }
            "resources/read" => id.map(|request_id| self.read_resource(request_id, params)),
            "prompts/list" => id
                .map(|request_id| jsonrpc_result(request_id, json!({ "prompts": self.prompts() }))),
            "prompts/get" => id.map(|request_id| self.get_prompt(request_id, params)),
            _ => id.map(|request_id| {
                jsonrpc_error(request_id, -32601, format!("Method not found: {method}"))
            }),
        }
    }

    fn initialize_result(&self) -> Value {
        json!({
            "protocolVersion": MCP_PROTOCOL_VERSION,
            "capabilities": {
                "prompts": { "listChanged": false },
                "resources": { "subscribe": false, "listChanged": false },
                "tools": { "listChanged": false }
            },
            "serverInfo": {
                "name": "photo-editor-mcp",
                "title": "Photo Editor MCP",
                "version": env!("CARGO_PKG_VERSION"),
                "description": "Repository-specific MCP server for the Photo Editor desktop app."
            },
            "instructions": concat!(
                "Use the repository tools instead of free-form shell commands whenever possible. ",
                "Prefer build presets, focused tests, and observability summaries over ad hoc commands.\n\n",
                "## Build & Test Conventions\n",
                "- Use `cmake --preset ci-headless` and `cmake --build --preset build-ci-headless` for headless native validation.\n",
                "- Use `ctest --preset test-ci-headless --output-on-failure` for headless C++ coverage.\n",
                "- Use `cargo test --workspace` (via the cargo_test tool) for Rust workspace validation.\n",
                "- For macOS desktop work, use the focused smoke loop: configure `dev-macos-debug`, build the smoke targets, then run `desktop_shell_smoke_test|app_shell_controller_test|app_bootstrap_test`.\n",
                "- Use `ci-headless-tsan` preset for ThreadSanitizer validation when touching concurrent code.\n",
                "- The `run_repo_checks` tool wraps the common validation suites: `rust`, `headless-ci`, `headless-ci-tsan`, and `desktop-smoke`.\n\n",
                "## Architecture Guardrails\n",
                "- Preserve the controller pattern: QML stays declarative; C++ controllers own state and orchestration.\n",
                "- Treat the Rust workspace as production code. Keep the FFI boundary stable and explicit.\n",
                "- For observability, keep the existing JSONL logs intact and layer OpenTelemetry on top.\n",
                "- Treat signing, notarization, and packaging as high-risk. Do not automate destructive release commands.\n",
                "- When touching preview, render, or job state plumbing, inspect nearby tests for stale expectations.\n",
                "- CI changes should improve confidence for the exact surface being edited. Note remaining gaps."
            )
        })
    }

    fn tools(&self) -> Vec<Value> {
        vec![
            tool(
                "search_code",
                "Search the repository with ripgrep.",
                json!({
                    "type": "object",
                    "properties": {
                        "pattern": { "type": "string", "description": "Ripgrep pattern to search for." },
                        "path": { "type": "string", "description": "Optional relative path to narrow the search." },
                        "glob": { "type": "string", "description": "Optional ripgrep glob such as '*.cpp'." },
                        "max_matches": { "type": "integer", "description": "Maximum matches to return. Defaults to 100." }
                    },
                    "required": ["pattern"]
                }),
            ),
            tool(
                "read_path",
                "Read a text file from the repository with optional line slicing.",
                json!({
                    "type": "object",
                    "properties": {
                        "path": { "type": "string", "description": "Relative path inside the repository." },
                        "start_line": { "type": "integer", "description": "1-based starting line. Defaults to 1." },
                        "line_count": { "type": "integer", "description": "Number of lines to read. Defaults to 200." }
                    },
                    "required": ["path"]
                }),
            ),
            tool(
                "cmake_configure",
                "Run cmake configure with one of the checked-in presets.",
                json!({
                    "type": "object",
                    "properties": {
                        "preset": { "type": "string", "description": "Configure preset name such as ci-headless or dev-macos-debug." },
                        "rust_binary_dir": { "type": "string", "description": "Optional absolute or relative Rust target directory passed as PHOTO_EDITOR_RUST_BINARY_DIR." }
                    },
                    "required": ["preset"]
                }),
            ),
            tool(
                "cmake_build",
                "Run cmake --build with a checked-in build preset.",
                json!({
                    "type": "object",
                    "properties": {
                        "preset": { "type": "string", "description": "Build preset name such as build-ci-headless or build-dev-macos-debug." }
                    },
                    "required": ["preset"]
                }),
            ),
            tool(
                "ctest_run",
                "Run ctest with either a preset or a regex filter.",
                json!({
                    "type": "object",
                    "properties": {
                        "preset": { "type": "string", "description": "Optional test preset such as test-ci-headless." },
                        "build_dir": { "type": "string", "description": "Optional build directory when not using a preset." },
                        "regex": { "type": "string", "description": "Optional ctest -R regex." }
                    }
                }),
            ),
            tool(
                "cargo_test",
                "Run cargo tests for the Rust workspace or a single package.",
                json!({
                    "type": "object",
                    "properties": {
                        "package": { "type": "string", "description": "Optional package name." },
                        "filter": { "type": "string", "description": "Optional test name filter." }
                    }
                }),
            ),
            tool(
                "run_repo_checks",
                "Run one of the curated repository validation suites.",
                json!({
                    "type": "object",
                    "properties": {
                        "suite": {
                            "type": "string",
                            "enum": ["rust", "headless-ci", "headless-ci-tsan", "desktop-smoke"],
                            "description": "Named validation suite."
                        }
                    },
                    "required": ["suite"]
                }),
            ),
            tool(
                "catalog_local_db",
                "Run the catalog_local_db helper through cargo.",
                json!({
                    "type": "object",
                    "properties": {
                        "action": {
                            "type": "string",
                            "enum": ["status", "ensure-running", "stop"],
                            "description": "Lifecycle action to invoke."
                        }
                    },
                    "required": ["action"]
                }),
            ),
            tool(
                "list_observability_runs",
                "List the most recent structured observability runs.",
                json!({
                    "type": "object",
                    "properties": {
                        "log_dir": { "type": "string", "description": "Optional log root. Defaults to PHOTO_EDITOR_LOG_DIR or ~/.photo-editor/logs." },
                        "limit": { "type": "integer", "description": "Maximum runs to return. Defaults to 10." }
                    }
                }),
            ),
            tool(
                "read_observability_run",
                "Read session metadata or a jsonl file from an observability run directory.",
                json!({
                    "type": "object",
                    "properties": {
                        "run_directory": { "type": "string", "description": "Absolute or relative path to the run directory." },
                        "file": { "type": "string", "description": "Optional file name inside the run, such as desktop.jsonl. Defaults to session.json." },
                        "line_count": { "type": "integer", "description": "Maximum lines to read from JSONL files. Defaults to 80." }
                    },
                    "required": ["run_directory"]
                }),
            ),
        ]
    }

    fn resources(&self) -> Vec<Value> {
        vec![
            resource(
                "repo://overview",
                "repo-overview",
                "Repository overview, architecture, and core workflows.",
            ),
            resource(
                "repo://build-presets",
                "build-presets",
                "CMake presets and development commands used by this repository.",
            ),
            resource(
                "repo://test-matrix",
                "test-matrix",
                "Available test suites, presets, and CI lanes with their coverage scope.",
            ),
            resource(
                "repo://migrations",
                "migrations",
                "SQL migration file listing and latest schema changes.",
            ),
            resource(
                "repo://agents",
                "agents",
                "Specialized agent definitions and their responsibilities.",
            ),
            resource(
                "repo://observability",
                "observability-guide",
                "Structured logging and OpenTelemetry guidance for the project.",
            ),
            resource(
                "repo://ai-runtime",
                "ai-runtime",
                "Current desktop AI runtime notes, including smart selection and inpaint support.",
            ),
            resource(
                "repo://claude-rules",
                "claude-rules",
                "Project-level Claude hooks, subagents, and guardrails.",
            ),
        ]
    }

    fn prompts(&self) -> Vec<Value> {
        vec![
            prompt(
                "plan-desktop-change",
                "Plan a desktop UI or controller change against the Qt/QML app.",
                vec![prompt_argument(
                    "goal",
                    "What should change in the desktop app?",
                    true,
                )],
            ),
            prompt(
                "debug-build-failure",
                "Investigate a failing build or test using the repo tools and presets.",
                vec![
                    prompt_argument("command", "The failing command or preset.", true),
                    prompt_argument("symptoms", "Observed failure details or logs.", false),
                ],
            ),
            prompt(
                "review-release-readiness",
                "Review packaging, CI, and release readiness for a change.",
                vec![prompt_argument("change_summary", "What changed?", true)],
            ),
        ]
    }

    fn call_tool(&self, request_id: Value, params: Value) -> Value {
        let Some(name) = params.get("name").and_then(Value::as_str) else {
            return jsonrpc_error(
                request_id,
                -32602,
                "tools/call requires params.name".to_string(),
            );
        };
        let arguments = params
            .get("arguments")
            .and_then(Value::as_object)
            .cloned()
            .unwrap_or_default();

        let result = match name {
            "search_code" => self.tool_search_code(&arguments),
            "read_path" => self.tool_read_path(&arguments),
            "cmake_configure" => self.tool_cmake_configure(&arguments),
            "cmake_build" => self.tool_cmake_build(&arguments),
            "ctest_run" => self.tool_ctest_run(&arguments),
            "cargo_test" => self.tool_cargo_test(&arguments),
            "run_repo_checks" => self.tool_run_repo_checks(&arguments),
            "catalog_local_db" => self.tool_catalog_local_db(&arguments),
            "list_observability_runs" => self.tool_list_observability_runs(&arguments),
            "read_observability_run" => self.tool_read_observability_run(&arguments),
            _ => Err(format!("Unknown tool: {name}")),
        };

        match result {
            Ok(text) => jsonrpc_result(
                request_id,
                json!({
                    "content": [{ "type": "text", "text": text }]
                }),
            ),
            Err(message) => {
                warn!(mcp.tool = name, error = %message, "tool failed");
                jsonrpc_result(
                    request_id,
                    json!({
                        "content": [{ "type": "text", "text": message }],
                        "isError": true
                    }),
                )
            }
        }
    }

    fn read_resource(&self, request_id: Value, params: Value) -> Value {
        let Some(uri) = params.get("uri").and_then(Value::as_str) else {
            return jsonrpc_error(
                request_id,
                -32602,
                "resources/read requires params.uri".to_string(),
            );
        };

        match self.resource_text(uri) {
            Ok(text) => jsonrpc_result(
                request_id,
                json!({
                    "contents": [{
                        "uri": uri,
                        "mimeType": "text/markdown",
                        "text": text
                    }]
                }),
            ),
            Err(message) => jsonrpc_error(request_id, -32602, message),
        }
    }

    fn get_prompt(&self, request_id: Value, params: Value) -> Value {
        let Some(name) = params.get("name").and_then(Value::as_str) else {
            return jsonrpc_error(
                request_id,
                -32602,
                "prompts/get requires params.name".to_string(),
            );
        };
        let arguments = params
            .get("arguments")
            .and_then(Value::as_object)
            .cloned()
            .unwrap_or_default();

        let prompt_text = match name {
            "plan-desktop-change" => {
                let Ok(goal) = required_string(&arguments, "goal") else {
                    return jsonrpc_error(
                        request_id,
                        -32602,
                        "prompts/get for plan-desktop-change requires arguments.goal".to_string(),
                    );
                };
                format!(
                    "Plan the desktop app change: {goal}\n\nUse repo://overview and repo://build-presets first. Then use read_path and search_code to inspect the relevant Qt controllers and QML workspaces. End with the smallest verification loop that matches the touched files."
                )
            }
            "debug-build-failure" => {
                let Ok(command) = required_string(&arguments, "command") else {
                    return jsonrpc_error(
                        request_id,
                        -32602,
                        "prompts/get for debug-build-failure requires arguments.command"
                            .to_string(),
                    );
                };
                let symptoms = optional_string(&arguments, "symptoms");
                format!(
                    "Investigate the failing command: {command}\nSymptoms: {}\n\nUse the repo tools instead of arbitrary shell commands. Start from the matching preset or validation suite, isolate the failing target, then read the owning source files and propose the next fix.",
                    symptoms.unwrap_or("not provided")
                )
            }
            "review-release-readiness" => {
                let Ok(summary) = required_string(&arguments, "change_summary") else {
                    return jsonrpc_error(
                        request_id,
                        -32602,
                        "prompts/get for review-release-readiness requires arguments.change_summary".to_string(),
                    );
                };
                format!(
                    "Review release readiness for this change: {summary}\n\nInspect CI workflow coverage, build presets, packaging scripts, and observability docs. Call out missing validation and packaging risks first."
                )
            }
            _ => return jsonrpc_error(request_id, -32602, format!("Unknown prompt: {name}")),
        };

        jsonrpc_result(
            request_id,
            json!({
                "description": format!("Prompt template: {name}"),
                "messages": [{
                    "role": "user",
                    "content": { "type": "text", "text": prompt_text }
                }]
            }),
        )
    }

    fn resource_text(&self, uri: &str) -> Result<String, String> {
        match uri {
            "repo://overview" => Ok(format!(
                "# Photo Editor repository overview\n\n- Repo root: {}\n- Desktop app: apps/desktop\n- Engine: engine\n- Rust workspace: rust\n- Tests: tests\n\nImportant local workflows:\n- `cmake --preset ci-headless && cmake --build --preset build-ci-headless`\n- `ctest --preset test-ci-headless --output-on-failure`\n- `cd rust && cargo test --workspace`\n- `./scripts/run-native.sh`\n",
                self.repo_root.display()
            )),
            "repo://build-presets" => self.read_repo_file("CMakePresets.json"),
            "repo://test-matrix" => {
                let presets = self.read_repo_file("CMakePresets.json")?;
                let ci = self.read_repo_file(".github/workflows/ci.yml")?;
                Ok(format!(
                    "# Test Matrix\n\n## CMake Presets\n```json\n{presets}\n```\n\n## CI Workflow\n```yaml\n{ci}\n```\n\n## Available `run_repo_checks` Suites\n- `rust` — `cargo test --workspace --locked`\n- `headless-ci` — configure + build + ctest with `ci-headless` preset\n- `headless-ci-tsan` — same as headless-ci with ThreadSanitizer\n- `desktop-smoke` — macOS-only: dev-macos-debug build + focused smoke tests\n"
                ))
            }
            "repo://migrations" => {
                let migrations_dir = self.repo_root.join("rust/crates/catalog/migrations");
                let mut entries = fs::read_dir(&migrations_dir)
                    .map_err(io_error)?
                    .filter_map(Result::ok)
                    .filter(|e| {
                        e.path()
                            .extension()
                            .and_then(|ext| ext.to_str())
                            == Some("sql")
                    })
                    .collect::<Vec<_>>();
                entries.sort_by_key(|e| e.file_name());

                let mut output = String::from("# SQL Migrations\n\n");
                for entry in &entries {
                    let name = entry.file_name();
                    output.push_str(&format!("## {}\n```sql\n", name.to_string_lossy()));
                    if let Ok(contents) = fs::read_to_string(entry.path()) {
                        output.push_str(&contents);
                    }
                    output.push_str("```\n\n");
                }
                Ok(output)
            }
            "repo://agents" => {
                let agents_dir = self.repo_root.join(".claude/agents");
                let mut sections = Vec::new();
                if agents_dir.is_dir() {
                    let mut entries: Vec<_> = fs::read_dir(&agents_dir)
                        .map_err(io_error)?
                        .filter_map(Result::ok)
                        .filter(|e| {
                            e.path()
                                .extension()
                                .and_then(|ext| ext.to_str())
                                == Some("md")
                        })
                        .collect();
                    entries.sort_by_key(|e| e.file_name());
                    for entry in entries {
                        if let Ok(contents) = fs::read_to_string(entry.path()) {
                            sections.push(format!(
                                "## {}\n{}",
                                entry.file_name().to_string_lossy(),
                                contents
                            ));
                        }
                    }
                }
                if sections.is_empty() {
                    Ok("No agent definitions found.\n".to_string())
                } else {
                    Ok(format!("# Agent Definitions\n\n{}", sections.join("\n---\n\n")))
                }
            }
            "repo://observability" => self.read_repo_file("docs/observability.md"),
            "repo://ai-runtime" => {
                let header = self.read_repo_file("apps/desktop/src/ai_model_runtime.h")?;
                let source = self.read_repo_file("apps/desktop/src/ai_model_runtime.cpp")?;
                Ok(format!("{header}\n\n---\n\n{source}"))
            }
            "repo://claude-rules" => {
                let mut sections = vec![self.read_repo_file("CLAUDE.md")?];
                let settings_path = self.repo_root.join(".claude/settings.json");
                if settings_path.exists() {
                    sections.push(self.read_repo_file(".claude/settings.json")?);
                }
                let rules_dir = self.repo_root.join(".claude/rules");
                if rules_dir.is_dir() {
                    let mut rules: Vec<_> = fs::read_dir(&rules_dir)
                        .map_err(io_error)?
                        .filter_map(Result::ok)
                        .filter(|e| {
                            e.path()
                                .extension()
                                .and_then(|ext| ext.to_str())
                                == Some("md")
                        })
                        .collect();
                    rules.sort_by_key(|e| e.file_name());
                    for entry in rules {
                        if let Ok(contents) = fs::read_to_string(entry.path()) {
                            sections.push(contents);
                        }
                    }
                }
                Ok(sections.join("\n\n---\n\n"))
            }
            _ => Err(format!("Unknown resource URI: {uri}")),
        }
    }

    #[instrument(skip_all, fields(tool.name = "search_code"))]
    fn tool_search_code(
        &self,
        arguments: &serde_json::Map<String, Value>,
    ) -> Result<String, String> {
        let pattern = required_string(arguments, "pattern")?;
        let path = optional_string(arguments, "path");
        let glob = optional_string(arguments, "glob");
        let max_matches = clamp_int(
            arguments.get("max_matches").and_then(Value::as_i64),
            1,
            200,
            100,
        );

        let mut command = Command::new("rg");
        command
            .arg("-n")
            .arg("--hidden")
            .arg("--color")
            .arg("never")
            .arg("--max-count")
            .arg(max_matches.to_string())
            .arg("--glob")
            .arg("!.git")
            .arg(pattern)
            .current_dir(&self.repo_root);
        if let Some(glob_value) = glob {
            command.arg("--glob").arg(glob_value);
        }
        if let Some(path_value) = path {
            command.arg(
                self.resolve_relative_path(path_value)?
                    .strip_prefix(&self.repo_root)
                    .map_err(path_error)?,
            );
        }

        run_command(command)
    }

    fn tool_read_path(&self, arguments: &serde_json::Map<String, Value>) -> Result<String, String> {
        let path = required_string(arguments, "path")?;
        let start_line = clamp_int(
            arguments.get("start_line").and_then(Value::as_i64),
            1,
            i64::MAX,
            1,
        );
        let line_count = clamp_int(
            arguments.get("line_count").and_then(Value::as_i64),
            1,
            400,
            200,
        );
        let resolved = self.resolve_relative_path(path)?;
        let contents = fs::read_to_string(&resolved).map_err(io_error)?;
        let lines = contents.lines().collect::<Vec<_>>();
        let start_index = usize::try_from(start_line.saturating_sub(1))
            .map_err(|_| "Invalid start_line".to_string())?;
        if start_index >= lines.len() {
            return Ok("No lines in requested range.\n".to_string());
        }
        let end_index = std::cmp::min(lines.len(), start_index.saturating_add(line_count as usize));

        let mut excerpt = String::new();
        for (offset, line) in lines[start_index..end_index].iter().enumerate() {
            let line_number = start_index + offset + 1;
            excerpt.push_str(&format!("{line_number:>5}: {line}\n"));
        }
        if excerpt.is_empty() {
            excerpt.push_str("No lines in requested range.\n");
        }
        Ok(excerpt)
    }

    fn tool_cmake_configure(
        &self,
        arguments: &serde_json::Map<String, Value>,
    ) -> Result<String, String> {
        let preset = required_string(arguments, "preset")?;
        let mut command = Command::new("cmake");
        command
            .arg("--preset")
            .arg(preset)
            .current_dir(&self.repo_root);

        if let Some(rust_binary_dir) = optional_string(arguments, "rust_binary_dir") {
            let rust_binary_path = self.resolve_user_path(rust_binary_dir);
            command.arg(format!(
                "-DPHOTO_EDITOR_RUST_BINARY_DIR={}",
                rust_binary_path.display()
            ));
        }

        run_command(command)
    }

    fn tool_cmake_build(
        &self,
        arguments: &serde_json::Map<String, Value>,
    ) -> Result<String, String> {
        let preset = required_string(arguments, "preset")?;
        let mut command = Command::new("cmake");
        command
            .arg("--build")
            .arg("--preset")
            .arg(preset)
            .current_dir(&self.repo_root);
        run_command(command)
    }

    fn tool_ctest_run(&self, arguments: &serde_json::Map<String, Value>) -> Result<String, String> {
        let preset = optional_string(arguments, "preset");
        let regex = optional_string(arguments, "regex");
        let build_dir = optional_string(arguments, "build_dir");
        let mut command = Command::new("ctest");
        command.current_dir(&self.repo_root);

        if let Some(preset_value) = preset {
            command.arg("--preset").arg(preset_value);
        } else if let Some(build_dir_value) = build_dir {
            command
                .arg("--test-dir")
                .arg(self.resolve_user_path(build_dir_value));
        }
        command.arg("--output-on-failure");
        if let Some(regex_value) = regex {
            command.arg("-R").arg(regex_value);
        }

        run_command(command)
    }

    fn tool_cargo_test(
        &self,
        arguments: &serde_json::Map<String, Value>,
    ) -> Result<String, String> {
        let package = optional_string(arguments, "package");
        let filter = optional_string(arguments, "filter");

        let mut command = Command::new("cargo");
        command
            .arg("test")
            .arg("--manifest-path")
            .arg(self.repo_root.join("rust/Cargo.toml"))
            .arg("--locked")
            .current_dir(&self.repo_root);

        if let Some(package_name) = package {
            command.arg("-p").arg(package_name);
        } else {
            command.arg("--workspace");
        }
        if let Some(filter_value) = filter {
            command.arg(filter_value);
        }

        run_command(command)
    }

    fn tool_run_repo_checks(
        &self,
        arguments: &serde_json::Map<String, Value>,
    ) -> Result<String, String> {
        let suite = required_string(arguments, "suite")?;
        match suite {
            "rust" => self.tool_cargo_test(&serde_json::Map::new()),
            "headless-ci" => {
                let configure_output =
                    self.tool_cmake_configure(&map_with("preset", "ci-headless"))?;
                let build_output =
                    self.tool_cmake_build(&map_with("preset", "build-ci-headless"))?;
                let test_output = self.tool_ctest_run(&map_with("preset", "test-ci-headless"))?;
                Ok(format!("{configure_output}\n{build_output}\n{test_output}"))
            }
            "headless-ci-tsan" => {
                let configure_output =
                    self.tool_cmake_configure(&map_with("preset", "ci-headless-tsan"))?;
                let build_output =
                    self.tool_cmake_build(&map_with("preset", "build-ci-headless-tsan"))?;
                let test_output =
                    self.tool_ctest_run(&map_with("preset", "test-ci-headless-tsan"))?;
                Ok(format!("{configure_output}\n{build_output}\n{test_output}"))
            }
            "desktop-smoke" => {
                if !cfg!(target_os = "macos") {
                    return Err(
                        "desktop-smoke is intended for macOS hosts with Qt available.".to_string(),
                    );
                }
                let configure_output =
                    self.tool_cmake_configure(&map_with("preset", "dev-macos-debug"))?;
                let build_output =
                    self.tool_cmake_build(&map_with("preset", "build-dev-macos-debug"))?;
                let mut test_args = map_with("build_dir", "build/dev-macos-debug");
                test_args.insert(
                    "regex".to_string(),
                    json!("desktop_shell_smoke_test|app_shell_controller_test|app_bootstrap_test"),
                );
                let test_output = self.tool_ctest_run(&test_args)?;
                Ok(format!("{configure_output}\n{build_output}\n{test_output}"))
            }
            _ => Err(format!("Unknown validation suite: {suite}")),
        }
    }

    fn tool_catalog_local_db(
        &self,
        arguments: &serde_json::Map<String, Value>,
    ) -> Result<String, String> {
        let action = required_string(arguments, "action")?;
        let mut command = Command::new("cargo");
        command
            .arg("run")
            .arg("--manifest-path")
            .arg(self.repo_root.join("rust/Cargo.toml"))
            .arg("--locked")
            .arg("-p")
            .arg("photo-catalog")
            .arg("--bin")
            .arg("catalog_local_db")
            .arg("--")
            .arg(action)
            .current_dir(&self.repo_root);
        run_command(command)
    }

    fn tool_list_observability_runs(
        &self,
        arguments: &serde_json::Map<String, Value>,
    ) -> Result<String, String> {
        let limit = clamp_int(arguments.get("limit").and_then(Value::as_i64), 1, 50, 10) as usize;
        let log_root = arguments
            .get("log_dir")
            .and_then(Value::as_str)
            .map(|value| self.resolve_user_path(value))
            .or_else(default_log_dir)
            .ok_or_else(|| {
                "Unable to infer PHOTO_EDITOR_LOG_DIR; pass log_dir explicitly.".to_string()
            })?;
        let runs_dir = log_root.join("runs");
        let mut entries = fs::read_dir(&runs_dir)
            .map_err(io_error)?
            .filter_map(Result::ok)
            .filter(|entry| entry.path().is_dir())
            .collect::<Vec<_>>();
        entries.sort_by_key(|entry| entry.file_name());
        entries.reverse();

        let mut output = String::new();
        for entry in entries.into_iter().take(limit) {
            let session_path = entry.path().join("session.json");
            let session_summary = fs::read_to_string(&session_path)
                .ok()
                .and_then(|contents| serde_json::from_str::<Value>(&contents).ok())
                .map(|json| {
                    let session_id = json
                        .get("session_id")
                        .and_then(Value::as_str)
                        .unwrap_or("unknown");
                    let started = json
                        .get("started_at")
                        .and_then(Value::as_str)
                        .unwrap_or("unknown");
                    format!(
                        "{} | session={} | started={}",
                        entry.path().display(),
                        session_id,
                        started
                    )
                })
                .unwrap_or_else(|| entry.path().display().to_string());
            output.push_str(&session_summary);
            output.push('\n');
        }
        if output.is_empty() {
            output.push_str("No observability runs found.\n");
        }
        Ok(output)
    }

    fn tool_read_observability_run(
        &self,
        arguments: &serde_json::Map<String, Value>,
    ) -> Result<String, String> {
        let run_directory = required_string(arguments, "run_directory")?;
        let file_name = optional_string(arguments, "file").unwrap_or("session.json");
        let line_count = clamp_int(
            arguments.get("line_count").and_then(Value::as_i64),
            1,
            400,
            80,
        );
        let run_path = self.resolve_user_path(run_directory);
        let file_path = run_path.join(file_name);
        let contents = fs::read_to_string(&file_path).map_err(io_error)?;

        if file_path.extension().and_then(|value| value.to_str()) == Some("jsonl") {
            let lines = contents
                .lines()
                .take(line_count as usize)
                .collect::<Vec<_>>();
            Ok(lines.join("\n"))
        } else {
            Ok(contents)
        }
    }

    fn read_repo_file(&self, path: &str) -> Result<String, String> {
        let resolved = self.resolve_relative_path(path)?;
        fs::read_to_string(resolved).map_err(io_error)
    }

    fn resolve_relative_path(&self, relative_path: &str) -> Result<PathBuf, String> {
        validate_relative_path(relative_path)?;
        let joined = self.repo_root.join(relative_path);
        let canonical = fs::canonicalize(&joined).map_err(io_error)?;
        if !canonical.starts_with(&self.repo_root) {
            return Err(format!("Path escapes repository root: {relative_path}"));
        }
        Ok(canonical)
    }

    fn resolve_user_path(&self, value: &str) -> PathBuf {
        let candidate = PathBuf::from(value);
        if candidate.is_absolute() {
            candidate
        } else {
            self.repo_root.join(candidate)
        }
    }
}

fn map_with(key: &str, value: &str) -> serde_json::Map<String, Value> {
    let mut map = serde_json::Map::new();
    map.insert(key.to_string(), json!(value));
    map
}

fn tool(name: &str, description: &str, input_schema: Value) -> Value {
    json!({
        "name": name,
        "title": name.replace('-', " "),
        "description": description,
        "inputSchema": input_schema
    })
}

fn resource(uri: &str, name: &str, description: &str) -> Value {
    json!({
        "uri": uri,
        "name": name,
        "title": name.replace('-', " "),
        "description": description,
        "mimeType": "text/markdown"
    })
}

fn prompt(name: &str, description: &str, arguments: Vec<Value>) -> Value {
    json!({
        "name": name,
        "title": name.replace('-', " "),
        "description": description,
        "arguments": arguments
    })
}

fn prompt_argument(name: &str, description: &str, required: bool) -> Value {
    json!({
        "name": name,
        "description": description,
        "required": required
    })
}

fn jsonrpc_result(id: Value, result: Value) -> Value {
    json!({
        "jsonrpc": "2.0",
        "id": id,
        "result": result
    })
}

fn jsonrpc_error(id: Value, code: i64, message: String) -> Value {
    json!({
        "jsonrpc": "2.0",
        "id": id,
        "error": {
            "code": code,
            "message": message
        }
    })
}

fn method_name(message: Option<&serde_json::Map<String, Value>>) -> String {
    message
        .and_then(|value| value.get("method"))
        .and_then(Value::as_str)
        .unwrap_or("unknown")
        .to_string()
}

fn required_string<'a>(
    arguments: &'a serde_json::Map<String, Value>,
    key: &str,
) -> Result<&'a str, String> {
    arguments
        .get(key)
        .and_then(Value::as_str)
        .ok_or_else(|| format!("Missing required string argument: {key}"))
}

fn optional_string<'a>(
    arguments: &'a serde_json::Map<String, Value>,
    key: &str,
) -> Option<&'a str> {
    arguments.get(key).and_then(Value::as_str)
}

fn clamp_int(value: Option<i64>, min: i64, max: i64, default: i64) -> i64 {
    value.unwrap_or(default).clamp(min, max)
}

fn io_error(error: std::io::Error) -> String {
    error.to_string()
}

fn path_error(error: std::path::StripPrefixError) -> String {
    error.to_string()
}

fn validate_relative_path(path: &str) -> Result<(), String> {
    let candidate = Path::new(path);
    if candidate.is_absolute() {
        return Err("Path must be relative to the repository root.".to_string());
    }
    if candidate
        .components()
        .any(|component| matches!(component, Component::ParentDir))
    {
        return Err("Path must not contain '..' segments.".to_string());
    }
    Ok(())
}

fn discover_repo_root(start: PathBuf) -> Option<PathBuf> {
    let mut current = Some(start.as_path());
    while let Some(path) = current {
        if path.join("AGENTS.md").exists()
            && path.join("CMakePresets.json").exists()
            && path.join("rust/Cargo.toml").exists()
        {
            return Some(path.to_path_buf());
        }
        current = path.parent();
    }
    None
}

fn default_log_dir() -> Option<PathBuf> {
    std::env::var_os("PHOTO_EDITOR_LOG_DIR")
        .map(PathBuf::from)
        .or_else(|| {
            std::env::var_os("HOME").map(|home| {
                PathBuf::from(home)
                    .join("Library/Application Support/PhotoEditor")
                    .join("logs")
            })
        })
}

#[instrument(skip(command), fields(command.bin = ?command.get_program(), command.args = ?command.get_args().collect::<Vec<_>>()))]
fn run_command(mut command: Command) -> Result<String, String> {
    let output = command.output().map_err(io_error)?;
    let stdout = String::from_utf8_lossy(&output.stdout);
    let stderr = String::from_utf8_lossy(&output.stderr);
    if output.status.success() {
        let trimmed = stdout.trim();
        if !trimmed.is_empty() {
            info!("command completed");
            Ok(trimmed.to_string())
        } else if !stderr.trim().is_empty() {
            Ok(stderr.trim().to_string())
        } else {
            Ok(format!(
                "Command completed successfully with status {}.",
                output.status
            ))
        }
    } else {
        error!(status = %output.status, stderr = %stderr.trim(), "command failed");
        let mut message = format!("Command failed with status {}.", output.status);
        if !stdout.trim().is_empty() {
            message.push_str("\n\nstdout:\n");
            message.push_str(stdout.trim());
        }
        if !stderr.trim().is_empty() {
            message.push_str("\n\nstderr:\n");
            message.push_str(stderr.trim());
        }
        Err(message)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::TempDir;

    fn create_repo() -> (TempDir, PhotoEditorMcpServer) {
        let temp_dir = TempDir::new().expect("temp dir");
        fs::write(temp_dir.path().join("AGENTS.md"), "root").expect("write AGENTS");
        fs::write(temp_dir.path().join("CMakePresets.json"), "{}").expect("write presets");
        fs::create_dir_all(temp_dir.path().join("rust")).expect("create rust dir");
        fs::write(temp_dir.path().join("rust/Cargo.toml"), "[workspace]").expect("write cargo");
        let server = PhotoEditorMcpServer {
            repo_root: temp_dir.path().to_path_buf(),
        };
        (temp_dir, server)
    }

    #[test]
    fn rejects_parent_segments() {
        let (_temp_dir, server) = create_repo();
        let error = server
            .resolve_relative_path("../secret.txt")
            .expect_err("should reject");
        assert!(error.contains("must not contain '..'"));
    }

    #[test]
    fn returns_initialize_result() {
        let (_temp_dir, server) = create_repo();
        let responses = server.handle_input_line(
            r#"{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}}}"#,
        );
        assert_eq!(responses.len(), 1);
        let protocol = responses[0]["result"]["protocolVersion"]
            .as_str()
            .expect("protocol version");
        assert_eq!(protocol, MCP_PROTOCOL_VERSION);
    }

    #[test]
    fn returns_prompt_argument_error() {
        let (_temp_dir, server) = create_repo();
        let responses = server.handle_input_line(
            r#"{"jsonrpc":"2.0","id":3,"method":"prompts/get","params":{"name":"plan-desktop-change","arguments":{}}}"#,
        );
        assert_eq!(responses.len(), 1);
        let message = responses[0]["error"]["message"]
            .as_str()
            .expect("error message");
        assert!(message.contains("arguments.goal"));
    }
}
