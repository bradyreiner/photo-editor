"""Prepare a local non-generative subject matte using Looksy's pinned SAM2 pair.
This invokes the model directly; it is not a recording of the Qt application.
"""
from pathlib import Path
import json, hashlib
import cv2
import numpy as np
import onnxruntime as ort
from PIL import Image, ImageCms
root=Path(__file__).resolve().parents[1]
repo=root.parents[1]
models=repo/'.photo-editor/ai/model-bundle/selection'
source=root/'public/media/IMG_5106.jpg'
out=root/'out/launch-subject';out.mkdir(exist_ok=True)
image=Image.open(source)
if image.info.get('icc_profile'):
 import io
 image=ImageCms.profileToProfile(image,ImageCms.ImageCmsProfile(io.BytesIO(image.info['icc_profile'])),ImageCms.createProfile('sRGB'),outputMode='RGB')
image=image.convert('RGB');width,height=image.size
ratio=1024/max(width,height);rw,rh=round(width*ratio),round(height*ratio)
resized=np.asarray(image.resize((rw,rh),Image.Resampling.BILINEAR),dtype=np.float32)/255
normalized=(resized-np.array([.485,.456,.406],np.float32))/np.array([.229,.224,.225],np.float32)
input_image=np.zeros((1,3,1024,1024),np.float32);input_image[0,:,:rh,:rw]=normalized.transpose(2,0,1)
options=ort.SessionOptions();options.intra_op_num_threads=4
encoder_path=models/'sam2_hiera_tiny_image_encoder.onnx';decoder_path=models/'sam2_hiera_tiny_image_decoder.onnx'
encoder=ort.InferenceSession(str(encoder_path),sess_options=options,providers=['CPUExecutionProvider'])
features=encoder.run(['image_embed','high_res_feats_0','high_res_feats_1'],{'image':input_image})
print('Encoded original source locally',flush=True)
# Tight box plus explicit subject/body/feet positives and background negatives.
points=[(455,740),(715,1560),(580,960),(600,1300),(630,1508),(420,1110),(760,1400)]
labels=[2,3,1,1,1,0,0]
coords=np.array([[[x/width*rw,y/height*rh] for x,y in points]],np.float32)
decoder=ort.InferenceSession(str(decoder_path),sess_options=options,providers=['CPUExecutionProvider'])
values={'image_embed':features[0],'high_res_feats_0':features[1],'high_res_feats_1':features[2], 'point_coords':coords,'point_labels':np.array([labels],np.float32),'mask_input':np.zeros((1,1,256,256),np.float32),'has_mask_input':np.zeros(1,np.float32)}
masks,scores=decoder.run(['masks','iou_predictions'],values)
index=int(np.argmax(scores[0]));logits=masks[0,index]
full=cv2.resize(logits,(1024,1024),interpolation=cv2.INTER_LINEAR)[:rh,:rw]
full=cv2.resize(full,(width,height),interpolation=cv2.INTER_LINEAR)
alpha=(255/(1+np.exp(-np.clip(full*1.5,-30,30)))).astype('uint8')
mask_path=root/'public/media/IMG_5106-subject-mask.png';Image.fromarray(alpha).save(mask_path)
rgba=np.dstack([np.asarray(image),alpha]);Image.fromarray(rgba).save(root/'public/media/IMG_5106-subject.png')
contours,_=cv2.findContours((alpha>127).astype('uint8'),cv2.RETR_EXTERNAL,cv2.CHAIN_APPROX_SIMPLE)
contour=max(contours,key=cv2.contourArea);curve=cv2.approxPolyDP(contour,.45,True)[:,0,:]
path='M'+' L'.join(f'{x} {y}' for x,y in curve)+' Z'
(root/'src/launch/subject-contour.ts').write_text('// Derived from local SAM2 mask of original Nepal source, not an invented silhouette.\nexport const subjectContour='+json.dumps(path)+';\n')
checker=np.zeros((height,width,3),dtype=np.uint8)
yy,xx=np.indices((height,width));checker[:]=np.where(((xx//35+yy//35)%2)[...,None],70,100)
a=alpha[:,:,None]/255;proof=(np.asarray(image)*a+checker*(1-a)).astype('uint8')
Image.fromarray(proof).save(out/'subject-proof.jpg',quality=95)
receipt={'source':source.name,'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'models':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in [encoder_path,decoder_path]},'method':'Direct CPU ONNX inference with the same pinned encoder/decoder as Looksy; normalized/padded source; box and point prompts authored for the trailer; selected highest model-scored candidate','prompts':points,'labels':labels,'scores':scores.tolist(),'selected':index,'mask_bounds':list(map(int,cv2.boundingRect(contour))),'contour_vertices':len(curve),'note':'Nongenerative matte only. Original RGB pixels preserved. This is not native Qt operation proof.'}
(out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps({'scores':scores.tolist(),'selected':index,'bounds':receipt['mask_bounds'],'vertices':len(curve)}),flush=True)
