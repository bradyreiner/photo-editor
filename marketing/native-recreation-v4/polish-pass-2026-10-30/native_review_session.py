"""Read/author only the explicitly isolated trailer worker; never print its token."""
from pathlib import Path
import json
import os
import sys

ROOT = Path(__file__).resolve().parents[1]
WORKER = json.loads((ROOT / 'evidence/dense-timeline-ui-review-worker.json').read_text())
SESSION = Path(WORKER['data']) / 'automation/session.json'
assert json.loads(SESSION.read_text())['pid'] == WORKER['pid']
os.environ['LOOKSY_NATIVE_MCP_SESSION'] = str(SESSION)
sys.path.insert(0, str(ROOT))
from native_mcp import call as raw_call


def call(name, values=None):
    response = raw_call(name, values or {})
    result = response.get('result', {})
    value = result.get('structuredContent', {})
    if response.get('error') or result.get('isError') or value.get('ok') is False:
        raise RuntimeError(json.dumps(response))
    if not value:
        raise RuntimeError('Expected a structured native receipt: ' + name)
    return value


def save(path, value):
    with Path(path).open('x') as output:
        json.dump(value, output, indent=2)
        output.write('\n')
