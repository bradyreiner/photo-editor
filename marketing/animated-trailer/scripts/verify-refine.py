"""Check the exported revision; creative approval remains a separate review."""
from pathlib import Path
import hashlib,json,re,subprocess

root=Path(__file__).resolve().parents[1]
run=lambda args:subprocess.run(args,capture_output=True,text=True,check=True)
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
inputs=json.loads((root/'out/refine/render-inputs.json').read_text())
assert all(sha(root/name)==digest for name,digest in inputs.items()),'Source changed during render'
film=root/'out/looksy-launch-v5.mp4'
meta=json.loads(run(['ffprobe','-v','error','-show_streams','-show_format','-of','json',str(film)]).stdout)
v=next(s for s in meta['streams'] if s['codec_type']=='video')
a=next(s for s in meta['streams'] if s['codec_type']=='audio')
assert (v['width'],v['height'],v['r_frame_rate'],int(v['nb_frames']))==(1080,1920,'60/1',8520)
assert [v[k] for k in ['color_space','color_transfer','color_primaries']]==['bt709']*3
assert abs(float(meta['format']['duration'])-142)<.04
assert a['sample_rate']=='48000' and a['channels']==2
decoded=run(['ffmpeg','-v','error','-i',str(film),'-f','null','-'])
assert not decoded.stderr.strip(),decoded.stderr
audio=run(['ffmpeg','-hide_banner','-i',str(film),'-vn','-af','loudnorm=I=-20:TP=-4:LRA=8:print_format=json','-f','null','-'])
levels=json.loads(re.search(r'\{\s*"input_i"[\s\S]*?\}',audio.stderr).group())
assert -24<=float(levels['input_i'])<=-18.5,levels
assert float(levels['input_tp'])<=-2,levels
black=run(['ffmpeg','-hide_banner','-i',str(film),'-an','-vf','blackdetect=d=0.35:pix_th=0.05:pic_th=0.98','-f','null','-'])
gaps=[line for line in black.stderr.splitlines() if 'black_start:' in line]
assert not gaps,gaps
receipt={'file':str(film.relative_to(root)),'sha256':sha(film),'duration_seconds':142,'dimensions':[1080,1920],'fps':60,'frames':8520,'color':'Rec.709','audio':'48kHz stereo AAC','levels':levels,'decode_errors':0,'blank_intervals_over_350ms':gaps,'render_source_hashes':inputs,'scope':['Technical integrity is not professional creative approval.','Track was selected from catalogue and measured, not aurally judged by the assistant.','This is a fully animated product film. RAW controls and retouching are illustrations, not native app performance proof.']}
(root/'out/refine/technical-verification.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps({k:receipt[k] for k in ['file','sha256','duration_seconds','frames','levels']},indent=2),flush=True)
