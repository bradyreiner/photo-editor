"""Correct a proposed shape/text conflict before any P17 mutations are run."""
import copy
import hashlib
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
parent = HERE / 'p17-photo-combined-native-plan.json'
digest = hashlib.sha256(parent.read_bytes()).hexdigest()
assert digest == json.loads((HERE / 'pass17-photo-admission-intent.json').read_text())['planSha256']
plan = json.loads(parent.read_text())
baseline = json.loads(Path(plan['planningSnapshot']['path']).read_text())
tracks = {t['displayLabel']: t for t in baseline['tracks']}
for name in ['G20', 'G21', 'G22', 'G23']:
    track = tracks[name]
    assert track['visible'] and not track['locked'] and track['opacity'] == 1
    assert track['groupTransformScale'] == 1 and not track['groupId']
    assert not [c for c in track['clips'] if c['startSeconds'] < 27 - 1e-9 and
                c['startSeconds'] + (c['trimEndSeconds'] - c['trimStartSeconds']) /
                c.get('playbackRate', 1) > 22 + 1e-9]
plates = {}
for step in plan['steps']:
    if not step['commandId'].endswith('createTimelineTitle'):
        continue
    values = step['values']
    style = values['values']
    kind = 'highlights' if 'highlights' in step['id'] else 'shadows'
    original_lane, text_lane = ('G20', 'G22') if kind == 'highlights' else ('G21', 'G23')
    assert values['trackId'] == tracks[original_lane]['trackId']
    values['trackId'] = tracks[text_lane]['trackId']
    if kind not in plates:
        plate_style = {k: copy.deepcopy(style[k]) for k in [
            'positionX', 'positionY', 'backgroundColor', 'shapeWidth',
            'shapeHeight', 'shapeCornerRadius', 'shapeStrokeColor', 'shapeStrokeWidth']}
        plates[kind] = {'id': 'photo-22/pill-' + kind + '-plate',
            'commandId': 'timeline.action.createTimelineGraphic',
            'values': {'trackId': tracks[original_lane]['trackId'],
                       'shapeType': 'rounded-rectangle', 'startSeconds': 22,
                       'durationSeconds': 5, 'values': plate_style}}
    style.update(fontSize=20.9, backgroundColor='#00000000', backgroundPadding=0)
    for key in list(style):
        if key.startswith('shape'):
            del style[key]
plan['steps'].extend(plates.values())
plan['revision'] = 2
plan['name'] += ' — separate editable pill plates'
plan['parentProposal'] = {'path': str(parent), 'sha256': digest,
                          'nativeMutationsAppliedFromParent': 0}
plan['executionContract']['mainPlanMutationCount'] = 82
plan['executionContract']['expectedBatching']['expectedMinimumBatches'] = 6
plan['applyGuard']['destinationTrackBaselines'] = [
    {'trackId': tracks[n]['trackId'], 'label': n, 'visible': True, 'locked': False,
     'existingClipCount': len(tracks[n]['clips']), 'overlapping22to27ClipIds': []}
    for n in ['G20', 'G21', 'G22', 'G23']]
plan['applyGuard']['destinationTrackRequirement'] = (
    'G20–G23 must remain visible, unlocked identity graphics lanes, free in '
    '[22,27). G20/G21 hold plates below labels on G22/G23.')
plan['revisionNotes'] = [
    'Caught before mutation: shapeType renders only a shape, so text must be a separate title.',
    'Keep all72 numeric label timings; use20.9px text (19px source under1.1 Stage scale).',
    'Two fixed-width plates intentionally avoid source auto-width chatter at one/two digits.',
    'Entry translation is19.8px after the source Stage scale; all existing media stays unchanged.']
for action in plan['postCreatePhase']['actions']:
    for channel in action['action']['valuesTemplate']['channels']:
        if channel['parameter'] == 'frameOffsetY':
            for key in channel['keyframes']:
                key['value'] *= 1.1

def smooth(t):
    u = max(0, min(1, (t - 4.65) / .35))
    return u * u * u * (10 + u * (-15 + 6 * u))

for plate in plates.values():
    channels = []
    for parameter in ['opacity', 'frameOffsetY']:
        keys = []
        for frame in range(151):
            t = frame / 30
            u = max(0, min(1, t / .45))
            entry = 1 - (1 - u) ** 4
            value = entry * (1 - smooth(t)) if parameter == 'opacity' else 19.8 / 270 * (1 - entry)
            keys.append({'localSeconds': t, 'value': value, 'interpolation': 'linear'})
        keys = [k for i, k in enumerate(keys) if i == 0 or i == len(keys) - 1 or
                keys[i-1]['value'] != k['value'] or keys[i+1]['value'] != k['value']]
        channels.append({'parameter': parameter, 'keyframes': keys})
    plan['postCreatePhase']['actions'].append({
        'id': plate['id'] + '-entry-exit', 'sourceCreateStepId': plate['id'],
        'action': {'commandId': 'timeline.action.setTimelineClipAnimation',
                   'valuesTemplate': {'channels': channels}}})
output = HERE / 'p17-photo-combined-native-plan-v2.json'
assert not output.exists()
with output.open('x') as stream:
    json.dump(plan, stream, indent=2)
    stream.write('\n')
print('Prepared82operations:72 transparent labels and2separate editable plates.')
