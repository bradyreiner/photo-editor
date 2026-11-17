"""Export time-ordered frames around actual encoded transitions for review."""
from pathlib import Path
from PIL import Image,ImageDraw
import subprocess,sys
out=Path(__file__).resolve().parents[1]/'out/creative-polish-v3'
for name in (sys.argv[1:] or ['landscape','portrait']):
 p=out/f'{name}.mp4'
 frames=out/'flow-boundary-boards';frames.mkdir(exist_ok=True)
 for group,times in [('chapter',[11.55,11.8,12,12.2,12.45]),('pixel',[48.8,49.2,49.6,50,50.4]),('layers',[118.9,119.6,120.2,120.8,121.8]),('film',[195.8,196.1,196.4,196.8,197.2])]:
  w,h=(384,216) if name=='landscape' else (216,384)
  board=Image.new('RGB',(w*5,h+28),'#090d13');d=ImageDraw.Draw(board)
  for i,t in enumerate(times):
   raw=subprocess.check_output(['ffmpeg','-v','error','-ss',str(t),'-i',str(p),'-frames:v','1','-vf',f'scale={w}:{h}','-pix_fmt','rgb24','-f','rawvideo','-'])
   board.paste(Image.frombytes('RGB',(w,h),raw),(i*w,28));d.text((i*w+8,8),f'{t:.2f}s',fill='#d7ff48')
  board.save(frames/f'{name}-{group}.jpg',quality=94)
