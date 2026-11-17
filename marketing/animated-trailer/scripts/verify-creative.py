from pathlib import Path
import json,subprocess
root=Path(__file__).resolve().parents[1]
folder=root/'out/creative-launch'
times=[5,10.7,19.2,29.2,31,41.6,43.5,56,60.5,64,69,74.3,79.4,85,92,100,107,114,121,132,140,145,152,154.5,157,163,171,178,189,202,207,213]
receipt=[]
for fmt in ['landscape','portrait']:
    video=folder/f'{fmt}.mp4'
    data=json.loads(subprocess.run(['ffprobe','-v','error','-show_format','-show_streams','-of','json',str(video)],capture_output=True,text=True,check=True).stdout)
    v=next(s for s in data['streams'] if s['codec_type']=='video');a=next(s for s in data['streams'] if s['codec_type']=='audio')
    assert (v['width'],v['height'])==((1920,1080) if fmt=='landscape' else (1080,1920))
    assert int(v['nb_frames'])==6480
    assert abs(float(v['duration'])-216)<.02
    assert abs(float(a['duration'])-216)<.05
    assert v['avg_frame_rate']=='30/1'
    assert v['color_space']=='bt709'
    assert v['color_primaries']=='bt709'
    assert v['color_transfer']=='iec61966-2-1'
    decode=subprocess.run(['ffmpeg','-v','error','-i',str(video),'-f','null','-'],capture_output=True,text=True,check=True)
    assert not decode.stderr.strip(),decode.stderr
    frames=folder/('frames' if fmt=='landscape' else 'portrait-frames');frames.mkdir(exist_ok=True)
    for t in times:
        subprocess.run(['ffmpeg','-v','error','-y','-ss',str(t),'-i',str(video),'-frames:v','1','-vf','scale=960:-1' if fmt=='landscape' else 'scale=540:-1','-q:v','2',str(frames/f'{round(t*30)}.jpg')],check=True)
    receipt.append({'format':fmt,'width':v['width'],'height':v['height'],'frames':int(v['nb_frames']),'fps':v['avg_frame_rate'],'video_duration':v['duration'],'audio_duration':a['duration'],'video_codec':v['codec_name'],'audio_codec':a['codec_name'],'sample_rate':a['sample_rate'],'channels':a['channels'],'full_decode':'passed','bytes':video.stat().st_size,'review_seconds':times})
(folder/'technical-verification.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps(receipt,indent=2))
