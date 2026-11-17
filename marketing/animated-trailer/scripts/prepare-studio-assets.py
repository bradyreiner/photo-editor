from pathlib import Path
import json,subprocess,hashlib
import numpy as np
from PIL import Image
root=Path(__file__).resolve().parents[1];media=root/'public/media/studio';out=root/'out/studio'
def run(a):subprocess.run(a,check=True,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
def image(p):return np.asarray(Image.open(p).convert('RGB'))
a=image(media/'color/4-8.png');b=image(media/'color/subject-global.png');mask=Image.open(root/'public/media/IMG_5106-subject-mask.png').convert('L').resize((a.shape[1],a.shape[0]),Image.Resampling.LANCZOS);m=np.asarray(mask)/255.
Image.fromarray(np.dstack([a,np.asarray(mask)])).save(media/'subject.png')
Image.fromarray(np.dstack([np.full_like(a,[114,222,215]),(m*120).astype('uint8')])).save(media/'selection.png')
blend=np.rint(a*(1-m[:,:,None])+b*m[:,:,None]).astype('uint8');Image.fromarray(blend).save(media/'subject-lift.png');outside=m==0;assert np.array_equal(a[outside],blend[outside])
(out/'mask-response.json').write_text(json.dumps({'source':'Actual IMG_5106.CR2 native engine plates','mask':'Locally selected subject, scaled to the exact native preview raster','scope':'Mask composite of native engine exposures, for animated demonstration; outside alpha-zero pixels unchanged exactly.','outside_mask_changed_pixels':int(np.any(a[outside]!=blend[outside],axis=1).sum()),'mean_subject_response':float(np.abs(blend.astype(float)-a).mean())},indent=2))
for kind in ['look','mono']:
 pix=image(media/f'color/lut-{kind}.png').reshape(-1,3)
 cube=media/f'looksy-{kind}.cube';cube.write_text('TITLE "Looksy native CPU color response"\nLUT_3D_SIZE 33\nDOMAIN_MIN 0 0 0\nDOMAIN_MAX 1 1 1\n'+''.join(' '.join(f'{v/255:.8f}' for v in row)+'\n' for row in pix))
 run(['ffmpeg','-v','error','-y','-ss','2','-i',str(root/'public/media/launch-1268.mp4'),'-t','6','-an','-vf',f'lut3d={cube}:interp=tetrahedral','-c:v','libx264','-preset','fast','-crf','15','-pix_fmt','yuv420p','-color_primaries','bt709','-color_trc','bt709','-colorspace','bt709',str(media/f'city-{ "grade" if kind=="look" else "mono"}.mp4')])
run(['ffmpeg','-v','error','-y','-framerate','30','-i',str(media/'scene/%04d.png'),'-an','-c:v','libx264','-preset','fast','-crf','14','-pix_fmt','yuv420p','-color_primaries','bt709','-color_trc','bt709','-colorspace','bt709',str(media/'gaussian-camera.mp4')])
for name,src,at in [('arrival','refine/arrival.mp4',6),('ridge','MVI_5051-2.3-5.9.mp4',1),('night','refine/night.mp4',1)]:run(['ffmpeg','-v','error','-y','-ss',str(at),'-i',str(root/'public/media'/src),'-frames:v','1','-vf','scale=640:-1',str(media/f'{name}-thumb.jpg')])
samples=json.loads((media/'scene/native-render.json').read_text())['cameras'];(root/'src/studio-film/scene-data.ts').write_text('export const sceneSamples='+json.dumps(samples,separators=(',',':'))+';\n')
print('Prepared native response composites, native-engine footage LUTs, Gaussian camera movie and exact source thumbnails.',flush=True)
