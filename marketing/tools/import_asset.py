#!/usr/bin/env python3
"""Stage a media file and safely update one Looksy campaign asset record."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import stat
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any

try:
    from PIL import Image
except ImportError:  # pragma: no cover - exercised only in an incomplete local setup
    Image = None  # type: ignore[assignment,misc]


MARKETING_ROOT = Path(__file__).resolve().parents[1]
VALID_KINDS = {"image", "video", "audio", "capture"}
VALID_MEDIA_TYPES = {"image", "video"}
RASTER_FORMATS = {
    ".bmp": "BMP",
    ".gif": "GIF",
    ".jpeg": "JPEG",
    ".jpg": "JPEG",
    ".png": "PNG",
    ".tif": "TIFF",
    ".tiff": "TIFF",
    ".webp": "WEBP",
}
VIDEO_EXTENSIONS = {".avi", ".m2ts", ".mkv", ".mov", ".mp4", ".m4v", ".mxf", ".mts", ".mpeg", ".mpg", ".ts", ".webm"}
AUDIO_EXTENSIONS = {".aac", ".aif", ".aiff", ".caf", ".flac", ".m4a", ".mp3", ".ogg", ".opus", ".wav", ".wma"}


class IntakeError(Exception):
    """A user-correctable intake, manifest, or media validation error."""


@dataclass
class ImportPlan:
    manifest_path: Path
    manifest: dict[str, Any]
    asset: dict[str, Any]
    asset_id: str
    source_path: Path
    destination_path: Path
    destination_relative: str
    sha256: str
    kind: str
    media_type: str | None
    bytes_changed: bool
    old_hash: str | None
    old_integrity_mismatch: bool
    destination_reused: bool
    note: str | None


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as handle:
            for block in iter(lambda: handle.read(1024 * 1024), b""):
                digest.update(block)
    except OSError as exc:
        raise IntakeError(f"cannot read {path}: {exc}") from exc
    return digest.hexdigest()


def _is_within(child: Path, parent: Path) -> bool:
    try:
        child.relative_to(parent)
        return True
    except ValueError:
        return False


def _manifest_path(path: Path, root: Path) -> Path:
    try:
        resolved = path.expanduser().resolve(strict=True)
    except OSError as exc:
        raise IntakeError(f"cannot resolve manifest {path}: {exc}") from exc
    root = root.resolve()
    if not _is_within(resolved, root):
        raise IntakeError("manifest must be inside the marketing directory")
    if not resolved.is_file():
        raise IntakeError(f"manifest is not a file: {resolved}")
    return resolved


def _marketing_file(root: Path, value: Any, label: str, *, required: bool) -> Path | None:
    if not isinstance(value, str) or not value.strip():
        if required:
            raise IntakeError(f"{label} must be a non-empty marketing-relative path")
        return None
    candidate = Path(value)
    if candidate.is_absolute():
        raise IntakeError(f"{label} must be relative to the marketing directory")
    resolved = (root / candidate).resolve()
    if not _is_within(resolved, root.resolve()):
        raise IntakeError(f"{label} escapes the marketing directory: {value}")
    return resolved


def _load_manifest(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except OSError as exc:
        raise IntakeError(f"cannot read manifest {path}: {exc}") from exc
    except json.JSONDecodeError as exc:
        raise IntakeError(f"invalid JSON in {path}:{exc.lineno}:{exc.colno}: {exc.msg}") from exc
    if not isinstance(value, dict):
        raise IntakeError("manifest root must be a JSON object")
    if not isinstance(value.get("assets"), list):
        raise IntakeError("manifest assets must be an array")
    return value


def _resolve_source(path: Path) -> Path:
    try:
        resolved = path.expanduser().resolve(strict=True)
    except OSError as exc:
        raise IntakeError(f"source file does not exist: {path}") from exc
    if not resolved.is_file():
        raise IntakeError(f"source is not a regular file: {resolved}")
    if resolved.stat().st_size == 0:
        raise IntakeError("source file is empty")
    return resolved


def _probe_stream(path: Path, stream_type: str) -> None:
    ffprobe = shutil.which("ffprobe")
    if ffprobe is None:
        raise IntakeError("ffprobe is required to validate video and audio files; install FFmpeg and retry")
    ffmpeg = shutil.which("ffmpeg")
    if ffmpeg is None:
        raise IntakeError("ffmpeg is required to decode video and audio files; install FFmpeg and retry")
    codec_type = {"v": "video", "a": "audio"}[stream_type]
    command = [
        ffprobe,
        "-v", "error",
        "-select_streams", f"{stream_type}:0",
        "-show_entries", "stream=codec_type",
        "-of", "default=noprint_wrappers=1:nokey=1",
        str(path),
    ]
    try:
        result = subprocess.run(command, check=False, capture_output=True, text=True, timeout=30)
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise IntakeError(f"ffprobe could not inspect {path.name}: {exc}") from exc
    if result.returncode != 0 or codec_type not in result.stdout.split():
        detail = result.stderr.strip().splitlines()
        reason = detail[-1] if detail else "no matching media stream found"
        raise IntakeError(f"source is not a readable {codec_type} file: {reason}")
    decode_command = [
        ffmpeg,
        "-hide_banner", "-v", "error", "-xerror",
        "-i", str(path),
        "-map", f"0:{stream_type}:0",
        "-f", "null", "-",
    ]
    try:
        decoded = subprocess.run(decode_command, check=False, capture_output=True, text=True, timeout=300)
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise IntakeError(f"ffmpeg could not fully decode {path.name}: {exc}") from exc
    if decoded.returncode != 0:
        detail = decoded.stderr.strip().splitlines()
        reason = detail[-1] if detail else "decode failed"
        raise IntakeError(f"source failed a full {codec_type} decode: {reason}")


def validate_media(path: Path, media_type: str) -> None:
    extension = path.suffix.lower()
    if media_type == "image":
        if extension in RASTER_FORMATS:
            if Image is None:
                raise IntakeError("Pillow is required to validate images; install marketing/requirements.txt")
            try:
                with Image.open(path) as image:
                    expected_format = RASTER_FORMATS[extension]
                    if image.format != expected_format:
                        raise IntakeError(
                            f"image extension {extension} does not match detected format {image.format}"
                        )
                    if image.width <= 0 or image.height <= 0 or image.width * image.height > 150_000_000:
                        raise IntakeError("image dimensions are empty or exceed the 150-megapixel intake limit")
                    image.verify()
                with Image.open(path) as image:
                    image.load()
            except IntakeError:
                raise
            except Exception as exc:  # Pillow raises several format-specific exception types.
                raise IntakeError(f"source is not a fully readable image: {exc}") from exc
            return
        raise IntakeError(
            f"unsupported image extension: {extension or '(none)'}; keep camera RAW originals with the capture evidence and use a decoded PNG/JPEG proxy in the campaign"
        )
    if media_type == "video":
        if extension not in VIDEO_EXTENSIONS:
            raise IntakeError(f"unsupported video extension: {extension or '(none)'}")
        _probe_stream(path, "v")
        return
    if media_type == "audio":
        if extension not in AUDIO_EXTENSIONS:
            raise IntakeError(f"unsupported audio extension: {extension or '(none)'}")
        _probe_stream(path, "a")
        return
    raise IntakeError(f"unsupported media type: {media_type}")


def _safe_basename(source: Path) -> str:
    # Replace separators before taking a basename so a Windows-style path is
    # also treated as a filename if supplied on another host.
    raw_name = source.name.replace("\\", "/").rsplit("/", 1)[-1]
    suffix = Path(raw_name).suffix.lower()
    if not re.fullmatch(r"\.[a-z0-9]{1,10}", suffix):
        suffix = ""
    stem = Path(raw_name).stem
    stem = re.sub(r"[^A-Za-z0-9._-]+", "-", stem).strip("._-")
    return f"{stem or 'asset'}{suffix}"


def _incoming_directory(root: Path, *, create: bool) -> Path:
    root = root.resolve()
    media_directory = root / "media"
    if media_directory.is_symlink():
        raise IntakeError("marketing/media must not be a symlink")
    media_resolved = media_directory.resolve()
    if not _is_within(media_resolved, root):
        raise IntakeError("marketing/media escapes the marketing directory")
    if media_resolved.exists() and not media_resolved.is_dir():
        raise IntakeError("marketing/media exists and is not a directory")
    if create:
        media_resolved.mkdir(parents=True, exist_ok=True)

    directory = media_resolved / "incoming"
    if directory.is_symlink():
        raise IntakeError("marketing/media/incoming must not be a symlink")
    if create:
        directory.mkdir(parents=True, exist_ok=True)
    resolved = directory.resolve()
    if not _is_within(resolved, root.resolve()):
        raise IntakeError("incoming media directory escapes the marketing directory")
    if resolved.exists() and not resolved.is_dir():
        raise IntakeError("marketing/media/incoming exists and is not a directory")
    return resolved


def _select_destination(root: Path, digest: str, source: Path) -> tuple[Path, bool]:
    directory = _incoming_directory(root, create=False)
    basename = _safe_basename(source)
    stem = Path(basename).stem
    suffix = Path(basename).suffix
    for length in (12, 16, 24, 32, 48, 64):
        candidate = directory / f"{digest[:length]}-{stem}{suffix}"
        if candidate.is_symlink():
            continue
        if not candidate.exists():
            return candidate, False
        if candidate.is_file() and sha256_file(candidate) == digest:
            return candidate, True
    raise IntakeError("could not choose a collision-free incoming filename; no files were overwritten")


def _old_asset_hash(root: Path, asset: dict[str, Any]) -> tuple[str | None, bool]:
    old_path = _marketing_file(root, asset.get("path"), "asset path", required=True)
    assert old_path is not None
    recorded = asset.get("sha256")
    if not isinstance(recorded, str) or not re.fullmatch(r"[a-fA-F0-9]{64}", recorded):
        recorded = None
    actual = sha256_file(old_path) if old_path.is_file() else None
    mismatch = bool(recorded and actual and recorded.lower() != actual)
    return actual or (recorded.lower() if recorded else None), mismatch


def plan_import(
    manifest_path: Path,
    asset_id: str,
    source_path: Path,
    *,
    kind: str | None = None,
    media_type: str | None = None,
    note: str | None = None,
    root: Path | None = None,
) -> ImportPlan:
    marketing_root = (root or MARKETING_ROOT).resolve()
    resolved_manifest = _manifest_path(manifest_path, marketing_root)
    manifest = _load_manifest(resolved_manifest)
    matches = [item for item in manifest["assets"] if isinstance(item, dict) and item.get("id") == asset_id]
    if not matches:
        raise IntakeError(f"asset id not found in manifest: {asset_id}")
    if len(matches) != 1:
        raise IntakeError(f"asset id is duplicated in manifest: {asset_id}")
    asset = matches[0]

    chosen_kind = kind or asset.get("kind")
    if chosen_kind not in VALID_KINDS:
        raise IntakeError(f"asset kind must be one of: {', '.join(sorted(VALID_KINDS))}")
    if media_type is not None and chosen_kind != "capture":
        raise IntakeError("--media-type is only valid when the asset kind is capture")
    if chosen_kind == "capture":
        chosen_media_type = media_type or asset.get("media_type")
        if chosen_media_type not in VALID_MEDIA_TYPES:
            raise IntakeError("capture assets require --media-type image|video (or an existing media_type)")
    else:
        chosen_media_type = None

    source = _resolve_source(source_path)
    validation_type = chosen_media_type if chosen_kind == "capture" else chosen_kind
    assert validation_type is not None
    validate_media(source, validation_type)
    digest = sha256_file(source)
    destination, destination_reused = _select_destination(marketing_root, digest, source)
    old_hash, old_integrity_mismatch = _old_asset_hash(marketing_root, asset)
    bytes_changed = old_hash != digest or old_integrity_mismatch
    return ImportPlan(
        manifest_path=resolved_manifest,
        manifest=manifest,
        asset=asset,
        asset_id=asset_id,
        source_path=source,
        destination_path=destination,
        destination_relative=destination.relative_to(marketing_root).as_posix(),
        sha256=digest,
        kind=chosen_kind,
        media_type=chosen_media_type,
        bytes_changed=bytes_changed,
        old_hash=old_hash,
        old_integrity_mismatch=old_integrity_mismatch,
        destination_reused=destination_reused,
        note=note,
    )


def _stage_copy(plan: ImportPlan, root: Path) -> None:
    incoming = _incoming_directory(root, create=True)
    if plan.destination_reused:
        return
    target = incoming / plan.destination_path.name
    if target.exists() or target.is_symlink():
        if target.is_file() and not target.is_symlink() and sha256_file(target) == plan.sha256:
            return
        raise IntakeError(f"destination appeared with different bytes; refusing to overwrite: {target}")

    temporary_path: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(prefix=".looksy-import-", dir=incoming, delete=False) as temporary:
            temporary_path = Path(temporary.name)
            with plan.source_path.open("rb") as source:
                shutil.copyfileobj(source, temporary, length=1024 * 1024)
            temporary.flush()
            os.fsync(temporary.fileno())
        if sha256_file(temporary_path) != plan.sha256:
            raise IntakeError("source changed while it was being copied; retry the import")
        try:
            # A hard link publishes the staged bytes atomically and fails if a
            # different process created this name in the meantime.
            os.link(temporary_path, target)
        except FileExistsError as exc:
            raise IntakeError(f"destination appeared during copy; refusing to overwrite: {target}") from exc
    except OSError as exc:
        raise IntakeError(f"could not stage media in {incoming}: {exc}") from exc
    finally:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)


def _write_manifest_atomic(path: Path, value: dict[str, Any]) -> None:
    try:
        mode = stat.S_IMODE(path.stat().st_mode)
        temporary_path: Path | None = None
        try:
            with tempfile.NamedTemporaryFile(
                mode="w", encoding="utf-8", newline="\n", prefix=f".{path.name}.", suffix=".tmp",
                dir=path.parent, delete=False,
            ) as temporary:
                temporary_path = Path(temporary.name)
                json.dump(value, temporary, indent=2, ensure_ascii=False)
                temporary.write("\n")
                temporary.flush()
                os.fsync(temporary.fileno())
            os.chmod(temporary_path, mode)
            os.replace(temporary_path, path)
        finally:
            if temporary_path is not None:
                temporary_path.unlink(missing_ok=True)
    except OSError as exc:
        raise IntakeError(f"could not update manifest {path}: {exc}") from exc


def apply_import(plan: ImportPlan, *, root: Path | None = None) -> None:
    marketing_root = (root or MARKETING_ROOT).resolve()
    _stage_copy(plan, marketing_root)

    asset = plan.asset
    asset["path"] = plan.destination_relative
    asset["sha256"] = plan.sha256
    asset["kind"] = plan.kind
    if plan.kind == "capture":
        asset["media_type"] = plan.media_type
    else:
        asset.pop("media_type", None)
    if plan.note is not None and plan.note.strip():
        asset["import_note"] = plan.note
    if plan.bytes_changed:
        asset["rights_status"] = "internal-review"
        asset["proof_status"] = "pending"
        asset["proof_refs"] = []
        asset["rights_refs"] = []
    _write_manifest_atomic(plan.manifest_path, plan.manifest)


def _print_plan(plan: ImportPlan, dry_run: bool) -> None:
    state = "dry run; no files written" if dry_run else "import complete"
    print(state)
    print(f"manifest: {plan.manifest_path}")
    print(f"asset: {plan.asset_id}")
    print(f"source: {plan.source_path}")
    print(f"staged path: {plan.destination_relative}" + (" (existing identical bytes)" if plan.destination_reused else ""))
    print(f"kind: {plan.kind}" + (f" ({plan.media_type})" if plan.media_type else ""))
    print(f"sha256: {plan.sha256}")
    print(f"asset bytes changed: {'yes' if plan.bytes_changed else 'no'}")
    print(f"rights/proof reset: {'yes' if plan.bytes_changed else 'no'}")
    if plan.old_integrity_mismatch:
        print("warning: existing asset bytes did not match its recorded hash; clearance was reset")
    if plan.note is not None and plan.note.strip():
        print(f"import note: {plan.note}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Copy a validated media file into Looksy marketing intake and update one campaign asset."
    )
    parser.add_argument("--manifest", type=Path, required=True, help="campaign JSON manifest")
    parser.add_argument("--asset", required=True, help="existing asset ID in that manifest")
    parser.add_argument("--source", type=Path, required=True, help="local source file to copy")
    parser.add_argument("--kind", choices=sorted(VALID_KINDS), help="asset kind; defaults to the current manifest value")
    parser.add_argument("--media-type", choices=sorted(VALID_MEDIA_TYPES), help="image or video when --kind capture")
    parser.add_argument("--note", help="short import note saved on the selected asset record")
    parser.add_argument("--dry-run", action="store_true", help="validate and print the planned changes without writing")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        plan = plan_import(
            args.manifest,
            args.asset,
            args.source,
            kind=args.kind,
            media_type=args.media_type,
            note=args.note,
        )
        if not args.dry_run:
            apply_import(plan)
        _print_plan(plan, args.dry_run)
        return 0
    except IntakeError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
