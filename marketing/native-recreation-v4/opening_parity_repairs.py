"""Explicit corrective commands for the retained native opening, not a rebuild."""
from copy import deepcopy
from pathlib import Path
import hashlib
import json

HERE = Path(__file__).resolve().parent

def build():
    plan_path = HERE / 'live-landscape-v4.commands.json'
    receipt_path = HERE / 'evidence/native-full-landscape-v4-receipt.json'
    plan = json.loads(plan_path.read_text())
    receipt = json.loads(receipt_path.read_text())
    steps = {step['id']: step for step in plan['steps']}
    results = {step['id']: step['result'] for step in receipt['steps']}
    image_id = results['opening/desktop-source-image']['createdClipId']
    frame_candidates = [step for step in plan['steps']
                        if step['id'] == 'opening/desktop-source-image-frame'
                        and step['values']['clipId'] == {
                            'result': 'opening/desktop-source-image', 'field': 'createdClipId'}]
    assert len(frame_candidates) == 1
    frame = deepcopy(frame_candidates[0]['values'])
    assert frame['objectFit'] == 'fit'
    frame.update(clipId=image_id, objectFit='stretch')
    repaired = [{'id': 'opening-parity/source-object-fit',
                 'commandId': 'timeline.action.setTimelineClipFrame', 'values': frame}]
    for step_id, offset in [('opening/desktop-handoff-label', 2.9),
                            ('opening/desktop-handoff-headline', 5.8)]:
        values = deepcopy(steps[step_id]['values']['values'])
        values['positionY'] += offset / 1080.0
        repaired.append({'id': 'opening-parity/' + step_id.split('/')[-1],
                         'commandId': 'timeline.action.setTimelineClipTitle',
                         'values': {'clipId': results[step_id]['createdClipId'], 'values': values}})
    return {'schema': 'looksy.native-explicit-parity-repairs/v1',
            'parentPlanSha256': hashlib.sha256(plan_path.read_bytes()).hexdigest(),
            'parentReceiptSha256': hashlib.sha256(receipt_path.read_bytes()).hexdigest(),
            'reason': 'Source uses object-fit fill and a measured31px label line box. '
                      'Native opening used fit and25.2px. Preserve all authored keyframes.',
            'steps': repaired, 'execution': 'unexecuted'}

if __name__ == '__main__':
    output = HERE / 'opening-parity-corrections.commands-fragment.json'
    repairs = build()
    if output.exists() and output.stat().st_size == 0:
        output.unlink()  # Only the empty file left by an earlier failed build.
    with output.open('x') as target:
        json.dump(repairs, target, indent=2)
        target.write('\n')
    print(output)
