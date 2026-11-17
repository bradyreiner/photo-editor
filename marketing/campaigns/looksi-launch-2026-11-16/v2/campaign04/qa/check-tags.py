from pathlib import Path
import importlib.util,json,subprocess
ROOT=Path(__file__).resolve().parents[1]
s=importlib.util.spec_from_file_location('campaign04',ROOT/'source/make_campaign.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
results={}
for name,filter in [('current',m.VIDEO_COLOR_FILTER),('setparams',m.VIDEO_COLOR_FILTER+',setparams=range=limited:color_primaries=bt709:color_trc=bt709:colorspace=bt709')]:
 target=ROOT/f'qa/tag-{name}.mp4'
 cmd=['ffmpeg','-hide_banner','-loglevel','error','-y','-f','rawvideo','-pix_fmt','rgb24','-s','32x32','-r','24','-i','pipe:0','-frames:v','1','-vf',filter,'-c:v','libx264','-pix_fmt','yuv420p',*m.VIDEO_COLOR_ARGS,str(target)]
 subprocess.run(cmd,input=bytes([255,59,48])*(32*32),check=True)
 p=subprocess.run(['ffprobe','-v','error','-show_streams','-of','json',str(target)],capture_output=True,text=True,check=True)
 v=json.loads(p.stdout)['streams'][0]
 results[name]={k:v.get(k) for k in ['color_range','color_space','color_transfer','color_primaries','pix_fmt']}
(ROOT/'qa/color-tag-comparison.json').write_text(json.dumps(results,indent=2))
print(json.dumps(results,indent=2))
