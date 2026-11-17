from pathlib import Path
import numpy as np,cv2,onnxruntime as ort
from PIL import Image
root=Path(__file__).resolve().parents[1];repo=root.parents[1];media=root/'public/media/refine'
im=Image.open(media/'portrait-window.jpg').convert('RGB')
w,h=im.size;rw,rh=683,1024
s=np.asarray(im.resize((rw,rh)),np.float32)/255;t=np.zeros((1,3,1024,1024),np.float32);t[0,:,:rh,:rw]=((s-np.array([.485,.456,.406]))/np.array([.229,.224,.225])).transpose(2,0,1)
o=ort.SessionOptions();o.intra_op_num_threads=4;m=repo/'.photo-editor/ai/model-bundle/selection'
e=ort.InferenceSession(str(m/'sam2_hiera_tiny_image_encoder.onnx'),sess_options=o,providers=['CPUExecutionProvider']);f=e.run(['image_embed','high_res_feats_0','high_res_feats_1'],{'image':t})
d=ort.InferenceSession(str(m/'sam2_hiera_tiny_image_decoder.onnx'),sess_options=o,providers=['CPUExecutionProvider'])
points=[(.10,.32),(.9,.99),(.48,.53),(.45,.8),(.1,.1),(.95,.2)];labels=[2,3,1,1,0,0]
masks,scores=d.run(['masks','iou_predictions'],{'image_embed':f[0],'high_res_feats_0':f[1],'high_res_feats_1':f[2],'point_coords':np.array([[[x*rw,y*rh] for x,y in points]],np.float32),'point_labels':np.array([labels],np.float32),'mask_input':np.zeros((1,1,256,256),np.float32),'has_mask_input':np.zeros(1,np.float32)})
ix=int(np.argmax(scores[0]));mask=cv2.resize(cv2.resize(masks[0,ix],(1024,1024))[:rh,:rw],(w,h));alpha=cv2.GaussianBlur((mask>0).astype('uint8')*255,(0,0),.55)
rgba=np.dstack([np.asarray(im),alpha]);Image.fromarray(rgba).save(media/'portrait-subject.png')
