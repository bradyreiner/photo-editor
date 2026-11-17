"""Two distinct original-speed cues, plus quiet individually timed accents."""
from pathlib import Path
import hashlib, json, subprocess, wave
import numpy as np

root=Path(__file__).resolve().parents[1]
sr=48000
duration=216
master=np.zeros((duration*sr,2),np.float32)

def decode(path):
    raw=subprocess.run(['ffmpeg','-v','error','-i',str(path),'-ar',str(sr),'-ac','2','-f','f32le','-'],capture_output=True,check=True).stdout
    return np.frombuffer(raw,np.float32).reshape(-1,2).copy()

receipt=[]
for filename,at,source_start,length,fade_in,fade_out in [
    ('sun-in-your-eyes.mp3',0,0,105.672,.12,3.4),
    ('machine-drum-vibes-alejandro-magana.mp3',104,19.56798186,112,1.67,4),
]:
    path=root/'public/audio'/filename
    source=decode(path)
    x=source[round(source_start*sr):round((source_start+length)*sr)].copy()
    active=x[np.max(np.abs(x),axis=1)>.015]
    rms=np.sqrt(np.mean(active**2))
    gain=min(.15/(rms+1e-9),.91/(np.max(np.abs(x))+1e-9))
    x*=gain
    n=min(round(fade_in*sr),len(x));x[:n]*=np.linspace(0,1,n)[:,None]
    n=min(round(fade_out*sr),len(x));x[-n:]*=np.linspace(1,0,n)[:,None]
    start=round(at*sr);end=min(len(master),start+len(x));master[start:end]+=x[:end-start]
    receipt.append({'source':filename,'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'timeline_start':at,'source_start':source_start,'duration':len(x)/sr,'playback_rate':1,'looped':False,'gain':float(gain)})

accents=[]
for filename,times,gain in [
    ('quick-air-woosh.mp3',[2.35,8.65,12,44,66,81,104,124,136,174,198,210],.046),
    ('interface-click.mp3',[17.1,29.2,33.3,50,68,73.2,89,106.2,111.7,137.2,143.2,181,190],.07),
]:
    x=decode(root/'public/audio/foley'/filename);x*=gain/(np.max(np.abs(x))+1e-9)
    for at in times:
        start=round(at*sr);end=min(len(master),start+len(x));master[start:end]+=x[:end-start]
        accents.append({'source':filename,'at':at,'peak_gain':gain})

output=root/'public/audio/creative-launch-master.wav'
with wave.open(str(output),'wb') as f:
    f.setnchannels(2);f.setsampwidth(2);f.setframerate(sr)
    f.writeframes((np.clip(master,-.98,.98)*32767).astype('<i2').tobytes())
info={'duration_seconds':duration,'primary':'Sun in Your Eyes / Alejandro Magaña (A. M.) / explicitly chosen by user','companion':'Machine Drum Vibes / same artist / provisional companion for second half, not user-approved','catalogue':'https://mixkit.co/free-stock-music/techno/','license':'https://mixkit.co/license/modal/musicFree/','arrangement':'Each cue used once at original speed. No looping, repetition or time stretching. Companion enters at carousel chapter.','cues':receipt,'accents':accents,'true_sample_peak':float(np.max(np.abs(master))),'output':str(output.relative_to(root))}
(root/'out/creative-launch/music.json').write_text(json.dumps(info,indent=2)+'\n')
print(json.dumps({'output':str(output),'duration':duration,'peak':info['true_sample_peak'],'cues':receipt},indent=2))
