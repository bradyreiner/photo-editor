#!/usr/bin/env python3
"""Exercise the real membership helper in a disposable, socket-only PostgreSQL cluster."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--helper", type=Path, required=True)
    parser.add_argument("--postgres-bin", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--native-driver", type=Path)
    parser.add_argument("--helper-fixture", type=Path)
    args = parser.parse_args()
    helper = args.helper.resolve(strict=True)
    root = Path(tempfile.mkdtemp(prefix="looksy-membership-pg-", dir="/private/tmp"))
    started = False
    receipt = {"accepted": False, "fixture_root": str(root), "requests": []}

    def pg(name, *arguments, **kwargs):
        return subprocess.run([str(args.postgres_bin / name), *arguments],
                              check=True, text=True, **kwargs)

    def sql(statement):
        return pg("psql", "-h", str(root), "-p", "55439", "-U", "membership_test",
                  "-d", "postgres", "-X", "-A", "-t", "-v", "ON_ERROR_STOP=1",
                  "-c", statement, capture_output=True).stdout.strip()

    try:
        pg("initdb", "-D", str(root / "data"), "-A", "trust", "--no-locale",
           "-U", "membership_test", stdout=subprocess.DEVNULL)
        pg("pg_ctl", "-D", str(root / "data"), "-l", str(root / "postgres.log"),
           "-o", f"-k {root} -p 55439 -c listen_addresses=''", "-w", "start")
        started = True
        env = os.environ.copy()
        env["PHOTO_EDITOR_DATABASE_URL"] = (
            f"postgresql://membership_test@localhost/postgres?host={root}&port=55439")
        env["PHOTO_EDITOR_APP_DATA_DIR"] = str(root / "appdata")

        def request(project, added, removed, expected=0):
            payload = root / "request.json"
            payload.write_text(json.dumps(dict(project_name=project, added=added, removed=removed)))
            start = time.monotonic()
            result = subprocess.run([str(helper), "update-project-memberships", str(payload)],
                                    env=env, capture_output=True, text=True, timeout=30)
            receipt["requests"].append(dict(project=project, added=len(added), removed=len(removed),
                                            elapsed_ms=(time.monotonic() - start) * 1000,
                                            exit_code=result.returncode))
            assert result.returncode == expected, (result.returncode, result.stdout, result.stderr)
            return result.stderr

        # Open and migrate the complete isolated catalog through the production helper.
        request("", [], [], 1)
        sql("INSERT INTO drives (drive_id, display_name, mount_path) VALUES ('d','fixture','/unused');"
            "INSERT INTO assets (asset_id, drive_id, root_path, relative_path, import_mode, "
            "capture_year, capture_month, capture_day) SELECT 'a-'||n, 'd', '/unused', n||'.jpg', "
            "'referenced', 2026, 10, 1 FROM generate_series(0,999) n;")
        sql("CREATE TABLE membership_audit (transaction_id bigint, operation text);"
            "CREATE FUNCTION audit_membership() RETURNS trigger LANGUAGE plpgsql AS $$ BEGIN "
            "INSERT INTO membership_audit VALUES (txid_current(), TG_OP); RETURN NULL; END $$;"
            "CREATE TRIGGER audit_membership AFTER INSERT OR DELETE ON project_assets "
            "FOR EACH ROW EXECUTE FUNCTION audit_membership();")
        ids = [f"a-{n}" for n in range(1000)]
        request("Batch", ids, [])
        assert sql("SELECT count(*) FROM project_assets WHERE project_name='Batch'") == "1000"
        assert sql("SELECT count(*)||':'||count(DISTINCT transaction_id) FROM membership_audit") == "1000:1"
        request("Batch", ids, [])
        assert sql("SELECT count(*) FROM membership_audit") == "1000"
        request("Batch", [], ids[:500])
        assert sql("SELECT count(*) FROM project_assets WHERE project_name='Batch'") == "500"
        # Unknown assets keep valid changes committed; the helper reports local-only intent.
        error = request("Batch", ["a-0", "missing"], ["a-500"], 1)
        assert "1 project memberships remain local" in error, error
        assert sql("SELECT count(*) FROM project_assets WHERE project_name='Batch'") == "500"
        assert sql("SELECT asset_id FROM project_assets WHERE asset_id IN ('a-0','a-500')") == "a-0"
        # A database failure must roll back removals and project creation as well as additions.
        sql("CREATE FUNCTION reject_membership() RETURNS trigger LANGUAGE plpgsql AS $$ BEGIN "
            "IF NEW.asset_id='a-1' THEN RAISE EXCEPTION 'fixture rollback'; END IF; RETURN NEW; END $$;"
            "CREATE TRIGGER reject_membership BEFORE INSERT ON project_assets "
            "FOR EACH ROW EXECUTE FUNCTION reject_membership();")
        audit_before = sql("SELECT count(*) FROM membership_audit")
        request("Batch", ["a-1"], ["a-0"], 1)
        assert sql("SELECT count(*) FROM project_assets WHERE asset_id='a-0'") == "1"
        request("RolledBack", ["a-1"], [], 1)
        assert sql("SELECT count(*) FROM projects WHERE name='RolledBack'") == "0"
        assert sql("SELECT count(*) FROM membership_audit") == audit_before
        request("Batch", ["a-0"], ["a-0"], 1)
        assert sql("SELECT count(*) FROM membership_audit") == audit_before
        sql("DROP TRIGGER reject_membership ON project_assets")
        subprocess.run([str(helper), "delete-asset", "a-0"], env=env, check=True,
                       capture_output=True, text=True, timeout=30)
        assert sql("SELECT count(*) FROM project_assets WHERE asset_id='a-0'") == "0"
        request("Batch", ["a-0", "a-2"], [], 1)
        assert sql("SELECT asset_id FROM project_assets WHERE asset_id IN ('a-0','a-2')") == "a-2"
        # SQL metacharacters are a literal bound project name.
        request("O'Brien; --", ["a-3"], [])
        assert sql("SELECT count(*) FROM projects WHERE name='O''Brien; --'") == "1"
        if args.native_driver:
            assert args.helper_fixture, "Native proof needs the gated helper fixture"
            sql("INSERT INTO assets (asset_id, drive_id, root_path, relative_path, import_mode, "
                "capture_year, capture_month, capture_day) SELECT 'asset-'||n, 'd', '/unused', "
                "'native-'||n||'.jpg', 'referenced', 2026, 10, 1 FROM generate_series(0,999) n;")
            capture = args.output.resolve().parent / "native"
            capture.mkdir(parents=True, exist_ok=True)
            env.update(QT_QPA_PLATFORM="cocoa", QSG_RHI_BACKEND="metal",
                       PHOTO_EDITOR_MEMBERSHIP_REAL_HELPER=str(helper))
            with (capture / "driver.log").open("w") as log:
                subprocess.run([str(args.native_driver.resolve(strict=True)), "--library-membership-proof",
                                "--membership-helper-fixture", str(args.helper_fixture.resolve(strict=True)),
                                "--membership-database-url", env["PHOTO_EDITOR_DATABASE_URL"],
                                "--capture-dir", str(capture), "--timeout-ms", "30000"],
                               env=env, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=90)
            native = json.loads((capture / "membership-receipt.json").read_text())
            assert native["accepted"] and native["persistenceMode"] == "gated real catalog_state helper"
            assert sql("SELECT count(*) FROM project_assets WHERE project_name='Native batch'") == "1000"
            assert sql("SELECT count(*) FROM project_assets WHERE project_name='Native batch' AND "
                       "asset_id IN (SELECT 'asset-'||n FROM generate_series(0,999) n)") == "1000"
            receipt["native"] = native
            receipt["native_database_membership_count"] = 1000
        receipt.update(accepted=True, checks=["1000 additions in one transaction", "idempotent retry",
                       "500 removals", "unknown ID with committed valid intent", "atomic rollback",
                       "rollback of project creation", "overlap rejection", "deleted asset",
                       "bound project name"], limitations=["No throughput comparison or packaged-app/cold-start claim; native proof is present only when requested."])
    finally:
        if started:
            pg("pg_ctl", "-D", str(root / "data"), "-m", "fast", "-w", "stop")
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(receipt, indent=2) + "\n")
        print(json.dumps(receipt, indent=2))


if __name__ == "__main__":
    main()
