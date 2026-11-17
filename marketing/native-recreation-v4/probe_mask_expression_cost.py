"""Matched FFmpeg experiment: cache frame-constant mask coordinates, not pixels."""
from pathlib import Path
import hashlib
import json
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(__file__).resolve().parent / 'evidence/mask-expression-cost-v2'

def balanced(rows):
    if len(rows) == 1:
        a, b = rows[0]
        return f'({a[1]:.12g}+({b[1]-a[1]:.12g})*clip((T-{a[0]:.12g})/{b[0]-a[0]:.12g},0,1))'
    middle = len(rows) // 2
    boundary = rows[middle][0][0]
    return f'if(lt(T,{boundary:.12g}),{balanced(rows[:middle])},{balanced(rows[middle:])})'

def main():
    OUT.mkdir(exist_ok=False)
    fixture = json.loads((ROOT / 'tests/fixtures/timeline_native_zoom_scale_v4.json').read_text())
    coordinates = []
    for parameter in ['mask:left', 'mask:top', 'mask:right', 'mask:bottom']:
        points = [(k['seconds'], k['value']) for k in fixture['keyframes'] if k['parameter'] == parameter]
        coordinates.append(balanced(list(zip(points, points[1:]))))
    def coverage(c):
        left, top, right, bottom = c
        width = f'(({right})-({left}))'
        height = f'(({bottom})-({top}))'
        rx, ry = f'({width}*W/2)', f'({height}*H/2)'
        dx = f'abs((X+0.5)-((({left})+({right}))*W/2))'
        dy = f'abs((Y+0.5)-((({top})+({bottom}))*H/2))'
        sd = f'(sqrt(pow(({dx})/max({rx},0.001),2)+pow(({dy})/max({ry},0.001),2))-1)*min({rx},{ry})'
        return f'if(gt({width},0)*gt({height},0),clip(0.5-({sd}),0,1),0)'
    original = coverage(coordinates)
    # geq owns a separate evaluator state per slice. The N+1 marker initializes
    # every slice on frame zero and refreshes it on each subsequent frame.
    stores = ';'.join(f'st({i},{c})' for i, c in enumerate(coordinates))
    cached = f'if(not(eq(ld(9),N+1)),{stores};st(9,N+1),0);' + coverage([f'ld({i})' for i in range(4)])
    receipt = {'fixture': 'timeline_native_zoom_scale_v4.json', 'frames': 66, 'size': [320, 180], 'runs': []}
    for name, expression in [('original', original), ('cached', cached)]:
        graph = f"nullsrc=s=320x180:r=30:d=2.2,setpts=PTS+8/TB,format=gray,geq=lum='255*({expression})',setpts=PTS-STARTPTS"
        script = OUT / f'{name}.ffgraph'
        output = OUT / f'{name}.gray'
        script.write_text(graph)
        start = time.monotonic()
        result = subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-filter_threads', '4',
                                 '-filter_complex_threads', '4', '-filter_complex_script', str(script),
                                 '-frames:v', '66', '-pix_fmt', 'gray', '-f', 'rawvideo', str(output)],
                                capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(result.stderr[-2000:])
        raw = output.read_bytes()
        receipt['runs'].append({'name': name, 'seconds': time.monotonic()-start,
                                'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest()})
    receipt['identicalPixels'] = receipt['runs'][0]['sha256'] == receipt['runs'][1]['sha256']
    (OUT / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps(receipt, indent=2))
    assert receipt['identicalPixels']

if __name__ == '__main__':
    main()
