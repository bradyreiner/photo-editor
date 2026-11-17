"""Open and immediately reattach an exact native autosave checkpoint.

Keep GUI review outside this handoff: native preparation has a deadline, after
which ordinary metadata hydration is allowed to resume. Never replay edits.
"""
import argparse
import json
import re
import time
import uuid
from pathlib import Path

from native_mcp import call


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--key', required=True)
    parser.add_argument('--timeline-id', required=True)
    parser.add_argument('--document-hash', required=True)
    parser.add_argument('--task-id', required=True)
    args = parser.parse_args()
    if not re.fullmatch(r'[a-z0-9-]+', args.key):
        raise ValueError('Use a fresh lowercase receipt prefix.')
    for value in (args.timeline_id, args.task_id):
        if str(uuid.UUID(value)) != value:
            raise ValueError('Use canonical native IDs.')
    if not re.fullmatch(r'[0-9a-f]{64}', args.document_hash):
        raise ValueError('Use the exact native document hash.')
    evidence = Path(__file__).resolve().parent / 'evidence'
    if list(evidence.glob(args.key + '-*')):
        raise RuntimeError('Preserve prior handoff evidence; reconcile before retrying.')

    def invoke(label, tool, arguments):
        intent = evidence / f'{args.key}-{label}-intent.json'
        result = evidence / f'{args.key}-{label}-result.json'
        with intent.open('x') as output:
            json.dump({'tool': tool, 'arguments': arguments}, output)
        response = call(tool, arguments)
        with result.open('x') as output:
            json.dump(response, output, separators=(',', ':'))
        envelope = response.get('result', {})
        body = envelope.get('structuredContent', {})
        if response.get('error') or envelope.get('isError') or body.get('ok') is False:
            # Preparation may legitimately require one more settling poll. A
            # timeout has no result receipt and must be reconciled separately.
            if not body.get('retryable'):
                raise RuntimeError(json.dumps(body or response.get('error')))
        return body

    state = invoke('start', 'timeline.inspect_document', {})
    if state.get('active') is not False:
        raise RuntimeError('Exact recovery handoff requires an inactive worker.')
    checkpoint = {'timelineId': args.timeline_id, 'documentHash': args.document_hash}
    opened = invoke('open', 'timeline.open_autosave_recovery', checkpoint)
    if opened.get('status') not in ('opening', 'opened'):
        raise RuntimeError('Native recovery did not begin.')
    for index in range(200):
        status = invoke(f'open-poll-{index:03d}', 'timeline.inspect_autosave_recovery', checkpoint)
        if status.get('status') == 'opened':
            break
        if status.get('status') not in ('opening', 'verifying'):
            raise RuntimeError('Native recovery stopped: ' + json.dumps(status))
        # Large native projects can spend tens of seconds constructing their
        # workspace. Keep status polling below the authenticated server budget.
        time.sleep(1.0)
    else:
        raise RuntimeError('Native recovery did not finish; no task was reattached.')

    target = 'timeline:' + args.timeline_id
    for index in range(40):
        recovered = invoke(f'reattach-{index:03d}', 'timeline.recover_task',
                           {'taskId': args.task_id, 'targetResourceId': target})
        if recovered.get('ok') is True and not recovered.get('retryable'):
            print(json.dumps({'recovered': True, 'taskId': args.task_id,
                              'resourceId': target, 'checkpointDocumentHash': args.document_hash,
                              'receiptPrefix': args.key}, indent=2))
            return
        if not recovered.get('retryable'):
            raise RuntimeError('Task reattachment stopped: ' + json.dumps(recovered))
        time.sleep(0.5)
    raise RuntimeError('Task preparation remains pending; inspect the native task before continuing.')


if __name__ == '__main__':
    main()
