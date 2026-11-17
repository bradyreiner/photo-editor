"""Import one exact startup grant and admit its frozen native continuation scope."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import time
from native_mcp import call

HERE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument('--prefix', required=True)
parser.add_argument('--plan', type=Path, required=True)
parser.add_argument('--grant', type=Path, required=True)
args = parser.parse_args()
assert re.fullmatch(r'[a-z0-9-]+', args.prefix)
evidence = HERE / 'evidence'
plan = json.loads(args.plan.read_text())
grants = json.loads(args.grant.read_text())
assert len(grants) == 1
grant = grants[0]
source = Path(grant['path'])
assert hashlib.sha256(source.read_bytes()).hexdigest() == grant['sha256']

def invoke(stage, tool, arguments):
    intent = evidence / f'{args.prefix}-{stage}-intent.json'
    result = evidence / f'{args.prefix}-{stage}-result.json'
    if intent.exists() or result.exists():
        raise RuntimeError('Existing intent: inspect its native state rather than replaying.')
    with intent.open('x') as target:
        json.dump({'tool': tool, 'arguments': arguments}, target)
    response = call(tool, arguments)
    with result.open('x') as target:
        json.dump(response, target, ensure_ascii=False, separators=(',', ':'))
        target.write('\n')
    envelope = response.get('result', {})
    body = envelope.get('structuredContent', {})
    return response, body, result

def success(response, body):
    return not response.get('error') and not response.get('result', {}).get('isError') and body.get('ok') is True

raw, inputs, _ = invoke('inputs', 'timeline.list_project_inputs', {})
assert success(raw, inputs) and inputs['configurationValid'] and inputs['inputCount'] == 1
assert inputs['inputs'][0]['displayName'] == source.stem
raw, imported, _ = invoke('copy', 'timeline.import_project_input',
                          {'inputId': inputs['inputs'][0]['inputId'], 'confirmed': True})
assert success(raw, imported), 'Import failed or uncertain; inspect preserved receipt, never replay.'
assert imported['sourceSha256'] == grant['sha256'] and imported['graphMatches'] and imported['readbackVerified'] and imported['libraryRegistered']
arguments = json.loads((evidence / 'native-v9-admission-arguments.json').read_text())
arguments.update(targetResourceId=imported['resourceId'], assetPaths=plan['assetPaths'],
                 outputDirectory=plan['outputDirectory'],
                 objective='Continue only the pending native trailer suffix from the verified copied checkpoint. Preserve all completed authored content and recovery. Rendered reference fidelity remains subject to review.')
for attempt in range(1, 6):
    raw, admitted, admitted_path = invoke(f'admit-attempt-{attempt}', 'timeline.admit_task', arguments)
    if success(raw, admitted):
        break
    assert admitted.get('ok') is False and admitted.get('preparationPhase') == 'settling-derived-work' and not admitted.get('taskId'), 'Admission was not an explicit no-task preparation retry; inspect state.'
    time.sleep(2)
else:
    raise RuntimeError('Native preparation did not settle; preserve all receipts and inspect.')
raw, document, _ = invoke('document', 'app.inspect_command',
                          {'commandId': 'timeline.method.inspectAgentDocument()', 'values': {}})
assert success(raw, document) and document['ready'] is True
assert document['resourceId'] == imported['resourceId']
resources = admitted['resources']
assert len(resources) == 1 and resources[0]['pending'] is False
assert resources[0]['beforeHash'] == resources[0]['currentHash'] == document['documentHash']
receipt = {'ok': True, 'resourceId': imported['resourceId'], 'taskId': admitted['taskId'],
           'documentHash': document['documentHash'], 'clipCount': imported['clipCount'],
           'trackCount': imported['trackCount'], 'sourceSha256': grant['sha256'],
           'admissionReceipt': str(admitted_path)}
with (evidence / f'{args.prefix}-bootstrap-receipt.json').open('x') as target:
    json.dump(receipt, target, indent=2)
    target.write('\n')
print(json.dumps(receipt, indent=2))
