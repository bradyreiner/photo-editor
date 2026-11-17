"""Analyze a painted reference into ordinary native brush geometry and colors.

This writes JSON only. It neither renders the painting nor imports reference
pixels into a native layer; native_brush_score_driver performs every mark.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter


def plan(reference, output, width=1600, height=900, quick=False):
    image = Image.open(reference).convert('RGB').resize((width, height), Image.Resampling.LANCZOS)
    rng = np.random.default_rng(410629)
    layers = []
    scales = [(52, .70), (25, .68), (12, .68), (6, .74), (3, .82)]
    if quick:
        scales = scales[:3]
    for index, (diameter, density) in enumerate(scales):
        # Reference-space analysis only; no intermediate raster is saved.
        sampled = np.asarray(image.filter(ImageFilter.GaussianBlur(diameter * .12)), dtype=np.float32)
        lum = sampled @ np.array([.2126, .7152, .0722], dtype=np.float32)
        gy, gx = np.gradient(lum)
        step = diameter * density
        strokes = []
        rows = np.arange(step * .3, height + step / 2, step)
        for iy, y0 in enumerate(rows):
            for x0 in np.arange(-step * .2 + (iy % 2) * step / 2, width + step / 2, step):
                x = float(np.clip(x0 + rng.uniform(-.22, .22) * step, 0, width - 1))
                y = float(np.clip(y0 + rng.uniform(-.22, .22) * step, 0, height - 1))
                ix, jy = round(x), round(y)
                angle = math.atan2(float(gy[jy, ix]), float(gx[jy, ix])) + math.pi / 2
                if math.hypot(float(gy[jy, ix]), float(gx[jy, ix])) < 1:
                    angle = rng.normal(-.4 if y < height * .55 else .05, .28)
                length = diameter * rng.uniform(.50, .90)
                color = sampled[jy, ix]
                dx, dy = math.cos(angle) * length, math.sin(angle) * length
                # End strokes before a strong material/contour transition.
                for _ in range(4):
                    a = sampled[round(np.clip(y-dy,0,height-1)), round(np.clip(x-dx,0,width-1))]
                    b = sampled[round(np.clip(y+dy,0,height-1)), round(np.clip(x+dx,0,width-1))]
                    if max(float(np.linalg.norm(a-color)),float(np.linalg.norm(b-color))) < 30 + diameter * .2:
                        break
                    dx *= .58; dy *= .58
                strokes.append([round(x,2),round(y,2),round(dx,2),round(dy,2),
                    round(diameter*rng.uniform(.86,1.15),2), *[int(round(c)) for c in color], .99])
        rng.shuffle(strokes)
        layers.append(dict(name=f'{index+1:02d} · {diameter:g}px form-following paint',strokes=strokes))
        print(f'{diameter:g}px pass: {len(strokes)} brush marks',flush=True)
    if not quick:
        # Fine hand/portrait/lens work stays concentrated in the focal exchange.
        pixels = np.asarray(image, dtype=np.float32)
        gy, gx = np.gradient(pixels @ np.array([.2126,.7152,.0722],dtype=np.float32))
        boxes = [(410,85,750,355),(605,350,855,505),(810,250,895,365),
                 (1085,245,1195,385),(40,700,205,825)]
        strokes=[]
        for x0,y0,x1,y1 in boxes:
            for y in np.arange(y0,y1,1.95):
                for x in np.arange(x0,x1,1.95):
                    xx=float(np.clip(x+rng.uniform(-.5,.5),0,width-1)); yy=float(np.clip(y+rng.uniform(-.5,.5),0,height-1))
                    ix,jy=round(xx),round(yy)
                    if math.hypot(float(gx[jy,ix]),float(gy[jy,ix])) < 2.8 and rng.random() < .65:
                        continue
                    a=math.atan2(float(gy[jy,ix]),float(gx[jy,ix]))+math.pi/2
                    strokes.append([round(xx,2),round(yy,2),round(math.cos(a)*.85,2),round(math.sin(a)*.85,2),
                        1.75,*[int(c) for c in pixels[jy,ix]],1])
        layers.append(dict(name='06 · Portrait, fingers, flower and optical glass',strokes=strokes))
        print(f'Focal detail: {len(strokes)} brush marks',flush=True)
    score=dict(schema='looksy-sampled-brush-score-v1',name='After the Rain · painted realism study v2',
        width=width,height=height,referenceProvenance=dict(method='Built-in imagegen painted reference, analyzed for stroke geometry and color',
            path=str(Path(reference).resolve()),sha256=hashlib.sha256(Path(reference).read_bytes()).hexdigest()),layers=layers)
    Path(output).write_text(json.dumps(score,separators=(',',':')))
    print('Total native marks:',sum(len(l['strokes']) for l in layers),flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('reference');parser.add_argument('output');parser.add_argument('--quick',action='store_true')
    args=parser.parse_args();plan(args.reference,args.output,quick=args.quick)
