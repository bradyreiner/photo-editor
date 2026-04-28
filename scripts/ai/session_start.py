import os

from hook_utils import emit_session_context


CONTEXT = """Photo Editor repo session defaults:
- Prefer CMake presets and focused native tests over ad hoc shell commands.
- Use `cargo test --workspace` for Rust work and focused desktop smoke tests for Qt/QML work.
- Use `ci-headless-tsan` preset for ThreadSanitizer validation when touching concurrent code.
- The project MCP server is defined in `.mcp.json` as `photo-editor`.
- Shared guardrails block destructive Bash commands, host-level package installs, and secret-bearing file edits.
- Preserve the controller pattern: QML stays declarative; C++ controllers own state and orchestration.
- Keep the Rust FFI boundary stable and explicit. Call out downstream C++ verification needs for FFI changes.
- Treat signing, notarization, and packaging as high-risk. Do not automate destructive release commands."""


def persist_env():
    env_file = os.environ.get("CLAUDE_ENV_FILE")
    if not env_file:
        return
    with open(env_file, "a", encoding="utf-8") as handle:
        handle.write("export RUST_BACKTRACE=1\n")
        handle.write("export QT_QPA_PLATFORM=offscreen\n")
        handle.write("export PHOTO_EDITOR_ENABLE_DEV_OBSERVABILITY=1\n")
        handle.write('export OTEL_SERVICE_NAMESPACE="${OTEL_SERVICE_NAMESPACE:-photo-editor-dev}"\n')
        handle.write('export OTEL_DEPLOYMENT_ENVIRONMENT="${OTEL_DEPLOYMENT_ENVIRONMENT:-development}"\n')


if __name__ == "__main__":
    persist_env()
    emit_session_context(CONTEXT)
