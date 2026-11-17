"""Create source-derived illustration responses and a real local portrait mask."""
from pathlib import Path
import numpy as np,cv2,onnxruntime as ort,json,subprocess
from PIL import Image
root=Path(__file__).resolve().parents[1];repo=root.parents[1];media=root/'public/media/range'
# Illustrative color responses on the licensed JPEG, not native RAW exports.
a=np.asarray(Image.open(media/'product.jpg').convert('RGB')).astype(np.float32)/255
for name,gain,warm in [('product-exposure',1.16,0),('product-highlights',1.13,0),('product-developed',1.13,.025)]:
 b=np.clip(a*gain*np.array([1+warm,1,1-warm]),0,1);Image.fromarray((b*255).astype('uint8')).save(media/(name+'.jpg'),quality=97,subsampling=0)
im=Image.open(media/'portrait.jpg').convert('RGB').resize((1200,1800),Image.Resampling.LANCZOS);im.save(media/'portrait-1200.jpg',quality=97,subsampling=0)
w,h=im.size;rw,rh=683,1024
s=np.asarray(im.resize((rw,rh)),np.float32)/255;t=np.zeros((1,3,1024,1024),np.float32);t[0,:,:rh,:rw]=((s-np.array([.485,.456,.406]))/np.array([.229,.224,.225])).transpose(2,0,1)
o=ort.SessionOptions();o.intra_op_num_threads=4;m=repo/'.photo-editor/ai/model-bundle/selection'
e=ort.InferenceSession(str(m/'sam2_hiera_tiny_image_encoder.onnx'),sess_options=o,providers=['CPUExecutionProvider']);f=e.run(['image_embed','high_res_feats_0','high_res_feats_1'],{'image':t})
d=ort.InferenceSession(str(m/'sam2_hiera_tiny_image_decoder.onnx'),sess_options=o,providers=['CPUExecutionProvider'])
points=[(.19,.10),(.91,.99),(.51,.3),(.5,.6),(.1,.4),(.92,.2)];labels=[2,3,1,1,0,0]
masks,scores=d.run(['masks','iou_predictions'],{'image_embed':f[0],'high_res_feats_0':f[1],'high_res_feats_1':f[2],'point_coords':np.array([[[x*rw,y*rh] for x,y in points]],np.float32),'point_labels':np.array([labels],np.float32),'mask_input':np.zeros((1,1,256,256),np.float32),'has_mask_input':np.zeros(1,np.float32)})
ix=int(np.argmax(scores[0]));mask=cv2.resize(cv2.resize(masks[0,ix],(1024,1024))[:rh,:rw],(w,h));alpha=cv2.GaussianBlur((mask>0).astype('uint8')*255,(0,0),.55)
rgba=np.dstack([np.asarray(im),alpha]);Image.fromarray(rgba).save(media/'portrait-subject.png')
(root/'out/polish/portrait-mask.json').write_text(json.dumps({'model':'App pinned SAM2 tiny, local CPU inference','scores':scores.tolist(),'selected':ix,'scope':'Source selection alpha, not generated photography or a native UI recording.'},indent=2)+'\n')
subprocess.run(['ffmpeg','-v','error','-i',str(media/'skate.mp4'),'-ss','1','-frames:v','1','-y',str(media/'skate-thumbnail.jpg')],check=True)
print('Range responses, portrait selection and video thumbnail ready.')
