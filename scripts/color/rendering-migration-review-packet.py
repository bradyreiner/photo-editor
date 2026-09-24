#!/usr/bin/env python3
"""Build and validate an exact-binary rendering-migration review packet."""

from __future__ import annotations

import argparse
import hashlib
import html
import json
import os
import re
import shutil
import sys
import tempfile
import zipfile
from datetime import datetime
from pathlib import Path, PurePosixPath
from typing import Any

from rendering_migration_report_identity import validate_report_identity


PACKET_CONTRACT = "photo-editor-rendering-migration-review-packet:v1"
RATINGS_CONTRACT = "photo-editor-rendering-migration-review-ratings:v1"
REVIEW_UI_CONTRACT = "photo-editor-rendering-migration-review-ui:v2"
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")
DECISIONS = {"pending", "approve", "revise", "reject"}
SCORE_FIELDS = (
    "colorFidelity",
    "highlightDetail",
    "shadowDetail",
    "stackIntegrity",
    "previewExportMatch",
)
REVIEW_ARTIFACT_ROLES = {
    "reference",
    "candidate",
    "heatmap",
    "referenceP3",
    "candidateP3",
}
ARCHIVE_MAX_FILES = 256
ARCHIVE_MAX_UNCOMPRESSED_BYTES = 4 * 1024 * 1024 * 1024
ARCHIVE_TIMESTAMP = (1980, 1, 1, 0, 0, 0)


def fail(message: str) -> int:
    print(f"error={message}", file=sys.stderr)
    return 1


