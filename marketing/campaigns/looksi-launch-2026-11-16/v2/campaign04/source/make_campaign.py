#!/usr/bin/env python3
"""LOOKSi 04: original kinetic graphic film. All drawings/audio authored here.

Run from repository root with marketing/.venv/bin/python PATH/source/make_campaign.py.
No software UI, product performance or captured editing results are represented.
"""
from pathlib import Path
from functools import lru_cache
import argparse, base64, html, json, math, shutil, subprocess, sys, tempfile, wave
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter, ImageChops, ImageCms

ROOT = Path(__file__).resolve().parents[1]
REPO = next((p for p in Path(__file__).resolve().parents if (p/'apps/desktop/assets/fonts').exists()),ROOT)
FONTS = ROOT/'source/fonts' if (ROOT/'source/fonts').exists() else REPO/'apps/desktop/assets/fonts'
W,H,FPS,SECONDS = 1080,1920,24,24
VIDEO_COLOR_FILTER='scale=in_range=pc:out_range=tv:out_color_matrix=bt709,setparams=range=limited:color_primaries=bt709:color_trc=bt709:colorspace=bt709'
VIDEO_COLOR_ARGS=['-color_range','tv','-colorspace','bt709','-color_primaries','bt709','-color_trc','bt709']
SRGB_PROFILE=ImageCms.ImageCmsProfile(ImageCms.createProfile('sRGB')).tobytes()
INK=(7,8,10); PAPER=(243,243,237); GRAY=(145,149,150)
R=(255,59,48); G=(48,209,88); B=(50,130,255)
FONT = {'sans':FONTS/'spacegrotesk/SpaceGrotesk[wght].ttf',
        'serif':FONTS/'instrumentserif/InstrumentSerif.ttf',
        'italic':FONTS/'instrumentserif/InstrumentSerif-Italic.ttf',
        'mono':FONTS/'spacemono/SpaceMono-Regular.ttf'}
HOOKS = {
 'hero': ['The last', 'tweak.'],
 'hook-b': ['Just one', 'more pixel.'],
 'hook-c': ['You said', 'it was done.'],
}

@lru_cache(256)
def font(size,kind='sans'):
    f=ImageFont.truetype(str(FONT[kind]),int(size))
    if kind=='sans':
        try: f.set_variation_by_axes([600])
        except Exception: pass
    return f

def ease(v):
    v=max(0,min(1,v));return 1-(1-v)**3
def smooth(v):
    v=max(0,min(1,v));return v*v*(3-2*v)
def enter(t,a,d=.5): return ease((t-a)/d)
def mix(a,b,v): return tuple(round(a[i]*(1-v)+b[i]*v) for i in range(3))
def text(im,xy,s,size=48,kind='sans',color=PAPER,anchor=None,spacing=0):
    dr=ImageDraw.Draw(im)
    if spacing:
        x,y=xy
        for ch in s:
            dr.text((x,y),ch,font=font(size,kind),fill=color,anchor=anchor)
            x+=dr.textlength(ch,font=font(size,kind))+spacing
    else: dr.text(xy,s,font=font(size,kind),fill=color,anchor=anchor)
def center(im,y,s,size=48,kind='sans',color=PAPER):text(im,(W/2,y),s,size,kind,color,'mt')
def line(im,points,color=PAPER,width=2):ImageDraw.Draw(im).line(points,fill=color,width=width,joint='curve')
def cropmarks(im,box,color=PAPER,length=26,width=2,offset=12):
    x0,y0,x1,y1=box
    for x,sgn in [(x0,-1),(x1,1)]:
        for y,sy in [(y0,-1),(y1,1)]:
            line(im,[(x+sgn*offset,y),(x+sgn*(offset+length),y)],color,width)
            line(im,[(x,y+sy*offset),(x,y+sy*(offset+length))],color,width)
def cursor(im,x,y,scale=1,color=PAPER):
    d=ImageDraw.Draw(im)
    pts=[(x,y),(x+2*scale,y+52*scale),(x+15*scale,y+39*scale),(x+27*scale,y+64*scale),(x+38*scale,y+58*scale),(x+25*scale,y+34*scale),(x+44*scale,y+33*scale)]
    d.polygon(pts,fill=INK)
    d.line(pts+[pts[0]],fill=color,width=max(2,int(3*scale)),joint='curve')
def click(im,x,y,t,at,color=PAPER):
    v=(t-at)/.55
    if 0<=v<=1:
        rr=18+v*47; col=mix(color,INK,v)
        ImageDraw.Draw(im).ellipse((x-rr,y-rr,x+rr,y+rr),outline=col,width=2)
