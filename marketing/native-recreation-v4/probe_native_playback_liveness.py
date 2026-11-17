"""Bounded MCP transport probe; does not establish rendered-frame performance."""
import argparse
import json
import os
from pathlib import Path
import time
import urllib.request

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--key', required=True)
parser.add_argument('--pid', required=True, type=int)
parser.add_argument('--start', type=float, default=184.0)
parser.add_argument('--run-seconds', type=float, default=6.0)
args = parser.parse_args()
assert args.key.replace('-', '').isalnum() and 0 < args.run_seconds <= 10
evidence = Path(__file__).resolve().parent / 'evidence'
assert not list(evidence.glob(args.key + '-*')), 'Use a fresh receipt prefix.'
session = json.loads(Path(os.environ['LOOKSY_NATIVE_MCP_SESSION']).read_text())
assert session['pid'] == args.pid
target = 'timeline:3db4e4fb-6737-415c-b6ee-dde766e7c237'
expected_hash = '6f6bcbce033152a7d7401a075e18ab8f6c622109b8f0f7a1a0dd24c6b5cdbdb2'
guard = {'resourceId': target, 'documentHash': expected_hash}
timings = {}

def invoke(label, tool, arguments):
    intent = {'tool': tool, 'arguments': arguments}
    (evidence / f'{args.key}-{label}-intent.json').write_text(json.dumps(intent))
    message = {'jsonrpc': '2.0', 'id': label, 'method': 'tools/call',
               'params': {'name': tool, 'arguments': arguments}}
    request = urllib.request.Request(session['mcpEndpoint'],
        data=json.dumps(message).encode(), headers={
            'Authorization': 'Bearer ' + session['token'],
            'Content-Type': 'application/json', 'Accept': 'application/json'})
    started = time.monotonic()
    with urllib.request.urlopen(request, timeout=15) as response:
        result = json.loads(response.read())
    timings[label] = round((time.monotonic() - started) * 1000, 3)
    (evidence / f'{args.key}-{label}-result.json').write_text(json.dumps(result))
    envelope = result.get('result', {})
    body = envelope.get('structuredContent', {})
    assert not result.get('error') and not envelope.get('isError') and body.get('ok') is not False, body
    print(json.dumps({'step': label, 'elapsedMs': timings[label]}), flush=True)
    return body

before = invoke('before', 'timeline.inspect_document', {})
assert before['documentHash'] == expected_hash
invoke('seek', 'timeline.preview_at_time', {**guard, 'seconds': args.start})
try:
    playing = invoke('play', 'timeline.navigate_view', {**guard, 'action': 'play'})
    assert playing.get('sequencePlaying') is True or playing.get('playbackStartPending') is True, playing
    time.sleep(args.run_seconds)
finally:
    paused = invoke('pause', 'timeline.navigate_view', {**guard, 'action': 'pause'})
after = invoke('after', 'timeline.inspect_document', {})
assert paused.get('sequencePlaying') is False and after['documentHash'] == expected_hash
advanced = float(paused.get('playheadSeconds', args.start)) - args.start
summary = {'controlLivenessVerified': True, 'timingsMs': timings,
           'timelineAdvancedSeconds': advanced,
           'playbackAdvanced': advanced > 1 / 30,
           'pausedState': paused, 'documentHashUnchanged': True,
           'renderedFramePerformanceVerified': False}
(evidence / f'{args.key}-summary.json').write_text(json.dumps(summary, indent=2))
print(json.dumps(summary, indent=2))
