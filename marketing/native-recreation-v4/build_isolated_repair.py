"""Build a fresh native app without overwriting any live Looksy binary."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--version', type=int, default=1)
    parser.add_argument('--configuration', choices=['debug', 'release'], default='debug')
    parser.add_argument('--parallel', type=int, default=3)
    args = parser.parse_args()
    version = args.version
    if not 1 <= args.parallel <= 8:
        raise ValueError('Use one to eight build jobs.')
    configure_preset = 'release-macos' if args.configuration == 'release' else 'dev-macos-debug'
    build_preset = 'build-release-macos' if args.configuration == 'release' else 'build-dev-macos-debug'
    build_directory = ROOT / 'build' / configure_preset
    if version < 1:
        raise ValueError('Version must be positive.')
    suffix = '' if version == 1 else str(version)
    receipt_path = HERE / f'evidence/isolated-native-repaired{suffix}-build.json'
    if receipt_path.exists():
        raise RuntimeError('Preserve previous build receipt; choose a new version.')
    binaries = set()
    for prior_path in (HERE / 'evidence').glob('isolated-native-*-build.json'):
        prior = json.loads(prior_path.read_text())
        binaries.update(prior.get('existingBinaryHashesAfter', {}))
        if prior.get('isolatedBinary'):
            binaries.add(prior['isolatedBinary'])
    binaries = sorted(binaries)
    release_binary = ROOT / 'build/release-macos/apps/desktop/Looksy.app/Contents/MacOS/Looksy'
    if release_binary.exists() and str(release_binary) not in binaries:
        binaries.append(str(release_binary))
    runtime = Path(tempfile.mkdtemp(prefix=f'looksy-native-repaired{suffix}-20261027-', dir='/private/tmp'))
    output = runtime / 'bin'
    record = dict(runtimeRoot=str(runtime), isolatedOutput=str(output),
                  existingBinaryHashesBefore={p: sha(p) for p in binaries}, status='building',
                  configuration=args.configuration, configurePreset=configure_preset, buildPreset=build_preset)
    def write():
        receipt_path.write_text(json.dumps(record, indent=2) + '\n')
    write()
    try:
        subprocess.run(['cmake', '--preset', configure_preset,
                        '-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=' + str(output)], cwd=ROOT, check=True)
        subprocess.run(['cmake', '--build', '--preset', build_preset,
                        '--target', 'photo_editor_app', '--parallel', str(args.parallel)], cwd=ROOT, check=True)
        binary = output / 'Looksy.app/Contents/MacOS/Looksy'
        record.update(status='built', isolatedBinary=str(binary), isolatedBinarySha256=sha(binary))
    except Exception as error:
        record.update(status='failed', error=str(error))
        raise
    finally:
        restored = subprocess.run(['cmake', '--preset', configure_preset,
                                   '-UCMAKE_RUNTIME_OUTPUT_DIRECTORY'], cwd=ROOT)
        record['buildConfigurationRestoreExitCode'] = restored.returncode
        cache = (build_directory / 'CMakeCache.txt').read_text()
        record['runtimeOutputOverrideRemoved'] = 'CMAKE_RUNTIME_OUTPUT_DIRECTORY:' not in cache
        record['existingBinaryHashesAfter'] = {p: sha(p) for p in binaries}
        record['existingBinariesUnchanged'] = record['existingBinaryHashesBefore'] == record['existingBinaryHashesAfter']
        write()
        if not record['existingBinariesUnchanged'] or not record['runtimeOutputOverrideRemoved'] or restored.returncode:
            raise RuntimeError('Isolated build preservation/restoration check failed.')

if __name__ == '__main__':
    main()
