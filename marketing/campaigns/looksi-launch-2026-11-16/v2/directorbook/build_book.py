#!/usr/bin/env python3
"""A designed20-page directorbook from actual V2 executions, source ledgers and art."""
from pathlib import Path
import json,re,subprocess,io,hashlib
from PIL import Image,ImageOps
from reportlab.pdfgen import canvas
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.lib.colors import HexColor
from reportlab.lib.utils import ImageReader,simpleSplit
ROOT=Path(__file__).resolve().parents[1];HERE=ROOT/'directorbook';REPO=ROOT.parents[3]
FONTROOT=REPO/'apps/desktop/assets/fonts'
for name,rel in [('Serif','instrumentserif/InstrumentSerif.ttf'),('Italic','instrumentserif/InstrumentSerif-Italic.ttf'),('Sans','spacegrotesk/SpaceGrotesk[wght].ttf'),('Mono','spacemono/SpaceMono-Regular.ttf'),('Heavy','archivoblack/ArchivoBlack-Regular.ttf')]:pdfmetrics.registerFont(TTFont(name,str(FONTROOT/rel)))
W,H=1200,800;PAPER='#F1EFE7';INK='#17201E';MUTED='#626B66';WHITE='#FFFFFF';BLUE='#3282FF';GREEN='#30D158';RED='#FF3B30';RULE='#C7CFC6'
OUT=ROOT/'creative-direction-book.pdf';C=canvas.Canvas(str(OUT),pagesize=(W,H),pageCompression=1);C.setTitle('LOOKSi - Creative direction book V2');C.setAuthor('LOOKSi / Brady');C.setSubject('Five campaign platforms, actual executions, source evidence and production direction')
PAGE=0;CHECK=[];PAGE_TITLES=[];IMAGE_SOURCE_RECORDS={}
def clean(s):
 s=re.sub(r'\\u([0-9a-fA-F]{4})',lambda m:chr(int(m.group(1),16)),str(s)).replace('\\n','\n')
 for d in ['–','—','‑','−']:s=s.replace(d,'-')
 s=re.sub(r',(?=\S)',', ',s);s=re.sub(r':(?=\d)',': ',s)
 s=s.replace('16Nov2026','16 Nov 2026').replace('25Oct2026','25 Oct 2026').replace('16Nov','16 Nov').replace('25Oct','25 Oct')
 s=s.replace('completed12','completed 12').replace('Completed12','Completed 12').replace('(2Nov)','(2 Nov)').replace('from2Nov','from 2 Nov')
 s=s.replace('exposure+','exposure +')
 s=s.replace('Actual1920','Actual 1920').replace('Combined5900','Combined 5900').replace('+12%contrast','+12% contrast').replace('+0.15EV','+0.15 EV').replace('16 Nov2026','16 Nov 2026').replace('24h, 72h and7days','24 hours, 72 hours and 7 days').replace('Allow25minutes','Allow 25 minutes').replace('one40s','one 40 second').replace('DAY11','DAY 11').replace('DAY14','DAY 14')
 s=s.replace('16November','16 November').replace('2November','2 November').replace('chose5900K','chose 5900 K').replace('5900K','5900 K').replace('contrast+12%','contrast +12%').replace('exposure+0.15EV','exposure +0.15 EV').replace(',and',', and').replace(',contrast',', contrast')
 return s

def rect(x,y,w,h,col):C.setFillColor(HexColor(col));C.rect(x,H-y-h,w,h,fill=1,stroke=0)
def line(x,y,x2,y2,col=RULE,width=1):C.setStrokeColor(HexColor(col));C.setLineWidth(width);C.line(x,H-y,x2,H-y2)
def txt(x,y,s,size=20,font='Sans',col=INK,leading=1.08):
 s=clean(s);C.setFillColor(HexColor(col));C.setFont(font,size)
 for j,t in enumerate(s.split('\n')):
  yy=y+j*size*leading;C.drawString(x,H-yy-size*.82,t)
  width=pdfmetrics.stringWidth(t,font,size);CHECK.append({'page':PAGE,'text':t,'x':x,'y':yy,'right':x+width,'bottom':yy+size,'kind':'text'})
 return y+len(s.split('\n'))*size*leading

