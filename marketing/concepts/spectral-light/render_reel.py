#!/usr/bin/env python3
"""Edit actual Looksy window recordings with original optical motion passes.

No software interface is reconstructed. All visible app panels and results are
cropped from the continuous, unmodified native screen recordings listed below.
Run from the repository using marketing/.venv/bin/python.
"""
from pathlib import Path
import argparse
import hashlib
import json
import math
import subprocess
import wave

import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter, ImageOps
from optics import Optics

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT/'renders/spectral-light'
W, H, FPS = 1080, 1920, 60
INK, PAPER = '#050505', '#F3F3F3'
FONT = '/System/Library/Fonts/HelveticaNeue.ttc'
LOGO = Image.open(ROOT/'media/looksy-wordmark.png').convert('RGBA')
FONTS = {}

# Times refer to the native continuous recording, before editorial compression.
# Crop coordinates refer to the app surface, with window shadow removed.
SHOTS = [
    dict(title='Camera RAW.', detail='Canon EOS 80D / .CR2', file='curves-session-02.mov',
         start=14, span=14, duration=3.5, shadow=(68,68,2570,1676),
         photo=(136,430,1450,1306), panel=(1630,600,2520,1150)),
    dict(title='Shape light.', detail='Editable tone curves', file='curves-session-02.mov',
         start=90, span=15, duration=4.0, shadow=(68,68,2570,1676),
         photo=(136,430,1450,1306), panel=(1640,800,2510,1345)),
    dict(title='Select precisely.', detail='Linked masks / local adjustments', file='masks-session-03.mov',
         start=5, span=20, duration=3.5, shadow=(112,112,3024,1900),
         photo=(136,483,1904,1662), panel=(2075,335,2985,970)),
    dict(title='Mask your type.', detail='Editable text / linked selection', file='text-paint-session-04.mov',
         start=0, span=13, duration=3.5, shadow=(112,112,3024,1900),
         photo=(136,483,1904,1662), panel=(2075,335,2985,970)),
    dict(title='Paint pixels.', detail='Separate pixel layer / brush controls', file='text-paint-session-04.mov',
         start=39, span=15, duration=3.5, shadow=(112,112,3024,1900),
         photo=(136,389,1904,1568), panel=(2075,1160,2985,1750)),
    dict(title='Know your color.', detail='sRGB / preview and proof controls', file='masks-session-03.mov',
         start=47, span=11, duration=3.0, shadow=(112,112,3024,1900),
         photo=(136,389,1904,1568), panel=(2090,480,2730,880)),
]
INTRO, OUTRO = 2.0, 3.0
DURATION = INTRO + sum(s['duration'] for s in SHOTS) + OUTRO


def f(size, bold=True):
    key=(size,bold)
    if key not in FONTS:
        FONTS[key]=ImageFont.truetype(FONT,size,index=1 if bold else 0)
    return FONTS[key]


def ease(x):
    x=max(0,min(1,x))
    return x*x*x*(x*(x*6-15)+10)


def label(image, copy, xy, size, bold=False, color=PAPER):
    ImageDraw.Draw(image).text(xy,copy,font=f(size,bold),fill=color,anchor='lt')


def mark(image, width=240, xy=(76,143)):
    logo=LOGO.resize((width,round(width*LOGO.height/LOGO.width)),Image.Resampling.LANCZOS)
    image.paste(logo,xy,logo)


def title(image, copy, t, y=270, size=112):
    # Short motion-blurred entrance, followed by a stable readable hold.
    p=ease(t/.42)
    layer=Image.new('RGBA',(W,190))
    ImageDraw.Draw(layer).text((72,round((1-p)*115)),copy,font=f(size),fill=PAPER,anchor='lt')
    if p<.94:
        layer=layer.filter(ImageFilter.GaussianBlur((1-p)*3))
    image.paste(layer,(0,y),layer)


