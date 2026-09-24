#!/usr/bin/env python3
"""Validate the single ownership registry for roadmap and checklist documents."""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import defaultdict
from pathlib import Path


TRACKING_NAME = re.compile(r"(?:checklist|roadmap|plan|audit|backlog)", re.IGNORECASE)
WORK_ID = re.compile(r"<!--\s*work-id:\s*([A-Z][A-Z0-9]+(?:-[A-Z0-9]+)+)\s*-->")
DOCUMENT_ID = re.compile(r"DOC-[A-Z0-9]+(?:-[A-Z0-9]+)*")
UNCHECKED_ITEM = re.compile(r"^\s*(?:[-*]|\d+\.)\s+\[ \]", re.MULTILINE)
VALID_STATES = {"active", "supporting", "historical", "parked", "complete"}
EXPLICIT_TRACKERS = {"docs/color-engine-and-app-foundations-completion-strategy.md"}
INDEX_DOCUMENTS = {"docs/checklist-index.md"}


def tracked_documents(source_root: Path) -> set[str]:
    docs_root = source_root / "docs"
    discovered = {
        path.relative_to(source_root).as_posix()
        for path in docs_root.glob("*.md")
        if TRACKING_NAME.search(path.stem)
    }
    return (discovered | EXPLICIT_TRACKERS) - INDEX_DOCUMENTS


def validate(source_root: Path, index_path: Path) -> list[str]:
    errors: list[str] = []
    try:
        document = json.loads(index_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        return [f"cannot read checklist index: {exc}"]
    entries = document.get("documents")
    if document.get("schemaVersion") != 1 or not isinstance(entries, list):
        return ["checklist index must use schemaVersion 1 and a documents array"]

    by_path: dict[str, dict] = {}
    seen_ids: set[str] = set()
    for number, entry in enumerate(entries):
        label = f"documents[{number}]"
        if not isinstance(entry, dict):
            errors.append(f"{label} must be an object")
            continue
        document_id = entry.get("id")
        path = entry.get("path")
        area = entry.get("area")
        state = entry.get("state")
        owner = entry.get("canonicalOwner")
        if not isinstance(document_id, str) or not DOCUMENT_ID.fullmatch(document_id):
            errors.append(f"{label}.id must be a stable DOC-* identifier")
        elif document_id in seen_ids:
            errors.append(f"duplicate document id: {document_id}")
        else:
            seen_ids.add(document_id)
        if not isinstance(path, str) or not path.startswith("docs/") or not path.endswith(".md"):
            errors.append(f"{label}.path must name a Markdown file under docs/")
            continue
        if path in by_path:
            errors.append(f"duplicate document path: {path}")
        by_path[path] = entry
        if not isinstance(area, str) or not area.strip():
            errors.append(f"{label}.area is required")
        if state not in VALID_STATES:
            errors.append(f"{label}.state must be one of {sorted(VALID_STATES)}")
        if not isinstance(owner, str) or not owner.startswith("docs/"):
            errors.append(f"{label}.canonicalOwner is required")
        if not (source_root / path).is_file():
            errors.append(f"indexed document does not exist: {path}")

    discovered = tracked_documents(source_root)
    indexed = set(by_path)
    for path in sorted(discovered - indexed):
        errors.append(f"unindexed planning document: {path}")
    for path in sorted(indexed - discovered):
        errors.append(f"indexed path is outside the planning-document inventory: {path}")

    for path, entry in sorted(by_path.items()):
        owner = entry.get("canonicalOwner")
        owner_entry = by_path.get(owner)
        if owner_entry is None:
            errors.append(f"{path} points to an unindexed canonical owner: {owner}")
            continue
        if owner_entry.get("state") != "active" or owner_entry.get("canonicalOwner") != owner:
            errors.append(f"{path} points to a non-canonical owner: {owner}")
        if entry.get("state") == "active" and owner != path:
            errors.append(f"active document must own its scope: {path}")
        if entry.get("state") != "active" and owner == path:
            errors.append(f"non-active document cannot claim canonical ownership: {path}")

    work_id_locations: dict[str, list[str]] = defaultdict(list)
    for path in sorted(discovered):
        try:
            source = (source_root / path).read_text(encoding="utf-8")
        except (OSError, UnicodeError) as exc:
            errors.append(f"cannot scan work ids in {path}: {exc}")
            continue
        for match in WORK_ID.finditer(source):
            line = source.count("\n", 0, match.start()) + 1
            work_id_locations[match.group(1)].append(f"{path}:{line}")
        if by_path.get(path, {}).get("state") == "complete" and UNCHECKED_ITEM.search(source):
            errors.append(f"completed document still contains unchecked items: {path}")
    for work_id, locations in sorted(work_id_locations.items()):
        if len(locations) > 1:
            errors.append(f"duplicate work id {work_id}: {', '.join(locations)}")

    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--index", type=Path)
    args = parser.parse_args()
    source_root = args.source_root.resolve()
    index_path = (args.index or source_root / "docs/checklist-index.json").resolve()
    errors = validate(source_root, index_path)
    if errors:
        for error in errors:
            print(f"checklist-index: error: {error}", file=sys.stderr)
        return 1
    entry_count = len(json.loads(index_path.read_text(encoding="utf-8"))["documents"])
    print(f"checklist-index: passed documents={entry_count}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
