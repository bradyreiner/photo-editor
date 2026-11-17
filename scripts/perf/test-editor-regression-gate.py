#!/usr/bin/env python3
"""Negative-path tests for merge/release regression enforcement; no native UI."""
import copy
import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))


def module(name, file):
    spec = importlib.util.spec_from_file_location(name, HERE / file)
    result = importlib.util.module_from_spec(spec)
    sys.modules[name] = result
    spec.loader.exec_module(result)
    return result


gate = module('editor_regression_gate_test_subject', 'editor-regression-gate.py')
provenance = module('editor_proof_run_test_subject', 'check-editor-proof-run.py')
board_runner = module('owned_board_workflow_test_subject', 'run-owned-board-workflow.py')
import editor_native_regression as native


def raw_color_output(source_sha256=None):
    fixture = f'raw_exposure_fixture sha256={source_sha256}\n' if source_sha256 is not None else ''
    return fixture + ''.join(
        f'raw_exposure_combined_preview_parity draft={draft} input_transform={transform} '
        f'raw_ev=1.9 light_ev={light} total_ev={1.9 + light:g} reopened=1 reset=1 cancelled=1\n'
        for draft in (0, 1) for transform in (0, 1) for light in (0.5, -1.9))


def exploration_output(seed=42, steps=75):
    operations = [dict(action=i, attempted=1, applied=1, rejected=0, eligible=1)
                  if i not in (10, 11) else dict(action=i, attempted=0, applied=0, rejected=0, eligible=0)
                  for i in range(20)]
    operations[6]['attempted'] += 1
    operations[6]['rejected'] += 1
    operations[0]['attempted'] += steps - 19
    operations[0]['applied'] += steps - 19
    operations[0]['eligible'] += steps - 19
    coverage = dict(seed=seed, steps=steps, attempted=steps, applied=steps-1, rejected=1,
                    inverseProbes=steps, lockedRejections=1, operations=operations)
    return (f'authoring_coverage {json.dumps(coverage)}\n'
            f'authoring_sequence seed={seed} steps={steps} status=passed\n')


