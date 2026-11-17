"""Master the complete review soundtrack while copying the rendered video."""
from pathlib import Path
import argparse, json, re, subprocess, os, shutil
root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('source', nargs='?', default='out/looksy-complete-trailer.mp4')
parser.add_argument('--receipt', help='Receipt path relative to project root')
parser.add_argument('--mixer-ambience', help='Prepared original ambience to place at 62.8 seconds')
parser.add_argument('--mixer-start', type=float, default=62.8, help='Start time of the pictured mixer ambience')
parser.add_argument('--true-peak', type=float, default=-1, help='Pre-encode true-peak ceiling in dBTP')
args=parser.parse_args()
source=(root/args.source).resolve()
master=source.with_name(source.stem+'-master.mp4')
video_duration=subprocess.check_output(['ffprobe','-v','error','-select_streams','v:0','-show_entries','stream=duration','-of','default=nw=1:nk=1',str(source)],text=True).strip()
base=['ffmpeg','-hide_banner','-nostdin','-i',str(source)]
if args.mixer_ambience:
 base += ['-i',str((root/args.mixer_ambience).resolve())]
def audio_filter(normalizer):
 if args.mixer_ambience:
  graph=f'[1:a]adelay={round(args.mixer_start*1000)}:all=1[location];[0:a][location]amix=inputs=2:duration=first:normalize=0,'+normalizer+'[mastered]'
  return ['-filter_complex',graph,'-map','[mastered]']
 return ['-map','0:a:0','-af',normalizer]
analysis=subprocess.run(base+audio_filter(f'loudnorm=I=-14:TP={args.true_peak}:LRA=9:print_format=json')+['-vn','-f','null','-'],capture_output=True,text=True,check=True)
match=re.search(r'\{\s*"input_i"[\s\S]*?\}',analysis.stderr)
if not match:raise RuntimeError('No loudness analysis received; preserving source render')
levels=json.loads(match.group())
normalizer=f'loudnorm=I=-14:TP={args.true_peak}:LRA=9:linear=true:print_format=json:'+':'.join([
 'measured_I='+levels['input_i'],'measured_TP='+levels['input_tp'],
 'measured_LRA='+levels['input_lra'],'measured_thresh='+levels['input_thresh'],
 'offset='+levels['target_offset']])
result=subprocess.run(base+['-map','0:v:0']+audio_filter(normalizer)+['-t',video_duration,'-c:v','copy','-c:a','aac','-b:a','320k','-ar','48000','-movflags','+faststart','-y',str(master)],capture_output=True,text=True,check=True)
output=re.search(r'\{\s*"input_i"[\s\S]*?\}',result.stderr)
receipt={'source':source.name,'target':{'integrated_lufs':-14,'true_peak_dbtp':args.true_peak,'lra_lu':9},'analysis':levels,'mastering':json.loads(output.group()) if output else None,'video':'Copied without re-encoding','audio':'AAC 320k, 48kHz stereo','mux':'faststart'}
if args.mixer_ambience: receipt['mixer_ambience']={'source':args.mixer_ambience,'start_seconds':args.mixer_start,'automation':'Prepared to match pictured fader'}
receipt_path=(root/args.receipt) if args.receipt else source.parent/('polished-audio-mastering.json' if source.stem=='looksy-polished-trailer' else 'audio-mastering.json')
receipt_path.write_text(json.dumps(receipt,indent=2)+'\n')
shutil.copy2(source,source.with_name(source.stem+'-pre-master.mp4'))
os.replace(master,source)
print('Mastered complete soundtrack; rendered video copied unchanged. Original render retained.')
