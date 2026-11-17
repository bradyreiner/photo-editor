"""Prepare a new continuous produced electronic cue for the rebuilt film."""
from pathlib import Path
import subprocess,json,hashlib,urllib.request
root=Path(__file__).resolve().parents[1]
source=root/'public/audio/cat-walk-arulo.mp3'
url='https://assets.mixkit.co/music/371/371.mp3'
if not source.exists():
 request=urllib.request.Request(url,headers={'User-Agent':'Looksy trailer music preparation'})
 with urllib.request.urlopen(request,timeout=60) as response:source.write_bytes(response.read())
meta=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_entries','format=duration','-of','json',str(source)]));duration=float(meta['format']['duration'])
ratio=1.0
subprocess.run(['ffmpeg','-v','error','-nostdin','-i',str(source),'-af','apad,atrim=duration=124,afade=t=in:d=0.08,afade=t=out:st=122.4:d=1.6','-ar','48000','-ac','2','-c:a','pcm_s16le','-y',str(root/'public/audio/looksy-launch-score.wav')],check=True)
receipt={'title':'Cat Walk','artist':'Arulo','catalog':'https://mixkit.co/free-stock-music/house/','source':url,'license':'https://mixkit.co/license/modal/musicFree/','duration_seconds':duration,'pitch_preserving_tempo_factor':ratio,'arrangement':'Complete produced cue at its original tempo and pitch, with a short end fade and 10ms padding to 124 seconds. No repeated phrase or middle splice. Picture-specific gain and sounds are authored in LaunchScore.tsx.','source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'status':'Soundtrack candidate; full creative/aural review remains unproven.'}
(root/'launch-music-source.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps({'duration':duration,'tempo_factor':ratio,'output_seconds':124}))
