from pathlib import Path
import subprocess,json,re,wave,hashlib
import numpy as np
from scipy.signal import butter,sosfilt,find_peaks
root=Path(__file__).resolve().parents[1];out=root/'out/studio';public=root/'public/audio/studio';sr=48000;duration=192;offset=2.
def decode(p,extra=[]):return np.frombuffer(subprocess.check_output(['ffmpeg','-v','error',*extra,'-i',str(p),'-ar',str(sr),'-ac','2','-f','f32le','-']),np.float32).reshape(-1,2).copy()
def save(p,a):
 with wave.open(str(p),'wb') as w:w.setnchannels(2);w.setsampwidth(2);w.setframerate(sr);w.writeframes((np.clip(a,-1,1)*32767).astype('<i2').tobytes())
def smooth(t,a,b):
 u=np.clip((t-a)/(b-a),0,1);return u*u*u*(10+u*(-15+6*u))
song=decode(public/'origami.mp3');assert len(song)/sr>180
music=np.zeros((duration*sr,2),np.float32);at=round(offset*sr);music[at:at+len(song)]=song*.32
clock=np.arange(len(music))/sr
# The soundtrack gain follows the score-fader demonstration at 2:10.
duck=smooth(clock,131,133)*(1-smooth(clock,135,136));music*=np.power(10,-6*duck[:,None]/20)
location=np.zeros_like(music)
night=decode(Path('/Users/bradyreiner/Downloads/IMG_1113.mov'),['-ss','2','-t','5'])
night*=.045/max(.001,float(np.abs(night).max()));start=130*sr;n=min(5*sr,len(night));location[start:start+n]=night[:n]*np.minimum(1,np.arange(n)/sr/.3)[:,None]*np.minimum(1,(n-np.arange(n))/sr/.5)[:,None]
fx=np.zeros_like(music);events=[]
def add(name,t,peak):
 a=decode(root/'public/audio/foley'/name);a*=peak/max(.001,float(np.abs(a).max()));i=round(t*sr);n=min(len(a),len(fx)-i);fx[i:i+n]+=a[:n];events.append({'sample':name,'time':t,'peak':peak})
for t in [2,10,14.9,19.3,22.5,26.6,31.2,36.5,37.7,40.6,44.5,48.2,53.9,62.4,65.7,68.1,71.1,73.3,76.1,80.5,85,87,88.1,90.2,94,98,104,114.5,116,119.1,122.4,124,125.5,131,144,147.3,152.5,158.7,164.6,169.5,171.6,173.4,175,178.2,181.1,184.3]:add('interface-click.mp3',t,.014)
for t in [1.1,12.1,60.8,77.5,92.2,101.2,112.1,136,140.1,155.2,165.1,186.3]:add('quick-air-woosh.mp3',t,.009)
rng=np.random.default_rng(42)
for a,b in [(53.8,56),(74.3,76.1)]:
 n=round((b-a)*sr);z=np.linspace(0,1,n);noise=sosfilt(butter(2,[600,4500],fs=sr,btype='bandpass',output='sos'),rng.normal(0,1,n));noise=noise/max(.001,np.max(np.abs(noise)))*.009*np.sin(np.pi*z)**.8;fx[round(a*sr):round(a*sr)+n]+=noise[:,None];events.append({'sample':'unpitched brush texture','time':a,'duration':b-a})
# Actual waveform and RMS measurements feed the illustrated timeline/mixer.
def waveform(a,bins=180):return [round(float(np.max(np.abs(x))),4) for x in np.array_split(a,bins)]
meters=[]
for frame in range(150):
 i=130*sr+frame*1600
 row={}
 for name,stem in [('score',music),('location',location),('effects',fx),('master',music+location+fx)]:
  rms=np.sqrt(np.mean(stem[i:i+1600]**2,axis=0));row[name]=[round(float(np.clip((20*np.log10(max(1e-7,v))+60)/60,0,1)),4) for v in rms]
 meters.append(row)
data={'meters':meters,'scoreWave':waveform(music[104*sr:135*sr].mean(axis=1)),'locationWave':waveform(night.mean(axis=1))}
(root/'src/studio-film/audio-data.ts').write_text('export const audioData='+json.dumps(data,separators=(',',':'))+';\n')
# Music transients choose the teaser's picture cuts, never repeat the song.
mono=song.mean(axis=1);energy=np.sqrt(np.mean(mono[:len(mono)//480*480].reshape(-1,480)**2,axis=1));onset=np.maximum(0,np.diff(np.log(energy+1e-5),prepend=0));peaks,_=find_peaks(onset,distance=18,prominence=.08)
cut=[]
for desired in [106,108,110]:
 choices=peaks[np.abs(peaks/100+offset-desired)<.30]
 pick=desired if not len(choices) else float(choices[np.argmax(onset[choices])]/100+offset)
 cut.append(round((pick-104)*60)/60)
(root/'src/studio-film/edit-data.ts').write_text('export const teaserCuts='+json.dumps(cut)+';\n')
save(out/'premaster.wav',music+location+fx);save(public/'foley.wav',fx+location)
base=['ffmpeg','-hide_banner','-y','-i',str(out/'premaster.wav')]
r=subprocess.run(base+['-af','loudnorm=I=-16.5:TP=-2:LRA=11:print_format=json','-f','null','-'],capture_output=True,text=True,check=True);m=json.loads(re.search(r'\{\s*"input_i"[\s\S]*?\}',r.stderr).group());f=f"loudnorm=I=-16.5:TP=-2:LRA=11:measured_I={m['input_i']}:measured_TP={m['input_tp']}:measured_LRA={m['input_lra']}:measured_thresh={m['input_thresh']}:offset={m['target_offset']}:linear=true:print_format=json"
r=subprocess.run(base+['-af',f,'-ar',str(sr),'-c:a','pcm_s24le',str(public/'master.wav')],capture_output=True,text=True,check=True)
receipt={'song':'Origami / Scott Buckley','source':'https://www.scottbuckley.com.au/library/origami/','source_sha256':hashlib.sha256((public/'origami.mp3').read_bytes()).hexdigest(),'license':'CC BY 4.0, commercial synchronization allowed with required credit in post/video description','song_original_seconds':len(song)/sr,'music_start_seconds':offset,'looping':False,'tempo_change':False,'arrangement_change':False,'picture_cuts_relative_to_film_chapter':cut,'audio_fader':'Original score ducked by 6 dB from 2:11 to 2:13 and restored after the audio UI; native-source night ambience at 2:10–2:15.','target_lufs':-16.5,'events':events,'scope':'Measured levels and source timing, not aural creative approval.'}
(out/'audio-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');(out/'posting-credit.txt').write_text("Music: 'Origami' by Scott Buckley — released under CC-BY 4.0. www.scottbuckley.com.au\nPhotographic Gaussian scan 'Houseplant' by Marcel Padilla — CC BY 4.0. https://marcelpadilla.com/Gaussian_Splat_Object_Dataset/\nTravel photographs and footage: Brady Reiner.\n")
print(json.dumps({'song_seconds':len(song)/sr,'cut_seconds':cut,'master':str(public/'master.wav')},indent=2),flush=True)