def ring(im,cx,cy,radius,color,width=3):
    ImageDraw.Draw(im).ellipse((cx-radius,cy-radius,cx+radius,cy+radius),outline=color,width=width)

@lru_cache(24)
def artwork(index=0):
    """The recurring original artwork; graphic poster, never a mock app screen."""
    pw,ph=960,1220
    im=Image.new('RGB',(pw,ph),PAPER);d=ImageDraw.Draw(im)
    # A photogram-like circular construction of additive light and ink.
    cx,cy=480+[-9,7,0,13,-5,4][index%6],560+[0,15,-11,4,-14,9][index%6]
    rr=[305,298,320,288,311,302][index%6]
    d.rectangle((54,54,pw-54,ph-54),outline=(212,214,208),width=2)
    for k,col in enumerate([R,G,B]):
        r=rr-k*14
        d.ellipse((cx-r+(k-1)*38,cy-r+(k-1)*16,cx+r+(k-1)*38,cy+r+(k-1)*16),fill=col)
    # Vector geometry forms a single, recognizable piece across every crop.
    d.pieslice((cx-rr,cy-rr,cx+rr,cy+rr),15,190,fill=INK)
    d.ellipse((cx-145,cy-145,cx+145,cy+145),fill=PAPER)
    d.rectangle((cx-30,cy-365,cx+30,cy+365),fill=PAPER)
    for offset in range(-190,191,20):
        d.line((140,cy+offset,815,cy+offset-145),fill=mix(INK,PAPER,.24),width=2)
    d.rectangle((54,899,906,900),fill=INK)
    text(im,(74,82),'AFTERIMAGE',40,'sans',INK,spacing=2)
    text(im,(72,928),'Almost',110,'serif',INK)
    text(im,(72,1032),'there.',110,'serif',INK)
    text(im,(832,1050),f'{index+1:02}',28,'mono',INK)
    # Deterministic, subtle print grain is original procedural texture.
    noise=np.random.default_rng(11).normal(0,1.1,(ph,pw,1))
    a=np.array(im,dtype=np.float32)+noise
    return Image.fromarray(np.clip(a,0,255).astype(np.uint8))

def paste_poster(im,box,index=0,angle=0,alpha=1):
    x0,y0,x1,y1=map(round,box)
    iw,ih=max(1,x1-x0),max(1,y1-y0)
    art=artwork(index).resize((iw,ih),Image.Resampling.LANCZOS)
    if angle:
        art=art.convert('RGBA').rotate(angle,Image.Resampling.BICUBIC,expand=True)
        x0-=round((art.width-iw)/2);y0-=round((art.height-ih)/2)
        if alpha<1:art.putalpha(art.getchannel('A').point(lambda n:round(n*alpha)))
        im.paste(art,(x0,y0),art)
    elif alpha<1:
        lay=Image.new('RGB',im.size,INK);lay.paste(art,(x0,y0));im.paste(Image.blend(im,lay,alpha))
    else:im.paste(art,(x0,y0))

def furniture(im,t,chapter):
    # Graphic-film editorial furniture, not software chrome.
    text(im,(84,116),'LOOKSi  /  A CREATOR’S RITUAL',23,'mono',GRAY)
    text(im,(84,1712),chapter,23,'mono',GRAY)
    for x,col in [(84,R),(103,G),(122,B)]:ImageDraw.Draw(im).ellipse((x,1775,x+11,1786),fill=col)
    line(im,[(84,1753),(996,1753)],(54,56,59),1)
    line(im,[(84,1753),(84+912*min(t/SECONDS,1),1753)],PAPER,2)

def opening(t,variant):
    im=Image.new('RGB',(W,H),INK); furniture(im,t,'01   THE DECLARATION')
    words=HOOKS[variant]
    sz=132 if variant=='hero' else 112
    for i,w in enumerate(words):
        v=1 if i==0 else enter(t,.03,.65)
        text(im,(82,390+i*160+(1-v)*100),w,sz,'sans',mix(INK,PAPER,v))
    # The word "last" receives a hand-drawn correction, in the film's own language.
    v=enter(t,1.3,.45)
    if v:
        yy=748
        xend=round(84+455*v)
        line(im,[(84,yy),(xend,yy-18)],R,7)
        text(im,(612+(1-v)*60,734),'Again.',107,'italic',mix(INK,R,v))
    p=enter(t,1.85,.55)
    paste_poster(im,(round(695+(1-p)*240),round(1028+(1-p)*80),round(1010+(1-p)*240),round(1435+(1-p)*80)),0,-8, p)
    text(im,(84,1115),'ONE FINAL',32,'mono',GRAY)
    text(im,(84,1165),'TINY CHANGE.',32,'mono',GRAY)
    x=890-340*enter(t,1.9,.7);y=1450-500*enter(t,1.9,.7)
    if t>1.8:cursor(im,x,y,.75)
    click(im,x,y,t,2.6,R)
    return im

