"""Replace identical old test-bundle resource files with APFS COW clones.

No application executable, project, media, catalog or recovery data is touched.
Every replacement must preserve the original file bytes, mode and timestamps.
"""
from pathlib import Path
import ctypes
import hashlib
import json
import os
import re
import stat
import subprocess
import uuid

HERE = Path(__file__).resolve().parent


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    receipt_path = HERE / 'evidence/native-v18-packaged-runtime-deduplication.json'
    if receipt_path.exists():
        raise RuntimeError('Preserve the existing deduplication receipt.')
    records = []
    protected = {}
    for path in (HERE / 'evidence').glob('isolated-native-repaired*-build.json'):
        match = re.fullmatch(r'isolated-native-repaired(\d*)-build.json', path.name)
        if not match or int(match.group(1) or '1') >= 10:
            continue
        record = json.loads(path.read_text())
        if not record.get('isolatedBinary'):
            continue  # An incomplete build has no validated application bundle.
        runtime = Path(record['runtimeRoot'])
        if runtime.parent != Path('/private/tmp') or not runtime.name.startswith('looksy-native-repaired'):
            raise RuntimeError('Unexpected test build root.')
        resources = runtime / 'bin/Looksy.app/Contents/Resources'
        if not resources.is_dir() or resources.is_symlink():
            continue
        binary = runtime / 'bin/Looksy.app/Contents/MacOS/Looksy'
        protected[str(binary)] = sha(binary)
        records.append((int(match.group(1) or '1'), resources))
    live = subprocess.run(['/bin/ps', '-axo', 'comm='], check=True,
                          capture_output=True, text=True).stdout.splitlines()
    live = {line.strip() for line in live}
    for version, resources in records:
        if version < 9 and str(resources.parent / 'MacOS/Looksy') in live:
            raise RuntimeError('An older test app is live; do not deduplicate its bundle.')

    library = ctypes.CDLL('/usr/lib/libSystem.B.dylib', use_errno=True)
    library.clonefile.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int]
    library.clonefile.restype = ctypes.c_int
    pool = {}
    replacements = []
    before_free = os.statvfs('/private/tmp').f_bavail * os.statvfs('/private/tmp').f_frsize
    for version, resources in sorted(records, reverse=True):
        for current, directories, files in os.walk(resources, followlinks=False):
            current = Path(current)
            directories[:] = [name for name in directories if not (current / name).is_symlink()]
            for name in sorted(files):
                target = current / name
                if target.is_symlink() or not target.is_file():
                    continue
                relative = target.relative_to(resources)
                if relative.parts[0] not in {'models', 'search-runtime', 'agent-runtime'}:
                    continue
                original_stat = target.stat()
                if original_stat.st_size < 64 * 1024:
                    continue
                original_hash = sha(target)
                key = (str(relative), original_stat.st_size, original_hash)
                source = pool.get(key)
                if source is None:
                    pool[key] = target
                    continue
                if version >= 9:
                    continue  # Keep the latest prior user-used bundle entirely untouched.
                temporary = target.with_name(target.name + '.looksy-cow-' + uuid.uuid4().hex)
                try:
                    if library.clonefile(os.fsencode(source), os.fsencode(temporary), 0) != 0:
                        raise OSError(ctypes.get_errno(), 'APFS clone failed; no copy fallback.')
                    if sha(temporary) != original_hash:
                        raise RuntimeError('The canonical runtime changed during cloning.')
                    current_stat = target.stat()
                    if (current_stat.st_ino, current_stat.st_mtime_ns, current_stat.st_size) != (
                            original_stat.st_ino, original_stat.st_mtime_ns, original_stat.st_size):
                        raise RuntimeError('The old resource changed concurrently; no replacement performed.')
                    os.chmod(temporary, stat.S_IMODE(original_stat.st_mode))
                    os.utime(temporary, ns=(original_stat.st_atime_ns, original_stat.st_mtime_ns))
                    os.replace(temporary, target)
                    if sha(target) != original_hash:
                        raise RuntimeError('A byte-preserving resource replacement failed verification.')
                    replacements.append(dict(path=str(target), cloneSource=str(source),
                                             sha256=original_hash, bytes=original_stat.st_size))
                finally:
                    if temporary.exists():
                        temporary.unlink()
    after = {path: sha(Path(path)) for path in protected}
    if after != protected:
        raise RuntimeError('An application executable changed unexpectedly.')
    after_free = os.statvfs('/private/tmp').f_bavail * os.statvfs('/private/tmp').f_frsize
    receipt = dict(method='APFS copy-on-write clones; identical content and modes verified',
                   appExecutablesUnchanged=True, priorUserBundleVersion9Untouched=True,
                   projectsMediaCatalogsRecoveryUntouched=True,
                   filesReplaced=len(replacements),
                   logicalDuplicateBytes=sum(row['bytes'] for row in replacements),
                   availableBytesBefore=before_free, availableBytesAfter=after_free,
                   replacements=replacements)
    with receipt_path.open('x') as output:
        json.dump(receipt, output, indent=2)
        output.write('\n')
    print(json.dumps({key: value for key, value in receipt.items() if key != 'replacements'}, indent=2))


if __name__ == '__main__':
    main()
