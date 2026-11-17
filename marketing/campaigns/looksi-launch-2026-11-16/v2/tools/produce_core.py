#!/usr/bin/env python3
"""Direct three campaign executions from verified native outputs and original graphics.

No app screen or photographic response is reconstructed. Comparison endpoints
are real native exports. Archived UI/video outputs retain visible dates and scope.
"""
from pathlib import Path
import argparse,base64,html,json,math,subprocess,wave,re
from functools import lru_cache
import numpy as np
from PIL import Image,ImageDraw,ImageFont,ImageOps,ImageFilter,ImageCms

ROOT=Path(__file__).resolve().parents[1];REPO=ROOT.parents[3]
FONTROOT=ROOT/'fonts'
if not FONTROOT.exists():FONTROOT=ROOT.parent/'artwork/fonts'
INK='#080A0C';PAPER='#F3F3EE';RED='#FF3B30';GREEN='#30D158';BLUE='#3282FF';MUTED='#8E969C'
FONTS={'sans':FONTROOT/'spacegrotesk/SpaceGrotesk[wght].ttf','serif':FONTROOT/'instrumentserif/InstrumentSerif.ttf','italic':FONTROOT/'instrumentserif/InstrumentSerif-Italic.ttf','mono':FONTROOT/'spacemono/SpaceMono-Regular.ttf','heavy':FONTROOT/'archivoblack/ArchivoBlack-Regular.ttf'}
PRE=ROOT/'media/previews';FRAMES=ROOT/'media/frames'
BEFORE=PRE/'native-photo-baseline.jpg';AFTER=PRE/'native-photo-finish.jpg';FLOWER=PRE/'IMG_4870.jpg';BOATS=PRE/'IMG_4969.jpg';DAWN=PRE/'IMG_5077.jpg'
BOARDS=[PRE/f'nepal-near-far-{i:02d}.jpg'for i in range(1,7)]
FPS=30
ICC=ImageCms.ImageCmsProfile(ImageCms.createProfile('sRGB')).tobytes()
SPEC={
 '02':dict(title='NEPAL. In three forms.',role='The professional casebook',bg=INK,fg=PAPER,accent=GREEN,duration=42,
      hook=['NEPAL.','In three forms.'],sub='A photo. A motion study. A story in six boards.',
      scenes=[(0,4,'The project'),(4,11,'A photo treatment'),(11,18,'The native motion study'),(18,25,'Six boards, one story'),(25,33,'Let the files speak'),(33,42,'The builder’s invitation')]),
 '03':dict(title='Keep the gold. Keep the quiet blues.',role='A welcoming recipe breakdown',bg='#EDF0EA',fg=INK,accent=BLUE,duration=33,
      hook=['Keep the gold.','Keep the quiet blues.'],sub='Three small choices for this photograph.',
      scenes=[(0,4,'Show the chosen treatment'),(4,9,'Look before touching'),(9,17,'Three deliberate choices'),(17,24,'Compare the combined result'),(24,29,'Keep the file'),(29,33,'Invite the first question')]),
 '05':dict(title='Make room for your eye.',role='The photographic manifesto',bg=INK,fg=PAPER,accent=BLUE,duration=36,
      hook=['Make room','for your eye.'],sub='Photo + video. A tool I’m building around the work.',
      scenes=[(0,4,'A way of seeing'),(4,9,'The light you noticed'),(9,15,'Color and feeling'),(15,21,'The product moment'),(21,28,'The work takes shape'),(28,36,'A personal signature')]),
}

@lru_cache(maxsize=256)
def font(size,kind='sans'):
    f=ImageFont.truetype(str(FONTS[kind]),round(size))
    if kind=='sans':
        try:f.set_variation_by_axes([550])
        except Exception:pass
    return f
@lru_cache(maxsize=200)
def photo(path):return Image.open(path).convert('RGB')
@lru_cache(maxsize=200)
def fitted(path,w,h):return ImageOps.fit(photo(path),(round(w),round(h)),method=Image.Resampling.LANCZOS)
def ease(x):return 1-(1-max(0,min(1,x)))**3
def smooth(x):x=max(0,min(1,x));return x*x*(3-2*x)
def fit(texts,size,width,kind='sans'):
    while max(font(size,kind).getlength(x)for x in texts)>width:size-=1
    return size
def wrap(text,size,width,kind='sans'):
    out=[];line=''
    for word in text.split():
        trial=(line+' '+word).strip()
        if font(size,kind).getlength(trial)>width and line:out.append(line);line=word
        else:line=trial
    if line:out.append(line)
    return out

