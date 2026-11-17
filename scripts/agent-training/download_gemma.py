#!/usr/bin/env python3
"""Download/verify the single revision pinned by the local Gemma editing experiment."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lock", type=Path, default=Path("training/gemma4-editing/model.lock.json"))
    parser.add_argument("--output", type=Path, default=Path(".photo-editor/agent-training/base/gemma4-26b-a4b-4bit"))
    parser.add_argument("--verify-only", action="store_true")
    args = parser.parse_args()
    lock = json.loads(args.lock.read_text())
    args.output.mkdir(parents=True, exist_ok=True)
    if not args.verify_only:
        needed = sum(f["bytes"] for f in lock["files"] if not (args.output / f["path"]).exists())
        if shutil.disk_usage(args.output).free < needed + 8 * 1024**3:
            raise SystemExit("Insufficient free disk for the pinned checkpoint plus an 8 GiB reserve.")
        os.environ.setdefault("HF_HOME", str(args.output.resolve().parents[1] / "hf-cache"))
        os.environ["HF_HUB_DISABLE_TELEMETRY"] = "1"
        from huggingface_hub import snapshot_download
        snapshot_download(lock["repository"], revision=lock["revision"], local_dir=args.output,
                          allow_patterns=[f["path"] for f in lock["files"]], max_workers=2)
    for item in lock["files"]:
        path = args.output / item["path"]
        if not path.is_file() or path.stat().st_size != item["bytes"]:
            raise SystemExit(f"Missing or wrong-size checkpoint file: {item['path']}")
        digest = hashlib.sha256()
        with path.open("rb") as stream:
            for chunk in iter(lambda: stream.read(8 * 1024**2), b""):
                digest.update(chunk)
        if digest.hexdigest() != item["sha256"]:
            raise SystemExit(f"Checkpoint hash mismatch: {item['path']}")
        print(f"verified {item['path']}", flush=True)


if __name__ == "__main__":
    main()
