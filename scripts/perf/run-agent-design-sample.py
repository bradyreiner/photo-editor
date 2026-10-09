#!/usr/bin/env python3
"""Produce a reference or unrestricted model design using Looksy's native tools.

The opt-in native driver keeps the user's catalog and model manifest untouched.
Reference mode is authored by the harness and never counts as agent autonomy.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile


def file_sha256(path):
    try:
        return hashlib.sha256(path.read_bytes()).hexdigest()
    except OSError:
        return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--mode', choices=['reference', 'model'], required=True)
    parser.add_argument('--binary', type=Path, default=Path('build/dev-macos-debug/tests/agent_thumbnail_live_proof'))
    parser.add_argument('--model', default='qwen3.6:35b')
    parser.add_argument('--review-reference', action='store_true')
    args = parser.parse_args()
    source, binary, output = args.source.resolve(), args.binary.resolve(), args.output.resolve()
    if not source.is_file() or not binary.is_file() or output.exists():
        parser.error('Source and built driver must exist; output must be a new directory.')
    source_sha = file_sha256(source)
    binary_sha = file_sha256(binary)
    if source_sha is None or binary_sha is None:
        parser.error('Source and built driver must remain readable for the full proof run.')
    env = os.environ.copy()
    for key in list(env):
        if key.startswith(('PHOTO_EDITOR_FAKE_', 'PHOTO_EDITOR_TEST_FAKE_')):
            env.pop(key)
    # Keep runtime artifacts for inspection. Never write the user's manifest.
    runtime = Path(tempfile.mkdtemp(prefix='looksy-design-sample-'))
    model_id = 'sample-installed-vision-model'
    model = dict(id=model_id, name=args.model, backend='ollama', runtime='external',
                 endpoint='http://127.0.0.1:11434', model=args.model, vision=True,
                 status='available', requestTimeoutMs=-1, contextWindow=16384,
                 maxImageEdge=1536, planningImageEdge=1024,
                 planningMaxOutputTokens=2048, toolCallPlanning=True)
    (runtime / 'agent-models').mkdir()
    (runtime / 'agent-models/models.json').write_text(json.dumps({'models': [model]}, indent=2) + '\n')
    env.update(QT_QPA_PLATFORM='cocoa', PHOTO_EDITOR_DESIGN_SAMPLE_DIR=str(output),
               PHOTO_EDITOR_DESIGN_SAMPLE_RUNTIME=str(runtime), PHOTO_EDITOR_DESIGN_SAMPLE_SOURCE=str(source))
    env.pop('PHOTO_EDITOR_THUMBNAIL_PROOF_DIR', None)
    env.pop('PHOTO_EDITOR_DESIGN_SAMPLE_REFERENCE', None)
    env.pop('PHOTO_EDITOR_DESIGN_SAMPLE_MODEL', None)
    if args.mode == 'reference':
        env['PHOTO_EDITOR_DESIGN_SAMPLE_REFERENCE'] = '1'
    if args.mode == 'model' or args.review_reference:
        env['PHOTO_EDITOR_DESIGN_SAMPLE_MODEL'] = model_id
    with (runtime / 'run.log').open('w') as log:
        try:
            result = subprocess.run([str(binary)], env=env, stdout=log, stderr=subprocess.STDOUT)
            exit_code = result.returncode
        except KeyboardInterrupt:
            exit_code = 130
    source_unchanged = file_sha256(source) == source_sha
    binary_unchanged = file_sha256(binary) == binary_sha
    receipt_path = output / 'run-receipt.json'
    try:
        receipt = json.loads(receipt_path.read_text())
        receipt_loaded = isinstance(receipt, dict)
    except (OSError, json.JSONDecodeError):
        receipt = {}
        receipt_loaded = False
    receipt_mode = 'host-authored-native-tool-reference' if args.mode == 'reference' else 'unrestricted-local-model-construction'
    receipt_valid = (
        receipt_loaded
        and receipt.get('contract') == 'agent-design-sample-v1'
        and receipt.get('mode') == receipt_mode
        and receipt.get('passed') is True
        and receipt.get('sourceSha256') == source_sha
        and receipt.get('sourceUnchanged') is True
        and source_unchanged
        and receipt.get('recipeReopenPixelIdentical') is True
        and receipt.get('wholeTaskRevertPassed') is True
        and (args.mode == 'reference' or (
            receipt.get('modelChoseEveryConstructionCall') is True
            and receipt.get('modelDeclaredComplete') is True
            and receipt.get('visualReviewPassed') is True
        ))
    )
    if not source_unchanged or not binary_unchanged or (exit_code == 0 and not receipt_valid):
        exit_code = 1
    output.mkdir(parents=True, exist_ok=True)
    (output / 'run.log').write_bytes((runtime / 'run.log').read_bytes())
    provenance = dict(mode=args.mode, exitCode=exit_code, binaryPath=str(binary),
                      binarySha256=binary_sha,
                      binaryUnchangedDuringRun=binary_unchanged,
                      sourcePath=str(source), sourceSha256=source_sha,
                      sourceUnchanged=source_unchanged, runReceiptValid=receipt_valid,
                      runReceiptPath=str(receipt_path),
                      privateRuntime=str(runtime), userCatalogUsed=False,
                      actualApplicationUiQualified=False)
    (output / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
    print(json.dumps(provenance))
    return exit_code


if __name__ == '__main__':
    raise SystemExit(main())