def para(x,y,s,width,size=18,font='Sans',col=INK,leading=1.35,maxh=None):
 s=clean(s);yy=y
 for block in s.split('\n\n'):
  lines=[]
  for raw in block.split('\n'):lines.extend(simpleSplit(raw,font,size,width) or [''])
  txt(x,yy,'\n'.join(lines),size,font,col,leading);yy+=len(lines)*size*leading+size*.55
 if maxh is not None and yy-y>maxh:raise RuntimeError(f'Text overflow page{PAGE}:{yy-y}>{maxh}: {s[:60]}')
 return yy

def pic(path,x,y,w,h,fit='contain'):
 p=Path(path);assert p.exists(),p;IMAGE_SOURCE_RECORDS[str(p.relative_to(ROOT)) if p.is_relative_to(ROOT) else str(p)]=hashlib.sha256(p.read_bytes()).hexdigest();im=Image.open(p).convert('RGB')
 if fit=='cover':im=ImageOps.fit(im,(round(w*2),round(h*2)),Image.Resampling.LANCZOS);ww,hh=w,h;xx,yy=x,y
 else:
  iw,ih=im.size;scale=min(w/iw,h/ih);ww,hh=iw*scale,ih*scale;xx=x+(w-ww)/2;yy=y+(h-hh)/2
 C.drawImage(ImageReader(im),xx,H-yy-hh,width=ww,height=hh,mask='auto');return xx,yy,ww,hh

def base(title,dark=False,label='CREATIVE DIRECTION / V2'):
 global PAGE
 PAGE+=1;PAGE_TITLES.append(title);rect(0,0,W,H,INK if dark else PAPER);col=WHITE if dark else INK;muted='#B6C0B7' if dark else MUTED
 txt(48,30,'LOOKSi',25,'Sans',col);txt(180,37,label,11,'Mono',muted)
 for j,color in enumerate([RED,GREEN,BLUE]):rect(1092+j*12,38,5,5,color)
 line(48,754,1152,754,'#3B4741' if dark else RULE);txt(48,771,'LOOKSi / CREATIVE DIRECTION / NOVEMBER 2026',10,'Mono',muted);txt(1119,768,f'{PAGE:02d}',16,'Mono',col)
 return col

def end():C.showPage()
def eyebrow(x,y,s,col=MUTED):txt(x,y,s.upper(),12,'Mono',col)
def source(x,y,s,width=1060,col=MUTED):para(x,y,s,width,11,'Sans',col,1.2)
def labelpill(x,y,s,col=BLUE):rect(x,y,7,22,col);txt(x+17,y+2,s,16)
def caption_for(i):
 if i=='01':return json.loads((ROOT/'campaign01/copy-update-proposal.json').read_text())['instagram_caption']
 if i=='04':return json.loads((ROOT/'campaign04/source/copy-and-testing.json').read_text())['instagram_caption']
 return json.loads((ROOT/f'campaign{i}/execution.json').read_text())['public_caption']

