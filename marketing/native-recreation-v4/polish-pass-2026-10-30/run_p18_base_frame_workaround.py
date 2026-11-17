"""Run reviewed clear/base/restore triplets atomically on the old worker build."""
import json
from pathlib import Path
import sys
from native_review_session import WORKER

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import author_native

assert WORKER['binarySha256'] == '7630662c8d35f37bc10a1b68983bdbae4a736eac7007b236f8e73c4ff2dfa8a3'
path = HERE / 'pass18-base-frame-workaround-bound.json'
plan = json.loads(path.read_text())
assert len(plan['steps']) == 51
author_native.BATCH_MAX_OPERATIONS = 15
for start in range(0, len(plan['steps']), 15):
    group = plan['steps'][start:start + 15]
    assert len(group) % 3 == 0
    assert author_native._build_batch_request('p18-preflight', group, {})['fitsLimits']
    for index in range(0, len(group), 3):
        triplet = group[index:index + 3]
        assert len({s['values']['clipId'] for s in triplet}) == 1
        assert [s['commandId'].split('.')[-1] for s in triplet] == [
            'setTimelineClipAnimation', 'setTimelineClipFrame', 'setTimelineClipAnimation']
author_native.execute(path, HERE / 'pass18-base-frame-workaround.execution.json',
                      HERE / 'pass18-base-frame-workaround.execution.ndjson',
                      compact_inspection=True)
