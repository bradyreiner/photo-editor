"""Link the compiled review changes into a separate bundle; leave running apps intact."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import shutil
import tempfile
from native_review_session import ROOT, WORKER, save

here = Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument('--label', default='')
parser.add_argument('--object-build-log', default='')
parser.add_argument('--configure-log', default='')
parser.add_argument('--link-command-json', default='')
parser.add_argument('--base-build-receipt', type=Path)
parser.add_argument('--refresh-source', action='append', default=[],
                    help='Overlay one repo-relative frozen source. Repeat for each changed input.')
parser.add_argument('--proof', action='append', default=[])
args = parser.parse_args()
assert not args.label or args.label.replace('-', '').isalnum()
assert not args.base_build_receipt or args.link_command_json, \
    'When staging from a base build receipt, pass that bundle’s explicit generated link-command JSON.'
record = here / ('release-build' + ('-' + args.label if args.label else '') + '.json')
assert not record.exists()
root = ROOT.parents[1]
build = root / 'build/release-macos'
link_json_path = Path(args.link_command_json) if args.link_command_json else Path('/private/tmp/looksy-polish-current-link.json')
prior = json.loads(link_json_path.read_text())
assert isinstance(prior.get('link'), list) and prior['link'], 'Missing CMake-generated linker argument vector.'
assert not any(value in {':', '&&', '||', ';', '|'} for value in prior['link']), \
    'Supply only the generated linker argv, without Ninja shell wrappers or post-build commands.'
regular = build / 'apps/desktop/Looksy.app/Contents/MacOS/Looksy'
sha = lambda p: hashlib.file_digest(Path(p).open('rb'), 'sha256').hexdigest()
base_app = WORKER['app']
base = None
base_frozen_sources = None
if args.base_build_receipt:
    base_receipt = args.base_build_receipt.resolve(strict=True)
    assert base_receipt.parent == here.resolve()
    base = json.loads(base_receipt.read_text())
    assert sha(Path(base['app']) / 'Contents/MacOS/Looksy') == base['binarySha256']
    base_app = base['app']
    base_frozen_sources = Path(base['frozenSourceRoot']).resolve(strict=True)
regular_before = sha(regular)
folder = Path(tempfile.mkdtemp(prefix='looksy-polish-native-build-', dir='/private/tmp'))
app = folder / 'Looksy Review.app'
subprocess.run(['/bin/cp', '-cR', base_app, str(app)], check=True)
link = list(prior['link'])
link[link.index('-o') + 1] = str(app / 'Contents/MacOS/Looksy')
with (folder / 'link.log').open('w') as log:
    result = subprocess.run(link, cwd=build, stdout=log, stderr=subprocess.STDOUT)
if result.returncode:
    raise RuntimeError('Review bundle link failed; inspect ' + str(folder / 'link.log'))
assert regular_before == sha(regular)
sources = ['photo_mcp_server.cpp', 'photo_automation_bridge.cpp', 'photo_automation_bridge.h', 'automation_capability_policy.h',
           'timeline_document_controller.cpp', 'timeline_document_controller.h', 'timeline_visible_proof_cache.h',
           'timeline_draft_lifecycle.cpp', 'timeline_draft_lifecycle.h',
           'agent_timeline_commands.h', 'agent_timeline_audio_state_commands.h',
           'job_runner.cpp', 'job_runner.h', 'timeline_mcp_export.h',
           'timeline_bounded_h264_export.cpp', 'timeline_bounded_h264_export.h',
           'timeline_sparse_title_roi.cpp', 'timeline_sparse_title_roi.h',
           'timeline_bounded_concat_clock.h',
           'timeline_bounded_ffmpeg_memory_policy.h', 'timeline_review_output_verification.h',
           'timeline_shared_png_inputs.h',
           'timeline_native_generator_frame_cache.cpp', 'timeline_native_generator_frame_cache.h', 'main.cpp', 'app_bootstrap.cpp', 'app_bootstrap.h',
           'timeline_playback_controller.cpp', 'timeline_playback_controller.h',
           'timeline_color_edit_controller.cpp', 'timeline_color_edit_controller.h',
           'timeline_video_grade_runtime.cpp', 'timeline_video_grade_runtime.h',
           'timeline_photo_filter_controls.h',
           'agent_task_coordinator.h', 'agent_task_journal.h', 'agent_app_command_registry.h']
test_sources = [
    'tests/CMakeLists.txt',
    'tests/agent_app_command_registry_test.cpp',
    'tests/agent_task_coordinator_test.cpp',
    'tests/timeline_document_controller_test.cpp',
    'tests/timeline_clip_frame_base_animation_workflow.inc',
    'tests/timeline_animated_xy_perspective_export_workflow.inc',
    'tests/timeline_export_snapshot_readiness_workflow.inc',
    'tests/timeline_recovery_test.cpp',
    'tests/timeline_photo_grade_test.cpp',
    'tests/timeline_mcp_export_range_test.cpp',
    'tests/timeline_track_audio_state_command_test.cpp',
    'tests/timeline_bounded_concat_clock_test.cpp',
    'tests/timeline_sparse_title_roi_test.cpp',
    'tests/timeline_shared_png_inputs_test.cpp',
    'tests/timeline_agent_mutation_batch_workflow.inc',
    'tests/timeline_bounded_h264_export_workflow.inc',
    'tests/timeline_clip_retime_action_test.cpp',
    'tests/timeline_native_generator_frame_cache_test.cpp',
    'tests/timeline_native_generator_vector_cache_test.cpp',
    'tests/photo_automation_bridge_test.cpp',
]
frozen_sources = folder / 'frozen-source'
freeze_paths = [root / 'apps/desktop/src' / s for s in sources] + [
    root / 'apps/desktop/qml/components/TimelineEffectLibraryRow.qml',
    root / 'apps/desktop/qml/components/TimelineEffectsPanel.qml',
    root / 'apps/desktop/qml/components/TimelineColorMaskEffect.qml',
    root / 'apps/desktop/qml/components/TimelinePhotoFilters.qml',
    root / 'apps/desktop/qml/workspaces/TimelineView.qml',
    root / 'apps/desktop/qml/components/TimelineSelectionRefresh.js',
    root / 'apps/desktop/CMakeLists.txt',
    root / 'marketing/native-recreation-v4/polish-pass-2026-10-30/stage_review_build.py']
freeze_paths += [root / s for s in test_sources]
def resolve_repo_input(value):
    path = Path(value)
    if not path.is_absolute():
        path = root / path
    path = path.resolve(strict=True)
    path.relative_to(root)
    return path

proof_paths = [Path(value).resolve(strict=True) for value in args.proof]
for proof in proof_paths:
    proof.relative_to(root)
freeze_paths += proof_paths
refresh_paths = [resolve_repo_input(value) for value in args.refresh_source]
explicit_refresh_set = {path.relative_to(root).as_posix() for path in refresh_paths}
proof_set = {path.relative_to(root).as_posix() for path in proof_paths}
managed_paths = {path.resolve().relative_to(root).as_posix() for path in freeze_paths}
assert explicit_refresh_set <= managed_paths, \
    'Only explicitly managed frozen-source inputs may be overlaid.'
assert len(explicit_refresh_set) == len(refresh_paths), 'Duplicate --refresh-source path.'
refresh_set = explicit_refresh_set | proof_set

# A chained bundle inherits its exact frozen source snapshot. The live checkout
# is consulted only for explicit --refresh-source inputs and new --proof files.
# A source omitted by an older receipt stays absent and is reported as such;
# newer worktree contents must not be mistaken for what the base binary used.
if base_frozen_sources:
    for entry in base_frozen_sources.rglob('*'):
        assert not entry.is_symlink(), f'Unexpected symlink in base frozen source: {entry}'
    shutil.copytree(base_frozen_sources, frozen_sources, dirs_exist_ok=True)

base_hash_maps = []
if base:
    base_hash_maps = [base.get(key, {}) for key in (
        'sourceHashes', 'buildSystemSourceHashes', 'testSourceHashes',
        'qmlSourceHashes', 'selectionRefreshSourceHashes')]

def declared_base_hash(relative_path):
    relative = Path(relative_path)
    keys = {relative.as_posix(), relative.name}
    if relative.as_posix().startswith('apps/desktop/src/'):
        keys.add(relative.relative_to('apps/desktop/src').as_posix())
    found = {mapping[key] for mapping in base_hash_maps for key in keys
             if isinstance(mapping, dict) and key in mapping}
    assert len(found) <= 1, f'Conflicting base source hashes for {relative_path}: {found}'
    return next(iter(found), '')

source_coverage = []
for source in dict.fromkeys(path.resolve() for path in freeze_paths):
    relative = source.relative_to(root).as_posix()
    target = frozen_sources / relative
    if relative in refresh_set:
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        assert sha(source) == sha(target)
        source_coverage.append({'path': relative, 'status': 'refreshed-from-worktree',
                                'sha256': sha(target)})
        continue
    if not base_frozen_sources:
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        assert sha(source) == sha(target)
        source_coverage.append({'path': relative, 'status': 'captured-from-worktree',
                                'sha256': sha(target)})
        continue
    if target.is_file() and not target.is_symlink():
        frozen_hash = sha(target)
        declared_hash = declared_base_hash(relative)
        if declared_hash:
            assert frozen_hash == declared_hash, \
                f'Base frozen source does not match its receipt: {relative}'
        source_coverage.append({'path': relative, 'status': 'inherited-from-base',
                                'sha256': frozen_hash,
                                'baseDeclaredSha256': declared_hash})
        continue
    assert not target.exists(), f'Unexpected non-file in base frozen source tree: {relative}'
    coverage_record = {'path': relative, 'status': 'sourceUnavailableInBase',
                       'baseBuildReceipt': str(args.base_build_receipt.resolve()),
                       'baseDeclaredSha256': declared_base_hash(relative)}
    # This is the exact object input used by the new link. It is not presented
    # as a historical object hash from the base receipt, which may not have
    # captured object identities for shared build-tree inputs.
    object_suffix = Path(relative).name + '.o'
    linked_objects = []
    for argument in prior['link']:
        if not isinstance(argument, str) or not argument.endswith(object_suffix):
            continue
        object_path = Path(argument)
        if not object_path.is_absolute():
            object_path = build / object_path
        if object_path.is_file():
            linked_objects.append({'path': str(object_path.resolve()),
                                   'sha256AtLinkInput': sha(object_path)})
    if linked_objects:
        coverage_record['linkedObjectInputs'] = linked_objects
    source_coverage.append(coverage_record)

inherited_proofs = []
if base:
    for entry in base.get('proofArtifacts', []):
        relative = Path(entry['path']).resolve().relative_to(root).as_posix()
        base_frozen_path = base_frozen_sources / relative
        assert base_frozen_path.is_file() and sha(base_frozen_path) == entry['sha256'], \
            f'Base proof artifact missing or changed: {relative}'
        if relative in refresh_set:
            frozen_path = frozen_sources / '__inherited_proofs__' / entry['sha256'] / Path(relative).name
            frozen_path.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(base_frozen_path, frozen_path)
        else:
            frozen_path = frozen_sources / relative
        assert frozen_path.is_file() and sha(frozen_path) == entry['sha256'], \
            f'Inherited proof artifact missing or changed: {relative}'
        inherited_proofs.append({'path': str(root / relative), 'sha256': sha(frozen_path),
                                 'frozenPath': str(frozen_path), 'origin': 'inherited-from-base'})
proof_records = list(inherited_proofs)
for proof in proof_paths:
    relative = proof.relative_to(root).as_posix()
    target = frozen_sources / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(proof, target)
    assert sha(proof) == sha(target)
    proof_records.append({'path': str(proof), 'sha256': sha(target),
                          'frozenPath': str(target), 'origin': 'refreshed-proof'})

def frozen_hash(relative_path):
    path = frozen_sources / relative_path
    return sha(path) if path.is_file() and not path.is_symlink() else None

def frozen_hashes(paths):
    return {key: value for key, relative in paths
            if (value := frozen_hash(relative)) is not None}
save(record, {'app': str(app), 'binarySha256': sha(app / 'Contents/MacOS/Looksy'),
    'frozenSourceRoot': str(frozen_sources),
    'previousApp': base_app, 'runningWorkerApp': WORKER['app'],
    'baseBuildReceipt': str(args.base_build_receipt.resolve()) if args.base_build_receipt else '',
    'regularBinaryUnchanged': True,
    'regularBinarySha256': regular_before,
    'sourceHashes': frozen_hashes([(s, f'apps/desktop/src/{s}') for s in sources]),
    'sourceOverlay': {
        'policy': 'inherit frozen base sources; overlay only --refresh-source and --proof inputs',
        'baseBuildReceipt': str(args.base_build_receipt.resolve()) if args.base_build_receipt else '',
        'baseBuildReceiptSha256': sha(args.base_build_receipt) if args.base_build_receipt else '',
        'refreshedSourcePaths': sorted(explicit_refresh_set),
        'refreshedProofPaths': sorted(proof_set),
        'sourceCoverage': source_coverage,
        'sourceUnavailableInBase': [entry for entry in source_coverage
                                    if entry['status'] == 'sourceUnavailableInBase']},
    'buildSystemSourceHashes': frozen_hashes([
        ('apps/desktop/CMakeLists.txt', 'apps/desktop/CMakeLists.txt'),
        ('tests/CMakeLists.txt', 'tests/CMakeLists.txt'),
        ('marketing/native-recreation-v4/polish-pass-2026-10-30/stage_review_build.py',
         'marketing/native-recreation-v4/polish-pass-2026-10-30/stage_review_build.py')]),
    'testCases': [
        'agent_task_coordinator_test',
        'agent_app_command_registry_test',
        'timeline_recovery_test --exact-autosave-recovery-diagnostic',
        'timeline_photo_grade_test',
        'timeline_mcp_export_range_test',
        'timeline_track_audio_state_command_test',
        'timeline_bounded_concat_clock_test',
        'timeline_sparse_title_roi_test',
        'timeline_document_controller_test --timeline-sparse-title-filter',
        'timeline_review_excerpt_export_workflow_test',
        'timeline_sequence_track_snapshot_workflow_test',
        'timeline_selection_refresh_coalescing_test',
        'timeline_export_snapshot_readiness_workflow_test',
        'photo_automation_bridge_test --timeline-preview-at-time-contract',
        'photo_automation_bridge_test --timeline-task-tool-contract',
        'timeline_document_controller_test --timeline-clip-frame-base-animation-workflow',
        'timeline_document_controller_test --timeline-animated-xy-perspective-export-workflow',
        'timeline_bounded_h264_export_workflow_test',
        'timeline_agent_mutation_batch_workflow_test',
        'timeline_clip_retime_action_test',
        'timeline_native_generator_frame_cache_test (standalone final-source build)',
        'timeline_grade_effect_command_scan_bypass_test'],
    'testSourceHashes': frozen_hashes([(s, s) for s in test_sources]),
    'testBinaryHashes': {
        name: {
            'path': str(root / 'build/dev-macos-debug/tests' / name),
            'sha256': sha(root / 'build/dev-macos-debug/tests' / name)}
        for name in ['agent_task_coordinator_test',
                     'agent_app_command_registry_test',
                     'timeline_document_controller_test',
                     'timeline_recovery_test',
                     'timeline_photo_grade_test',
                     'timeline_mcp_export_range_test',
                     'timeline_track_audio_state_command_test',
                     'timeline_bounded_concat_clock_test',
                     'timeline_sparse_title_roi_test',
                     'photo_automation_bridge_test']},
    'additionalFocusedTestBinaries': {
        'timeline_clip_retime_action_test': {
            'path': str(root / 'build/release-macos/tests/timeline_clip_retime_action_test'),
            'sha256': sha(root / 'build/release-macos/tests/timeline_clip_retime_action_test')},
        'timeline_native_generator_frame_cache_test': {
            'path': '/private/tmp/timeline_native_generator_frame_cache_test',
            'sha256': sha('/private/tmp/timeline_native_generator_frame_cache_test')},
        'timeline_sparse_title_filter_workflow_release': {
            'path': str(root / 'build/release-macos/tests/timeline_document_controller_test'),
            'sha256': sha(root / 'build/release-macos/tests/timeline_document_controller_test')},
    },
    'qmlSourceHashes': frozen_hashes([(s, f'apps/desktop/qml/components/{s}')
                        for s in ['TimelineEffectLibraryRow.qml', 'TimelineEffectsPanel.qml',
                                  'TimelineColorMaskEffect.qml', 'TimelinePhotoFilters.qml']]),
    'selectionRefreshSourceHashes': frozen_hashes([(s, s) for s in [
        'apps/desktop/qml/workspaces/TimelineView.qml',
        'apps/desktop/qml/components/TimelineSelectionRefresh.js',
        'apps/desktop/CMakeLists.txt']]),
    'objectBuildLog': args.object_build_log or (
        '/private/tmp/looksy-polish-' + (args.label or 'current') + '-qml-objects.log'),
    'configureLog': args.configure_log,
    'linkCommandJson': str(link_json_path.resolve()),
    'linkCommandJsonSha256': sha(link_json_path),
    'generatedLinkGraph': {key: value for key, value in prior.items() if key != 'link'},
    'proofArtifacts': proof_records,
    'linkLog': str(folder / 'link.log')})
print(json.dumps(json.loads(record.read_text()), indent=2)[:1200])
