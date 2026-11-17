"""Prepare longer local footage and real waveform data for the animated film."""
from pathlib import Path
import subprocess,json,hashlib
import numpy as np
root=Path(__file__).resolve().parents[1];records=[];waves={}
for number in ['1207','1268']:
 source=Path('/Users/bradyreiner/Downloads')/f'IMG_{number}.mov'
 target=root/'public/media'/f'launch-{number}.mp4'
 subprocess.run(['ffmpeg','-v','error','-nostdin','-ss','1','-i',str(source),'-t','11','-map','0:v:0','-an','-vf','scale=1920:-2:flags=lanczos,fps=30','-c:v','libx264','-crf','16','-preset','fast','-pix_fmt','yuv420p','-color_primaries','bt709','-color_trc','bt709','-colorspace','bt709','-movflags','+faststart','-y',str(target)],check=True)
 raw=subprocess.check_output(['ffmpeg','-v','error','-ss','1','-i',str(source),'-t','11','-map','0:a:0','-ac','1','-ar','4000','-f','f32le','-'])
 audio=np.frombuffer(raw,dtype=np.float32);chunks=np.array_split(audio,384);rms=np.array([np.sqrt(np.mean(x*x)) for x in chunks]);rms/=max(float(np.quantile(rms,.98)),1e-6)
 waves[number]=[round(min(1,float(x)),4) for x in rms]
 sha=hashlib.sha256()
 with source.open('rb') as stream:
  for chunk in iter(lambda:stream.read(1024*1024),b''):sha.update(chunk)
 metadata=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_entries','format=duration:stream=codec_type,width,height,r_frame_rate,color_space,color_transfer,color_primaries','-of','json',str(source)]))
 records.append({'source':str(source),'source_sha256':sha.hexdigest(),'preview':str(target.relative_to(root)),'preview_sha256':hashlib.sha256(target.read_bytes()).hexdigest(),'trim_seconds':[1,12],'preview_dimensions':[1920,1080],'source_metadata':metadata,'provenance':'Local preview from original 4K Rec.709 source; original unchanged. Not a Looksy export. Source audio used for waveform only.'})
 print(f'Prepared {target.name}',flush=True)
(root/'src/launch/waveforms.ts').write_text('// RMS envelopes from the original source audio; 384 samples per 11-second excerpt.\nexport const sourceWaveforms='+json.dumps(waves)+';\n')
(root/'launch-media-manifest.json').write_text(json.dumps(records,indent=2)+'\n')
