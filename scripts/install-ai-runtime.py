#!/usr/bin/env python3
"""Install and verify Looksy's checksum-pinned local AI model runtime."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
from pathlib import Path, PurePosixPath
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_LOCK = REPO_ROOT / "apps/desktop/models/production-runtime.lock.json"
DEFAULT_INSTALL_ROOT = REPO_ROOT / ".photo-editor/ai"
LOCK_SCHEMA = "framehouse-ai-runtime-lock-v1"
INPAINT_CONTRACT = "opencv-lama-image-mask-nchw-float32-512-v1"
INPAINT_PATH = "inpaint/lama.onnx"
SELECTION_ARTIFACTS = {
    "sam2-hiera-tiny-image-encoder-fp32-1024-v1":
        "selection/sam2_hiera_tiny_image_encoder.onnx",
    "sam2-hiera-tiny-image-decoder-fp32-1024-v1":
        "selection/sam2_hiera_tiny_image_decoder.onnx",
}
RECEIPT_NAME = ".framehouse-install-receipt.json"


class InstallError(RuntimeError):
    pass


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def required_string(value: Any, label: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise InstallError(f"{label} must be a non-empty string")
    return value.strip()


def required_positive_int(value: Any, label: str) -> int:
    if not isinstance(value, int) or isinstance(value, bool) or value <= 0:
        raise InstallError(f"{label} must be a positive integer")
    return value


def checked_relative(value: Any, label: str) -> Path:
    relative = Path(required_string(value, label))
    if relative.is_absolute() or ".." in relative.parts:
        raise InstallError(f"{label} must stay inside its install root")
    return relative


def load_lock(path: Path) -> dict[str, Any]:
    try:
        lock = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise InstallError(f"unable to read runtime lock {path}: {error}") from error
    if not isinstance(lock, dict) or lock.get("schema") != LOCK_SCHEMA or lock.get("version") != 1:
        raise InstallError(f"runtime lock must use {LOCK_SCHEMA} version 1")
    host = lock.get("supportedHost")
    if not isinstance(host, dict):
        raise InstallError("runtime lock supportedHost must be an object")
    expected_system = required_string(host.get("system"), "supportedHost system")
    expected_machine = required_string(host.get("machine"), "supportedHost machine")
    if platform.system() != expected_system or platform.machine() != expected_machine:
        raise InstallError(
            f"this lock supports {expected_system}/{expected_machine}; "
            f"current host is {platform.system()}/{platform.machine()}"
        )
    bundle = lock.get("modelBundle")
    if not isinstance(bundle, dict) or bundle.get("schema") != "framehouse-ai-model-bundle-v1":
        raise InstallError("runtime lock modelBundle is invalid")
    artifacts = bundle.get("artifacts")
    if not isinstance(artifacts, list) or not artifacts:
        raise InstallError("runtime lock must declare a non-empty production model artifact list")
    artifact_ids: set[str] = set()
    selection_contracts: set[str] = set()
    inpaint_count = 0
    for index, artifact in enumerate(artifacts):
        if not isinstance(artifact, dict):
            raise InstallError(f"model artifact {index} must be an object")
        artifact_id = required_string(artifact.get("id"), f"model artifact {index} id")
        if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,127}", artifact_id):
            raise InstallError(f"model artifact {index} id contains unsupported characters")
        if artifact_id in artifact_ids:
            raise InstallError(f"duplicate model artifact id: {artifact_id}")
        artifact_ids.add(artifact_id)
        role = required_string(artifact.get("role"), f"model artifact {index} role")
        contract = required_string(artifact.get("contract"), f"model artifact {index} contract")
        path = required_string(artifact.get("path"), f"model artifact {index} path")
        if role == "inpaint":
            if contract != INPAINT_CONTRACT or path != INPAINT_PATH:
                raise InstallError(f"inpaint model must use {INPAINT_CONTRACT} at {INPAINT_PATH}")
            inpaint_count += 1
        elif role == "selection":
            if SELECTION_ARTIFACTS.get(contract) != path:
                raise InstallError(f"model artifact {index} has an unsupported SAM2 contract or path")
            if contract in selection_contracts:
                raise InstallError(f"duplicate SAM2 selection component: {contract}")
            selection_contracts.add(contract)
        else:
            raise InstallError(f"model artifact {index} role must be inpaint or selection")
    if inpaint_count != 1:
        raise InstallError("runtime lock must declare exactly one LaMa inpaint model")
    if selection_contracts and selection_contracts != set(SELECTION_ARTIFACTS):
        raise InstallError("SAM2 selection requires exactly one encoder and one decoder artifact")
    runtime = lock.get("onnxRuntime")
    if not isinstance(runtime, dict) or not isinstance(runtime.get("archive"), dict):
        raise InstallError("runtime lock onnxRuntime archive is invalid")
    return lock


def verify_pinned_file(path: Path, spec: dict[str, Any], label: str) -> None:
    expected_bytes = required_positive_int(spec.get("bytes"), f"{label} bytes")
    expected_sha256 = required_string(spec.get("sha256"), f"{label} sha256")
    if len(expected_sha256) != 64 or expected_sha256 != expected_sha256.lower():
        raise InstallError(f"{label} sha256 must be 64 lowercase hexadecimal characters")
    if not path.is_file():
        raise InstallError(f"{label} is missing: {path}")
    actual_bytes = path.stat().st_size
    if actual_bytes != expected_bytes:
        raise InstallError(
            f"{label} byte size mismatch: expected {expected_bytes}, got {actual_bytes}"
        )
    actual_sha256 = sha256_file(path)
    if actual_sha256 != expected_sha256:
        raise InstallError(
            f"{label} SHA-256 mismatch: expected {expected_sha256}, got {actual_sha256}"
        )


def download_file(url: str, destination: Path, timeout: float) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_name(f".{destination.name}.part")
    request = urllib.request.Request(url, headers={"User-Agent": "LooksyAIRuntime/1.0"})
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response, temporary.open("wb") as output:
            shutil.copyfileobj(response, output, length=1024 * 1024)
        os.replace(temporary, destination)
    except Exception:
        temporary.unlink(missing_ok=True)
        raise


def ensure_download(
    downloads: Path,
    spec: dict[str, Any],
    file_name: str,
    label: str,
    allow_download: bool,
    timeout: float,
) -> Path:
    destination = downloads / file_name
    try:
        verify_pinned_file(destination, spec, label)
        print(f"reuse verified {label}: {destination}")
        return destination
    except InstallError:
        if not allow_download:
            raise
    url = required_string(spec.get("downloadUrl"), f"{label} downloadUrl")
    print(f"download {label}: {url}")
    download_file(url, destination, timeout)
    verify_pinned_file(destination, spec, label)
    print(f"verified {label}: {destination}")
    return destination


def write_json_atomic(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.tmp")
    temporary.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    os.replace(temporary, path)


def install_model_bundle(
    lock: dict[str, Any], model_downloads: dict[str, Path], bundle_root: Path
) -> None:
    bundle = lock["modelBundle"]
    manifest_artifacts = []
    for artifact in bundle["artifacts"]:
        artifact_id = required_string(artifact.get("id"), "model artifact id")
        model_download = model_downloads.get(artifact_id)
        if model_download is None:
            raise InstallError(f"downloaded model artifact is missing: {artifact_id}")
        relative = checked_relative(artifact.get("path"), f"{artifact_id} path")
        destination = bundle_root / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        temporary = destination.with_name(f".{destination.name}.tmp")
        shutil.copyfile(model_download, temporary)
        verify_pinned_file(temporary, artifact, f"installed model {artifact_id}")
        os.replace(temporary, destination)
        manifest_artifacts.append(
            {
                key: value
                for key, value in artifact.items()
                if key not in {"downloadUrl", "upstreamArchitectureUrl"}
            }
        )
    write_json_atomic(
        bundle_root / "manifest.json",
        {
            "schema": bundle["schema"],
            "version": bundle["version"],
            "artifacts": manifest_artifacts,
        },
    )


def validate_archive_members(archive: tarfile.TarFile) -> None:
    for member in archive.getmembers():
        path = PurePosixPath(member.name)
        if path.is_absolute() or ".." in path.parts or member.isdev():
            raise InstallError(f"unsafe ONNX Runtime archive member: {member.name}")
        if member.issym() or member.islnk():
            target = PurePosixPath(member.linkname)
            if target.is_absolute() or ".." in target.parts:
                raise InstallError(f"unsafe ONNX Runtime archive link: {member.name}")


def sdk_file_inventory(sdk_root: Path, required_files: list[Any]) -> list[dict[str, Any]]:
    inventory: list[dict[str, Any]] = []
    for index, value in enumerate(required_files):
        relative = checked_relative(value, f"requiredFiles {index}")
        path = sdk_root / relative
        if not path.is_file():
            raise InstallError(f"ONNX Runtime required file is missing: {path}")
        inventory.append(
            {
                "path": relative.as_posix(),
                "bytes": path.stat().st_size,
                "sha256": sha256_file(path),
            }
        )
    return inventory


def verify_sdk(runtime: dict[str, Any], sdk_root: Path) -> None:
    receipt_path = sdk_root / RECEIPT_NAME
    try:
        receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise InstallError(f"ONNX Runtime install receipt is missing or invalid: {error}") from error
    archive = runtime["archive"]
    if (
        receipt.get("schema") != "framehouse-onnxruntime-install-v1"
        or receipt.get("version") != runtime.get("version")
        or receipt.get("archiveSha256") != archive.get("sha256")
    ):
        raise InstallError("ONNX Runtime install receipt does not match the production lock")
    version_path = sdk_root / "VERSION_NUMBER"
    if not version_path.is_file() or version_path.read_text(encoding="utf-8").strip() != runtime.get("version"):
        raise InstallError("ONNX Runtime VERSION_NUMBER does not match the production lock")
    expected_inventory = receipt.get("files")
    actual_inventory = sdk_file_inventory(sdk_root, runtime.get("requiredFiles", []))
    if expected_inventory != actual_inventory:
        raise InstallError("ONNX Runtime installed files do not match their install receipt")


def install_sdk(lock: dict[str, Any], archive_path: Path, install_root: Path) -> Path:
    runtime = lock["onnxRuntime"]
    sdk_root = install_root / "onnxruntime"
    if sdk_root.exists():
        verify_sdk(runtime, sdk_root)
        print(f"reuse verified ONNX Runtime SDK: {sdk_root}")
        return sdk_root
    with tempfile.TemporaryDirectory(prefix="framehouse-onnxruntime-", dir=install_root) as temporary:
        extraction_root = Path(temporary)
        with tarfile.open(archive_path, "r:gz") as archive:
            validate_archive_members(archive)
            archive.extractall(extraction_root)
        extracted_sdk = extraction_root / required_string(runtime.get("archiveRoot"), "archiveRoot")
        inventory = sdk_file_inventory(extracted_sdk, runtime.get("requiredFiles", []))
        receipt = {
            "schema": "framehouse-onnxruntime-install-v1",
            "version": runtime["version"],
            "platform": runtime["platform"],
            "archiveSha256": runtime["archive"]["sha256"],
            "sourceUrl": runtime["archive"]["sourceUrl"],
            "files": inventory,
        }
        write_json_atomic(extracted_sdk / RECEIPT_NAME, receipt)
        extracted_sdk.rename(sdk_root)
    verify_sdk(runtime, sdk_root)
    print(f"installed verified ONNX Runtime SDK: {sdk_root}")
    return sdk_root


def verify_model_bundle(bundle_root: Path) -> None:
    verifier = REPO_ROOT / "scripts/verify-ai-model-bundle.py"
    result = subprocess.run(
        [sys.executable, "-B", str(verifier), str(bundle_root), "--require-inpaint"],
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip()
        raise InstallError(f"installed model bundle verification failed: {detail}")


def verify_install(lock: dict[str, Any], install_root: Path) -> tuple[Path, Path]:
    bundle_root = install_root / "model-bundle"
    sdk_root = install_root / "onnxruntime"
    verify_model_bundle(bundle_root)
    verify_sdk(lock["onnxRuntime"], sdk_root)
    return bundle_root, sdk_root


def install(lock: dict[str, Any], install_root: Path, allow_download: bool, timeout: float) -> tuple[Path, Path]:
    install_root.mkdir(parents=True, exist_ok=True)
    downloads = install_root / "downloads"
    model_downloads = {}
    for artifact in lock["modelBundle"]["artifacts"]:
        artifact_id = required_string(artifact.get("id"), "model artifact id")
        download_url = required_string(artifact.get("downloadUrl"), f"{artifact_id} downloadUrl")
        file_name = Path(download_url.split("?", maxsplit=1)[0]).name
        if not file_name:
            raise InstallError(f"{artifact_id} downloadUrl must end with a filename")
        model_name = f"{artifact_id}-{file_name}"
        model_downloads[artifact_id] = ensure_download(
            downloads, artifact, model_name, artifact_id, allow_download, timeout
        )
    runtime = lock["onnxRuntime"]
    archive = runtime["archive"]
    archive_download = ensure_download(
        downloads,
        archive,
        required_string(archive.get("fileName"), "ONNX Runtime archive fileName"),
        "ONNX Runtime archive",
        allow_download,
        timeout,
    )
    install_model_bundle(lock, model_downloads, install_root / "model-bundle")
    install_sdk(lock, archive_download, install_root)
    return verify_install(lock, install_root)


def run_self_test() -> None:
    with tempfile.TemporaryDirectory(prefix="framehouse-ai-runtime-self-test-") as temporary:
        root = Path(temporary)
        model_bytes = b"pinned-lama-self-test-model"
        encoder_bytes = b"pinned-sam2-encoder-self-test"
        decoder_bytes = b"pinned-sam2-decoder-self-test"
        archive_root = "onnxruntime-osx-arm64-test"
        source_sdk = root / "source" / archive_root
        required_files = [
            "VERSION_NUMBER",
            "LICENSE",
            "ThirdPartyNotices.txt",
            "include/onnxruntime_cxx_api.h",
            "include/coreml_provider_factory.h",
            "lib/libonnxruntime.test.dylib",
        ]
        for relative in required_files:
            path = source_sdk / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"test-version\n" if relative == "VERSION_NUMBER" else relative.encode())
        downloads = root / "install" / "downloads"
        downloads.mkdir(parents=True)
        model_download = downloads / "lama-test.onnx"
        model_download.write_bytes(model_bytes)
        encoder_download = downloads / "encoder-test.onnx"
        decoder_download = downloads / "decoder-test.onnx"
        encoder_download.write_bytes(encoder_bytes)
        decoder_download.write_bytes(decoder_bytes)
        archive_download = downloads / "onnxruntime-test.tgz"
        with tarfile.open(archive_download, "w:gz") as archive:
            archive.add(source_sdk, arcname=archive_root)
        lock = {
            "schema": LOCK_SCHEMA,
            "version": 1,
            "supportedHost": {"system": platform.system(), "machine": platform.machine()},
            "modelBundle": {
                "schema": "framehouse-ai-model-bundle-v1",
                "version": 1,
                "artifacts": [
                    {
                        "id": "self-test-lama",
                        "role": "inpaint",
                        "contract": INPAINT_CONTRACT,
                        "path": "inpaint/lama.onnx",
                        "bytes": len(model_bytes),
                        "sha256": hashlib.sha256(model_bytes).hexdigest(),
                        "downloadUrl": model_download.as_uri(),
                        "sourceUrl": "https://example.invalid/model",
                        "license": {"name": "test", "url": "https://example.invalid/license"},
                    },
                    {
                        "id": "self-test-sam2-encoder",
                        "role": "selection",
                        "contract": "sam2-hiera-tiny-image-encoder-fp32-1024-v1",
                        "path": SELECTION_ARTIFACTS[
                            "sam2-hiera-tiny-image-encoder-fp32-1024-v1"
                        ],
                        "bytes": len(encoder_bytes),
                        "sha256": hashlib.sha256(encoder_bytes).hexdigest(),
                        "downloadUrl": encoder_download.as_uri(),
                        "sourceUrl": "https://example.invalid/sam2-encoder.onnx",
                        "license": {"name": "test", "url": "https://example.invalid/license"},
                    },
                    {
                        "id": "self-test-sam2-decoder",
                        "role": "selection",
                        "contract": "sam2-hiera-tiny-image-decoder-fp32-1024-v1",
                        "path": SELECTION_ARTIFACTS[
                            "sam2-hiera-tiny-image-decoder-fp32-1024-v1"
                        ],
                        "bytes": len(decoder_bytes),
                        "sha256": hashlib.sha256(decoder_bytes).hexdigest(),
                        "downloadUrl": decoder_download.as_uri(),
                        "sourceUrl": "https://example.invalid/sam2-decoder.onnx",
                        "license": {"name": "test", "url": "https://example.invalid/license"},
                    },
                ],
            },
            "onnxRuntime": {
                "version": "test-version",
                "platform": "test-platform",
                "archive": {
                    "fileName": archive_download.name,
                    "bytes": archive_download.stat().st_size,
                    "sha256": sha256_file(archive_download),
                    "downloadUrl": archive_download.as_uri(),
                    "sourceUrl": "https://example.invalid/runtime",
                },
                "archiveRoot": archive_root,
                "requiredFiles": required_files,
            },
        }
        lock_path = root / "runtime-lock.json"
        lock_path.write_text(json.dumps(lock), encoding="utf-8")
        lock = load_lock(lock_path)
        partial_lock = json.loads(json.dumps(lock))
        partial_lock["modelBundle"]["artifacts"].pop()
        partial_lock_path = root / "partial-runtime-lock.json"
        partial_lock_path.write_text(json.dumps(partial_lock), encoding="utf-8")
        try:
            load_lock(partial_lock_path)
        except InstallError as error:
            if "exactly one encoder and one decoder" not in str(error):
                raise
        else:
            raise InstallError("runtime lock loader accepted a partial SAM2 model pair")
        install_root = root / "install"
        install(lock, install_root, allow_download=True, timeout=1.0)
        verify_install(lock, install_root)
        (install_root / "model-bundle/inpaint/lama.onnx").write_bytes(b"corrupted")
        try:
            verify_install(lock, install_root)
        except InstallError as error:
            if "bundle verification failed" not in str(error):
                raise
        else:
            raise InstallError("self-test accepted a corrupted installed model")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lock", type=Path, default=DEFAULT_LOCK)
    parser.add_argument("--install-root", type=Path, default=DEFAULT_INSTALL_ROOT)
    parser.add_argument("--download", action="store_true", help="Download missing pinned artifacts.")
    parser.add_argument("--verify-only", action="store_true")
    parser.add_argument("--timeout", type=float, default=120.0)
    parser.add_argument("--self-test", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        if args.self_test:
            run_self_test()
            print("AI runtime installer self-test passed")
            return 0
        lock = load_lock(args.lock.expanduser().resolve())
        install_root = args.install_root.expanduser().resolve()
        if args.verify_only:
            bundle_root, sdk_root = verify_install(lock, install_root)
        else:
            bundle_root, sdk_root = install(lock, install_root, args.download, args.timeout)
    except (InstallError, OSError, tarfile.TarError) as error:
        print(f"AI runtime install failed: {error}", file=sys.stderr)
        return 1
    print("AI runtime verified")
    print(f"model_bundle={bundle_root}")
    print(f"onnxruntime_sdk={sdk_root}")
    print(
        "configure: cmake --preset dev-macos-debug "
        f"-DPHOTO_EDITOR_ENABLE_AI_INFERENCE=ON "
        f"-DPHOTO_EDITOR_ONNXRUNTIME_ROOT={sdk_root} "
        "-DPHOTO_EDITOR_ONNXRUNTIME_ENABLE_COREML=OFF "
        f"-DPHOTO_EDITOR_BUNDLED_AI_MODELS_DIR={bundle_root}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
