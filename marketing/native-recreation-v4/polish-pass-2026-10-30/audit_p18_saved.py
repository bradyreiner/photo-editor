"""Audit exact saved native P18 changes against the preserved P17 checkpoint."""
import argparse
import hashlib
import json
import math
from pathlib import Path

HERE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument('--checkpoint-label', default='pass18-library-base-fixed')
parser.add_argument('--plan-file', default='pass18-library-bound-native-plan.json')
parser.add_argument('--before-file', default='native-projects/looksy-polish-pass17-photo-parity.json')
parser.add_argument('--audit-file', default='pass18-library-native-audit.json')
args = parser.parse_args()
assert args.checkpoint_label.replace('-', '').isalnum()
plan = json.loads((HERE / args.plan_file).read_text())
receipt = json.loads((HERE / ('checkpoint-' + args.checkpoint_label + '-result.json')).read_text())
before = json.loads((HERE / args.before_file).read_text())
after = json.loads(Path(receipt['outputPath']).read_text())
clips = lambda d: {c['clipId']: c for t in d['tracks'] for c in t['clips']}
membership = lambda d: {c['clipId']: t['trackId'] for t in d['tracks'] for c in t['clips']}
b, a = clips(before), clips(after)
bm, am = membership(before), membership(after)
assert set(a) == set(b), 'Existing clip identities must be preserved.'
assert [{k: v for k, v in t.items() if k != 'clips'} for t in before['tracks']] == [
    {k: v for k, v in t.items() if k != 'clips'} for t in after['tracks']]
title_fields = {'text': 'titleText', 'alignment': 'titleAlignment', 'fontSize': 'titleFontSize',
    'fontFamily': 'titleFontFamily', 'fontWeight': 'titleFontWeight', 'color': 'titleColor',
    'positionX': 'titlePositionX', 'positionY': 'titlePositionY',
    'shapeWidth': 'titleShapeWidth', 'shapeHeight': 'titleShapeHeight',
    'shapeCornerRadius': 'titleShapeCornerRadius', 'shapeStrokeColor': 'titleShapeStrokeColor',
    'shapeStrokeWidth': 'titleShapeStrokeWidth', 'backgroundColor': 'titleBackgroundColor',
    'lineHeight': 'titleLineHeight', 'letterSpacing': 'titleLetterSpacing'}
frame_fields = {'offsetX': 'frameOffsetX', 'offsetY': 'frameOffsetY', 'scale': 'frameScale',
    'rotationDegrees': 'frameRotationDegrees', 'perspectiveX': 'framePerspectiveX',
    'perspectiveY': 'framePerspectiveY', 'opacity': 'opacity', 'objectFit': 'frameMode',
    'width': 'frameWidth', 'height': 'frameHeight', 'cornerRadius': 'frameCornerRadius',
    'cropLeft': 'sourceCropLeft', 'cropTop': 'sourceCropTop',
    'cropRight': 'sourceCropRight', 'cropBottom': 'sourceCropBottom'}
allowed, expected_fields, expected_channels, moves = {}, {}, {}, {}
expected_masks = {}
for step in plan['steps']:
    values = step['values']
    cid = values['clipId']
    fields = allowed.setdefault(cid, set())
    action = step['commandId'].split('.')[-1]
    if action == 'setTimelineClipTiming':
        moves[cid] = values['trackId']
        assert values['startSeconds'] == b[cid]['startSeconds']
        assert math.isclose(values['durationSeconds'],
            (b[cid]['trimEndSeconds'] - b[cid]['trimStartSeconds']) / b[cid]['playbackRate'], abs_tol=1e-9)
    elif action in {'setTimelineClipTitle', 'setTimelineClipFrame'}:
        mapping = title_fields if action == 'setTimelineClipTitle' else frame_fields
        requested = values['values'] if action == 'setTimelineClipTitle' else values
        for key, value in requested.items():
            if key == 'clipId':
                continue
            field = mapping[key]
            # All-zero source-crop sentinels mean the complete image rectangle.
            if action == 'setTimelineClipFrame' and key in {'cropRight', 'cropBottom'} and not any(
                    values[k] for k in ['cropLeft', 'cropTop', 'cropRight', 'cropBottom']):
                value = 1
            fields.add(field)
            expected_fields.setdefault(cid, {})[field] = value
    elif action == 'setTimelineClipAnimation':
        fields.add('keyframes')
        for channel in values['channels']:
            expected_channels.setdefault(cid, {})[channel['parameter']] = channel['keyframes']
    elif action == 'setTimelineClipMask':
        fields.add('clipMask')
        expected_masks[cid] = {k: v for k, v in values.items() if k != 'clipId'}
    else:
        raise AssertionError('Unexpected mutation scope: ' + action)

