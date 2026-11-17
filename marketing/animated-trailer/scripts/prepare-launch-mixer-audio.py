"""Couple the pictured mixer fader to the original local street recording."""
from pathlib import Path
import json
import subprocess
import numpy as np

root = Path(__file__).resolve().parents[1]
source = Path('/Users/bradyreiner/Downloads/IMG_1268.mov')
rate = 48000
raw = subprocess.check_output([
    'ffmpeg', '-v', 'error', '-i', str(source), '-t', '3.2',
    '-ac', '2', '-ar', str(rate), '-f', 'f32le', '-',
])
samples = np.frombuffer(raw, np.float32).reshape(-1, 2).copy()
t = np.arange(len(samples)) / rate
# Same cubic-bezier(.65, 0, .18, 1), timing and 0.5 dB increments
# as NativeMixer: local timeline 15.5–16.7s, ambience begins at 14.8s.
x = np.clip((t - .7) / 1.2, 0, 1)
lo, hi = np.zeros_like(x), np.ones_like(x)
for _ in range(24):
    u = (lo + hi) / 2
    bx = 3 * (1-u)**2 * u * .65 + 3 * (1-u) * u**2 * .18 + u**3
    lo = np.where(bx < x, u, lo)
    hi = np.where(bx >= x, u, hi)
u = (lo + hi) / 2
eased = 3 * (1-u) * u**2 + u**3
db = np.round((-6 + 6 * eased) * 2) / 2
fade = np.minimum(np.clip(t / .12, 0, 1), np.clip((3.2-t) / .12, 0, 1))
samples *= (10 ** (db / 20) * fade)[:, None]
target = root / 'public/audio/launch-mixer-ambience.wav'
subprocess.run([
    'ffmpeg', '-v', 'error', '-f', 'f32le', '-ar', str(rate), '-ac', '2',
    '-i', '-', '-c:a', 'pcm_s24le', '-y', str(target),
], input=samples.astype(np.float32).tobytes(), check=True)
(root / 'out/launch-mixer-audio.json').write_text(json.dumps({
    'source': str(source), 'output': str(target.relative_to(root)),
    'film_start_seconds': 62.8, 'duration_seconds': 3.2,
    'gain': 'Same pictured -6 to 0 dB fader, 0.5 dB increments',
    'scope': 'Original location audio; no generated replacement sound',
}, indent=2) + '\n')
print('Prepared original ambience with the pictured fader automation.')
