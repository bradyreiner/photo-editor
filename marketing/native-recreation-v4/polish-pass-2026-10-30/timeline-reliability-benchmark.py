"""Bounded foreground trailer benchmark; never changes authored content."""
import argparse
import json
import re
import statistics
import subprocess
import time
from pathlib import Path
from native_review_session import WORKER, call

HERE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument('--label', required=True)
parser.add_argument('--runtime-log', required=True)
args = parser.parse_args()
assert args.label.replace('-', '').isalnum()
out = HERE / 'timeline-reliability' / args.label
out.mkdir(parents=True, exist_ok=False)
baseline = call('timeline.inspect_document')
assert baseline['active'] and baseline['ready']
assert baseline['documentHash'] == 'fa0befe66d02dde6e75bb67453ad954fd313b2e12fe2d7b74536a6e06282837a'
guard = {k: baseline[k] for k in ('resourceId', 'documentHash')}
runtime_log = Path(args.runtime_log)
log_start = runtime_log.stat().st_size
rows = []
def measured(name, values):
    started = time.perf_counter()
    value = call(name, values)
    row = {'command': name, 'arguments': values,
           'elapsedMs': (time.perf_counter() - started) * 1000, 'result': value}
    rows.append(row)
    (out / 'calls.json').write_text(json.dumps(rows, indent=2) + '\n')
    return value
try:
    measured('timeline.navigate_view', dict(guard, action='pause'))
    for seconds in [18, 148.5, 180, 218.5, 223.5, 242, 270, 295, 218.5]:
        measured('timeline.preview_at_time', dict(guard, seconds=seconds))
    measured('timeline.navigate_view', dict(guard, action='play'))
    # Native main/render-thread samples, with no synthetic input or UI control.
    subprocess.run(['/usr/bin/sample', str(WORKER['pid']), '6', '5', '-file',
                    str(out / 'playback.sample.txt')], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
finally:
    measured('timeline.navigate_view', dict(guard, action='pause'))
final = call('timeline.inspect_document')
assert final['documentHash'] == baseline['documentHash']
perf = call('timeline.inspect_playback_performance')
(out / 'playback-performance.json').write_text(json.dumps(perf, indent=2) + '\n')
memory = subprocess.check_output(['/bin/ps', '-p', str(WORKER['pid']), '-o', 'rss='], text=True).strip()
with runtime_log.open('rb') as source:
    source.seek(log_start)
    logs = source.read().decode(errors='replace')
(out / 'runtime.log').write_text(logs)
frames = [dict(zip(('samples', 'maxMs', 'jank', 'dropped'), map(int, match)))
          for match in re.findall(r'qml.frame.summary surface=timeline samples=(\d+) max=(\d+)ms jank=(\d+) dropped=(\d+)', logs)]
seek_ms = [row['elapsedMs'] for row in rows if row['command'] == 'timeline.preview_at_time']
summary = {'label': args.label, 'pid': WORKER['pid'], 'authoredStateUnchanged': True,
           'documentHash': final['documentHash'], 'rssKiB': int(memory),
           'seekRpcMedianMs': statistics.median(seek_ms), 'seekRpcMaxMs': max(seek_ms),
           'qtFrameSummaries': frames, 'frameSamples': sum(r['samples'] for r in frames),
           'jank': sum(r['jank'] for r in frames),
           'dropped': sum(r['dropped'] for r in frames),
           'qualification': 'Exact project, foreground native QML frame telemetry and controller round-trip times; not input-to-photon latency or decoded-video FPS.'}
(out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
print(json.dumps(summary))
