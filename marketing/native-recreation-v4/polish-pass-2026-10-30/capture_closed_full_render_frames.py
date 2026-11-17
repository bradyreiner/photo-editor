"""Retain exact frames from closed chunks of the currently owned P16 render.

These are partial production review artifacts, never a completed-film receipt.
The native full render is neither changed nor restarted.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--frame', type=int, action='append', required=True)
args = parser.parse_args()
here = Path(__file__).resolve().parent
worker = json.loads((here.parent / 'evidence/dense-timeline-ui-review-worker.json').read_text())
assert worker['pid'] == 98633
assert worker['binarySha256'] == '7630662c8d35f37bc10a1b68983bdbae4a736eac7007b236f8e73c4ff2dfa8a3'
owned = json.loads((here / 'full-review-p16-owned-render-directory.json').read_text())
assert owned['workerPid'] == worker['pid'] and len(owned['directories']) == 1
source_root = Path(owned['directories'][0])
log = Path('/private/tmp/looksy-polish-integrity-domains-native-runtime.log').read_text()
windows = [tuple(map(int, match.groups())) for match in re.finditer(
    r'timeline\.bounded\.window startFrame=(\d+) endFrame=(\d+) '
    r'generatorMs=\d+ graphMs=\d+ encodeMs=\d+ totalMs=\d+ inputs=\d+\n', log)]
assert windows and windows[0][0] == 0
assert all(left[1] == right[0] for left, right in zip(windows, windows[1:]))
destination = here / 'full-p16-in-progress-frame-review'
destination.mkdir(exist_ok=True)
sha = lambda path: hashlib.file_digest(path.open('rb'), 'sha256').hexdigest()
for frame in args.frame:
    assert 0 <= frame < windows[-1][1], 'This frame is not encoded yet.'
    index, (start, end) = next((i, span) for i, span in enumerate(windows)
                             if span[0] <= frame < span[1])
    source = source_root / f'video-{index:06d}.mp4'
    digest = sha(source)
    target = destination / f'frame-{frame:06d}.png'
    receipt = destination / f'frame-{frame:06d}.json'
    if receipt.exists():
        old = json.loads(receipt.read_text())
        assert old['chunkSha256'] == digest and sha(target) == old['frameSha256']
        print(f'Already retained exact native frame {frame}')
        continue
    assert not target.exists()
    subprocess.run(['ffmpeg', '-v', 'error', '-i', str(source), '-vf',
                    f'select=eq(n\\,{frame - start})', '-frames:v', '1', '-n',
                    str(target)], check=True)
    assert sha(source) == digest
    value = {'requestId': owned['requestId'], 'workerPid': worker['pid'],
             'binarySha256': worker['binarySha256'],
             'documentHash': '438df6c817d17155464f532f50dffb8cd718718fa4a86c83615e3fa1b342f24a',
             'globalFrame': frame, 'sequenceSeconds': frame / 30,
             'chunkIndex': index, 'chunkFrames': [start, end],
             'sourceChunk': str(source), 'chunkSha256': digest,
             'framePath': str(target), 'frameSha256': sha(target),
             'scope': 'Exact decoded frame from a closed native production chunk. '
                      'Full render, AV mux, and complete film review remain pending.'}
    with receipt.open('x') as stream:
        json.dump(value, stream, indent=2)
        stream.write('\n')
    print(f'Retained native frame {frame} at {frame / 30:.3f}s')