def public_copy(s):
    s=re.sub(r'(?<=[A-Za-z])(?=\d)', ' ',s)
    s=re.sub(r'(?<=\d)(?=[A-Za-z])', ' ',s)
    s=re.sub(r'(?<=[,:;])(?=\S)', ' ',s)
    s=re.sub(r'(?<=[.!?])(?=[A-Za-z])', ' ',s)
    return re.sub(r'(?<=%)(?=[A-Za-z])',' ',s)

class C:
    def __init__(self,w,h,bg,svg=False):
        self.w,self.h=w,h;self.im=Image.new('RGB',(w,h),bg);self.d=ImageDraw.Draw(self.im);self.svg=svg;self.parts=[f'<rect width="{w}" height="{h}" fill="{bg}"/>'];self.clips=0
    def rect(self,box,color,stroke=None,width=1):
        x,y,x2,y2=map(round,box);self.d.rectangle((x,y,x2,y2),fill=color,outline=stroke,width=width)
        if self.svg:self.parts.append(f'<rect x="{x}" y="{y}" width="{x2-x}" height="{y2-y}" fill="{color or "none"}" stroke="{stroke or "none"}" stroke-width="{width}"/>')
    def text(self,xy,text,size,fill,kind='sans'):
        x,y=map(round,xy);f=font(size,kind)
        if x+f.getlength(text)>self.w-5:raise ValueError(f'Overflow {text}')
        self.d.text((x,y),text,font=f,fill=fill,anchor='lt')
        if self.svg:self.parts.append(f'<text x="{x}" y="{y}" font-family="{kind}" font-weight="{550 if kind=="sans"else 400}" font-size="{round(size)}" dominant-baseline="text-before-edge" fill="{fill}">{html.escape(text)}</text>')
    def lines(self,texts,x,y,size,fill,kind='sans',leading=1.1):
        for j,text in enumerate(texts):self.text((x,y+j*size*leading),text,size,fill,kind)
    def line(self,xy,fill,width=1):
        self.d.line(tuple(round(v)for v in xy),fill=fill,width=width)
        if self.svg:self.parts.append(f'<polyline points="{" ".join(f"{xy[i]},{xy[i+1]}"for i in range(0,len(xy),2))}" stroke="{fill}" stroke-width="{width}" fill="none"/>')
    def circle(self,x,y,r,color):
        self.d.ellipse((x-r,y-r,x+r,y+r),fill=color)
        if self.svg:self.parts.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="{color}"/>')
    def image(self,path,box,contain=False,outline=None):
        x,y,x2,y2=map(round,box);w,h=x2-x,y2-y
        if contain:
            im=ImageOps.contain(photo(str(path)),(w,h),Image.Resampling.LANCZOS);px=x+(w-im.width)//2;py=y+(h-im.height)//2;self.im.paste(im,(px,py))
        else:
            im=fitted(str(path),w,h);self.im.paste(im,(x,y))
        self.d=ImageDraw.Draw(self.im)
        if self.svg:
            suffix=Path(path).suffix.lower();mime='image/jpeg'if suffix in['.jpg','.jpeg']else'image/png';b64=base64.b64encode(Path(path).read_bytes()).decode()
            self.parts.append(f'<svg x="{x}" y="{y}" width="{w}" height="{h}" viewBox="0 0 {w} {h}"><image width="{w}" height="{h}" preserveAspectRatio="xMidYMid {"meet"if contain else"slice"}" href="data:{mime};base64,{b64}"/></svg>')
        if outline:self.rect(box,None,outline,2)
    def save(self,stem):
        stem.parent.mkdir(parents=True,exist_ok=True);self.im.save(stem.with_suffix('.png'),icc_profile=ICC)
        if self.svg:
            faces=''.join(f'@font-face{{font-family:{k};src:url(data:font/ttf;base64,{base64.b64encode(v.read_bytes()).decode()});font-weight:{"100 900"if k=="sans"else"400"};}}'for k,v in FONTS.items())
            stem.with_suffix('.svg').write_text(f'<svg xmlns="http://www.w3.org/2000/svg" width="{self.w}" height="{self.h}" viewBox="0 0 {self.w} {self.h}"><style>{faces}</style>{"".join(self.parts)}</svg>')

def brand(c,x=70,y=110,fg=PAPER,size=32):
    c.text((x,y),'LOOKSi',size,fg)
    for j,col in enumerate([RED,GREEN,BLUE]):c.circle(x+size*4.25+j*12,y+size*.5,5,col)
