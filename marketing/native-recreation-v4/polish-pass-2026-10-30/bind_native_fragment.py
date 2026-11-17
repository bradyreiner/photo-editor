"""Bind a reviewed fragment only while its exact target authoring state is unchanged."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from native_review_session import WORKER, call, save
from native_fragment_placement import validate_clip_references, verify_placement

p = argparse.ArgumentParser()
p.add_argument('fragment', type=Path)
p.add_argument('output', type=Path)
args = p.parse_args()
plan = json.loads(args.fragment.read_text())
# Validate symbolic references before making any native calls. Only a prior
# create operation in this same plan may supply the created stable clip ID.
existing_target_ids, created_clip_references = validate_clip_references(plan)
source = Path(WORKER['source'])
assert hashlib.file_digest(source.open('rb'), 'sha256').hexdigest() == WORKER['sourceSha256']
doc = call('timeline.inspect_document')
task = call('timeline.inspect_task')
assert task['active'] and task['status'] == 'running'
assert doc['resourceId'] == plan['resourceId']
live_path = Path(WORKER['candidate'])
native = json.loads(subprocess.check_output(['/private/tmp/looksy-dense-native-hash', str(live_path)], text=True))
assert native['documentHash'] == doc['documentHash'], 'Wait for the autosave; do not rebind stale targets.'
planning = plan.get('planningSnapshot', {})
baseline_path = Path(planning['path']) if planning.get('path') else source
if planning:
    expected_sha = planning.get('sha256', planning.get('fileSha256'))
    assert expected_sha and hashlib.file_digest(baseline_path.open('rb'), 'sha256').hexdigest() == expected_sha
original = json.loads(baseline_path.read_text())
live = json.loads(live_path.read_text())
placement = verify_placement(plan, original, live)
clips = lambda d: {c['clipId']: c for t in d['tracks'] for c in t['clips']}
before, now = clips(original), clips(live)
targets = existing_target_ids
fields = ['mediaType', 'assetId', 'filePath', 'startSeconds', 'durationSeconds', 'keyframes',
          'trimStartSeconds', 'trimEndSeconds', 'playbackRate', 'opacity',
          'frameRotationDegrees', 'clipMask', 'outputVariantOverrides',
          'frameScale', 'frameOffsetX', 'frameOffsetY', 'frameWidth', 'frameHeight',
          'titleText', 'titlePositionX', 'titlePositionY', 'titleFontSize',
          'titleShapeType', 'titleShapeWidth', 'titleShapeHeight',
          'titleShapeCornerRadius', 'titleShapeStrokeWidth', 'titleShapeStrokeColor',
          'titleBackgroundColor', 'titleColor', 'titleFontFamily', 'titleFontWeight',
          'titleAlignment', 'titleLineHeight', 'titleLetterSpacing',
          'captionBackgroundPadding', 'captionBackgroundRadius',
          'frameMode', 'sourceCropLeft', 'sourceCropTop', 'sourceCropRight', 'sourceCropBottom',
          'effectSettings', 'effectStack', 'blendMode']
for clip_id in targets:
    assert clip_id in before and clip_id in now
    for field in fields:
        assert now[clip_id].get(field) == before[clip_id].get(field), (clip_id, field, 'Target already revised; inspect instead of rebinding.')
assert call('timeline.inspect_document')['documentHash'] == doc['documentHash']
plan.update(startingDocumentHash=doc['documentHash'], taskId=task['taskId'])
plan['bindingEvidence'] = {'targetIds': sorted(targets), 'comparedFields': fields,
                           'createdClipReferences': [
                               {'stepId': step_id, 'creatorStepId': creator_id,
                                'field': 'createdClipId'}
                               for step_id, creator_id in created_clip_references],
                           'nativeAutosaveHash': native['documentHash'],
                           'baselinePath': str(baseline_path),
                           'placement': placement}
save(args.output, plan)
print(json.dumps({'bound': str(args.output), 'targetCount': len(targets), 'documentHash': doc['documentHash']}))
