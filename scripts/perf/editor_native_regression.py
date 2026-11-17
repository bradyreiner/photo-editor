"""Native editor proof capture and validation, used only with explicit visible mode."""
from __future__ import annotations

import importlib.util
import json
import math
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def desktop_session():
    """Read only lock telemetry; native foreground proof remains mandatory."""
    result = {'sessionAvailable': False, 'screenLocked': None}
    if sys.platform != 'darwin':
        return result
    import ctypes
    cf = ctypes.CDLL('/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation')
    cg = ctypes.CDLL('/System/Library/Frameworks/CoreGraphics.framework/CoreGraphics')
    cg.CGSessionCopyCurrentDictionary.restype = ctypes.c_void_p
    cf.CFStringCreateWithCString.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_uint32]
    cf.CFStringCreateWithCString.restype = ctypes.c_void_p
    cf.CFDictionaryGetValue.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    cf.CFDictionaryGetValue.restype = ctypes.c_void_p
    cf.CFBooleanGetValue.argtypes = [ctypes.c_void_p]
    cf.CFBooleanGetValue.restype = ctypes.c_bool
    cf.CFRelease.argtypes = [ctypes.c_void_p]
    session = cg.CGSessionCopyCurrentDictionary()
    if not session:
        return result
    key = None
    try:
        result['sessionAvailable'] = True
        key = cf.CFStringCreateWithCString(None, b'CGSSessionScreenIsLocked', 0x08000100)
        value = cf.CFDictionaryGetValue(session, key)
        result['screenLocked'] = cf.CFBooleanGetValue(value) if value else None
        return result
    finally:
        if key:
            cf.CFRelease(key)
        cf.CFRelease(session)


AUTHORING = {
    'text_authoring': ('looksy-text-authoring-workflow:v1', (
        'directCreation', 'multiline', 'oneActionGroup', 'inspectorSharesDraft',
        'popupEscapePreservesDraft', 'authoringEscapeCancels', 'undoExact', 'redoExact',
        'defaultTextLegible', 'draftCaptured', 'editMenuSharesSession', 'draftPixelProof')),
    'selection_authoring': ('looksy-selection-authoring-workflow:v1', (
        'noDrawOperationUnchanged', 'deleteOwnerRemovesMasks', 'noResurrection',
        'undoExact', 'redoExact', 'sessionCancelExact', 'railMenuPreservesDirtySession')),
    'board_source': ('looksy-board-source-workflow:v1', (
        'returnSource', 'sourceRecipePreserved', 'finalDeletion', 'undoExact',
        'redoExact', 'standaloneNoGuess', 'newerSourcePreserved', 'chronologicalBeyond32')),
}
SCENARIOS = {'text_move', 'text_corner', 'text_edge', 'slider', 'selection', 'paint', 'eraser', 'crop', 'workflow', 'real_shoot', 'real_inpaint'} | set(AUTHORING)


def require(value, message):
    if not value:
        raise ValueError(message)


def read(path):
    return json.loads(path.read_text())


def number(value, maximum, label):
    require(type(value) in (int, float) and math.isfinite(value) and 0 <= value <= maximum,
            f'{label}: missing, invalid, or over budget')


def p95(values):
    require(isinstance(values, list) and values, 'missing native timing samples')
    for value in values:
        number(value, float('inf'), 'native sample')
    return sorted(values)[max(0, math.ceil(len(values) * .95) - 1)]


def records(path):
    result = []
    for line in path.read_text().splitlines():
        item = json.loads(line)
        fields = item.get('fields', {})
        result.append((item.get('event', fields.get('event')), fields))
    return result


def stage_rows(items, stage):
    return [fields for event, fields in items if
            event == 'benchmark.actual_app_workflow.stage_completed' and fields.get('stage') == stage]


def workflow_checker():
    spec = importlib.util.spec_from_file_location('looksy_actual_workflow_gate', ROOT / 'scripts/perf/run-actual-app-workflow-gate.py')
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def proof_path(directory, name):
    require(isinstance(name, str) and not Path(name).is_absolute() and '..' not in Path(name).parts,
            'unsafe native proof path')
    path = directory / name
    require(path.resolve().is_relative_to(directory.resolve()) and path.is_file() and not path.is_symlink(),
            'missing or unsafe native proof file')
    return path


