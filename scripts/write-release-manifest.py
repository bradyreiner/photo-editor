#!/usr/bin/env python3
"""Create a checksummed machine-readable release manifest."""

from __future__ import annotations

import argparse
import hashlib
import json
import mimetypes
from datetime import datetime, timezone
from pathlib import Path


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--artifacts", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--channel", default="alpha")
    parser.add_argument("--base-url", default="")
    args = parser.parse_args()

    assets = []
    for path in sorted(args.artifacts.iterdir()):
        if not path.is_file() or path == args.output or path.name.startswith("."):
            continue
        content_type = mimetypes.guess_type(path.name)[0] or "application/octet-stream"
        entry = {
            "name": path.name,
            "size": path.stat().st_size,
            "sha256": sha256(path),
            "contentType": content_type,
        }
        if args.base_url:
            entry["url"] = f"{args.base_url.rstrip('/')}/{path.name}"
        assets.append(entry)

    document = {
        "schemaVersion": 1,
        "product": "Looksy",
        "version": args.version,
        "channel": args.channel,
        "publishedAt": datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
        "assets": assets,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
