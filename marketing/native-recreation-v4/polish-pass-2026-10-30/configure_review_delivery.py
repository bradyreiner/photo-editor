"""Correct the saved landscape delivery preset through native owned editing."""
from pathlib import Path
import json
from native_review_session import call, save

here = Path(__file__).resolve().parent
intent = here / 'landscape-delivery-intent.json'
assert not intent.exists(), 'Inspect the previous native outcome before any retry.'
doc = call('timeline.inspect_document')
checkpoint = json.loads((here / 'checkpoint-pass2-result.json').read_text())
assert doc['documentHash'] == checkpoint['documentHash'] and doc['ready']
task_id = json.loads((here / 'admission-result.json').read_text())['taskId']
save(intent, {'document': doc, 'taskId': task_id, 'preset': 'youtube'})
save(here / 'landscape-delivery-continue.json', call('timeline.finish_task', {'taskId': task_id, 'action': 'continue'}))
task = call('timeline.inspect_task')
assert task['active'] and task['status'] == 'running' and task['taskId'] == task_id
save(here / 'landscape-delivery-result.json', call('app.invoke_command', {
    'commandId': 'timeline.method.applyOutputPreset(QString)', 'values': {'preset': 'youtube'}}))
after = call('timeline.inspect_document')
assert (after['width'], after['height'], after['frameRate'], after['durationSeconds']) == (1920,1080,30,298)
save(here / 'landscape-delivery-document.json', after)
print(json.dumps(after))
