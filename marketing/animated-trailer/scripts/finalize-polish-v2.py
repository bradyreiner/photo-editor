"""Finish the complete fresh 288-second picture renders; no legacy patch splices."""
from pathlib import Path
import subprocess,json,hashlib,sys
root=Path(__file__).resolve().parents[1]
name=sys.argv[1]
assert name in ['landscape','portrait']
out=root/'out/creative-polish-v2'
source=out/f'{name}-picture.mp4'
audio=root/'public/audio/creative-polish-v2-master.wav'
dest=out/f'{name}.mp4'
probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_streams','-show_format','-of','json',str(source)],text=True))
v=next(s for s in probe['streams'] if s['codec_type']=='video')
assert int(v['nb_frames'])==8640 and v['avg_frame_rate']=='30/1',v
# Remotion 4 default encodes the RGB composition with BT.601 matrix, sRGB TRC
# and BT.709 primaries. Convert the actual matrix, retaining the original TRC.
assert v.get('color_space') in [None,'unknown','smpte170m','bt470bg'],v.get('color_space')
subprocess.run(['ffmpeg','-hide_banner','-y','-i',str(source),'-i',str(audio),'-map','0:v:0','-map','1:a:0','-vf','zscale=matrixin=170m:matrix=709:rangein=limited:range=limited','-c:v','libx264','-preset','fast','-crf','17','-pix_fmt','yuv420p','-color_primaries','bt709','-color_trc','iec61966-2-1','-colorspace','bt709','-color_range','tv','-c:a','aac','-b:a','256k','-ar','48000','-movflags','+faststart','-t','288',str(dest)],check=True)
receipt={'picture_source':str(source.relative_to(root)),'picture_frames':8640,'duration':288,'fps':30,'audio_source':str(audio.relative_to(root)),'audio_sha256':hashlib.sha256(audio.read_bytes()).hexdigest(),'output':str(dest.relative_to(root)),'bytes':dest.stat().st_size,'sha256':hashlib.sha256(dest.read_bytes()).hexdigest(),'color':'Converted default BT.601 matrix to BT.709 with explicit-range zscale, BT.709 primaries, retained original sRGB transfer, limited range. Not merely relabeled.','encode':'H.264 CRF 17, fast, yuv420p; AAC 256kbps stereo 48kHz; faststart. Full fresh picture; no old-version correction splices.'}
(out/f'{name}-finish.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps(receipt,indent=2))
