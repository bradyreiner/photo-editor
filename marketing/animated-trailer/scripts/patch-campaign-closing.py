"""Assemble the corrected six-second closing with the reviewed body and audio."""
from pathlib import Path
import subprocess,json,hashlib,shutil
root=Path(__file__).resolve().parents[1]
base=root/'out/looksy-launch-workflows.mp4'
closing=root/'out/campaign/corrected-closing.mp4'
final=root/'out/looksy-launch-workflows-final.mp4'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
before=json.loads((root/'out/campaign/render-inputs.json').read_text())
after={name:sha(root/name) for name in before}
changed=[name for name in before if before[name]!=after[name]]
assert changed==['src/campaign/Closing.tsx'],changed
subprocess.run(['ffmpeg','-hide_banner','-nostdin','-i',str(base),'-i',str(closing),'-filter_complex','[0:v]trim=start_frame=0:end_frame=8880,setpts=PTS-STARTPTS[v0];[1:v]trim=start_frame=0:end_frame=360,setpts=PTS-STARTPTS[v1];[v0][v1]concat=n=2:v=1:a=0[picture]','-map','[picture]','-map','0:a:0','-t','154','-r','60','-fps_mode','cfr','-c:v','libx264','-preset','fast','-crf','14','-pix_fmt','yuv420p','-colorspace','bt709','-color_trc','bt709','-color_primaries','bt709','-color_range','tv','-c:a','copy','-movflags','+faststart','-y',str(final)],check=True)
shutil.copy2(root/'out/campaign/render-inputs.json',root/'out/campaign/render-inputs-base.json')
shutil.copy2(root/'out/campaign/technical-verification.json',root/'out/campaign/base-technical-verification.json')
(root/'out/campaign/render-inputs.json').write_text(json.dumps(after,indent=2)+'\n')
receipt={'base_sha256':sha(base),'closing_sha256':sha(closing),'final_sha256':sha(final),'replaced_frame_range':[8880,9239],'source_changes':changed,'picture':'Reviewed first 148 seconds plus corrected 6-second closing, encoded at CRF14 / fast / Rec.709. One additional high-quality picture encode.','audio':'Original 154-second mastered AAC stream copied without re-encoding.','visual_fix':'All three finished-work images remain through the last frame.'}
(root/'out/campaign/final-picture-patch.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps(receipt,indent=2))