def cover(i):return ROOT/('campaign01/images/01-i-wanted-to-make-this.png' if i=='01' else f'campaign{i}/images/cover.png')
def board(i):return ROOT/('campaign01/boards/01-storyboard.png' if i=='01' else f'campaign{i}/storyboard/board.png')
def slide(i,n):return ROOT/(f'campaign01/carousel/{n:02d}-slide.png' if i=='01' else f'campaign{i}/carousels/slide-{n:02d}.png')
FINISH=ROOT/'media/previews/native-photo-finish.jpg';BEFORE=ROOT/'media/previews/native-photo-baseline.jpg';BOARD5=ROOT/'media/previews/nepal-near-far-05.jpg';SOURCE=ROOT/'media/previews/IMG_5071.jpg'
#01 / COVER
base('Five ways to meet the tool.');pic(FINISH,440,0,760,800,'cover');rect(0,0,470,800,PAPER);txt(48,55,'LOOKSi',40);eyebrow(48,125,'Launch director book / V2');txt(48,215,'Five ways\nto meet\nthe tool.',87,'Serif',leading=.99);para(50,540,'The editor Brady built.\nThe work it can help people make.\nFive different reasons to look closer.',355,21,maxh=140);txt(50,708,'16 November 2026',14,'Mono',MUTED);end()
#02 / POSITIONING
base('The project is the introduction.');eyebrow(50,105,'Positioning');txt(50,145,'The project is\nthe introduction.',74,'Serif');para(53,330,'A photograph. A short motion piece. A story in boards. The same maker can need all three.',500,24,'Serif',leading=1.25,maxh=150);para(53,475,'LOOKSi is the photo and video editor Brady is building around deliberate creative decisions - with room for the boards and layouts that grow around the work.',500,20,maxh=150);pic(cover('02'),670,105,395,495);eyebrow(680,630,'The promise to test');para(680,662,'Useful control for work to deliver.\nA clear place to begin and explore.',420,20,maxh=80);source(53,704,'Basis: current photo/Timeline/board surfaces and actual dated outputs. This is positioning, not a comparative speed or quality claim.',520);end()
#03 / AUDIENCE JOBS
base('Start with the job, not a skill label.');eyebrow(50,100,'Audience');txt(50,140,'Start with the job,\nnot a skill label.',68,'Serif');pic(ROOT/'media/previews/IMG_4870.jpg',960,110,190,570,'cover')
segments=[('01','THE HYBRID ASSIGNMENT','A small project needs a photograph, a short cut and social context.','Need: inspect actual outputs and the decisions behind them.'),('02','THE PHOTO WORTH KEEPING','The subject is right; the light and color do not yet feel like the intended moment.','Need: a reason for the adjustment and time to compare.'),('03','THE PERSONAL EXPERIMENT','The maker wants to try a treatment, pull back, and decide when to stop.','Need: room to revise without making the process feel foolish.'),('04','THE TOOL WORTH FOLLOWING','A maker encounters Brady’s work and asks whether the project shares their taste.','Need: a person, a point of view and a real artifact.')]
for i,(n,title,trigger,need) in enumerate(segments):
 x=50+(i%2)*455;y=340+(i//2)*185;line(x,y,x+410,y);txt(x,y+20,n,36,'Serif',BLUE);eyebrow(x+65,y+22,title);para(x+65,y+55,trigger,345,17,maxh=80);para(x+65,y+123,need,345,14,col=MUTED,maxh=60)
end()
#04 / PLATFORM INDEX
base('Five entrances. One maker.');eyebrow(50,103,'The campaign system');txt(50,143,'Five entrances. One maker.',67,'Serif');names=[('01','The letter','Meet the builder','32s / paper + photography'),('02','The casebook','Inspect the outputs','42s / mixed-session proof'),('03','The recipe','Understand a decision','33s / actual endpoints'),('04','The joke','Recognize the habit','24s / original graphic story'),('05','The manifesto','Share a point of view','36s / photographic film')]
for j,(i,title,job,fmt) in enumerate(names):
 x=50+j*225;panel=slide(i,{'01':4,'02':4,'03':3,'05':3}[i]) if i!='04' else cover(i);pic(panel,x,270,190,237.5);txt(x,533,f'{i} / {title}',28,'Serif');para(x,581,job,190,17,maxh=70);para(x,638,fmt,190,12,'Mono',MUTED,maxh=70)
source(50,716,'Selected actual carousel panels and the graphic campaign cover. Complete sequences remain in each campaign folder.')
end()
#05 /01 PLATFORM
base('01 / I wanted to make this.');pic(cover('01'),45,112,385,481);eyebrow(476,110,'01 / Candid builder letter');txt(473,149,'I wanted\nto make this.',73,'Serif');para(477,329,'The person and the work arrive together. A finished letter film invites a specific project, without waiting for a face-to-camera shoot.',660,21,maxh=110);eyebrow(477,452,'Ready caption');para(477,480,caption_for('01'),660,16,leading=1.25,maxh=230);rect(50,628,5,42,BLUE);para(68,628,'Ask for a real project to put through the tool.',340,17,maxh=63);source(50,702,'Reddit: first-person builder context and a useful project question. Instagram: finished letter, actual work and a clear invitation. Founder performance is optional.');end()
#06 /01 DIRECTION
base('01 / Letter film direction');eyebrow(50,95,'32 seconds / open text / original sound');txt(50,130,'A letter, already complete.',59,'Serif');para(50,205,'The work carries the invitation. Founder filming is a separate optional version.',900,17,maxh=40);pic(board('01'),1000,115,150,108)
for j,(sec,cap) in enumerate([(2,'00-04 / The letter'),(6,'04-08 / Native exports,16Nov'),(14,'12-16 / App archive,25Oct'),(18,'16-20 / Native board,16Nov')]):
 frame=HERE/'frames'/f'01-film-{sec:02d}.png'
 if not frame.exists():subprocess.run(['ffmpeg','-v','error','-y','-ss',str(sec),'-i',str(ROOT/'campaign01/video/01-builder-letter.mp4'),'-frames:v','1','-vf','scale=540:-1',str(frame)],check=True)
 x=50+j*285;pic(frame,x,260,240,427);para(x,704,cap,240,12,'Mono',MUTED,maxh=38)
source(50,735,'Selected actual film frames. Native photo/board exports:16Nov2026. Genuine app recording:25Oct2026. Full shotboard and optional founder script remain in campaign01.');end()
#07 /02 PLATFORM
base('02 / NEPAL. In three forms.',True);eyebrow(50,104,'02 / Professional casebook','#B6C0B7');txt(47,145,'NEPAL.\nIn three forms.',80,'Serif',WHITE);para(50,354,'A photo treatment. A 12-second motion study. A story in six boards.',570,25,'Serif',WHITE,maxh=120);para(50,490,caption_for('02'),570,17,'Sans',WHITE,1.25,maxh=220);pic(cover('02'),744,108,360,450)
eyebrow(50,662,'Channel treatment','#B6C0B7');para(50,687,'Instagram: output-led film and carousel. Reddit: a builder-disclosed case with actual files and a specific inspection question.',570,14,'Sans',WHITE,1.25,maxh=55)
for j,(a,b,cdate) in enumerate([('PHOTO','Actual native treatment','16 Nov 2026'),('MOTION','Native Timeline output','2 Nov 2026'),('BOARDS','Six native PNG exports','16 Nov 2026')]):
 x=690+j*158;line(x,601,x+140,601,'#53645B');txt(x,620,a,14,'Mono','#9ECAFF');para(x,653,b,140,14,col=WHITE,maxh=50);txt(x,715,cdate,11,'Mono','#B6C0B7')
end()
#08 /02 DIRECTION
base('02 / Casebook direction');eyebrow(50,95,'42 seconds / output-led / six story beats');txt(50,130,'Let the files carry the case.',58,'Serif');pic(board('02'),50,225,1100,480);source(50,709,'Main film: a prism/brand still from the completed12-second native Timeline export (2Nov). Full dated source companion retained. Photo/boards:16Nov. This is a mixed-session casebook; optional continuous current proof remains separate.');end()
#09 /03 PLATFORM
base('03 / Keep the gold. Keep the quiet blues.');eyebrow(50,103,'03 / Welcoming recipe');txt(47,145,'Keep the gold.\nKeep the quiet blues.',65,'Serif');para(50,335,caption_for('03'),630,18,leading=1.3,maxh=250);pic(cover('03'),776,117,350,438)
eyebrow(50,533,'Channel treatment');para(50,560,'Instagram: a saveable recipe and matched comparison. Reddit: a self-contained lesson that asks where the explanation needs to slow down.',630,16,maxh=65)
for j,(a,b) in enumerate([('5900 K','Temperature'),('+12%','Contrast'),('+0.15 EV','Exposure')]):x=50+j*365;line(x,637,x+325,637);txt(x,655,a,38,'Serif');txt(x,707,b,14,'Mono',MUTED)
end()
#10 /03 DIRECTION
base('03 / Recipe direction');eyebrow(50,95,'33 seconds / same-source endpoints / no simulated controls');txt(50,130,'Three choices. One combined result.',54,'Serif');pic(board('03'),50,225,1100,480);source(50,709,'Actual 16 Nov native PNG exports from the same camera-JPEG-preview source. The comparison shows the combined treatment; it does not isolate each control or claim a continuous screen recording.');end()
#11 /04 PLATFORM
base('04 / The last tweak. Again.',True);pic(cover('04'),45,115,440,550);eyebrow(550,110,'04 / Playful editing habit','#B6C0B7');txt(547,151,'The last tweak.',73,'Serif',WHITE);txt(547,241,'Again.',89,'Italic',RED);para(550,365,'A tiny nudge becomes another one. An original poster is the recurring character, and the final extra pixel is the payoff.',590,23,'Serif',WHITE,maxh=125);eyebrow(550,515,'Ready caption','#B6C0B7');para(550,546,caption_for('04'),590,17,'Sans',WHITE,1.3,maxh=170);source(50,708,'Instagram: original graphic micro-story. Reddit: disclose the builder and ask about a real creative habit. The poster is campaign artwork, not captured app output.',1070,'#B6C0B7');end()
#12 /04 DIRECTION
base('04 / Comic timing');eyebrow(50,95,'24 seconds / three hooks / original mastered sound');txt(50,130,'Let the pause do the work.',59,'Serif');pic(board('04'),50,225,1100,480);source(50,709,'Three openings share the same story body. No founder filming or product capture is required. Graphic cursor/crop motifs are illustrative; no version-management or time-savings claim.');end()
#13 /05 PLATFORM
base('05 / Make room for your eye.',True);pic(cover('05'),715,98,410,512.5);eyebrow(50,110,'05 / Photographic manifesto','#B6C0B7');txt(47,153,'Make room\nfor your eye.',79,'Serif',WHITE);para(50,353,'The observation comes before the software. Photography, a genuine tool moment, and a board give the point of view something real to rest on.',600,23,'Serif',WHITE,maxh=145);eyebrow(50,518,'Ready caption','#B6C0B7');para(50,548,caption_for('05'),600,17,'Sans',WHITE,1.3,maxh=170);txt(50,719,'Instagram: visual film. Reddit: maker process + source context.',12,'Mono','#B6C0B7');source(735,660,'The light interlude is original campaign art. The recorded tool moment is visibly historical. The board is an actual native export.',365,'#B6C0B7');end()
#14 /05 DIRECTION
base('05 / Editorial film direction');eyebrow(50,95,'36 seconds / photographic rhythm / actual product moment');txt(50,130,'The work stays in the frame.',59,'Serif');pic(board('05'),50,225,1100,480);source(50,709,'Original source photographs, genuine dated tone-curve footage and native board output. Procedural RGB light connects the sequences as campaign artwork; it is not product-rendering proof.');end()
#15 /PROOF LEDGER
base('What each artifact proves.');eyebrow(50,103,'Evidence / keep the scopes visible');txt(47,145,'What each artifact proves.',65,'Serif');rows=[('16 NOV','PHOTO ENDPOINTS','Actual native before/chosen PNG exports,1800x1200, embedded profile.','Combined5900K/+12%contrast/+0.15EV treatment. No isolated-control response or continuous recording claim.'),('16 NOV','SIX BOARDS','Actual1920x1536 PNGs from NEPAL / NEAR & FAR. Delivery manifest retained.','An export proves the delivered files. It does not alone prove fresh editable text authoring.'),('2 NOV','12-SECOND MOTION','Native 12-second Timeline export; film uses a prism still. Full source and receipt retained.','Output from2Nov, across a different session. The full source companion is separate from the main film’s still. No fresh all-workflow run implied.'),('25 OCT','NATIVE APP ACTION','Genuine tone-curve recording from the build archive, dated on-screen.','Historical interface behavior, not a current live recording. Separate from current mountain treatment.'),('CAMPAIGN','ILLUSTRATION + SOUND','Original graphic poster, procedural RGB light and sample-free synthesized sound.','Authored campaign assets; not software outputs, founder performances or benchmarks.')]
y=259
for date,kind,what,limit in rows:
 line(50,y,1150,y);txt(50,y+20,date,20,'Mono',BLUE);eyebrow(190,y+20,kind);para(190,y+49,what,440,16,leading=1.2,maxh=67);para(680,y+25,limit,455,16,leading=1.2,maxh=85);y+=96
source(50,735,'Full source hashes, original paths and receipts: media/provenance.json and campaign-specific provenance / QA records.');end()
#16 /RESEARCH
base('Borrow the mechanism, make the work.');eyebrow(50,103,'Research / five reference mechanisms');txt(47,145,'Borrow the mechanism.\nMake the work.',64,'Serif');research=json.loads((ROOT/'research/research-v2.json').read_text());cases={x['id']:x for x in research['cases']};refs=[('R01','A disclosed maker + a specific creative task.','Use a concrete job before introducing breadth.'),('R13','An actual feature tied to a practical release.','Let the operation and output carry the professional case.'),('R11','A learning path connected to first results.','Teach an intention, a visible action and a comparison.'),('R14','A playful release voice tied to the tool.','Give the joke a product reason without insulting the craft.'),('R15','Human attention, expressive footage and a tool moment.','Build the manifesto around observed work.')]
y=335
for rid,obs,lesson in refs:
 r=cases[rid];line(50,y,1150,y);txt(50,y+17,rid,19,'Mono',BLUE);title=clean(r['title']);txt(133,y+16,title,21,'Serif');para(947,y+17,'SOURCE / '+clean(r['source_date']),190,10,'Mono',MUTED,1.1,maxh=35);C.linkURL(r['url'],(133,H-y-45,1125,H-y-10),relative=0,thickness=0);para(133,y+46,obs+' '+lesson,1000,15,maxh=45);y+=75
source(50,720,'20 purposive case records; qualitative mechanisms, not conversion winners. Native Instagram post access was not established. Full dated source dossier: research/research-v2.md.');end()
#17 /LAUNCH PLAN
base('Two weeks of useful learning.');eyebrow(50,103,'Working release sequence / subject to readiness');txt(47,145,'Two weeks of useful learning.',63,'Serif');slots=[('01','DAY 1','The letter','Introduce Brady and ask for a real project.'),('03','DAY 3','The recipe','Offer a saveable decision and ask the next learning question.'),('04','DAY 5','The joke','Invite specific revision habits; keep recognition separate from demand.'),('02','DAY 8','The casebook','Let people inspect the actual files and ask an operational question.'),('05','DAY11','The manifesto','Consolidate the point of view; invite a relevant follow.'),('REPLY','DAY14','A real answer','Make a short response to one actual audience question.')]
for j,(cid,day,title,body) in enumerate(slots):
 x=50+(j%3)*375;y=289+(j//3)*196;rect(x,y,330,5,[BLUE,GREEN,RED][j%3]);txt(x,y+25,day,16,'Mono',MUTED);txt(x,y+60,title,32,'Serif');para(x,y+110,body,330,17,maxh=80)
source(50,713,'Observe at24h,72h and7days. Record raw counts and the actual denominator. Compare hooks within one channel; small organic samples provide direction, not causal proof. No posting or paid spend is included.');end()
#18 /PRODUCTION
base('The next shoot is optional, and specific.');eyebrow(50,103,'Production / priorities');txt(47,145,'The next shoot is\noptional, and specific.',65,'Serif');pic(cover('01'),810,110,300,375)
priorities=[('01','REVIEW THE FINISHED WORK','Choose the strongest opening and confirm the caption sounds like Brady. The letter, graphic story and photographic films already have complete executions.'),('02','OPTIONAL FOUNDER SESSION','Allow25minutes: three openings, one40s read, three questions, looking-at-work detail and room tone. Use Brady’s real voice and actual workspace.'),('03','OPTIONAL SLOWER PROOF','A current continuous photo edit and a separate readable board-authoring/export cut. Work on an isolated copy; preserve active documents.'),('04','BEFORE A POST GOES LIVE','Choose the permitted community/placement and the exact ready caption. Confirm source permissions and the actual destination if an access CTA is added.')]
for j,(n,head,body) in enumerate(priorities):
 y=353+j*92;txt(50,y,n,25,'Serif',BLUE);eyebrow(110,y+3,head);para(110,y+29,body,630,16,leading=1.2,maxh=70)
source(823,533,'Shot direction and clean lines: campaign01/self-shoot-director.md. Production tasks and remaining inputs: production-tracker.xlsx.',280);end()
#19 /SPECIFIC IMPROVEMENT
base('V1 to V2: the correction is visible.');eyebrow(50,103,'Creative review / what changed');txt(47,145,'V1 to V2: the correction is visible.',61,'Serif');v1=ROOT.parent/'images/01-image.png'
if v1.exists():pic(v1,50,270,260,325)
else:rect(50,270,260,325,'#DBDFD5');txt(65,300,'V1 archive',24)
pic(cover('01'),355,270,260,325);eyebrow(50,622,'V1 / slogans and future footage');eyebrow(355,622,'V2 / real work and a letter')
changes=[('A reason to care','Audience jobs replace a repeated “photo + video” slogan.'),('A finished execution','The builder letter works now; a founder shoot is a future variant.'),('A task to inspect','Actual output casebook and same-source recipe replace generic UI promises.'),('A useful evidence trail','Source dates, scope, manifests and rejected failed export remain clear.')]
y=268
for h,b in changes:txt(690,y,h,28,'Serif');para(690,y+43,b,450,18,maxh=85);y+=111
source(50,729,'V1 retained for comparison. V2 pairs actual corrected native output with dated product records.');end()
#20 /HANDOFF
base('Review the work. Choose the next question.',True);eyebrow(50,104,'The handoff','#B6C0B7');txt(47,146,'Review the work.\nChoose the next question.',68,'Serif',WHITE);para(50,350,'Five distinct campaigns. A real casebook. A specific learning example. A finished builder letter. Original graphic humor and a photographic point of view.',670,24,'Serif',WHITE,maxh=155);paths=[('CREATIVE REVIEW','Campaign folders / images, carousels, storyboards and MP4s'),('COPY + STRATEGY','strategy.md / copybook.md / execution.json per delivered campaign'),('PRODUCTION','production-tracker.xlsx / founder and capture variants'),('EVIDENCE','media/provenance.json / research dossier / campaign QA')]
y=535
for a,b in paths:eyebrow(50,y,a,'#9ECAFF');para(245,y-2,b,725,16,'Sans',WHITE,maxh=38);y+=47
pic(BOARD5,865,95,285,360);source(882,490,'NEPAL / NEAR & FAR\nFIRST LIGHT\nActual native board export\n16 Nov2026',250,'#B6C0B7');end()
C.save()
# Basic structural bounds, exact source list and page index retained for QA.
viol=[x for x in CHECK if x['right']>W-20 or x['x']<0 or x['bottom']>H-12]
(HERE/'layout-audit.json').write_text(json.dumps({'page_count':PAGE,'page_titles':PAGE_TITLES,'text_bounds_violations':viol,'embedded_image_source_hashes':IMAGE_SOURCE_RECORDS,'source_note':'Actual V2 covers and shotboards; delivered captions read from execution.json or campaign-specific copy records. Literal escaped Unicode is decoded before drawing.'},indent=2)+'\n')
if viol:raise RuntimeError(f'{len(viol)} text bounds violations: {viol[:3]}')
assert PAGE==20
print(f'Wrote{PAGE} designed landscape pages: {OUT}')
