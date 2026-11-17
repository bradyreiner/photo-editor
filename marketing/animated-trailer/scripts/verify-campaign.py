"""Check encoded delivery integrity; this does not approve creative quality."""
from pathlib import Path
import hashlib
import argparse
import json
import re
import subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('source', nargs='?', default='out/looksy-launch-workflows.mp4')
parser.add_argument('--receipt', default='out/campaign/technical-verification.json')
parser.add_argument('--rec709', action='store_true')
parser.add_argument('--max-true-peak', type=float, default=-.5)
args = parser.parse_args()
film = (root / args.source).resolve()

def run(args):
    return subprocess.run(args, capture_output=True, text=True, check=True)

metadata = json.loads(run([
    'ffprobe', '-v', 'error', '-show_entries',
    'stream=codec_name,codec_type,width,height,r_frame_rate,sample_rate,channels,duration,nb_frames,color_space,color_transfer,color_primaries,color_range:format=duration,size',
    '-of', 'json', str(film),
]).stdout)
video = next(s for s in metadata['streams'] if s['codec_type'] == 'video')
audio = next(s for s in metadata['streams'] if s['codec_type'] == 'audio')
assert (video['width'], video['height'], video['r_frame_rate']) == (1080, 1920, '60/1')
assert int(video['nb_frames']) == 9240
assert audio['sample_rate'] == '48000' and audio['channels'] == 2
assert abs(float(metadata['format']['duration']) - 154) < .03
if args.rec709:
    assert [video[key] for key in ('color_space', 'color_transfer', 'color_primaries')] == ['bt709'] * 3
    assert video['color_range'] == 'tv'
decode = run(['ffmpeg', '-hide_banner', '-nostdin', '-v', 'error', '-i', str(film), '-f', 'null', '-'])
assert not decode.stderr.strip(), decode.stderr
levels = run([
    'ffmpeg', '-hide_banner', '-nostdin', '-i', str(film), '-vn',
    '-af', 'loudnorm=I=-14:TP=-1:LRA=9:print_format=json', '-f', 'null', '-',
])
match = re.search(r'\{\s*"input_i"[\s\S]*?\}', levels.stderr)
if not match:
    raise RuntimeError('Audio analysis missing')
loudness = json.loads(match.group())
assert -15 <= float(loudness['input_i']) <= -13
assert float(loudness['input_tp']) <= args.max_true_peak
files = [root / 'src/Root.tsx']
files += sorted((root / 'src/launch').glob('*.ts*'))
files += sorted((root / 'src/campaign').glob('*.ts*'))
files += [root / 'out/campaign/final-picture-patch.json', root / 'campaign-music-source.json', root / 'WORKFLOW-TRAILER.md', root / 'campaign-stock-sources.json', root / 'out/campaign/native-skin-retouch.json', root / 'out/campaign/score-arrangement.json']
files += sorted((root / 'src/app-kit').glob('*.ts*'))
files += [root / 'out/launch-workflows.html']
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
render_inputs = json.loads((root / 'out/campaign/render-inputs.json').read_text())
assert all(sha(root / name) == expected for name, expected in render_inputs.items()), 'Video source changed after the render input snapshot'
receipt = {
    'composition': 'Looksy-Launch-Workflows', 'duration_seconds': 154,
    'technical_integrity': 'passed', 'creative_approval': 'not established by this script',
    'metadata': metadata, 'decoded_video_frames': 9240, 'decode_errors': 0,
    'measured_loudness': loudness,
    'source_sha256': {str(path.relative_to(root)): sha(path) for path in files},
    'output_sha256': sha(film),
    'limits': [
        'Authored product animation, not a recording of native app execution',
        'Illustrative photo responses use conventional local transforms, not native engine exports',
        'Technical validation does not establish motion, story or music quality',
    ],
}
(root / args.receipt).write_text(json.dumps(receipt, indent=2) + '\n')
print(json.dumps({'duration': 154, 'frames': 9240, 'lufs': loudness['input_i'], 'true_peak': loudness['input_tp'], 'decode_errors': 0}))