def footer(c,p,number,fg=None):
    fg=fg or p['fg'];c.line((70,1610,1010,1610),MUTED,1);c.text((70,1642),number,18,fg,'mono');c.text((70,1690),'A PHOTO + VIDEO EDITOR I’M BUILDING',18,fg,'mono')
def paper_frame(c,path,box):
    x,y,x2,y2=box;c.rect((x+12,y+16,x2+12,y2+16),'#D5D6CF');c.rect(box,PAPER);c.image(path,(x+18,y+18,x2-18,y2-18))

def compare(c,box,progress,before=BEFORE,after=AFTER):
    x,y,x2,y2=map(round,box);w,h=x2-x,y2-y
    c.image(before,box)
    endpoint=fitted(str(after),w,h);cut=round(w*max(0,min(1,progress)))
    if cut:
        c.im.paste(endpoint.crop((0,0,cut,h)),(x,y));c.d=ImageDraw.Draw(c.im)
    c.line((x+cut,y,x+cut,y2),PAPER,3)
    c.rect((x+18,y+18,x+170,y+61),INK);c.text((x+31,y+29),'CHOSEN',20,PAPER,'mono')
    c.rect((x2-180,y+18,x2-18,y+61),INK);c.text((x2-167,y+29),'BASELINE',20,PAPER,'mono')

def photons(t):
    # Original low-resolution light pass, composited with native-resolution photos/type.
    yy,xx=np.mgrid[-1.8:1.8:480j,-1:1:270j]
    channels=[]
    for i in range(3):
        phase=t*.18+i*.055
        curve=.34*np.sin(yy*1.7+phase)+.19*np.cos(yy*3.3-phase)
        core=np.exp(-((xx-curve-i*.018+.018)/.024)**2)
        glow=np.exp(-((xx-curve-i*.025+.025)/.16)**2)*.12
        light=np.clip((core+glow)*(.45+.3*np.sin(yy*.5+t*.2)**2),0,1)
        channels.append(light)
    data=np.stack(channels,axis=2)
    return Image.fromarray((data**.6*255).astype('uint8')).resize((1080,1920),Image.Resampling.BICUBIC)

def frame02(t,variant='a'):
    p=SPEC['02'];c=C(1080,1920,INK);brand(c);c.text((70,184),'02 / THE PROFESSIONAL CASEBOOK',19,MUTED,'mono')
    if t<4:
        y=315-round(24*ease(t/.7));c.lines(['NEPAL.','In three forms.']if variant=='a'else(['The files.','The finished work.']if variant=='b'else['One project.','Several ways to tell it.']),70,y,fit(['In three forms.'if variant=='a'else'Several ways to tell it.'],114,930),PAPER)
        c.image(AFTER,(70,650,1010,1260));c.image(BOARDS[4],(705,1100,1000,1470),contain=True);c.rect((84,1280,653,1410),'#1C2125');c.text((104,1310),'PHOTO / MOTION / SIX BOARDS',23,PAPER,'mono')
    elif t<11:
        local=t-4;c.text((70,310),'01 / THE PHOTO',25,GREEN,'mono');c.lines(['Small choices.','A considered finish.'],70,375,76,PAPER)
        compare(c,(70,655,1010,1300),.22+.72*smooth(local/5.5))
        c.text((70,1350),'5900 K  /  +12% CONTRAST  /  +0.15 EV',23,PAPER,'mono');c.text((70,1420),'Actual Looksy exports · 16 Nov 2026',25,MUTED)
    elif t<18:
        c.text((70,310),'02 / THE MOTION STUDY',25,GREEN,'mono');c.lines(['A native Timeline','review export.'],70,375,76,PAPER)
        c.image(FRAMES/'native-timeline-output-00.jpg',(40,665,1040,1228),contain=True)
        c.text((70,1290),'12 seconds · 1920 × 1080 · 30 fps',29,PAPER);c.text((70,1355),'Still from the completed export · 2 Nov 2026',25,MUTED)
        c.lines(['The original native review file is included.','This is a motion-design artifact, not live UI.'],70,1420,24,MUTED,leading=1.4)
    elif t<25:
        c.text((70,310),'03 / THE BOARD SET',25,GREEN,'mono');c.lines(['A story.','Room to unfold.'],70,375,86,PAPER)
        local=t-18
        for j,path in enumerate(BOARDS):
            row=j//3;col=j%3;offset=round(22*(1-ease((local-col*.1-row*.15)/.7)))
            x=70+col*320;y=720+row*315+offset;c.image(path,(x,y,x+300,y+240));c.text((x,y+255),f'{j+1:02d} / 06',17,MUTED,'mono')
        c.text((70,1400),'Six native 1920 × 1536 board exports',26,PAPER);c.text((70,1450),'NEPAL / NEAR & FAR · 16 Nov 2026',24,MUTED)
    elif t<33:
        c.lines(['Three related forms.','Actual files to inspect.'],70,330,fit(['Three related forms.','Actual files to inspect.'],77,940),PAPER)
        c.image(AFTER,(70,650,530,1040));c.image(BOARDS[4],(560,650,1010,1210),contain=True);c.image(ROOT/'media/frames/native-timeline-output-00.jpg',(70,1080,530,1340),contain=True)
        c.lines(['Created across sessions while building the tool.','Photo + boards: 16 Nov.  Motion study: 2 Nov.'],70,1405,24,MUTED,leading=1.4)
    else:
        c.text((70,330),'I’m building',65,PAPER);c.text((64,425),'LOOKSi.',160,PAPER)
        c.lines(['For professional projects.','For the work you make for yourself.'],70,675,fit(['For the work you make for yourself.'],50,930),PAPER,leading=1.4)
        c.image(AFTER,(70,900,1010,1310));c.lines(['What would you need to inspect','before trying a new editor?'],70,1390,45,PAPER,leading=1.2)
    footer(c,p,'ACTUAL OUTPUTS / DEVELOPMENT CASEBOOK');return c.im