def validate_native(directory, checks, policy):
    require(isinstance(checks, dict) and set(checks) == SCENARIOS, 'incomplete native scenario inventory')
    for name, (schema, flags) in AUTHORING.items():
        path = proof_path(directory, checks[name])
        data = read(path)
        require(data.get('schema') == schema and data.get('status') == 'passed', f'{name}: journey did not complete')
        require(data.get('platform') == 'cocoa' and data.get('windowExposed') is True and
                data.get('foreground', {}).get('qualified') is True,
                f'{name}: exposed foreground Cocoa window required; offscreen is not native proof')
        for flag in flags:
            require(data.get(flag) is True, f'{name}: {flag} not proved')
        if name == 'text_authoring':
            proof_path(path.parent, 'text-authoring-draft.png')
        if name == 'board_source':
            catalog = read(proof_path(path.parent, 'owned-catalog.json'))
            require(catalog.get('schema') == 'looksy-owned-board-catalog:v1' and
                    catalog.get('status') == 'passed' and catalog.get('catalogStopped') is True,
                    'board_source: disposable catalog must finish and stop successfully')
    inpaint_path = proof_path(directory, checks['real_inpaint'])
    inpaint = read(inpaint_path)
    require(inpaint.get('schema') == 'looksy-real-inpaint-controller:v1' and inpaint.get('status') == 'passed'
            and inpaint.get('model') == 'lama-onnx' and bool(inpaint.get('provider'))
            and bool(inpaint.get('repairId')) and inpaint.get('sourceSha256') == policy['source_sha256'],
            'real_inpaint: verified model, provider, repair and photographic source required')
    for flag in ('finalQuality', 'undoRedoExact', 'reopenPixelsExact', 'cancelPreservesAccepted'):
        require(inpaint.get(flag) is True, f'real_inpaint: {flag} not proved')
    for name in ('real-inpaint-before.png', 'real-inpaint-after.png'):
        proof_path(inpaint_path.parent, name)
    for name in ('text_move', 'text_corner', 'text_edge'):
        path = proof_path(directory, checks[name])
        data = read(path)
        for flag in ('native_window_samples_qualified', 'presented_canvas_changed_while_held',
                     'held_capture_saved', 'interaction_finished', 'style_unchanged', 'scale_unchanged',
                     'rotation_unchanged', 'undo_exact', 'redo_exact', 'undo_again_exact',
                     'final_render_settled', 'final_capture_saved'):
            require(data.get(flag) is True, f'{name}: {flag} not proved')
        for sample in ('native_window_before', 'native_window_held'):
            require(data.get(sample, {}).get('qualified') is True, f'{name}: unqualified foreground sample')
        require(type(data.get('held_canvas_changed_pixels')) is int and data['held_canvas_changed_pixels'] > 0,
                f'{name}: no visible glyph changes')
        require(data.get('native_updates_during_movement', 0) >= 2, f'{name}: no repeated held updates')
        number(data.get('first_native_update_ms'), policy['first_feedback_max_ms'], name)
        updates = data.get('native_revision_updates', [])
        require(len(updates) >= 3, f'{name}: insufficient presented glyph revisions')
        stamps = [r.get('at_ms') for r in updates]
        for stamp in stamps:
            number(stamp, float('inf'), f'{name} glyph timestamp')
        held_until = data.get('held_capture_ms')
        number(held_until, float('inf'), f'{name} held capture time')
        stamps = [t for t in stamps if t <= held_until]
        intervals = [b-a for a, b in zip(stamps, stamps[1:])]
        require(len(intervals or []) >= 2, f'{name}: insufficient frame intervals')
        number(p95(intervals), policy['held_interval_p95_max_ms'], name)
        require(data.get('fixture', {}).get('typography', {}).get('version') == 10,
                f'{name}: mixed-style v10 fixture required')
        require(data['fixture'].get('source_sha256') == policy['source_sha256'],
                f'{name}: photographic source changed')
        proof_path(path.parent, data.get('held_canvas_capture'))
        if name != 'text_move':
            require(data.get('geometry_contract') is True and data.get('text_reflow_proven') is True,
                    f'{name}: text reflow not proved')

    items = records(proof_path(directory, checks['slider']))
    rows = stage_rows(items, 'color_edit_burst_settled')
    require(len(rows) == 1, 'missing/duplicate held-slider proof')
    row = rows[0]
    for flag in ('foreground_before_warmup', 'foreground_before_interaction_warmup',
                 'foreground_before_held_input', 'foreground_after_held_input',
                 'foreground_first_native_frame', 'foreground_after_first_frame_and_settle',
                 'native_frame_presented_during_input'):
        require(row.get(flag) is True, f'slider: {flag} not proved')
    require(row.get('foreground_unqualified_sample_count') == 0 and row.get('held_input_foreground_sample_count', 0) >= 2,
            'slider foreground proof incomplete')
    number(row.get('maximum_foreground_sample_gap_ms'), policy['maximum_foreground_sample_gap_ms'], 'slider foreground gap')
    number(row.get('first_native_frame_ms'), policy['first_feedback_max_ms'], 'slider first frame')
    stamps = row.get('held_native_frame_timestamps_ms', [])
    require(len(stamps) >= 3, 'slider did not present repeated native frames while held')
    number(p95([b-a for a, b in zip(stamps, stamps[1:])]), policy['held_interval_p95_max_ms'], 'slider held intervals')
    samples = row.get('native_window_foreground_samples', [])
    require(samples and all(s.get('qualified') is True for s in samples), 'slider lost foreground')

    items = records(proof_path(directory, checks['selection']))
    rows = stage_rows(items, 'brush_native_window_foreground_held')
    require(len(rows) >= 5, 'at least five Selection strokes required')
    for row in rows:
        require(row.get('foreground_warmup_qualified') is True and row.get('foreground_after_stroke_qualified') is True and
                row.get('unqualified_sample_count') == 0 and row.get('held_sample_count', 0) >= 2,
                'Selection stroke lost continuous foreground qualification')
        samples = row.get('samples', [])
        require(samples and all(s.get('qualified') is True for s in samples), 'unqualified Selection samples')
        number(row.get('maximum_held_sample_gap_ms'), policy['maximum_foreground_sample_gap_ms'], 'Selection foreground gap')
    for stage, budget in [('brush_controller_point_to_qml_frame', 'brush_feedback_p95_max_ms'),
                          ('brush_release_to_qml_commit_visible', 'brush_commit_p95_max_ms')]:
        timings = stage_rows(items, stage)
        require(len(timings) >= 5, f'{stage}: missing trials')
        number(p95([r.get('elapsed_ms') for r in timings]), policy[budget], stage)

    for name, operation in [('paint', 'paint'), ('eraser', 'base_eraser')]:
        data = read(proof_path(directory, checks[name]))
        require(data.get('platform') == 'cocoa' and data.get('native_texture_item') is True and
                data.get('visible_pixels_and_window_qualified') is True and
                data.get('native_window_before', {}).get('qualified') is True and
                data.get('native_window_after', {}).get('qualified') is True, f'{name}: native window/pixels unqualified')
        number(data.get('peak_rss_bytes'), policy['peak_rss_max_bytes'], f'{name} peak RSS')
        trials = data.get('trials', [])
        require(len(trials) >= 5, f'{name}: missing five trials')
        require(all(t.get('operation') == operation and t.get('visible_pixels_verified') is True and
                    t.get('completion_capture_valid') is True for t in trials), f'{name}: visible pixels not proved')
        number(p95([t.get('first_presented_ms') for t in trials]), policy['brush_feedback_p95_max_ms'], name)
        number(p95([t.get('commit_complete_ms') for t in trials]), policy['brush_commit_p95_max_ms'], name)

    crop = proof_path(directory, checks['crop'])
    require(crop.stat().st_size > 0 and list(crop.parent.rglob('*.png')), 'missing native Crop captures')
    workflow = workflow_checker()
    source = proof_path(directory, checks['workflow'])
    require(any(event == 'benchmark.actual_app_workflow.completed' for event, _ in records(source)), 'workflow did not complete')
    failures = workflow.check_budgets(workflow.collect_stage_summaries(source))
    require(not failures, f'workflow budgets failed: {failures}')


    path = proof_path(directory, checks['real_shoot'])
    shoot = read(path)
    require(shoot.get('schema') == 'looksy-real-shoot-workflow:v1' and shoot.get('status') == 'passed' and
            shoot.get('rawDecodeExercised') is True, 'real RAW workflow did not complete')
    media = shoot.get('mediaCopies', [])
    require(len(media) == 3 and {m.get('fileName'): m.get('sha256') for m in media} ==
            policy['real_shoot_fixture_sha256'], 'real-shoot source inventory changed')
    imported = shoot.get('import', {})
    require(imported.get('status') == 'Completed' and imported.get('importedCount') == 3 and
            imported.get('catalogRows') == 3, 'production RAW import not proved')
    batch = shoot.get('refineAndBatch', {})
    require(all(batch.get(k) is True for k in ('unselectedThirdAssetUnchanged', 'batchUndoVerified',
                'batchRedoVerified', 'noOpSyncDidNotAddHistory')) and batch.get('maskCount', 0) > 0,
            'batch/refine/undo workflow incomplete')
    exported = shoot.get('exportRetry', {})
    require(exported.get('failureStatus') == 'Failed' and exported.get('retryStatus') == 'Completed' and
            exported.get('outputBytes', 0) > 0, 'export failure/retry not proved')
    delivered = proof_path(path.parent, 'delivery/' + Path(exported.get('outputPath', '')).name)
    import hashlib
    require(hashlib.sha256(delivered.read_bytes()).hexdigest() == exported.get('outputSha256') and
            delivered.stat().st_size == exported['outputBytes'], 'delivered export bytes changed')
    reopened = shoot.get('reopen', {})
    for flag in ('freshCatalogModel', 'freshProcess', 'mainQmlReloaded', 'mainWindowVisible',
                 'mainWindowExposed', 'sameIsolatedRuntime', 'cullingPersisted',
                 'selectiveRecipePersisted', 'relinkPersisted', 'exportBytesPersisted',
                 'allGalleryThumbnailsReady', 'pickedThumbnailImageReady'):
        require(reopened.get(flag) is True, f'reopen: {flag} missing')
    child = read(proof_path(path.parent, 'process-reopen.json'))
    require(child.get('schema') == 'looksy-real-shoot-process-reopen:v1' and child.get('status') == 'passed' and
            child.get('workflowRunId') == shoot.get('runId') and child.get('processSeparationVerified') is True and
            type(child.get('processId')) is int and type(child.get('parentProcessId')) is int and
            child['processId'] > 1 and child['parentProcessId'] > 1 and child['processId'] != child['parentProcessId'] and
            child.get('exportSha256') == exported.get('outputSha256'), 'fresh-process receipt not proved')