def native_fixture(directory):
    policy = gate.load(gate.CONTRACT)['native']
    checks = {}
    for name in ('text_move', 'text_corner', 'text_edge'):
        data = {flag: True for flag in (
            'native_window_samples_qualified', 'presented_canvas_changed_while_held',
            'held_capture_saved', 'interaction_finished', 'style_unchanged', 'scale_unchanged',
            'rotation_unchanged', 'undo_exact', 'redo_exact', 'undo_again_exact',
            'final_render_settled', 'final_capture_saved', 'geometry_contract', 'text_reflow_proven')}
        data.update(native_window_before={'qualified': True}, native_window_held={'qualified': True},
                    held_canvas_changed_pixels=100, native_updates_during_movement=3,
                    first_native_update_ms=10, held_capture_ms=70,
                    native_revision_updates=[{'at_ms': t} for t in (10, 30, 50)],
                    fixture={'typography': {'version': 10}, 'source_sha256': policy['source_sha256']},
                    held_canvas_capture='held.png')
        path = directory / (name + '.json')
        gate.write(path, data)
        checks[name] = path.name
    (directory / 'held.png').write_bytes(b'mocked capture for validator testing only')
    slider = {flag: True for flag in ('foreground_before_warmup', 'foreground_before_interaction_warmup',
        'foreground_before_held_input', 'foreground_after_held_input', 'foreground_first_native_frame',
        'foreground_after_first_frame_and_settle', 'native_frame_presented_during_input')}
    slider.update(foreground_unqualified_sample_count=0, held_input_foreground_sample_count=3,
                  maximum_foreground_sample_gap_ms=50, first_native_frame_ms=10,
                  held_native_frame_timestamps_ms=[10, 30, 50], native_window_foreground_samples=[{'qualified': True}])
    def log(name, rows):
        path = directory / (name + '.jsonl')
        path.write_text(''.join(json.dumps({'event': event, 'fields': fields})+'\n' for event, fields in rows))
        checks[name] = path.name
    log('slider', [('benchmark.actual_app_workflow.stage_completed', dict(slider, stage='color_edit_burst_settled'))])
    rows = []
    for _ in range(5):
        rows.append(('benchmark.actual_app_workflow.stage_completed', dict(stage='brush_native_window_foreground_held',
            foreground_warmup_qualified=True, foreground_after_stroke_qualified=True,
            unqualified_sample_count=0, held_sample_count=3, maximum_held_sample_gap_ms=50,
            samples=[{'qualified': True}])))
        for stage in ('brush_controller_point_to_qml_frame', 'brush_release_to_qml_commit_visible'):
            rows.append(('benchmark.actual_app_workflow.stage_completed', {'stage': stage, 'elapsed_ms': 20}))
    log('selection', rows)
    for name, operation in [('paint', 'paint'), ('eraser', 'base_eraser')]:
        data = dict(platform='cocoa', native_texture_item=True, visible_pixels_and_window_qualified=True,
                    native_window_before={'qualified': True}, native_window_after={'qualified': True},
                    peak_rss_bytes=1024,
                    trials=[dict(operation=operation, visible_pixels_verified=True, completion_capture_valid=True,
                                 first_presented_ms=20, commit_complete_ms=30) for _ in range(5)])
        gate.write(directory / (name + '.json'), data)
        checks[name] = name + '.json'
    (directory / 'crop.log').write_text('synthetic successful process log')
    checks['crop'] = 'crop.log'
    workflow = native.workflow_checker()
    rows = [('benchmark.actual_app_workflow.completed', {})]
    for budget in workflow.DEFAULT_BUDGETS:
        rows.append(('benchmark.actual_app_workflow.stage_summary',
                     dict(stage=budget.stage, iterations=5, p95_ms=1, max_ms=1)))
    log('workflow', rows)
    shoot = directory / 'real-shoot'
    (shoot / 'delivery').mkdir(parents=True)
    delivered = shoot / 'delivery/export.jpg'
    delivered.write_bytes(b'mocked exported bytes for validator testing only')
    output_hash = gate.sha(delivered)
    reopen_flags = ('freshCatalogModel', 'freshProcess', 'mainQmlReloaded', 'mainWindowVisible',
                    'mainWindowExposed', 'sameIsolatedRuntime', 'cullingPersisted',
                    'selectiveRecipePersisted', 'relinkPersisted', 'exportBytesPersisted',
                    'allGalleryThumbnailsReady', 'pickedThumbnailImageReady')
    workflow = dict(schema='looksy-real-shoot-workflow:v1', status='passed', runId='fixture-run',
                    rawDecodeExercised=True,
                    mediaCopies=[dict(fileName=n, sha256=h) for n,h in policy['real_shoot_fixture_sha256'].items()],
                    refineAndBatch=dict(maskCount=1, unselectedThirdAssetUnchanged=True,
                        batchUndoVerified=True, batchRedoVerified=True, noOpSyncDidNotAddHistory=True),
                    exportRetry=dict(failureStatus='Failed', retryStatus='Completed',
                        outputPath=str(delivered), outputBytes=delivered.stat().st_size, outputSha256=output_hash),
                    reopen={flag: True for flag in reopen_flags})
    workflow['import'] = dict(status='Completed', importedCount=3, catalogRows=3)
    gate.write(shoot / 'workflow.json', workflow)
    gate.write(shoot / 'process-reopen.json', dict(schema='looksy-real-shoot-process-reopen:v1', status='passed',
        workflowRunId='fixture-run', processSeparationVerified=True, processId=42, parentProcessId=41,
        exportSha256=output_hash))
    checks['real_shoot'] = 'real-shoot/workflow.json'
    for name, (schema, flags) in native.AUTHORING.items():
        data = {'schema': schema, 'status': 'passed', 'platform': 'cocoa', 'windowExposed': True,
                'foreground': {'qualified': True}, **{flag: True for flag in flags}}
        gate.write(directory / (name + '.json'), data)
        checks[name] = name + '.json'
    (directory / 'text-authoring-draft.png').write_bytes(b'mocked native draft capture for validator tests only')
    gate.write(directory / 'owned-catalog.json', dict(schema='looksy-owned-board-catalog:v1',
        status='passed', catalogStopped=True))
    gate.write(directory / 'real-inpaint-controller.json', dict(
        schema='looksy-real-inpaint-controller:v1', status='passed', model='lama-onnx',
        provider='cpu', repairId='mocked-repair', sourceSha256=policy['source_sha256'],
        finalQuality=True, undoRedoExact=True, reopenPixelsExact=True, cancelPreservesAccepted=True))
    for name in ('real-inpaint-before.png', 'real-inpaint-after.png'):
        (directory / name).write_bytes(b'mocked pixels for validator tests only')
    checks['real_inpaint'] = 'real-inpaint-controller.json'
    return checks


