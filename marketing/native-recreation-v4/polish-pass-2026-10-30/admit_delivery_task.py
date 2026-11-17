"""Use a new owned task after the earlier task was released; preserve its journal."""
from pathlib import Path
import json
from native_review_session import call, save

here = Path(__file__).resolve().parent
assert not (here / 'delivery-task-intent.json').exists()
doc = call('timeline.inspect_document')
prior = json.loads((here / 'checkpoint-pass2-result.json').read_text())
assert doc['ready'] and doc['documentHash'] == prior['documentHash']
assert not call('timeline.inspect_task')['active']
actions = json.loads((here / 'admission-result.json').read_text())['approvedSnapshot']['actions']
values = {'targetResourceId': doc['resourceId'],
          'objective': 'Correct the saved landscape delivery preset and prepare a full native review render of the revised Looksy trailer.',
          'actions': actions, 'assetPaths': [],
          'outputDirectory': str((here / 'native-projects').resolve()), 'confirmed': True}
save(here / 'delivery-task-intent.json', {'document': doc, 'arguments': values,
    'priorContinueRejectedWithoutMutation': 'The requested native Timeline task is not active.'})
save(here / 'delivery-task-admission.json', call('timeline.admit_task', values))
save(here / 'delivery-preset-result.json', call('app.invoke_command', {
    'commandId': 'timeline.method.applyOutputPreset(QString)', 'values': {'preset': 'youtube'}}))
after = call('timeline.inspect_document')
assert (after['width'], after['height'], after['frameRate'], after['durationSeconds']) == (1920,1080,30,298)
save(here / 'delivery-preset-document.json', after)
print(json.dumps(after))
