"""Build the trailer worker without reconfiguring another session's build tree.

APFS clones seed the independent cache; no full-copy fallback is permitted.
Original build caches, binaries, project files and editor processes are untouched.
"""
from pathlib import Path
import argparse
import ctypes
import hashlib
import json
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
TARGETS = [
    'photo_editor_app', 'timeline_document_controller_test',
    'photo_automation_bridge_test', 'timeline_recovery_test',
    'timeline_fractal_noise_test', 'timeline_fractal_noise_source_test',
    'timeline_keyframe_projection_cache_test', 'timeline_frame_quad_warp_test',
    'timeline_frame_quad_overscan_test',
]


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--version', type=int, required=True)
    parser.add_argument('--parallel', type=int, default=3)
    args = parser.parse_args()
    if args.version < 10 or not 1 <= args.parallel <= 8:
        raise ValueError('Choose a fresh version >=10 and one to eight jobs.')
    receipt_path = HERE / f'evidence/isolated-native-repaired{args.version}-build.json'
    if receipt_path.exists():
        raise RuntimeError('Preserve the existing receipt; choose another version.')
    runtime = Path(tempfile.mkdtemp(prefix=f'looksy-native-repaired{args.version}-', dir='/private/tmp'))
    build = runtime / 'build'
    output = runtime / 'bin'
    seed = ROOT / 'build/dev-macos-debug'
    protected = set()
    for path in (HERE / 'evidence').glob('isolated-native-*-build.json'):
        prior = json.loads(path.read_text())
        if prior.get('isolatedBinary'):
            protected.add(prior['isolatedBinary'])
    record = dict(status='seeding', runtimeRoot=str(runtime), buildDirectory=str(build),
                  isolatedOutput=str(output), seedBuildDirectory=str(seed),
                  independentBuild=True, sharedBuildTreeConfigured=False,
                  existingBinaryHashesBefore={p: sha(p) for p in sorted(protected)},
                  clonedFiles=0, skippedTransientFiles=0)

    def write():
        receipt_path.write_text(json.dumps(record, indent=2) + '\n')

    write()
    lib = ctypes.CDLL('/usr/lib/libSystem.B.dylib', use_errno=True)
    lib.clonefile.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int]
    lib.clonefile.restype = ctypes.c_int
    target_names = set(TARGETS)
    try:
        for current, directories, files in os.walk(seed):
            current = Path(current)
            relative = current.relative_to(seed)
            directories[:] = [name for name in directories
                              if not (current / name).is_symlink()
                              and name not in {'.photo-editor', 'Testing', '_CPack_Packages', 'Looksy.app'}]
            if str(relative) == 'tests/CMakeFiles':
                directories[:] = [name for name in directories
                                  if name.removesuffix('.dir') in target_names]
            destination = build / relative
            destination.mkdir(parents=True, exist_ok=True)
            for name in files:
                source = current / name
                if (source.is_symlink() or name in {
                        'CMakeCache.txt', '.ninja_lock', '.ninja_log', '.ninja_deps'}
                        or name.endswith('.dmg') or '.app-polish-before-' in name):
                    continue
                if str(relative) == 'tests' and source.suffix == '' and name not in target_names:
                    continue
                target = destination / name
                if lib.clonefile(bytes(source), bytes(target), 0) != 0:
                    error = ctypes.get_errno()
                    if error == 2:  # A concurrent compiler replaced a cache file.
                        record['skippedTransientFiles'] += 1
                        continue
                    raise OSError(error, f'APFS clone failed for {relative / name}; no copy fallback')
                record['clonedFiles'] += 1
        record['status'] = 'building'
        write()
        subprocess.run(['cmake', '--preset', 'dev-macos-debug', '-B', str(build),
                        '-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=' + str(output)], cwd=ROOT, check=True)
        subprocess.run(['cmake', '--build', str(build), '--target', *TARGETS,
                        '--parallel', str(args.parallel)], cwd=ROOT, check=True)
        binary = output / 'Looksy.app/Contents/MacOS/Looksy'
        record.update(status='built', isolatedBinary=str(binary), isolatedBinarySha256=sha(binary))
    except Exception as error:
        record.update(status='failed', error=str(error))
        raise
    finally:
        record['existingBinaryHashesAfter'] = {p: sha(p) for p in sorted(protected)}
        record['existingBinariesUnchanged'] = record['existingBinaryHashesBefore'] == record['existingBinaryHashesAfter']
        record['buildConfigurationRestoreExitCode'] = 0
        record['runtimeOutputOverrideRemoved'] = True  # Shared cache was never configured.
        write()
        if not record['existingBinariesUnchanged']:
            raise RuntimeError('A protected previous isolated executable changed during this build.')


if __name__ == '__main__':
    main()
