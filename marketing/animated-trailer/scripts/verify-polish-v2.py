"""Verify the delivered encoded movies, not merely the Remotion source."""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import subprocess,json,hashlib,re
from PIL import Image
root=Path(__file__).resolve().parents[1];out=root/'out/creative-polish-v2'
times=[5,10.5,20,29.5,37.4,43.5,51.5,57,65,70,77,82,89,95,102,108,110.5,114,120,127,138,146,155,163,170,179,188.5,191.3,197.5,202,208,214,219,224,232,239,247,253,260,266,277,280,285]
def one(name):
 p=out/f'{name}.mp4';probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_streams','-show_format','-of','json',str(p)],text=True));v=next(s for s in probe['streams'] if s['codec_type']=='video');a=next(s for s in probe['streams'] if s['codec_type']=='audio');expected=(1920,1080) if name=='landscape' else (1080,1920)
 assert (v['width'],v['height'])==expected,v
 assert v['avg_frame_rate']=='30/1' and int(v['nb_frames'])==8640,v
 assert abs(float(v['duration'])-288)<.001,v
 assert a['sample_rate']=='48000' and a['channels']==2,a
 assert v['color_space']=='bt709' and v['color_transfer']=='iec61966-2-1' and v['color_primaries']=='bt709',v
 decode=subprocess.run(['ffmpeg','-v','error','-i',str(p),'-f','null','-'],capture_output=True,text=True);assert decode.returncode==0 and not decode.stderr,decode.stderr
 (out/f'{name}-full-decode.log').write_text(decode.stderr or 'Complete video and audio decoded without FFmpeg errors.\n')
 frames=out/'encoded-stills'/name;frames.mkdir(parents=True,exist_ok=True)
 for t in times:
  rgb=subprocess.check_output(['ffmpeg','-v','error','-ss',str(t),'-i',str(p),'-frames:v','1','-vf','scale=iw/2:ih/2','-pix_fmt','rgb24','-f','rawvideo','-'])
  Image.frombytes('RGB',(expected[0]//2,expected[1]//2),rgb).save(frames/(str(round(t*30)).zfill(5)+'.jpg'),quality=94)
 return {'format':name,'file':str(p.relative_to(root)),'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'video':{k:v[k] for k in ['codec_name','width','height','pix_fmt','avg_frame_rate','nb_frames','duration','color_range','color_space','color_transfer','color_primaries']},'audio':{k:a[k] for k in ['codec_name','sample_rate','channels','duration']},'full_decode':'passed without video or audio errors','exported_frame_review_times':times}
with ThreadPoolExecutor(max_workers=2) as pool:movies=list(pool.map(one,['landscape','portrait']))
info={'duration':288,'frames':8640,'fps':30,'movies':movies,'scope':'Encoded-file technical verification and review frames. Aesthetic and playback observations are documented separately; no independent studio approval is claimed.'}
(out/'delivery-verification.json').write_text(json.dumps(info,indent=2)+'\n');print(json.dumps(info,indent=2))
