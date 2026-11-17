from pathlib import Path
import subprocess,json,re,wave,hashlib
import numpy as np
from scipy.signal import butter,sosfilt,find_peaks
root=Path(__file__).resolve().parents[1];out=root/'out/rebuild/full';public=root/'public/audio/rebuild';public.mkdir(parents=True,exist_ok=True);sr=48000;duration=201.25;offset=12.
def decode(p,extra=[]):return np.frombuffer(subprocess.check_output(['ffmpeg','-v','error',*extra,'-i',str(p),'-ar',str(sr),'-ac','2','-f','f32le','-']),np.float32).reshape(-1,2).copy()
def save(p,a):
 with wave.open(str(p),'wb') as w:w.setnchannels(2);w.setsampwidth(2);w.setframerate(sr);w.writeframes((np.clip(a,-1,1)*32767).astype('<i2').tobytes())
def smooth(t,a,b):
 u=np.clip((t-a)/(b-a),0,1);return u*u*u*(10+u*(-15+6*u))
song=decode(root/'public/audio/studio/origami.mp3');assert len(song)/sr>180
music=np.zeros((round(duration*sr),2),np.float32);at=round(offset*sr);music[at:at+len(song)]=song*.32
clock=np.arange(len(music))/sr
# The soundtrack gain follows the score-fader demonstration at 2:10.
duck=smooth(clock,156.8,158.8)*(1-smooth(clock,160,161.2));music*=np.power(10,-6*duck[:,None]/20)
location=np.zeros_like(music)
night=decode(Path('/Users/bradyreiner/Downloads/IMG_1113.mov'),['-ss','2','-t','5'])
night*=.045/max(.001,float(np.abs(night).max()));start=156*sr;n=min(4*sr,len(night));location[start:start+n]=night[:n]*np.minimum(1,np.arange(n)/sr/.3)[:,None]*np.minimum(1,(n-np.arange(n))/sr/.5)[:,None]
fx=np.zeros_like(music);events=[]
def add(name,t,peak):
 a=decode(root/'public/audio/foley'/name);a*=peak/max(.001,float(np.abs(a).max()));i=round(t*sr);n=min(len(a),len(fx)-i);fx[i:i+n]+=a[:n];events.append({'sample':name,'time':t,'peak':peak})
for t in [2,6.8,12,16,19.5,22.8,29,32.5,35.5,39,42.5,49.9,53.2,57,62.8,69,70.7,77.5,82,86,90,94,101.5,105,109,115,116,118,121.9,126.8,129.8,133,137,142.5,147,151,154,156.8,161,165,168.8,172,176.5,179,182,185.5,188,192.7,196]:add('interface-click.mp3',t,.014)
for t in [1,3.5,10.5,13,27,59.5,73.5,78,91.5,98.5,111.5,124,164,173.5,192,195.5]:add('quick-air-woosh.mp3',t,.009)
rng=np.random.default_rng(42)
for a,b in [(50.05,52.6),(68.3,69.2)]:
 n=round((b-a)*sr);z=np.linspace(0,1,n);noise=sosfilt(butter(2,[600,4500],fs=sr,btype='bandpass',output='sos'),rng.normal(0,1,n));noise=noise/max(.001,np.max(np.abs(noise)))*.009*np.sin(np.pi*z)**.8;fx[round(a*sr):round(a*sr)+n]+=noise[:,None];events.append({'sample':'unpitched brush texture','time':a,'duration':b-a})
# Actual waveform and RMS measurements feed the illustrated timeline/mixer.
def waveform(a,bins=180):return [round(float(np.max(np.abs(x))),4) for x in np.array_split(a,bins)]
meters=[]
for frame in range(240):
 i=156*sr+frame*1600
 row={}
 for name,stem in [('score',music),('location',location),('effects',fx),('master',music+location+fx)]:
  rms=np.sqrt(np.mean(stem[i:i+1600]**2,axis=0));row[name]=[round(float(np.clip((20*np.log10(max(1e-7,v))+60)/60,0,1)),4) for v in rms]
 meters.append(row)
data={'meters':meters,'scoreWave':waveform(music[124*sr:155*sr].mean(axis=1)),'locationWave':waveform(night.mean(axis=1))}
(root/'src/rebuild/StoryboardAudio.ts').write_text('export const audioData='+json.dumps(data,separators=(',',':'))+';\n')
save(out/'premaster.wav',music+location+fx);save(public/'foley.wav',fx+location)
base=['ffmpeg','-hide_banner','-y','-i',str(out/'premaster.wav')]
r=subprocess.run(base+['-af','loudnorm=I=-16.5:TP=-2:LRA=11:print_format=json','-f','null','-'],capture_output=True,text=True,check=True);m=json.loads(re.search(r'\{\s*"input_i"[\s\S]*?\}',r.stderr).group());f=f"loudnorm=I=-16.5:TP=-2:LRA=11:measured_I={m['input_i']}:measured_TP={m['input_tp']}:measured_LRA={m['input_lra']}:measured_thresh={m['input_thresh']}:offset={m['target_offset']}:linear=true:print_format=json"
r=subprocess.run(base+['-af',f,'-ar',str(sr),'-c:a','pcm_s24le',str(public/'master.wav')],capture_output=True,text=True,check=True)
receipt={'duration':duration,'offset':offset,'song':'Origami / Scott Buckley','source_seconds':len(song)/sr,'looping':False,'tempo_change':False,'arrangement_change':False,'duck':[156.8,158.8,160,161.2],'events':events,'scope':'Temporary full-length timing score, not final aural approval.'}
(out/'audio-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps({'duration':duration,'song_end':offset+len(song)/sr,'master':str(public/'master.wav')},indent=2),flush=True)