def load_object(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        value = json.load(handle)
    if not isinstance(value, dict):
        raise ValueError(f"{path}: root must be an object")
    return value


def write_json(path: Path, value: dict[str, Any]) -> None:
    path.write_text(
        json.dumps(value, indent=2, sort_keys=True, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )


def canonical_bytes(value: Any) -> bytes:
    return json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=False
    ).encode("utf-8")


def value_sha256(value: Any) -> str:
    return hashlib.sha256(canonical_bytes(value)).hexdigest()


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def artifact_hash(file_name: str) -> str:
    match = re.search(r"-sha256-([0-9a-f]{64})\.[A-Za-z0-9]+$", file_name)
    if match is None:
        raise ValueError(f"artifact filename has no SHA-256 identity: {file_name}")
    return match.group(1)


def safe_relative_path(value: Any) -> Path:
    if not isinstance(value, str) or not value:
        raise ValueError("packet artifact path is invalid")
    relative = PurePosixPath(value)
    if relative.is_absolute() or ".." in relative.parts or "." in relative.parts:
        raise ValueError(f"packet artifact path is unsafe: {value}")
    return Path(*relative.parts)


def packet_relative_files(manifest: dict[str, Any]) -> list[Path]:
    paths = [Path("index.html"), Path("manifest.json"), Path("ratings.json")]
    assets = manifest.get("assets")
    if not isinstance(assets, list):
        raise ValueError("packet assets are missing")
    for asset in assets:
        if not isinstance(asset, dict):
            raise ValueError("packet asset is invalid")
        artifacts = asset.get("artifacts")
        if not isinstance(artifacts, dict) or set(artifacts) != REVIEW_ARTIFACT_ROLES:
            raise ValueError(f"{asset.get('assetId')}: packet artifacts are invalid")
        for artifact in artifacts.values():
            if not isinstance(artifact, dict):
                raise ValueError(f"{asset.get('assetId')}: packet artifact is invalid")
            paths.append(safe_relative_path(artifact.get("path")))
    names = [path.as_posix() for path in paths]
    if len(names) != len(set(names)):
        raise ValueError("packet contains duplicate file paths")
    return sorted(paths, key=lambda path: path.as_posix())


def materialize(source: Path, destination: Path, expected_hash: str) -> None:
    if not source.is_file() or file_sha256(source) != expected_hash:
        raise ValueError(f"artifact identity mismatch: {source}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.exists():
        if file_sha256(destination) != expected_hash:
            raise ValueError(f"existing packet artifact is different: {destination}")
        return
    try:
        os.link(source, destination)
    except OSError:
        shutil.copy2(source, destination)
    if file_sha256(destination) != expected_hash:
        destination.unlink(missing_ok=True)
        raise ValueError(f"materialized packet artifact is invalid: {destination}")


def selected_artifacts(asset: dict[str, Any]) -> dict[str, str]:
    names = asset.get("evidenceArtifactFileNames")
    if not isinstance(names, list):
        raise ValueError(f"{asset.get('assetId')}: artifact list is missing")
    roles = {
        "reference": "reference-export-sha256-",
        "candidate": "candidate-export-sha256-",
        "heatmap": "delta-e00-heatmap-sha256-",
        "referenceP3": "reference-target-display-p3-simulation-sha256-",
        "candidateP3": "candidate-target-display-p3-simulation-sha256-",
    }
    result: dict[str, str] = {}
    for role, prefix in roles.items():
        matches = [name for name in names if isinstance(name, str) and name.startswith(prefix)]
        if len(matches) != 1:
            raise ValueError(f"{asset.get('assetId')}: expected one {role} artifact")
        result[role] = matches[0]
    return result


def packet_html(manifest: dict[str, Any]) -> str:
    cards: list[str] = []
    for index, asset in enumerate(manifest["assets"], start=1):
        asset_id = html.escape(asset["assetId"])
        basename = html.escape(asset["sourceFileName"])
        risks = ", ".join(html.escape(item) for item in asset["risks"]) or "none"
        metrics = asset["metrics"]
        images = asset["artifacts"]
        score_controls = "".join(
            f'<label>{html.escape(field)} <select data-score="{field}">'
            '<option value="0">Choose</option>'
            + "".join(f'<option value="{score}">{score}</option>' for score in range(1, 6))
            + "</select></label>"
            for field in SCORE_FIELDS
        )
        cards.append(f"""
<section class="card" data-asset="{asset_id}">
  <header><span>{index:02d}</span><div><h2>{basename}</h2><code>{asset_id}</code></div></header>
  <div class="metrics">p95 ΔE00 {metrics['p95DeltaE00']:.3f} · max {metrics['maximumDeltaE00']:.3f} · shadow clips +{metrics['addedShadowClipCount']} · highlight clips +{metrics['addedHighlightClipCount']}</div>
  <div class="compare"><figure><img src="{images['reference']['path']}"><figcaption>Current renderer</figcaption></figure><figure><img src="{images['candidate']['path']}"><figcaption>ACES 2 candidate</figcaption></figure></div>
  <details><summary>Difference and Display P3</summary><div class="compare three"><figure><img src="{images['heatmap']['path']}"><figcaption>ΔE00 heatmap</figcaption></figure><figure><img src="{images['referenceP3']['path']}"><figcaption>Current · P3</figcaption></figure><figure><img src="{images['candidateP3']['path']}"><figcaption>Candidate · P3</figcaption></figure></div></details>
  <p><strong>Review flags:</strong> {risks}</p>
  <div class="scores">{score_controls}</div>
  <div class="decision"><label>Decision <select data-field="decision"><option>pending</option><option>approve</option><option>revise</option><option>reject</option></select></label><label>Notes <textarea data-field="notes" placeholder="Required for a completed decision"></textarea></label></div>
</section>""")
    digest = html.escape(manifest["packetDigestSha256"])
    return f"""<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Rendering migration review</title><style>
:root{{--bg:#111319;--panel:#1b1f29;--ink:#f5f7fb;--muted:#aeb7c7;--line:#343b4a;--accent:#82b7ff}}*{{box-sizing:border-box}}body{{margin:0;background:var(--bg);color:var(--ink);font:15px/1.45 -apple-system,BlinkMacSystemFont,sans-serif}}main{{max-width:1500px;margin:auto;padding:28px}}.intro,.card{{background:var(--panel);border:1px solid var(--line);border-radius:18px;padding:20px;margin-bottom:20px}}h1,h2{{margin:0 0 6px}}code,.metrics{{color:var(--muted)}}header{{display:flex;gap:14px;align-items:center}}header>span{{font-size:28px;color:var(--accent)}}.compare{{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-top:16px}}.three{{grid-template-columns:repeat(3,1fr)}}figure{{margin:0}}img{{display:block;width:100%;max-height:70vh;object-fit:contain;background:#090a0d;border-radius:10px}}figcaption{{padding:7px;color:var(--muted);text-align:center}}details{{margin-top:12px}}label{{display:block;margin-top:10px}}.scores{{display:grid;grid-template-columns:repeat(5,1fr);gap:8px}}select,textarea,input{{width:100%;margin-top:5px;padding:10px;background:#0e1117;color:var(--ink);border:1px solid var(--line);border-radius:8px}}input[type=checkbox]{{width:auto}}textarea{{min-height:72px}}button{{margin-top:12px;padding:11px 16px;border:0;border-radius:9px;background:var(--accent);color:#07101d;font-weight:700}}@media(max-width:800px){{.compare,.three,.scores{{grid-template-columns:1fr}}}}
</style></head><body><main><section class="intro"><h1>Rendering migration review</h1><p>Review current renderer against the exact-binary ACES 2 candidate. Machine flags are context, not approval. Score every quality dimension from 1 to 5 before validation.</p><p><code>packet {digest}</code></p><label>Independent reviewer <input id="reviewer"></label><label><input type="checkbox" id="independent"> I attest that this is an independent human review.</label><button id="download">Download ratings JSON</button></section>{''.join(cards)}</main><script>
const packetDigest={json.dumps(manifest['packetDigestSha256'])};const reportSha={json.dumps(manifest['sourceReportFileSha256'])};document.getElementById('download').onclick=()=>{{const cases={{}};document.querySelectorAll('.card').forEach(card=>{{const scores={{}};card.querySelectorAll('[data-score]').forEach(field=>scores[field.dataset.score]=Number(field.value));cases[card.dataset.asset]={{decision:card.querySelector('[data-field=decision]').value,notes:card.querySelector('[data-field=notes]').value,scores}}}});const data={{contract:{json.dumps(RATINGS_CONTRACT)},packetDigestSha256:packetDigest,sourceReportFileSha256:reportSha,reviewer:document.getElementById('reviewer').value,reviewedAt:new Date().toISOString(),independentReview:document.getElementById('independent').checked,cases}};const a=document.createElement('a');a.href=URL.createObjectURL(new Blob([JSON.stringify(data,null,2)+'\\n'],{{type:'application/json'}}));a.download='ratings.json';a.click()}};
</script></body></html>"""


def build_packet(report_path: Path, executable_path: Path, output: Path) -> int:
    report_path = report_path.resolve()
    executable_path = executable_path.resolve()
    output = output.resolve()
    if output.exists():
        return fail(f"output already exists: {output}")
    report, producer = validate_report_identity(report_path, executable_path)
    source_artifacts = Path(str(report_path) + ".assets")
    packet_assets: list[dict[str, Any]] = []
    assets = report.get("assets")
    if not isinstance(assets, list):
        return fail("report assets are missing")
    output.mkdir(parents=True)
    for asset in assets:
        if not isinstance(asset, dict) or asset.get("evidenceValid") is not True:
            continue
        asset_id = asset.get("assetId")
        if not isinstance(asset_id, str) or not asset_id:
            raise ValueError("review asset id is invalid")
        artifacts: dict[str, dict[str, str]] = {}
        for role, name in selected_artifacts(asset).items():
            digest = artifact_hash(name)
            extension = Path(name).suffix.lower()
            relative = Path("assets") / asset_id / f"{role}{extension}"
            materialize(source_artifacts / name, output / relative, digest)
            artifacts[role] = {"path": relative.as_posix(), "sha256": digest}
        packet_assets.append({
            "assetId": asset_id,
            "sourceFileName": Path(str(asset.get("sourcePath", ""))).name,
            "evidenceSha256": asset.get("evidenceSha256"),
            "risks": asset.get("risks", []),
            "metrics": {
                "p95DeltaE00": float(asset.get("p95DeltaE00", 0.0)),
                "maximumDeltaE00": float(asset.get("maximumDeltaE00", 0.0)),
                "addedShadowClipCount": int(asset.get("addedShadowClipCount", 0)),
                "addedHighlightClipCount": int(asset.get("addedHighlightClipCount", 0)),
            },
            "artifacts": artifacts,
        })
    if not packet_assets:
        return fail("report contains no evidence-ready assets")
    manifest: dict[str, Any] = {
        "contract": PACKET_CONTRACT,
        "sourceReportSha256": report.get("reportSha256"),
        "sourceReportFileSha256": file_sha256(report_path),
        "producer": producer,
        "reviewUiContract": REVIEW_UI_CONTRACT,
        "assetCount": len(packet_assets),
        "assets": packet_assets,
    }
    manifest["packetDigestSha256"] = value_sha256(manifest)
    ratings = {
        "contract": RATINGS_CONTRACT,
        "packetDigestSha256": manifest["packetDigestSha256"],
        "sourceReportFileSha256": manifest["sourceReportFileSha256"],
        "reviewer": "",
        "reviewedAt": "",
        "independentReview": False,
        "cases": {
            asset["assetId"]: {
                "decision": "pending",
                "notes": "",
                "scores": {field: 0 for field in SCORE_FIELDS},
            }
            for asset in packet_assets
        },
    }
    write_json(output / "manifest.json", manifest)
    write_json(output / "ratings.json", ratings)
    (output / "index.html").write_text(packet_html(manifest), encoding="utf-8")
    print(f"packet={output}")
    print(f"assets={len(packet_assets)}")
    print(f"packet_digest_sha256={manifest['packetDigestSha256']}")
    return 0


def valid_timestamp(value: Any) -> bool:
    if not isinstance(value, str) or not value:
        return False
    try:
        parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError:
        return False
    return parsed.tzinfo is not None


def validate_packet(
    packet: Path,
    executable: Path,
    require_complete: bool,
    *,
    require_approved: bool = False,
    ratings_path: Path | None = None,
    emit_status: bool = True,
) -> int:
    packet = packet.resolve()
    manifest = load_object(packet / "manifest.json")
    ratings = load_object(
        ratings_path.resolve() if ratings_path is not None else packet / "ratings.json"
    )
    claimed = manifest.get("packetDigestSha256")
    unsigned = dict(manifest)
    unsigned.pop("packetDigestSha256", None)
    if manifest.get("contract") != PACKET_CONTRACT or claimed != value_sha256(unsigned):
        return fail("packet manifest identity is invalid")
    if manifest.get("reviewUiContract") != REVIEW_UI_CONTRACT:
        return fail("packet review UI contract is invalid")
    index_path = packet / "index.html"
    if not index_path.is_file() or index_path.read_text(encoding="utf-8") != packet_html(manifest):
        return fail("packet review UI identity is invalid")
    producer = manifest.get("producer")
    if not isinstance(producer, dict) or producer.get("executableSha256") != file_sha256(executable.resolve()):
        return fail("packet executable identity is invalid")
    assets = manifest.get("assets")
    if not isinstance(assets, list) or manifest.get("assetCount") != len(assets):
        return fail("packet asset count is invalid")
    asset_ids: list[str] = []
    for asset in assets:
        if not isinstance(asset, dict):
            return fail("packet contains an invalid asset")
        asset_id = asset.get("assetId")
        if not isinstance(asset_id, str) or asset_id in asset_ids:
            return fail("packet contains an invalid or duplicate asset")
        asset_ids.append(asset_id)
        artifacts = asset.get("artifacts")
        if not isinstance(artifacts, dict) or set(artifacts) != REVIEW_ARTIFACT_ROLES:
            return fail(f"{asset_id}: packet must contain every required review artifact")
        for artifact in artifacts.values():
            if not isinstance(artifact, dict):
                return fail(f"{asset_id}: packet artifact is invalid")
            path = packet / safe_relative_path(artifact.get("path"))
            if not path.is_file() or file_sha256(path) != artifact.get("sha256"):
                return fail(f"{asset_id}: packet artifact identity is invalid")
    if (
        ratings.get("contract") != RATINGS_CONTRACT
        or ratings.get("packetDigestSha256") != claimed
        or ratings.get("sourceReportFileSha256") != manifest.get("sourceReportFileSha256")
    ):
        return fail("ratings belong to a different packet")
    if set(ratings) != {
        "contract",
        "packetDigestSha256",
        "sourceReportFileSha256",
        "reviewer",
        "reviewedAt",
        "independentReview",
        "cases",
    }:
        return fail("ratings contain missing or unexpected fields")
    cases = ratings.get("cases")
    if not isinstance(cases, dict) or set(cases) != set(asset_ids):
        return fail("ratings must contain exactly every packet asset")
    pending = 0
    not_approved = 0
    for asset_id in asset_ids:
        rating = cases[asset_id]
        if not isinstance(rating, dict) or rating.get("decision") not in DECISIONS:
            return fail(f"{asset_id}: decision is invalid")
        if set(rating) != {"decision", "notes", "scores"}:
            return fail(f"{asset_id}: rating contains missing or unexpected fields")
        scores = rating.get("scores")
        if not isinstance(scores, dict) or set(scores) != set(SCORE_FIELDS):
            return fail(f"{asset_id}: every quality score is required")
        if rating["decision"] == "pending":
            pending += 1
            continue
        if rating["decision"] != "approve":
            not_approved += 1
        if not isinstance(rating.get("notes"), str) or not rating["notes"].strip():
            return fail(f"{asset_id}: completed decision requires notes")
        if any(not isinstance(scores[field], int) or isinstance(scores[field], bool) or not 1 <= scores[field] <= 5 for field in SCORE_FIELDS):
            return fail(f"{asset_id}: quality scores must be integers from 1 to 5")
    if require_complete or require_approved:
        if pending:
            return fail(f"human review is incomplete: {pending} pending")
        if ratings.get("independentReview") is not True:
            return fail("completed review requires independentReview=true")
        if not isinstance(ratings.get("reviewer"), str) or not ratings["reviewer"].strip():
            return fail("completed review requires a reviewer")
        if not valid_timestamp(ratings.get("reviewedAt")):
            return fail("completed review requires a timezone-aware reviewedAt")
    if require_approved and not_approved:
        return fail(f"human review is not approved: {not_approved} revise/reject")
    if emit_status:
        print("status=valid-rendering-migration-review-packet")
        print(f"assets={len(asset_ids)}")
        print(f"pending={pending}")
        print(f"not_approved={not_approved}")
    return 0


def record_completed_ratings(
    packet: Path,
    executable: Path,
    ratings_path: Path,
    output: Path,
) -> int:
    packet = packet.resolve()
    executable = executable.resolve()
    ratings_path = ratings_path.resolve()
    output = output.resolve()
    if output.exists():
        return fail(f"output already exists: {output}")
    if output.suffix.lower() != ".json":
        return fail("completed ratings output must end in .json")
    if ratings_path == output:
        return fail("completed ratings input and output must be different")
    if validate_packet(
        packet,
        executable,
        True,
        ratings_path=ratings_path,
        emit_status=False,
    ) != 0:
        return 1
    ratings = load_object(ratings_path)
    manifest = load_object(packet / "manifest.json")
    payload = ratings_path.read_bytes()
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary_path: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            prefix=f".{output.name}.", suffix=".tmp", dir=output.parent, delete=False
        ) as temporary:
            temporary.write(payload)
            temporary.flush()
            os.fsync(temporary.fileno())
            temporary_path = Path(temporary.name)
        os.chmod(temporary_path, 0o600)
        os.link(temporary_path, output)
        temporary_path.unlink()
        temporary_path = None
    finally:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)
    print(f"completed_ratings={output}")
    print(f"completed_ratings_sha256={file_sha256(output)}")
    print(f"packet_digest_sha256={manifest['packetDigestSha256']}")
    print(f"reviewer={ratings['reviewer']}")
    print(f"reviewed_at={ratings['reviewedAt']}")
    return 0