def frame03(t,variant='a'):
    p=SPEC['03'];c=C(1080,1920,p['bg']);brand(c,fg=INK);c.text((70,184),'03 / THREE CHOICES, ONE PHOTOGRAPH',19,'#54615F','mono')
    if t<4:
        texts=['Keep the gold.','Keep the quiet blues.']if variant=='a'else(['A little warmer.','Still your photograph.']if variant=='b'else['One photo.','Three deliberate choices.'])
        c.lines(texts,70,310,fit(texts,94,935),INK,'serif');paper_frame(c,AFTER,(70,675,1000,1395));c.text((70,1470),'A real treatment exported from Looksy.',29,INK)
    elif t<9:
        c.text((70,300),'FIRST, LOOK.',24,BLUE,'mono');c.lines(['Warm summit light.','Cool, quiet layers.'],70,365,88,INK,'serif');paper_frame(c,BEFORE,(70,680,1000,1370))
        c.line((690,960,870,800),BLUE,3);c.text((730,750),'Keep this warmth.',26,INK);c.line((200,1190,120,1370),BLUE,3);c.text((75,1420),'Keep this depth.',28,INK)
    elif t<17:
        c.text((70,300),'THREE CHOICES FOR THIS PHOTOGRAPH.',24,BLUE,'mono')
        choices=[('Temperature','5900 K','A warmer direction.'),('Contrast','+12%','A little separation.'),('Exposure','+0.15 EV','A modest lift.')]
        active=min(2,int((t-9)/(8/3)));label,value,note=choices[active]
        c.lines(wrap(note,98,930,'serif'),70,365,98,INK,'serif');c.image(AFTER,(70,620,1010,1110))
        c.text((70,1175),label,42,INK);c.text((65,1250),value,126,INK)
        c.text((845,1290),f'0{active+1}/03',27,BLUE,'mono')
        c.line((70,1440,1010,1440),'#AEB8B3',1);c.text((70,1475),'Combined treatment shown throughout.',26,INK)
    elif t<24:
        c.text((70,300),'LOOK AGAIN.',24,BLUE,'mono');c.lines(['The same frame.','A different feeling.'],70,365,88,INK,'serif');compare(c,(70,680,1010,1360),.5+.45*math.sin((t-17)*math.pi/6))
        c.lines(['This comparison shows all three choices together.','The numbers are a starting point for this image.'],70,1420,26,INK,leading=1.4)
    elif t<29:
        c.text((70,300),'KEEP THE RESULT.',24,BLUE,'mono');c.lines(['A photograph you','want to come back to.'],70,365,fit(['A photograph you','want to come back to.'],88,930,'serif'),INK,'serif')
        paper_frame(c,AFTER,(70,680,1000,1370));c.text((70,1420),'1800 × 1200 native PNG export · sRGB',25,INK);c.text((70,1470),'Baseline and chosen file included for review.',24,'#54615F')
    else:
        c.lines(['Make the choices','you can see.'],70,330,112,INK,'serif');c.image(AFTER,(70,680,1010,1265));c.text((70,1340),'I’m building LOOKSi. Photo + video.',34,INK);c.lines(['What would you like to learn','to change in your own photos?'],70,1420,43,INK,leading=1.15)
    footer(c,p,'ACTUAL EXPORTS / 16 NOV 2026');return c.im

