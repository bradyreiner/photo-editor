"""Run one rebuilt isolated worker with native draft and delivery tools."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import time

HERE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument('--label', default='')
parser.add_argument('--backup-receipt', default='')
parser.add_argument('--project-input', nargs=2, action='append', default=[],
                    metavar=('PATH', 'SHA256'))
args = parser.parse_args()
assert not args.label or args.label.replace('-', '').isalnum()
suffix = ('-' + args.label) if args.label else ''
ROOT = HERE.parent
receipt = ROOT / 'evidence/dense-timeline-ui-review-worker.json'
worker = json.loads(receipt.read_text())
backup_path = HERE / (args.backup_receipt or ('worker-restart-backup' + suffix + '.json'))
assert backup_path.resolve().parent == HERE
backup = json.loads(backup_path.read_text())
assert backup['verified'] and (backup.get('closedOnlyIsolatedWorker') or
    (backup.get('workerWasAlreadyTerminal') and
     (backup.get('terminalPidAbsenceVerified') is True or
      backup.get('observedExitCode') == 139 or
      (backup.get('observedExitCode') == 1 and
       backup.get('startupFailedBeforeTimelineOpened') is True))))
assert backup['pid'] == worker['pid']
assert backup['binary'] == str(Path(worker['app']) / 'Contents/MacOS/Looksy')
try:
    os.kill(backup['pid'], 0)
except ProcessLookupError:
    pass
else:
    raise RuntimeError('Old worker is still alive; never launch a duplicate.')
build = json.loads((HERE / ('release-build' + suffix + '.json')).read_text())
binary = Path(build['app']) / 'Contents/MacOS/Looksy'
assert hashlib.file_digest(binary.open('rb'), 'sha256').hexdigest() == build['binarySha256']
assert len(args.project_input) <= 8
project_inputs = []
for input_path, expected_sha in args.project_input:
    path = Path(input_path)
    assert not path.is_symlink() and path.is_file()
    canonical = path.resolve(strict=True)
    assert 0 < canonical.stat().st_size <= 256 * 1024 * 1024
    assert len(expected_sha) == 64 and all(c in '0123456789abcdef' for c in expected_sha)
    assert hashlib.file_digest(canonical.open('rb'), 'sha256').hexdigest() == expected_sha
    assert str(canonical) not in [entry['path'] for entry in project_inputs]
    project_inputs.append({'path': str(canonical), 'sha256': expected_sha})
worker.update(previousApp=worker['app'], app=build['app'], binarySha256=build['binarySha256'],
              pid=os.getpid(), launchedAt=time.time(), lastBackupReceipt=str(backup_path),
              expectedDocumentHash=backup['document']['documentHash'],
              startupProjectInputs=project_inputs)
receipt.write_text(json.dumps(worker, indent=2) + '\n')
data = Path(worker['data'])
env = dict(os.environ, PHOTO_EDITOR_APP_DATA_DIR=str(data), PHOTO_EDITOR_CACHE_DIR=worker['cache'],
           PHOTO_EDITOR_LOG_DIR=str(data/'logs'), PHOTO_EDITOR_BACKUP_DIR=str(data/'backups'),
           PHOTO_EDITOR_PREFERENCES_DIR=str(data/'preferences'), PHOTO_EDITOR_ENABLE_AUTOMATION='1',
           PHOTO_EDITOR_AUTOMATION_CAPABILITIES='read,draft,export',
           PHOTO_EDITOR_TIMELINE_EXPORT_TIMINGS='1',
           PHOTO_EDITOR_AUTOMATION_PROJECT_INPUTS=json.dumps(project_inputs))
os.execve(str(binary), [str(binary), '--enable-automation'], env)
