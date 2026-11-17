from pathlib import Path
import json,hashlib,re,subprocess
r=Path(__file__).resolve().parents[1];film=r/'out/looksy-full-storyboard.mp4';out=r/'out/rebuild/full'
def run(a):return subprocess.run(a,capture_output=True,text=True,check=True)
meta=json.loads(run(['ffprobe','-v','error','-show_streams','-show_format','-show_chapters','-of','json',str(film)]).stdout);v=next(s for s in meta['streams'] if s['codec_type']=='video');a=next(s for s in meta['streams'] if s['codec_type']=='audio')
assert (v['width'],v['height'],v['r_frame_rate'],int(v['nb_frames']))==(720,1280,'30/1',6038)
assert [v[k] for k in ['color_space','color_transfer','color_primaries']]==['bt709']*3
assert abs(float(meta['format']['duration'])-201.25)<.04 and len(meta['chapters'])==17
assert a['sample_rate']=='48000' and a['channels']==2
m=json.loads((out/'render-inputs.json').read_text());assert all(hashlib.sha256((r/p).read_bytes()).hexdigest()==h for p,h in m.items())
x=run(['ffmpeg','-v','error','-i',str(film),'-f','null','-']);assert not x.stderr.strip(),x.stderr
x=run(['ffmpeg','-hide_banner','-i',str(film),'-vn','-af','loudnorm=I=-16.5:TP=-2:LRA=11:print_format=json','-f','null','-']);levels=json.loads(re.search(r'\{\s*"input_i"[\s\S]*?\}',x.stderr).group());assert -17.5<float(levels['input_i'])<-15.5;assert float(levels['input_tp'])<=-1.8
x=run(['ffmpeg','-hide_banner','-i',str(film),'-an','-vf','blackdetect=d=0.35:pix_th=0.05:pic_th=0.98','-f','null','-']);gaps=[line for line in x.stderr.splitlines() if 'black_start:' in line];assert not gaps,gaps
receipt={'file':str(film.relative_to(r)),'sha256':hashlib.sha256(film.read_bytes()).hexdigest(),'duration_seconds':float(meta['format']['duration']),'planned_seconds':201.25,'dimensions':[720,1280],'fps':30,'frames':6038,'embedded_chapters':17,'decode_errors':0,'blank_intervals_over_350ms':gaps,'source_code_fonts_and_audio_match':True,'levels':levels,'scope':'Full-length storyboard animatic for timing, story and handoff review; final production polish, aural judgment and audience engagement are not established by these checks.'};(out/'technical-verification.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps(receipt,indent=2),flush=True)
