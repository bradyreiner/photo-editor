#!/usr/bin/env python3
"""Stage a pinned app-private Ollama runtime; never install models or services."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import tarfile
import tempfile
import urllib.request

LOCK = Path(__file__).resolve().parents[1] / "apps/desktop/models/agent-runtime.lock.json"
RECEIPT = "framehouse-runtime.json"


def sha256(path):
    result = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def inventory(root):
    files = {}
    for path in sorted(root.rglob("*")):
        relative = path.relative_to(root).as_posix()
        if relative == RECEIPT:
            continue
        if path.is_symlink():
            # Includes dangling links and chains that escape the staged root.
            path.resolve(strict=True).relative_to(root.resolve())
            files[relative] = {"link": os.readlink(path)}
        elif path.is_file():
            files[relative] = {"sha256": sha256(path), "bytes": path.stat().st_size}
        elif not path.is_dir():
            raise ValueError(f"unsupported runtime entry: {relative}")
    return files


def verify(root, lock):
    receipt = json.loads((root / RECEIPT).read_text())
    files = inventory(root)
    canonical = (json.dumps(files, sort_keys=True, separators=(",", ":")) + "\n").encode()
    if hashlib.sha256(canonical).hexdigest() != lock["fileInventorySha256"]:
        raise ValueError("runtime file inventory differs from the pinned release")
    if receipt.get("schema") != "framehouse-agent-runtime-install-v1" or receipt.get("lock") != lock or receipt.get("files") != files:
        raise ValueError("runtime contents differ from the verified installation receipt")
    for name in ("ollama", "llama-server"):
        if not os.access(root / name, os.X_OK):
            raise ValueError(f"missing executable: {name}")
    if sha256(root / "OLLAMA_LICENSE") != lock["licenseSha256"]:
        raise ValueError("runtime license integrity check failed")
    return receipt


def fetch(url, target):
    if not url.startswith("https://"):
        raise ValueError("downloads require HTTPS")
    with urllib.request.urlopen(url, timeout=30) as response, target.open("xb") as output:
        if not response.url.startswith("https://"):
            raise ValueError("insecure download redirect")
        shutil.copyfileobj(response, output, 1024 * 1024)


def stage(archive, license_path, output, lock):
    if output.exists() or output.is_symlink():
        raise ValueError("output already exists; verify it or select a new staging directory")
    if archive.stat().st_size != lock["archive"]["bytes"] or sha256(archive) != lock["archive"]["sha256"]:
        raise ValueError("runtime archive checksum or size mismatch")
    if sha256(license_path) != lock["licenseSha256"]:
        raise ValueError("runtime license checksum mismatch")
    output.parent.mkdir(parents=True, exist_ok=True)
    # Build and verify in a new sibling; an incomplete extraction is never published.
    with tempfile.TemporaryDirectory(prefix=".agent-runtime-stage-", dir=output.parent) as temp:
        staged = Path(temp) / "runtime"
        staged.mkdir()
        with tarfile.open(archive, "r:gz") as source:
            members = source.getmembers()
            if any(not (item.isfile() or item.isdir() or item.issym()) for item in members):
                raise ValueError("archive contains unsupported special files or hard links")
            if sum(item.size for item in members) > 2 * 1024**3:
                raise ValueError("runtime archive exceeds the extraction budget")
            source.extractall(staged, members=members, filter="data")
        shutil.copyfile(license_path, staged / "OLLAMA_LICENSE")
        receipt = {"schema": "framehouse-agent-runtime-install-v1", "lock": lock,
                   "files": inventory(staged)}
        (staged / RECEIPT).write_text(json.dumps(receipt, indent=2) + "\n")
        verify(staged, lock)
        staged.rename(output)
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--archive", type=Path)
    parser.add_argument("--license-file", type=Path)
    parser.add_argument("--download", action="store_true")
    parser.add_argument("--verify-only", action="store_true")
    args = parser.parse_args()
    lock = json.loads(LOCK.read_text())
    if platform.system() != lock["platform"]:
        parser.error("this pinned runtime currently targets macOS only")
    try:
        if args.verify_only:
            receipt = verify(args.output, lock)
        elif args.download:
            if args.archive or args.license_file:
                raise ValueError("choose download or local archive inputs, not both")
            with tempfile.TemporaryDirectory(prefix="framehouse-runtime-download-") as temp:
                archive, license_path = Path(temp) / "runtime.tgz", Path(temp) / "LICENSE"
                fetch(lock["archive"]["url"], archive)
                fetch(lock["licenseUrl"], license_path)
                receipt = stage(archive, license_path, args.output, lock)
        else:
            if not args.archive or not args.license_file:
                raise ValueError("provide --archive and --license-file, or --download")
            receipt = stage(args.archive, args.license_file, args.output, lock)
        print(json.dumps({"ok": True, "version": lock["version"],
                          "runtimeRoot": str(args.output.resolve()),
                          "verifiedFileCount": len(receipt["files"]), "modelsInstalled": False}))
    except (OSError, ValueError, KeyError, tarfile.TarError) as error:
        parser.exit(1, f"runtime preparation failed: {error}\n")


if __name__ == "__main__":
    main()
