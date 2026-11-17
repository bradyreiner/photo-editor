from pathlib import Path
import subprocess,json,hashlib,urllib.request
root=Path(__file__).resolve().parents[1]
source=root/'public/audio/minimal-techno-01-alejandro-magana.mp3'
assets={
 'minimal-techno-01-alejandro-magana.mp3':'https://assets.mixkit.co/music/162/162.mp3',
 'foley/interface-click.mp3':'https://assets.mixkit.co/active_storage/sfx/1126/1126-preview.mp3',
 'foley/classic-click.mp3':'https://assets.mixkit.co/active_storage/sfx/1116/1116-preview.mp3',
 'foley/quick-air-woosh.mp3':'https://assets.mixkit.co/active_storage/sfx/2605/2605-preview.mp3',
}
for name,url in assets.items():
 target=root/'public/audio'/name
 if not target.exists():
  target.parent.mkdir(parents=True,exist_ok=True)
  request=urllib.request.Request(url,headers={'User-Agent':'Looksy trailer asset preparation'})
  with urllib.request.urlopen(request,timeout=60) as response:
   target.write_bytes(response.read())
raw=subprocess.check_output(['ffmpeg','-v','error','-i',str(source),'-f','f32le','-ac','1','-ar','11025','-'])
duration=len(raw)/4/11025
ratio=duration/120
subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-nostdin','-i',str(source),'-af',f'atempo={ratio:.12f},apad,atrim=duration=120,afade=t=in:d=0.06,afade=t=out:st=118.6:d=1.4','-ar','48000','-ac','2','-c:a','pcm_s16le','-y',str(root/'public/audio/looksy-continuous-score.wav')],check=True)
receipt={'primary_track':'Minimal Techno 01','artist':'Alejandro Magaña (A. M.)','catalog':'https://mixkit.co/free-stock-music/techno/','asset':'https://assets.mixkit.co/music/162/162.mp3','license':'https://mixkit.co/license/modal/musicFree/','source_duration':duration,'pitch_preserving_tempo_factor':ratio,'film_duration':120,'arrangement':'Entire produced cue, pitch-preserving fit to 120 seconds; no repeated middle phrase or arbitrary crossfade. Section gain is authored in the film.','status':'Production selection; auditory fit requires human review','source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'sfx':[{'title':'Interface click','source':'https://assets.mixkit.co/active_storage/sfx/1126/1126-preview.mp3'},{'title':'Single classic click','source':'https://assets.mixkit.co/active_storage/sfx/1116/1116-preview.mp3'},{'title':'Quick air woosh','source':'https://assets.mixkit.co/active_storage/sfx/2605/2605-preview.mp3'}],'sfx_license':'https://mixkit.co/license/modal/sfxFree/'}
(root/'polished-soundtrack-source.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(f'Prepared complete, continuous 120-second score with {ratio:.6f} pitch-preserving tempo fit.')
