#!/usr/bin/env python3
"""Run a bounded 100k-asset PostgreSQL catalog proof in a disposable cluster."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import socket
import subprocess
import tempfile
import time
from pathlib import Path
from typing import Any


REPORT_CONTRACT = "photo-editor-postgres-large-library-proof:v1"
DEFAULT_ASSET_COUNT = 100_000
DEFAULT_EDITED_COUNT = 10_000
DEFAULT_PROJECT_COUNT = 20
DEFAULT_REVIEW_COUNT = 20_000
DEFAULT_RELINK_COUNT = 1_000
MAX_SECONDS = {
    "fixtureInsert": 30.0,
    "fullSnapshot": 30.0,
    "migrationInventory": 30.0,
    "integrityScan": 15.0,
}
MAX_RSS_BYTES = {
    "fullSnapshot": 1_610_612_736,
    "migrationInventory": 1_073_741_824,
    "integrityScan": 536_870_912,
}


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def canonical_sha256(value: Any) -> str:
    payload = json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=False
    ).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def require_executable(path: Path) -> Path:
    if not path.is_file() or not os.access(path, os.X_OK):
        raise ValueError(f"missing executable: {path}")
    return path.resolve()


def available_port() -> int:
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        return int(listener.getsockname()[1])


def run(
    arguments: list[str],
    *,
    environment: dict[str, str] | None = None,
    stdout: Any = subprocess.DEVNULL,
    timeout: float = 60.0,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        arguments,
        check=True,
        env=environment,
        stdout=stdout,
        stderr=subprocess.PIPE,
        text=True,
        timeout=timeout,
    )


def parse_peak_rss(path: Path) -> int | None:
    if not path.is_file():
        return None
    match = re.search(
        r"(?m)^\s*(\d+)\s+maximum resident set size\s*$",
        path.read_text(encoding="utf-8", errors="replace"),
    )
    return int(match.group(1)) if match else None


def measure_to_file(
    arguments: list[str],
    output: Path,
    environment: dict[str, str],
    timing_path: Path,
) -> dict[str, Any]:
    measured_arguments = arguments
    if platform.system() == "Darwin" and Path("/usr/bin/time").is_file():
        measured_arguments = ["/usr/bin/time", "-l", "-o", str(timing_path), *arguments]
    started = time.perf_counter()
    with output.open("wb") as handle:
        result = subprocess.run(
            measured_arguments,
            check=False,
            env=environment,
            stdout=handle,
            stderr=subprocess.PIPE,
            timeout=45.0,
        )
    elapsed = time.perf_counter() - started
    if result.returncode != 0:
        raise RuntimeError(
            f"command failed ({result.returncode}): {arguments[0]}: "
            + result.stderr.decode("utf-8", errors="replace")
        )
    return {
        "seconds": round(elapsed, 6),
        "peakRssBytes": parse_peak_rss(timing_path),
        "outputBytes": output.stat().st_size,
    }


def fixture_sql(
    asset_count: int,
    edited_count: int,
    project_count: int,
    review_count: int,
    relink_count: int,
) -> str:
    legacy_count = edited_count // 2
    canonical_start = legacy_count + 1
    return f"""
INSERT INTO drives (drive_id, display_name, mount_path, is_online)
VALUES ('large-library-drive', 'Large Library Drive', '/offline-large-library', FALSE);

INSERT INTO assets (
  asset_id, drive_id, root_path, relative_path, import_mode,
  capture_year, capture_month, capture_day,
  camera_make, camera_model, lens_model, iso, shutter_speed, aperture,
  created_at, updated_at
)
SELECT
  'large-asset-' || lpad(value::text, 6, '0'),
  'large-library-drive', '/offline-large-library',
  'roll-' || lpad(((value - 1) / 1000 + 1)::text, 3, '0') ||
    '/image-' || lpad(value::text, 6, '0') || '.dng',
  'referenced',
  2020 + (value % 7), 1 + (value % 12), 1 + (value % 28),
  'Looksy Camera', 'Scale Proof', '50mm', 100 + (value % 8) * 100,
  '1/125', 4.0,
  TIMESTAMPTZ '2026-01-01 00:00:00+00' + value * INTERVAL '1 second',
  TIMESTAMPTZ '2026-01-01 00:00:00+00' + value * INTERVAL '1 second'
FROM generate_series(1, {asset_count}) AS value;

INSERT INTO projects (name)
SELECT 'Large Project ' || lpad(value::text, 2, '0')
FROM generate_series(1, {project_count}) AS value;

INSERT INTO project_assets (project_name, asset_id)
SELECT
  'Large Project ' || lpad((1 + ((value - 1) % {project_count}))::text, 2, '0'),
  'large-asset-' || lpad(value::text, 6, '0')
FROM generate_series(1, {asset_count}) AS value;

INSERT INTO asset_review_states (asset_id, rating, picked, rejected, color_label)
SELECT
  'large-asset-' || lpad(value::text, 6, '0'),
  value % 6, value % 3 = 0, FALSE,
  CASE WHEN value % 2 = 0 THEN 'green' ELSE '' END
