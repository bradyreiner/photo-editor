"""Read-only chapter sampling with user-authorized native macOS screencapture.

These are visible screen observations, not an exact decoded-frame export contract.
Keep the isolated Looksy review window exposed while running this helper.
"""
import argparse
import json
from pathlib import Path
import subprocess
import time
from native_review_session import call, save

parser = argparse.ArgumentParser()
parser.add_argument('--label', required=True)
parser.add_argument('--times', type=float, nargs='+', required=True)
args = parser.parse_args()
out = Path(__file__).resolve().parent / ('native-' + args.label)
out.mkdir(exist_ok=False)
doc = call('timeline.inspect_document')
guard = {k: doc[k] for k in ('resourceId', 'documentHash')}
rows = []
for seconds in args.times:
    seek = call('timeline.preview_at_time', dict(guard, seconds=seconds))
    # Wait for native processing to settle where reported; retain actual diagnostics
    # rather than claiming a matching frame merely because the clock changed.
    for _ in range(20):
        status = call('timeline.inspect_playback_performance')
        if not status.get('frameProcessingInFlight', False):
            break
        time.sleep(.1)
    shot = out / ('t%09.3f.png' % seconds)
    subprocess.run(['/usr/sbin/screencapture', '-x', '-R0,0,1512,982', str(shot)], check=True)
    rows.append({'requestedSeconds': seconds, 'seek': seek, 'performance': status,
                 'screenshot': str(shot), 'decodedFrameIdentityVerified': False})
    print(json.dumps({'seconds': seconds, 'screenshot': str(shot)}), flush=True)
save(out / 'receipt.json', {'document': doc, 'samples': rows,
     'after': call('timeline.inspect_document'), 'scope': 'Visible native screen samples; inspect framing and source readiness before judging.'})
