#!/usr/bin/env python3
"""Efficient native-video assembly of the spectral light local review pilot.

Keeps all recorded app imagery in FFmpeg's video pipeline. The optical passes
are evaluated at 720x1280, 30 fps, then smoothly resampled to the 1080x1920 master.
"""
import hashlib
import json
import subprocess
from pathlib import Path

from PIL import Image
import render_reel as reel
from optics import Optics

OUT=reel.OUT
FPS=30


def run(args):
    subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-y',*args],check=True)


def encoder(path,size):
    return subprocess.Popen(['ffmpeg','-hide_banner','-loglevel','error','-y',
        '-f','rawvideo','-pixel_format','rgb24','-video_size',size,'-framerate',str(FPS),
        '-i','-','-an','-c:v','h264_videotoolbox','-b:v','16000000','-pix_fmt','yuv420p',
        '-vf','scale=1080:1920:out_color_matrix=bt709','-color_primaries','bt709',
        '-color_trc','bt709','-colorspace','bt709','-movflags','+faststart',str(path)],stdin=subprocess.PIPE)


def graphics(name,duration,start):
    target=OUT/(name+'.mp4')
    if target.exists():
        print('Reusing '+name,flush=True)
        return target
    optics=Optics(720,1280,samples=600)
    enc=encoder(target,'720x1280')
    for n in range(round(duration*FPS)):
        t=n/FPS
        low=optics.caustic(start+t*.65)
        # Typography is rasterized at master resolution, then sampled together
        # with the light plate so the original 60 fps renderer remains reusable.
        frame=low.resize((1080,1920),Image.Resampling.LANCZOS)
        if name=='native-opening':
            reel.title(frame,'Light,',t,y=635,size=190)
            reel.title(frame,'shaped.',max(0,t-.12),y=825,size=190)
            reel.mark(frame)
            reel.label(frame,'A LOOK INSIDE LOOKSY',(76,1768),27)
        else:
            reel.mark(frame,width=850,xy=(115,755))
            reel.title(frame,'Light. Your way.',t,y=1130,size=105)
            reel.label(frame,'RAW / CURVES / MASKS / PIXELS / sRGB',(76,1630),28)
            reel.label(frame,'RECORDED IN LOOKSY',(76,1768),27)
        enc.stdin.write(frame.resize((720,1280),Image.Resampling.LANCZOS).tobytes())
        if n%30==0:print(f'{name}: {n//30}/{duration}s',flush=True)
    enc.stdin.close()
    if enc.wait()!=0:raise RuntimeError('Optical encoder failed')
    return target


def app_clip(shot,index):
    output=OUT/f'native-shot-{index+1}.mp4'
    # Source dimensions are authoritative for the matte; no UI is recreated.
    sw,sh=shot['shadow'][2:]
    matte=reel.frame_app(Image.new('RGB',(sw,sh),reel.INK),shot,1,index)
    matte.save(OUT/f'native-matte-{index+1}.png')
    sx,sy,_,_=shot['shadow']
    px,py,pr,pb=shot['photo']
    cx,cy,cr,cb=shot['panel']
    scale=min(980/(cr-cx),610/(cb-cy))
    cw,ch=round((cr-cx)*scale/2)*2,round((cb-cy)*scale/2)*2
    speed=shot['span']/shot['duration']
    graph=(
        f'[0:v]setpts=(PTS-STARTPTS)/{speed},fps={FPS},split=2[p][c];'
        f'[p]crop={pr-px}:{pb-py}:{px+sx}:{py+sy},'
        'scale=1080:575:force_original_aspect_ratio=increase,crop=1080:575,setsar=1[photo];'
        f'[c]crop={cr-cx}:{cb-cy}:{cx+sx}:{cy+sy},scale={cw}:{ch},setsar=1[panel];'
        '[1:v][photo]overlay=0:440:shortest=1[b];'
        f'[b][panel]overlay={(1080-cw)//2}:1120:shortest=1,format=yuv420p[v]')
    run(['-hwaccel','videotoolbox','-ss',str(shot['start']),'-t',str(shot['span']),
        '-i',str(OUT/shot['file']),'-loop','1','-framerate',str(FPS),'-i',str(OUT/f'native-matte-{index+1}.png'),
        '-filter_complex',graph,'-map','[v]','-an','-t',str(shot['duration']),
        '-r',str(FPS),'-c:v','h264_videotoolbox','-b:v','16000000',
        '-color_primaries','bt709','-color_trc','bt709','-colorspace','bt709',
        '-movflags','+faststart',str(output)])
    print('Rendered native '+shot['title'],flush=True)
    return output


def main():
    OUT.mkdir(parents=True,exist_ok=True)
    clips=[graphics('native-opening',reel.INTRO,0)]
    for i,shot in enumerate(reel.SHOTS):clips.append(app_clip(shot,i))
    clips.append(graphics('native-outro',reel.OUTRO,4))
    concat=OUT/'native-concat.txt'
    concat.write_text(''.join("file '"+p.name+"'\n" for p in clips))
    reel.soundbed()
    output=OUT/'looksy-spectral-light-pilot.mp4'
    run(['-f','concat','-safe','0','-i',str(concat),'-i',str(OUT/'original-optical-sound.wav'),
        '-c:v','copy','-c:a','aac','-b:a','256k','-ar','48000',
        '-af','loudnorm=I=-16:TP=-1.5:LRA=9','-t',str(reel.DURATION),'-movflags','+faststart',str(output)])
    manifest={'status':'local review pilot; not published','duration_s':reel.DURATION,
        'width':1080,'height':1920,'fps':FPS,'optics_pass_resolution':[720,1280],
        'app_footage':'actual native window recordings; simultaneous image and inspector crops',
        'editing':'hard cuts and time compression; no fabricated interface or results',
        'optics':'thin refractive heightfield, photon-density splats and linear RGB mixing',
        'sound':'original deterministic sample-free synthesis','shots':reel.SHOTS,
        'sources':{name:hashlib.sha256((OUT/name).read_bytes()).hexdigest()
                   for name in sorted({s['file'] for s in reel.SHOTS})}}
    (OUT/'pilot-provenance.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(output,flush=True)


if __name__=='__main__':main()
