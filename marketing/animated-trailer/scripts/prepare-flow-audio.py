"""One rhythmic licensed excerpt, picture-timed foley and measured mastering."""
from pathlib import Path
import subprocess,json,re,wave,hashlib
import numpy as np

root=Path(__file__).resolve().parents[1];out=root/'out/flow';public=root/'public/audio/flow';sr=48000;duration=138
def decode(p):
 raw=subprocess.check_output(['ffmpeg','-v','error','-i',str(p),'-ar',str(sr),'-ac','2','-f','f32le','-'])
 return np.frombuffer(raw,np.float32).reshape(-1,2).copy()
def save(p,a):
 with wave.open(str(p),'wb') as w:
  w.setnchannels(2);w.setsampwidth(2);w.setframerate(sr);w.writeframes((np.clip(a,-1,1)*32767).astype('<i2').tobytes())
music=decode(root/'public/audio/looksy-polish-sun-in-your-eyes.wav')[:duration*sr]
# Retain the exact arrangement and tempo the user selected. Remove its old
# picture-specific volume envelope before applying this cut's intro/outro.
old_times=[0,.12,3,12,28,34,42,56,68,82,86,100,114,120,126,138,148,152,156,158]
old_gain=[.5,.74,.78,.68,.74,.62,.78,.8,.82,.65,.82,.76,.78,.82,.57,.62,.72,.85,.7,0]
original_gain=np.interp(np.arange(len(music))/sr,old_times,old_gain)
music=music/np.maximum(.05,original_gain[:,None])*.25
time=np.arange(len(music))/sr
envelope=np.minimum(1,time/1.4)*np.minimum(1,(duration-time)/4.5)
music*=envelope[:,None]
foley=np.zeros_like(music);events=[]
def add(name,t,peak):
 a=decode(root/'public/audio/foley'/name);a*=peak/max(.001,float(np.abs(a).max()));i=round(t*sr);n=min(len(a),len(foley)-i)
 if n>0:foley[i:i+n]+=a[:n]
 events.append({'sound':name,'seconds':round(t,4),'peak':peak})
# Sync effects to the new drawn light, edits, layer reveals and export actions.
for t in [10.4,13.3,15.1,17.2,19.2,21.4,23.3,27.4,28.5,35.1,36.3,43.1,44.4,49.2,51.2,57.8,60.4,64.5,65.7,77.3,78.8,81.6,83.3,87.5,92.2,95.6,99,100.8,107.2,113,116.2,119.1,121,125,127.1,129.4]:add('interface-click.mp3',t,.021)
for t in [2.2,6.8,31.5,37.9,47.9,53.6,60.3,63.3,66.3,74.7,80.3,86.7,91.4,94.7,97.8,102.1,105.5,111.2,118.1,132.4]:add('quick-air-woosh.mp3',t,.018)
# Short non-musical, stereo air ribbons and soft edit impacts. No extra music bed.
rng=np.random.default_rng(709)
def air(at,length,peak,reverse=False):
 n=round(length*sr);noise=rng.standard_normal(n);from scipy.signal import butter,sosfilt
 a=sosfilt(butter(2,[450,4200],btype='bandpass',fs=sr,output='sos'),noise)
 z=np.linspace(0,1,n);env=np.sin(np.pi*z)**2;pan=np.linspace(-.8,.8,n)
 if reverse:pan=-pan
 a=a/max(.001,np.max(np.abs(a)))*env*peak
 stereo=np.stack([a*np.sqrt((1-pan)/2),a*np.sqrt((1+pan)/2)],1)
 i=round(at*sr);foley[i:i+n]+=stereo[:len(foley)-i]
 events.append({'sound':'procedural stereo air / no pitched oscillator','seconds':at,'duration':length,'peak':peak})
for at,length,peak in [(0,2.7,.026),(5.7,1.7,.016),(18.2,1.2,.017),(55.7,1.6,.02),(66.2,1.8,.026),(74.7,1.3,.018),(80.5,1.2,.014),(132.4,2.2,.023)]:air(at,length,peak)
for at in [69.85,71.08,72.93,74.8]:
 n=round(.1*sr);z=np.arange(n)/sr;impact=rng.normal(0,1,n);impact=np.convolve(impact,np.ones(24)/24,'same')*np.exp(-z*60);impact=impact/max(.001,np.max(np.abs(impact)))*.026;i=round(at*sr);foley[i:i+n]+=impact[:,None]
 events.append({'sound':'soft source cut impact','seconds':at,'peak':.026})
save(public/'foley.wav',foley);save(out/'premaster.wav',music+foley)
base=['ffmpeg','-hide_banner','-y','-i',str(out/'premaster.wav')]
first=subprocess.run(base+['-af','loudnorm=I=-17.2:TP=-2.5:LRA=10:print_format=json','-f','null','-'],capture_output=True,text=True,check=True)
m=json.loads(re.search(r'\{\s*"input_i"[\s\S]*?\}',first.stderr).group())
f=f"loudnorm=I=-17.2:TP=-2.5:LRA=10:measured_I={m['input_i']}:measured_TP={m['input_tp']}:measured_LRA={m['input_lra']}:measured_thresh={m['input_thresh']}:offset={m['target_offset']}:linear=true:print_format=json"
second=subprocess.run(base+['-af',f,'-ar',str(sr),'-ac','2','-c:a','pcm_s24le',str(public/'sun-in-your-eyes-master.wav')],capture_output=True,text=True,check=True)
measured=json.loads(re.search(r'\{\s*"input_i"[\s\S]*?\}',second.stderr).group())
(out/'sun-audio-receipt.json').write_text(json.dumps({'source':'Sun in Your Eyes / Alejandro Magaña (A. M.) / Mixkit','selection':'Explicit user choice; retained original reviewed arrangement and tempo','source_sha256':hashlib.sha256((root/'public/audio/sun-in-your-eyes.mp3').read_bytes()).hexdigest(),'excerpt_seconds':[0,138],'tempo':'Same user-selected arrangement as revision 4, tempo factor 0.972481; no new musical splice' ,'target_lufs':-17.2,'target_true_peak':-2.5,'normalization':measured,'events':events,'scope':'User-selected song. Final mix numerically measured; not aurally judged by the assistant.'},indent=2)+'\n')
print('Mastered 138-second rhythmic score and picture-timed foley.',flush=True)
