#!/usr/bin/env python3
"""Save production desktop payloads through Rust/PostgreSQL and reopen them."""
import argparse
import copy
import getpass
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--desktop-test', type=Path, required=True)
    parser.add_argument('--catalog-bin-dir', type=Path, required=True)
    parser.add_argument('--postgres-bin', type=Path,
                        default=os.environ.get('PHOTO_EDITOR_TEST_POSTGRES_BIN_DIR'))
    parser.add_argument('--require-postgres', action='store_true',
                        help='Fail instead of returning CTest skip code when PostgreSQL is missing')
    args = parser.parse_args()
    pg = args.postgres_bin
    if pg is None and shutil.which('pg_config'):
        pg = Path(subprocess.check_output(['pg_config', '--bindir'], text=True).strip())
    if pg is None or not all((pg / name).is_file() for name in ('initdb', 'pg_ctl', 'psql')):
        require(not args.require_postgres, 'Required PostgreSQL runtime is missing')
        print('SKIP: set PHOTO_EDITOR_TEST_POSTGRES_BIN_DIR to run the real catalog contract')
        return 77
    desktop = args.desktop_test.resolve()
    helpers = args.catalog_bin_dir.resolve()
    # Never accept an existing database URL. No production data is touched.
    with tempfile.TemporaryDirectory(prefix='looksy-save-contract-', dir='/tmp') as temp:
        root = Path(temp)
        # macOS release builds produce one multicall binary; install aliases in
        # the disposable runtime exactly as packaging does, preserving argv[0].
        commands = {}
        aliases = root / 'helpers'
        aliases.mkdir()
        for name in ('catalog_migrate', 'save_edit_recipe', 'catalog_snapshot'):
            command = helpers / name
            if not command.is_file() and (helpers / 'catalog_helper').is_file():
                command = aliases / name
                command.symlink_to(helpers / 'catalog_helper')
            require(command.is_file() and os.access(command, os.X_OK),
                    f'Required catalog helper missing or not executable: {command}')
            commands[name] = command
        cluster, sock, cases = root / 'pg', root / 'socket', root / 'cases'
        sock.mkdir()
        user = getpass.getuser()
        env = {**os.environ,
               'PHOTO_EDITOR_APP_DATA_DIR': str(root / 'app'),
               'PHOTO_EDITOR_LOG_DIR': str(root / 'logs'),
               'PHOTO_EDITOR_BACKUP_DIR': str(root / 'backups'),
               'PHOTO_EDITOR_DATABASE_URL': f'postgresql://{user}@localhost/postgres?host={sock}',
               'QT_QPA_PLATFORM': 'offscreen'}

        def run(command, *, input=None, success=True):
            result = subprocess.run([str(x) for x in command], env=env, text=True,
                                    capture_output=True, input=input, timeout=60)
            if success:
                require(result.returncode == 0,
                        f'{Path(command[0]).name} failed ({result.returncode}): {result.stderr}')
            return result

        def sql(statement, *, success=True):
            return run([pg / 'psql', env['PHOTO_EDITOR_DATABASE_URL'], '-X', '-A', '-t',
                        '-v', 'ON_ERROR_STOP=1'], input=statement, success=success)

        def save(payload, *, success=True):
            return run([commands['save_edit_recipe'], '--stdin-json-v1'],
                       input=json.dumps(payload), success=success)

        def stored(asset):
            rows = json.loads(run([commands['catalog_snapshot'], '--recipes', asset]).stdout)
            require(len(rows) == 1, f'Expected one saved recipe for {asset}')
            return rows[0]

        run([desktop, '--write-catalog-contract', cases])
        run([pg / 'initdb', '-D', cluster, '-A', 'trust', '-U', user, '--no-locale', '-E', 'UTF8'])
        run([pg / 'pg_ctl', '-D', cluster, '-l', root / 'postgres.log', '-o',
             f"-k {sock} -c listen_addresses=''", '-w', 'start'])
        try:
            run([commands['catalog_migrate']])
            sql("INSERT INTO drives(drive_id,display_name,mount_path) VALUES ('contract','Contract','/isolated');")
            # Recreate the pre-fix constraint and migration ledger in this
            # disposable database, seed existing v1/v2 data, then exercise the
            # actual forward migrations. Existing user recipes must not change.
            repo = Path(__file__).resolve().parents[1]
            old_constraint = (repo / 'rust/crates/catalog/migrations/0014_canonical_recipe_history.sql').read_text()
            sql('BEGIN;\n' + old_constraint +
                '\nDELETE FROM _sqlx_migrations WHERE version IN (17, 18, 19);\nCOMMIT;')
            history_case = json.loads((cases / 'history.json').read_text())['arguments']
            for version in (1, 2):
                provenance = json.loads(history_case[4])
                provenance['schema'] = f'photo-editor-edit-recipe-provenance:v{version}'
                if version == 1:
                    provenance.pop('history')
                asset = f'upgrade-v{version}'
                encoded = json.dumps(provenance).replace("'", "''")
                sql(f"INSERT INTO assets(asset_id,drive_id,root_path,relative_path,import_mode,"
                    f"capture_year,capture_month,capture_day) VALUES ('{asset}','contract','/isolated',"
                    f"'{asset}.png','referenced',2026,10,30);"
                    f"INSERT INTO canonical_edit_recipes_v13(asset_id,recipe_payload,cache_key,provenance_payload) "
                    f"VALUES ('{asset}','preserved-recipe','preserved-cache','{encoded}'::jsonb);")
            existing = "SELECT row_to_json(c)::text FROM canonical_edit_recipes_v13 c ORDER BY asset_id;"
            before_upgrade = sql(existing).stdout
            run([commands['catalog_migrate']])
            require(sql(existing).stdout == before_upgrade, 'Forward migration changed existing v1/v2 recipes')
            print('PASS: migrations 0017/0018/0019 preserve existing v1/v2 recipes exactly')
            contract_cases = {
                path.stem: json.loads(path.read_text())['arguments']
                for path in cases.glob('*.json')
            }
            upgrade_payloads = {
                3: json.loads(contract_cases['selection'][4]),
                5: json.loads(contract_cases['component'][4]),
                6: json.loads(contract_cases['layer-styles'][4]),
                7: json.loads(contract_cases['document-light'][4]),
            }
            version_four = copy.deepcopy(upgrade_payloads[3])
            version_four['schema'] = 'photo-editor-edit-recipe-provenance:v4'
            version_four['requiredCapabilities'] = ['smart-object-documents-v1']
            upgrade_payloads[4] = version_four
            for version, old_provenance in upgrade_payloads.items():
                asset = f'upgrade-v{version}'
                encoded = json.dumps(old_provenance).replace("'", "''")
                sql(f"INSERT INTO assets(asset_id,drive_id,root_path,relative_path,import_mode,"
                    f"capture_year,capture_month,capture_day) VALUES ('{asset}','contract','/isolated',"
                    f"'{asset}.png','referenced',2026,10,30);"
                    f"INSERT INTO canonical_edit_recipes_v13(asset_id,recipe_payload,cache_key,provenance_payload) "
                    f"VALUES ('{asset}','preserved-v{version}-recipe','preserved-v{version}-cache','{encoded}'::jsonb);")
            previous_constraint = (repo / 'rust/crates/catalog/migrations/0018_canonical_recipe_layer_style_light.sql').read_text()
            sql('BEGIN;\n' + previous_constraint +
                '\nDELETE FROM _sqlx_migrations WHERE version = 19;\nCOMMIT;')
            before_v8_upgrade = sql(existing).stdout
            run([commands['catalog_migrate']])
            require(sql(existing).stdout == before_v8_upgrade,
                    'Migration 0019 changed a v1-v7 provenance record')
            print('PASS: migration 0019 preserves existing v1-v7 recipes exactly')
            cases_written = sorted(
                cases.glob('*.json'),
                key=lambda path: path.stem == 'z-document-light-unlinked-updated')
            require(len(cases_written) == 9, 'Desktop contract cases are missing')
            seen = set()
            saved_assets = set()
            for path in cases_written:
                payload = json.loads(path.read_text())
                asset, legacy, recipe, cache, provenance_json = payload['arguments']
                provenance = json.loads(provenance_json)
                seen.add(provenance['schema'])
                if path.stem == 'document-light':
                    legacy_recipe = json.loads(legacy)
                    require(legacy_recipe.get('recipeVersion') == 20 and
                            legacy_recipe.get('documentLayerStyleLightAngle') == 123.5 and
                            any(layer.get('layerStyleLightLinked') is True
                                for layer in legacy_recipe.get('layers', [])),
                            'Desktop writer omitted linked-light recipe semantics')
                    require(provenance.get('schema') ==
                            'photo-editor-edit-recipe-provenance:v7' and
                            provenance.get('documentLayerStyleLightAngle') == 123.5 and
                            'document-layer-style-light-v1' in
                            provenance.get('requiredCapabilities', []),
                            'Canonical writer omitted the v7 linked-light capability')
                if path.stem in ('document-light-unlinked',
                                  'z-document-light-unlinked-updated'):
                    legacy_recipe = json.loads(legacy)
                    require(legacy_recipe.get('recipeVersion') == 20 and
                            legacy_recipe.get('documentLayerStyleLightAngle') in (123.5, 222.0) and
                            not any(layer.get('layerStyleLightLinked') is True
                                    for layer in legacy_recipe.get('layers', [])) and
                            provenance.get('schema') ==
                            'photo-editor-edit-recipe-provenance:v7' and
                            provenance.get('documentLayerStyleLightAngle') ==
                            legacy_recipe['documentLayerStyleLightAngle'],
                            'Desktop writer omitted the configured unlinked document angle')
                if path.stem == 'text-dimension':
                    legacy_recipe = json.loads(legacy)
                    text_layers = [layer for layer in legacy_recipe.get('layers', [])
                                   if 'textDimension' in layer]
                    require(legacy_recipe.get('recipeVersion') == 21 and
                            len(text_layers) == 1 and
                            text_layers[0]['textDimension'].get('version') == 1 and
                            provenance.get('schema') ==
                            'photo-editor-edit-recipe-provenance:v8' and
                            provenance.get('requiredCapabilities') ==
                            ['text-dimension-v1'],
                            'Desktop writer omitted the v8 text-dimension capability')
                if path.stem == 'text-dimension-linked-light':
                    legacy_recipe = json.loads(legacy)
                    capabilities = provenance.get('requiredCapabilities', [])
                    require(legacy_recipe.get('recipeVersion') == 21 and
                            any('textDimension' in layer
                                for layer in legacy_recipe.get('layers', [])) and
                            provenance.get('schema') ==
                            'photo-editor-edit-recipe-provenance:v8' and
                            'text-dimension-v1' in capabilities and
                            'document-layer-style-light-v1' in capabilities and
                            provenance.get('documentLayerStyleLightAngle') == 123.5,
                            'Desktop writer omitted composed v8 text-dimension and linked-light capabilities')
                before_save = stored(asset) if asset in saved_assets else None
                if before_save is None:
                    sql(f"INSERT INTO assets(asset_id,drive_id,root_path,relative_path,import_mode,"
                        f"capture_year,capture_month,capture_day) VALUES ('{asset}','contract','/isolated',"
                        f"'{asset}.png','referenced',2026,10,30);")
                else:
                    require(path.stem == 'z-document-light-unlinked-updated',
                            f'Unexpected duplicate canonical save fixture for {asset}')
                save(payload)
                expected = stored(asset)
                saved_assets.add(asset)
                require(expected['edit_recipe'] == json.loads(legacy), 'Legacy edit data changed during save')
                canonical = expected['canonical_edit_recipe_v13']
                require(canonical['recipe_payload'] == recipe and canonical['cache_key'] == cache,
                        'Canonical recipe bytes or cache identity changed during save')
                require(json.loads(canonical['provenance_payload']) == provenance,
                        'Selection, history, presets or required capabilities changed during save')
                if before_save is not None:
                    previous_canonical = before_save['canonical_edit_recipe_v13']
                    require(previous_canonical['recipe_payload'] == canonical['recipe_payload'] and
                            previous_canonical['cache_key'] == canonical['cache_key'] and
                            previous_canonical['provenance_payload'] != canonical['provenance_payload'] and
                            before_save['edit_recipe'] != expected['edit_recipe'] and
                            json.loads(canonical['provenance_payload']).get(
                                'documentLayerStyleLightAngle') == 222.0,
                            'A same-cache-key save failed to persist a changed unlinked document angle')
                saved = root / 'saved.json'
                saved.write_text(json.dumps(expected))
                run([desktop, '--verify-catalog-contract', cases, path.stem, saved])
                # Both validation layers must reject malformed envelopes without
                # changing the last successful recipe pair.
                invalid_values = []
                for field in ('schema', 'presetApplications', 'history'):
                    invalid = copy.deepcopy(provenance)
                    invalid.pop(field)
                    invalid_values.append(invalid)
                invalid = copy.deepcopy(provenance)
                invalid['unexpected'] = True
                invalid_values.append(invalid)
                invalid = copy.deepcopy(provenance)
                invalid['schema'] = 'photo-editor-edit-recipe-provenance:v999'
                invalid_values.append(invalid)
                for value in (None, {}, {'activeBranchId': ' ', 'snapshots': []},
                              {'activeBranchId': 'main', 'snapshots': {}},
                              {'activeBranchId': 'main', 'snapshots': [], 'unknown': True}):
                    invalid = copy.deepcopy(provenance)
                    invalid['history'] = value
                    invalid_values.append(invalid)
                if 'selectionState' in provenance:
                    for value in (None, [], 'invalid'):
                        invalid = copy.deepcopy(provenance)
                        invalid['selectionState'] = value
                        invalid_values.append(invalid)
                if 'requiredCapabilities' in provenance:
                    invalid = copy.deepcopy(provenance)
                    invalid.pop('requiredCapabilities')
                    invalid_values.append(invalid)
                    for value in (None, [], ['unknown-capability'], ['smart-object-documents-v1', 'unknown']):
                        invalid = copy.deepcopy(provenance)
                        invalid['requiredCapabilities'] = value
                        invalid_values.append(invalid)
                if provenance.get('schema') == 'photo-editor-edit-recipe-provenance:v7':
                    invalid = copy.deepcopy(provenance)
                    invalid.pop('documentLayerStyleLightAngle')
                    invalid_values.append(invalid)
                if provenance.get('schema') == 'photo-editor-edit-recipe-provenance:v8':
                    invalid = copy.deepcopy(provenance)
                    invalid['requiredCapabilities'] = [
                        item for item in provenance['requiredCapabilities']
                        if item != 'text-dimension-v1']
                    invalid_values.append(invalid)
                    invalid = copy.deepcopy(provenance)
                    invalid['requiredCapabilities'] = provenance['requiredCapabilities'] + [
                        'text-dimension-v1']
                    invalid_values.append(invalid)
                    if 'documentLayerStyleLightAngle' in provenance:
                        invalid = copy.deepcopy(provenance)
                        invalid['documentLayerStyleLightAngle'] = 3600.1
                        invalid_values.append(invalid)
                        invalid = copy.deepcopy(provenance)
                        invalid.pop('documentLayerStyleLightAngle')
                        invalid_values.append(invalid)
                    for value in (None, 3600.1, -3600.1):
                        invalid = copy.deepcopy(provenance)
                        invalid['documentLayerStyleLightAngle'] = value
                        invalid_values.append(invalid)
                    if 'document-layer-style-light-v1' in provenance['requiredCapabilities']:
                        invalid = copy.deepcopy(provenance)
                        invalid['requiredCapabilities'] = [
                            item for item in provenance['requiredCapabilities']
                            if item != 'document-layer-style-light-v1']
                        invalid_values.append(invalid)
                for invalid in invalid_values:
                    changed = copy.deepcopy(payload)
                    changed['arguments'][4] = json.dumps(invalid)
                    require(save(changed, success=False).returncode != 0,
                            f'Helper accepted invalid provenance: {invalid}')
                    encoded = json.dumps(invalid).replace("'", "''")
                    result = sql(f"UPDATE canonical_edit_recipes_v13 SET provenance_payload='{encoded}'::jsonb "
                                 f"WHERE asset_id='{asset}';", success=False)
                    require(result.returncode != 0 and 'provenance_schema_check' in result.stderr,
                            f'Database accepted invalid provenance: {invalid}')
                require(stored(asset) == expected, 'Rejected save modified the stored recipe pair')
                # Reopening/migrating again must retain the saved data exactly.
                run([commands['catalog_migrate']])
                require(stored(asset) == expected, 'Repeated migration changed the stored recipe')
                print(f'PASS: {path.stem}: real desktop -> helper -> PostgreSQL -> desktop reopen')
            expected_schemas = {
                f'photo-editor-edit-recipe-provenance:v{version}'
                for version in (2, 3, 5, 6, 7, 8)}
            require(seen == expected_schemas,
                    f'Production serializer schema coverage changed; expected {sorted(expected_schemas)}, got {sorted(seen)}')
        finally:
            run([pg / 'pg_ctl', '-D', cluster, '-m', 'fast', '-w', 'stop'])
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
