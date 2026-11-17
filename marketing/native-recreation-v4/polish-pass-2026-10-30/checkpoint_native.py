"""Save a native editable copy through the active reversible task, once per label."""
import argparse
import json
from pathlib import Path
from native_review_session import call, save

p = argparse.ArgumentParser()
p.add_argument('--label', required=True)
args = p.parse_args()
assert args.label.replace('-', '').isalnum()
here = Path(__file__).resolve().parent
intent = here / ('checkpoint-' + args.label + '-intent.json')
receipt = here / ('checkpoint-' + args.label + '-result.json')
output = here / 'native-projects' / ('looksy-polish-' + args.label + '.json')
assert not intent.exists() and not output.exists(), 'Inspect existing state instead of replaying a save.'
doc = call('timeline.inspect_document')
task = call('timeline.inspect_task')
assert task['active'] and task['status'] == 'running'
save(intent, {'document': doc, 'taskId': task['taskId'], 'outputPath': str(output)})
result = call('app.invoke_command', {'commandId': 'timeline.action.saveTimeline',
                                    'values': {'outputPath': str(output)}})
save(receipt, result)
print(json.dumps(result)[:2200])
