"""Submit one immutable native review excerpt and retain its exact receipt."""
import argparse
import json
from pathlib import Path
import time
import uuid
from native_review_session import call, save

p = argparse.ArgumentParser()
p.add_argument('--label', required=True)
p.add_argument('--start-frame', type=int, required=True)
p.add_argument('--end-frame', type=int, required=True)
args = p.parse_args()
assert args.label.replace('-', '').isalnum()
here = Path(__file__).resolve().parent
intent = here / ('excerpt-' + args.label + '-intent.json')
assert not intent.exists(), 'Inspect the retained request; never blindly replay.'
doc = call('timeline.inspect_document')
assert doc['active'], 'Open the intended native Timeline before requesting a render.'
resource_id = doc['resourceId']
for attempt in range(31):
    assert doc['active'] and doc['resourceId'] == resource_id, 'The active Timeline changed; no render was requested.'
    export_ready = doc.get('exportSnapshotReady', doc['ready'])
    if export_ready and doc['documentHash']:
        break
    assert attempt < 30, 'Native readiness did not settle; no render was requested.'
    time.sleep(1)
    doc = call('timeline.inspect_document')
assert not call('timeline.inspect_task')['active'], \
    'Save, review and release the active native editing task before submitting an export.'
jobs = call('timeline.inspect_render_jobs')
assert jobs['resourceId'] == doc['resourceId']
assert not any(j.get('status') in ['Queued', 'Running', 'Canceling']
               for j in jobs['jobs']), 'Wait for the current native render; no new request was submitted.'
values = {'resourceId': doc['resourceId'], 'documentHash': doc['documentHash'],
          'requestId': str(uuid.uuid4()), 'confirmed': True,
          'range': {'startFrame': args.start_frame, 'endFrameExclusive': args.end_frame}}
save(intent, {'arguments': values, 'document': doc, 'purpose': 'Native audiovisual review of the current edit.'})
try:
    result = call('timeline.export_h264', values)
except Exception as error:
    save(here / ('excerpt-' + args.label + '-submission-error.json'), {
        'requestId': values['requestId'], 'error': str(error),
        'nextAction': 'Inspect this exact request before retrying; a transport error is not proof of rejection.'})
    raise
save(here / ('excerpt-' + args.label + '-submitted.json'), result)
print(json.dumps(result)[:3500])
