import re
import sys

from hook_utils import deny_pre_tool, normalized_paths, read_payload, tool_input, tool_name


COMMAND_RULES = [
    (re.compile(r"\brm\s+-rf\b"), "Destructive recursive deletes require an explicit human decision."),
    (re.compile(r"\bgit\s+reset\s+--hard\b"), "Hard git resets are blocked by the project guardrails."),
    (re.compile(r"\bgit\s+checkout\s+--\b"), "Blind file reverts are blocked by the project guardrails."),
    (re.compile(r"\bgit\s+clean\s+-f"), "Git clean is blocked unless a human runs it directly."),
    (re.compile(r"\bgit\s+push\s+--force\b"), "Force pushes are blocked by the project guardrails."),
    (re.compile(r"\bsudo\b"), "Privilege escalation is blocked from agent hooks."),
    (re.compile(r"\b(brew|apt-get|apt|dnf|yum|pipx|cargo)\s+install\b"), "Host-level package installs require a human decision."),
    (re.compile(r"\bnpm\s+install\s+-g\b"), "Global package installs require a human decision."),
    (re.compile(r"curl\s+[^|]+\|\s*(sh|bash)"), "Piped shell installers are blocked."),
    (re.compile(r"\bxcrun\s+notarytool\b"), "Notarization should be run manually with explicit credentials."),
    (re.compile(r"scripts/sign-notarize-macos\.sh"), "Signing and notarization should be run manually."),
    (re.compile(r"\bdropdb\b"), "Dropping databases requires an explicit human decision."),
]

SECRET_SUFFIXES = (
    ".env",
    ".env.local",
    ".pem",
    ".key",
    ".p12",
    ".mobileprovision"
)


def guard_bash(payload):
    command = str(tool_input(payload).get("command", ""))
    for pattern, reason in COMMAND_RULES:
        if pattern.search(command):
            deny_pre_tool(reason)
            return True
    return False


def guard_file_writes(payload):
    for path in normalized_paths(payload):
        lowered = path.lower()
        if lowered.endswith(SECRET_SUFFIXES):
            deny_pre_tool(f"Editing `{path}` is blocked because it may contain secrets or signing material.")
            return True
        if lowered == ".claude/settings.local.json":
            deny_pre_tool("Project hooks should not modify `.claude/settings.local.json`; keep local overrides human-owned.")
            return True
    return False


if __name__ == "__main__":
    payload = read_payload()
    name = tool_name(payload)
    if name == "Bash":
        guard_bash(payload)
    elif name in {"Edit", "MultiEdit", "Write"}:
        guard_file_writes(payload)
    sys.exit(0)
