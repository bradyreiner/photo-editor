#!/usr/bin/env python3
"""Original campaign01 layout/motion. Source images stay color-unaltered; all graphic work is editable SVG."""
from pathlib import Path
import base64,html,json,math,subprocess,hashlib,wave,sys
import numpy as np
from PIL import Image,ImageDraw,ImageFont,ImageOps,ImageCms
ROOT=Path(__file__).resolve().parents[1]
REPO=ROOT.parents[4]
F=REPO/'apps/desktop/assets/fonts'
FONT={'serif':F/'instrumentserif/InstrumentSerif.ttf','italic':F/'instrumentserif/InstrumentSerif-Italic.ttf','sans':F/'spacegrotesk/SpaceGrotesk[wght].ttf','mono':F/'spacemono/SpaceMono-Regular.ttf'}
PAPER='#F1EFE7';INK='#202522';BLUE='#2456B8';RED='#FF3B30';GREEN='#30D158';MUTED='#626961';RULE='#C6C9BE'
W,H,FPS,DURATION=1080,1920,30,32
SRGB=ImageCms.ImageCmsProfile(ImageCms.createProfile('sRGB')).tobytes()
for n in ['images','carousel','video','audio','boards','qa']: (ROOT/n).mkdir(exist_ok=True)
IM={k:Image.open(ROOT/'sources'/v).convert('RGB') for k,v in {'before':'before-native-export.png','chosen':'chosen-native-export.png','board':'first-light-native-board.png','archive':'archive-tone-still.png'}.items()}
fonts={}
def font(sz,kind='sans'):
 key=(sz,kind)
 if key not in fonts: fonts[key]=ImageFont.truetype(str(FONT[kind]),sz)
 return fonts[key]
def wrapped(text,width,sz,kind='sans'):
 out=[]
 for para in text.split('\n'):
  words=para.split();line=''
  for word in words:
   new=(line+' '+word).strip()
   if font(sz,kind).getlength(new)>width and line:out.append(line);line=word
   else:line=new
  out.append(line)
 return out
class C:
 def __init__(self,w,h,record=True):
  self.w,self.h,self.record=w,h,record;self.im=Image.new('RGB',(w,h),PAPER);self.d=ImageDraw.Draw(self.im);self.parts=[]
  self.rect((0,0,w,h),PAPER)
 def rect(self,box,fill,outline=None,width=1):
  self.d.rectangle(box,fill=fill,outline=outline,width=width)
  if self.record:
   x,y,x2,y2=box;self.parts.append(f'<rect x="{x}" y="{y}" width="{x2-x}" height="{y2-y}" fill="{fill}" stroke="{outline or "none"}" stroke-width="{width}"/>')
 def line(self,xy,fill=INK,width=2):
  self.d.line(xy,fill=fill,width=width)
  if self.record:self.parts.append(f'<polyline points="{" ".join(f"{xy[i]},{xy[i+1]}" for i in range(0,len(xy),2))}" fill="none" stroke="{fill}" stroke-width="{width}" stroke-linecap="round"/>')
 def text(self,x,y,txt,sz=32,kind='sans',fill=INK,leading=1.1):
  for j,t in enumerate(txt.split('\n')):
   yy=y+j*sz*leading;self.d.text((x,yy),t,font=font(sz,kind),fill=fill,anchor='lt')
   if self.record:self.parts.append(f'<text x="{x}" y="{yy}" font-family="{kind}" font-size="{sz}" dominant-baseline="text-before-edge" fill="{fill}">{html.escape(t)}</text>')
 def para(self,x,y,txt,width,sz=30,kind='sans',fill=INK,leading=1.3):self.text(x,y,'\n'.join(wrapped(txt,width,sz,kind)),sz,kind,fill,leading)
 def pic(self,key,box,crop=None,zoom=1):
  im=IM[key] if isinstance(key,str) else key;x,y,x2,y2=map(round,box)
  if crop: im=im.crop(crop)
  if zoom!=1:
   iw,ih=im.size;dx=iw*(1-1/zoom)/2;dy=ih*(1-1/zoom)/2;im=im.crop((dx,dy,iw-dx,ih-dy))
  out=ImageOps.fit(im,(x2-x,y2-y),Image.Resampling.LANCZOS);self.im.paste(out,(x,y));self.d=ImageDraw.Draw(self.im)
  if self.record:
   import io
   b=io.BytesIO();im.save(b,format='JPEG',quality=94);data=base64.b64encode(b.getvalue()).decode();iw,ih=im.size
   self.parts.append(f'<svg x="{x}" y="{y}" width="{x2-x}" height="{y2-y}" viewBox="0 0 {x2-x} {y2-y}"><image width="{x2-x}" height="{y2-y}" preserveAspectRatio="xMidYMid slice" href="data:image/jpeg;base64,{data}"/></svg>')
 def note(self,x,y,txt,sz=34):self.text(x,y,txt,sz,'italic',BLUE)
 def underline(self,x,y,w,progress=1):
  pts=[]
  for i in range(int(w*progress)+1):pts.extend((x+i,y+math.sin(i*.014)*2.4))
  if pts:self.line(tuple(pts),BLUE,3)
 def arrow(self,x,y,dx,dy):
  self.line((x,y,x+dx*.5,y+dy*.35,x+dx,y+dy),BLUE,3);self.line((x+dx-10,y+dy-16,x+dx,y+dy,x+dx+8,y+dy-16),BLUE,3)
 def header(self,no=None):
  self.text(60,52,'LOOKSi',34,'sans');self.text(60,107,'A NOTE FROM THE BUILDER',18,'mono',MUTED)
  for j,col in enumerate([RED,GREEN,'#3282FF']):self.rect((922+j*16,64,929+j*16,71),col)
  if no:self.text(self.w-128,self.h-65,no,20,'mono',MUTED)
 def save(self,path):
  self.im.save(path.with_suffix('.png'),icc_profile=SRGB)
  faces=''.join(f'@font-face{{font-family:{k};src:url(data:font/ttf;base64,{base64.b64encode(p.read_bytes()).decode()})}}' for k,p in FONT.items())
  path.with_suffix('.svg').write_text(f'<svg xmlns="http://www.w3.org/2000/svg" width="{self.w}" height="{self.h}" viewBox="0 0 {self.w} {self.h}"><style>{faces}</style>{"".join(self.parts)}</svg>')