def frame05(t,variant='a'):
    p=SPEC['05'];c=C(1080,1920,INK)
    if t<4:
        c.image(AFTER,(0,0,1080,1920));c.rect((0,0,1080,600),INK)
        band=round(1000-400*ease(t/3.5));c.rect((0,600,1080,max(600,band)),INK);c.rect((0,1540,1080,1920),INK)
        texts=['Make room','for your eye.']if variant=='a'else(['You saw','something.']if variant=='b'else['The way','you see it.'])
        c.lines(texts,65,255,fit(texts,126,940,'serif'),PAPER,'serif');c.text((70,1660),'A POINT OF VIEW STARTS HERE.',22,PAPER,'mono')
    elif t<9:
        c.image(DAWN,(0,350,1080,1490));c.rect((0,0,1080,580),INK);c.lines(['Before it was a file,','it was a moment.'],65,285,fit(['Before it was a file,','it was a moment.'],97,940,'serif'),PAPER,'serif');c.rect((0,1490,1080,1920),INK);c.text((70,1580),'THE LIGHT YOU NOTICED.',24,PAPER,'mono')
    elif t<15:
        local=t-9;c.image(FLOWER,(0,0,1080,1920));c.rect((0,0,1080,700),INK)
        c.text((65,325),'Color.',145,PAPER,'serif');c.text((65,515),'Feeling.',145,PAPER,'serif');c.rect((0,1510,1080,1920),INK);c.text((70,1600),'THE PART THAT MAKES IT YOURS.',22,PAPER,'mono')
        # A narrow original light seam motivates the move between photographs.
        x=round(1000-1000*ease(local/5));c.line((x,280,x,1510),PAPER,2)
    elif t<21:
        c.rect((0,0,1080,1920),INK);c.lines(['Then you','shape the feeling.'],70,300,102,PAPER,'serif')
        idx=min(179,round((t-15)*30));src=ROOT/'media/curve-frames'/f'{idx+1:04d}.jpg';c.image(src,(0,735,1080,1367),contain=True)
        c.text((70,1430),'Recorded in Looksy · 25 Oct 2026',26,PAPER);c.text((70,1490),'A real tone-curve decision, shown at its recorded pace.',21,MUTED)
    elif t<28:
        c.lines(['And give it','somewhere to live.'],70,300,103,PAPER,'serif')
        local=t-21;x0=round(-1250*ease(local/7))
        for j,path in enumerate([BOARDS[0],BOARDS[4],BOARDS[5]]):
            x=70+j*760+x0;c.image(path,(x,750,x+700,1310),contain=True)
        c.text((70,1410),'Actual native board exports · 16 Nov 2026',25,PAPER)
    else:
        c.im=photons(t);c.d=ImageDraw.Draw(c.im)
        c.rect((0,0,1080,850),INK);c.text((70,300),'I’m building',52,PAPER);c.text((62,405),'LOOKSi.',167,PAPER)
        c.lines(['Photo + video.','For the work only you would make.'],70,685,fit(['For the work only you would make.'],49,940,'serif'),PAPER,'serif',1.35)
        c.rect((0,1430,1080,1920),INK);c.lines(['Make room for your eye.','Follow the build.'],70,1500,57,PAPER,'serif',1.35)
    brand(c,fg=PAPER);return c.im

FRAME={'02':frame02,'03':frame03,'05':frame05}

def cover(id):
    p=SPEC[id];c=C(1080,1350,p['bg'],svg=True);brand(c,70,55,p['fg'],32);label={'02':'A PROJECT IN PHOTO, MOTION AND BOARDS','03':'A PHOTO RECIPE TO TRY','05':'ON LIGHT, COLOR AND MAKING'}[id];c.text((70,130),label,18,p['accent'],'mono')
    if id=='02':
        c.lines(['NEPAL.','In three forms.'],65,215,110,PAPER);c.text((70,470),'A PHOTO / A MOTION STUDY / SIX BOARDS',22,GREEN,'mono');c.image(AFTER,(70,535,1010,1005));c.image(FRAMES/'native-timeline-output-00.jpg',(70,1040,560,1315),contain=True);c.image(BOARDS[4],(600,1010,1010,1325),contain=True)
    elif id=='03':
        c.lines(['Keep the gold.','Keep the quiet blues.'],65,210,fit(['Keep the gold.','Keep the quiet blues.'],94,940,'serif'),INK,'serif');paper_frame(c,AFTER,(70,495,995,1115));c.lines(['Three small choices for this photograph.','An editor I’m building. A place to learn.'],70,1170,29,INK,leading=1.55)
    else:
        c.image(FLOWER,(0,430,1080,1350));c.rect((0,0,1080,465),INK);c.lines(['Make room','for your eye.'],65,205,123,PAPER,'serif');c.rect((0,1135,1080,1350),INK);c.lines(['Photo + video. Your point of view.','A tool I’m building around the work.'],70,1180,31,PAPER,leading=1.45)
        brand(c,70,55,PAPER,32);c.text((70,130),label,18,BLUE,'mono')
    c.save(ROOT/f'campaign{id}/images/cover');return c.im

