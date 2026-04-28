import json
import os
import sys
from pathlib import Path


def read_payload():
    raw = sys.stdin.read().strip()
    if not raw:
        return {}
    try:
        return json.loads(raw)
    except json.JSONDecodeError:
        return {}


def tool_name(payload):
    return str(payload.get("tool_name", ""))


def tool_input(payload):
    value = payload.get("tool_input")
    if isinstance(value, dict):
        return value
    return {}


def extract_paths(payload):
    input_value = tool_input(payload)
    candidates = []
    for key in ("file_path", "path", "old_file_path", "new_file_path"):
        value = input_value.get(key)
        if isinstance(value, str) and value:
            candidates.append(value)
    paths_value = input_value.get("paths")
    if isinstance(paths_value, list):
        for value in paths_value:
            if isinstance(value, str) and value:
                candidates.append(value)
    return candidates


def normalized_paths(payload):
    project_dir = Path(payload.get("cwd") or os.getcwd())
    values = []
    for raw_path in extract_paths(payload):
        path = Path(raw_path)
        if not path.is_absolute():
            path = project_dir / path
        try:
            relative = path.resolve().relative_to(project_dir.resolve())
            values.append(relative.as_posix())
        except Exception:
            values.append(path.as_posix())
    return values


def deny_pre_tool(reason):
    emit_json(
        {
            "hookSpecificOutput": {
                "hookEventName": "PreToolUse",
                "permissionDecision": "deny",
                "permissionDecisionReason": reason
            }
        }
    )


def emit_system_message(message):
    emit_json({"systemMessage": message})


def emit_session_context(message):
    emit_json(
        {
            "hookSpecificOutput": {
                "hookEventName": "SessionStart",
                "additionalContext": message
            }
        }
    )


def emit_json(payload):
    sys.stdout.write(json.dumps(payload))
