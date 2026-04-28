from hook_utils import emit_system_message, normalized_paths, read_payload


CONCURRENCY_PATTERNS = (
    "mutex", "atomic", "thread", "future", "concurrent",
    "QFutureWatcher", "QMutex", "QThread",
)


def guidance_for(paths):
    notes = []
    joined = " ".join(paths)
    if any(path.startswith("rust/") for path in paths):
        if any(path.startswith("rust/crates/mcp/") for path in paths):
            notes.append("Run `cd rust && cargo test -p photo-editor-mcp -- --nocapture` before stopping.")
        else:
            notes.append("Run `cd rust && cargo test --workspace` before stopping.")
        if any(path.startswith("rust/crates/ffi/") for path in paths):
            notes.append("FFI changes require downstream C++ verification. Check that the static library links and any new symbols are declared in the C++ headers.")
    if any(path.startswith("engine/") for path in paths):
        notes.append("Engine changes should get the headless native loop: `cmake --preset ci-headless && cmake --build --preset build-ci-headless && ctest --preset test-ci-headless --output-on-failure`.")
    if any(path.startswith("apps/desktop/") for path in paths) or any(path.startswith("tests/") for path in paths):
        notes.append("Desktop changes should get the focused smoke loop on macOS: configure `dev-macos-debug`, build the smoke targets, then run `desktop_shell_smoke_test|app_shell_controller_test|app_bootstrap_test`.")
    if any(path.startswith("rust/crates/catalog/migrations/") for path in paths):
        notes.append("Schema migration changes require a running local Postgres to validate. Use the `catalog_local_db` MCP tool with action `ensure-running`.")
    if (
        ".github/workflows/ci.yml" in paths
        or "CMakePresets.json" in paths
        or any(path.startswith("cmake/") for path in paths)
        or any(path.startswith("scripts/") and ("release" in path or "notarize" in path or "verify" in path) for path in paths)
    ):
        notes.append("CI or release-path edits should be validated in both Linux headless CI and the macOS desktop smoke lane.")
    if not notes:
        return ""
    touched = f"Touched files: {joined}."
    return f"{touched} {' '.join(notes)}"


if __name__ == "__main__":
    payload = read_payload()
    paths = normalized_paths(payload)
    message = guidance_for(paths)
    if message:
        emit_system_message(message)