CAROUSELS={
'02':[
 ('NEPAL.','In three forms.','A photo treatment. A motion study. A story in six boards.',AFTER),
 ('01 / THE PHOTO','Small choices. A considered finish.','Actual Looksy export:5900K,+12%contrast,+0.15EV. A choice for this image.',AFTER),
 ('02 / THE MOTION','A native Timeline review export.','12seconds.1920×1080.30fps. Export receipt verified;2Nov2026.',FRAMES/'native-timeline-output-00.jpg'),
 ('03 / THE BOARDS','A story in six pieces.','NEPAL / NEAR & FAR. Six native1920×1536 PNG exports;16Nov2026.',BOARDS[4]),
 ('THE CASEBOOK','Related work, across sessions.','Photo and boards exported16Nov. Motion review exported2Nov. The actual files accompany the review.',BOARDS[0]),
 ('I’M BUILDING LOOKSi.','What would you inspect?','Photo+video. For professional projects and the work you make for yourself.',AFTER)],
'03':[
 ('KEEP THE GOLD.','Keep the quiet blues.','Three deliberate choices for this photograph.',AFTER),
 ('01 / LOOK FIRST','What is already working?','Golden summit light. Cool layered shadows. Start by naming what you want to keep.',BEFORE),
 ('02 / THE DIRECTION','A little warmer.','Temperature5900K was one choice for this JPEG. Start from your image, rather than copying a number.',AFTER),
 ('03 / THE SHAPE','A little separation.','Contrast+12% and exposure+0.15EV complete this treatment. Small moves, then look again.',AFTER),
 ('04 / COMPARE','The same frame. A different feeling.','The comparison shows the three adjustments together. Neither image has a simulated treatment.',BEFORE),
 ('05 / KEEP THE FILE','Make something you want to revisit.','Both images are real1800×1200 native PNG exports. The original source remains untouched.',AFTER),
 ('I’M BUILDING LOOKSi.','What would you like to learn?','Photo+video. For the work you make for yourself, and the work you make for others.',AFTER)],
'05':[
 ('MAKE ROOM','for your eye.','A point of view starts with what you notice.',AFTER),
 ('LIGHT.','Before it was a file.','It was a moment you wanted to keep.',DAWN),
 ('COLOR.','The feeling stays with you.','A photograph can hold more than a record.',FLOWER),
 ('THE DECISION.','Then you shape it.','Looksy tone-curve recording,25Oct2026. Real native controls; development footage.',FRAMES/'tone-curve-20.jpg'),
 ('THE WORK.','Give it somewhere to live.','Actual native board export from NEPAL / NEAR & FAR,16Nov2026.',BOARDS[4]),
 ('I’M BUILDING LOOKSi.','Photo+video. Your point of view.','Make room for your eye. Follow the build.',BOATS)]}

def carousels(id):
    p=SPEC[id]
    for j,(eyebrow,title,body,image)in enumerate(CAROUSELS[id]):
        c=C(1080,1350,p['bg'],svg=True);brand(c,70,55,p['fg'],30);c.text((890,60),f'{j+1:02d}/{len(CAROUSELS[id]):02d}',18,p['fg'],'mono');c.text((70,155),eyebrow,21,p['accent'],'mono')
        kind='serif'if id in['03','05']else'sans';texts=wrap(title,87,940,kind);size=fit(texts,87,940,kind);c.lines(texts,65,225,size,p['fg'],kind)
        yy=max(460,225+len(texts)*size*1.1+65);bottom=1030;c.image(image,(70,yy,1010,bottom),contain=id=='02'and j in[2,3,4])
        c.line((70,1080,1010,1080),p['accent'],2);body=body.replace('5900K','5900 K').replace('12seconds','12 seconds').replace('16Nov','16 Nov').replace('2Nov','2 Nov').replace('25Oct','25 Oct').replace('Photo+video','Photo + video').replace('1920×','1920 × ').replace('1800×','1800 × ').replace('30fps','30 fps').replace('.1920','. 1920').replace('.30','. 30').replace(';2','; 2').replace(',contrast',', contrast').replace(',and',', and').replace('Contrast+','Contrast +').replace('exposure+','exposure +')
        body=public_copy(body);c.lines(wrap(body,33,930),70,1125,33,p['fg'],leading=1.35);c.save(ROOT/f'campaign{id}/carousels/slide-{j+1:02d}')