def collect_native(gate):
    output, build = gate.output, gate.build
    source = ROOT / 'marketing/media/stock/landscape-mountain.jpg'
    env = dict(gate.env, QT_QPA_PLATFORM='cocoa', QSG_RHI_BACKEND='metal',
               PHOTO_EDITOR_ENABLE_DEV_OBSERVABILITY='1', PHOTO_EDITOR_SMOKE_NATIVE_POINTER='1',
               PHOTO_EDITOR_SMOKE_NATIVE_TEXTURE='1')
    checks = {}
    def relative(path):
        return str(path.relative_to(output))
    def one(path, pattern):
        paths = list(path.rglob(pattern))
        require(len(paths) == 1, f'expected one {pattern} under {path}, found {len(paths)}')
        return paths[0]
    text = output / 'native-text'
    text_env = dict(env, PHOTO_EDITOR_VISUAL_PROOF_DIR=str(text),
                    PHOTO_EDITOR_STUDIO_PHOTO_SOURCE=str(source), PHOTO_EDITOR_SMOKE_TYPOGRAPHY_ONLY='1',
                    PHOTO_EDITOR_SMOKE_TEXT_PERF_ONLY='1', PHOTO_EDITOR_SMOKE_TEXT_DRAG_PERF='1',
                    PHOTO_EDITOR_SMOKE_TEXT_DRAG_PERF_LABEL='candidate')
    gate.run('native-text', [gate.executable('desktop_shell_studio_polish_test')], text_env)
    for key, pattern in [('text_move', 'text-drag-perf-candidate.json'),
                         ('text_corner', 'text-resize-perf-corner-candidate.json'),
                         ('text_edge', 'text-resize-perf-right-edge-candidate.json')]:
        checks[key] = relative(one(text, pattern))
    driver = gate.executable('actual_app_workflow_benchmark_driver')
    for name, arguments in [('slider', ['--color-interaction-proof', '--color-control', 'exposure',
                                        '--require-live-native-feedback', '--require-native-foreground-samples']),
                             ('selection', ['--brush-only', '--iterations', '5', '--require-native-foreground-samples'])]:
        directory = output / f'native-{name}'
        gate.run(f'native-{name}', [sys.executable, ROOT / 'scripts/perf/run-benchmark.py',
                 '--scenario', f'editor-regression-{name}', '--app', driver, '--output-root', directory,
                 '--event', 'benchmark.actual_app_workflow.completed', '--timeout', '300', '--settle-seconds', '0',
                 '--', '--source', source, '--timeout-ms', '30000', *arguments], env)
        checks[name] = relative(one(directory, 'desktop.jsonl'))
    for name in ('paint', 'eraser'):
        receipt = output / f'native-{name}.json'
        paint_env = dict(env, PHOTO_PAINT_BENCH_NATIVE_TEXTURE='1', PHOTO_PAINT_BENCH_VISIBLE_PIXELS='1',
                         PHOTO_PAINT_BENCH_TRIALS='5', PHOTO_PAINT_BENCH_BRUSH_SIZE='96',
                         PHOTO_PAINT_BENCH_SOURCE=str(source), PHOTO_PAINT_PERF_OUTPUT=str(receipt))
        if name == 'eraser':
            paint_env['PHOTO_PAINT_BENCH_BASE_ERASER'] = '1'
        gate.run(f'native-{name}', [gate.executable('photo_paint_performance_test')], paint_env)
        checks[name] = relative(receipt)
    crop = output / 'native-crop'
    crop_env = dict(env, PHOTO_EDITOR_PHOTO_TOOLS_PROOF_DIR=str(crop), PHOTO_EDITOR_SMOKE_PHOTO_TOOLS_WORKFLOW='1')
    gate.run('native-crop', [gate.executable('desktop_shell_photo_tools_test')], crop_env)
    # Keep its successful process log beside the capture set.
    crop.mkdir(exist_ok=True)
    import shutil
    shutil.copyfile(output / gate.checks[-1]['log'], crop / 'process.log')
    checks['crop'] = relative(crop / 'process.log')
    directory = output / 'native-workflow'
    gate.run('native-workflow', [sys.executable, ROOT / 'scripts/perf/run-actual-app-workflow-gate.py',
             '--app', driver, '--output-root', directory, '--source', source, '--iterations', '5', '--timeout', '300'], env)
    checks['workflow'] = relative(one(directory, 'desktop.jsonl'))
    real_root = output / 'native-real-shoot'
    real_env = dict(env, PHOTO_EDITOR_REAL_SHOOT_PROOF_DIR=str(real_root),
                    PHOTO_EDITOR_REAL_SHOOT_SOURCE_DIR=gate.env['LOOKSY_REAL_SHOOT_SOURCE_DIR'],
                    PHOTO_EDITOR_SMOKE_STOP_AFTER_REAL_SHOOT_WORKFLOW='1')
    gate.run('native-real-shoot', [gate.executable('desktop_shell_real_shoot_workflow_test')], real_env, timeout=900)
    checks['real_shoot'] = relative(one(real_root, 'workflow.json'))
    authoring_text = output / 'native-text-authoring'
    gate.run('native-text-authoring', [gate.executable('desktop_shell_studio_polish_test')],
             dict(env, PHOTO_EDITOR_VISUAL_PROOF_DIR=str(authoring_text),
                  PHOTO_EDITOR_STUDIO_PHOTO_SOURCE=str(source), PHOTO_EDITOR_SMOKE_TYPOGRAPHY_ONLY='1'))
    checks['text_authoring'] = relative(one(authoring_text, 'text-authoring-workflow.json'))
    authoring_selection = output / 'native-selection-authoring'
    gate.run('native-selection-authoring', [gate.executable('desktop_shell_mask_reselection_pointer_test')],
             dict(env, PHOTO_EDITOR_VISUAL_PROOF_DIR=str(authoring_selection),
                  PHOTO_EDITOR_SMOKE_MASK_RESELECTION_POINTER='1', PHOTO_EDITOR_MASK_AUTHORING_WORKFLOW='1'))
    checks['selection_authoring'] = relative(one(authoring_selection, 'selection-authoring-workflow.json'))
    board_source = output / 'native-board-source'
    gate.run('native-board-source', [sys.executable, ROOT / 'scripts/perf/run-owned-board-workflow.py',
             '--app', gate.executable('desktop_shell_studio_polish_test'), '--proof-dir', board_source], env, timeout=900)
    checks['board_source'] = relative(one(board_source, 'board-source-workflow.json'))
    real_inpaint = output / 'real-inpaint-controller'
    gate.run('real-inpaint-controller', [gate.executable('editor_real_inpaint_workflow_test'), source, real_inpaint],
             env, timeout=240)
    checks['real_inpaint'] = relative(one(real_inpaint, 'real-inpaint-controller.json'))
    validate_native(output, checks, gate.contract['native'])
    return checks
