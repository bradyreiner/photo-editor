"""Verify a complete native recovery backup, then admit one reversible polish task."""
import ctypes
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
from native_review_session import ROOT, WORKER, call, save

HERE = Path(__file__).resolve().parent
receipt = HERE / 'baseline-backup.json'
assert not receipt.exists(), 'Preserve prior receipts; never replay task admission.'
data = Path(WORKER['data']).resolve(strict=True)
assert data == Path('/private/tmp/looksy-dense-timeline-v7iaxqgh/data')
binary = Path(WORKER['app']) / 'Contents/MacOS/Looksy'
assert subprocess.check_output(['/bin/ps', '-p', str(WORKER['pid']), '-o', 'comm='], text=True).strip() == str(binary)
sha = lambda p: hashlib.file_digest(Path(p).open('rb'), 'sha256').hexdigest()
assert sha(binary) == WORKER['binarySha256']
assert sha(WORKER['source']) == WORKER['sourceSha256']
assert call('timeline.inspect_task')['active'] is False
doc = call('timeline.inspect_document')
assert doc['ready'] and doc['resourceId'] == 'timeline:' + WORKER['timelineId']
backup = Path(tempfile.mkdtemp(prefix='looksy-polish-complete-baseline-', dir='/private/tmp'))
os.chmod(backup, 0o700)
lib = ctypes.CDLL('/usr/lib/libSystem.B.dylib', use_errno=True)
lib.clonefile.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int]
lib.clonefile.restype = ctypes.c_int
files = []
for relative in ['timelines', 'agent-tasks', 'agent-runtime/tasks', 'preferences']:
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
        files.append({'source': str(source), 'backup': str(target), 'sha256': digest, 'bytes': target.stat().st_size})
target = backup / 'binary/Looksy'
target.parent.mkdir(parents=True)
assert lib.clonefile(os.fsencode(binary), os.fsencode(target), 0) == 0
assert sha(target) == WORKER['binarySha256']
native = lambda p: json.loads(subprocess.check_output(['/private/tmp/looksy-dense-native-hash', str(p)], text=True))
relative = Path('timelines/documents') / (WORKER['timelineId'] + '.json.autosave')
assert native(backup / relative)['documentHash'] == doc['documentHash']
assert native(data / relative)['documentHash'] == doc['documentHash']
assert call('timeline.inspect_document')['documentHash'] == doc['documentHash']
save(receipt, {'verified': True, 'backupRoot': str(backup), 'files': files,
               'binarySha256': WORKER['binarySha256'], 'document': doc,
               'originalProjectSha256': WORKER['sourceSha256'], 'closedAnyProcess': False})
out = (HERE / 'native-projects')
out.mkdir(exist_ok=True)
actions = [{'kind': kind, 'summary': text} for kind, text in [
    ('color', 'Make demonstrated color edits visible and coherent.'),
    ('framing', 'Polish spatial composition and fluid motion.'),
    ('masking', 'Correct mask and reveal demonstrations.'),
    ('text', 'Fix copy collisions, readability and timing.'),
    ('layers', 'Keep editable native layers and coherent hierarchy.'),
    ('effects', 'Refine technically impressive connected motion graphics.'),
    ('audio', 'Polish music structure and synchronized sound.'),
    ('media', 'Review and improve shot choices using existing sources.'),
    ('delivery', 'Save native revisions and review landscape and portrait delivery.')]]
save(HERE / 'admission-intent.json', {'resourceId': doc['resourceId'], 'baselineHash': doc['documentHash'], 'actions': actions})
result = call('timeline.admit_task', {'targetResourceId': doc['resourceId'],
    'objective': 'Polish the existing full Looksy trailer, preserving successful design while correcting jank and improving the native authoring workflow.',
    'actions': actions, 'assetPaths': [], 'outputDirectory': str(out.resolve()), 'confirmed': True})
save(HERE / 'admission-result.json', result)
print(json.dumps({'backupVerified': True, 'backupRoot': str(backup), 'admission': result})[:2400])
