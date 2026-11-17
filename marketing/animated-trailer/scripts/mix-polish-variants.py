"""Master three licensed soundtrack candidates against one reviewed picture."""
from pathlib import Path
import subprocess,json,hashlib,re,sys,os
root=Path(__file__).resolve().parents[1]
picture=root/'out/polish/picture.mp4';out=root/'out';receipts=[]
for slug in (sys.argv[1:] or ['no-warning','coffee-or-tea','sun-in-your-eyes']):
 film=out/f'looksy-launch-polish-{slug}.mp4'
 temporary=film.with_name(film.stem+'-working.mp4')
 ceiling=-2.2 if slug=='no-warning' else -1.8
 base=['ffmpeg','-hide_banner','-nostdin','-i',str(picture),'-i',str(root/f'public/audio/looksy-polish-{slug}.wav'),'-i',str(root/'public/audio/launch-mixer-ambience.wav')]
 graph='[2:a]adelay=82800:all=1[ambience];[0:a][1:a][ambience]amix=inputs=3:duration=first:normalize=0,'
 analysis=subprocess.run(base+['-filter_complex',graph+f'loudnorm=I=-14:TP={ceiling}:LRA=9:print_format=json[a]','-map','[a]','-vn','-f','null','-'],capture_output=True,text=True,check=True)
 levels=json.loads(re.search(r'\{\s*"input_i"[\s\S]*?\}',analysis.stderr).group())
 normalizer=f'loudnorm=I=-14:TP={ceiling}:LRA=9:linear=true:print_format=json:'+':'.join(['measured_I='+levels['input_i'],'measured_TP='+levels['input_tp'],'measured_LRA='+levels['input_lra'],'measured_thresh='+levels['input_thresh'],'offset='+levels['target_offset']])
 result=subprocess.run(base+['-filter_complex',graph+normalizer+'[a]','-map','0:v:0','-map','[a]','-c:v','copy','-c:a','aac','-b:a','320k','-ar','48000','-t','158','-movflags','+faststart','-y',str(temporary)],capture_output=True,text=True,check=True)
 os.replace(temporary,film)
 r={'track':slug,'file':film.name,'sha256':hashlib.sha256(film.read_bytes()).hexdigest(),'analysis':levels,'target':{'lufs':-14,'true_peak':ceiling},'picture':'Same reviewed H.264 stream copied unchanged for all three candidates.','audio':'Original arranged WAV plus pictured foley and source ambience, one final AAC encode. Subjective approval pending.'};receipts.append(r)
 (root/f'out/polish/audio/{slug}-master.json').write_text(json.dumps(r,indent=2)+'\n');print(slug,'mastered',flush=True)
previous=root/'out/polish/variants.json'
old=json.loads(previous.read_text()) if previous.exists() else []
updated={r['track']:r for r in old};updated.update({r['track']:r for r in receipts})
previous.write_text(json.dumps([updated[k] for k in ['no-warning','coffee-or-tea','sun-in-your-eyes']],indent=2)+'\n')
