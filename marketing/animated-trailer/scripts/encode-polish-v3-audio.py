"""Use the verified AAC coder; the default coder introduced reconstruction peaks."""
from pathlib import Path
import subprocess,json,re,hashlib
r=Path(__file__).resolve().parents[1];out=r/'out/creative-polish-v3';source=r/'public/audio/creative-polish-v3-master.wav';dest=out/'audio-delivery.m4a'
subprocess.run(['ffmpeg','-hide_banner','-y','-i',str(source),'-c:a','aac','-aac_coder','fast','-b:a','320k','-ar','48000',str(dest)],check=True)
p=subprocess.run(['ffmpeg','-hide_banner','-i',str(dest),'-af','loudnorm=I=-16:TP=-1.5:LRA=9:print_format=json','-f','null','-'],capture_output=True,text=True,check=True);(out/'audio-delivery-loudness.log').write_text(p.stderr);levels=json.loads(re.search(r'\{\s*"input_i".*?\}',p.stderr,re.S).group(0));assert float(levels['input_tp'])<=-1.5,levels
(out/'audio-delivery.json').write_text(json.dumps({'source':str(source.relative_to(r)),'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'output':str(dest.relative_to(r)),'sha256':hashlib.sha256(dest.read_bytes()).hexdigest(),'codec':'AAC LC / fast coder / 320kbps / stereo 48kHz','measured':levels,'reason':'The default two-loop 256kbps encoding introduced isolated reconstructed peaks above full scale; fast coder at 320kbps preserved the master peaks and loudness.'},indent=2)+'\n')
