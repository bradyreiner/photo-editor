"""One licensed electronic track, continuous at original speed, with action accents."""
from pathlib import Path
import numpy as np,subprocess,wave,json,hashlib
r=Path(__file__).resolve().parents[1];sr=48000;duration=298
out=r/'out/creative-polish-v3'
def decode(p):return np.frombuffer(subprocess.run(['ffmpeg','-v','error','-i',str(p),'-ar',str(sr),'-ac','2','-f','f32le','-'],capture_output=True,check=True).stdout,np.float32).reshape(-1,2).copy()
def smooth(t,a,b):
 u=np.clip((t-a)/(b-a),0,1);return u**3*(10+u*(-15+6*u))
p=r/'public/audio/edm-detection-mode-kevin-macleod.mp3';src=decode(p);music=src[:duration*sr].copy();active=music[np.max(np.abs(music),axis=1)>.01];gain=min(.13/(np.sqrt(np.mean(active**2))+1e-9),.76/(np.max(np.abs(music))+1e-9));music*=gain
music[:int(.18*sr)]*=np.linspace(0,1,int(.18*sr))[:,None];music[-2*sr:]*=np.linspace(1,0,2*sr)[:,None]
location=decode(r/'public/audio/creative-polish-location.wav');location*=.075/(np.sqrt(np.mean(location**2))+1e-9);location=np.clip(location,-.55,.55)
x=np.arange(6*sr)/sr;u=smooth(x,.8,2)*(1-smooth(x,4.5,5.7));music_before=music[226*sr:232*sr].copy();music[226*sr:232*sr]*=(10**(-12*u/20))[:,None];master=music.copy();master[226*sr:232*sr]+=location[:6*sr]*u[:,None]
def peaks(a):return [round(float(np.max(abs(chunk))),5) for chunk in np.array_split(a,128)]
(r/'src/creative-launch/audio-data.ts').write_text('export const audioPeaks='+json.dumps({'music':peaks(music_before),'location':peaks(location),'timeline':peaks(music[184*sr:196*sr])},separators=(',',':'))+';\n')
accents=[]
for name,times,peak in [('quick-air-woosh.mp3',[2.35,8.65,12,62,92,106,123,133,160,184,196,252,280,292],.03),('interface-click.mp3',[16.7,32.9,35.5,45.9,46.8,47.7,48.5,51.1,69,74.8,97,100.8,102.3,103.5,105.15,111.5,115.5,118.6,119.6,120.3,121.7,126.4,144.5,146.6,147.1,173.5,199.5,204,209,227,253,260,267,275,277.5],.045)]:
 a=decode(r/'public/audio/foley'/name);a*=peak/(np.max(abs(a))+1e-9)
 for at in times:
  i=round(at*sr);end=min(len(master),i+len(a));master[i:end]+=a[:end-i];accents.append({'file':name,'at':at,'peak':peak})
rng=np.random.default_rng(28)
for at,length,pan_moving in [(123.9,1.8,False),(134.4,2.9,True)]:
 n=round(length*sr);t=np.arange(n)/sr;a=rng.normal(0,1,n);a=np.convolve(a,np.ones(9)/9,mode='same')*np.sin(np.linspace(0,np.pi,n))**.6*.034;pan=smooth(t,0,length) if pan_moving else np.full(n,.5);a=np.stack([a*np.sqrt(1-pan),a*np.sqrt(pan)],axis=1);i=round(at*sr);master[i:i+n]+=a
headroom_db=1.0;master*=10**(headroom_db/20);assert np.max(abs(master))<.98
with wave.open(str(r/'public/audio/creative-polish-v3-master.wav'),'wb') as f:f.setnchannels(2);f.setsampwidth(2);f.setframerate(sr);f.writeframes((master*32767).astype('<i2').tobytes())
info={'duration':duration,'track':json.loads((out/'music-source.json').read_text()),'timeline_start':0,'source_start':0,'seconds':duration,'source_duration':len(src)/sr,'playback_rate':1,'track_changes':0,'looped':False,'music_gain':float(gain),'master_output_gain_db':headroom_db,'sample_peak':float(np.max(abs(master))),'mixer_demo_global':[226,232],'music_duck_db':'-12 * smooth(s,.8,2) * (1-smooth(s,4.5,5.7))','location_gain':'smooth(s,.8,2) * (1-smooth(s,4.5,5.7))','accents':accents,'changes':'Continuous excerpt, short entry/end fades, fixed gain and six-second demonstrated mixer duck. No tempo or pitch changes.'}
(out/'music.json').write_text(json.dumps(info,indent=2)+'\n');print('Prepared single-track 298-second score.')
