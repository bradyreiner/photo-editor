"""Verify the rendered file and frozen inputs; not a creative-quality score."""
from pathlib import Path
import hashlib,json,re,subprocess
root=Path(__file__).resolve().parents[1]
run=lambda args:subprocess.run(args,capture_output=True,text=True,check=True)
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
inputs=json.loads((root/'out/studio/render-inputs.json').read_text())
changed=[name for name,digest in inputs.items() if sha(root/name)!=digest]
assert not changed, f'Inputs changed during render: {changed}'
film=root/'out/looksy-studio-film.mp4'
meta=json.loads(run(['ffprobe','-v','error','-show_streams','-show_format','-show_chapters','-of','json',str(film)]).stdout)
assert len(meta.get('chapters',[]))==9
v=next(s for s in meta['streams'] if s['codec_type']=='video');a=next(s for s in meta['streams'] if s['codec_type']=='audio')
assert (v['width'],v['height'],v['r_frame_rate'],int(v['nb_frames']))==(1080,1920,'60/1',11520),v
assert [v[k] for k in ['color_space','color_transfer','color_primaries']]==['bt709']*3
assert abs(float(meta['format']['duration'])-192)<.04
assert a['sample_rate']=='48000' and a['channels']==2
r=run(['ffmpeg','-v','error','-i',str(film),'-f','null','-']);assert not r.stderr.strip(),r.stderr
r=run(['ffmpeg','-hide_banner','-i',str(film),'-vn','-af','loudnorm=I=-16.5:TP=-2:LRA=11:print_format=json','-f','null','-'])
levels=json.loads(re.search(r'\{\s*"input_i"[\s\S]*?\}',r.stderr).group())
assert -17.5<=float(levels['input_i'])<=-15.5,levels
assert float(levels['input_tp'])<=-1.8,levels
r=run(['ffmpeg','-hide_banner','-i',str(film),'-an','-vf','blackdetect=d=0.35:pix_th=0.05:pic_th=0.98','-f','null','-'])
gaps=[line for line in r.stderr.splitlines() if 'black_start:' in line];assert not gaps,gaps
receipt={'file':str(film.relative_to(root)),'sha256':sha(film),'duration_seconds':192,'dimensions':[1080,1920],'fps':60,'frames':11520,'color':'Rec.709','audio':'48kHz stereo AAC','levels':levels,'decode_errors':0,'blank_intervals_over_350ms':gaps,'render_input_count':len(inputs),'render_inputs_match':True,'embedded_chapters':9,'scope':['Technical integrity is not creative approval.','Fully animated interfaces, not a native Qt app recording or performance qualification.','Actual CR2 decoding and native CPU color-engine plates, native local CPU inpainting, and native Metal Gaussian renders.','Video look and monochrome images use native-engine LUTs; no claim of native timeline export parity.','Optional local search and agent proposals are animated demonstrations, not live agent execution.','Whole new original score Origami by Scott Buckley; no loop or tempo changes. Required posting credits supplied.']}
(root/'out/studio/technical-verification.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps(receipt,indent=2),flush=True)
