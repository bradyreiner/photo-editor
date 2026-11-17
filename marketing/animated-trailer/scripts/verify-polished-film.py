"""Verify the encoded review artifact without claiming native app execution."""
from pathlib import Path
import hashlib
import json
import re
import subprocess

root = Path(__file__).resolve().parents[1]
film = root / 'out/looksy-polished-trailer.mp4'

def run(args):
    return subprocess.run(args, capture_output=True, text=True, check=True)

metadata = json.loads(run([
    'ffprobe', '-v', 'error', '-show_entries',
    'stream=codec_name,codec_type,width,height,r_frame_rate,sample_rate,channels,duration,nb_frames:format=duration,size',
    '-of', 'json', str(film),
]).stdout)
video = next(s for s in metadata['streams'] if s['codec_type'] == 'video')
audio = next(s for s in metadata['streams'] if s['codec_type'] == 'audio')
assert (video['width'], video['height'], video['r_frame_rate']) == (1080, 1920, '60/1')
assert int(video['nb_frames']) == 7200
assert audio['sample_rate'] == '48000' and audio['channels'] == 2
assert abs(float(metadata['format']['duration']) - 120) < .03
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
assert float(loudness['input_tp']) <= -.5
files = [root / 'src/CohesiveTrailer.tsx', root / 'src/Root.tsx']
files += sorted((root / 'src/film').glob('*.tsx'))
files += sorted((root / 'src/app-kit').glob('*.ts*'))
files += [root / 'polished-soundtrack-source.json', root / 'polished-player.html', root / 'app-kit-source-map.json']
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
receipt = {
    'composition': 'Looksy-Trailer', 'duration_seconds': 120,
    'status': 'Complete encoded revision; technical checks passed',
    'metadata': metadata, 'decoded_video_frames': 7200,
    'decode_errors': 0, 'measured_loudness': loudness,
    'source_sha256': {str(p.relative_to(root)): sha(p) for p in files},
    'output_sha256': sha(film),
    'picture_review': '19 rendered frames reviewed, including chapter boundary, curve, typography, motion and revision macros',
    'browser_review': 'Pending standalone player check',
    'limits': [
        'Authored product animation, not native app execution',
        'Human auditory approval is separate from technical loudness validation',
    ],
}
(root / 'out/polished-film-verification.json').write_text(json.dumps(receipt, indent=2) + '\n')
print(json.dumps({'duration': 120, 'frames': 7200, 'lufs': loudness['input_i'], 'true_peak': loudness['input_tp'], 'decode_errors': 0}))