def revisions(t):
    im=Image.new('RGB',(W,H),INK);furniture(im,t,'02   THE NEGOTIATION')
    u=t-3
    q=enter(u,0,.65)
    box=(220-30*q,490-90*q,860+30*q,1310+30*q)
    if u<1.4:dx=-14*enter(u,.7,.5); note='a little left.'
    elif u<2.6:dx=-14+26*enter(u,1.4,.5);note='a little right.'
    else:dx=12-11*enter(u,2.6,.6);note='maybe one pixel.'
    box=tuple(v+dx if i%2==0 else v for i,v in enumerate(box))
    paste_poster(im,box,0)
    cropmarks(im,box,(183,188,188),26)
    # Registration crosses expose the small nudge visually.
    line(im,[(540,310),(540,1470)],(43,46,50),1)
    center(im,220,'a little left.' if u<1.4 else note,76,'italic',PAPER)
    text(im,(84,1465),'THE LAST TWEAK',29,'mono',GRAY)
    value='−14 px' if u<1.4 else '+12 px' if u<2.6 else '+01 px'
    text(im,(700,1460),value,40,'mono',R if u<2.6 else G)
    x=790+dx;y=1210
    cursor(im,x,y,.9)
    for at in [3.7,4.4,5.6]:click(im,x,y,t,at,G)
    return im

def crop_choice(t):
    im=Image.new('RGB',(W,H),INK);furniture(im,t,'03   THE ALTERNATIVES')
    u=t-7
    center(im,240,'or this?',102,'serif',PAPER)
    # The evolving window crops the same original graphic without mimicking a product.
    art=artwork(0)
    z=1+.24*smooth(u/2.2)
    aw,ah=round(740*z),round(940*z)
    art=art.resize((aw,ah),Image.Resampling.LANCZOS)
    x=round((aw-740)*(.5+.22*math.sin(u*.9)))
    y=round((ah-940)*(.5+.19*math.cos(u*.8)))
    art=art.crop((x,y,x+740,y+940))
    im.paste(art,(170,490))
    # Visible crop bars tighten in fractional time.
    c=round(44*smooth(u/3.6))
    ImageDraw.Draw(im).rectangle((170,490,910,490+c),fill=INK)
    ImageDraw.Draw(im).rectangle((170,1430-c,910,1430),fill=INK)
    cropmarks(im,(170,490+c,910,1430-c),PAPER)
    if u>1.7:
        v=enter(u,1.7,.4)
        text(im,(88,1480+(1-v)*40),'or… this?',70,'italic',mix(INK,R,v))
        line(im,[(700,1550),(884,1495),(900,1440)],R,3)
    cursor(im,888-88*smooth(u/3.5),1420-85*smooth(u/3.5),.85)
    click(im,800,1335,t,9.5,R)
    return im

