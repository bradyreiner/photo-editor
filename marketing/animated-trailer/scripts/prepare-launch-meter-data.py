"""Measured stereo peak/RMS envelopes for the illustrated mixer, not app telemetry."""
from pathlib import Path
import subprocess,json
import numpy as np
root=Path(__file__).resolve().parents[1]
files={'ambience':'/Users/bradyreiner/Downloads/IMG_1268.mov','music':str(root/'public/audio/looksy-continuous-score.wav')}
result={}
for name,file in files.items():
 raw=subprocess.check_output(['ffmpeg','-v','error','-i',file,'-t','11','-ac','2','-ar','48000','-f','f32le','-'])
 data=np.frombuffer(raw,np.float32).reshape(-1,2);values=[]
 for chunk in np.array_split(data,220):
  rms=np.sqrt(np.mean(chunk*chunk,axis=0));peak=np.max(np.abs(chunk),axis=0)
  values.append([round(float(v),6) for v in [*peak,*rms]])
 result[name]=values
(root/'src/launch/meter-data.ts').write_text('// Measured stereo [peakL, peakR, rmsL, rmsR], 20 Hz, from local source audio.\nexport const mixerEnvelope='+json.dumps(result)+';\n')
print('Prepared 48 kHz stereo peak/RMS data for two source tracks.')
