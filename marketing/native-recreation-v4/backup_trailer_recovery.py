"""APFS-only backup of native recovery state; never changes or closes Looksy."""
from pathlib import Path
import argparse
import ctypes
import datetime
import hashlib
import json
import os
import subprocess
import tempfile
import uuid

HERE = Path(__file__).resolve().parent


def sha(path):
    with path.open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data-root', type=Path, required=True)
    parser.add_argument('--timeline-id', required=True)
    parser.add_argument('--document-hash', required=True)
    parser.add_argument('--hash-tool', type=Path, required=True)
    parser.add_argument('--compound-child-id')
    parser.add_argument('--receipt', type=Path, required=True)
    args = parser.parse_args()
    data = args.data_root.resolve(strict=True)
    if (data.parent.parent != Path('/private/tmp') or
            not data.parent.name.startswith('looksy-native-repaired')):
        raise ValueError('Back up only the explicitly isolated trailer data root.')
    if str(uuid.UUID(args.timeline_id)) != args.timeline_id:
        raise ValueError('Use the canonical native Timeline UUID.')
    if len(args.document_hash) != 64 or any(c not in '0123456789abcdef' for c in args.document_hash):
        raise ValueError('Use the exact native document SHA-256.')
    if args.receipt.exists():
        raise ValueError('Preserve the existing backup receipt; choose a fresh filename.')

    relative_autosave = Path('timelines/documents') / (args.timeline_id + '.json.autosave')

    def inspect(path):
        result = subprocess.run([str(args.hash_tool), str(path)], check=True,
                                capture_output=True, text=True)
        body = json.loads(result.stdout)
        if body['timelineId'] != args.timeline_id or body['documentHash'] != args.document_hash:
            raise RuntimeError('The native checkpoint differs from the exact live document hash.')
        return body

    original = inspect(data / relative_autosave)
    backup = Path(tempfile.mkdtemp(prefix='looksy-native-complete-recovery-', dir='/private/tmp'))
    os.chmod(backup, 0o700)
    library = ctypes.CDLL('/usr/lib/libSystem.B.dylib', use_errno=True)
    library.clonefile.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int]
    library.clonefile.restype = ctypes.c_int
    manifest = []

    def clone(source, relative):
        if source.is_symlink() or not source.is_file():
            raise RuntimeError('Recovery backup requires regular, nonsymlink source files: ' + str(source))
        target = backup / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        if library.clonefile(os.fsencode(source), os.fsencode(target), 0) != 0:
            raise OSError(ctypes.get_errno(), 'APFS clone failed; no full-copy fallback is permitted.')
        expected = sha(target)
        if sha(source) != expected:
            raise RuntimeError('A recovery file changed during backup: ' + str(source))
        manifest.append(dict(source=str(source), backup=str(target), sha256=expected,
                             fileBytes=target.stat().st_size))

    for name in ('timelines', 'agent-tasks'):
        directory = data / name
        if not directory.is_dir() or directory.is_symlink():
            raise RuntimeError('The native recovery directory is unavailable: ' + str(directory))
        for current, directories, files in os.walk(directory, followlinks=False):
            current = Path(current)
            if any((current / name).is_symlink() for name in directories):
                raise RuntimeError('Recovery directories must not contain symlink descendants.')
            for name in sorted(files):
                source = current / name
                clone(source, source.relative_to(data))

    if args.compound_child_id:
        child = args.compound_child_id
        if str(uuid.UUID(child)) != child:
            raise ValueError('Use the canonical task-owned compound child UUID.')
        for suffix in ('.json', '.mov', '.producer.json'):
            source = HERE / 'projects' / ('compound-' + child + suffix)
            clone(source, Path('task-owned-outputs') / source.name)

    copied = inspect(backup / relative_autosave)
    final_source = inspect(data / relative_autosave)
    if not original['rawSha256'] == copied['rawSha256'] == final_source['rawSha256']:
        raise RuntimeError('The live native autosave changed; this is not a restart-qualified backup.')
    for row in manifest:
        if sha(Path(row['source'])) != row['sha256']:
            raise RuntimeError('A source changed before backup verification finished; do not restart.')

    record = dict(schema='looksy.native-recovery-backup/v1', verified=True,
                  method='APFS clones with full per-file SHA-256 and exact Qt-native document hash',
                  createdAt=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                  sourceDataRoot=str(data), backupRoot=str(backup),
                  timelineId=args.timeline_id, documentHash=args.document_hash,
                  autosave=original, fileCount=len(manifest),
                  logicalBytes=sum(row['fileBytes'] for row in manifest), files=manifest,
                  scope='Native Timeline history, autosaves, task journals and completed compound outputs; source media remain at their original paths. No database migration or process closure performed.')
    with args.receipt.open('x') as output:
        json.dump(record, output, indent=2)
        output.write('\n')
    print(json.dumps({k: record[k] for k in ('verified', 'backupRoot', 'documentHash', 'fileCount', 'logicalBytes')}, indent=2))


if __name__ == '__main__':
    main()
