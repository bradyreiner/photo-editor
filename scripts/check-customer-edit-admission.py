#!/usr/bin/env python3
"""Audit admission coverage for every QML-exposed document mutation API.

The manifest is the policy review record. This parser checks that those APIs
remain present, that direct admission precedes recognized mutations, and that
explicitly reviewed delegates resolve to a guarded API without a call cycle.
"""
import argparse
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
API_PREFIX = re.compile(r'\b(?:set|add|remove|delete|create|clear|update|apply|move|insert|erase|rename|restore|commit|undo|redo|paste|group|ungroup|split|trim|lift|overwrite|extract|duplicate|assign|toggle|replace|nudge|roll|slip|normalize|relink|import|begin|finish|close|reset|cut|queue|schedule|retry|refine|bake|align|distribute|pick|select|copy|save|load|accept|reject|advance|reopen|regenerate|retry|cancel|end|append|link|unlink|isolate|mask|object|cleanup|sample|write|persist|acknowledge)[A-Z_][A-Za-z0-9_]*\s*\(')
MUTATING_CALLS = re.compile(
    r'\b(?:pushUndoSnapshot|setDirty|appendEditJournalEntry|submit[A-Za-z0-9_]*(?:Edit|Mutation)[A-Za-z0-9_]*|'
    r'commit[A-Z]\w*|apply[A-Z]\w*|add[A-Z]\w*|remove[A-Z]\w*|delete[A-Z]\w*|create[A-Z]\w*|'
    r'clear[A-Z]\w*|update[A-Z]\w*|move[A-Z]\w*|insert[A-Z]\w*|erase[A-Z]\w*|rename[A-Z]\w*|'
    r'restore[A-Z]\w*|write[A-Z]\w*|persist[A-Z]\w*|queue[A-Z]\w*|set[A-Z]\w*)\s*\('
)
MEMBER_WRITE = re.compile(r'\b(?:this\s*->\s*)?[A-Za-z_]\w*_\s*(?:=(?!=)|\+=|-=|\*=|/=|\+\+|--)')


def policy_entry(value):
    if isinstance(value, str):
        return {'mode': value, 'delegates': []}
    if isinstance(value, dict) and value.get('mode') in {'read', 'edit', 'restore', 'finish'}:
        delegates = value.get('delegates', [])
        if isinstance(delegates, list) and all(isinstance(name, str) for name in delegates):
            return {'mode': value['mode'], 'delegates': delegates}
    raise ValueError(f'invalid admission policy entry: {value!r}')


def mask_comments_and_literals(source):
    """Blank C++ comments and literals while preserving offsets and braces."""
    result = list(source)
    i = 0
    state = 'code'
    quote = ''
    while i < len(source):
        if state == 'code':
            if source.startswith('//', i):
                result[i] = result[i + 1] = ' '
                i += 2
                state = 'line'
                continue
            if source.startswith('/*', i):
                result[i] = result[i + 1] = ' '
                i += 2
                state = 'block'
                continue
            if source[i] in ('"', "'"):
                quote = source[i]
                result[i] = ' '
                i += 1
                state = 'string'
                continue
            i += 1
            continue
        if state == 'line':
            if source[i] == '\n':
                state = 'code'
            else:
                result[i] = ' '
            i += 1
            continue
        if state == 'block':
            if source.startswith('*/', i):
                result[i] = result[i + 1] = ' '
                i += 2
                state = 'code'
            else:
                if source[i] != '\n':
                    result[i] = ' '
                i += 1
            continue
        if state == 'string':
            if source[i] == '\\':
                result[i] = ' '
                if i + 1 < len(source):
                    if source[i + 1] != '\n':
                        result[i + 1] = ' '
                    i += 2
                else:
                    i += 1
            elif source[i] == quote:
                result[i] = ' '
                i += 1
                state = 'code'
            else:
                if source[i] != '\n':
                    result[i] = ' '
                i += 1
    return ''.join(result)


def matching(source, start, opening, closing):
    depth = 0
    for index in range(start, len(source)):
        if source[index] == opening:
            depth += 1
        elif source[index] == closing:
            depth -= 1
            if depth == 0:
                return index
    return None


def exposed_methods(header):
    names = set(re.findall(r'\bWRITE\s+(\w+)', header))
    clean = mask_comments_and_literals(header)
    for match in re.finditer(r'\bQ_INVOKABLE\b', clean):
        semicolon = clean.find(';', match.end())
        if semicolon < 0:
            continue
        declaration = clean[match.end():semicolon]
        method = re.search(r'\b(\w+)\s*\([^;]*\)\s*(const\s*)?(?:noexcept\s*)?$', declaration, re.S)
        if method and not method.group(2):
            names.add(method.group(1))
    return names


def cpp_methods(source, class_name):
    clean = mask_comments_and_literals(source)
    found = {}
    signature = re.compile(r'\b' + re.escape(class_name) + r'::(\w+)\s*\(')
    for match in signature.finditer(clean):
        paren_start = clean.find('(', match.start())
        paren_end = matching(clean, paren_start, '(', ')')
        if paren_end is None:
            continue
        cursor = paren_end + 1
        while cursor < len(clean) and clean[cursor].isspace():
            cursor += 1
        while clean.startswith(('const', 'noexcept', 'override', 'final'), cursor):
            token = re.match(r'(?:const|noexcept|override|final)\b', clean[cursor:])
            if not token:
                break
            cursor += token.end()
            while cursor < len(clean) and clean[cursor].isspace():
                cursor += 1
        # A trailing return type or attributes may appear before the body.
        if cursor < len(clean) and clean[cursor] not in '{':
            body_start = clean.find('{', cursor)
            delimiter = min((pos for pos in (clean.find(';', cursor), clean.find('\n\n', cursor)) if pos >= 0), default=len(clean))
            if body_start < 0 or body_start > delimiter:
                continue
            cursor = body_start
        if cursor >= len(clean) or clean[cursor] != '{':
            continue
        body_end = matching(clean, cursor, '{', '}')
        if body_end is not None:
            found[match.group(1)] = {
                'body': source[cursor + 1:body_end],
                'clean_body': clean[cursor + 1:body_end],
            }
    return found