def storyboard(id):
    p=SPEC[id];c=C(2400,1500,p['bg'],svg=True);brand(c,80,60,p['fg'],42);c.text((80,150),p['role'].upper(),27,p['accent'],'mono');c.text((78,225),p['title'],fit([p['title']],98,2230,'serif'if id!='02'else'sans'),p['fg'],'serif'if id!='02'else'sans')
    for j,(start,end,title)in enumerate(p['scenes']):
        x=80+j*375;sample=FRAME[id](start+min(1.3,(end-start)/2));path=ROOT/f'campaign{id}/qa/story-{j+1:02d}.jpg';path.parent.mkdir(parents=True,exist_ok=True);sample.save(path,quality=94)
        c.image(path,(x,410,x+340,1015));c.text((x,1050),f'{start:02d}–{end:02d}s',27,p['accent'],'mono');c.lines(wrap(title,29,340),x,1110,29,p['fg'],leading=1.25)
    c.line((80,1240,2310,1240),MUTED,1)
    note={'02':'Casebook across sessions. Fresh photo + six boards:16Nov2026. Native Timeline review output:2Nov2026. No single-run or current-playback claim.',
          '03':'Verified native baseline / chosen exports. Temperature5900K +contrast12% +exposure0.15EV are a combined treatment. No recorded Undo or isolated-control response claimed.',
          '05':'Original photographs, actual native board exports and dated native tone-curve footage. Light interlude is original procedural campaign art, rather than a product effect.'}[id]
    c.lines(wrap(public_copy(note),29,2210),80,1290,29,p['fg'],leading=1.45);c.save(ROOT/f'campaign{id}/storyboard/board')

def score(id):
    p=SPEC[id];rate=48000;duration=p['duration'];t=np.arange(round(rate*duration),dtype=np.float64)/rate;signal=np.zeros_like(t);rng=np.random.default_rng(int(id))
    chords={'02':[130.81,196,261.63],'03':[130.81,164.81,196,246.94],'05':[65.41,130.81,196,293.66]}[id]
    for j,f in enumerate(chords):
        amp=.017 if id=='02'else.022
        swell=(.5+.5*np.sin(2*np.pi*t/duration-math.pi/2))
        signal+=amp*np.sin(2*np.pi*f*t+.008*np.sin(t*.7))*swell
        signal+=amp*.12*np.sin(2*np.pi*f*2.003*t)*swell
    for start,_,_ in p['scenes']:
        local=t-start;gate=np.maximum(local,0);env=np.where(local>=0,np.exp(-gate/0.23)*np.minimum(gate/.008,1),0)
        signal+=.055*np.sin(2*np.pi*(440 if id=='02' else 660)*local)*env
    if id=='05':
        noise=rng.normal(0,.0035,len(t));kernel=np.ones(128)/128;signal+=np.convolve(noise,kernel,'same')*np.sin(np.pi*t/duration)**2
    signal*=np.minimum(t/.15,1)*np.minimum((duration-t)/1.2,1)
    stereo=np.stack([signal,signal*.96],1);path=ROOT/f'campaign{id}/audio/original-score.wav';path.parent.mkdir(parents=True,exist_ok=True)
    with wave.open(str(path),'wb')as f:f.setnchannels(2);f.setsampwidth(2);f.setframerate(rate);f.writeframes(np.int16(np.clip(stereo,-1,1)*32767).tobytes())
    return path

def prepare_video_frames():
    for folder,src,start,duration in [('motion-frames','native-timeline-review-2026-11-02.mp4',0,12),('curve-frames','tone-curve-2026-10-25.mp4',18,6)]:
        dest=ROOT/'media'/folder;dest.mkdir(exist_ok=True)
        if not (dest/'0001.jpg').exists():
            subprocess.run(['ffmpeg','-v','error','-y','-ss',str(start),'-i',str(ROOT/'media/archive'/src),'-t',str(duration),'-vf','fps=30,scale=1080:-2','-q:v','2',str(dest/'%04d.jpg')],check=True)

