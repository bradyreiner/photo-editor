#!/usr/bin/env python3
"""Prove catalog metadata persistence against a disposable socket-only PostgreSQL cluster."""

import argparse
import json
import os
from pathlib import Path
import re
import secrets
import struct
import subprocess
import tempfile
import time
from urllib.parse import quote


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--helper", type=Path, required=True, help="production catalog_state binary")
    parser.add_argument("--snapshot-helper", type=Path, required=True,
                        help="production catalog_snapshot binary")
    parser.add_argument("--import-helper", type=Path, required=True,
                        help="production import_catalog binary")
    parser.add_argument("--postgres-bin", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    helper = args.helper.resolve(strict=True)
    snapshot_helper = args.snapshot_helper.resolve(strict=True)
    import_helper = args.import_helper.resolve(strict=True)
    postgres_bin = args.postgres_bin.resolve(strict=True)
    required_tools = ("initdb", "pg_ctl", "psql")
    missing = [tool for tool in required_tools if not (postgres_bin / tool).is_file()]
    if missing:
        raise FileNotFoundError(f"Missing PostgreSQL tools in {postgres_bin}: {', '.join(missing)}")

    scratch_parent = Path("/private/tmp") if Path("/private/tmp").is_dir() else Path("/tmp")
    scratch = tempfile.TemporaryDirectory(prefix="looksy-metadata-pg-", dir=scratch_parent)
    root = Path(scratch.name)
    data_dir = root / "data"
    app_data_dir = root / "appdata"
    # No TCP listener is opened. The port only names this cluster's private socket.
    port = 55000 + secrets.randbelow(9000)
    user = "metadata_test"
    started = False
    receipt = {"accepted": False, "fixture_root": str(root), "requests": []}

    def clean_diagnostic(value):
        diagnostic = str(value or "").strip()
        diagnostic = re.sub(r"(?i)(postgres(?:ql)?://[^/\s:@]+:)[^@\s/]+@",
                            r"\1<redacted>@", diagnostic)
        diagnostic = re.sub(r"(?im)(password|pass|pwd)\s*=\s*[^\s,;]+",
                            r"\1=<redacted>", diagnostic)
        return diagnostic[-4000:]

    def process_failure(label, returncode, stdout, stderr):
        details = [f"{label} exited with status {returncode}"]
        for name, value in (("stdout", stdout), ("stderr", stderr)):
            excerpt = clean_diagnostic(value)
            if excerpt:
                details.append(f"{name}: {excerpt}")
        return "\n".join(details)

    def pg(name, *arguments, **kwargs):
        if "capture_output" not in kwargs and "stderr" not in kwargs:
            kwargs["stderr"] = subprocess.PIPE
        result = subprocess.run([str(postgres_bin / name), *arguments], check=False,
                                text=True, encoding="utf-8", timeout=45, **kwargs)
        if result.returncode != 0:
            raise RuntimeError(process_failure(name, result.returncode,
                                               result.stdout, result.stderr))
        return result

    def sql(statement):
        result = pg("psql", "-h", str(root), "-p", str(port), "-U", user,
                    "-d", "postgres", "-X", "-A", "-t", "-v", "ON_ERROR_STOP=1",
                    "-c", statement, capture_output=True)
        return result.stdout.strip()

    env = os.environ.copy()
    env["PHOTO_EDITOR_DATABASE_URL"] = (
        f"postgresql://{user}@localhost/postgres?host={quote(str(root), safe='')}&port={port}")
    env["PHOTO_EDITOR_APP_DATA_DIR"] = str(app_data_dir)
    env["PHOTO_EDITOR_LOG_DIR"] = str(app_data_dir / "logs")
    env["PHOTO_EDITOR_BACKUP_DIR"] = str(app_data_dir / "backups")
    env["PHOTO_EDITOR_DATABASE_MAX_CONNECTIONS"] = "4"

    def helper_call(binary, operation, arguments=(), expected=0):
        started_at = time.monotonic()
        result = subprocess.run([str(binary), operation, *map(str, arguments)], env=env,
                                capture_output=True, text=True, encoding="utf-8", timeout=45)
        receipt["requests"].append({
            "operation": operation,
            "exit_code": result.returncode,
            "elapsed_ms": round((time.monotonic() - started_at) * 1000, 2),
        })
        if result.returncode != expected:
            raise RuntimeError(process_failure(f"{Path(binary).name} {operation}",
                                               result.returncode, result.stdout, result.stderr))
        return result

    def write_metadata(records, expected=0):
        request_number = sum(1 for call in receipt["requests"]
                             if call["operation"] == "set-asset-metadata") + 1
        payload = root / f"metadata-request-{request_number}.json"
        payload.write_text(json.dumps({"records": records}, ensure_ascii=False), encoding="utf-8")
        result = helper_call(helper, "set-asset-metadata", (payload,), expected)
        if expected != 0:
            return result, None
        return result, json.loads(result.stdout)

    def record(asset_id, metadata):
        return {"asset_id": asset_id, "metadata": metadata}

    def stored_metadata(asset_id):
        raw = sql("SELECT metadata_json::text FROM asset_metadata "
                  f"WHERE asset_id = '{asset_id}'")
        return json.loads(raw) if raw else None

    def snapshot():
        result = helper_call(snapshot_helper, "")
        return json.loads(result.stdout)

    def write_exif_tiff(path):
        """Write a minimal TIFF carrying real DateTimeOriginal EXIF fields."""
        fields = [
            (0x9003, b"2024:02:29 23:59:58\0"),
            (0x9011, b"-07:00\0"),
            (0x9291, b"037\0"),
        ]
        exif_ifd_offset = 26
        payload_offset = exif_ifd_offset + 2 + 12 * len(fields) + 4
        payload = bytearray()
        entries = []
        cursor = payload_offset
        for tag, value in fields:
            if len(value) <= 4:
                encoded_value = value.ljust(4, b"\0")
            else:
                if cursor % 2:
                    payload.append(0)
                    cursor += 1
                encoded_value = struct.pack("<I", cursor)
                payload.extend(value)
                cursor += len(value)
            entries.append(struct.pack("<HHI", tag, 2, len(value)) + encoded_value)

        header = b"II*\0" + struct.pack("<I", 8)
        primary_ifd = (struct.pack("<H", 1)
                       + struct.pack("<HHII", 0x8769, 4, 1, exif_ifd_offset)
                       + struct.pack("<I", 0))
        exif_ifd = (struct.pack("<H", len(entries)) + b"".join(entries)
                    + struct.pack("<I", 0))
        path.write_bytes(header + primary_ifd + exif_ifd + payload)

    def stop_server_if_running():
        status = subprocess.run([str(postgres_bin / "pg_ctl"), "-D", str(data_dir), "status"],
                                capture_output=True, text=True, encoding="utf-8", timeout=15)
        if status.returncode == 0:
            result = subprocess.run([str(postgres_bin / "pg_ctl"), "-D", str(data_dir),
                                     "-m", "fast", "-w", "stop"],
                                    capture_output=True, text=True, encoding="utf-8", timeout=30)
            if result.returncode != 0:
                raise RuntimeError(process_failure("pg_ctl stop", result.returncode,
                                                   result.stdout, result.stderr))

    try:
        pg("initdb", "-D", str(data_dir), "-A", "trust", "--no-locale", "-E", "UTF8",
           "-U", user, stdout=subprocess.DEVNULL)
        # Mark before starting: pg_ctl can fail after it has launched postmaster.
        started = True
        pg("pg_ctl", "-D", str(data_dir), "-l", str(root / "postgres.log"),
           "-o", f"-k {root} -p {port} -c listen_addresses=''", "-w", "start")

        # Opening the real snapshot helper initializes every catalog migration.
        initial_snapshot = snapshot()
        assert initial_snapshot.get("asset_metadata") == {}, initial_snapshot.get("asset_metadata")
        assert sql("SELECT count(*) FROM _sqlx_migrations WHERE version=15 AND success") == "1"
        assert sql("SELECT count(*) FROM _sqlx_migrations WHERE version=16 AND success") == "1"
        assert sql("SELECT count(*) FROM information_schema.tables WHERE table_schema='public' "
                   "AND table_name='asset_metadata'") == "1"
        assert sql("SELECT count(*) FROM information_schema.columns WHERE table_schema='public' "
                   "AND table_name='assets' AND column_name='capture_timestamp'") == "1"

        sql("INSERT INTO drives (drive_id, display_name, mount_path) "
            "VALUES ('metadata-drive', 'metadata fixture', '/unused'); "
            "INSERT INTO assets (asset_id, drive_id, root_path, relative_path, import_mode, "
            "capture_year, capture_month, capture_day) VALUES "
            "('asset-a', 'metadata-drive', '/unused', 'a.cr3', 'referenced', 2026, 10, 7), "
            "('asset-b', 'metadata-drive', '/unused', 'b.jpg', 'referenced', 2026, 10, 7), "
            "('asset-delete', 'metadata-drive', '/unused', 'delete.jpg', 'referenced', 2026, 10, 7), "
            "('asset-fail', 'metadata-drive', '/unused', 'fail.jpg', 'referenced', 2026, 10, 7);")
        assert sql("SELECT count(*) FROM assets WHERE capture_timestamp IS NULL") == "4"

        # Import a real EXIF TIFF through the production importer, then read it
        # back through the catalog's production list/snapshot mapper. This also
        # proves migration 16 leaves pre-existing rows NULL.
        source_root = root / "timestamp-import"
        source_root.mkdir()
        write_exif_tiff(source_root / "capture.tif")
        import_started = time.monotonic()
        imported = subprocess.run(
            [str(import_helper), str(source_root), "timestamp-drive", "timestamp fixture",
             "referenced", "-", "-", "-", "source", "both", "skip"],
            env=env, capture_output=True, text=True, encoding="utf-8", timeout=60)
        receipt["requests"].append({
            "operation": "import-catalog-exif-timestamp",
            "exit_code": imported.returncode,
            "elapsed_ms": round((time.monotonic() - import_started) * 1000, 2),
        })
        if imported.returncode != 0:
            raise RuntimeError(process_failure("import_catalog EXIF timestamp fixture",
                                               imported.returncode, imported.stdout,
                                               imported.stderr))
        expected_capture_timestamp = "2024-02-29T23:59:58.037-07:00"
        assert sql("SELECT capture_timestamp FROM assets WHERE drive_id='timestamp-drive' "
                   "AND relative_path='capture.tif'") == expected_capture_timestamp
        imported_snapshot = snapshot()
        imported_asset = next(asset for asset in imported_snapshot["assets"]
                              if asset["location"]["drive_id"] == "timestamp-drive")
        assert imported_asset["capture_timestamp"] == expected_capture_timestamp, imported_asset
        legacy_asset_ids = {"asset-a", "asset-b", "asset-delete", "asset-fail"}
        snapshot_by_id = {asset["asset_id"]: asset for asset in imported_snapshot["assets"]}
        assert all(snapshot_by_id[asset_id]["capture_timestamp"] is None
                   for asset_id in legacy_asset_ids), imported_snapshot["assets"]
        assert sql("SELECT bool_and(capture_timestamp IS NULL) FROM assets WHERE asset_id IN "
                   "('asset-a','asset-b','asset-delete','asset-fail')") == "t"

        metadata_a = {
            "title": "夏の光 — dawn 🌄",
            "caption": "Málaga at first light; 夜明け",
            "creator": "Zoë Photographer",
            "copyright": "© 2026 Looksy",
            "location": "Málaga, España",
            "captureTime": "2026-10-07T15:30:12-06:00",
            "keywords": ["Nepal", "हिमालय", "山 🌄"],
        }
        metadata_b = {
            "title": "Night crossing — 冬",
            "caption": "Frames after dark",
            "keywords": ["night", "雪"],
        }
        metadata_delete = {"title": "Will be cascaded", "keywords": ["temporary"]}
        _, applied = write_metadata([
            record("asset-a", metadata_a),
            record("asset-b", metadata_b),
            record("asset-delete", metadata_delete),
        ])
        assert set(applied["applied"]) == {"asset-a", "asset-b", "asset-delete"}, applied
        assert applied["missing"] == [], applied
        assert stored_metadata("asset-a") == metadata_a
        assert stored_metadata("asset-b") == metadata_b
        assert stored_metadata("asset-delete") == metadata_delete

        # A mixed batch reports unknown IDs while committing records for known assets.
        updated_a = dict(metadata_a, caption="Updated caption — édité")
        _, mixed_receipt = write_metadata([
            record("asset-a", updated_a),
            record("missing-asset", {"title": "Still local"}),
        ])
        assert set(mixed_receipt["applied"]) == {"asset-a"}, mixed_receipt
        assert mixed_receipt["missing"] == ["missing-asset"], mixed_receipt
        assert stored_metadata("asset-a") == updated_a
        assert stored_metadata("asset-b") == metadata_b

        # Sequence effects survive transaction rollback, so they prove invalid
        # requests never reach an asset_metadata INSERT or UPDATE trigger.
        sql("CREATE SEQUENCE metadata_mutation_probe_seq; "
            "CREATE FUNCTION metadata_mutation_probe() RETURNS trigger LANGUAGE plpgsql AS $$ "
            "BEGIN PERFORM nextval('metadata_mutation_probe_seq'); RETURN NEW; END $$; "
            "CREATE TRIGGER metadata_mutation_probe BEFORE INSERT OR UPDATE ON asset_metadata "
            "FOR EACH ROW EXECUTE FUNCTION metadata_mutation_probe();")
        baseline_a = stored_metadata("asset-a")
        invalid_requests = [
            ("unsupported-field", [record("asset-a", dict(updated_a, fabricated="nope"))]),
            ("invalid-capture-time", [record("asset-a", dict(updated_a, captureTime="next Thursday"))]),
            ("duplicate-asset-id", [record("asset-a", {"title": "first"}),
                                    record("asset-a", {"title": "second"})]),
        ]
        for label, records in invalid_requests:
            rejected, _ = write_metadata(records, expected=1)
            assert rejected.stderr, (label, rejected.stdout, rejected.stderr)
            assert sql("SELECT CASE WHEN is_called THEN 'called' ELSE 'not-called' END "
                       "FROM metadata_mutation_probe_seq") == "not-called", label
            assert stored_metadata("asset-a") == baseline_a, label
        sql("DROP TRIGGER metadata_mutation_probe ON asset_metadata; "
            "DROP FUNCTION metadata_mutation_probe(); DROP SEQUENCE metadata_mutation_probe_seq;")

        # The second sorted write fails after asset-a has been updated inside the
        # same transaction. The first update and the failing insert both roll back.
        sql("CREATE FUNCTION reject_metadata_failure() RETURNS trigger LANGUAGE plpgsql AS $$ "
            "BEGIN IF NEW.asset_id='asset-fail' THEN RAISE EXCEPTION 'metadata rollback fixture'; "
            "END IF; RETURN NEW; END $$; "
            "CREATE TRIGGER reject_metadata_failure BEFORE INSERT OR UPDATE ON asset_metadata "
            "FOR EACH ROW EXECUTE FUNCTION reject_metadata_failure();")
        failed, _ = write_metadata([
            record("asset-a", dict(updated_a, title="Must roll back")),
            record("asset-fail", {"title": "Trigger failure"}),
        ], expected=1)
        assert "metadata rollback fixture" in failed.stderr, failed.stderr
        assert stored_metadata("asset-a") == baseline_a
        assert stored_metadata("asset-fail") is None
        sql("DROP TRIGGER reject_metadata_failure ON asset_metadata; "
            "DROP FUNCTION reject_metadata_failure();")

        # Snapshot serialization must expose exact metadata, including Unicode.
        decoded = snapshot()
        snapshot_metadata = decoded.get("asset_metadata")
        assert snapshot_metadata["asset-a"] == baseline_a, snapshot_metadata
        assert snapshot_metadata["asset-b"] == metadata_b, snapshot_metadata
        assert snapshot_metadata["asset-delete"] == metadata_delete, snapshot_metadata

        # Deleting an asset through the production helper must cascade its metadata row.
        helper_call(helper, "delete-asset", ("asset-delete",))
        assert sql("SELECT count(*) FROM assets WHERE asset_id='asset-delete'") == "0"
        assert sql("SELECT count(*) FROM asset_metadata WHERE asset_id='asset-delete'") == "0"
        after_delete = snapshot()
        assert "asset-delete" not in after_delete.get("asset_metadata", {}), after_delete.get("asset_metadata")

        receipt.update(accepted=True, checks=[
            "migration 15 creates the metadata table",
            "migration 16 adds nullable capture timestamps and preserves legacy NULL rows",
            "production importer stores EXIF timestamp with subseconds and offset; catalog snapshot reads it back",
            "production helper writes and reads exact Unicode metadata and keywords",
            "known and missing asset IDs are distinguished in a mixed batch receipt",
            "invalid fields, capture times, and duplicate IDs are rejected before metadata DML",
            "database trigger failure rolls back the whole metadata batch",
            "catalog_snapshot decodes stored metadata records",
            "asset deletion cascades its metadata row",
        ], limitations=["PostgreSQL was isolated to a temporary Unix socket cluster with TCP disabled."])
    except Exception as error:
        receipt["error"] = f"{type(error).__name__}: {error}"
        raise
    finally:
        try:
            if started:
                stop_server_if_running()
        except Exception as error:
            receipt["shutdown_error"] = f"{type(error).__name__}: {error}"
            raise
        finally:
            # If PostgreSQL failed to stop, retain its directory rather than
            # removing files beneath a running server. Otherwise remove all
            # data and socket state before returning.
            if "shutdown_error" not in receipt:
                scratch.cleanup()
            output = args.output.resolve()
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_text(json.dumps(receipt, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
            print(json.dumps(receipt, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
