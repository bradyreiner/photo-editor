#!/usr/bin/env python3
"""Inventory popup families and reject custom windows that bypass shared chrome."""
from pathlib import Path
import json
import re
import sys

root = Path(__file__).resolve().parents[1]
counts = {name: 0 for name in ("AppPopup", "AppDialog", "AppDrawer", "AppToolTip", "AppMenu", "native_file_pickers", "native_application_menus")}
violations = []
shared = {"AppPopup.qml": "Popup", "AppDialog.qml": "Dialog", "AppDrawer.qml": "Drawer", "AppToolTip.qml": "ToolTip", "AppMenu.qml": "Menu"}
for path in sorted((root / "apps/desktop/qml").rglob("*.qml")):
    source = path.read_text()
    for line_number, line in enumerate(source.splitlines(), 1):
        match = re.match(r"\s*(?:(?:popup|delegate):\s*)?(?:Controls\.)?(AppPopup|AppDialog|AppDrawer|AppToolTip|AppMenu|Popup|Dialog|Drawer|ToolTip|Menu|FileDialog|FolderDialog)\s*\{", line)
        if match:
            kind = match.group(1)
            if kind in counts:
                counts[kind] += 1
            elif kind in ("FileDialog", "FolderDialog"):
                counts["native_file_pickers"] += 1
            elif shared.get(path.name) == kind:
                pass
            elif kind == "Menu" and path.name == "Main.qml":
                counts["native_application_menus"] += 1
            else:
                violations.append(f"{path.relative_to(root)}:{line_number}: use the shared {kind} component")
        if re.match(r"\s*(?:Controls\.)?ToolTip\.(visible|text|delay|timeout):", line):
            violations.append(f"{path.relative_to(root)}:{line_number}: use AppToolTip")
report = {"counts": counts, "violations": violations, "passed": not violations}
print(json.dumps(report, indent=2))
sys.exit(1 if violations else 0)
