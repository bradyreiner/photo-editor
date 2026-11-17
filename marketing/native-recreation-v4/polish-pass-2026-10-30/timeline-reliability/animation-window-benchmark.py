"""Same exact-project animation window; no authored changes."""
import json
from pathlib import Path
import re
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
from native_review_session import WORKER, call

label, runtime = sys.argv[1:]
assert label.replace('-', '').isalnum()
out = HERE / label
out.mkdir(exist_ok=False)
document = call('timeline.inspect_document')
assert document['ready'] and document['documentHash'] == 'fa0befe66d02dde6e75bb67453ad954fd313b2e12fe2d7b74536a6e06282837a'
guard = {k: document[k] for k in ('resourceId', 'documentHash')}
seek = call('timeline.preview_at_time', dict(guard, seconds=62.5))
play = call('timeline.navigate_view', dict(guard, action='play'))
assert play['sequencePlaying'] and not play['playbackStartPending']
log = Path(runtime)
offset = log.stat().st_size
started = time.monotonic()
sample = subprocess.Popen(['/usr/bin/sample', str(WORKER['pid']), '3', '5', '-file',
                           str(out / 'main-thread.sample.txt')], stdout=subprocess.DEVNULL,
                          stderr=subprocess.DEVNULL)
try:
    time.sleep(6)
finally:
    # Read before the guarded pause RPC adds its own serialization work.
    with log.open('rb') as source:
        source.seek(offset)
        text = source.read().decode(errors='replace')
    paused = call('timeline.navigate_view', dict(guard, action='pause'))
sample.wait(timeout=45)
assert paused['documentHashUnchanged'] and not paused['sequencePlaying']
memory = int(subprocess.check_output(['/bin/ps', '-p', str(WORKER['pid']), '-o', 'rss='], text=True).strip())
rows = [dict(zip(('samples', 'maxMs', 'jank', 'dropped'), map(int, values)))
        for values in re.findall(r'frame.summary surface=timeline samples=(\d+) max=(\d+)ms jank=(\d+) dropped=(\d+)', text)]
result = {'label': label, 'pid': WORKER['pid'], 'seek': seek, 'play': play, 'pause': paused,
          'wallSecondsIncludingSampleSymbolication': time.monotonic() - started,
          'rssKiB': memory, 'frameSummaries': rows, 'steadySummaries': rows[1:],
          'authoredStateUnchanged': True,
          'qualification': 'Native foreground Qt animation telemetry; first mixed summary excluded. No input-to-photon or decoded-video FPS claim.'}
(out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
(out / 'runtime.log').write_text(text)
print(json.dumps(result))
