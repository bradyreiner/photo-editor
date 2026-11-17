"""Keep the corrected captions and restore the reference comparison badges."""
import copy
import hashlib
import json
from pathlib import Path
from native_fragment_placement import verify_placement

HERE = Path(__file__).resolve().parent
parent = HERE / 'p21-film-layout/p21-film-caption-layout-v2-footer-anchor.commands-fragment.json'
plan = json.loads(parent.read_text())
baseline = json.loads(Path(plan['planningSnapshot']['path']).read_text())
live = json.loads((HERE / 'native-projects/looksy-polish-pass19-canvas-polish.json').read_text())
clips = {c['clipId']: c for t in baseline['tracks'] for c in t['clips']}
before = 'ec5b59c9-9145-49a3-b7e1-1065f0c29bc8'
after = '435f3a71-275e-4e6c-b82a-aebd0714b454'
assert clips[before]['titleText'] == 'BEFORE' and not clips[before]['keyframes']
assert clips[after]['titleText'] == 'NATIVE EFFECTS' and not clips[after]['keyframes']
reserved = {s['values']['trackId'] for s in plan['steps'] if 'trackId' in s['values']}
free = []
for track in live['tracks']:
    if (track.get('role') != 'graphics' or track['trackId'] in reserved or
            track['locked'] or not track['visible'] or track['opacity'] != 1 or
            track['groupId'] or track['groupTransformScale'] != 1 or
            track['groupTransformOffsetX'] or track['groupTransformOffsetY'] or
            track['groupTransformRotationDegrees']):
        continue
    if any(c['startSeconds'] < 226 - 1e-7 and c['startSeconds'] +
           (c['trimEndSeconds'] - c['trimStartSeconds']) / c.get('playbackRate', 1) > 218 + 1e-7
           for c in track['clips']):
        continue
    free.append(track)
assert len(free) >= 4
plates, labels = free[:2], free[2:4]
y = 204.5 / 1080
for name, cid, track, color in [('before', before, labels[0], '#fff1efdf'),
                                 ('effects', after, labels[1], '#ffd7ff48')]:
    plan['steps'].extend([
        {'id': 'comparison-badge-move-' + name,
         'commandId': 'timeline.action.setTimelineClipTiming',
         'values': {'clipId': cid, 'trackId': track['trackId'],
                    'startSeconds': 218, 'durationSeconds': 8}},
        {'id': 'comparison-badge-copy-' + name,
         'commandId': 'timeline.action.setTimelineClipTitle',
         'values': {'clipId': cid, 'values': {
             'positionY': y, 'letterSpacing': 1.98, 'color': color}}}])
for name, track, center_x, width in [('before', plates[0], 719.3, 99),
                                     ('effects', plates[1], 1629.7, 201)]:
    plan['steps'].append({'id': 'comparison-badge-plate-' + name,
        'commandId': 'timeline.action.createTimelineGraphic',
        'values': {'trackId': track['trackId'], 'shapeType': 'rectangle',
                   'startSeconds': 218, 'durationSeconds': 8,
                   'values': {'positionX': center_x / 1920, 'positionY': y,
                              'shapeWidth': width / 1920, 'shapeHeight': 48 / 1080,
                              'shapeCornerRadius': 0, 'shapeStrokeWidth': 0,
                              'backgroundColor': '#ce09111f'}}})
keys = []
for frame in range(241):
    t = frame / 30
    u = max(0, min(1, (t - 6.5) / .8))
    value = 1 - u * u * u * (10 + u * (-15 + 6 * u))
    keys.append({'localSeconds': t, 'value': value, 'interpolation': 'linear'})
keys = [k for i, k in enumerate(keys) if i == 0 or i == len(keys) - 1 or
        keys[i-1]['value'] != k['value'] or keys[i+1]['value'] != k['value']]
for name, cid in [('before-copy', before), ('before-plate', {
        'result': 'comparison-badge-plate-before', 'field': 'createdClipId'})]:
    plan['steps'].append({'id': 'comparison-badge-fade-' + name,
        'commandId': 'timeline.action.setTimelineClipAnimation',
        'values': {'clipId': cid, 'channels': [
            {'parameter': 'opacity', 'keyframes': copy.deepcopy(keys)}]}})
plan['name'] = 'P21 Film caption layout, footer anchors and comparison badges'
plan['revision'] = 3
plan['amendment'] = {
    'parent': str(parent), 'parentSha256': hashlib.sha256(parent.read_bytes()).hexdigest(),
    'source': 'FilmPolish.tsx phase3: Stage650/120 at1.1, image top35, label left/right18, top20, padding10/font16/letterSpacing1.8.',
    'reference': 'full-p16-in-progress-frame-review/reference-006705.png',
    'reason': 'Native labels sat above the image without plates/tracking; Before lacked its fade as the comparison closes.',
    'geometryQualification': '48px high plates with99/201px widths follow the reference and nativefont rounding; post-edit pixel review pending.',
    'laneAllocation': [{'label': t['displayLabel'], 'trackId': t['trackId']} for t in free[:4]],
    'preserve': 'All footage, color/effects, source timing, music and prior native polish.'}
plan.setdefault('applyGuard', {})['existingTargetIds'] = sorted({
    s['values']['clipId'] for s in plan['steps'] if isinstance(s['values'].get('clipId'), str)})
assert len(plan['steps']) == 20
result = verify_placement(plan, baseline, live)
output = HERE / 'p21-film-layout/p21-film-caption-comparison-v3.fragment.json'
with output.open('x') as stream:
    json.dump(plan, stream, indent=2)
    stream.write('\n')
print(json.dumps({'plan': str(output), 'steps': 20, 'placement': result,
                  'lanes': plan['amendment']['laneAllocation']}))
