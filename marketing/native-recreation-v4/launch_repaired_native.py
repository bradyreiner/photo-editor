"""Launch only the fresh repair build, with its own catalog and exact input grant."""
from pathlib import Path
import argparse
import fcntl
import hashlib
import json
import os
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--version', type=int, default=1)
parser.add_argument('--resume-data', type=Path)
parser.add_argument('--resume-cache', type=Path)
parser.add_argument('--check-only', action='store_true')
parser.add_argument('--input-grant-file', type=Path)
parser.add_argument('--no-project-input', action='store_true')
args = parser.parse_args()
assert args.version >= 1
suffix = '' if args.version == 1 else str(args.version)
receipt = json.loads((HERE / f'evidence/isolated-native-repaired{suffix}-build.json').read_text())
if receipt.get('status') != 'built' or receipt.get('qualificationBlocked'):
    raise RuntimeError('This isolated worker is not qualified for launch: '
                       + str(receipt.get('qualificationBlockedReason', receipt.get('status'))))
runtime = Path(receipt['runtimeRoot'])
binary = Path(receipt['isolatedBinary'])
assert hashlib.sha256(binary.read_bytes()).hexdigest() == receipt['isolatedBinarySha256']
data = runtime / 'data-mcp'
if args.resume_data:
    data = args.resume_data.resolve(strict=True)
    if data.parent.parent != Path('/private/tmp') or not data.parent.name.startswith('looksy-native-repaired'):
        raise RuntimeError('Resume only a known isolated repair runtime.')
session_path = data / 'automation/session.json'
if session_path.exists():
    previous_pid = json.loads(session_path.read_text())['pid']
    try:
        os.kill(previous_pid, 0)  # Read-only process-liveness probe.
    except ProcessLookupError:
        pass  # The app replaces its own stale automation session on startup.
    else:
        raise RuntimeError('The prior isolated app process is still live; do not share its catalog.')

# A different isolated catalog is not permission to leave another test editor
# running. Inspect executable paths only; never read process environments or
# close an editor here. The regular checkout app is intentionally excluded.
known_test_binaries = set()
known_runtime_roots = set()
for build_receipt_path in (HERE / 'evidence').glob('isolated-native-*-build.json'):
    prior = json.loads(build_receipt_path.read_text())
    if prior.get('runtimeRoot'):
        known_runtime_roots.add(Path(prior['runtimeRoot']))
    if prior.get('isolatedBinary'):
        known_test_binaries.add(str(Path(prior['isolatedBinary']).resolve()))
processes = subprocess.run(['/bin/ps', '-axo', 'pid=,comm='],
                           check=True, capture_output=True, text=True)
live_processes = {}
for line in processes.stdout.splitlines():
    fields = line.strip().split(None, 1)
    if len(fields) == 2:
        live_processes[int(fields[0])] = fields[1]
live_test_editors = []
seen_worker_pids = set()
for prior_runtime in known_runtime_roots:
    for prior_session_path in prior_runtime.glob('data*/automation/session.json'):
        prior_session = json.loads(prior_session_path.read_text())
        worker_pid = prior_session.get('pid')
        worker_binary = live_processes.get(worker_pid)
        if worker_binary in known_test_binaries and worker_pid not in seen_worker_pids:
            seen_worker_pids.add(worker_pid)
            live_test_editors.append({'pid': worker_pid, 'binary': worker_binary,
                                      'dataRoot': str(prior_session_path.parents[1])})
if live_test_editors:
    raise RuntimeError('An isolated Looksy test editor is already running. '
                       'Save and close or reuse it before launching another: '
                       + json.dumps(live_test_editors))
# A regular user session may use a previously built test executable. Only an
# exact live PID in an isolated data root identifies a worker. The lifetime
# lock also covers the startup interval before that worker publishes a session.
worker_lock = (HERE / 'evidence/native-trailer-worker.lock').open('a+')
try:
    fcntl.flock(worker_lock.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
except BlockingIOError as error:
    raise RuntimeError('The native trailer worker launch lock is held; reuse the current worker.') from error
env = os.environ.copy()
env.pop('PHOTO_EDITOR_DATABASE_URL', None)
env.update(PHOTO_EDITOR_APP_DATA_DIR=str(data),
           PHOTO_EDITOR_CACHE_DIR=str(runtime / (data.name + '-cache')),
           PHOTO_EDITOR_LOG_DIR=str(runtime / (data.name + '-logs')),
           PHOTO_EDITOR_BACKUP_DIR=str(runtime / (data.name + '-backups')),
           PHOTO_EDITOR_ENABLE_AUTOMATION='1',
           PHOTO_EDITOR_AUTOMATION_CAPABILITIES='read,draft,export',
           PHOTO_EDITOR_AUTOMATION_PROJECT_INPUTS=json.dumps([{
               'path': str(HERE / 'projects/looksy-native-landscape-in-progress-checkpoint-2.json'),
               'sha256': 'bcd04fe53a66dafe252c4b59c31fe74f2ad82ca0a09284655e8de6af268006c2'}]))
if args.resume_cache:
    cache = args.resume_cache.resolve(strict=True)
    if (cache.parent.parent != Path('/private/tmp') or
            not cache.parent.name.startswith('looksy-native-repaired') or
            cache.name not in {'data-cache', 'data-mcp-cache'}):
        raise RuntimeError('Resume only a known isolated trailer cache.')
    env['PHOTO_EDITOR_CACHE_DIR'] = str(cache)
# The compressed Timeline journal format is opt-in for isolated v11+ authoring
# workers only. Existing sessions keep their v1 writer; the app reads both.
if args.version >= 11:
    env['PHOTO_EDITOR_TIMELINE_COMPRESSED_JOURNAL'] = '1'
else:
    env.pop('PHOTO_EDITOR_TIMELINE_COMPRESSED_JOURNAL', None)
if args.input_grant_file and args.no_project_input:
    raise RuntimeError('Choose an exact input grant or no input grant, not both.')
if args.input_grant_file:
    grants = json.loads(args.input_grant_file.read_text())
    assert isinstance(grants, list) and len(grants) == 1
    grant = grants[0]
    assert set(grant) == {'path', 'sha256'}
    source = Path(grant['path'])
    assert source.is_file() and not source.is_symlink() and source.resolve() == source
    assert hashlib.sha256(source.read_bytes()).hexdigest() == grant['sha256']
    env['PHOTO_EDITOR_AUTOMATION_PROJECT_INPUTS'] = json.dumps(grants)
elif args.no_project_input:
    env.pop('PHOTO_EDITOR_AUTOMATION_PROJECT_INPUTS', None)
if args.check_only:
    print(json.dumps({'binary': str(binary), 'binarySha256': receipt['isolatedBinarySha256'],
                      'appDataRoot': str(data), 'priorProcessLive': False,
                      'automationEnabled': env['PHOTO_EDITOR_ENABLE_AUTOMATION'],
                      'otherIsolatedEditors': live_test_editors,
                      'noProcessesLaunched': True}, indent=2))
    raise SystemExit(0)
worker_lock.seek(0)
worker_lock.truncate()
worker_lock.write(str(os.getpid()) + '\n')
worker_lock.flush()
# Keep ownership across exec into Looksy; exiting the worker releases it.
os.set_inheritable(worker_lock.fileno(), True)
subprocess.run([str(ROOT / 'rust/target/debug/catalog_local_db'), 'ensure-running'],
               env=env, cwd=ROOT, check=True)
os.execve(str(binary), [str(binary)], env)