def hero():
 c=C(1080,1350);c.header();c.text(60,170,'I wanted\nto make this.',109,'serif',leading=.99);c.note(744,343,'start with the work',32);c.arrow(877,383,-45,46)
 c.pic('chosen',(60,440,1020,1080));c.text(60,1104,'A real photograph. A real build.',24,'sans',MUTED)
 c.text(60,1162,'A photo + video editor I’m building',32);c.text(60,1204,'for the way people make their work.',32)
 c.note(60,1270,'— Brady',36);c.text(530,1284,'What should I put it through?',25)
 c.save(ROOT/'images/01-i-wanted-to-make-this');return c

def slide(n):
 if n==1:return hero()
 c=C(1080,1350);c.header(f'{n:02d} / 06')
 if n==2:
  c.text(60,170,'A frame I want\nto keep.',103,'serif');c.pic('chosen',(60,420,1020,1060));c.note(68,1092,'First light. A deliberate look.',38);c.para(60,1180,'Start with the work. Then decide what you want it to hold onto.',880,31);c.text(60,1288,'Native photo export · 16 Nov 2026',22,'mono',MUTED)
 if n==3:
  c.text(60,170,'Make a choice.\nLook again.',103,'serif');c.pic('archive',(60,438,1020,1120),(1190,322,1920,1124));c.note(63,1144,'The small decisions are the work.',38);c.text(60,1210,'Genuine native app recording',28);c.text(60,1250,'25 Oct 2026 · historical interface',24,'mono',MUTED)
 if n==4:
  c.text(60,170,'Try a look.\nKeep what helps.',103,'serif');c.pic('before',(60,450,1020,1090));chosen=ImageOps.fit(IM['chosen'],(960,640),Image.Resampling.LANCZOS);c.im.paste(chosen.crop((480,0,960,640)),(540,450));c.d=ImageDraw.Draw(c.im)
  # SVG adds the true chosen half as a separate editable image clip.
  if c.record:
   temp=IM['chosen'].crop((IM['chosen'].width/2,0,IM['chosen'].width,IM['chosen'].height));c.pic(temp,(540,450,1020,1090))
  c.line((540,450,540,1090),PAPER,4);c.text(81,466,'BEFORE',22,'mono','#FFFFFF');c.text(729,466,'CHOSEN',22,'mono','#FFFFFF');c.note(60,1120,'A comparison, not a magic setting.',37);c.para(60,1190,'The same photo, exported from the native app. A real choice you can inspect.',900,29);c.text(60,1288,'Native exports · 16 Nov 2026',22,'mono',MUTED)
 if n==5:
  c.text(60,170,'Give it somewhere\nto go.',98,'serif');c.pic('board',(60,410,1020,1178));c.text(60,1203,'FIRST LIGHT / from NEPAL · NEAR & FAR',23,'mono');c.text(60,1245,'Actual native board export · 16 Nov 2026',22,'mono',MUTED)
 if n==6:
  c.text(60,180,'So I’m building\nLOOKSi.',107,'serif');c.underline(60,405,330);c.text(60,465,'Photo + video.',39);c.para(60,535,'For professional work. For the work you make just because.',840,39,leading=1.3);c.text(60,760,'What would you\nwant to test?',94,'serif');c.note(60,1010,'Tell me the project, not just the feature.',37);c.pic('chosen',(60,1090,1020,1250));c.text(60,1288,'— Brady',28,'italic')
 return c