def frame_app(source, shot, local, index):
    frame=Image.new('RGB',(W,H),INK)
    mark(frame)
    label(frame,f'{index+1:02d} / LIGHT STUDIES',(798,163),23)
    size=112
    while f(size).getlength(shot['title'])>930:
        size-=2
    title(frame,shot['title'],local,y=265,size=size)
    # Two close-ups from the same timestamp in the same real recording.
    # Reframing is explicit in the footer; this is not a fabricated app layout.
    photo_box=shot['photo']
    if index==0 and local<1.05:
        p=ease(local/1.05)
        full=(0,0,source.width,source.height)
        photo_box=tuple(round(a+(b-a)*p) for a,b in zip(full,photo_box))
    photo=source.crop(photo_box)
    photo=ImageOps.fit(photo,(1080,575),method=Image.Resampling.LANCZOS)
    # A subtle camera push keeps still holds alive without moving native controls.
    push=1+.018*ease(local/shot['duration'])
    photo=photo.resize((round(1080*push),round(575*push)),Image.Resampling.LANCZOS)
    dx=(photo.width-1080)//2;dy=(photo.height-575)//2
    frame.paste(photo.crop((dx,dy,dx+1080,dy+575)),(0,440))
    label(frame,shot['detail'],(76,1050),31)
    panel=source.crop(shot['panel'])
    panel.thumbnail((980,610),Image.Resampling.LANCZOS)
    frame.paste(panel,((W-panel.width)//2,1120))
    label(frame,'LOOKSY / RECORDED APP SURFACE',(76,1780),23)
    label(frame,'Reframed / time compressed',(76,1820),22,color='#A4A4A4')
    return frame


def decoder(shot):
    x,y,w,h=shot['shadow']
    speed=shot['span']/shot['duration']
    return subprocess.Popen([
        'ffmpeg','-hide_banner','-loglevel','error','-hwaccel','videotoolbox','-ss',str(shot['start']),
        '-t',str(shot['span']),'-i',str(OUT/shot['file']),
        '-vf',f'crop={w}:{h}:{x}:{y},setpts=(PTS-STARTPTS)/{speed},fps={FPS}',
        '-frames:v',str(round(shot['duration']*FPS)),'-pix_fmt','rgb24',
        '-f','rawvideo','-',
    ],stdout=subprocess.PIPE)


def soundbed():
    rate=48000
    n=round(DURATION*rate)
    t=np.arange(n,dtype=np.float64)/rate
    rng=np.random.default_rng(2841)
    left=np.zeros(n);right=np.zeros(n)
    # Original sample-free, slowly moving harmonic bed.
    for i,hz in enumerate((55,110,164.8138,220,277.1826)):
        env=.5+.5*np.sin(t*(.21+i*.023)+i)
        v=np.sin(2*np.pi*hz*t+.08*np.sin(t*.31))*.019*env
        left+=v*(.8 if i%2 else 1);right+=v*(1 if i%2 else .8)
    cuts=[0,INTRO]
    for shot in SHOTS:
        cuts.append(cuts[-1]+shot['duration'])
    for j,at in enumerate(cuts):
        u=t-at
        active=(u>=0)&(u<.65)
        z=u[active]
        transient=np.sin(2*np.pi*(57*z+23*(1-np.exp(-z*14))/14))*np.exp(-z*10)*.17
        transient+=rng.normal(0,1,len(z))*np.exp(-z*38)*.035
        left[active]+=transient;right[active]+=transient
        # A short reverse breath anticipates the editorial cut.
        pre=(u>-.20)&(u<0)
        if np.any(pre):
            noise=rng.normal(0,1,np.count_nonzero(pre))
            noise=np.convolve(noise,np.ones(13)/13,mode='same')
            left[pre]+=noise*((u[pre]+.2)/.2)**2*.055
            right[pre]+=noise*((u[pre]+.2)/.2)**2*.055
    fade=np.minimum(t/.2,1)*np.minimum((DURATION-t)/.65,1)
    audio=np.stack((left*fade,right*fade),axis=1)
    audio=audio/max(np.max(np.abs(audio)),.01)*.71
    with wave.open(str(OUT/'original-optical-sound.wav'),'wb') as wav:
        wav.setnchannels(2);wav.setsampwidth(2);wav.setframerate(rate)
        wav.writeframes((audio*32767).astype('<i2').tobytes())


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--stills',action='store_true')
    args=parser.parse_args()
    OUT.mkdir(parents=True,exist_ok=True)
    optics=Optics()
    if args.stills:
        pictures=[]
        for i,shot in enumerate(SHOTS):
            x,y,w,h=shot['shadow']
            raw=subprocess.check_output(['ffmpeg','-v','error','-ss',str(shot['start']+shot['span']*.5),
                '-i',str(OUT/shot['file']),'-frames:v','1','-vf',f'crop={w}:{h}:{x}:{y}',
                '-f','rawvideo','-pix_fmt','rgb24','-'])
            source=Image.frombytes('RGB',(w,h),raw)
            frame=frame_app(source,shot,1,i)
            frame.save(OUT/f'layout-{i+1}.jpg',quality=95)
            pictures.append(frame.resize((270,480)))
        sheet=Image.new('RGB',(810,960),INK)
        for i,p in enumerate(pictures):sheet.paste(p,((i%3)*270,(i//3)*480))
        sheet.save(OUT/'pilot-contact-sheet.jpg',quality=96)
        return
    silent=OUT/'pilot-silent.mp4'
    enc=subprocess.Popen(['ffmpeg','-hide_banner','-loglevel','error','-y','-f','rawvideo',
        '-pixel_format','rgb24','-video_size',f'{W}x{H}','-framerate',str(FPS),'-i','-',
        '-an','-c:v','libx264','-preset','fast','-crf','17','-pix_fmt','yuv420p',
        '-vf','scale=out_color_matrix=bt709','-x264-params','colorprim=bt709:transfer=bt709:colormatrix=bt709',
        '-movflags','+faststart',str(silent)],stdin=subprocess.PIPE)
    def write(frame):enc.stdin.write(frame.tobytes())
    last=None
    opening_cache=OUT/'optical-opening-cache.mp4'
    if opening_cache.exists():
        opening=subprocess.Popen(['ffmpeg','-v','error','-i',str(opening_cache),
            '-frames:v',str(round(INTRO*FPS)),'-f','rawvideo','-pix_fmt','rgb24','-'],stdout=subprocess.PIPE)
        for n in range(round(INTRO*FPS)):
            data=opening.stdout.read(W*H*3)
            if len(data)!=W*H*3:raise RuntimeError('Opening cache ended early')
            last=Image.frombytes('RGB',(W,H),data)
            write(last)
        opening.stdout.close()
        if opening.wait()!=0:raise RuntimeError('Opening cache decoder failed')
    else:
        for n in range(round(INTRO*FPS)):
            t=n/FPS
            frame=optics.caustic(t*.9)
            # Keep the center quiet enough for the headline to remain legible.
            title(frame,'Light,',t,y=635,size=190)
            title(frame,'shaped.',max(0,t-.12),y=825,size=190)
            mark(frame)
            label(frame,'A LOOK INSIDE LOOKSY',(76,1768),27)
            write(frame);last=frame
    print('Rendered optical opening',flush=True)
    for i,shot in enumerate(SHOTS):
        dec=decoder(shot)
        w,h=shot['shadow'][2:]
        total=round(shot['duration']*FPS)
        previous=last.copy()
        for n in range(total):
            data=dec.stdout.read(w*h*3)
            if len(data)!=w*h*3:
                raise RuntimeError(f'Capture ended early: {shot["file"]}, frame {n}')
            source=Image.frombytes('RGB',(w,h),data)
            frame=frame_app(source,shot,n/FPS,i)
            if n<18:
                # A moving refracted band drives the transition between real shots.
                p=ease(n/18)
                x=np.arange(W,dtype=np.float32)[None,:]
                y=np.arange(H,dtype=np.float32)[:,None]
                wave=85*np.sin(y/240+p*3)+35*np.sin(x/170-y/410+p*2)
                edge=-650+p*(W+1300)+.17*(y-H/2)+wave
                distance=edge-x
                a=np.clip(distance/145+.5,0,1)[...,None]
                a=a*a*(3-2*a)
                old=np.asarray(previous,dtype=np.float32)
                new=np.asarray(frame,dtype=np.float32)
                # Refraction is confined to the moving optical front. Native
                # pixels remain unchanged during the readable demonstration.
                distortion=np.exp(-(distance/95)**2)
                ix=np.clip(x+22*distortion*np.sin(y/140+p*5),0,W-1).astype(np.int32)
                iy=np.clip(y+13*distortion*np.cos(x/160+p*4),0,H-1).astype(np.int32)
                new=new[iy,ix]
                band=np.exp(-(distance/9)**2)[...,None]
                optical=np.array([.75,.87,1.0],dtype=np.float32)*band*105
                frame=Image.fromarray(np.clip(old*(1-a)+new*a+optical,0,255).astype(np.uint8))
            write(frame);last=frame
        dec.stdout.close()
        if dec.wait()!=0:raise RuntimeError('Source decoder failed')
        print('Rendered '+shot['title'],flush=True)
    for n in range(round(OUTRO*FPS)):
        t=n/FPS
        frame=optics.caustic(4+t*.7)
        # The same additive caustics carry the existing RGB logo signature.
        dark=Image.new('RGB',(W,H),INK)
        frame=Image.blend(frame,dark,.24)
        mark(frame,width=850,xy=(115,755))
        title(frame,'Light. Your way.',t,y=1130,size=105)
        label(frame,'RAW / CURVES / MASKS / PIXELS / sRGB',(76,1630),28)
        label(frame,'RECORDED IN LOOKSY',(76,1768),27)
        if n<18:frame=Image.blend(last,frame,ease(n/18))
        write(frame)
    enc.stdin.close()
    if enc.wait()!=0:raise RuntimeError('Encoder failed')
    soundbed()
    movie=OUT/'looksy-spectral-light-pilot.mp4'
    subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-y','-i',str(silent),
        '-i',str(OUT/'original-optical-sound.wav'),'-c:v','copy','-c:a','aac','-b:a','256k','-ar','48000',
        '-af','loudnorm=I=-16:TP=-1.5:LRA=9','-t',str(DURATION),'-movflags','+faststart',str(movie)],check=True)
    silent.unlink()
    manifest={'status':'local review pilot; not published','duration_s':DURATION,
        'width':W,'height':H,'fps':FPS,'app_footage':'actual native window recordings',
        'editing':'simultaneous image and inspector crops; time compressed',
        'optics':'original thin-heightfield photon-density approximation; RGB dispersion',
        'sound':'original deterministic sample-free synthesis','shots':SHOTS,
        'sources':{name:hashlib.sha256((OUT/name).read_bytes()).hexdigest()
                   for name in sorted({s['file'] for s in SHOTS})}}
    (OUT/'pilot-provenance.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(movie,flush=True)


if __name__=='__main__':main()
