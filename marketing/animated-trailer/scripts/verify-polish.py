"""Verify delivery integrity for the common picture and three sound candidates."""
from pathlib import Path
import json,subprocess,hashlib,re
root=Path(__file__).resolve().parents[1]
run=lambda a:subprocess.run(a,capture_output=True,text=True,check=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
inputs=json.loads((root/'out/polish/render-inputs.json').read_text());assert all(sha(root/n)==h for n,h in inputs.items()),'Render source changed'
variants=json.loads((root/'out/polish/variants.json').read_text());stream_hashes=[];results=[]
for i,item in enumerate(variants):
 film=root/'out'/item['file'];meta=json.loads(run(['ffprobe','-v','error','-show_streams','-show_format','-of','json',str(film)]).stdout)
 v=next(s for s in meta['streams'] if s['codec_type']=='video');a=next(s for s in meta['streams'] if s['codec_type']=='audio')
 assert (v['width'],v['height'],v['r_frame_rate'],int(v['nb_frames']))==(1080,1920,'60/1',9480)
 assert [v[k] for k in ['color_space','color_transfer','color_primaries']]==['bt709']*3 and v['color_range']=='tv'
 assert abs(float(meta['format']['duration'])-158)<.03 and a['sample_rate']=='48000' and a['channels']==2
 # The first candidate decodes the shared picture and sound; the remaining
 # candidates share the identical video payload and decode their new sound.
 decode=run(['ffmpeg','-v','error','-i',str(film)]+([] if i==0 else ['-vn'])+['-f','null','-']);assert not decode.stderr.strip(),decode.stderr
 payload=run(['ffmpeg','-v','error','-i',str(film),'-map','0:v:0','-c','copy','-f','hash','-hash','sha256','-']).stdout.strip();stream_hashes.append(payload)
 levels=run(['ffmpeg','-hide_banner','-nostdin','-i',str(film),'-vn','-af','loudnorm=I=-14:TP=-1:LRA=9:print_format=json','-f','null','-'])
 measured=json.loads(re.search(r'\{\s*"input_i"[\s\S]*?\}',levels.stderr).group());assert -15<=float(measured['input_i'])<=-13 and float(measured['input_tp'])<=-1
 results.append({'file':film.name,'sha256':sha(film),'video_stream_hash':payload,'metadata':{'duration':158,'frames':9480,'width':1080,'height':1920,'fps':60,'color':'Rec.709 / limited range','audio':'AAC / 48kHz stereo'},'loudness':measured,'decode_errors':0});print(film.name,measured['input_i'],measured['input_tp'],flush=True)
assert len(set(stream_hashes))==1,'Sound variants changed the picture'
(root/'out/polish/technical-verification.json').write_text(json.dumps({'composition':'Looksy-Launch-Polish','results':results,'same_picture_all_variants':True,'source_sha256':inputs,'limits':['Technical checks do not establish professional creative approval.','Music candidates have not been aurally approved.','Licensed JPEGs illustrate RAW controls; they are not native RAW color-engine exports.']},indent=2)+'\n')
