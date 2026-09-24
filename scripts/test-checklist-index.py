#!/usr/bin/env python3
"""Self-test for validate-checklist-index.py."""

from __future__ import annotations

import importlib.util
import json
import tempfile
from pathlib import Path


SCRIPT = Path(__file__).with_name("validate-checklist-index.py")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    spec = importlib.util.spec_from_file_location("checklist_index_validator", SCRIPT)
    if spec is None or spec.loader is None:
        raise RuntimeError("unable to load checklist index validator")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)

    with tempfile.TemporaryDirectory() as temporary:
        source_root = Path(temporary)
        docs = source_root / "docs"
        docs.mkdir()
        strategy = docs / "color-engine-and-app-foundations-completion-strategy.md"
        strategy.write_text("# Strategy\n", encoding="utf-8")
        plan = docs / "example-plan.md"
        plan.write_text("# Plan\n<!-- work-id: EXAMPLE-001 -->\n", encoding="utf-8")
        index_path = docs / "checklist-index.json"
        index = {
            "schemaVersion": 1,
            "documents": [
                {
                    "id": "DOC-COLOR",
                    "path": "docs/color-engine-and-app-foundations-completion-strategy.md",
                    "area": "color",
                    "state": "active",
                    "canonicalOwner": "docs/color-engine-and-app-foundations-completion-strategy.md",
                },
                {
                    "id": "DOC-EXAMPLE",
                    "path": "docs/example-plan.md",
                    "area": "example",
                    "state": "active",
                    "canonicalOwner": "docs/example-plan.md",
                },
            ],
        }
        index_path.write_text(json.dumps(index), encoding="utf-8")
        require(not module.validate(source_root, index_path), "valid ownership index should pass")

        duplicate = docs / "duplicate-checklist.md"
        duplicate.write_text("# Duplicate\n<!-- work-id: EXAMPLE-001 -->\n", encoding="utf-8")
        errors = module.validate(source_root, index_path)
        require(any("unindexed planning document" in error for error in errors), "unindexed tracker should fail")
        require(any("duplicate work id" in error for error in errors), "duplicate work id should fail")

        index["documents"][1]["state"] = "complete"
        plan.write_text("# Plan\n- [ ] Still open\n", encoding="utf-8")
        index_path.write_text(json.dumps(index), encoding="utf-8")
        errors = module.validate(source_root, index_path)
        require(
            any("completed document still contains unchecked items" in error for error in errors),
            "false completion should fail",
        )

    print("checklist-index self-test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
