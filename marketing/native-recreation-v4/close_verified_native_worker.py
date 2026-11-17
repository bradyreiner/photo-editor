"""Close only the exact isolated trailer worker after a complete verified backup."""
import argparse
import ctypes
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--backup-receipt', type=Path, required=True)
parser.add_argument('--pid', type=int, required=True)
parser.add_argument('--key', required=True)
args = parser.parse_args()
assert re.fullmatch(r'[a-z0-9-]+', args.key)
evidence = Path(__file__).resolve().parent / 'evidence'
out = evidence / (args.key + '-close.json')
assert not out.exists()
backup = json.loads(args.backup_receipt.read_text())
assert backup['verified'] and backup['timelineId'] == '3db4e4fb-6737-415c-b6ee-dde766e7c237'
data = Path(backup['sourceDataRoot'])
assert data.parent.parent == Path('/private/tmp') and data.parent.name.startswith('looksy-native-repaired')
assert json.loads((data / 'automation/session.json').read_text())['pid'] == args.pid
build_receipt = evidence / 'isolated-native-repaired16-build.json'
build = json.loads(build_receipt.read_text())
binary = Path(build['isolatedBinary'])

def sha(path):
    with path.open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()

def comm():
    return subprocess.check_output(['/bin/ps', '-p', str(args.pid), '-o', 'comm='], text=True).strip()

assert comm() == str(binary) and sha(binary) == build['isolatedBinarySha256']
autosave = data / 'timelines/documents' / (backup['timelineId'] + '.json.autosave')
expected = {Path(row['source']) for row in backup['files']}
actual = set()
for name in ('timelines', 'agent-tasks'):
    actual.update(path for path in (data / name).rglob('*') if path.is_file())
actual.update(path for path in expected if not path.is_relative_to(data))
assert actual == expected, 'Recovery inventory changed after backup.'
for row in backup['files']:
    assert sha(Path(row['backup'])) == row['sha256']
    if Path(row['source']) != autosave:
        assert sha(Path(row['source'])) == row['sha256'], row['source']

library = ctypes.CDLL('/usr/lib/libSystem.B.dylib', use_errno=True)
library.clonefile.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int]
library.clonefile.restype = ctypes.c_int

def clone(source, target):
    assert source.is_file() and not source.is_symlink() and not target.exists()
    target.parent.mkdir(parents=True, exist_ok=True)
    assert library.clonefile(os.fsencode(source), os.fsencode(target), 0) == 0
    assert sha(source) == sha(target)

backup_root = Path(backup['backupRoot'])
checkpoint = backup_root / (args.key + '-final-autosave.json')
clone(autosave, checkpoint)
native = json.loads(subprocess.check_output(
    ['/private/tmp/looksy-native-projection-hash', str(checkpoint)], text=True))
assert native['timelineId'] == backup['timelineId'] and native['documentHash'] == backup['documentHash']
archive = backup_root / (args.key + '-binary/Looksy')
clone(binary, archive)
assert comm() == str(binary) and sha(autosave) == sha(checkpoint)
record = {'authorization': 'Yes, allow backed-up worker restarts', 'pid': args.pid,
          'binary': str(binary), 'binarySha256': sha(binary), 'binaryArchive': str(archive),
          'backupReceipt': str(args.backup_receipt), 'completeInventoryVerified': True,
          'finalCheckpoint': str(checkpoint), 'documentHash': native['documentHash'],
          'regularAppUntouched': True, 'signal': 'SIGTERM',
          'startedAt': datetime.datetime.now(datetime.timezone.utc).isoformat()}
out.write_text(json.dumps(record, indent=2) + '\n')
os.kill(args.pid, signal.SIGTERM)
for _ in range(100):
    try:
        os.kill(args.pid, 0)
    except ProcessLookupError:
        break
    time.sleep(.1)
else:
    assert comm() == str(binary)
    os.kill(args.pid, signal.SIGKILL)
    record['signal'] = 'SIGTERM then SIGKILL'
record['closureRequested'] = True
out.write_text(json.dumps(record, indent=2) + '\n')
build['qualificationBlocked'] = True
build['qualificationBlockedReason'] = 'Native projection repair awaiting rebuild and verification'
build_receipt.write_text(json.dumps(build, indent=2) + '\n')
print(json.dumps({'isolatedWorkerClosed': args.pid, 'backupVerified': True,
                  'documentHash': native['documentHash']}))
