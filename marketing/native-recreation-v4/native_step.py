"""Issue one native MCP call, preserving exclusive intent/result receipts."""
from pathlib import Path
import argparse
import json
import re
import sys
from native_mcp import call

HERE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument('--key', required=True)
parser.add_argument('--tool', required=True)
inputs = parser.add_mutually_exclusive_group(required=True)
inputs.add_argument('--arguments')
inputs.add_argument('--arguments-file', type=Path)
args = parser.parse_args()
if not re.fullmatch(r'[a-z0-9-]+', args.key):
    raise ValueError('Receipt key must contain lowercase letters, digits and hyphens.')
arguments = json.loads(args.arguments if args.arguments is not None else args.arguments_file.read_text())
assert isinstance(arguments, dict)
intent = HERE / f'evidence/{args.key}-intent.json'
result = HERE / f'evidence/{args.key}-result.json'
if intent.exists() or result.exists():
    raise RuntimeError('This call already has an intent; inspect its state before choosing another action.')
with intent.open('x') as target:
    json.dump({'tool': args.tool, 'arguments': arguments}, target)
    target.write('\n')
response = call(args.tool, arguments)
with result.open('x') as target:
    json.dump(response, target, ensure_ascii=False, separators=(',', ':'))
    target.write('\n')
envelope = response.get('result', {})
body = envelope.get('structuredContent', {})
summary = {key: body.get(key) for key in [
    'ok', 'ready', 'active', 'status', 'taskId', 'resourceId', 'documentHash',
    'retryable', 'preparationPhase', 'preparationRetained', 'error',
    'sourceIntegritySettled', 'requestId', 'jobId'] if key in body}
print(json.dumps(summary, indent=2))
if response.get('error') or envelope.get('isError') or body.get('ok') is False:
    sys.exit(2)
