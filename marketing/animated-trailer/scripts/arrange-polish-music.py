"""Prepare three licensed cue candidates for the same 158-second picture."""
from pathlib import Path
import subprocess,numpy as np,wave,json,hashlib
root=Path(__file__).resolve().parents[1];sr=48000;duration=158
receipts=[]
for slug in ['no-warning','coffee-or-tea','sun-in-your-eyes']:
 source=root/f'public/audio/{slug}.mp3'
 raw=subprocess.run(['ffmpeg','-v','error','-i',str(source),'-ar',str(sr),'-ac','2','-f','f32le','-'],capture_output=True,check=True).stdout
 x=np.frombuffer(raw,np.float32).reshape(-1,2)
 mono=x.mean(axis=1)[::6];n=256;h=80
 spec=np.abs(np.fft.rfft(np.lib.stride_tricks.sliding_window_view(mono,n)[::h]*np.hanning(n)));hz=np.fft.rfftfreq(n,1/8000)
 energy=np.sum(spec[:,(hz>=35)&(hz<=180)]**2,axis=1);onset=np.maximum(0,np.diff(energy));onset=(onset-onset.mean())/(onset.std()+1e-9);a=np.arange(len(onset))
 best=None
 for bpm in np.arange(100,176,.25):
  lag=60/bpm/.01;idx=np.arange(len(onset)-int(4*lag)-2)
  score=sum(np.mean(onset[idx]*np.interp(idx+k*lag,a,onset)) for k in [1,2,4])
  if best is None or score>best[0]:best=(score,float(bpm))
 bpm=best[1];bars=max(4,round((duration-len(x)/sr)/(4*60/bpm)/4)*4);phrase=round(bars*4*60/bpm*sr)
 # Repeat a complete four-bar multiple once; pick the closest same-phase join.
 length=min(phrase,len(x)//2);join=None;win=round(.1*sr)
 for end in range(max(length+win,round(.55*len(x))),len(x)-round(12*sr),round(.03*sr)):
  u=x[end-win:end+win];v=x[end-length-win:end-length+win];score=float(np.mean((u-v)**2)/(np.mean(u*u)+np.mean(v*v)+1e-8))
  if join is None or score<join[0]:join=(score,end)
 if join is None:raise RuntimeError('No safe musical splice for '+slug)
 end=join[1];start=end-length;fade=round(.02*sr);alpha=np.linspace(0,1,fade)[:,None]
 y=np.concatenate([x[:end-fade],x[end-fade:end]*(1-alpha)+x[start:start+fade]*alpha,x[start+fade:end],x[end:]],axis=0)
 tempo=len(y)/sr/duration
 temp=root/f'out/polish/audio/{slug}-extended.wav'
 with wave.open(str(temp),'wb') as f:f.setnchannels(2);f.setsampwidth(2);f.setframerate(sr);f.writeframes(np.clip(y*32767,-32768,32767).astype('<i2').tobytes())
 raw=subprocess.run(['ffmpeg','-v','error','-i',str(temp),'-af',f'atempo={tempo},apad,atrim=duration={duration}', '-ar',str(sr),'-ac','2','-f','f32le','-'],capture_output=True,check=True).stdout
 y=np.frombuffer(raw,np.float32).reshape(-1,2).copy()
 times=[0,.12,3,12,28,34,42,56,68,82,86,100,114,120,126,138,148,152,156,158]
 gain=[.5,.74,.78,.68,.74,.62,.78,.8,.82,.65,.82,.76,.78,.82,.57,.62,.72,.85,.7,0]
 y*=np.interp(np.arange(len(y))/sr,times,gain)[:,None]
 out=root/f'public/audio/looksy-polish-{slug}.wav'
 with wave.open(str(out),'wb') as f:f.setnchannels(2);f.setsampwidth(2);f.setframerate(sr);f.writeframes(np.clip(y*32767,-32768,32767).astype('<i2').tobytes())
 r={'track':slug,'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'duration':duration,'estimated_bpm':bpm,'inserted_bars':bars,'phrase_seconds':[start/sr,end/sr],'tempo_factor':tempo,'pitch':'Preserved with atempo','scope':'Catalogue and waveform selection; subjective listening approval pending.'};receipts.append(r);print(r,flush=True)
(root/'out/polish/music-arrangements.json').write_text(json.dumps(receipts,indent=2)+'\n')