FROM generate_series(1, {review_count}) AS value;

INSERT INTO asset_relinks (asset_id, relinked_file_path)
SELECT
  'large-asset-' || lpad(value::text, 6, '0'),
  '/offline-relinked/image-' || lpad(value::text, 6, '0') || '.dng'
FROM generate_series(1, {relink_count}) AS value;

INSERT INTO edit_recipes (asset_id, recipe_json)
SELECT
  'large-asset-' || lpad(value::text, 6, '0'),
  '{{"recipeVersion":12,"colorPipeline":{{"photographicRenderingModel":"legacy-oklch-v1"}}}}'::jsonb
FROM generate_series(1, {legacy_count}) AS value;

INSERT INTO canonical_edit_recipes_v13 (
  asset_id, recipe_payload, cache_key, provenance_payload
)
SELECT
  'large-asset-' || lpad(value::text, 6, '0'),
  '{{"schema":"photo-editor-edit-recipe-v13.2"}}',
  'sha256:large-library-' || lpad(value::text, 6, '0'),
  '{{"schema":"photo-editor-edit-recipe-provenance:v1","presetApplications":[]}}'::jsonb
FROM generate_series({canonical_start}, {edited_count}) AS value;

ANALYZE;
"""


def write_report(path: Path, report: dict[str, Any]) -> None:
    if path.exists():
        raise ValueError(f"output already exists: {path}")
    path.parent.mkdir(parents=True, exist_ok=True)
    report["reportSha256"] = canonical_sha256(report)
    payload = json.dumps(report, indent=2, sort_keys=True) + "\n"
    descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(descriptor, "w", encoding="utf-8") as handle:
        handle.write(payload)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--catalog-bin-dir", type=Path, required=True)
    parser.add_argument("--postgres-runtime-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--asset-count", type=int, default=DEFAULT_ASSET_COUNT)
    args = parser.parse_args()
    if args.asset_count != DEFAULT_ASSET_COUNT:
        raise ValueError("release proof requires exactly 100000 assets")

    helpers = {
        name: require_executable(args.catalog_bin_dir / name)
        for name in ("catalog_migrate", "catalog_snapshot", "catalog_migration_inventory", "catalog_integrity")
    }
    postgres_bin = args.postgres_runtime_dir / "bin"
    postgres = {
        name: require_executable(postgres_bin / name)
        for name in ("initdb", "pg_ctl", "psql", "postgres")
    }

    proof_root = Path(tempfile.mkdtemp(prefix="framehouse-postgres-large-library.", dir="/tmp"))
    data_dir = proof_root / "data"
    log_path = proof_root / "postgres.log"
    port = available_port()
    database_name = "photo_editor_large_library_proof"
    database_url = f"postgresql://photo_editor@127.0.0.1:{port}/{database_name}"
    environment = os.environ.copy()
    environment.update(
        {
            "PHOTO_EDITOR_DATABASE_URL": database_url,
            "PHOTO_EDITOR_APP_DATA_DIR": str(proof_root / "app-data"),
            "PHOTO_EDITOR_LOG_DIR": str(proof_root / "logs"),
            "PHOTO_EDITOR_BACKUP_DIR": str(proof_root / "backups"),
        }
    )
    started = False
    try:
        run([str(postgres["initdb"]), "-D", str(data_dir), "--username=photo_editor", "--auth=trust"])
        run(
            [
                str(postgres["pg_ctl"]), "-D", str(data_dir), "-l", str(log_path),
                "-w", "start", "-o", f"-c listen_addresses=127.0.0.1 -c port={port}",
            ]
        )
        started = True
        run(
            [
                str(postgres["psql"]), "-h", "127.0.0.1", "-p", str(port),
                "-U", "photo_editor", "-d", "postgres", "-v", "ON_ERROR_STOP=1",
                "-c", f"CREATE DATABASE {database_name}",
            ]
        )
        run([str(helpers["catalog_migrate"])], environment=environment)

        fixture_statement = fixture_sql(
            DEFAULT_ASSET_COUNT,
            DEFAULT_EDITED_COUNT,
            DEFAULT_PROJECT_COUNT,
            DEFAULT_REVIEW_COUNT,
            DEFAULT_RELINK_COUNT,
        )
        insert_started = time.perf_counter()
        run(
            [str(postgres["psql"]), database_url, "-v", "ON_ERROR_STOP=1", "-c",
             fixture_statement],
            timeout=40.0,
        )
        insert_seconds = round(time.perf_counter() - insert_started, 6)

        snapshot_path = proof_root / "snapshot.json"
        inventory_path = proof_root / "inventory.json"
        integrity_path = proof_root / "integrity.json"
        metrics = {
            "fixtureInsert": {"seconds": insert_seconds, "peakRssBytes": None, "outputBytes": 0},
            "fullSnapshot": measure_to_file(
                [str(helpers["catalog_snapshot"])], snapshot_path, environment,
                proof_root / "snapshot.time",
            ),
            "migrationInventory": measure_to_file(
                [str(helpers["catalog_migration_inventory"])], inventory_path, environment,
                proof_root / "inventory.time",
            ),
            "integrityScan": measure_to_file(
                [str(helpers["catalog_integrity"]), "--require-clean"], integrity_path,
                environment, proof_root / "integrity.time",
            ),
        }

        snapshot = json.loads(snapshot_path.read_text(encoding="utf-8"))
        inventory = json.loads(inventory_path.read_text(encoding="utf-8"))
        integrity = json.loads(integrity_path.read_text(encoding="utf-8"))
        counts = {
            "assets": len(snapshot.get("assets", [])),
            "drives": len(snapshot.get("drives", [])),
            "projects": len(snapshot.get("projects", [])),
            "reviewStates": len(snapshot.get("review_states", {})),
            "relinks": len(snapshot.get("relinked_paths", {})),
            "editedAssets": inventory.get("edited_asset_count"),
            "legacyEditedAssets": sum(
                item.get("recipe_authority") == "legacy-compatibility"
                for item in inventory.get("assets", [])
            ),
            "canonicalEditedAssets": sum(
                item.get("recipe_authority") == "canonical-v13"
                for item in inventory.get("assets", [])
            ),
            "integrityScannedRecords": integrity.get("scanned_record_count"),
            "integrityFindings": len(integrity.get("findings", [])),
        }
        expected_counts = {
            "assets": DEFAULT_ASSET_COUNT,
            "drives": 1,
            "projects": DEFAULT_PROJECT_COUNT,
            "reviewStates": DEFAULT_REVIEW_COUNT,
            "relinks": DEFAULT_RELINK_COUNT,
            "editedAssets": DEFAULT_EDITED_COUNT,
            "legacyEditedAssets": DEFAULT_EDITED_COUNT // 2,
            "canonicalEditedAssets": DEFAULT_EDITED_COUNT // 2,
            "integrityScannedRecords": (
                DEFAULT_ASSET_COUNT + 1 + DEFAULT_PROJECT_COUNT
                + DEFAULT_REVIEW_COUNT + DEFAULT_RELINK_COUNT
            ),
            "integrityFindings": 0,
        }
        if counts != expected_counts:
            raise RuntimeError(f"large-library accounting mismatch: {counts} != {expected_counts}")
        for label, maximum in MAX_SECONDS.items():
            if metrics[label]["seconds"] > maximum:
                raise RuntimeError(
                    f"{label} exceeded {maximum}s: {metrics[label]['seconds']}s"
                )
        for label, maximum in MAX_RSS_BYTES.items():
            peak = metrics[label]["peakRssBytes"]
            if platform.system() == "Darwin" and (peak is None or peak > maximum):
                raise RuntimeError(f"{label} exceeded RSS budget or RSS was unavailable: {peak}")

        schema_state = run(
            [str(postgres["psql"]), database_url, "-At", "-c",
             "SELECT count(*) || ':' || min(version) || ':' || max(version) || ':' || bool_and(success) FROM _sqlx_migrations"],
            stdout=subprocess.PIPE,
        ).stdout.strip()
        report: dict[str, Any] = {
            "contract": REPORT_CONTRACT,
            "platform": platform.platform(),
            "database": {
                "postgresVersion": run(
                    [str(postgres["postgres"]), "--version"], stdout=subprocess.PIPE
                ).stdout.strip(),
                "migrationState": schema_state,
            },
            "fixture": expected_counts,
            "proofIdentity": {
                "scriptSha256": file_sha256(Path(__file__).resolve()),
                "fixtureSqlSha256": hashlib.sha256(
                    fixture_statement.encode("utf-8")
                ).hexdigest(),
            },
            "observed": counts,
            "budgets": {"maximumSeconds": MAX_SECONDS, "maximumRssBytes": MAX_RSS_BYTES},
            "metrics": metrics,
            "helpers": {
                name: {"name": path.name, "sha256": file_sha256(path)}
                for name, path in sorted(helpers.items())
            },
            "passed": True,
        }
        write_report(args.output.resolve(), report)
        print("postgres_large_library_proof=passed")
        print(f"report={args.output.resolve()}")
        print(f"report_sha256={report['reportSha256']}")
        print(f"report_file_sha256={file_sha256(args.output.resolve())}")
        for label in ("fixtureInsert", "fullSnapshot", "migrationInventory", "integrityScan"):
            print(
                f"{label}_seconds={metrics[label]['seconds']} "
                f"peak_rss_bytes={metrics[label]['peakRssBytes']}"
            )
        return 0
    finally:
        if started:
            subprocess.run(
                [str(postgres["pg_ctl"]), "-D", str(data_dir), "-m", "fast", "stop"],
                check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                timeout=15.0,
            )
        if proof_root.parent == Path("/tmp") and proof_root.name.startswith(
            "framehouse-postgres-large-library."
        ):
            shutil.rmtree(proof_root, ignore_errors=True)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"postgres large-library proof: {error}", file=os.sys.stderr)
        raise SystemExit(1)
