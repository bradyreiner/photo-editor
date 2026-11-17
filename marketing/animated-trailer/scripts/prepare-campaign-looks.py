"""Prepare illustrative JPEG color responses and a genuine native local retouch.

Stock JPEGs illustrate the editing story; they are not camera RAW files or native
color-engine exports. Retouch pixels come from production run_ai_inpaint, built
headlessly without ONNX to exercise Local CPU Fill. No generative image model.
"""
from pathlib import Path
import hashlib,json,subprocess,shlex
import numpy as np
from PIL import Image,ImageDraw
root=Path(__file__).resolve().parents[1]
repo=root.parents[1]
media=root/'public/media/campaign'
out=root/'out/campaign';out.mkdir(exist_ok=True)
def develop(rgb, exposure=.32, highlights=.18, warmth=.018):
 x=rgb.astype(np.float32)/255
 linear=np.where(x<=.04045,x/12.92,((x+.055)/1.055)**2.4)
 linear*=2**exposure
 linear*=np.array([1+warmth,1,1-warmth],np.float32)
 luma=linear@np.array([.2126,.7152,.0722],np.float32)
 linear*=((1+.08*(1-np.clip(luma,0,1))**3)/(1+highlights*np.maximum(luma-.25,0)))[...,None]
 x=np.where(linear<=.0031308,linear*12.92,1.055*np.maximum(linear,0)**(1/2.4)-.055)
 return np.clip(x*255,0,255).astype('uint8')
for name in ['wedding-bride-01','wedding-bride-02','wedding-bride-03','wedding-couple','wedding-dawn']:
 rgb=np.asarray(Image.open(media/f'{name}.jpg').convert('RGB'))
 Image.fromarray(develop(rgb)).save(media/f'{name}-developed.jpg',quality=97,subsampling=0)
 if name=='wedding-bride-02':
  Image.fromarray(develop(rgb,exposure=.32,highlights=0,warmth=0)).save(media/'wedding-exposure.jpg',quality=97,subsampling=0)
  Image.fromarray(develop(rgb,exposure=.32,highlights=.18,warmth=0)).save(media/'wedding-highlights.jpg',quality=97,subsampling=0)
source=media/'wedding-bride-02-developed.jpg'
image=Image.open(source).convert('RGB');image.save(out/'retouch-input.png')
mask=Image.new('L',image.size);brush=ImageDraw.Draw(mask)
spots=[(519,739,7),(552,761,8),(536,790,7),(603,737,6),(592,830,7)]
for x,y,r in spots:brush.ellipse((x-r,y-r,x+r,y+r),fill=255)
mask.save(media/'wedding-skin-mask.png')
flags=shlex.split(subprocess.check_output(['pkg-config','--cflags','--libs','Qt6Core','Qt6Gui'],text=True))
subprocess.run(['clang++','-std=c++20','-O2','-include','cstring','-I'+str(repo/'apps/desktop/src'),str(root/'tools/native-retouch.cpp'),str(repo/'apps/desktop/src/ai_model_runtime.cpp'),str(repo/'apps/desktop/src/vision_segmentation_stub.cpp'),'-Wl,-rpath,/opt/homebrew/lib','-o',str(out/'native-retouch')]+flags,check=True)
result=subprocess.run([str(out/'native-retouch'),str(out/'retouch-input.png'),str(media/'wedding-skin-mask.png'),str(media/'wedding-skin-retouched.png')],capture_output=True,text=True,check=True)
receipt=json.loads(result.stdout);receipt['brush_spots']=spots
before=np.asarray(image);after=np.asarray(Image.open(media/'wedding-skin-retouched.png').convert('RGB'))
assert np.array_equal(before[np.asarray(mask)==0],after[np.asarray(mask)==0])
assert np.any(before!=after)
receipt['outside_mask_exact']=True
receipt['source_sha256']=hashlib.sha256(source.read_bytes()).hexdigest()
receipt['native_source_sha256']=hashlib.sha256((repo/'apps/desktop/src/ai_model_runtime.cpp').read_bytes()).hexdigest()
receipt['color_response_scope']='Conventional illustrative local color transforms on licensed photographic JPEGs; not camera RAW metadata or a native color-engine render.'
(out/'native-skin-retouch.json').write_text(json.dumps(receipt,indent=2)+'\n')
# Real source-resolution detail, not generated or sharpened detail.
original=Image.open(root/'assets/licensed/weddings/wedding-bride-02.jpg');w,h=original.size
original.crop((1204,3333,2124,4293)).save(media/'wedding-lace-detail.jpg',quality=97,subsampling=0)
print(json.dumps(receipt,indent=2))
