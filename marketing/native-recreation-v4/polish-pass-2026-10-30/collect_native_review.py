"""Retain a completed native excerpt and optionally decode an exact global frame."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


def sha256(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def retain_json(path, value):
    if path.exists():
        assert json.loads(path.read_text()) == value, 'Existing receipt differs.'
    else:
        with path.open('x') as stream:
            json.dump(value, stream, indent=2)
            stream.write('\n')


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--label', required=True)
parser.add_argument('--sample-frame', type=int, action='append', default=[])
args = parser.parse_args()
assert args.label.replace('-', '').isalnum()
root = Path(__file__).resolve().parent
submitted = json.loads((root / f'excerpt-{args.label}-submitted.json').read_text())
events = (root / f'excerpt-{args.label}-watch.ndjson').read_text().splitlines()
assert events, 'No retained native status.'
receipt = json.loads(events[-1])
assert receipt['requestId'] == submitted['requestId']
assert receipt['documentHash'] == submitted['documentHash']
assert receipt['status'] == 'Completed', 'Native request is not complete.'
assert receipt['reviewOutputVerification']['ok'], 'Native AV verification failed.'
integrity = receipt['outputIntegrity']
assert integrity['valid'] and integrity['algorithm'] == 'sha256-full-v1'
source = Path(receipt['outputPath'])
assert sha256(source) == integrity['sha256'], 'Native output integrity changed.'
destination = root / f'native-{args.label}'
destination.mkdir(exist_ok=True)
movie = destination / f'{args.label}.mp4'
if movie.exists():
    assert sha256(movie) == integrity['sha256'], 'Existing retained movie differs.'
else:
    with source.open('rb') as reader, movie.open('xb') as writer:
        shutil.copyfileobj(reader, writer)
assert sha256(movie) == integrity['sha256']
retain_json(destination / 'native-render-receipt.json', receipt)
span = receipt['renderRange']
for frame in args.sample_frame:
    assert span['startFrame'] <= frame < span['endFrameExclusive']
    target = destination / f'frame-{frame}.png'
    if not target.exists():
        subprocess.run([
            'ffmpeg', '-v', 'error', '-i', str(movie),
            '-vf', f'select=eq(n\\,{frame - span["startFrame"]})',
            '-frames:v', '1', '-n', str(target)
        ], check=True)
    assert target.stat().st_size > 0
print(json.dumps({'artifact': str(movie), 'sha256': integrity['sha256'],
                  'verifiedFrames': receipt['reviewOutputVerification']['decodedFrameCount'],
                  'sampleFrames': args.sample_frame}))
