"""Close only the approved isolated worker after verifying its current native backup."""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time
from native_review_session import ROOT, WORKER, call, save

HERE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument('--label', default='')
args = parser.parse_args()
assert not args.label or args.label.replace('-', '').isalnum()
receipt = HERE / ('worker-restart-backup' + ('-' + args.label if args.label else '') + '.json')
assert not receipt.exists()
data = Path(WORKER['data']).resolve(strict=True)
assert data == Path('/private/tmp/looksy-dense-timeline-v7iaxqgh/data')
binary = Path(WORKER['app']) / 'Contents/MacOS/Looksy'
pid = WORKER['pid']
comm = lambda: subprocess.check_output(['/bin/ps', '-p', str(pid), '-o', 'comm='], text=True).strip()
sha = lambda p: hashlib.file_digest(Path(p).open('rb'), 'sha256').hexdigest()
assert comm() == str(binary) and sha(binary) == WORKER['binarySha256']
doc = call('timeline.inspect_document')
assert doc.get('active') and doc.get('ready') and doc.get('documentHash'), (
    'Wait for native document readiness before creating a restart backup; '
    'the isolated worker remains open.'
)
task = call('timeline.inspect_task')
assert not task['active'] or task['status'] == 'running'
jobs = call('timeline.inspect_render_jobs')
assert jobs['resourceId'] == doc['resourceId']
assert all(j.get('status') not in ['Queued', 'Running', 'Canceling'] for j in jobs['jobs']), 'Preserve the running native render; do not restart.'
backup = Path(tempfile.mkdtemp(prefix='looksy-polish-verified-restart-', dir='/private/tmp'))
os.chmod(backup, 0o700)
lib = ctypes.CDLL('/usr/lib/libSystem.B.dylib', use_errno=True)
lib.clonefile.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int]
lib.clonefile.restype = ctypes.c_int
files = []
def clone(source, relative):
    assert source.is_file() and not source.is_symlink()
    target = backup / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    assert lib.clonefile(os.fsencode(source), os.fsencode(target), 0) == 0
    digest = sha(target)
    assert sha(source) == digest
    files.append({'source': str(source), 'backup': str(target), 'sha256': digest, 'bytes': target.stat().st_size})
for relative in ['timelines', 'agent-tasks', 'agent-runtime/tasks', 'preferences']:
    folder = data / relative
    if not folder.exists():
        continue
    for source in sorted(folder.rglob('*')):
        assert not source.is_symlink()
        if source.is_file():
            clone(source, source.relative_to(data))
for source in sorted((HERE / 'native-projects').glob('*.json')):
    clone(source, Path('owned-projects') / source.name)
clone(binary, Path('binary/Looksy'))
native = lambda p: json.loads(subprocess.check_output(['/private/tmp/looksy-dense-native-hash', str(p)], text=True))
# A clean saved document can be newer than its autosave. Preserve both and
# require a complete native file matching the current in-memory document;
# never replace a newer canonical save with an older recovery file.
candidates = [Path('timelines/documents') / (WORKER['timelineId'] + suffix)
              for suffix in ['.json', '.json.autosave']]
matching = [rel for rel in candidates if (backup / rel).is_file()
            and native(backup / rel)['documentHash'] == doc['documentHash']]
assert matching, 'No complete native backup matches the live document; keep worker open.'
assert call('timeline.inspect_document')['documentHash'] == doc['documentHash']
for row in files:
    rel = Path(row['source']).relative_to(data) if Path(row['source']).is_relative_to(data) else None
    if rel in matching:
        # Native hashes exclude updatedAt, but include every authored field.
        assert native(Path(row['source']))['documentHash'] == doc['documentHash']
        assert native(Path(row['backup']))['documentHash'] == doc['documentHash']
        row['rawAutosaveTimestampMayDiffer'] = sha(row['source']) != row['sha256']
    else:
        assert sha(row['source']) == row['sha256'], 'Live recovery changed; do not restart: ' + row['source']
assert sha(WORKER['source']) == WORKER['sourceSha256'] and comm() == str(binary)
record = {'verified': True, 'authorization': 'Yes, allow backed-up worker restarts',
          'pid': pid, 'binary': str(binary), 'backupRoot': str(backup), 'files': files,
          'document': doc, 'matchingNativeBackups': [str(backup / rel) for rel in matching],
          'task': task, 'closedOnlyIsolatedWorker': False}
save(receipt, record)
os.kill(pid, signal.SIGTERM)
for _ in range(80):
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        break
    time.sleep(.1)
else:
    assert comm() == str(binary)
    os.kill(pid, signal.SIGKILL)
    record['forceClosed'] = True
record['closedOnlyIsolatedWorker'] = True
receipt.write_text(json.dumps(record, indent=2) + '\n')
WORKER.update(expectedDocumentHash=doc['documentHash'], lastBackupReceipt=str(receipt))
(ROOT / 'evidence/dense-timeline-ui-review-worker.json').write_text(json.dumps(WORKER, indent=2) + '\n')
print(json.dumps({'verifiedBackup': str(backup), 'closedOnlyPid': pid,
                  'documentHash': doc['documentHash'], 'taskId': task.get('taskId')}))
