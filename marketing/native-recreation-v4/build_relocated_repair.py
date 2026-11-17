"""Reuse our private compiler cache, directing every new executable elsewhere."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
TARGETS = [
    'photo_editor_app', 'timeline_document_controller_test',
    'photo_automation_bridge_test', 'timeline_recovery_test',
    'timeline_fractal_noise_test', 'timeline_fractal_noise_source_test',
    'timeline_keyframe_projection_cache_test', 'timeline_frame_quad_warp_test',
    'timeline_frame_quad_overscan_test', 'timeline_compound_producer_identity_test',
    'timeline_edit_journal_codec_test',
    'actual_app_workflow_benchmark_driver',
    'timeline_ui_consistency_contract_test',
    'timeline_virtual_track_rows_ui_test', 'timeline_overview_ui_test',
    'timeline_horizontal_wheel_handler_ui_test',
]


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--version', type=int, required=True)
    parser.add_argument('--from-version', type=int, required=True)
    parser.add_argument('--parallel', type=int, default=3)
    args = parser.parse_args()
    if args.version <= args.from_version or args.from_version < 10:
        raise ValueError('Relocate only a prior independently isolated private build.')
    if not 1 <= args.parallel <= 6:
        raise ValueError('Use one to six build jobs.')
    prior_path = HERE / f'evidence/isolated-native-repaired{args.from_version}-build.json'
    prior = json.loads(prior_path.read_text())
    build = Path(prior['buildDirectory']).resolve(strict=True)
    if (prior.get('independentBuild') is not True or
            prior.get('sharedBuildTreeConfigured') is not False or
            build.parent.parent != Path('/private/tmp') or
            not build.parent.name.startswith('looksy-native-repaired') or build.name != 'build'):
        raise RuntimeError('Never relocate or reconfigure a shared/user compiler cache.')
    receipt_path = HERE / f'evidence/isolated-native-repaired{args.version}-build.json'
    if receipt_path.exists():
        raise RuntimeError('Preserve the previous build receipt; choose a fresh version.')
    protected = set()
    for path in (HERE / 'evidence').glob('isolated-native-*-build.json'):
        record = json.loads(path.read_text())
        if record.get('isolatedBinary'):
            protected.add(record['isolatedBinary'])
    runtime = Path(tempfile.mkdtemp(prefix=f'looksy-native-repaired{args.version}-', dir='/private/tmp'))
    output = runtime / 'bin'
    record = dict(status='building', runtimeRoot=str(runtime), isolatedOutput=str(output),
                  buildDirectory=str(build), reusedPrivateBuildReceipt=str(prior_path),
                  independentBuild=True, sharedBuildTreeConfigured=False,
                  existingBinaryHashesBefore={path: sha(path) for path in sorted(protected)})
    # Give each diagnostic worker its own Launch Services identity. Sharing the
    # user app's identifier makes native UI targeting ambiguous even when its
    # catalog and executable path are isolated.
    worker_bundle_id = f'com.virel.studio.nativeworker.v{args.version}'
    record['bundleIdentifier'] = worker_bundle_id

    def write():
        receipt_path.write_text(json.dumps(record, indent=2) + '\n')

    write()
    try:
        subprocess.run(['cmake', '--preset', 'dev-macos-debug', '-B', str(build),
                        '-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=' + str(output),
                        '-DFRAMEHOUSE_MACOS_BUNDLE_IDENTIFIER=' + worker_bundle_id], cwd=ROOT, check=True)
        subprocess.run(['cmake', '--build', str(build), '--target', *TARGETS,
                        '--parallel', str(args.parallel)], cwd=ROOT, check=True)
        binary = output / 'Looksy.app/Contents/MacOS/Looksy'
        record.update(status='built', isolatedBinary=str(binary), isolatedBinarySha256=sha(binary))
    except Exception as error:
        record.update(status='failed', error=str(error))
        raise
    finally:
        record['existingBinaryHashesAfter'] = {path: sha(path) for path in sorted(protected)}
        record['existingBinariesUnchanged'] = record['existingBinaryHashesAfter'] == record['existingBinaryHashesBefore']
        record['buildConfigurationRestoreExitCode'] = 0
        record['runtimeOutputOverrideRemoved'] = True  # Shared caches are never configured.
        write()
        if not record['existingBinariesUnchanged']:
            raise RuntimeError('An existing application executable changed; stop and preserve evidence.')


if __name__ == '__main__':
    main()
