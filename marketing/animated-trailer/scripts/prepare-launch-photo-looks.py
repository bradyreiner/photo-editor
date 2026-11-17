"""Nongenerative image variants for the authored color/detail illustration.
These are trailer assets made from camera pixels, not native Looksy exports.
"""
from pathlib import Path
import cv2,numpy as np,json,hashlib
from PIL import Image
root=Path(__file__).resolve().parents[1];source=root/'public/media/IMG_5106.jpg'
a=np.asarray(Image.open(source).convert('RGB'));hsv=cv2.cvtColor(a,cv2.COLOR_RGB2HSV).astype(np.float32)
# Soft blue range; neutral colors and the yellow jacket are protected.
h=hsv[:,:,0]*2;s=hsv[:,:,1]/255
weight=np.clip(1-np.abs(h-224)/42,0,1)*np.clip((s-.06)/.18,0,1)
hsv[:,:,0]=(hsv[:,:,0]-4.5*weight)%180;hsv[:,:,1]=np.clip(hsv[:,:,1]*(1+.18*weight),0,255);hsv[:,:,2]*=(1-.08*weight)
color=cv2.cvtColor(hsv.astype(np.uint8),cv2.COLOR_HSV2RGB)
clean=cv2.fastNlMeansDenoisingColored(color,None,2.0,2.0,7,21)
blur=cv2.GaussianBlur(clean,(0,0),.8);detail=cv2.addWeighted(clean,1.4,blur,-.4,0)
for name,data in [('IMG_5106-color',color),('IMG_5106-developed',detail)]:Image.fromarray(data).save(root/'public/media'/f'{name}.jpg',quality=98,subsampling=0)
mask=np.asarray(Image.open(root/'public/media/IMG_5106-subject-mask.png').convert('L'));Image.fromarray(np.dstack([detail,mask])).save(root/'public/media/IMG_5106-developed-subject.png')
receipt={'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'method':'Soft blue hue range, nongenerative local HSV color illustration, low-strength denoise and unsharp detail illustration. Original camera RGB is the only image source.','limits':'These graphic response assets illustrate controls; they are not exports from the native custom engine.'}
(root/'out/launch-subject/photo-response-provenance.json').write_text(json.dumps(receipt,indent=2)+'\n')
print('Prepared source-derived color/detail image variants; originals unchanged.')