class RegressionContracts(unittest.TestCase):
    def test_locked_native_desktop_is_host_unqualified(self):
        with self.assertRaises(gate.HostUnqualified):
            gate.check_desktop_session({'sessionAvailable': True, 'screenLocked': True})

    def test_unknown_lock_telemetry_does_not_claim_foreground(self):
        # Unknown telemetry still has to pass every real native foreground receipt.
        gate.check_desktop_session({'sessionAvailable': False, 'screenLocked': None})

    def test_owned_native_catalog_uses_isolated_paths_and_stops(self):
        with tempfile.TemporaryDirectory() as root:
            root = Path(root)
            app, helper = root / 'app', root / 'helper'
            app.touch(); helper.touch()
            child = mock.Mock(pid=123, **{'wait.return_value': 0})
            outcomes = [subprocess.CompletedProcess([], 0, 'postgresql://temporary:test@127.0.0.1:6543/test\n', ''),
                        subprocess.CompletedProcess([], 0, 'stopped\n', '')]
            with mock.patch.object(board_runner.subprocess, 'run', side_effect=outcomes) as runner, \
                 mock.patch.object(board_runner.subprocess, 'Popen', return_value=child) as launch, \
                 mock.patch.dict(board_runner.os.environ, {'PHOTO_EDITOR_APP_DATA_DIR': '/user-library',
                     'PHOTO_EDITOR_DATABASE_URL': 'postgresql://user-private-library'}):
                result = board_runner.run(app, root / 'proof', helper)
            self.assertEqual(result['status'], 'passed')
            self.assertTrue(result['catalogStopped'])
            first_env = runner.call_args_list[0].kwargs['env']
            self.assertNotEqual(first_env['PHOTO_EDITOR_APP_DATA_DIR'], '/user-library')
            self.assertNotIn('PHOTO_EDITOR_DATABASE_URL', first_env)
            self.assertFalse(Path(first_env['PHOTO_EDITOR_APP_DATA_DIR']).exists())
            self.assertEqual(runner.call_args_list[-1].args[0][-1], 'stop')
            ui = launch.call_args.kwargs['env']
            self.assertEqual(ui['PHOTO_EDITOR_SMOKE_NATIVE_POINTER'], '1')
            self.assertEqual(ui['PHOTO_EDITOR_SMOKE_NATIVE_TEXTURE'], '1')
            self.assertEqual(ui['PHOTO_EDITOR_SMOKE_STOP_AFTER_EDIT_BOARDS'], '1')
            self.assertEqual(ui['PHOTO_EDITOR_SMOKE_STOP_AFTER_BLANK_CANVAS'], '1')
            self.assertNotIn('temporary:test', (root / 'proof/catalog-start.log').read_text())

    def test_failed_native_journey_still_stops_its_catalog(self):
        with tempfile.TemporaryDirectory() as root:
            root = Path(root)
            app, helper = root / 'app', root / 'helper'
            app.touch(); helper.touch()
            child = mock.Mock(pid=124, **{'wait.return_value': 1})
            outcomes = [subprocess.CompletedProcess([], 0, 'postgresql://127.0.0.1/test\n', ''),
                        subprocess.CompletedProcess([], 0, '', '')]
            with mock.patch.object(board_runner.subprocess, 'run', side_effect=outcomes) as runner, \
                 mock.patch.object(board_runner.subprocess, 'Popen', return_value=child), self.assertRaises(RuntimeError):
                board_runner.run(app, root / 'proof', helper)
            report = gate.load(root / 'proof/owned-catalog.json')
            self.assertEqual(report['status'], 'failed')
            self.assertTrue(report['catalogStopped'])
            self.assertEqual(runner.call_args_list[-1].args[0][-1], 'stop')

    def test_failed_catalog_stop_preserves_its_runtime_and_fails_proof(self):
        with tempfile.TemporaryDirectory() as root:
            root = Path(root)
            app, helper = root / 'app', root / 'helper'
            app.touch(); helper.touch()
            child = mock.Mock(pid=125, **{'wait.return_value': 0})
            outcomes = [subprocess.CompletedProcess([], 0, 'postgresql://127.0.0.1/test\n', ''),
                        subprocess.CompletedProcess([], 1, '', 'cannot stop')]
            with mock.patch.object(board_runner.subprocess, 'run', side_effect=outcomes), \
                 mock.patch.object(board_runner.subprocess, 'Popen', return_value=child), self.assertRaises(RuntimeError):
                board_runner.run(app, root / 'proof', helper)
            report = gate.load(root / 'proof/owned-catalog.json')
            try:
                self.assertEqual(report['status'], 'failed')
                self.assertFalse(report['catalogStopped'])
                self.assertTrue(Path(report['retainedRuntime']).is_dir())
            finally:
                board_runner.shutil.rmtree(report['retainedRuntime'])

    def test_authoring_and_real_model_receipts_cannot_silently_skip(self):
        with tempfile.TemporaryDirectory() as root:
            directory = Path(root)
            policy = gate.load(gate.CONTRACT)['native']
            checks = native_fixture(directory)
            native.validate_native(directory, checks, policy)
            for name, (_, flags) in native.AUTHORING.items():
                path = directory / checks[name]
                good = gate.load(path)
                for flag in flags:
                    changed = copy.deepcopy(good)
                    changed.pop(flag)
                    gate.write(path, changed)
                    with self.subTest(name=name, flag=flag), self.assertRaises(ValueError):
                        native.validate_native(directory, checks, policy)
                gate.write(path, good)
                changed = dict(good, platform='offscreen')
                gate.write(path, changed)
                with self.subTest(name=name), self.assertRaises(ValueError):
                    native.validate_native(directory, checks, policy)
                gate.write(path, good)
            path = directory / checks['real_inpaint']
            good = gate.load(path)
            for field, value in [('model', 'fallback'), ('provider', ''), ('repairId', ''),
                                 ('sourceSha256', '0'*64), ('finalQuality', False),
                                 ('cancelPreservesAccepted', False)]:
                changed = dict(good, **{field: value})
                gate.write(path, changed)
                with self.subTest(field=field), self.assertRaises(ValueError):
                    native.validate_native(directory, checks, policy)
            gate.write(path, good)

    def test_junit_rejects_skip_missing_duplicate_and_failure(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / 'tests.xml'
            path.write_text('<testsuite><testcase name="a" status="run"/></testsuite>')
            self.assertEqual(gate.check_junit(path, ['a']), ['a'])
            for xml in [
                '<testsuite/>',
                '<testsuite><testcase name="a"><skipped/></testcase></testsuite>',
                '<testsuite><testcase name="a"><failure/></testcase></testsuite>',
                '<testsuite><testcase name="a" status="notrun"/></testsuite>',
                '<testsuite><testcase name="a"/><testcase name="a"/></testsuite>',
                '<testsuite><testcase name="a"/><testcase name="other"/></testsuite>',
            ]:
                with self.subTest(xml=xml), self.assertRaises(ValueError):
                    path.write_text(xml)
                    gate.check_junit(path, ['a'])

    def test_fixed_inventory_and_fixture_hashes(self):
        contract = gate.load(gate.CONTRACT)
        self.assertEqual(len(contract['core_tests']), 58)
        self.assertEqual(len(gate.contract_tests(contract, 'portable')), 61)
        self.assertEqual(len(gate.contract_tests(contract, 'native')), 64)
        self.assertTrue({
            'color_highlight_reconstruction_test', 'color_pipeline_property_test',
            'edit_recipe_pipeline_bridge_test', 'edit_recipe_v13_adapter_test',
            'edit_recipe_v13_persistence_test', 'raw_camera_linear_decoder_test', 'library_selection_actions_ui_test',
            'timeline_shared_edit_color_test', 'timeline_shared_edit_color_ui_test',
            'raw_exposure_render_test', 'raw_exposure_preview_test', 'raw_highlight_preview_test',
            'photo_canvas_coordinates_test', 'photo_paint_resampling_test', 'photo_paint_source_basis_test', 'brush_matte_rebuild_test', 'photo_retouch_quality_test', 'source_layer_thumbnail_test', 'mask_thumbnail_color_test', 'catalog_thumbnail_reuse_test',
        } <= set(gate.contract_tests(contract, 'portable')))
        for path, expected in contract['fixture_sha256'].items():
            self.assertEqual(gate.sha(gate.ROOT / path), expected)
        for invalid in ('missing', 'duplicate', 'overlap'):
            changed = copy.deepcopy(contract)
            if invalid == 'missing':
                changed['hardware_tests'].append('unknown_test')
            elif invalid == 'duplicate':
                changed['core_tests'].append(changed['core_tests'][0])
            else:
                changed['recovery_tests'].append(changed['core_tests'][0])
            with self.assertRaises(ValueError):
                gate.contract_tests(changed, 'portable')

    def test_exploration_is_reproducible_and_revision_diverse(self):
        contract = gate.load(gate.CONTRACT)
        pinned = gate.exploration_cases(contract)
        self.assertEqual(pinned, [(7, 60), (29, 60), (113, 60), (701, 60)])
        self.assertEqual(gate.exploration_cases(contract, commit='abcdef12'+'0'*32)[-1],
                         (int('abcdef12', 16), 60))
        self.assertEqual(gate.exploration_cases(contract, [42, 42], 75), [(42, 75)])
        for seeds, steps in [([], 10), ([-1], 10), ([2**32], 10), ([1], 0), ([1], 501)]:
            with self.subTest(seeds=seeds, steps=steps), self.assertRaises(ValueError):
                gate.exploration_cases(contract, seeds, steps)
        gate.check_exploration('noise\n' + exploration_output(), 42, 75)
        for output in ['', 'authoring_sequence seed=43 steps=75 status=passed',
                       'authoring_sequence seed=42 steps=74 status=passed',
                       'authoring_sequence seed=42 steps=75 status=passed\n'*2]:
            with self.subTest(output=output), self.assertRaises(ValueError):
                gate.check_exploration(output, 42, 75)

    def test_exploration_rejects_noop_only_or_missing_coverage(self):
        valid = exploration_output()
        coverage = json.loads(valid.splitlines()[0][len('authoring_coverage '):])
        failures = []
        changed = copy.deepcopy(coverage)
        changed['operations'].pop()
        failures.append(changed)
        changed = copy.deepcopy(coverage)
        changed['attempted'] -= 1
        failures.append(changed)
        changed = copy.deepcopy(coverage)
        for row in changed['operations']:
            row.update(applied=0, rejected=row['attempted'], eligible=0)
        changed.update(applied=0, rejected=75, inverseProbes=0)
        failures.append(changed)
        for changed in failures:
            output = ('authoring_coverage ' + json.dumps(changed) + '\n'
                      'authoring_sequence seed=42 steps=75 status=passed\n')
            with self.subTest(coverage=changed), self.assertRaises(ValueError):
                gate.check_exploration(output, 42, 75)
        with self.assertRaises(ValueError):
            gate.check_exploration('authoring_sequence seed=42 steps=75 status=passed\n', 42, 75)

    def test_real_raw_color_requires_all_exposure_and_lifecycle_cases(self):
        output = raw_color_output()
        self.assertEqual(gate.check_raw_color_preview(output), 8)
        lines = output.splitlines(keepends=True)
        for bad in ('', ''.join(lines[:-1]), ''.join(lines[:-1] + lines[:1]),
                    output.replace('reopened=1', 'reopened=0', 1),
                    output.replace('reset=1', 'reset=0', 1),
                    output.replace('cancelled=1', 'cancelled=0', 1),
                    output.replace('total_ev=2.4', 'total_ev=nan', 1),
                    output.replace('reopened=1', 'reopened=0 reopened=1', 1),
                    output.replace('input_transform=1', 'input_transform=2', 1)):
            with self.subTest(output=bad), self.assertRaises(ValueError):
                gate.check_raw_color_preview(bad)

    def test_real_raw_color_requires_one_matching_source_fixture(self):
        source_sha256 = 'a' * 64
        output = raw_color_output(source_sha256)
        self.assertEqual(gate.check_raw_color_preview(output, source_sha256), 8)
        for bad in (raw_color_output(), raw_color_output('b' * 64),
                    output + f'raw_exposure_fixture sha256={source_sha256}\n',
                    output.replace(source_sha256, source_sha256.upper(), 1),
                    output.replace(source_sha256, source_sha256 + ' unexpected=1', 1)):
            with self.subTest(output=bad), self.assertRaises(ValueError):
                gate.check_raw_color_preview(bad, source_sha256)
        for expected in ('', 'A' * 64, 'a' * 63, 42):
            with self.subTest(expected=expected), self.assertRaises(ValueError):
                gate.check_raw_color_preview(output, expected)

    def test_perf_budgets_reject_slow_absolute_and_relative_invalid_samples(self):
        policy = {'samples': 5, 'max_candidate_reference_ratio': .85,
                  'budgets': {'render': {'median_ms': 30, 'p95_ms': 40}}}
        rows = {'render': [{'candidate_ms': 20, 'reference_ms': 40} for _ in range(5)]}
        self.assertEqual(gate.check_performance(rows, policy)['render']['median_ms'], 20)
        for bad in ('absolute', 'relative', 'tail', 'missing', 'nan', 'negative', 'boolean'):
            changed = copy.deepcopy(rows)
            if bad == 'absolute':
                changed['render'] = [{'candidate_ms': 35, 'reference_ms': 100} for _ in range(5)]
            elif bad == 'relative':
                changed['render'] = [{'candidate_ms': 20, 'reference_ms': 21} for _ in range(5)]
            elif bad == 'tail':
                changed['render'][-1] = {'candidate_ms': 60, 'reference_ms': 100}
            elif bad == 'missing':
                changed['render'].pop()
            else:
                changed['render'][0]['candidate_ms'] = {'nan': float('nan'), 'negative': -1, 'boolean': True}[bad]
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                gate.check_performance(changed, policy)

    def test_missing_or_duplicate_metrics_fail(self):
        line = 'result candidate_ms=20 reference_ms=40'
        self.assertEqual(gate.metric(line, 'result ', 'candidate_ms', 'reference_ms')['candidate_ms'], 20)
        for text in ('', line + '\n' + line):
            with self.assertRaises(ValueError):
                gate.metric(text, 'result ', 'candidate_ms', 'reference_ms')

    def test_workflow_provenance_rejects_wrong_sha_forks_skips_and_untrusted_workflow(self):
        good = dict(id=12, head_sha='a' * 40, head_branch='main', event='workflow_dispatch',
                    status='completed', conclusion='success', path='.github/workflows/editor-native-regression.yml',
                    repository={'full_name': 'owner/repo'}, head_repository={'full_name': 'owner/repo'})
        provenance.validate(good, 'owner/repo', 'a' * 40, 12)
        for key, value in [('head_sha', 'b' * 40), ('head_branch', 'feature'), ('event', 'pull_request'),
                           ('status', 'in_progress'), ('conclusion', 'skipped'), ('conclusion', 'failure'),
                           ('path', '.github/workflows/other.yml'), ('id', 13),
                           ('head_repository', {'full_name': 'fork/repo'})]:
            changed = dict(good, **{key: value})
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                provenance.validate(changed, 'owner/repo', 'a' * 40, 12)

    def test_native_mode_never_opens_windows_without_explicit_flag(self):
        result = subprocess.run([sys.executable, HERE / 'editor-regression-gate.py', '--tier', 'native',
                                 '--build-dir', '/does-not-exist', '--output', '/does-not-exist'],
                                capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('no windows opened', result.stderr)

    def test_incomplete_native_scenarios_and_unsafe_paths_fail(self):
        with tempfile.TemporaryDirectory() as root:
            directory = Path(root)
            with self.assertRaises(ValueError):
                native.validate_native(directory, {}, gate.load(gate.CONTRACT)['native'])
            for path in ('../outside', '/tmp/outside', ''):
                with self.assertRaises(ValueError):
                    native.proof_path(directory, path)

    def test_failed_process_and_exit_77_are_never_accepted(self):
        with tempfile.TemporaryDirectory() as root:
            subject = object.__new__(gate.Gate)
            subject.output, subject.counter, subject.env = Path(root), 0, None
            subject.checks = []
            for exit_code in (1, 77):
                with self.assertRaises(ValueError):
                    subject.run('failure', [sys.executable, '-c', f'raise SystemExit({exit_code})'])
            self.assertEqual([c['exit_code'] for c in subject.checks], [1, 77])

    def test_native_evidence_checks_glyph_cadence_not_empty_frame_swaps(self):
        with tempfile.TemporaryDirectory() as root:
            directory = Path(root)
            checks = native_fixture(directory)
            policy = gate.load(gate.CONTRACT)['native']
            native.validate_native(directory, checks, policy)
            path = directory / checks['text_move']
            good = gate.load(path)
            # A fast scene graph must not hide stale glyph content.
            changed = copy.deepcopy(good)
            changed['native_revision_updates'] = [{'at_ms': t} for t in (10, 125, 240)]
            changed['held_capture_ms'] = 300
            changed['frame_intervals_during_movement_ms'] = [8]*30
            gate.write(path, changed)
            with self.assertRaisesRegex(ValueError, 'over budget'):
                native.validate_native(directory, checks, policy)
            gate.write(path, good)
            changed = copy.deepcopy(good)
            changed['native_window_held']['qualified'] = False
            gate.write(path, changed)
            with self.assertRaises(ValueError):
                native.validate_native(directory, checks, policy)

    def test_complete_bundle_revalidation_and_tamper_detection(self):
        with tempfile.TemporaryDirectory() as root:
            directory = Path(root)
            contract = gate.load(gate.CONTRACT)
            native_checks = native_fixture(directory)
            cases = ''.join(f'<testcase name="{name}" status="run"/>'
                            for name in gate.contract_tests(contract, 'native'))
            (directory / 'correctness.xml').write_text('<testsuite>'+cases+'</testsuite>')
            (directory / 'process.log').write_text('synthetic successful test process')
            commands = {'cpu-profile', 'inventory', 'correctness', 'brush-rss',
                        'native-text', 'native-slider', 'native-selection', 'native-paint',
                        'native-eraser', 'native-crop', 'native-workflow', 'native-real-shoot'}
            commands.update(f'{name}-{i}' for name in contract['performance']['budgets']
                            for i in range(contract['performance']['samples']))
            commands.update({'native-text-authoring', 'native-selection-authoring', 'native-board-source', 'real-inpaint-controller'})
            raw_color_logs = {}
            for name, digest in contract['native']['real_shoot_fixture_sha256'].items():
                command = 'native-raw-color-' + Path(name).stem.lower()
                commands.add(command)
                raw_color_logs[command] = command + '.log'
                (directory / raw_color_logs[command]).write_text(raw_color_output(digest))
            report = dict(schema=gate.SCHEMA, status='passed', release_eligible=True,
                          commit_sha='a'*40, source_clean=True, tier='native', configuration='Release',
                          contract_sha256=gate.sha(gate.CONTRACT), system='Darwin', machine='arm64',
                          cpu=contract['performance']['profile']['cpu'],
                          tests=gate.contract_tests(contract, 'native'), native_checks=native_checks,
                          brush_peak_rss_bytes=1024,
                          checks=[dict(name=name, exit_code=0,
                                       log=raw_color_logs.get(name, 'process.log'))
                                  for name in sorted(commands)])
            report['host_load_samples'] = [dict(one_minute=1, logical_cpus=14) for _ in range(
                contract['performance']['samples'] * len(contract['performance']['budgets']) + 2)]
            report['performance_samples'] = {
                name: [dict(candidate_ms=budget['median_ms']/2, reference_ms=budget['median_ms']*2)
                       for _ in range(contract['performance']['samples'])]
                for name, budget in contract['performance']['budgets'].items()}
            names = set(report['tests']) | {'Looksy', 'desktop_shell_studio_polish_test',
                'actual_app_workflow_benchmark_driver', 'photo_paint_performance_test', 'desktop_shell_photo_tools_test',
                'desktop_shell_real_shoot_workflow_test', 'desktop_shell_mask_reselection_pointer_test', 'editor_real_inpaint_workflow_test'}
            report['executable_sha256'] = {name: 'b'*64 for name in names}
            report['files'] = {str(p.relative_to(directory)): gate.sha(p) for p in directory.rglob('*') if p.is_file()}
            gate.write(directory / 'report.json', report)
            gate.validate_report(directory, 'a'*40)
            for mutation in ('command', 'hash', 'inventory', 'contract', 'memory', 'exe'):
                changed = copy.deepcopy(report)
                if mutation == 'command': changed['checks'].pop()
                if mutation == 'hash': changed['files']['process.log'] = 'c'*64
                if mutation == 'inventory': changed['files'].pop('held.png')
                if mutation == 'contract': changed['contract_sha256'] = 'c'*64
                if mutation == 'memory': changed['brush_peak_rss_bytes'] = 2**40
                if mutation == 'exe': changed['executable_sha256'].pop('Looksy')
                gate.write(directory / 'report.json', changed)
                with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                    gate.validate_report(directory, 'a'*40)

            # Three successful command names cannot stand in for three RAWs
            # when all of them reuse evidence from a single source file.
            changed = copy.deepcopy(report)
            raw_checks = [check for check in changed['checks'] if check['name'] in raw_color_logs]
            for check in raw_checks:
                check['log'] = raw_checks[0]['log']
            gate.write(directory / 'report.json', changed)
            with self.assertRaisesRegex(ValueError, 'wrong source fixture SHA-256'):
                gate.validate_report(directory, 'a'*40)

            # Rehashing substituted source evidence must still fail its
            # semantic binding, independently of artifact-tamper detection.
            changed = copy.deepcopy(report)
            log_name = next(iter(raw_color_logs.values()))
            log_path = directory / log_name
            original = log_path.read_text()
            log_path.write_text(raw_color_output('f' * 64))
            changed['files'][log_name] = gate.sha(log_path)
            gate.write(directory / 'report.json', changed)
            with self.assertRaisesRegex(ValueError, 'wrong source fixture SHA-256'):
                gate.validate_report(directory, 'a'*40)
            log_path.write_text(original)

    def test_real_shoot_requires_raw_decode_fresh_process_and_export_bytes(self):
        with tempfile.TemporaryDirectory() as root:
            directory = Path(root)
            checks = native_fixture(directory)
            policy = gate.load(gate.CONTRACT)['native']
            native.validate_native(directory, checks, policy)
            path = directory / checks['real_shoot']
            good = gate.load(path)
            changed = copy.deepcopy(good)
            changed['rawDecodeExercised'] = False
            gate.write(path, changed)
            with self.assertRaises(ValueError): native.validate_native(directory, checks, policy)
            gate.write(path, good)
            child = path.parent / 'process-reopen.json'
            good_child = gate.load(child)
            changed = dict(good_child, processId=good_child['parentProcessId'])
            gate.write(child, changed)
            with self.assertRaises(ValueError): native.validate_native(directory, checks, policy)
            gate.write(child, good_child)
            (path.parent / 'delivery/export.jpg').write_bytes(b'changed export')
            with self.assertRaises(ValueError): native.validate_native(directory, checks, policy)

    def test_baseline_limits_cannot_drift_independently(self):
        policy = gate.load(gate.CONTRACT)['performance']
        rows = {name: [dict(candidate_ms=b['median_ms']/2, reference_ms=b['median_ms']*2)
                       for _ in range(policy['samples'])] for name,b in policy['budgets'].items()}
        gate.check_performance(rows, policy)
        changed = copy.deepcopy(policy)
        changed['budgets']['icc']['median_ms'] += 1
        with self.assertRaises(ValueError): gate.check_performance(rows, changed)
        changed = copy.deepcopy(policy)
        changed['max_regression_fraction'] = .06
        with self.assertRaises(ValueError): gate.check_performance(rows, changed)

    def test_busy_or_missing_host_telemetry_never_qualifies(self):
        gate.check_host_load(2, 14, .5)
        for load, cores in [(13.22, 14), (float('nan'), 14), (1, None), (-1, 14)]:
            with self.assertRaises(gate.HostUnqualified):
                gate.check_host_load(load, cores, .5)

    def test_mid_run_source_change_cannot_get_native_approval(self):
        subject = object.__new__(gate.Gate)
        subject.tier = 'native'
        subject.report = dict(commit_sha='a'*40, contract_sha256=gate.sha(gate.CONTRACT))
        subject.git = lambda *args: 'modified' if args[0] == 'status' else 'a'*40
        with self.assertRaisesRegex(ValueError, 'changed during'):
            subject.finish()

    def test_stale_or_non_native_reports_fail(self):
        with tempfile.TemporaryDirectory() as root:
            directory = Path(root)
            for report in [dict(schema=gate.SCHEMA, status='skipped'),
                           dict(schema=gate.SCHEMA, status='passed', commit_sha='b'*40, source_clean=True),
                           dict(schema=gate.SCHEMA, status='passed', commit_sha='a'*40, source_clean=False),
                           dict(schema=gate.SCHEMA, status='passed', commit_sha='a'*40, source_clean=True,
                                tier='portable', configuration='Release')]:
                gate.write(directory / 'report.json', report)
                with self.assertRaises(ValueError):
                    gate.validate_report(directory, 'a'*40)


if __name__ == '__main__':
    unittest.main()
