#!/usr/bin/env python3
"""Build/verify an offline, relocatable search worker; never fetch model weights."""
from __future__ import annotations

import argparse
import hashlib
import importlib.metadata
import json
import os
from pathlib import Path
import platform
import plistlib
import re
import shutil
import subprocess
import sys
import sysconfig

SOURCE = Path(__file__).resolve().parent
RECEIPT = "framehouse-search-runtime.json"
NAME = "framehouse-search-worker"
SCHEMA = "framehouse-search-runtime-bundle-v1"
SOURCES = ("search_model_worker.py", "search_model_runtime.py", "install-search-model.py")
PINNED = {
    "pyinstaller": "6.22.2", "pyinstaller-hooks-contrib": "2026.7",
    "altgraph": "0.17.5", "macholib": "1.16.4",
    "torch": "2.10.0", "transformers": "4.57.6", "pillow": "12.1.1",
    "numpy": "2.5.3", "tokenizers": "0.22.2", "safetensors": "0.8.0",
    "packaging": "26.3", "setuptools": "84.0.0", "certifi": "2026.7.22",
    "charset-normalizer": "3.5.1", "filelock": "3.32.6", "fsspec": "2026.7.0",
    "hf-xet": "1.6.0", "huggingface_hub": "0.36.2", "idna": "3.19",
    "Jinja2": "3.1.6", "MarkupSafe": "3.0.3", "mpmath": "1.3.0",
    "networkx": "3.6.1", "PyYAML": "6.0.3", "regex": "2026.9.3",
    "requests": "2.34.2", "sympy": "1.14.0", "tqdm": "4.70.0",
    "typing_extensions": "4.16.0", "urllib3": "2.7.0",
}


def sha256(path):
    result = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def inventory(root):
    if not root.is_absolute() or root.is_symlink() or not root.is_dir():
        raise ValueError("Runtime root must be an absolute non-symlink directory")
    files, total = {}, 0
    for path in sorted(root.rglob("*")):
        relative = path.relative_to(root).as_posix()
        if relative == RECEIPT:
            continue
        if path.is_symlink():
            target = os.readlink(path)
            if Path(target).is_absolute():
                raise ValueError("Absolute runtime symlink: " + relative)
            path.resolve(strict=True).relative_to(root.resolve())
            files[relative] = {"link": target}
        elif path.is_file():
            size = path.stat().st_size
            total += size
            if total > 4 * 1024**3:
                raise ValueError("Runtime exceeds the 4 GiB package budget")
            files[relative] = {"bytes": size, "sha256": sha256(path)}
        elif not path.is_dir():
            raise ValueError("Unsupported runtime entry: " + relative)
        if len(files) > 20000:
            raise ValueError("Runtime exceeds file-count budget")
    return files


def verify(root, expected_receipt_sha256=None):
    receipt_path = root / RECEIPT
    if receipt_path.is_symlink() or not receipt_path.is_file() or receipt_path.stat().st_size > 8 * 1024**2:
        raise ValueError("Missing, oversized or symlinked runtime receipt")
    if expected_receipt_sha256 is not None and sha256(receipt_path) != expected_receipt_sha256:
        raise ValueError("Runtime receipt differs from the trusted build identity")
    receipt = json.loads(receipt_path.read_text())
    expected_sources = {name: sha256(SOURCE / name) for name in SOURCES}
    if (receipt.get("schema") != SCHEMA or receipt.get("platform") != "Darwin" or
            receipt.get("architecture") != "arm64" or receipt.get("packages") != PINNED or
            receipt.get("sources") != expected_sources or receipt.get("files") != inventory(root)):
        raise ValueError("Runtime differs from its source, dependency or file inventory contract")
    if not (root / NAME).is_file() or not os.access(root / NAME, os.X_OK):
        raise ValueError("Missing search worker executable")
    return receipt


def native_paths(root):
    binaries = []
    for path in sorted(root.rglob("*")):
        if path.is_symlink() or not path.is_file():
            continue
        with path.open("rb") as stream:
            magic = stream.read(4)
        if magic in (b"\xcf\xfa\xed\xfe", b"\xfe\xed\xfa\xcf", b"\xca\xfe\xba\xbe",
                     b"\xbe\xba\xfe\xca", b"\xca\xfe\xba\xbf", b"\xbf\xba\xfe\xca"):
            binaries.append(path)
    if not binaries:
        raise ValueError("No native search runtime binaries found")
    return binaries


def code_containers(root):
    return sorted((path for path in root.rglob("*") if not path.is_symlink() and
                   path.is_dir() and path.suffix in (".framework", ".app", ".xpc", ".appex")),
                  key=lambda path: (-len(path.parts), str(path)))


