from hook_utils import emit_system_message, read_payload, tool_input, tool_name


MCP_PREFIX = "mcp__photo-editor__"

BUILD_TOOLS = {"cmake_configure", "cmake_build", "ctest_run", "cargo_test", "run_repo_checks"}

SUITE_FOLLOW_UPS = {
    "rust": "If a Rust test failed, read the failing test source with `read_path` and check the assertion.",
    "headless-ci": "If the headless build or tests failed, check the compiler error output first — strict warnings are enabled.",
    "headless-ci-tsan": "TSAN failures indicate data races. Read the reported stack traces carefully — the first access and the conflicting access point to the unprotected shared state.",
    "desktop-smoke": "Desktop smoke failures may be Qt/platform-specific. Check QT_QPA_PLATFORM and whether the test assumes on-screen rendering.",
}


def mcp_tool_name(full_name):
    if full_name.startswith(MCP_PREFIX):
        return full_name[len(MCP_PREFIX):]
    return None


def guidance_for_mcp_result(payload):
    full_name = tool_name(payload)
    short_name = mcp_tool_name(full_name)
    if not short_name:
        return ""

    tool_result = payload.get("tool_result", "")
    if isinstance(tool_result, dict):
        tool_result = str(tool_result.get("content", ""))
    tool_result_str = str(tool_result)

    is_error = "isError" in tool_result_str or "Command failed" in tool_result_str

    if short_name not in BUILD_TOOLS:
        return ""

    if not is_error:
        return ""

    notes = []
    input_data = tool_input(payload)

    if short_name == "run_repo_checks":
        suite = input_data.get("suite", "")
        follow_up = SUITE_FOLLOW_UPS.get(suite, "")
        if follow_up:
            notes.append(follow_up)
    elif short_name == "cargo_test":
        package = input_data.get("package", "the workspace")
        notes.append(f"Cargo test failed for {package}. Read the failing test source to understand the assertion before changing code.")
    elif short_name == "cmake_configure":
        preset = input_data.get("preset", "unknown")
        notes.append(f"CMake configure failed for preset `{preset}`. Check CMakeLists.txt and cmake/ modules for the error.")
    elif short_name == "cmake_build":
        notes.append("Build failed. Check the compiler error — strict warnings (-Wall -Wextra -Wpedantic -Wconversion) are enabled.")
    elif short_name == "ctest_run":
        notes.append("CTest failed. Use `ctest_run` with a `regex` filter to isolate the failing test, then `read_path` the test source.")

    if not notes:
        return ""

    return " ".join(notes)


if __name__ == "__main__":
    payload = read_payload()
    message = guidance_for_mcp_result(payload)
    if message:
        emit_system_message(message)
