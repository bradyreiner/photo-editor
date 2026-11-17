"""Extend the approved electronic cue by one 16-bar phrase at native tempo."""
from pathlib import Path
import subprocess,numpy as np,json,wave
root=Path(__file__).resolve().parents[1]
sr=48000
raw=subprocess.run(['ffmpeg','-v','error','-i',str(root/'public/audio/cat-walk-arulo.mp3'),'-ar',str(sr),'-ac','2','-f','f32le','-'],capture_output=True,check=True).stdout
x=np.frombuffer(raw,np.float32).reshape(-1,2)
bpm=130;phrase=round(64*60/bpm*sr)
# Choose the closest corresponding musical texture around a same-phase phrase
# splice, then use a short equal-gain crossfade rather than a hard sample jump.
window=round(.14*sr);best=None
for end in range(round(80*sr),round(96*sr),round(.01*sr)):
 a=x[end-window:end+window];b=x[end-phrase-window:end-phrase+window]
 score=float(np.mean((a-b)**2)/(np.mean(a*a)+np.mean(b*b)+1e-8))
 if best is None or score<best[0]:best=(score,end)
end=best[1];start=end-phrase;fade=round(.012*sr)
segment=x[start:end].copy();alpha=np.linspace(0,1,fade)[:,None]
junction=x[end-fade:end]*(1-alpha)+segment[:fade]*alpha
result=np.concatenate([x[:end-fade],junction,segment[fade:],x[end:]],axis=0)
target=154*sr
if len(result)<target:result=np.pad(result,((0,target-len(result)),(0,0)))
result=result[:target]
# Keep the original cue's ending, with a short shared film fade.
result[-round(1.4*sr):]*=np.linspace(1,0,round(1.4*sr))[:,None]
out=root/'public/audio/looksy-workflows-score.wav'
with wave.open(str(out),'wb') as f:f.setnchannels(2);f.setsampwidth(2);f.setframerate(sr);f.writeframes(np.clip(result*32767,-32768,32767).astype('<i2').tobytes())
(root/'out/campaign/score-arrangement.json').write_text(json.dumps({'source':'Cat Walk / Arulo / Mixkit','bpm':bpm,'inserted_bars':16,'source_phrase_seconds':[start/sr,end/sr],'crossfade_ms':12,'native_tempo':True,'native_pitch':True,'duration':154,'limits':'Beat periodicity and waveform continuity checked; subjective music approval remains a listening review.'},indent=2)+'\n')
print('Arranged',out.name,'phrase',round(start/sr,3),round(end/sr,3),'join score',round(best[0],3))
