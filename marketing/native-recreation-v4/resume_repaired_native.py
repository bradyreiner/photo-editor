"""One-shot native copy/admission stages, each retaining its real MCP receipt."""
from pathlib import Path
import argparse
import json
from native_mcp import call

HERE = Path(__file__).resolve().parent
EVIDENCE = HERE / 'evidence'

def read(name):
    return json.loads((EVIDENCE / name).read_text())

def body(envelope):
    result = envelope.get('result', {})
    value = result.get('structuredContent', {})
    if envelope.get('error') or result.get('isError') or value.get('ok') is not True:
        raise RuntimeError('Native stage failed; inspect the preserved response before any retry.')
    return value

def invoke_once(stage, name, arguments):
    intent = EVIDENCE / f'native-import-v8-{stage}-intent.json'
    result = EVIDENCE / f'native-import-v8-{stage}-result.json'
    if intent.exists() or result.exists():
        raise RuntimeError('Stage already has an intent; do not blindly replay a mutation.')
    intent.write_text(json.dumps({'tool': name, 'arguments': arguments}, indent=2) + '\n')
    response = call(name, arguments)
    result.write_text(json.dumps(response, indent=2) + '\n')
    return body(response)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('stage', choices=['copy', 'admit', 'admit-settled', 'inspect', 'inspect-admitted',
                                         'open-relaunch', 'recover-relaunch', 'inspect-recovered'])
    stage = parser.parse_args().stage
    if stage == 'copy':
        inputs = body(read('native-import-v8-inputs-mcp.json'))['inputs']
        assert len(inputs) == 1
        value = invoke_once(stage, 'timeline.import_project_input',
                            {'inputId': inputs[0]['inputId'], 'confirmed': True})
        print(json.dumps({key: value.get(key) for key in [
            'ok', 'resourceId', 'graphMatches', 'readbackVerified', 'libraryRegistered',
            'sourceSha256', 'clipCount', 'trackCount', 'savedDocumentPath']}, indent=2))
    elif stage in ('admit', 'admit-settled'):
        if stage == 'admit-settled':
            prior = read('native-import-v8-admit-result.json')['result']['structuredContent']
            inspected = body(read('native-import-v8-inspect-result.json'))
            assert prior.get('ok') is False and prior.get('preparationPhase') == 'settling-derived-work'
            assert not prior.get('taskId') and inspected.get('ready') is True
        imported = body(read('native-import-v8-copy-result.json'))
        arguments = read('isolated-native-admission-intent.json')
        arguments['targetResourceId'] = imported['resourceId']
        arguments['objective'] = ('Continue the existing native 298-second landscape trailer from the verified '
                                  '518-clip copied checkpoint. Execute only the remaining frozen native '
                                  'authoring actions. Preserve source media and task recovery. '
                                  'Exact reference fidelity remains subject to rendered review.')
        plan = json.loads((HERE / 'full-landscape-v6-continuation.commands.json').read_text())
        assert sorted(arguments['assetPaths']) == sorted(plan['assetPaths'])
        value = invoke_once(stage, 'timeline.admit_task', arguments)
        print(json.dumps({key: value.get(key) for key in ['ok', 'taskId', 'status', 'active']}, indent=2))
    elif stage == 'open-relaunch':
        imported = body(read('native-import-v8-copy-result.json'))
        value = invoke_once(stage, 'timeline.open_saved_draft', {'timelineId': imported['timelineId']})
        print(json.dumps({key: value.get(key) for key in ['ok', 'resourceId', 'ready']}, indent=2))
    elif stage == 'recover-relaunch':
        admitted = body(read('native-import-v8-admit-settled-result.json'))
        imported = body(read('native-import-v8-copy-result.json'))
        value = invoke_once(stage, 'timeline.recover_task',
                            {'taskId': admitted['taskId'], 'targetResourceId': imported['resourceId']})
        print(json.dumps({key: value.get(key) for key in ['ok', 'taskId', 'status', 'active']}, indent=2))
    else:
        value = invoke_once(stage, 'app.inspect_command',
                            {'commandId': 'timeline.method.inspectAgentDocument()', 'values': {}})
        print(json.dumps({key: value.get(key) for key in ['ok', 'resourceId', 'documentHash']}, indent=2))

if __name__ == '__main__':
    main()
