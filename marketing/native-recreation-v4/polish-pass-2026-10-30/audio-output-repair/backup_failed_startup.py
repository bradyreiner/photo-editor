"""Preserve the crashed isolated worker without starting or signaling an app."""
import ctypes
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
POLISH = HERE.parent
WORKER_PATH = POLISH.parent / 'evidence/dense-timeline-ui-review-worker.json'
worker = json.loads(WORKER_PATH.read_text())
assert worker['pid'] == 22133
runtime_log = Path('/private/tmp/looksy-audio-output-native-runtime.log')
assert 'QQmlApplicationEngine failed to load component' in runtime_log.read_text()
assert 'SelectionRangeControls.qml:31:25' in runtime_log.read_text()
observed = subprocess.run(['ps', '-p', str(worker['pid']), '-o', 'comm='],
                          capture_output=True, text=True, check=False)
assert observed.returncode == 1 and not observed.stdout.strip(), 'Worker is alive; do not back up as terminal.'
sha = lambda p: hashlib.file_digest(Path(p).open('rb'), 'sha256').hexdigest()
native = lambda p: json.loads(subprocess.check_output(
    ['/private/tmp/looksy-dense-native-hash', str(p)], text=True))
data = Path(worker['data']).resolve(strict=True)
assert data == Path('/private/tmp/looksy-dense-timeline-v7iaxqgh/data')
binary = Path(worker['app']) / 'Contents/MacOS/Looksy'
assert sha(binary) == worker['binarySha256']
assert sha(worker['source']) == worker['sourceSha256']
checkpoint = json.loads((POLISH / 'checkpoint-pass23-late-proof-result.json').read_text())
assert checkpoint['readbackVerified'] and sha(checkpoint['outputPath']) == checkpoint['fileSha256']
current_document = native(data / ('timelines/documents/' + worker['timelineId'] + '.json'))
assert current_document['timelineId'] == worker['timelineId']
current_hash = current_document['documentHash']
receipt = POLISH / 'worker-restart-backup-audio-output-native-02.json'
assert not receipt.exists(), 'Inspect the existing backup rather than replaying.'
backup = Path(tempfile.mkdtemp(prefix='looksy-crash-verified-backup-', dir='/private/tmp'))
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
    files.append({'source': str(source), 'backup': str(target), 'sha256': digest,
                  'bytes': target.stat().st_size})
for relative in ['timelines', 'agent-tasks', 'agent-runtime/tasks', 'preferences']:
    folder = data / relative
    if folder.exists():
        for source in sorted(folder.rglob('*')):
            assert not source.is_symlink()
            if source.is_file():
                clone(source, source.relative_to(data))
for source in sorted((POLISH / 'native-projects').glob('*.json')):
    clone(source, Path('owned-projects') / source.name)
clone(binary, Path('binary/Looksy'))
for entry in files:
    assert sha(entry['source']) == entry['sha256'], 'State changed during terminal backup.'
assert native(data / ('timelines/documents/' + worker['timelineId'] + '.json'))['documentHash'] == current_hash
record = {'verified': True, 'workerWasAlreadyTerminal': True, 'observedExitCode': 1, 'startupFailedBeforeTimelineOpened': True, 'runtimeLogSha256': sha(runtime_log),
          'closedOnlyIsolatedWorker': False, 'noSignalSent': True, 'pid': worker['pid'],
          'binary': str(binary), 'backupRoot': str(backup), 'files': files,
          'document': {'documentHash': current_hash}, 'canonicalDocument': current_document, 'previousReferenceHash': checkpoint['documentHash'],
          'scope': 'All native timeline/recovery/task/preferences files and owned native project copies; model weights and unrelated catalogs excluded.'}
with receipt.open('x') as stream:
    json.dump(record, stream, indent=2)
    stream.write('\n')
print(json.dumps({'backupRoot': str(backup), 'files': len(files),
                  'documentHash': current_hash, 'noSignalSent': True}))
