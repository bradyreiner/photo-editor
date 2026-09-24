#!/usr/bin/env python3
"""Explicit opt-in installer/verifier for the pinned SigLIP 2 retrieval model.

No package installation, remote code execution, implicit hub lookup or photo I/O.
Upstream revision and file identities are from Google's Hugging Face repository.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

MODEL = "google/siglip2-base-patch16-224"
REVISION = "75de2d55ec2d0b4efc50b3e9ad70dba96a7b2fa2"
# Small files are pinned by their Git blob object, large files by LFS SHA-256.
FILES = {
    "README.md": (3375, "git-sha1", "51f163598c3995137b033afdde373bd25b0c6821"),
    "config.json": (253, "git-sha1", "c8cd2a20e58a738f44f267ae19f9568ff1095698"),
    "model.safetensors": (1500800904, "sha256", "612923381c76ec5a9bed335d1c48827e3f2e506ac31b044b63b2031fadee6a0b"),
    "preprocessor_config.json": (394, "git-sha1", "2e52d8e8492b5c496ae04c37bfa09760469fb18b"),
    "special_tokens_map.json": (636, "git-sha1", "8d6368f7e735fbe4781bf6e956b7c6ad0586df80"),
    "tokenizer.json": (34363039, "sha256", "cb9140fae3ac5122c972d37adf83e1248471a38147ad76f8215c8872c6fd8322"),
    "tokenizer.model": (4241003, "sha256", "61a7b147390c64585d6c3543dd6fc636906c9af3865a5548f27f31aee1d4c8e2"),
    "tokenizer_config.json": (47164, "git-sha1", "d97c5412159422c3b56fbc99076b1dcc25dd7856"),
}


def verify_file(path: Path, spec: tuple[int, str, str]) -> str:
    size, algorithm, expected = spec
    if path.is_symlink() or not path.is_file() or path.stat().st_size != size:
        raise ValueError(f"Missing, symlinked or wrong-sized model artifact: {path.name}")
    digest = hashlib.sha256() if algorithm == "sha256" else hashlib.sha1()
    sha256 = hashlib.sha256()
    if algorithm == "git-sha1":
        digest.update(f"blob {size}\0".encode())
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
            sha256.update(block)
    if digest.hexdigest() != expected:
        raise ValueError(f"Pinned artifact checksum mismatch: {path.name}")
    return sha256.hexdigest()


def verify_model(root: Path) -> dict:
    if not root.is_absolute() or root.is_symlink() or not root.is_dir():
        raise ValueError("Model root must be an absolute non-symlink directory")
    inventory = {name: {"bytes": spec[0], "sha256": verify_file(root / name, spec)}
                 for name, spec in FILES.items()}
    return {"model": MODEL, "revision": REVISION, "files": inventory}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--download", action="store_true", help="Explicitly authorize fetching missing pinned artifacts")
    args = parser.parse_args()
    root = args.root
    if not root.is_absolute() or root.is_symlink():
        parser.error("--root must be an absolute non-symlink directory")
    if args.download:
        root.mkdir(parents=True, exist_ok=True, mode=0o700)
        for name, spec in FILES.items():
            destination = root / name
            # Never overwrite a pre-existing corrupt/unverified file or symlink.
            if destination.exists() or destination.is_symlink():
                verify_file(destination, spec)
                print(f"reuse verified {name}", flush=True)
                continue
            temporary = root / (name + ".part")
            if temporary.exists() or temporary.is_symlink():
                raise ValueError(f"Partial artifact already exists; inspect it before retry: {temporary}")
            print(f"download {name}: {spec[0]} bytes", flush=True)
            subprocess.run(["curl", "--fail", "--silent", "--show-error", "--location", "--retry", "2",
                            "--proto", "=https", "--proto-redir", "=https", "--connect-timeout", "30", "--max-time", "900",
                            # Redirect response bodies can exceed tiny JSON files;
                            # the final artifact still has an exact size/hash gate.
                            "--max-filesize", str(max(spec[0], 65536)), "--output", str(temporary),
                            f"https://huggingface.co/{MODEL}/resolve/{REVISION}/{name}"], check=True)
            verify_file(temporary, spec)
            temporary.chmod(0o600)
            os.replace(temporary, destination)
            print(f"verified {name}", flush=True)
    print(json.dumps(verify_model(root), indent=2), flush=True)


if __name__ == "__main__":
    main()
