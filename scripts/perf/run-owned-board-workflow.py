#!/usr/bin/env python3
"""Run the native Board journey with a disposable, explicitly owned catalog."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def run(app, proof, helper, timeout=600):
    app, helper, proof = app.resolve(), helper.resolve(), proof.resolve()
    if not app.is_file() or not helper.is_file():
        raise ValueError('Built test executable and catalog_local_db helper are required')
    if proof.exists():
        raise ValueError('A fresh proof directory is required')
    proof.mkdir(parents=True)
    runtime = Path(tempfile.mkdtemp(prefix='looksy-board-proof-'))
    env = {key: value for key, value in os.environ.items() if not key.startswith('PHOTO_')}
    env.update(PHOTO_EDITOR_APP_DATA_DIR=str(runtime / 'app-data'),
               PHOTO_EDITOR_APP_DATA_ROOT=str(runtime / 'app-data'),
               PHOTO_EDITOR_CACHE_DIR=str(runtime / 'cache'),
               PHOTO_EDITOR_REPOSITORY_ROOT=str(ROOT))
    for name in ('app-data', 'cache'):
        (runtime / name).mkdir()
    child = None
    result = {'schema': 'looksy-owned-board-catalog:v1', 'status': 'failed', 'catalogStopped': False}
    def catalog(command, log):
        process = subprocess.run([str(helper), *command], env=env, capture_output=True,
                                 text=True, timeout=90, check=False)
        # The throwaway connection password has no place in retained logs.
        text = process.stdout + process.stderr
        (proof / log).write_text(re.sub(r'postgres(?:ql)?://\S+', 'postgres://[disposable-endpoint]', text))
        if process.returncode:
            raise RuntimeError(f'Owned catalog {command[0]} failed; see {proof / log}')
        return process.stdout
    try:
        output = catalog(['connection-url', '--ensure-running'], 'catalog-start.log')
        urls = [line.strip() for line in output.splitlines() if line.startswith(('postgres://', 'postgresql://'))]
        if len(urls) != 1:
            raise ValueError('Owned catalog returned no unique connection URL')
        ui_env = dict(env, QT_QPA_PLATFORM='cocoa', QSG_RHI_BACKEND='metal',
                      PHOTO_EDITOR_SMOKE_NATIVE_POINTER='1',
                      PHOTO_EDITOR_SMOKE_NATIVE_TEXTURE='1',
                      PHOTO_EDITOR_SMOKE_STOP_AFTER_BLANK_CANVAS='1',
                      PHOTO_EDITOR_SMOKE_STOP_AFTER_EDIT_BOARDS='1',
                      PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE='1',
                      PHOTO_EDITOR_CANVAS_PROOF_DATABASE_URL=urls[0],
                      PHOTO_EDITOR_VISUAL_PROOF_DIR=str(proof))
        with (proof / 'process.log').open('w') as log:
            child = subprocess.Popen([str(app)], env=ui_env, stdout=log, stderr=subprocess.STDOUT)
            result['processId'] = child.pid
            try:
                result['exitCode'] = child.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                child.kill()  # Only this invocation's disposable test process.
                child.wait()
                raise
        if result['exitCode']:
            raise RuntimeError(f'Board journey failed; see {proof / "process.log"}')
        result['status'] = 'passed'
    finally:
        try:
            catalog(['stop'], 'catalog-stop.log')
            result['catalogStopped'] = True
            shutil.rmtree(runtime)
        finally:
            if not result['catalogStopped']:
                # Preserve a possibly live catalog rather than delete its data.
                result['retainedRuntime'] = str(runtime)
                result['status'] = 'failed'
            (proof / 'owned-catalog.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=Path, required=True)
    parser.add_argument('--proof-dir', type=Path, required=True)
    parser.add_argument('--helper', type=Path)
    args = parser.parse_args()
    helper = args.helper or next((path for path in (
        ROOT / 'rust/target/release/catalog_local_db', ROOT / 'rust/target/debug/catalog_local_db') if path.is_file()), None)
    try:
        if helper is None:
            raise ValueError('Build the catalog_local_db helper first')
        run(args.app, args.proof_dir, helper)
        return 0
    except (ValueError, RuntimeError, OSError, subprocess.SubprocessError) as error:
        print(str(error), file=__import__('sys').stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
