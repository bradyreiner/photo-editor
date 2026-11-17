"""Back up authored state and admit one metadata-only P17 task.

The already-running P16 job owns an immutable export snapshot. This does not
cancel/restart it, clear its caches, alter source files, or submit a new export.
An admission intent is exclusive: inspect any existing intent instead of replay.
"""
import ctypes
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

from native_review_session import WORKER, call, save

HERE = Path(__file__).resolve().parent
BASE_HASH = '438df6c817d17155464f532f50dffb8cd718718fa4a86c83615e3fa1b342f24a'
REQUEST_ID = 'f8e97299-e41d-458a-8af9-f6b004e5c5b4'
intent = HERE / 'pass17-photo-admission-intent.json'
assert not intent.exists(), 'Inspect the preserved admission receipt; never replay.'
plan = json.loads((HERE / 'p17-photo-combined-native-plan.json').read_text())
assert plan['planningSnapshot']['documentHash'] == BASE_HASH
assert plan['startingDocumentHash'] in {BASE_HASH, 'BIND_FRESH_NATIVE_HASH_AT_EXECUTION'}
assert plan['resourceId'] == 'timeline:' + WORKER['timelineId']
assert all(step['commandId'] in {
    'timeline.action.setTimelineClipTitle',
    'timeline.action.createTimelineTitle',
    'timeline.action.setTimelineClipAnimation',
} for step in plan['steps']), 'This admission is limited to title metadata.'
sha = lambda p: hashlib.file_digest(Path(p).open('rb'), 'sha256').hexdigest()
assert sha(plan['planningSnapshot']['path']) == plan['planningSnapshot']['sha256']
binary = Path(WORKER['app']) / 'Contents/MacOS/Looksy'
assert sha(binary) == WORKER['binarySha256']
assert sha(WORKER['source']) == WORKER['sourceSha256']
assert not call('timeline.inspect_task')['active']
document = call('timeline.inspect_document')
assert document['documentHash'] == BASE_HASH and document['ready']
export_args = {'resourceId': document['resourceId'],
               'documentHash': BASE_HASH, 'requestId': REQUEST_ID}
export_before = call('timeline.inspect_export', export_args)
assert export_before['status'] in {'Queued', 'Running', 'Completed'}
assert export_before['accepted']

# Clone only authoritative state. The export's growing derived MP4/chunks are
# deliberately left with their running owner; the immutable snapshot is backed up.
data = Path(WORKER['data'])
backup = Path(tempfile.mkdtemp(prefix='looksy-p17-authoring-backup-', dir='/private/tmp'))
os.chmod(backup, 0o700)
lib = ctypes.CDLL('/usr/lib/libSystem.B.dylib', use_errno=True)
lib.clonefile.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int]
lib.clonefile.restype = ctypes.c_int
files = []
for relative in ['timelines/documents', 'timelines/render-snapshots',
                 'agent-tasks', 'agent-runtime/tasks', 'preferences']:
    directory = data / relative
    if not directory.exists():
        continue
    assert not directory.is_symlink()
    for source in sorted(directory.rglob('*')):
        assert not source.is_symlink()
        if not source.is_file():
            continue
        target = backup / source.relative_to(data)
        target.parent.mkdir(parents=True, exist_ok=True)
        assert lib.clonefile(os.fsencode(source), os.fsencode(target), 0) == 0
        digest = sha(target)
        assert sha(source) == digest
        files.append({'source': str(source), 'backup': str(target), 'sha256': digest})
candidate = Path(WORKER['candidate'])
native_hash = lambda p: json.loads(subprocess.check_output(
    ['/private/tmp/looksy-dense-native-hash', str(p)], text=True))['documentHash']
assert native_hash(candidate) == BASE_HASH
assert native_hash(backup / candidate.relative_to(data)) == BASE_HASH
assert call('timeline.inspect_document')['documentHash'] == BASE_HASH
save(HERE / 'pass17-photo-authoring-backup.json', {
    'verified': True, 'backupRoot': str(backup), 'files': files,
    'documentHash': BASE_HASH, 'binarySha256': sha(binary),
    'runningExport': export_before, 'closedAnyProcess': False})
values = {
    'targetResourceId': document['resourceId'],
    'objective': 'Restore photo chapter reference typography, editable highlight/shadow readouts and the inset AFTER badge, preserving media, existing motion and all other chapters.',
    'actions': [
        {'kind': 'text', 'summary': 'Correct existing photo titles and badge placement; add editable reference-matched highlight/shadow labels.'},
        {'kind': 'framing', 'summary': 'Preserve all media transforms; adjust only specified title block geometry.'},
        {'kind': 'delivery', 'summary': 'Save and audit an exact native editable checkpoint.'},
    ],
    'assetPaths': [],
    'outputDirectory': str(HERE / 'native-projects'),
    'confirmed': True,
}
save(intent, {'document': document, 'arguments': values,
              'exportBefore': export_before, 'planSha256': sha(HERE / 'p17-photo-combined-native-plan.json')})
result = call('timeline.admit_task', values)
save(HERE / 'pass17-photo-admission-result.json', result)
after = call('timeline.inspect_export', export_args)
save(HERE / 'pass17-photo-export-preserved.json', after)
assert after['jobId'] == export_before['jobId']
assert after['documentHash'] == BASE_HASH
assert after['status'] not in {'Canceled', 'Failed'}
print(json.dumps({'admission': result, 'exportStatus': after['status'],
                  'backupRoot': str(backup)})[:3500])