def verify_signatures(root, receipt, identity, hardened=False, timestamp=False):
    signing = receipt.get("signing", {})
    # An intermediate ad-hoc outer seal may retain already Developer-ID-signed
    # dependencies. A release seal must use the exact prepared identity/options.
    if identity != "-" and signing.get("identity") != identity:
        raise ValueError("Prepare the search runtime with the release signing identity before building the app")
    if hardened and signing.get("hardenedRuntime") is not True:
        raise ValueError("Search runtime was not prepared with hardened runtime")
    if timestamp and signing.get("timestamp") is not True:
        raise ValueError("Search runtime was not prepared with a trusted timestamp")
    for path in native_paths(root) + code_containers(root):
        subprocess.run(["/usr/bin/codesign", "--verify", "--strict", str(path)], check=True)


def sign_copy(root, expected, output, identity, hardened=False, timestamp=False):
    """Finalize a NEW runtime copy before its digest is compiled into the app."""
    receipt = verify(root, expected)
    if not identity or platform.system() != "Darwin":
        raise ValueError("Runtime signing requires macOS and an explicit signing identity")
    if identity == "-" and (hardened or timestamp):
        raise ValueError("Ad-hoc signing has no Team ID for hardened library validation or trusted timestamp; use a real signing identity")
    if (not output.is_absolute() or output.exists() or output.is_symlink() or
            output.resolve().is_relative_to(root.resolve())):
        raise ValueError("Signing requires a new absolute output outside the source runtime")
    required = sum(entry.get("bytes", 0) for entry in receipt["files"].values()) + 256 * 1024**2
    if shutil.disk_usage(output.parent).free < required:
        raise ValueError("Insufficient space for an independent signed runtime copy")
    shutil.copytree(root, output, symlinks=True)
    verify(output, expected)
    arguments = ["/usr/bin/codesign", "--force", "--sign", identity]
    if hardened:
        arguments.extend(("--options", "runtime"))
    if timestamp:
        arguments.append("--timestamp")
    for path in native_paths(output) + code_containers(output):
        subprocess.run(arguments + [str(path)], check=True)
    receipt["signing"] = {"identity": identity, "hardenedRuntime": hardened, "timestamp": timestamp}
    verify_signatures(output, receipt, identity, hardened, timestamp)
    receipt["files"] = inventory(output)
    (output / RECEIPT).write_text(json.dumps(receipt, sort_keys=True, indent=2) + "\n")
    digest = sha256(output / RECEIPT)
    verify(output, digest)
    return {"runtime": str(output), "receiptSha256": digest, "signing": receipt["signing"]}


def verify_app_signing(app, identity, hardened=False, timestamp=False):
    """Use the same configured digest as the app; never regenerate its inventory."""
    with (app / "Contents/Info.plist").open("rb") as stream:
        expected = plistlib.load(stream).get("LooksySearchRuntimeReceiptSHA256", "")
    root = app / "Contents/Resources/search-runtime"
    if not expected and not root.exists() and not root.is_symlink():
        return {"searchRuntime": False}
    if not isinstance(expected, str) or not re.fullmatch("[0-9a-f]{64}", expected):
        raise ValueError("App is missing its configured search runtime digest; rebuild before signing")
    receipt = verify(root, expected)
    verify_signatures(root, receipt, identity, hardened, timestamp)
    return {"searchRuntime": True, "receiptSha256": expected, "preserved": True}


def audit_macho(root):
    """Read-only linkage audit; clean PATH alone cannot rule out absolute dylibs."""
    binaries, minimum = native_paths(root), (0, 0)
    for path in binaries:
        dependencies = subprocess.check_output(["/usr/bin/otool", "-L", str(path)], text=True)
        for line in dependencies.splitlines()[1:]:
            dependency = line.strip().split(" (", 1)[0]
            if not dependency.startswith(("@rpath/", "@loader_path/", "@executable_path/", "/usr/lib/", "/System/Library/")):
                raise ValueError(f"External/nonrelocatable native dependency in {path.name}: {dependency}")
        commands = subprocess.check_output(["/usr/bin/otool", "-l", str(path)], text=True)
        for rpath in re.findall(r"\s+path (.+) \(offset \d+\)", commands):
            if not rpath.startswith(("@loader_path", "@executable_path", "/usr/lib/", "/System/Library/")):
                raise ValueError(f"External native search path in {path.name}: {rpath}")
        for version in re.findall(r"\bminos ([0-9.]+)", commands):
            minimum = max(minimum, tuple(int(part) for part in version.split(".")))
    return {"nativeFiles": len(binaries), "nonSystemAbsoluteDependencies": 0,
            "highestRecordedMinimumMacOS": ".".join(map(str, minimum))}