equal = lambda x, y: math.isclose(x, y, rel_tol=0, abs_tol=1e-9) if (
    isinstance(x, (int, float)) and isinstance(y, (int, float))) else x == y
changes = {}
for cid, original in b.items():
    fields = {k for k in set(original) | set(a[cid]) if original.get(k) != a[cid].get(k)}
    assert fields - {'outputVariantOverrides'} <= allowed.get(cid, set()), (cid, fields)
    assert am[cid] == moves.get(cid, bm[cid]), (cid, 'Unplanned lane change')
    for variant in set(original.get('outputVariantOverrides', {})) | set(a[cid].get('outputVariantOverrides', {})):
        old = original.get('outputVariantOverrides', {}).get(variant, {})
        new = a[cid].get('outputVariantOverrides', {}).get(variant, {})
        changed = {k for k in set(old) | set(new) if old.get(k) != new.get(k)}
        assert variant == 'landscape' or not changed
        if 'framingKeyframes' in changed:
            channels = expected_channels.get(cid, {})
            assert channels, (cid, 'Unexpected variant curve update')
            base_keys = {(k['parameter'], k['seconds']): k for k in a[cid]['keyframes']}
            for key in new['framingKeyframes']:
                assert key == base_keys[(key['parameter'], key['seconds'])]
            untouched = lambda keys: [k for k in keys if k['parameter'] not in channels]
            assert untouched(old.get('framingKeyframes', [])) == untouched(new['framingKeyframes'])
            changed.remove('framingKeyframes')
        assert changed <= allowed.get(cid, set()), (cid, variant, changed)
    if fields or bm[cid] != am[cid]:
        changes[cid] = {'fields': sorted(fields), 'sourceTrackId': bm[cid], 'trackId': am[cid]}
for cid, fields in expected_fields.items():
    for field, expected in fields.items():
        assert equal(a[cid].get(field), expected), (cid, field, a[cid].get(field), expected)
for cid, channels in expected_channels.items():
    for parameter, wanted in channels.items():
        actual = sorted((k for k in a[cid]['keyframes'] if k['parameter'] == parameter), key=lambda k: k['seconds'])
        assert len(actual) == len(wanted)
        for x, y in zip(actual, wanted):
            assert equal(x['seconds'], y['localSeconds']) and equal(x['value'], y['value'])
            assert x['interpolation'] == y['interpolation']
    untouched = lambda clip: [k for k in clip['keyframes'] if k['parameter'] not in channels]
    assert untouched(b[cid]) == untouched(a[cid]), (cid, 'Unrequested keyframe channel changed')
for cid, wanted in expected_masks.items():
    for field, expected in wanted.items():
        assert equal(a[cid]['clipMask'].get(field), expected), (cid, field, 'Unexpected mask value')
sha = hashlib.file_digest(Path(receipt['outputPath']).open('rb'), 'sha256').hexdigest()
assert receipt['readbackVerified'] and sha == receipt['fileSha256']
output = {'documentHash': receipt['documentHash'], 'fileSha256': sha,
    'scopedTargets': len(allowed), 'changedClips': changes,
    'untouchedOutsideScope': len(b) - len(allowed), 'clipCount': len(a),
    'newTracks': 0, 'trackMetadataUnchanged': True,
    'allRequestedValuesAndCurvesMatch': True, 'scope': 'Native saved-state proof; post-edit pixels pending.'}
with (HERE / args.audit_file).open('x') as stream:
    json.dump(output, stream, indent=2)
    stream.write('\n')
print(json.dumps({k: v for k, v in output.items() if k != 'changedClips'}))