def source_for(root, stem):
    source_root = root / 'apps/desktop/src'
    paths = sorted(source_root.glob(stem + '*.cpp'))
    text = '\n'.join(path.read_text() for path in paths)
    if stem == 'editor_preview_controller':
        migration = source_root / 'editor_rendering_migration.inc'
        if migration.exists():
            text += '\n' + migration.read_text()
    return text


def admission_call(body, mode):
    allowed = {'edit': ('enter',), 'restore': ('restore',), 'finish': ('restore',)}.get(mode, ())
    for method in allowed:
        found = re.search(r'customer_edit_access_\s*\.\s*' + method + r'\s*\(\s*\)', body)
        if found:
            return found.start()
    return None


def mutation_before_guard(body, guard_position, api_modes):
    before = body[:guard_position]
    probes = [MEMBER_WRITE.search(before)]
    for mutating_call in MUTATING_CALLS.finditer(before):
        if not re.match(r'setStatus\s*\(', mutating_call.group()):
            probes.append(mutating_call)
            break
    for call in API_PREFIX.finditer(before):
        name = re.match(r'\w+', call.group()).group()
        if api_modes.get(name) in {'edit', 'restore'} and name != 'setStatus':
            probes.append(call)
            break
    hits = [hit for hit in probes if hit]
    return min(hits, key=lambda hit: hit.start()) if hits else None


def audit(root=ROOT, manifest_path=None):
    manifest_path = manifest_path or (root / 'release/customer-edit-admission.json')
    manifest = json.loads(manifest_path.read_text())
    errors = []
    bodies = {}
    functions = {}
    policies = {}
    for stem, section in manifest.items():
        header_path = root / 'apps/desktop/src' / (stem + '.h')
        if not header_path.exists():
            errors.append(f'{stem}: exposed-method header is missing')
            continue
        declared = exposed_methods(header_path.read_text())
        normalized = {}
        for name, entry in section.items():
            try:
                normalized[name] = policy_entry(entry)
            except ValueError as error:
                errors.append(f'{stem}::{name}: {error}')
        policies[stem] = normalized
        if declared != normalized.keys():
            errors.append(f'{stem}: unreviewed {sorted(declared-normalized.keys())}; removed {sorted(normalized.keys()-declared)}')
        class_name = ''.join(word.title() for word in stem.split('_'))
        implementations = cpp_methods(source_for(root, stem), class_name)
        functions.update({(stem, name): function for name, function in implementations.items()})
        bodies.update({(stem, name): implementations[name] for name in normalized if name in implementations})

    api_modes = {name: entry['mode'] for section in policies.values() for name, entry in section.items()}
    for stem, section in policies.items():
        for name, entry in section.items():
            mode = entry['mode']
            if mode == 'read':
                continue
            key = (stem, name)
            function = bodies.get(key)
            if not function:
                errors.append(f'{stem}::{name}: implementation body not found')
                continue
            guard = admission_call(function['clean_body'], mode)
            if guard is not None:
                mutation = mutation_before_guard(function['clean_body'], guard, api_modes)
                if mutation:
                    line = function['clean_body'][:mutation.start()].count('\n') + 1
                    errors.append(f'{stem}::{name}: mutation may occur before {mode} admission (body line {line})')
                continue
            delegates = entry['delegates']
            if not delegates:
                errors.append(f'{stem}::{name}: missing {mode} admission or reviewed delegate')
            else:
                calls = set(re.findall(r'\b([A-Za-z_]\w*)\s*\(', function['clean_body']))
                missing = sorted(set(delegates) - calls)
                if missing:
                    errors.append(f'{stem}::{name}: reviewed delegate is not called: {missing}')

    # Resolve each delegate graph explicitly. Every path must terminate at a
    # method with a direct guard, with cycles and unreviewed targets rejected.
    def resolves(key, trail):
        if key in trail:
            return False, 'delegate cycle'
        stem, name = key
        entry = policies.get(stem, {}).get(name)
        function = functions.get(key)
        if not function or (entry and entry['mode'] == 'read'):
            return False, 'delegate target is not a reviewed mutation API'
        mode = entry['mode'] if entry else 'edit'
        if admission_call(function['clean_body'], mode) is not None:
            return True, ''
        targets = entry['delegates'] if entry else []
        if not targets:
            return False, 'delegate target has no direct admission guard'
        for target in targets:
            target_key = (stem, target)
            ok, reason = resolves(target_key, trail | {key})
            if not ok:
                return False, f'{target}: {reason}'
        return True, ''

    for stem, section in policies.items():
        for name, entry in section.items():
            if entry['mode'] == 'read' or not entry['delegates']:
                continue
            ok, reason = resolves((stem, name), set())
            if not ok:
                errors.append(f'{stem}::{name}: {reason}')
    return errors


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, default=ROOT)
    parser.add_argument('--manifest', type=Path)
    args = parser.parse_args()
    errors = audit(args.root.resolve(), args.manifest.resolve() if args.manifest else None)
    if errors:
        print('\n'.join(errors), file=sys.stderr)
        return 1
    print('Reviewed customer admission covers all exposed photo, board, timeline and scene mutation APIs.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