def archive_packet(packet: Path, executable: Path, output: Path) -> int:
    packet = packet.resolve()
    executable = executable.resolve()
    output = output.resolve()
    if output.exists():
        return fail(f"output already exists: {output}")
    if output.suffix.lower() != ".zip":
        return fail("review packet archive output must end in .zip")
    if validate_packet(packet, executable, False, emit_status=False) != 0:
        return 1
    manifest = load_object(packet / "manifest.json")
    relative_files = packet_relative_files(manifest)
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary_path: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            prefix=f".{output.name}.", suffix=".tmp", dir=output.parent, delete=False
        ) as temporary:
            temporary_path = Path(temporary.name)
        with zipfile.ZipFile(
            temporary_path, mode="w", compression=zipfile.ZIP_STORED, strict_timestamps=True
        ) as archive:
            for relative in relative_files:
                source = packet / relative
                info = zipfile.ZipInfo(relative.as_posix(), date_time=ARCHIVE_TIMESTAMP)
                info.compress_type = zipfile.ZIP_STORED
                info.create_system = 3
                info.external_attr = (0o100644 & 0xFFFF) << 16
                archive.writestr(info, source.read_bytes())
        os.link(temporary_path, output)
        temporary_path.unlink()
        temporary_path = None
    finally:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)
    print(f"archive={output}")
    print(f"archive_sha256={file_sha256(output)}")
    print(f"packet_digest_sha256={manifest['packetDigestSha256']}")
    print(f"files={len(relative_files)}")
    return 0


