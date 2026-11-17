"""Watch one existing native export receipt; never submit or retry a render."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import time

from native_review_session import call

parser = argparse.ArgumentParser()
parser.add_argument('--label', required=True)
args = parser.parse_args()
assert args.label.replace('-', '').isalnum()
here = Path(__file__).resolve().parent
intent = json.loads((here / ('excerpt-' + args.label + '-intent.json')).read_text())
values = {key: intent['arguments'][key] for key in
          ['resourceId', 'documentHash', 'requestId']}
log = here / ('excerpt-' + args.label + '-watch.ndjson')
with log.open('x') as output:
    while True:
        result = call('timeline.inspect_export', values)
        event = {'observedAt': datetime.now(timezone.utc).isoformat(), **result}
        output.write(json.dumps(event, separators=(',', ':')) + '\n')
        output.flush()
        print(json.dumps({key: result.get(key) for key in
                         ['requestId', 'status', 'stage', 'progress',
                          'errorMessage', 'reviewOutputVerification']}), flush=True)
        if result.get('status') in ['Completed', 'Failed', 'Canceled', 'Cancelled']:
            break
        time.sleep(20)
