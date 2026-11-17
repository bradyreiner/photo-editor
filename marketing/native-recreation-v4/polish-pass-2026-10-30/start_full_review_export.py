"""Render the current work-in-progress natively for full audiovisual review.

This releases edit ownership without claiming creative approval or final delivery.
Never replay this wrapper after an uncertain response; inspect its retained IDs.
"""
import argparse
import json
import os
from pathlib import Path
import sys
import time
import uuid
from native_review_session import ROOT, SESSION, call, save

HERE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument('--checkpoint', default='pass2')
parser.add_argument('--label', default='')
args = parser.parse_args()
assert args.checkpoint.replace('-', '').isalnum()
assert not args.label or args.label.replace('-', '').isalnum()
suffix = ('-' + args.label) if args.label else ''
intent = HERE / ('full-review-export-request' + suffix + '.json')
assert not intent.exists(), 'Inspect the existing request; never create a duplicate render.'
checkpoint_path = HERE / ('checkpoint-' + args.checkpoint + '-result.json')
checkpoint = json.loads(checkpoint_path.read_text())
doc = call('timeline.inspect_document')
assert doc['documentHash'] == checkpoint['documentHash'] and checkpoint['readbackVerified']
task = call('timeline.inspect_task')
assert task['active'] and task['status'] == 'running'
request_id = str(uuid.uuid4())
save(intent, {'requestId': request_id, 'resourceId': doc['resourceId'],
              'documentHash': doc['documentHash'], 'purpose': 'Full-length native work-in-progress review, not final creative approval.',
              'checkpointReceipt': str(checkpoint_path), 'taskId': task['taskId']})
save(HERE / (args.checkpoint + '-task-review.json'), call('timeline.finish_task', {'taskId': task['taskId'], 'action': 'review'}))
save(HERE / (args.checkpoint + '-task-release.json'), call('timeline.release_reviewed_task', {'taskId': task['taskId'], 'confirmed': True}))
# Releasing edit ownership resumes native derived work. Wait for its readiness
# contract rather than immediately attempting an export or bypassing the guard.
started = time.monotonic()
readiness = []
while True:
    observed = call('timeline.inspect_document')
    assert observed['resourceId'] == doc['resourceId']
    readiness.append({'elapsedSeconds': time.monotonic() - started,
                      'ready': observed['ready'], 'documentHash': observed['documentHash']})
    if observed['ready']:
        assert observed['documentHash'] == doc['documentHash'], 'Native revision changed; save and review the new state before exporting.'
        break
    if time.monotonic() - started > 300:
        save(HERE / ('review-export-readiness' + suffix + '.json'), readiness)
        raise RuntimeError('Background readiness did not settle; no export was submitted.')
    time.sleep(2)
save(HERE / ('review-export-readiness' + suffix + '.json'), readiness)
args = [sys.executable, str(ROOT / 'export_native_298.py'), 'landscape',
        '--resource-id', doc['resourceId'], '--document-hash', doc['documentHash'],
        '--backup-receipt', str(checkpoint_path),
        '--request-id', request_id, '--start', '--confirm-export', '--poll-seconds', '10']
env = dict(os.environ, LOOKSY_NATIVE_MCP_SESSION=str(SESSION))
os.execve(sys.executable, args, env)
