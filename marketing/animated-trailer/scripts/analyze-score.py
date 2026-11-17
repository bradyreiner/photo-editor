from pathlib import Path
import json,subprocess
import numpy as np
from scipy import signal
from PIL import Image, ImageDraw
root=Path(__file__).resolve().parents[1]
tracks=['a-game-michael-ramir-c','minimal-techno-01-alejandro-magana','machine-drum-vibes-alejandro-magana']
results=[]
image=Image.new('RGB',(1400,660),'#101010');draw=ImageDraw.Draw(image)
for n,name in enumerate(tracks):
 data=subprocess.check_output(['ffmpeg','-v','error','-i',str(root/'public/audio'/f'{name}.mp3'),'-f','f32le','-ac','1','-ar','11025','-'])
 y=np.frombuffer(data,dtype=np.float32);sr=11025;duration=len(y)/sr
 freq,t,s=signal.stft(y,fs=sr,nperseg=512,noverlap=384)
 power=np.abs(s);low=power[(freq>=35)&(freq<180)].sum(axis=0)
 novelty=np.maximum(0,np.diff(low,prepend=low[0]));novelty-=novelty.mean()
 ac=signal.fftconvolve(novelty,novelty[::-1],mode='full')[len(novelty)-1:]
 dt=t[1]-t[0];lo=int(60/150/dt);hi=int(60/95/dt)
 lag=lo+np.argmax(ac[lo:hi]);bpm=60/(lag*dt)
 phase_grid=np.linspace(0,60/bpm,180,endpoint=False)
 def value(ph):
  ticks=np.arange(ph,min(duration,75),60/bpm)
  return np.interp(ticks,t,novelty).mean()
 phase=float(max(phase_grid,key=value))
 energies=[]
 for start in range(0,int(duration),4):
  chunk=y[start*sr:min(len(y),(start+4)*sr)]
  energies.append({'start':start,'rms_db':round(20*np.log10(np.sqrt(np.mean(chunk**2))+1e-9),1)})
 results.append({'track':name,'duration':duration,'estimated_bpm':round(bpm,2),'estimated_first_beat':round(phase,4),'four_second_energy':energies})
 draw.text((24,n*220+15),f'{name} | {duration:.2f}s | ~{bpm:.2f} BPM | phase {phase:.3f}s',fill='#eeeeee')
 for x in range(1320):
  i=int(x/1320*len(y));j=max(i+1,int((x+1)/1320*len(y)));v=np.sqrt(np.mean(y[i:j]**2));h=min(75,float(v)*300)
  draw.line((40+x,n*220+125-h,40+x,n*220+125+h),fill=['#ff6254','#30d158','#3282ff'][n])
 for sec in range(0,int(duration),8):
  x=40+sec/duration*1320;draw.text((x,n*220+195),str(sec),fill='#999999')
(root/'out/score-analysis.json').write_text(json.dumps(results,indent=2)+'\n');image.save(root/'out/score-analysis.jpg')
print(json.dumps([{k:v for k,v in row.items() if k!='four_second_energy'} for row in results],indent=2))