def validate_archive(
    archive_path: Path,
    executable: Path,
    require_complete: bool,
    require_approved: bool,
    ratings_path: Path | None,
) -> int:
    archive_path = archive_path.resolve()
    executable = executable.resolve()
    if not archive_path.is_file():
        return fail(f"archive does not exist: {archive_path}")
    with zipfile.ZipFile(archive_path, mode="r") as archive:
        infos = archive.infolist()
        names = [info.filename for info in infos]
        if len(infos) > ARCHIVE_MAX_FILES or len(names) != len(set(names)):
            return fail("review packet archive file inventory is invalid")
        if any(info.is_dir() for info in infos):
            return fail("review packet archive must not contain directory entries")
        if sum(info.file_size for info in infos) > ARCHIVE_MAX_UNCOMPRESSED_BYTES:
            return fail("review packet archive exceeds the uncompressed size limit")
        for name in names:
            safe_relative_path(name)
        with tempfile.TemporaryDirectory(
            prefix="framehouse-rendering-migration-review-"
        ) as temporary:
            packet = Path(temporary)
            for info in infos:
                destination = packet / safe_relative_path(info.filename)
                destination.parent.mkdir(parents=True, exist_ok=True)
                with archive.open(info, mode="r") as source, destination.open("wb") as target:
                    shutil.copyfileobj(source, target, length=1024 * 1024)
            manifest = load_object(packet / "manifest.json")
            expected = {path.as_posix() for path in packet_relative_files(manifest)}
            if set(names) != expected:
                return fail("review packet archive contains missing or unexpected files")
            if validate_packet(
                packet,
                executable,
                require_complete,
                require_approved=require_approved,
                ratings_path=ratings_path,
                emit_status=False,
            ) != 0:
                return 1
            asset_count = manifest.get("assetCount")
    print("status=valid-rendering-migration-review-archive")
    print(f"archive_sha256={file_sha256(archive_path)}")
    print(f"assets={asset_count}")
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    build = commands.add_parser("build")
    build.add_argument("--report", type=Path, required=True)
    build.add_argument("--executable", type=Path, required=True)
    build.add_argument("--output", type=Path, required=True)
    validate = commands.add_parser("validate")
    validate.add_argument("--packet", type=Path, required=True)
    validate.add_argument("--executable", type=Path, required=True)
    validate.add_argument("--ratings", type=Path)
    validate.add_argument("--require-complete", action="store_true")
    validate.add_argument("--require-approved", action="store_true")
    record = commands.add_parser("record-ratings")
    record.add_argument("--packet", type=Path, required=True)
    record.add_argument("--executable", type=Path, required=True)
    record.add_argument("--ratings", type=Path, required=True)
    record.add_argument("--output", type=Path, required=True)
    archive = commands.add_parser("archive")
    archive.add_argument("--packet", type=Path, required=True)
    archive.add_argument("--executable", type=Path, required=True)
    archive.add_argument("--output", type=Path, required=True)
    validate_archive_command = commands.add_parser("validate-archive")
    validate_archive_command.add_argument("--archive", type=Path, required=True)
    validate_archive_command.add_argument("--executable", type=Path, required=True)
    validate_archive_command.add_argument("--ratings", type=Path)
    validate_archive_command.add_argument("--require-complete", action="store_true")
    validate_archive_command.add_argument("--require-approved", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        if args.command == "build":
            return build_packet(args.report, args.executable, args.output)
        if args.command == "validate":
            return validate_packet(
                args.packet,
                args.executable,
                args.require_complete,
                require_approved=args.require_approved,
                ratings_path=args.ratings,
            )
        if args.command == "record-ratings":
            return record_completed_ratings(
                args.packet, args.executable, args.ratings, args.output
            )
        if args.command == "archive":
            return archive_packet(args.packet, args.executable, args.output)
        return validate_archive(
            args.archive,
            args.executable,
            args.require_complete,
            args.require_approved,
            args.ratings,
        )
    except (
        OSError,
        RuntimeError,
        ValueError,
        json.JSONDecodeError,
        zipfile.BadZipFile,
    ) as exc:
        return fail(str(exc))


if __name__ == "__main__":
    raise SystemExit(main())
