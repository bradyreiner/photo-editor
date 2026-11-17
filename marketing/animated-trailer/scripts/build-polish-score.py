"""Keep the selected cue; extend with distinct original-speed cues and sync mix."""
from pathlib import Path
import numpy as np,subprocess,wave,json,hashlib
r=Path(__file__).resolve().parents[1];sr=48000;duration=288
def decode(p):return np.frombuffer(subprocess.run(['ffmpeg','-v','error','-i',str(p),'-ar',str(sr),'-ac','2','-f','f32le','-'],capture_output=True,check=True).stdout,np.float32).reshape(-1,2).copy()
def smooth(x,a,b):
 u=np.clip((x-a)/(b-a),0,1);return u**3*(10+u*(-15+6*u))
music=np.zeros((sr*duration,2),np.float32);receipt=[]
for name,at,start,length,fadein,fadeout in [('sun-in-your-eyes.mp3',0,0,105.672,.12,3.4),('machine-drum-vibes-alejandro-magana.mp3',104,16,108,1.67,2),('minimal-techno-01-alejandro-magana.mp3',210,None,78,2,3.5)]:
 p=r/'public/audio'/name;src=decode(p);start=(len(src)/sr-length) if start is None else start;x=src[round(start*sr):round((start+length)*sr)].copy();active=x[np.max(np.abs(x),axis=1)>.015];gain=min(.15/(np.sqrt(np.mean(active**2))+1e-9),.91/(np.max(np.abs(x))+1e-9));x*=gain
 n=min(round(fadein*sr),len(x));x[:n]*=np.linspace(0,1,n)[:,None];n=min(round(fadeout*sr),len(x));x[-n:]*=np.linspace(1,0,n)[:,None]
 a=round(at*sr);b=min(len(music),a+len(x));music[a:b]+=x[:b-a]
 receipt.append({'file':name,'timeline':at,'source_start':start,'seconds':len(x)/sr,'playback_rate':1,'repeat':False,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
source=decode(r/'public/audio/creative-polish-location.wav');source*=.075/(np.sqrt(np.mean(source**2))+1e-9);source=np.clip(source,-.55,.55)
demo_t=np.arange(6*sr)/sr;u=smooth(demo_t,.8,2)*(1-smooth(demo_t,4.5,5.7));music_before=music[216*sr:222*sr].copy()
music[216*sr:222*sr]*=(10**(-12*u/20))[:,None]
master=music.copy();master[216*sr:222*sr]+=source[:6*sr]*u[:,None]
def peaks(x):
 a=np.max(np.abs(x),axis=1);return [round(float(np.max(chunk)),5) for chunk in np.array_split(a,128)]
(r/'src/creative-launch/audio-data.ts').write_text('export const audioPeaks='+json.dumps({'music':peaks(music_before),'location':peaks(source[:6*sr])},separators=(',',':'))+';\n')
accents=[]
for name,times,peak in [('quick-air-woosh.mp3',[2.35,8.65,12,62,92,104,110,123,150,174,186,242,270,282],.042),('interface-click.mp3',[17.7,33.9,36.5,49.1,69,74.8,99,106,111,125,134.5,136.6,137.1,159.5,189.5,194,199,217,243,250,257,265,267.5],.055)]:
 x=decode(r/'public/audio/foley'/name);x*=peak/(np.max(np.abs(x))+1e-9)
 for at in times:
  a=round(at*sr);b=min(len(master),a+len(x));master[a:b]+=x[:b-a];accents.append({'file':name,'time':at,'peak':peak})
# Original synthesized brush friction follows the same stroke timing and pan.
rng=np.random.default_rng(28)
for at,length in [(117.4,1.3),(124.4,2.9)]:
 count=round(length*sr);t=np.arange(count)/sr;noise=rng.normal(0,1,count).astype(np.float32);noise=np.convolve(noise,np.ones(9)/9,mode='same');env=np.sin(np.linspace(0,np.pi,count))**.6;noise*=env*.045;pan=smooth(t,0,length) if at>120 else np.full(count,.5);x=np.stack([noise*np.sqrt(1-pan),noise*np.sqrt(pan)],axis=1);a=round(at*sr);master[a:a+count]+=x;accents.append({'file':'original synthesized brush friction','time':at,'seconds':length,'pan':'follows brush left to right' if at>120 else 'centered vertical brush'})
# Reserve headroom for inter-sample peaks and AAC delivery; this fixed gain
# preserves every relative fader move in the audible mixer demonstration.
master*=10**(-1.5/20)
output=r/'public/audio/creative-polish-v2-master.wav'
with wave.open(str(output),'wb') as f:f.setnchannels(2);f.setsampwidth(2);f.setframerate(sr);f.writeframes((np.clip(master,-.98,.98)*32767).astype('<i2').tobytes())
info={'duration':duration,'primary_user_selected':'Sun in Your Eyes','companion_cues':'Machine Drum Vibes and Minimal Techno 01 remain provisional; same artist, Alejandro Magaña (A. M.)','arrangement':'Three distinct excerpts, each once at original speed; no loops or slowing.','cues':receipt,'mixer_demonstration':{'global_range':[216,222],'source_audio':'capture/film-source/IMG_1268.mov, 2–8 seconds','music_gain_db':'-12 * smooth(s,.8,2) * (1-smooth(s,4.5,5.7))','location_gain':'smooth(s,.8,2) * (1-smooth(s,4.5,5.7))','same_clock':'src/creative-launch/State.ts mixerState','waveform_data':'src/creative-launch/audio-data.ts, actual PCM peaks'},'accents':accents,'master_headroom_gain_db':-1.5,'sample_peak':float(np.max(np.abs(master))),'license_catalogue':'https://mixkit.co/free-stock-music/techno/','license':'https://mixkit.co/license/modal/musicFree/'}
(r/'out/creative-polish-v2/music.json').write_text(json.dumps(info,indent=2)+'\n');print('Prepared 288-second score with matching audible mixer response.',flush=True)
