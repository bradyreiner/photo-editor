#!/usr/bin/env python3
"""Run the bounded real-import overlap fixture on a private Unix-socket PostgreSQL cluster."""

import argparse
import getpass
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def run(command, env, timeout=60):
    result = subprocess.run([str(value) for value in command], env=env, text=True,
                            capture_output=True, timeout=timeout)
    require(result.returncode == 0,
            f'{Path(command[0]).name} failed ({result.returncode}):\n'
            f'{result.stdout}\n{result.stderr}')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--driver', type=Path, required=True)
    parser.add_argument('--repo-root', type=Path, required=True)
    parser.add_argument('--catalog-bin-dir', type=Path, required=True)
    parser.add_argument('--postgres-bin-dir', type=Path,
                        default=os.environ.get('PHOTO_EDITOR_TEST_POSTGRES_BIN_DIR'))
    parser.add_argument('--photo-fixture', type=Path, required=True)
    args = parser.parse_args()

    repo = args.repo_root.resolve()
    catalog_bin = args.catalog_bin_dir.resolve()
    driver = args.driver.resolve()
    fixture = args.photo_fixture.resolve()
    require(driver.is_file() and os.access(driver, os.X_OK), f'Workload driver missing: {driver}')
    require(fixture.is_file(), f'Bundled photo fixture missing: {fixture}')

    pg = args.postgres_bin_dir
    if pg is None and shutil.which('pg_config'):
        pg = Path(subprocess.check_output(['pg_config', '--bindir'], text=True).strip())
    if pg is None:
        print('SKIP: PostgreSQL initdb/pg_ctl are unavailable', file=sys.stderr)
        return 77
    pg = Path(pg).resolve()
    require(all((pg / name).is_file() for name in ('initdb', 'pg_ctl', 'postgres')),
            f'Required PostgreSQL runtime binaries are missing from {pg}')
    require(all((catalog_bin / name).is_file() and os.access(catalog_bin / name, os.X_OK)
                for name in ('catalog_migrate', 'catalog_snapshot', 'import_catalog')),
            f'Required catalog helpers are missing or not executable from {catalog_bin}')

    # Refuse a stale helper set. The root owns rebuilding these targets under
    # its build lock before it runs this wrapper.
    source_paths = [repo / 'rust/Cargo.toml', repo / 'rust/Cargo.lock']
    source_paths.extend((repo / 'rust/crates/catalog/src').rglob('*.rs'))
    require(all(path.is_file() for path in source_paths), 'Rust catalog source freshness inventory is incomplete')
    newest_source = max(path.stat().st_mtime for path in source_paths)
    for name in ('catalog_migrate', 'catalog_snapshot', 'import_catalog'):
        helper = catalog_bin / name
        require(helper.stat().st_mtime >= newest_source,
                f'{name} is older than current Rust catalog sources; rebuild Rust helper targets under the root build lock')

    with tempfile.TemporaryDirectory(prefix='looksy-round10-import-', dir='/private/tmp') as temp:
        root = Path(temp).resolve()
        require(str(root).startswith('/private/tmp/looksy-round10-import-'),
                'Temporary PostgreSQL root escaped the approved private /private/tmp namespace')
        cluster = root / 'pg'
        socket = root / 'socket'
        socket.mkdir()
        user = getpass.getuser()
        database_url = f'postgresql://{user}@localhost/postgres?host={socket}'
        env = {
            **os.environ,
            'PHOTO_EDITOR_ROUND10_PRIVATE_ROOT': str(root),
            'PHOTO_EDITOR_DATABASE_URL': database_url,
            'PHOTO_EDITOR_REPOSITORY_ROOT': str(repo),
            'PHOTO_EDITOR_APP_DATA_DIR': str(root / 'runtime'),
            'PHOTO_EDITOR_CACHE_DIR': str(root / 'cache'),
            'PHOTO_EDITOR_MANAGED_MEDIA_DIR': str(root / 'managed-media'),
            'PHOTO_EDITOR_ORIGINALS_BACKUP_DIR': str(root / 'originals-backup'),
            'PHOTO_EDITOR_BACKUP_DIR': str(root / 'catalog-backups'),
            'PHOTO_EDITOR_LOG_DIR': str(root / 'logs'),
            'PHOTO_EDITOR_OBSERVABILITY_RUN_DIR': str(root / 'logs'),
            'PHOTO_EDITOR_ENABLE_DEV_OBSERVABILITY': '1',
            'PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE': '1',
            'QT_QPA_PLATFORM': 'offscreen',
            'PHOTO_EDITOR_DISABLE_METAL_PREVIEW': '1',
        }
        for directory in ('runtime', 'cache', 'managed-media', 'originals-backup', 'catalog-backups', 'logs'):
            (root / directory).mkdir()

        started = False
        try:
            run([pg / 'initdb', '-D', cluster, '-A', 'trust', '-U', user,
                 '--no-locale', '-E', 'UTF8'], env, timeout=60)
            run([pg / 'pg_ctl', '-D', cluster, '-l', root / 'postgres.log', '-o',
                 f"-k {socket} -c listen_addresses=''", '-w', 'start'], env, timeout=60)
            started = True
            run([catalog_bin / 'catalog_migrate'], env, timeout=60)
            result = subprocess.run([str(driver)], env=env, cwd=repo, text=True,
                                    capture_output=True, timeout=240)
            if result.returncode != 0:
                raise RuntimeError(
                    f'real-import workload driver failed ({result.returncode}):\n'
                    f'{result.stdout}\n{result.stderr}\n'
                    f'private PostgreSQL log:\n{(root / "postgres.log").read_text(errors="replace")}')
            print(result.stdout, end='')
            if result.stderr:
                print(result.stderr, file=sys.stderr, end='')
        finally:
            # A failed/timeout startup can still leave our own postmaster alive.
            if started or (cluster / 'postmaster.pid').exists():
                stopped = subprocess.run(
                    [str(pg / 'pg_ctl'), '-D', str(cluster), '-m', 'fast', '-w', 'stop'],
                    env=env, text=True, capture_output=True, timeout=60)
                if stopped.returncode != 0 and (cluster / 'postmaster.pid').exists():
                    stopped = subprocess.run(
                        [str(pg / 'pg_ctl'), '-D', str(cluster), '-m', 'immediate', '-w', 'stop'],
                        env=env, text=True, capture_output=True, timeout=30)
                require(not (cluster / 'postmaster.pid').exists(),
                        f'Private PostgreSQL cluster did not stop: {stopped.stderr}')
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except Exception as exc:  # Keep a concise CTest failure while preserving the cause.
        print(f'ERROR: {exc}', file=sys.stderr)
        raise SystemExit(1)
