#!/usr/bin/env python3
"""Keep the reviewed desktop edit-admission inventory in sync with QML APIs.

This coverage check supplements runtime workflow tests. It does not prove that
an exemption is side-effect free; new entries must be reviewed as product policy.
"""
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    manifest = json.loads((ROOT / 'release/customer-edit-admission.json').read_text())
    errors = []
    for stem, policy in manifest.items():
        header = (ROOT / 'apps/desktop/src' / (stem + '.h')).read_text()
        names = set(re.findall(r'\bWRITE\s+(\w+)', header))
        names.update(name for name, const in re.findall(
            r'Q_INVOKABLE\s+[\w:*<>]+\s+(\w+)\s*\([^;]*?\)\s*(const)?\s*;', header) if not const)
        if names != policy.keys():
            errors.append(f'{stem}: unreviewed {sorted(names-policy.keys())}; removed {sorted(policy.keys()-names)}')
        source = '\n'.join(p.read_text() for p in (ROOT / 'apps/desktop/src').glob(stem + '*.cpp'))
        if stem == 'editor_preview_controller':
            source += (ROOT / 'apps/desktop/src/editor_rendering_migration.inc').read_text()
        cls = ''.join(word.title() for word in stem.split('_'))
        bodies = {match[1]: match[2] for match in re.finditer(
            cls + r'::(\w+)\s*\([^;{}]*?\)\s*\{([^{}]{0,220})', source)}
        for name, mode in policy.items():
            if mode == 'read':
                continue
            expected = 'enter()' if mode == 'edit' else 'restore()'
            if 'customer_edit_access_.' + expected not in bodies.get(name, ''):
                errors.append(f'{stem}::{name}: missing {mode} admission before mutation')
    if errors:
        print('\n'.join(errors), file=sys.stderr)
        return 1
    print('Reviewed customer admission covers all exposed photo, board, timeline and scene mutation APIs.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