def frame(t,variant='a',ui=None):
 c=C(W,H,False);c.header();k=min(7,int(t//4));u=(t-k*4)/4;s=1-(1-min(u*2,1))**3
 if k==0:
  heads={'a':'I wanted\nto make this.','b':'I built a tool.\nBring a real project.','c':'What should this\neditor handle?'};sz=113 if variant=='a' else 97
  c.text(60,205,heads[variant],sz,'serif',leading=1.03);c.pic('chosen',(60,540-int(s*14),1020,1190-int(s*14)),zoom=1+.045*u);c.note(60,1230,'start with the work',48);c.arrow(775,1270,120,-55);c.para(60,1390,'A note from Brady, the builder of LOOKSi.',900,40);c.text(60,1640,'PHOTO + VIDEO / IN DEVELOPMENT',25,'mono',MUTED)
 elif k==1:
  c.text(60,205,'One photo.\nA deliberate look.',108,'serif');box=(60,560,1020,1200);c.pic('before',box);chosen=ImageOps.fit(IM['chosen'],(960,640),Image.Resampling.LANCZOS);split=int(960*(.32+.36*u));c.im.paste(chosen.crop((split,0,960,640)),(60+split,560));c.d=ImageDraw.Draw(c.im);c.line((60+split,560,60+split,1200),PAPER,4);c.text(80,585,'BEFORE',25,'mono','#FFFFFF');c.text(800,585,'CHOSEN',25,'mono','#FFFFFF');c.note(60,1260,'keep what helps the picture',47);c.para(60,1415,'Same source. Actual native exports.\n16 Nov 2026.',900,33,fill=MUTED)
 elif k in (2,3):
  c.text(60,205,'The small decisions\nare the work.' if k==2 else 'Make a choice.\nLook again.',103 if k==2 else 110,'serif')
  if ui is not None:c.pic(ui,(60,565,1020,1335))
  else:c.pic('archive',(60,565,1020,1335),(95,250,1110,950) if k==2 else (1200,320,1920,1124))
  c.rect((60,1370,1020,1480),'#E3E6DE');c.text(82,1390,'NATIVE APP ARCHIVE',31,'mono');c.text(82,1430,'25 Oct 2026 · historical recording',31)
  c.note(60,1550,'an actual app action, from the archive',40)
 elif k==4:
  c.text(60,205,'Give it somewhere\nto go.',109,'serif');c.pic('board',(60,535,1020,1303),zoom=1+.018*u);c.note(60,1350,'an image, a headline, a story',43);c.para(60,1460,'FIRST LIGHT / NEPAL · NEAR & FAR\nActual native board export · 16 Nov 2026',900,30,fill=MUTED)
 elif k==5:
  c.pic('chosen',(0,0,1080,1920),zoom=1.02+.04*u);c.rect((0,1210,1080,1920),PAPER);c.text(60,1270,'For professional work.',62,'serif');c.text(60,1362,'For the work you make\njust because.',62,'serif');c.note(60,1560,'both deserve care',46);c.underline(60,1630,425,s)
 elif k==6:
  c.pic('chosen',(60,175,1020,600),zoom=1.05);c.text(60,700,'So I’m building\nLOOKSi.',121,'serif');c.underline(60,980,450,s);c.text(60,1070,'Photo + video.',57);c.para(60,1210,'A tool for people who want\nto make something of their own.',900,40);c.note(60,1480,'— Brady',58)
 else:
  c.text(60,220,'What would you\nwant to test?',116,'serif');c.note(60,550,'bring a real project',49);c.underline(62,620,500,s);c.para(60,735,'Tell me what you make —\nand the step that gets in the way.',900,42);c.pic('chosen',(60,1010,1020,1510),zoom=1.02+.02*u);c.text(60,1570,'LOOKSi',44);c.text(60,1640,'Follow the build.',32)
 return c

SCENES=[('00–04','THE LETTER','I wanted to make this.','Fresh native image export; print-like frame, subtle push. No founder proxy.'),('04–08','THE LOOK','One photo. A deliberate look.','Actual native before/chosen exports; a labeled editorial wipe, not a simulated app adjustment.'),('08–12','THE DECISION','The small decisions are the work.','Genuine native tone-curve recording, photo crop. Archive date stays visible.'),('12–16','THE DETAIL','Make a choice. Look again.','Same genuine recording, curve close-up. Historical 25 Oct 2026, not current action.'),('16–20','THE BOARD','Give it somewhere to go.','Actual FIRST LIGHT board export, 16 Nov 2026; typography and photograph visible.'),('20–24','THE AUDIENCE','For professional work. For work you make just because.','Actual photo with a paper letter panel; both audiences get equal care.'),('24–28','THE SIGNATURE','So I’m building LOOKSi. Photo + video.','Personal builder letter, RGB pencil underline, no synthetic founder or voice.'),('28–32','THE INVITATION','What would you want to test?','Specific project request and actual work; final question holds.')]

def board():
 c=C(2520,1820);c.text(60,50,'I wanted to make this.',91,'serif');c.text(62,155,'01 / BUILDER LETTER / 32 SEC / ORIGINAL SOUND + OPEN TEXT / NO FOUNDER SHOOT REQUIRED',22,'mono',MUTED)
 for i,(time,title,words,detail) in enumerate(SCENES):
  x=60+(i%4)*615;y=250+(i//4)*740;c.line((x,y,x+565,y),RULE,2);c.text(x,y+24,f'{i+1:02d} / {time}',24,'mono',BLUE);thumb=frame(i*4+2).im;c.pic(thumb,(x,y+78,x+244,y+512));c.text(x+270,y+86,title,23,'mono');c.para(x+270,y+134,words,275,31,'serif',leading=1.18);c.para(x+270,y+300,detail,275,22,fill=MUTED,leading=1.25)
 c.text(60,1750,'Sources: native photo + board exports / 16 Nov 2026. UI action recording / 25 Oct 2026. Motion design is campaign artwork.',24)
 c.save(ROOT/'boards/01-storyboard');return c

def sound():
 sr=48000;n=sr*DURATION;t=np.arange(n)/sr;sig=np.zeros(n,dtype=np.float64)
 notes=[146.8324,220,293.6648,329.6276,220,293.6648,196,146.8324]
 for j,freq in enumerate(notes):
  start=j*4;local=t-start;env=np.where((local>=0)&(local<7),(1-np.exp(-np.maximum(local,0)*2.3))*np.exp(-np.maximum(local,0)*.65),0)
  sig+=.045*env*(np.sin(2*np.pi*freq*t)+.25*np.sin(2*np.pi*freq*2*t)+.1*np.sin(2*np.pi*freq*3*t))
 rng=np.random.default_rng(41)
 for j in range(1,8):
  a=j*4;local=t-a;env=np.where((local>=0)&(local<.18),np.exp(-np.maximum(local,0)*30),0);sig+=.004*env*rng.normal(0,1,n)
 fade=np.minimum(1,t/1.5)*np.minimum(1,(DURATION-t)/2);sig*=fade*5;st=np.stack([sig,sig*.99],axis=1);pcm=np.clip(st*32767,-32768,32767).astype('<i2')
 with wave.open(str(ROOT/'audio/01-original-letter-bed.wav'),'wb') as wav:wav.setnchannels(2);wav.setsampwidth(2);wav.setframerate(sr);wav.writeframes(pcm.tobytes())

def render_video():
 out=ROOT/'video/01-builder-letter.mp4'
 cmd=['ffmpeg','-v','error','-y','-f','rawvideo','-pixel_format','rgb24','-video_size',f'{W}x{H}','-framerate',str(FPS),'-i','pipe:0','-i',str(ROOT/'audio/01-original-letter-bed.wav'),'-c:v','libx264','-preset','fast','-crf','19','-threads','2','-vf','scale=in_range=pc:out_range=tv:out_color_matrix=bt709,format=yuv420p','-colorspace','bt709','-color_primaries','bt709','-color_trc','bt709','-color_range','tv','-pix_fmt','yuv420p','-g','30','-force_key_frames','4,8,12,16,20,24,28','-c:a','aac','-b:a','192k','-movflags','+faststart','-shortest',str(out)]
 enc=subprocess.Popen(cmd,stdin=subprocess.PIPE)
 dec=None
 for i in range(FPS*DURATION):
  t=i/FPS;ui=None
  if 8<=t<16:
   if dec is None:dec=subprocess.Popen(['ffmpeg','-v','error','-i',str(ROOT/'sources/archive-native-tone-12-20s.mp4'),'-f','rawvideo','-pix_fmt','rgb24','pipe:1'],stdout=subprocess.PIPE)
   raw=dec.stdout.read(1920*1124*3)
   if len(raw)!=1920*1124*3:raise RuntimeError('Archive decode incomplete')
   im=Image.frombytes('RGB',(1920,1124),raw);ui=im.crop((95,250,1110,1060) if t<12 else (1200,320,1920,1124))
  c=frame(t,ui=ui);enc.stdin.write(c.im.tobytes())
  if i%120==0:print(f'film {t:.0f}s / {DURATION}s',flush=True)
 enc.stdin.close();code=enc.wait()
 if dec:dec.stdout.close();dec.wait()
 if code:raise RuntimeError(f'Film encode failed {code}')
 # Two alternate openings share the identical28-second body, without pretending three films are different ideas.
 subprocess.run(['ffmpeg','-v','error','-y','-ss','4','-i',str(out),'-c','copy',str(ROOT/'video/body-4-32s.mp4')],check=True)
 for var,slug in [('b','bring-a-real-project'),('c','what-should-this-handle')]:
  hook=ROOT/f'video/hook-{var}.mp4';enc=subprocess.Popen(['ffmpeg','-v','error','-y','-f','rawvideo','-pix_fmt','rgb24','-s',f'{W}x{H}','-r',str(FPS),'-i','pipe:0','-i',str(ROOT/'audio/01-original-letter-bed.wav'),'-t','4','-c:v','libx264','-crf','19','-preset','fast','-threads','2','-vf','scale=in_range=pc:out_range=tv:out_color_matrix=bt709,format=yuv420p','-colorspace','bt709','-color_primaries','bt709','-color_trc','bt709','-color_range','tv','-pix_fmt','yuv420p','-g','30','-c:a','aac','-b:a','192k',str(hook)],stdin=subprocess.PIPE)
  for i in range(FPS*4):enc.stdin.write(frame(i/FPS,var).im.tobytes())
  enc.stdin.close();assert enc.wait()==0
  listing=ROOT/f'video/concat-{var}.txt';listing.write_text(f"file '{hook.name}'\nfile 'body-4-32s.mp4'\n")
  subprocess.run(['ffmpeg','-v','error','-y','-f','concat','-safe','0','-i',str(listing),'-c','copy','-movflags','+faststart',str(ROOT/f'video/01-hook-{slug}.mp4')],check=True)
 for name in ['body-4-32s.mp4','hook-b.mp4','hook-c.mp4','concat-b.txt','concat-c.txt']:(ROOT/'video'/name).unlink()

def static():
 h=hero()
 for n in range(1,7):
  c=slide(n);c.save(ROOT/f'carousel/{n:02d}-slide')
 b=board()
 contact=Image.new('RGB',(1080,540),PAPER)
 for j,t in enumerate([2,6,10,14,18,22,26,30]):contact.paste(frame(t).im.resize((135,240)),(j*135,0))
 for j in range(6):contact.paste(slide(j+1).im.resize((180,225)),(j*180,280))
 contact.save(ROOT/'qa/design-contact.jpg',quality=92)

if __name__=='__main__':
 static();sound()
 if '--static-only' not in sys.argv:render_video()
