"""Render a short brush-keypose performance with the native Looksy brush engine."""
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
import hashlib
import json
import math
import os
import re
import subprocess
import wave
import numpy as np

ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'marketing/outputs/after-the-rain-v2'
SCORE=OUT/'motion-brush-score-v3.json'
DRIVER=ROOT/'build/release-macos/tests/native_brush_score_driver'
TEMPLATE=ROOT/'marketing/outputs/painterly-keyframe/looksy-export-session/carousels/documents/b6603aa4-4759-45fb-aba8-506ad0fd77e6.json'
FPS=24
SECONDS=4
COUNT=FPS*SECONDS


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def smooth(v):
    v=max(0,min(1,v));return v*v*(3-2*v)


def audio(path):
    rate=48000;t=np.arange(COUNT*rate//FPS)/rate
    rng=np.random.default_rng(78290)
    hiss=rng.normal(0,1,len(t));rain=np.convolve(hiss,np.ones(11)/11,mode='same')*.004
    track=rain.copy()
    for at,hz,gain in [(.25,440,.065),(1.02,554.365,.048),(1.75,659.255,.052),(2.8,880,.039)]:
        tt=t-at;positive=np.maximum(tt,0)
        envelope=(1-np.exp(-positive*180))*np.exp(-positive*2.2)*(tt>=0)
        tone=np.sin(2*np.pi*hz*tt)+.27*np.sin(2*np.pi*hz*2.003*tt)+.09*np.sin(2*np.pi*hz*3.98*tt)
        track+=tone*envelope*gain
    servo_time=np.maximum(0,t-1.4);servo_mask=(t>=1.4)&(t<1.56)
    track+=np.sin(2*np.pi*(520*servo_time+280*servo_time**2))*np.sin(np.pi*np.clip(servo_time/.16,0,1))*.008*servo_mask
    fade=np.minimum(np.clip(t/.08,0,1),np.clip((SECONDS-t)/.32,0,1))
    stereo=np.c_[track*fade,track*fade*.97]
    with wave.open(str(path),'wb') as file:
        file.setnchannels(2);file.setsampwidth(2);file.setframerate(rate)
        file.writeframes((np.clip(stereo,-1,1)*32767).astype('<i2').tobytes())


def main():
    identity=dict(driverSha256=sha(DRIVER),scoreSha256=sha(SCORE),fps=FPS,frames=COUNT,
        templateSha256=sha(TEMPLATE),method='Native brush redraws with tracked keypose geometry and a 3.5 percent camera push')
    manifest=OUT/'motion-render-manifest.json'
    if manifest.exists() and json.loads(manifest.read_text())!=identity:
        raise RuntimeError('The pinned render inputs changed; existing frames were preserved')
    manifest.write_text(json.dumps(identity,indent=2)+'\n')
    frames=OUT/'motion-frames';frames.mkdir(exist_ok=True)
    env=os.environ.copy();env['QT_QPA_PLATFORM']='offscreen'
    def frame(index):
        destination=frames/f'{index:04d}'
        if (destination/'painted-frame.png').is_file():return index
        if destination.exists():raise RuntimeError(f'Incomplete frame preserved: {destination}')
        time=index/FPS;pose=smooth((time-.45)/2.15);camera=smooth(index/(COUNT-1))
        with (frames/f'{index:04d}.log').open('wb') as log:
            subprocess.run([str(DRIVER),str(SCORE),str(TEMPLATE),str(destination),str(pose),str(camera)],
                env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
        return index
    with ThreadPoolExecutor(max_workers=3) as executor:
        completed=0
        for result in as_completed([executor.submit(frame,i) for i in range(COUNT)]):
            result.result();completed+=1
            if completed%8==0 or completed==COUNT:print(f'{completed}/{COUNT} native painted frames',flush=True)
    music=OUT/'original-motion-score.wav'
    if not music.exists():audio(music)
    movie=OUT/'after-the-rain-motion-v2.mp4'
    subprocess.run(['ffmpeg','-nostdin','-n','-hide_banner','-loglevel','error','-framerate',str(FPS),
        '-start_number','0','-i',str(frames/'%04d/painted-frame.png'),'-i',str(music),'-frames:v',str(COUNT),
        '-c:v','libx264','-preset','medium','-crf','17','-pix_fmt','yuv420p','-color_primaries','bt709',
        '-color_trc','bt709','-colorspace','bt709','-c:a','aac','-b:a','192k','-movflags','+faststart',str(movie)],check=True)
    proof=subprocess.run(['ffprobe','-v','error','-count_frames','-show_streams','-show_format','-of','json',str(movie)],capture_output=True,text=True,check=True)
    (OUT/'motion-media-inspection.json').write_text(proof.stdout)
    score=json.loads(SCORE.read_text())
    planned=sum(len(layer['strokes']) for layer in score['layers'])
    actual=[int(re.search(r'Native temporal brush frame: (\d+) marks',(frames/f'{i:04d}.log').read_text()).group(1)) for i in range(COUNT)]
    receipt=dict(**identity,width=1600,height=900,durationSeconds=SECONDS,
        plannedBrushMarksPerFrame=planned,renderedBrushMarksTotal=sum(actual),
        renderedMarksPerFrame=actual,
        movieSha256=sha(movie),movie=str(movie),
        generatedReferences=['painted-reference.png','reaction-reference.png'],
        completeCommercial=False,review='Pending visual review of rendered in-betweens and playback')
    (OUT/'motion-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print('Native brush motion movie ready:',movie,flush=True)


if __name__=='__main__':main()