def build(output):
    if platform.system() != "Darwin" or platform.machine() != "arm64" or sys.version_info[:2] != (3, 14):
        raise ValueError("This build contract currently supports macOS arm64 / Python 3.14 only")
    actual = {name: importlib.metadata.version(name) for name in PINNED}
    if actual != PINNED:
        raise ValueError("Build dependencies differ from the pinned inference/packaging environment")
    if not output.is_absolute() or output.exists() or output.is_symlink():
        raise ValueError("Choose a new absolute build directory; existing output is never overwritten")
    if shutil.disk_usage(output.parent).free < 8 * 1024**3:
        raise ValueError("Search packaging requires at least 8 GiB free scratch space")
    output.mkdir(mode=0o700)
    environment = os.environ.copy()
    for key in tuple(environment):
        if key.startswith(("PYTHON", "HF_", "TRANSFORMERS_", "TOKENIZERS_", "DYLD_")) or key == "LD_PRELOAD":
            environment.pop(key)
    environment.update(HF_HUB_OFFLINE="1", TRANSFORMERS_OFFLINE="1", HF_HUB_DISABLE_TELEMETRY="1",
                       PYINSTALLER_CONFIG_DIR=str(output / "cache"))
    args = [sys.executable, "-I", "-m", "PyInstaller", "--onedir", "--console",
            "--name", NAME, "--distpath", str(output / "dist"),
            "--workpath", str(output / "work"), "--specpath", str(output / "spec"),
            "--paths", str(SOURCE), "--target-arch", "arm64"]
    # Lazy transformers imports and dependency-version metadata must survive
    # freezing; model weights remain separate and are verified at worker start.
    for package in ("torch", "transformers", "tokenizers", "safetensors"):
        args.extend(("--collect-all", package))
    for package in PINNED:
        if package not in ("pyinstaller", "pyinstaller-hooks-contrib", "altgraph", "macholib"):
            args.extend(("--copy-metadata", package))
    args.extend(("--add-data", str(SOURCE / "install-search-model.py") + ":."))
    python_license = Path(sysconfig.get_path("stdlib")) / "LICENSE.txt"
    distribution = importlib.metadata.distribution("pyinstaller")
    licenses = [distribution.locate_file(path) for path in distribution.files
                if str(path).endswith(".dist-info/licenses/COPYING.txt")]
    if not python_license.is_file() or len(licenses) != 1:
        raise ValueError("Python/PyInstaller redistribution notices are missing")
    args.extend(("--add-data", str(python_license) + ":licenses/python",
                 "--add-data", str(licenses[0]) + ":licenses/pyinstaller"))
    args.append(str(SOURCE / "search_model_worker.py"))
    subprocess.run(args, env=environment, check=True)
    root = output / "dist" / NAME
    receipt = {"schema": SCHEMA, "platform": "Darwin", "architecture": "arm64",
               "python": sys.version, "packages": PINNED,
               "sources": {name: sha256(SOURCE / name) for name in SOURCES}, "files": inventory(root)}
    (root / RECEIPT).write_text(json.dumps(receipt, sort_keys=True, indent=2) + "\n")
    verify(root)
    return {"runtime": str(root), "receiptSha256": sha256(root / RECEIPT),
            "bytes": sum(value.get("bytes", 0) for value in receipt["files"].values())}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--build", type=Path, help="New scratch/build directory")
    mode.add_argument("--verify", type=Path, help="Existing runtime bundle directory")
    mode.add_argument("--audit-macho", type=Path, help="Read-only native linkage/deployment audit")
    mode.add_argument("--sign-copy", type=Path, help="Verified source runtime to sign in a new copy")
    mode.add_argument("--verify-app-signing", type=Path, help="App whose prepared search runtime must be preserved")
    parser.add_argument("--receipt-sha256", help="Trusted build receipt digest, required for verification")
    parser.add_argument("--output", type=Path, help="New signed runtime output; never an existing app")
    parser.add_argument("--identity", help="Exact codesign identity, or '-' for local ad-hoc proof")
    parser.add_argument("--hardened-runtime", action="store_true")
    parser.add_argument("--timestamp", action="store_true")
    args = parser.parse_args()
    if (args.verify or args.sign_copy) and not args.receipt_sha256:
        parser.error("--verify/--sign-copy requires --receipt-sha256 from the trusted build output")
    if (args.sign_copy or args.verify_app_signing) and not args.identity:
        parser.error("Signing verification requires an explicit --identity")
    if args.sign_copy and not args.output:
        parser.error("--sign-copy requires a new --output directory")
    if args.sign_copy:
        result = sign_copy(args.sign_copy, args.receipt_sha256, args.output, args.identity,
                           args.hardened_runtime, args.timestamp)
    elif args.verify_app_signing:
        result = verify_app_signing(args.verify_app_signing, args.identity, args.hardened_runtime, args.timestamp)
    elif args.audit_macho:
        result = audit_macho(args.audit_macho)
    else:
        result = build(args.build) if args.build else verify(args.verify, args.receipt_sha256)
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
