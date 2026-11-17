"""Master from original WAV at48kHz without re-encoding the completed video."""
from pathlib import Path
import json,subprocess
ROOT=Path(__file__).resolve().parents[1]
records=[]
for id,duration in [('02',42),('03',33),('05',36)]:
    for name in['hero','hook-b','hook-c']:
        path=ROOT/f'campaign{id}/videos/{name}.mp4';temp=path.with_stem(path.stem+'-audio-master')
        subprocess.run(['ffmpeg','-v','error','-y','-i',str(path),'-i',str(ROOT/f'campaign{id}/audio/original-score.wav'),'-map','0:v:0','-map','1:a:0','-c:v','copy','-af','loudnorm=I=-18:TP=-1.5:LRA=9','-ar','48000','-c:a','aac','-b:a','192k','-t',str(duration),'-movflags','+faststart',str(temp)],check=True)
        temp.replace(path)
        records.append(str(path.relative_to(ROOT)))
(ROOT/'qa/audio-mastering.json').write_text(json.dumps({'masters':records,'source':'original sample-free WAV score; no source-camera or archive film audio','sample_rate':48000,'video':'bitstream copied, no frame/graphic changes','target_lufs':-18,'target_true_peak_db':-1.5,'note':'AAC endpoint checked separately; no claim of exact target equality.'},indent=2))
print(json.dumps({'audio_masters':len(records),'sample_rate':48000,'video_reencoding':False}))
