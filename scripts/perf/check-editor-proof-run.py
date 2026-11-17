#!/usr/bin/env python3
"""Check GitHub's workflow-run provenance before downloading native proof."""
import argparse
import json
from pathlib import Path


def validate(run, repository, commit, run_id):
    expected = {
        'id': int(run_id), 'head_sha': commit, 'head_branch': 'main',
        'event': 'workflow_dispatch', 'status': 'completed', 'conclusion': 'success',
        'path': '.github/workflows/editor-native-regression.yml',
    }
    for key, value in expected.items():
        if run.get(key) != value:
            raise ValueError(f'native proof workflow {key}: expected {value!r}, found {run.get(key)!r}')
    for key in ('repository', 'head_repository'):
        if run.get(key, {}).get('full_name') != repository:
            raise ValueError(f'native proof {key} does not match this repository')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--metadata', type=Path, required=True)
    p.add_argument('--repository', required=True)
    p.add_argument('--commit', required=True)
    p.add_argument('--run-id', required=True, type=int)
    a = p.parse_args()
    try:
        validate(json.loads(a.metadata.read_text()), a.repository, a.commit, a.run_id)
    except (ValueError, KeyError, TypeError, OSError) as error:
        p.exit(1, f'native proof provenance rejected: {error}\n')
    print('editor_native_proof_provenance=passed')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
