from pathlib import Path
import subprocess,re,json,hashlib
r=Path(__file__).resolve().parents[1];src=r/'out/looksy-motion-rebuild-unmastered.mp4';dst=r/'out/looksy-motion-rebuild.mp4'
def run(args):return subprocess.run(args,capture_output=True,text=True,check=True)
base=['ffmpeg','-hide_banner','-y','-i',str(src)]
a=run(base+['-vn','-af','loudnorm=I=-16.5:TP=-2:LRA=11:print_format=json','-f','null','-']);m=json.loads(re.search(r'\{\s*"input_i"[\s\S]*?\}',a.stderr).group());assert float(m['input_i'])>-40
f=f"loudnorm=I=-16.5:TP=-2:LRA=11:measured_I={m['input_i']}:measured_TP={m['input_tp']}:measured_LRA={m['input_lra']}:measured_thresh={m['input_thresh']}:offset={m['target_offset']}:linear=true"
run(base+['-map','0:v:0','-map','0:a:0','-c:v','copy','-af',f,'-ar','48000','-c:a','aac','-b:a','320k','-movflags','+faststart',str(dst)])
meta=json.loads(run(['ffprobe','-v','error','-show_streams','-show_format','-of','json',str(dst)]).stdout);v=next(s for s in meta['streams'] if s['codec_type']=='video');a=next(s for s in meta['streams'] if s['codec_type']=='audio')
assert (v['width'],v['height'],v['r_frame_rate'],int(v['nb_frames']))==(1080,1920,'60/1',2520)
assert [v[k] for k in ['color_space','color_transfer','color_primaries']]==['bt709']*3
assert abs(float(meta['format']['duration'])-42)<.04 and a['sample_rate']=='48000' and a['channels']==2
inputs=json.loads((r/'out/rebuild/render-inputs.json').read_text());assert all(hashlib.sha256((r/p).read_bytes()).hexdigest()==h for p,h in inputs.items())
a=run(['ffmpeg','-v','error','-i',str(dst),'-f','null','-']);assert not a.stderr.strip(),a.stderr
levels=run(['ffmpeg','-hide_banner','-i',str(dst),'-vn','-af','loudnorm=I=-16.5:TP=-2:LRA=11:print_format=json','-f','null','-']);levels=json.loads(re.search(r'\{\s*"input_i"[\s\S]*?\}',levels.stderr).group());assert -17.3<float(levels['input_i'])<-15.7;assert float(levels['input_tp'])<=-1.8
black=run(['ffmpeg','-hide_banner','-i',str(dst),'-an','-vf','blackdetect=d=0.35:pix_th=0.05:pic_th=0.98','-f','null','-']);gaps=[x for x in black.stderr.splitlines() if 'black_start:' in x];assert not gaps,gaps
receipt={'file':str(dst.relative_to(r)),'seconds':42,'size':[1080,1920],'fps':60,'frames':2520,'decode_errors':0,'source_code_and_fonts_match':True,'levels':levels,'blank_intervals_over_350ms':gaps,'sha256':hashlib.sha256(dst.read_bytes()).hexdigest(),'scope':'42-second creative-direction prototype, not the full rebuilt trailer; no aural creative approval claimed.'};(r/'out/rebuild/technical-verification.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps(receipt,indent=2),flush=True)
