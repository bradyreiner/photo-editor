"""One rhythmic licensed excerpt, picture-timed foley and measured mastering."""
from pathlib import Path
import subprocess,json,re,wave,hashlib
import numpy as np

root=Path(__file__).resolve().parents[1];out=root/'out/pulse';public=root/'public/audio/pulse';sr=48000;duration=126
def decode(p):
 raw=subprocess.check_output(['ffmpeg','-v','error','-i',str(p),'-ar',str(sr),'-ac','2','-f','f32le','-'])
 return np.frombuffer(raw,np.float32).reshape(-1,2).copy()
def save(p,a):
 with wave.open(str(p),'wb') as w:
  w.setnchannels(2);w.setsampwidth(2);w.setframerate(sr);w.writeframes((np.clip(a,-1,1)*32767).astype('<i2').tobytes())
chapters=[(0,14,0,10),(14,24,10,22),(38,8,32,7),(46,16,39,14),(62,14,53,12),(76,24,65,23),(100,10,88,9),(110,8,97,7),(118,16,104,15),(134,8,119,7)]
def mapped(t):
 for old,length,new,seconds in chapters:
  if old<=t<old+length:return new+(t-old)*seconds/length
 return 126
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
for t in [18.2,20,22,24,32.4,40,41.5,49.3,51.2,56.8,57.85,65.4,80.4,84.5,89,94,96.5,105,112,121,128,132]:add('interface-click.mp3',mapped(t),.018)
for t in [11.3,37.1,45.1,55.5,75.2,99.2,110.8,118.45,134.8]:add('quick-air-woosh.mp3',mapped(t),.012)
save(public/'foley.wav',foley);save(out/'premaster.wav',music+foley)
base=['ffmpeg','-hide_banner','-y','-i',str(out/'premaster.wav')]
first=subprocess.run(base+['-af','loudnorm=I=-17.2:TP=-2.5:LRA=10:print_format=json','-f','null','-'],capture_output=True,text=True,check=True)
m=json.loads(re.search(r'\{\s*"input_i"[\s\S]*?\}',first.stderr).group())
f=f"loudnorm=I=-17.2:TP=-2.5:LRA=10:measured_I={m['input_i']}:measured_TP={m['input_tp']}:measured_LRA={m['input_lra']}:measured_thresh={m['input_thresh']}:offset={m['target_offset']}:linear=true:print_format=json"
second=subprocess.run(base+['-af',f,'-ar',str(sr),'-ac','2','-c:a','pcm_s24le',str(public/'sun-in-your-eyes-master.wav')],capture_output=True,text=True,check=True)
measured=json.loads(re.search(r'\{\s*"input_i"[\s\S]*?\}',second.stderr).group())
(out/'sun-audio-receipt.json').write_text(json.dumps({'source':'Sun in Your Eyes / Alejandro Magaña (A. M.) / Mixkit','selection':'Explicit user choice; retained original reviewed arrangement and tempo','source_sha256':hashlib.sha256((root/'public/audio/sun-in-your-eyes.mp3').read_bytes()).hexdigest(),'excerpt_seconds':[0,126],'tempo':'Same user-selected arrangement as revision 4, tempo factor 0.972481; no new musical splice' ,'target_lufs':-17.2,'target_true_peak':-2.5,'normalization':measured,'events':events,'scope':'User-selected song. Final mix numerically measured; not aurally judged by the assistant.'},indent=2)+'\n')
print('Mastered 126-second rhythmic score and picture-timed foley.',flush=True)