def contact_sheet(t):
    im=Image.new('RGB',(W,H),INK);furniture(im,t,'04   A FEW OPTIONS')
    u=t-11
    center(im,240,'a few options.',89,'serif',PAPER)
    for i in range(6):
        v=enter(u,i*.12,.7)
        x=108+(i%3)*298;y=488+(i//3)*475
        bh=352
        # Individual sheets spring into place with slightly different rotations.
        off=(1-v)*200
        paste_poster(im,(x,y+off,x+267,y+bh+off),i,angle=(1-v)*(-5 if i%2 else 5),alpha=v)
        if v>.98:text(im,(x,y+373),f'ALMOST {i+1:02}',22,'mono',GRAY)
    v=enter(u,1.2,.5)
    if v:
        # An elliptical pencil circle picks option 04, drawn along its path.
        points=[]
        for a in np.linspace(-1.5,-1.5+math.pi*2*v,80):
            points.append((240+156*math.cos(a),1139+230*math.sin(a)))
        if len(points)>1:line(im,points,R,5)
    cursor(im,922-630*enter(u,.3,1.2),1440-290*enter(u,.3,1.2),.8)
    p=enter(u,1.25,.4)
    text(im,(108,1500+(1-p)*25),'Yes, they’re different.',51,'italic',mix(INK,R,p))
    return im

def breath(t):
    im=Image.new('RGB',(W,H),INK);furniture(im,t,'05   THE PAUSE')
    u=t-14
    v=enter(u,0,1.15)
    # Scale the selected poster down slowly; space is the punchline's inhale.
    pw=round(610-56*v);ph=round(pw*1220/960)
    x=round((W-pw)/2); y=round(510+20*v)
    paste_poster(im,(x,y,x+pw,y+ph),3)
    center(im,270,'Maybe that’s it.',86,'serif',PAPER)
    q=enter(u,1.3,.5)
    text(im,(290,1390),'…',96,'serif',mix(INK,GRAY,q))
    return im

def relapse(t):
    im=Image.new('RGB',(W,H),INK);furniture(im,t,'06   THE LAST TWEAK. AGAIN.')
    u=t-16
    v=enter(u,.2,.65)
    # The poster holds almost perfectly still; the authored one-pixel nudge is literal.
    dx=round(1*enter(u,1.03,.06))
    paste_poster(im,(263+dx,530,817+dx,1234),3)
    center(im,260,'One more pixel.',100,'italic',R)
    cursor(im,850-65*v,1310-130*v,.8)
    click(im,785,1180,t,17.05,R)
    text(im,(84,1480),'YOU GET IT.',29,'mono',GRAY)
    return im

def ending(t):
    im=Image.new('RGB',(W,H),INK)
    u=t-18
    furniture(im,t,'A TOOL I’M BUILDING. A CONVERSATION TO START.')
    v=enter(u,0,.8)
    text(im,(84,300+(1-v)*70),'I’m building',72,'sans',mix(INK,PAPER,v))
    # RGB registration resolves to one white wordmark as a graphic treatment.
    dx=(1-v)*34
    text(im,(79-dx,405),'LOOKSi.',174,'sans',mix(INK,R,v))
    text(im,(79+dx,405),'LOOKSi.',174,'sans',mix(INK,B,v))
    text(im,(79,405),'LOOKSi.',174,'sans',mix(INK,PAPER,v))
    q=enter(u,.45,.7)
    text(im,(84,645+(1-q)*35),'Photo + video.',66,'sans',mix(INK,PAPER,q))
    for i,s in enumerate(['For people who care','how it looks.']):
        p=enter(u,.8+i*.15,.7)
        text(im,(84,810+i*93+(1-p)*30),s,75,'serif',mix(INK,PAPER,p))
    line(im,[(84,1090),(996,1090)],(60,63,66),2)
    z=enter(u,1.85,.6)
    text(im,(84,1190+(1-z)*28),'What’s your',68,'sans',mix(INK,PAPER,z))
    text(im,(84,1280+(1-z)*28),'“last tweak”?',86,'italic',mix(INK,R,z))
    text(im,(84,1510),'IN DEVELOPMENT  /  BUILDER INVITATION',22,'mono',GRAY)
    for i,col in enumerate([R,G,B]):
        ring(im,930-i*23,1450,32,col,3)
    return im

def scene(t,variant='hero'):
    if t<3:return opening(t,variant)
    if t<7:return revisions(t)
    if t<11:return crop_choice(t)
    if t<14:return contact_sheet(t)
    if t<16:return breath(t)
    if t<18:return relapse(t)
    return ending(t)

def frame(t,variant='hero'):
    """Short optical dissolves retain continuity across the narrative chapters."""
    im=scene(t,variant)
    for cut in [3,7,11,14,16,18]:
        if cut<=t<cut+.22:
            prev=scene(cut-.001,variant)
            im=Image.blend(prev,im,smooth((t-cut)/.22))
    return im

def make_audio():
    sr=48000;n=sr*SECONDS
    mixx=np.zeros((n,2),np.float64)
    rng=np.random.default_rng(411)
    def add(at,x,pan=0):
        start=round(at*sr);length=min(len(x),n-start)
        gains=np.array([math.sqrt((1-pan)/2),math.sqrt((1+pan)/2)])
        if length>0:mixx[start:start+length]+=x[:length,None]*gains
    # Quiet sustained original chord. The pause removes it before the relapse.
    tt=np.arange(n)/sr
    ambient=np.zeros(n)
    for hz,amp in [(110,.017),(164.81,.012),(220,.008),(329.63,.006)]:
        ambient+=amp*np.sin(2*np.pi*hz*tt)*(1+.15*np.sin(2*np.pi*.09*tt))
    gate=np.clip(tt/1.3,0,1)*np.clip((24-tt)/1.1,0,1)
    gate*=1-.85*np.exp(-((tt-15.2)/.7)**4)
    add(0,ambient*gate)
    # Glassy two-tone plucks move from question to a lightly resolved cadence.
    for at,hz in [(0.15,329.63),(1.36,392),(3.1,293.66),(7.1,349.23),(11.1,392),(14.15,440),(18.15,329.63),(20.0,493.88)]:
        x=np.arange(round(sr*1.5))/sr
        wavey=(np.sin(2*np.pi*hz*x)+.32*np.sin(2*np.pi*hz*2.003*x))
        env=(1-np.exp(-x*90))*np.exp(-x*3.7)
        add(at,wavey*env*.11,pan=.15*math.sin(at))
    # Crisp click: a tiny noise transient plus a downward pitched tap.
    for i,at in enumerate([2.6,3.7,4.4,5.6,9.5,11.05,11.2,11.35,11.5,11.65,11.8,17.05]):
        x=np.arange(round(sr*.09))/sr
        noise=rng.normal(0,1,len(x))
        sig=(noise*.07*np.exp(-x*100)+.11*np.sin(2*np.pi*(1450*x-3900*x*x))*np.exp(-x*70))
        add(at,sig,pan=(-.35 if i%2 else .35))
    # Short airy paper-like sweeps at crop and contact-sheet transitions.
    for at in [6.9,10.9,13.9,17.9]:
        x=np.arange(round(sr*.45))/sr
        ns=rng.normal(0,1,len(x));lp=np.convolve(ns,np.ones(18)/18,'same')
        env=np.sin(np.pi*x/.45)**2
        add(at,lp*env*.085,pan=-.2)
    peak=np.max(np.abs(mixx));mixx*=.78/max(peak,1e-8)
    pcm=(np.clip(mixx,-1,1)*32767).astype('<i2')
    source=ROOT/'audio/original-sound-design-source.wav'
    with wave.open(str(source),'wb') as f:
        f.setnchannels(2);f.setsampwidth(2);f.setframerate(sr);f.writeframes(pcm.tobytes())
    # Master to a restrained social-film level; retain pre-master for future revisions.
    analysis=subprocess.run(['ffmpeg','-hide_banner','-i',str(source),'-af','loudnorm=I=-18:TP=-1.5:LRA=8:print_format=json','-f','null','-'],capture_output=True,text=True,check=True)
    measured=json.loads(analysis.stderr[analysis.stderr.rfind('{'):analysis.stderr.rfind('}')+1])
    af=(f'loudnorm=I=-18:TP=-1.5:LRA=8:measured_I={measured["input_i"]}:measured_TP={measured["input_tp"]}'
        f':measured_LRA={measured["input_lra"]}:measured_thresh={measured["input_thresh"]}:offset={measured["target_offset"]}:linear=false')
    wav=ROOT/'audio/original-sound-design.wav'
    subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-y','-i',str(source),'-af',af+',alimiter=limit=0.79:level=false','-ar','48000','-c:a','pcm_s16le',str(wav)],check=True)
    (ROOT/'audio/mastering-analysis.json').write_text(json.dumps({'target_integrated_lufs':-18,'true_peak_target_dbtp':-1.5,'source_measurement':measured,'process':'Two-pass FFmpeg loudnorm followed by a 0.79-amplitude limiter with auto-level disabled. This is mix normalization, not a platform guarantee. See QA for measured final values.'},indent=2))
    return wav

def render(variant='hero',start=0,end=SECONDS,output=None):
    out=output or ROOT/f'videos/{variant}.mp4'
    cmd=['ffmpeg','-hide_banner','-loglevel','error','-y','-f','rawvideo','-vcodec','rawvideo','-pix_fmt','rgb24','-s',f'{W}x{H}','-r',str(FPS),'-i','-', '-ss',str(start),'-i',str(ROOT/'audio/original-sound-design.wav'),'-map','0:v','-map','1:a','-t',str(end-start),'-vf',VIDEO_COLOR_FILTER,'-c:v','libx264','-preset','medium','-crf','18','-pix_fmt','yuv420p',*VIDEO_COLOR_ARGS,'-c:a','aac','-b:a','192k','-movflags','+faststart',str(out)]
    p=subprocess.Popen(cmd,stdin=subprocess.PIPE)
    for i in range(round(start*FPS),round(end*FPS)):
        p.stdin.write(frame(i/FPS,variant).tobytes())
        if i%96==0:print(f'{variant}: frame {i}/{round(end*FPS)}',flush=True)
    p.stdin.close()
    if p.wait():raise RuntimeError('ffmpeg encoding failed')
    return out

def svg_fonts():
    definitions=[]
    for kind in ['sans','serif','italic','mono']:
        encoded=base64.b64encode(FONT[kind].read_bytes()).decode()
        definitions.append(f'@font-face{{font-family:Campaign{kind};src:url(data:font/ttf;base64,{encoded}) format("truetype");font-weight:{"100 900" if kind=="sans" else "400"};}}')
    return '<style>'+''.join(definitions)+'</style>'

def svg_text(x,y,s,size=48,kind='sans',color='#f3f3ed',anchor='start'):
    # Match Pillow's default ascender anchor while retaining editable SVG text.
    baseline=y+font(size,kind).getmetrics()[0]
    return f'<text x="{x}" y="{baseline}" fill="{color}" font-family="Campaign{kind}" font-weight="{"600" if kind=="sans" else "400"}" font-size="{size}" text-anchor="{anchor}">{html.escape(s)}</text>'

def svg_artwork(index=3,x=325,y=545,width=430,height=547):
    cx,cy=480+[-9,7,0,13,-5,4][index%6],560+[0,15,-11,4,-14,9][index%6]
    rr=[305,298,320,288,311,302][index%6]
    out=[f'<g id="original-editable-poster" transform="translate({x} {y}) scale({width/960} {height/1220})">', '<rect width="960" height="1220" fill="#f3f3ed"/>', '<rect x="54" y="54" width="852" height="1112" fill="none" stroke="#d4d6d0" stroke-width="2"/>']
    for k,col in enumerate(['#ff3b30','#30d158','#3282ff']):
        out.append(f'<circle cx="{cx+(k-1)*38}" cy="{cy+(k-1)*16}" r="{rr-k*14}" fill="{col}"/>')
    ax,ay=cx+rr*math.cos(math.radians(15)),cy+rr*math.sin(math.radians(15))
    bx,by=cx+rr*math.cos(math.radians(190)),cy+rr*math.sin(math.radians(190))
    out.append(f'<path d="M{cx} {cy} L{ax} {ay} A{rr} {rr} 0 0 1 {bx} {by} Z" fill="#07080a"/>')
    out.append(f'<circle cx="{cx}" cy="{cy}" r="145" fill="#f3f3ed"/><rect x="{cx-30}" y="{cy-365}" width="60" height="730" fill="#f3f3ed"/>')
    for offset in range(-190,191,20):out.append(f'<path d="M140 {cy+offset} L815 {cy+offset-145}" stroke="#404143" stroke-width="2"/>')
    out.append('<path d="M54 899 L906 899" stroke="#07080a" stroke-width="2"/>')
    out.append(svg_text(74,82,'AFTERIMAGE',40,'sans','#07080a'))
    out.append(svg_text(72,928,'Almost',110,'serif','#07080a'))
    out.append(svg_text(72,1032,'there.',110,'serif','#07080a'))
    out.append(svg_text(832,1050,f'{index+1:02}',28,'mono','#07080a'))
    out.append('</g>')
    return '\n'.join(out)

def cover():
    im=Image.new('RGB',(1080,1350),INK)
    text(im,(72,60),'LOOKSi  /  PHOTO + VIDEO',22,'mono',GRAY)
    text(im,(68,148),'The last',123,'sans',PAPER)
    text(im,(68,288),'tweak.',123,'sans',PAPER)
    line(im,[(77,448),(570,428)],R,7)
    text(im,(596,397),'Again.',100,'italic',R)
    paste_poster(im,(325,545,755,1092),3)
    cropmarks(im,(325,545,755,1092),(125,131,134),20)
    cursor(im,741,944,.8)
    center(im,1153,'What’s your “last tweak”?',68,'italic',PAPER)
    center(im,1272,'I’m building LOOKSi. Photo + video.',27,'mono',GRAY)
    im.save(ROOT/'images/cover.png',icc_profile=SRGB_PROFILE)
    im.resize((360,450),Image.Resampling.LANCZOS).save(ROOT/'qa/cover-phone-360.png')
    # Native vector poster, editable type and explicitly embedded font software.
    art=artwork(3);art.save(ROOT/'images/original-artwork.png',icc_profile=SRGB_PROFILE)
    svg=['<svg xmlns="http://www.w3.org/2000/svg" width="1080" height="1350" viewBox="0 0 1080 1350">', '<title>The last tweak. Again. LOOKSi campaign cover</title>',svg_fonts(),'<rect width="1080" height="1350" fill="#07080a"/>']
    svg.extend([svg_text(72,60,'LOOKSi / PHOTO + VIDEO',22,'mono','#919596'),svg_text(68,148,'The last',123),svg_text(68,288,'tweak.',123),'<path d="M77 448 L570 428" stroke="#ff3b30" stroke-width="7"/>',svg_text(596,397,'Again.',100,'italic','#ff3b30'),svg_artwork()])
    for xx,ss in [(325,-1),(755,1)]:
        for yy,sy in [(545,-1),(1092,1)]:
            svg.append(f'<path d="M{xx+ss*12} {yy} h{ss*20} M{xx} {yy+sy*12} v{sy*20}" stroke="#7d8386" stroke-width="2"/>')
    svg.append('<path d="M741 944 L742.6 985.6 L753 975.2 L762.6 995.2 L771.4 990.4 L761 971.2 L776.2 970.4 Z" fill="#07080a" stroke="#f3f3ed" stroke-width="2"/>')
    svg.extend([svg_text(540,1153,'What’s your “last tweak”?',68,'italic',anchor='middle'),svg_text(540,1272,'I’m building LOOKSi. Photo + video.',27,'mono','#919596','middle'),'</svg>'])
    (ROOT/'images/cover.svg').write_text('\n'.join(svg))

def storyboard():
    moments=[(1.8,'00–03','The declaration','A confident “last tweak” gets an immediate correction.'),
             (5.9,'03–11','The negotiation','One poster nudges left, then right. The crop keeps changing.'),
             (12.7,'11–14','The alternatives','Six near-identical choices. One gets circled.'),
             (17.2,'14–18','The relapse','A breath. Then a literal one-pixel nudge.'),
             (22.8,'18–24','The invitation','Builder disclosure and one question for fellow creators.')]
    board=Image.new('RGB',(2400,1500),(17,19,22));d=ImageDraw.Draw(board)
    text(board,(72,58),'04  /  THE LAST TWEAK. AGAIN.',59,'sans',PAPER)
    text(board,(72,139),'24-second original graphic film. Relatable perfectionism. Independent builder invitation.',26,'mono',GRAY)
    embedded=[]
    for i,(ts,timing,title,desc) in enumerate(moments):
        shot=frame(ts);p=ROOT/f'storyboard/panel-{i+1:02}.png';shot.save(p,icc_profile=SRGB_PROFILE)
        preview=shot.resize((410,729),Image.Resampling.LANCZOS)
        xx=72+i*465;yy=255
        board.paste(preview,(xx,yy));d.rectangle((xx,yy,xx+410,yy+729),outline=(69,73,79),width=1)
        text(board,(xx,1022),timing,25,'mono',R)
        text(board,(xx,1072),title,34,'sans',PAPER)
        words=desc.split();lines=[];current=''
        for word in words:
            test=(current+' '+word).strip()
            if d.textlength(test,font=font(26,'sans'))>408:lines.append(current);current=word
            else:current=test
        if current:lines.append(current)
        for j,s in enumerate(lines):text(board,(xx,1135+j*38),s,26,'sans',GRAY)
        embedded.append((xx,yy,base64.b64encode(p.read_bytes()).decode(),timing,title,lines))
    text(board,(72,1380),'Original graphic artwork + procedural sound. No product UI or feature demonstration. No filming required.',26,'mono',GRAY)
    board.save(ROOT/'storyboard/board.png',icc_profile=SRGB_PROFILE)
    svg=['<svg xmlns="http://www.w3.org/2000/svg" width="2400" height="1500" viewBox="0 0 2400 1500">','<rect width="2400" height="1500" fill="#111316"/>','<g font-family="Space Grotesk" fill="#f3f3ed">', '<text x="72" y="110" font-size="59">04 / THE LAST TWEAK. AGAIN.</text>','<text x="72" y="165" font-size="26" fill="#919596">24-second original graphic film. Relatable perfectionism. Independent builder invitation.</text>']
    for xx,yy,b64,timing,title,lines in embedded:
        svg.append(f'<image x="{xx}" y="{yy}" width="410" height="729" href="data:image/png;base64,{b64}"/>')
        svg.append(f'<text x="{xx}" y="1048" font-size="25" fill="#ff3b30">{html.escape(timing)}</text><text x="{xx}" y="1107" font-size="34">{html.escape(title)}</text>')
        for j,s in enumerate(lines):svg.append(f'<text x="{xx}" y="{1162+j*38}" font-size="26" fill="#919596">{html.escape(s)}</text>')
    svg.append('<text x="72" y="1410" font-size="26" fill="#919596">Original graphic artwork + procedural sound. No product UI or feature demonstration. No filming required.</text></g></svg>')
    (ROOT/'storyboard/board.svg').write_text('\n'.join(svg))

def metadata():
    timing=[{'start':0,'end':3,'scene':'The declaration','action':'Type rises. Red strike and Again appear. Cursor approaches.'},
            {'start':3,'end':7,'scene':'The negotiation','action':'Original artwork shifts left, right, then by one pixel. Registration line remains still.'},
            {'start':7,'end':11,'scene':'The crop','action':'Same artwork zooms and changes crop. A second thought appears as annotation.'},
            {'start':11,'end':14,'scene':'The alternatives','action':'Six original layout variants cascade in. A red circle selects number 04.'},
            {'start':14,'end':16,'scene':'The pause','action':'Selected artwork holds. Music thins. Maybe that’s it.'},
            {'start':16,'end':18,'scene':'The relapse','action':'One more pixel. Cursor click nudges the artwork literally one output pixel.'},
            {'start':18,'end':24,'scene':'The invitation','action':'Maker disclosure, photo + video, care-for-the-work message and question. All on screen by 20.5s.'}]
    (ROOT/'source/timing.json').write_text(json.dumps({'fps':FPS,'seconds':SECONDS,'size':[W,H],'hooks':HOOKS,'scenes':timing},indent=2,ensure_ascii=False))
    (ROOT/'source/provenance.json').write_text(json.dumps({'campaign':'04 The last tweak. Again.','created':'2026-11-16','artwork':'Original procedural vector-inspired artwork authored in make_campaign.py. No stock imagery or product UI. The recurring poster is an original campaign graphic, not output shown as made in LOOKSi.','fonts':{k:str(v.relative_to(ROOT) if v.is_relative_to(ROOT) else v.relative_to(REPO)) for k,v in FONT.items()},'font_licenses':'SIL Open Font License files are bundled beside the source fonts.','audio':'Original synthesized stereo sound design from sine oscillators, deterministic noise, envelopes and authored event timing. No recordings, samples, music library or voice likeness.','product_claims':'Builder intent and category only. This film makes no claim about version management, time saving, app actions, output fidelity or availability.','filming_required':False,'outputs':'Hero plus two opening-hook variations use continuous full-length encodes and the same narrative after their optical opening dissolve. Sound-on and silent masters supplied.','name_status':'Campaign use of LOOKSi; app identity is unchanged.'},indent=2))
    transcript=ROOT/'source/on-screen-text.srt'
    if transcript.exists():
        base=transcript.read_text()
        for variant,opening in [('hero','The last tweak.'),('hook-b','Just one more pixel.'),('hook-c','You said it was done.')]:
            caption=base.replace('The last tweak.\nAgain.',opening+'\nAgain.',1)
            stem='on-screen-text' if variant=='hero' else variant+'-on-screen-text'
            (ROOT/f'source/{stem}.srt').write_text(caption)
            (ROOT/f'source/{stem}.vtt').write_text('WEBVTT\n\nNOTE On-screen text transcript. No spoken voice.\n\n'+caption.replace(',','.'))

def bundle_and_outline():
    destination=ROOT/'source/fonts'
    for source in FONT.values():
        target=destination/source.parent.name/source.name
        target.parent.mkdir(parents=True,exist_ok=True)
        if source.resolve()!=target.resolve():shutil.copy2(source,target)
        license_source=source.parent/'OFL.txt';license_target=target.parent/'OFL.txt'
        if license_source.exists() and license_source.resolve()!=license_target.resolve():shutil.copy2(license_source,license_target)
    if sys.platform!='darwin':return
    binary=Path(tempfile.gettempdir())/'looksi-campaign04-outline'
    subprocess.run(['clang','-fobjc-arc','-fblocks','-framework','Foundation','-framework','CoreText','-framework','CoreGraphics',str(ROOT/'source/outline_svg.m'),'-o',str(binary)],check=True)
    for target in [ROOT/'images/cover.svg',ROOT/'storyboard/board.svg']:
        live=target.with_name(target.stem+'-editable-text.svg')
        shutil.copy2(target,live)
        subprocess.run([str(binary),str(live),str(target),str(destination)],check=True)

def main():
    p=argparse.ArgumentParser();p.add_argument('--render',choices=['all','hero','hooks','stills'],default='all');a=p.parse_args()
    make_audio();metadata();cover();storyboard();bundle_and_outline()
    if a.render in ['all','hero']:render('hero')
    if a.render in ['all','hooks']:
        # Continuous full-length encodes avoid AAC priming gaps and preserve the
        # correct optical dissolve from each variant's unique opening.
        for hook in ['hook-b','hook-c']:
            render(hook)
    if a.render!='stills':
        for name in ['hero','hook-b','hook-c']:
            source=ROOT/f'videos/{name}.mp4'
            if source.exists():subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-y','-i',str(source),'-map','0:v:0','-c:v','copy','-an','-movflags','+faststart',str(ROOT/f'videos/{name}-silent.mp4')],check=True)
    for t in [0,1.8,3.7,5.9,8.5,10.2,12.7,15.2,17.2,19.2,21.2,23.5]:frame(t).save(ROOT/f'qa/frame-{t:04.1f}.png')
    print('Done',flush=True)
if __name__=='__main__':main()
