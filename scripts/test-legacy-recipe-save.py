#!/usr/bin/env python3
"""Exercise legacy recipe persistence in a disposable PostgreSQL cluster."""
import argparse
import getpass
import json
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--postgres-bin', default='/opt/homebrew/opt/postgresql@14/bin')
    parser.add_argument('--recipe-dir', type=Path)
    parser.add_argument('--desktop-test', type=Path,
                        help='Run a catalog test against the disposable database instead')
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    helpers = repo / 'rust/target/debug'
    pg = Path(args.postgres_bin)
    with tempfile.TemporaryDirectory(prefix='framehouse-legacy-save-') as temp:
        root = Path(temp)
        cluster, socket = root / 'pg', root / 'socket'
        socket.mkdir()
        user = getpass.getuser()
        env = {**os.environ, 'PHOTO_EDITOR_APP_DATA_DIR': str(root / 'app'),
               'QT_QPA_PLATFORM': 'offscreen',
               'PHOTO_EDITOR_LOG_DIR': str(root / 'logs'),
               'PHOTO_EDITOR_BACKUP_DIR': str(root / 'backups'),
               'PHOTO_EDITOR_DATABASE_URL': f'postgresql://{user}@localhost/postgres?host={socket}'}
        def run(command, **kwargs):
            return subprocess.run([str(x) for x in command], env=env, text=True,
                                  capture_output=True, check=True, **kwargs)
        run([pg / 'initdb', '-D', cluster, '-A', 'trust', '-U', user])
        run([pg / 'pg_ctl', '-D', cluster, '-l', root / 'postgres.log',
             '-o', f"-k {socket} -c listen_addresses=''", '-w', 'start'])
        try:
            run([helpers / 'catalog_migrate'])
            if args.desktop_test:
                completed = subprocess.run([str(args.desktop_test.resolve())], env=env,
                                           text=True, capture_output=True)
                print(completed.stdout, end='')
                print(completed.stderr, end='')
                if completed.returncode:
                    raise RuntimeError(f'Desktop test exited {completed.returncode}')
                return
            def sql(statement):
                return run([pg / 'psql', '-h', socket, '-U', user, '-d', 'postgres',
                            '-X', '-A', '-t', '-v', 'ON_ERROR_STOP=1'], input=statement).stdout.strip()
            sql("INSERT INTO drives(drive_id,display_name,mount_path) VALUES ('proof','Proof','/isolated');")
            recipes = [json.loads(p.read_text()) for p in sorted(args.recipe_dir.glob('recipe-*.json'))] if args.recipe_dir else [
                {'recipeVersion': 12, 'colorPipeline': {'photographicRenderingModel': 'legacy-oklch-v1'},
                 'maskData': 'large-mask-' * 300000}]
            assert recipes, 'No recipes supplied'
            for i, recipe in enumerate(recipes):
                asset = f'legacy-proof-{i}'
                sql(f"INSERT INTO assets(asset_id,drive_id,root_path,relative_path,import_mode,capture_year,capture_month,capture_day) VALUES ('{asset}','proof','/isolated','{i}.raw','referenced',2026,9,10);")
                payload = json.dumps({'schema': 'photo-editor-catalog-arguments:v1',
                                      'arguments': ['--legacy-v1', asset, json.dumps(recipe)]})
                run([helpers / 'save_edit_recipe', '--stdin-json-v1'], input=payload)
                assert json.loads(sql(f"SELECT recipe_json::text FROM edit_recipes WHERE asset_id='{asset}';")) == recipe
                assert sql(f"SELECT count(*) FROM canonical_edit_recipes_v13 WHERE asset_id='{asset}';") == '0'
                # Simulate an already accepted canonical migration; the legacy
                # writer must reject the stale snapshot and preserve both rows.
                sql(f"INSERT INTO canonical_edit_recipes_v13(asset_id,recipe_payload,cache_key) VALUES ('{asset}','canonical-proof','canonical-key');")
                rejected = subprocess.run([str(helpers / 'save_edit_recipe'), '--stdin-json-v1'],
                                          input=payload, text=True, capture_output=True, env=env)
                assert rejected.returncode != 0 and 'already migrated' in rejected.stderr, rejected.stderr
                assert json.loads(sql(f"SELECT recipe_json::text FROM edit_recipes WHERE asset_id='{asset}';")) == recipe
                assert sql(f"SELECT recipe_payload FROM canonical_edit_recipes_v13 WHERE asset_id='{asset}';") == 'canonical-proof'
            print(f'PASS: {len(recipes)} exact legacy saves; canonical recipes protected; isolated catalog only')
        finally:
            run([pg / 'pg_ctl', '-D', cluster, '-m', 'fast', '-w', 'stop'])


if __name__ == '__main__':
    main()
