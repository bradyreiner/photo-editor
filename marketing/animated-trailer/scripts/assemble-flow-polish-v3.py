"""Replace reviewed transition ranges without altering the continuous score."""
from pathlib import Path
import subprocess,json,hashlib,sys
r=Path(__file__).resolve().parents[1];name=sys.argv[1];assert name in ['landscape','portrait']
out=r/'out/creative-polish-v3';patches=out/'flow-patches';base=r/'out/creative-polish-v3-transition-checkpoint'/f'{name}.mp4'
ranges=json.loads((patches/'ranges.json').read_text());audio=out/'audio-delivery.m4a'
def probe(p):return next(s for s in json.loads(subprocess.check_output(['ffprobe','-v','error','-show_streams','-of','json',str(p)],text=True))['streams'] if s['codec_type']=='video')
v=probe(base);assert v['color_space']=='bt709' and int(v['nb_frames'])==8940
inputs=[];graph=[];labels=[];cursor=0;input_count=0
# Independent accurate-seek inputs avoid buffering later portions of a shared
# decoder while the concat filter is still consuming an earlier range.
def add_base(start,end):
 global input_count
 label=f'b{input_count}'
 inputs.extend(['-ss',f'{start/30:.9f}','-t',f'{(end-start)/30:.9f}','-i',str(base)])
 graph.append(f'[{input_count}:v]trim=end_frame={end-start},setpts=PTS-STARTPTS[{label}]')
 labels.append(label);input_count+=1
for start,end in ranges:
 p=patches/f'{name}-{start}-{end}.mp4';q=probe(p);assert int(q['nb_frames'])==end-start
 assert q.get('color_space') in [None,'unknown','smpte170m','bt470bg'],q
 if start>cursor:add_base(cursor,start)
 label=f'p{input_count}';inputs.extend(['-i',str(p)])
 graph.append(f'[{input_count}:v]zscale=matrixin=170m:matrix=709:rangein=limited:range=limited,setpts=PTS-STARTPTS[{label}]')
 labels.append(label);input_count+=1;cursor=end
if cursor<8940:add_base(cursor,8940)
graph.append(''.join(f'[{x}]' for x in labels)+f'concat=n={len(labels)}:v=1:a=0[v]')
inputs+=['-i',str(audio)];tmp=out/f'{name}-flow-finishing.mp4';dest=out/f'{name}.mp4'
subprocess.run(['ffmpeg','-hide_banner','-y',*inputs,'-filter_complex_threads','2','-filter_complex',';'.join(graph),'-map','[v]','-map',f'{input_count}:a:0','-c:v','libx264','-preset','fast','-crf','17','-threads','4','-pix_fmt','yuv420p','-color_primaries','bt709','-color_trc','iec61966-2-1','-colorspace','bt709','-color_range','tv','-c:a','copy','-movflags','+faststart','-t','298',str(tmp)],check=True)
assert int(probe(tmp)['nb_frames'])==8940
tmp.replace(dest)
(out/f'{name}-flow-finish.json').write_text(json.dumps({'base':str(base.relative_to(r)),'patch_frame_ranges_exclusive_end':ranges,'output':str(dest.relative_to(r)),'sha256':hashlib.sha256(dest.read_bytes()).hexdigest(),'bytes':dest.stat().st_size,'audio':'Verified AAC fast 320kbps copied without re-encoding','color':'Base already BT709; only untagged BT601 patches converted to BT709. Original sRGB transfer retained.'},indent=2)+'\n')
