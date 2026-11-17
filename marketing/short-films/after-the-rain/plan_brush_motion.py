"""Track native brush geometry between painted keyposes, without rendering pixels."""
import json
from pathlib import Path
import numpy as np
from PIL import Image, ImageFilter

OUT=Path(__file__).resolve().parents[2]/'outputs/after-the-rain-v2'


def sample(array, positions):
    h,w=array.shape[:2]
    x=np.clip(positions[:,0],0,w-1);y=np.clip(positions[:,1],0,h-1)
    ix=np.floor(x).astype(int);iy=np.floor(y).astype(int)
    jx=np.minimum(ix+1,w-1);jy=np.minimum(iy+1,h-1)
    u=(x-ix)[:,None];v=(y-iy)[:,None]
    return (array[iy,ix]*(1-u)+array[iy,jx]*u)*(1-v)+(array[jy,ix]*(1-u)+array[jy,jx]*u)*v


def kernel(r2):
    return r2*np.log(np.maximum(r2,1e-12))


def main():
    score=json.loads((OUT/'brush-score.json').read_text())
    meta=json.loads((OUT/'pose-flow-landmarks.json').read_text())
    flow_meta=json.loads((OUT/'pose-flow.json').read_text())
    fw,fh=flow_meta['width'],flow_meta['height']
    flow=np.fromfile(OUT/'pose-flow.f32',dtype='<f4').reshape(fh,fw,2)
    pairs=[]
    for name in ['faceContour','leftEye','rightEye','leftEyebrow','rightEyebrow','nose','noseCrest','outerLips','innerLips']:
        a,b=meta['sourceFace'].get(name,[]),meta['targetFace'].get(name,[])
        if len(a)==len(b):pairs.extend(zip(a,b))
    if len(pairs)<30:raise RuntimeError('Reliable paired face landmarks are required')
    # Hair silhouette controls supplement the observed facial planes. Fixed
    # scene controls keep the bench, daisy and robot body planted in the shot.
    pairs.extend([((295,16),(250,6)),((235,85),(191,82)),((290,150),(220,138)),((250,170),(188,151))])
    for p in [(0,0),(480,0),(960,0),(0,270),(0,540),(480,540),(960,540),
              (520,180),(510,260),(500,320),(180,290),(260,350),(100,350)]:pairs.append((p,p))
    src=np.array([a for a,b in pairs])/fw;dst=np.array([b for a,b in pairs])/fw
    p=np.c_[np.ones(len(src)),src]
    r2=((src[:,None]-src[None,:])**2).sum(axis=2)
    matrix=np.block([[kernel(r2)+np.eye(len(src))*.000025,p],[p.T,np.zeros((3,3))]])
    weights=np.linalg.solve(matrix,np.r_[dst-src,np.zeros((3,2))])
    def face_warp(points):
        result=[]
        for start in range(0,len(points),4096):
            query=points[start:start+4096]/fw
            r2=((query[:,None]-src[None,:])**2).sum(axis=2)
            result.append((kernel(r2)@weights[:len(src)]+np.c_[np.ones(len(query)),query]@weights[len(src):])*fw)
        return np.concatenate(result)
    def ellipse(points,cx,cy,rx,ry):
        radius=np.sqrt(((points-np.array([cx,cy]))/np.array([rx,ry]))**2 @ np.ones(2))
        return np.clip((1.15-radius)/.32,0,1)[:,None]
    head_polygon=np.array([(275,20),(315,8),(365,26),(415,60),(440,111),(430,155),
        (410,183),(390,207),(370,225),(350,218),(329,210),(297,205),(260,184),
        (235,143),(230,100),(245,58)],dtype=float)
    def inside(points,polygon):
        x,y=points[:,0],points[:,1];hit=np.zeros(len(points),dtype=bool)
        for i in range(len(polygon)):
            a,b=polygon[i-1],polygon[i]
            if abs(b[1]-a[1])<1e-8:continue
            hit^=((a[1]>y)!=(b[1]>y)) & (x<(b[0]-a[0])*(y-a[1])/(b[1]-a[1])+a[0])
        return hit
    target_head_polygon=head_polygon+face_warp(head_polygon)
    def warp(points):
        head=inside(points,head_polygon).astype(float)[:,None]
        robot=ellipse(points,716,172,100,76)
        raw=sample(flow,points)
        raw=np.clip(raw,-28,28)
        field=raw*robot
        field=field*(1-head)+face_warp(points)*head
        return points+field,np.maximum(head,robot)
    sx,sy=score['width']/fw,score['height']/fh
    scale=np.array([sx,sy])
    target=Image.open(OUT/'reaction-reference.png').convert('RGB').resize((score['width'],score['height']),Image.Resampling.LANCZOS)
    for li,layer in enumerate(score['layers']):
        a=np.array(layer['strokes'],dtype=float)
        centers=a[:,:2]/scale;half=a[:,2:4]/scale
        mapped,mask=warp(centers)
        foreground=inside(centers,head_polygon)
        # A stroke belongs to one moving surface. Crossing a semantic boundary
        # must never stretch its two endpoints onto different objects.
        robot=ellipse(centers,716,172,100,76)
        left=centers-half+np.clip(sample(flow,centers-half),-28,28)*robot
        right=centers+half+np.clip(sample(flow,centers+half),-28,28)*robot
        left[foreground]=centers[foreground]-half[foreground]+face_warp(centers[foreground]-half[foreground])
        right[foreground]=centers[foreground]+half[foreground]+face_warp(centers[foreground]+half[foreground])
        movement=(mapped-centers)*scale
        new_half=(right-left)/2
        limit=np.maximum(np.linalg.norm(half,axis=1)*1.7,.25)
        new_half*=np.minimum(1,limit/np.maximum(np.linalg.norm(new_half,axis=1),1e-6))[:,None]
        segment=(new_half-half)*scale
        diameter=float(np.median(a[:,4]))
        colors=sample(np.asarray(target.filter(ImageFilter.GaussianBlur(diameter*.12)),dtype=float),mapped*scale)
        colors=a[:,5:8]+(colors-a[:,5:8])*mask
        extra=np.c_[movement,segment,colors]
        tracked=np.c_[a,np.round(extra,2)]
        # The end-pose painting supplies only newly revealed background colors.
        # These remain ordinary fixed colored brush marks behind the moving head.
        reveal=foreground & ~inside(centers,target_head_polygon)
        fill=a[reveal].copy()
        fill[:,5:8]=sample(np.asarray(target.filter(ImageFilter.GaussianBlur(diameter*.12)),dtype=float),fill[:,:2])
        fill[:,8]=1
        background=np.c_[fill,np.zeros((len(fill),4)),fill[:,5:8]]
        # Keep subject strokes in front of the local background at each scale.
        layer['strokes']=np.r_[background,tracked[~foreground],tracked[foreground]].tolist()
        print(layer['name'],len(a),'tracked marks',flush=True)
    score['motionMethod']='Native Vision flow + facial landmarks, localized thin-plate deformation of brush geometry; no image crossfade'
    score['poseLandmarkCount']=len(pairs)
    (OUT/'motion-brush-score-v3.json').write_text(json.dumps(score,separators=(',',':')))
    print('Native temporal brush score ready',flush=True)


if __name__=='__main__':main()
