"""Translate the existing type controls, preserving their native alpha animation."""
import hashlib
import json
import os
import sys
from pathlib import Path

P = Path(__file__).resolve().parent
sys.path.insert(0, str(P.parent))
from native_review_session import call, save, WORKER

assert not (P / 'p27-admission-intent.json').exists(), 'Inspect retained receipts; do not replay.'
before = P.parent / 'native-projects/looksy-polish-pass26-opening-handoff.json'
digest = hashlib.file_digest(before.open('rb'), 'sha256').hexdigest()
assert digest == '469751f5ca645005893425dbac913d542d4dfb3c40b6432fa9c62860278e28fa'
doc = call('timeline.inspect_document')
assert doc['ready'] and doc['documentHash'] == '4152ebc24fb0b919bb76ff86c6050360fa7dd65a73dc2fd966ac0ab8bc46dbce'
assert not call('timeline.inspect_task')['active']
clips = {c['clipId']: c for t in json.loads(before.read_text())['tracks'] for c in t['clips']}
panel_ids = [
    '6af9629c-84c9-4a8c-9d87-881cf666b2e8',
    '62b573a6-019e-4d64-96fe-806a4d2fa346',
    '181af920-1686-4e00-8b89-b28c99d9b797',
    '59cc27db-aea3-4aa0-8790-9922bff77079',
    '2cc44aa7-f93c-48f4-a729-60cfb301328e',
    'be76255a-cf32-48e4-a1fd-3bbdb183e919',
    '18ade386-f0ce-4090-b5b6-6206e7b71aaa',
    '25fffe57-4d21-4a9a-a180-3f38e6f6845b',
    '6670606b-190e-4bb7-86a9-30b6dc3be03b',
]
steps = []
for n, clip_id in enumerate(panel_ids):
    clip = clips[clip_id]
    assert clip['startSeconds'] == 112 and clip['durationSeconds'] == 7.8
    assert {k['parameter'] for k in clip['keyframes']} == {'opacity'}
    variant = clip['outputVariantOverrides']['landscape']
    steps.append({'id': f'p27/panel-part-{n+1}',
                  'commandId': 'timeline.action.setTimelineClipTitle',
                  'values': {'clipId': clip_id, 'values': {'positionX': variant['titlePositionX'] + .095}}})
selection = '1f659fb4-a392-427b-929f-820890234036'
assert not clips[selection]['keyframes']
steps.append({'id': 'p27/clear-obscuring-selection',
              'commandId': 'timeline.action.setTimelineClipAnimation',
              'values': {'clipId': selection, 'channels': [{'parameter': 'opacity', 'keyframes': [
                  {'localSeconds': 0, 'value': 0, 'interpolation': 'hold'},
                  {'localSeconds': clips[selection]['durationSeconds'], 'value': 0, 'interpolation': 'hold'}]}]}})
values = {
    'targetResourceId': doc['resourceId'],
    'objective': 'Polish the existing poster demonstration: move the complete floating type panel into the right gutter and clear the selection overlay that obscures the art. Preserve footage, poster type, masks, timing, music and prior polish.',
    'actions': [
        {'kind': 'framing', 'summary': 'Translate all nine type-panel parts together into the right gutter; preserve their alpha envelopes.'},
        {'kind': 'framing', 'summary': 'Hide the selection overlay covering the lower poster without deleting it or changing its underlying source.'},
        {'kind': 'delivery', 'summary': 'Save and verify the native editable checkpoint and inspect the rendered poster.'},
    ],
    'assetPaths': [], 'outputDirectory': str((P.parent / 'native-projects').resolve()), 'confirmed': True,
}
save(P / 'p27-admission-intent.json', {'document': doc, 'checkpoint': str(before), 'sha256': digest, 'values': values})
task = call('timeline.admit_task', values)
save(P / 'p27-admission-result.json', task)
plan = {'schema': 'looksy.native-authoring-plan/v1', 'name': 'P27 poster and type-panel clarity',
        'resourceId': doc['resourceId'], 'taskId': task['taskId'], 'startingDocumentHash': doc['documentHash'],
        'composition': {'width': 1920, 'height': 1080, 'fps': 30}, 'durationSeconds': 298,
        'planningSnapshot': {'path': str(before), 'sha256': digest, 'documentHash': doc['documentHash']},
        'steps': steps,
        'preserve': ['All original media and trims', 'Poster masks and behind-subject typography',
                     'New Gods soundtrack', 'P24-P26 opening, carousel and scrub improvements', 'User saved rotation']}
save(P / 'p27-canvas-plan.json', plan)
os.environ['LOOKSY_NATIVE_MCP_SESSION'] = str(Path(WORKER['data']) / 'automation/session.json')
sys.path.insert(0, str(P.parent.parent))
from author_native import execute
execute(P / 'p27-canvas-plan.json', P / 'p27-canvas-execution.json', compact_inspection=True)
