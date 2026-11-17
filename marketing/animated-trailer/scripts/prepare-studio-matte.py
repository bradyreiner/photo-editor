"""Local source-preserving moving foreground plates for the animated demonstration."""
from pathlib import Path
import json,time
import numpy as np,cv2,onnxruntime as ort
from PIL import Image
root=Path(__file__).resolve().parents[1];repo=root.parents[1];dst=root/'public/media/studio/hike';dst.mkdir(parents=True,exist_ok=True)
cap=cv2.VideoCapture(str(root/'public/media/refine/hike.mp4'));cap.set(cv2.CAP_PROP_POS_MSEC,2000)
o=ort.SessionOptions();o.intra_op_num_threads=4;m=repo/'.photo-editor/ai/model-bundle/selection'
e=ort.InferenceSession(str(m/'sam2_hiera_tiny_image_encoder.onnx'),sess_options=o,providers=['CPUExecutionProvider'])
d=ort.InferenceSession(str(m/'sam2_hiera_tiny_image_decoder.onnx'),sess_options=o,providers=['CPUExecutionProvider'])
scores=[];prev=None;tracked=None;previous_gray=None;start=time.time()
for i in range(185):
 ok,bgr=cap.read()
 if not ok:raise RuntimeError('Source video ended')
 rgb=cv2.cvtColor(cv2.resize(bgr,(1280,720)),cv2.COLOR_BGR2RGB);small=cv2.resize(rgb,(1024,576)).astype(np.float32)/255
 tensor=np.zeros((1,3,1024,1024),np.float32);tensor[0,:,:576]=((small-np.array([.485,.456,.406]))/np.array([.229,.224,.225])).transpose(2,0,1)
 feats=e.run(['image_embed','high_res_feats_0','high_res_feats_1'],{'image':tensor})
 # Box covers the hiker and backpack through the walking cycle; positive points
 # on pack and legs, background negatives prevent the stone stairs leaking in.
 gray=cv2.cvtColor(rgb,cv2.COLOR_RGB2GRAY)
 if tracked is None:tracked=np.array([(.36,.25),(.37,.55),(.38,.79),(.285,.33),(.435,.32)],np.float32)*np.array([1280,720],np.float32)
 elif previous_gray is not None:
  moved,status,err=cv2.calcOpticalFlowPyrLK(previous_gray,gray,tracked.reshape(-1,1,2),None,winSize=(31,31),maxLevel=4)
  valid=(status.reshape(-1)>0)&(err.reshape(-1)<45)
  tracked[valid]=moved.reshape(-1,2)[valid]
 previous_gray=gray
 if i<105:
  if i==104:prev=np.asarray(Image.open(dst/'104.png'))[:,:,3]
  continue
 if prev is None:box=[(.20,0),(.55,.99)]
 else:
  ys,xs=np.where(prev>127);box=[(max(0,float(xs.min()-18)/1280),max(0,float(ys.min()-18)/720)),(min(1,float(xs.max()+18)/1280),min(1,float(ys.max()+18)/720))]
 pts=box+[(float(x)/1280,float(y)/720) for x,y in tracked]+[(.70,.65),(.12,.45),(.72,.18)]
 labels=[2,3]+[1]*5+[0]*3
 masks,score=d.run(['masks','iou_predictions'],{'image_embed':feats[0],'high_res_feats_0':feats[1],'high_res_feats_1':feats[2],'point_coords':np.array([[[x*1024,y*576] for x,y in pts]],np.float32),'point_labels':np.array([labels],np.float32),'mask_input':np.zeros((1,1,256,256),np.float32),'has_mask_input':np.zeros(1,np.float32)})
 ix=int(np.argmax(score[0]));mask=cv2.resize(cv2.resize(masks[0,ix],(1024,1024))[:576],(1280,720))
 binary=(mask>0).astype('uint8')*255
 # Fill only enclosed holes in the selected person, keeping the outside intact.
 contours,_=cv2.findContours(binary,cv2.RETR_EXTERNAL,cv2.CHAIN_APPROX_SIMPLE)
 binary[:]=0
 cv2.drawContours(binary,[c for c in contours if cv2.contourArea(c)>100],-1,255,cv2.FILLED)
 if float((binary>0).mean())>.55:raise RuntimeError(f'Mask leaked into background at {i}')
 count,components,stats,centers=cv2.connectedComponentsWithStats((binary>0).astype('uint8'),8)
 keep=1+int(np.argmax(stats[1:,cv2.CC_STAT_AREA]))
 binary=(components==keep).astype('uint8')*255
 alpha=cv2.GaussianBlur(binary,(0,0),.65);prev=alpha
 Image.fromarray(rgb).save(dst/f'{i:03d}.jpg',quality=94)
 Image.fromarray(np.dstack([rgb,alpha])).save(dst/f'{i:03d}.png')
 scores.append(float(score[0,ix]))
 if i%15==0:print(i,round(time.time()-start,1),float(score[0,ix]),flush=True)
cap.release()
(root/'out/studio/moving-matte.json').write_text(json.dumps({'source':'media/refine/hike.mp4','source_interval':[2,8.1666667],'frames':185,'fps':30,'size':[1280,720],'method':'Pinned local SAM2 tiny ONNX, Optical-flow-tracked positive prompts and adaptive bounds per source frame, enclosed-hole cleanup. Original RGB preserved; no generated imagery.','scope':'Trailer composite plates, not evidence of native app tracking operation.','scores':scores},indent=2))
