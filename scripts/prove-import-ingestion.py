#!/usr/bin/env python3
"""Exercise production import_catalog with disposable PostgreSQL and media."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    log = (out / "ingestion-commands.log").open("w")

    def run(command, **kwargs):
        return subprocess.run(command, check=True, stdout=log, stderr=log, **kwargs)

    run(["cargo", "build", "-p", "photo-catalog", "--bin", "import_catalog"], cwd=repo / "rust")
    run(["cargo", "test", "-p", "photo-catalog", "--lib", "importer::"], cwd=repo / "rust")
    helper = repo / "rust/target/debug/import_catalog"
    report = {"scope": "production import helper, disposable PostgreSQL, actual copied bytes and database rows",
              "helper_sha256": hashlib.sha256(helper.read_bytes()).hexdigest()}
    with tempfile.TemporaryDirectory(prefix="looksy-ingest-", dir="/tmp") as directory:
        root = Path(directory)
        source, managed, backup = [root / name for name in ("source", "managed", "backup")]
        for path in (source, managed, backup):
            path.mkdir()
        dng_writer = root / "write-dng.cpp"
        dng_writer.write_text('#include "fixtures/synthetic_dng.h"\n#include <fstream>\nint main(int n,char**v){if(n!=2)return 1;auto b=photo_editor::test_fixtures::synthetic_dng();std::ofstream f(v[1],std::ios::binary);f.write(reinterpret_cast<const char*>(b.data()),b.size());return !f;}\n')
        run(["clang++", "-std=c++20", "-I", str(repo / "tests"), str(dng_writer), "-o", str(root / "write-dng")])
        run([str(root / "write-dng"), str(root / "fixture.dng")])
        run(["ffmpeg", "-v", "error", "-f", "lavfi", "-i", "color=c=gray:s=96x64:d=1", "-c:v", "mpeg4", "-y", str(root / "fixture.mp4")])
        photos = []
        for index in range(400):
            folder = source / f"day-{index // 40:02d}"
            folder.mkdir(exist_ok=True)
            for extension, original in (("jpg", repo / "tests/fixtures/creative-tools/landscape-mountain-source.jpg"),
                                        ("dng", root / "fixture.dng"), ("mp4", root / "fixture.mp4")):
                path = folder / f"shot-{index:04d}.{extension}"
                shutil.copyfile(original, path)
                photos.append(path)
        first, large = source / "day-00/shot-0000.jpg", source / "day-00/shot-0001.jpg"
        # JPEG permits trailing data. A bounded large valid file makes an actual
        # interruption during verification observable instead of relying on sleep.
        with large.open("ab") as file:
            file.write(bytes(64 * 1024 * 1024))
        photos = [first, large] + [p for p in photos if p not in (first, large)]
        (source / "notes.txt").write_text("unsupported fixture; preserve me\n")
        manifest = root / "selection.txt"
        manifest.write_text("\n".join(map(str, photos)) + "\n")
        expected = {str(p.relative_to(source)): hashlib.sha256(p.read_bytes()).hexdigest() for p in photos}
        report["fixture"] = {"files": len(photos), "jpeg": 400, "raw_dng": 400, "video": 400,
                             "synthetic_media": True, "source_bytes": sum(p.stat().st_size for p in photos)}
        pgdata = root / "postgres"
        run(["initdb", "-D", str(pgdata), "-A", "trust", "-U", "looksy_fixture", "--no-locale"])
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            port = sock.getsockname()[1]
        run(["pg_ctl", "-D", str(pgdata), "-l", str(root / "postgres.log"), "-o",
             f"-h 127.0.0.1 -p {port} -k {root}", "-w", "start"])
        process = None
        try:
            database = f"postgresql://looksy_fixture@127.0.0.1:{port}/postgres"
            env = dict(os.environ, PHOTO_EDITOR_DATABASE_URL=database,
                       PHOTO_EDITOR_APP_DATA_DIR=str(root / "app-data"), PHOTO_EDITOR_CACHE_DIR=str(root / "cache"),
                       PHOTO_EDITOR_LOG_DIR=str(root / "logs"))
            def command(policy):
                return [str(helper), str(source), "fixture-drive", "Mixed fixture", "managed", str(manifest),
                        str(managed), str(backup), "source", "both", policy]

            print("interrupting a real mixed-media copy", flush=True)
            process = subprocess.Popen(command("skip"), env=env, stdout=log, stderr=log)
            partial = managed / "fixture-drive/day-00/shot-0001.jpg.virel-partial"
            published_first = managed / "fixture-drive/day-00/shot-0000.jpg"
            deadline = time.monotonic() + 120
            while not (partial.exists() and published_first.exists()):
                if process.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError("Could not observe the real copy boundary; no interruption proof claimed")
                time.sleep(0.002)
            process.kill()
            process.wait(timeout=10)
            report["interruption"] = {"terminated_during_managed_partial": True,
                                       "verified_first_file_published": published_first.exists(),
                                       "partial_present": partial.exists()}
            print("resuming the reviewed manifest with identical-copy reuse", flush=True)
            start = time.monotonic()
            run(command("reuse-identical"), env=env)
            report["resume_seconds"] = round(time.monotonic() - start, 3)
            for relative, digest in expected.items():
                for base in (source, managed / "fixture-drive", backup / "fixture-drive"):
                    assert hashlib.sha256((base / relative).read_bytes()).hexdigest() == digest, str(base / relative)
            assert not list(managed.rglob("*.virel-partial")) and not list(backup.rglob("*.virel-partial"))
            run(["pg_ctl", "-D", str(pgdata), "-m", "fast", "-w", "stop"])
            run(["pg_ctl", "-D", str(pgdata), "-l", str(root / "postgres.log"), "-o",
                 f"-h 127.0.0.1 -p {port} -k {root}", "-w", "start"])
            count = int(subprocess.check_output(["psql", database, "-At", "-c", "SELECT count(*) FROM assets WHERE drive_id='fixture-drive'"], text=True))
            assert count == len(photos), (count, len(photos))
            rows = subprocess.check_output(["psql", database, "-At", "-c",
                "SELECT row_to_json(t) FROM (SELECT root_path,relative_path FROM assets WHERE drive_id='fixture-drive') t"], text=True)
            for line in rows.splitlines():
                row = json.loads(line)
                linked = Path(row["root_path"]) / row["relative_path"]
                assert hashlib.sha256(linked.read_bytes()).hexdigest() == expected[row["relative_path"]]
            before = count
            run(command("skip"), env=env)
            count = int(subprocess.check_output(["psql", database, "-At", "-c", "SELECT count(*) FROM assets WHERE drive_id='fixture-drive'"], text=True))
            assert count == before
            report.update({"catalog_rows_after_resume": count, "sha256_verified_files": len(photos) * 3,
                           "repeat_skip_preserved_rows": True, "partials_removed_after_resume": True,
                           "catalog_links_verified": count, "postgres_restart_preserved_rows": True,
                           "source_originals_unchanged": True, "disposable_postgres": True})
        finally:
            if process is not None and process.poll() is None:
                process.kill()
                process.wait(timeout=10)
            run(["pg_ctl", "-D", str(pgdata), "-m", "fast", "-w", "stop"])
    report["status"] = "passed"
    (out / "ingestion-verification.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2), flush=True)


if __name__ == "__main__":
    main()