def render(id,variant='a'):
    p=SPEC[id];out=ROOT/f'campaign{id}/videos';out.mkdir(parents=True,exist_ok=True);audio=score(id);path=out/('hero.mp4'if variant=='a'else f'hook-{variant}.mp4')
    cmd=['ffmpeg','-hide_banner','-loglevel','error','-y','-f','rawvideo','-pix_fmt','rgb24','-s','1080x1920','-r',str(FPS),'-i','pipe:0','-i',str(audio),'-c:v','libx264','-threads','4','-preset','fast','-crf','18','-vf','scale=in_range=pc:out_range=tv:out_color_matrix=bt709,setparams=range=limited:color_primaries=bt709:color_trc=bt709:colorspace=bt709,format=yuv420p','-pix_fmt','yuv420p','-c:a','aac','-b:a','192k','-af','loudnorm=I=-18:TP=-1.5:LRA=9','-t',str(p['duration']),'-colorspace','bt709','-color_primaries','bt709','-color_trc','bt709','-color_range','tv','-movflags','+faststart',str(path)]
    cmd[cmd.index('-t'):cmd.index('-t')]=['-ar','48000']
    proc=subprocess.Popen(cmd,stdin=subprocess.PIPE)
    try:
        for n in range(FPS*p['duration']):proc.stdin.write(FRAME[id](n/FPS,variant).tobytes())
    finally:proc.stdin.close()
    if proc.wait()!=0:raise RuntimeError(f'Encoding failed:{path}')
    silent=path.with_stem(path.stem+'-silent');subprocess.run(['ffmpeg','-v','error','-y','-i',str(path),'-c:v','copy','-an','-movflags','+faststart',str(silent)],check=True)
    print(f'Rendered {path.relative_to(ROOT)}',flush=True)

def manifest(id):
    p=SPEC[id]
    info={'title':p['title'],'role':p['role'],'duration_seconds':p['duration'],'fps':FPS,'dimensions':[1080,1920],'sources':'../media/provenance.json','scene_plan':[{'start':s,'end':e,'title':title}for s,e,title in p['scenes']],
       'audio':'Original sample-free sine/harmonic synthesis with authored scene accents; archived source audio muted.',
       'source_scope':{'02':'Actual native photo/board/Timeline exports across sessions, not one fresh recorded end-to-end run.','03':'Actual same-source native photo exports; three combined adjustment choices; no single-control animation stands in for app response.','05':'Original source photography + genuine dated product recording and actual board export; procedural light is campaign art.'}[id],
       'public_caption':{'02':'NEPAL, in three forms: a photo treatment, a 12-second motion study, and a story in six boards. These are actual Looksy outputs made across sessions while building the tool. Photo and board exports:16November. Native Timeline review excerpt:2November. I’m building LOOKSi for professional projects and the work you make for yourself. What would you want to inspect before trying a new editor?',
          '03':'Keep the gold. Keep the quiet blues. For this Nepal photograph I chose5900K,contrast+12%,and exposure+0.15EV. The comparison shows the combined treatment, using actual Looksy PNG exports from the same source. Your photograph may need different choices. I’m building LOOKSi for photo and video, and I’d like the learning to feel approachable. What would you like to learn to change in your own photos?',
          '05':'It starts with something you notice: light, color, a moment you want to keep. I’m building LOOKSi, a photo and video editor, to make room for the way people see. The film brings together original Nepal photographs, actual board exports and a dated native tone-curve recording. The light interlude is original campaign artwork. Follow the build. Bring your eye.'}[id],
       'alt_text':p['title']+' '+p['sub'],'remaining':'Creative review, media publication permissions and current placement check. Optional continuous current-build demo and founder narration are tracked separately.'}
    info['public_caption']=public_copy(info['public_caption']);(ROOT/f'campaign{id}/execution.json').write_text(json.dumps(info,indent=2))

def main():
    a=argparse.ArgumentParser();a.add_argument('--ids',nargs='+',default=['02','03','05']);a.add_argument('--stills-only',action='store_true');a.add_argument('--video-only',action='store_true');a.add_argument('--variants',action='store_true');args=a.parse_args();prepare_video_frames()
    for id in args.ids:
        for folder in ['qa','images','storyboard','carousels','audio']:(ROOT/f'campaign{id}/{folder}').mkdir(parents=True,exist_ok=True)
        if not args.video_only:cover(id);carousels(id);storyboard(id)
        if not args.stills_only:
            render(id)
            if args.variants:
                render(id,'b');render(id,'c')
        manifest(id)
if __name__=='__main__':main()
