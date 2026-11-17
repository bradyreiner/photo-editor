#!/usr/bin/env python3
"""Six original graphic carousel panels, using the existing campaign04 poster.
Build with marketing/.venv/bin/python PATH/source/build_carousel.py.
Live-text SVGs preserve editable headlines; portable SVGs outline the same fonts.
PNG exports are rendered from portable SVGs through macOS Quick Look.
No product interface, product output or founder performance is represented.
"""
from pathlib import Path
from functools import lru_cache
import base64,html,json,math,shutil,subprocess,tempfile,hashlib
from PIL import Image,ImageFont,ImageDraw
ROOT=Path(__file__).resolve().parents[1]
FONTS=ROOT/'source/fonts'
FONT={'sans':FONTS/'spacegrotesk/SpaceGrotesk[wght].ttf','serif':FONTS/'instrumentserif/InstrumentSerif.ttf','italic':FONTS/'instrumentserif/InstrumentSerif-Italic.ttf','mono':FONTS/'spacemono/SpaceMono-Regular.ttf'}
K='#07080a';P='#f3f3ed';M='#919596';R='#ff3b30';G='#30d158';B='#3282ff'
W,H=1080,1350
TEXT_QA=[]
@lru_cache(64)
def font(size,kind='sans'):
 f=ImageFont.truetype(str(FONT[kind]),int(size))
 if kind=='sans':
  try:f.set_variation_by_axes([600])
  except Exception:pass
 return f

def text(x,y,s,size=48,kind='sans',fill=P,anchor='start',essential=True):
 width=font(size,kind).getlength(s)
 left=x-width/2 if anchor=='middle' else x-width if anchor=='end' else x
 baseline=y+font(size,kind).getmetrics()[0]
 TEXT_QA.append({'copy':s,'x':x,'top':y,'baseline':baseline,'width':round(width,2),'size':size,'family':kind,'left':round(left,2),'right':round(left+width,2),'essential':essential})
 return f'<text x="{x}" y="{baseline}" font-family="Campaign{kind}" font-size="{size}" font-weight="{600 if kind=="sans" else 400}" fill="{fill}" text-anchor="{anchor}">{html.escape(s)}</text>'

def fonts():
 return '<style>'+''.join(f'@font-face{{font-family:Campaign{k};src:url(data:font/ttf;base64,{base64.b64encode(v.read_bytes()).decode()}) format("truetype");font-weight:{"100 900" if k=="sans" else "400"};}}' for k,v in FONT.items())+'</style>'

def path(d,color=P,width=2):return f'<path d="{d}" fill="none" stroke="{color}" stroke-width="{width}" stroke-linecap="round" stroke-linejoin="round"/>'
def rect(x,y,w,h,fill=K,stroke=None,width=1):return f'<rect x="{x}" y="{y}" width="{w}" height="{h}" fill="{fill}"'+(f' stroke="{stroke}" stroke-width="{width}"' if stroke else '')+'/>'
def marks(x,y,w,h,color=M,length=25):
 return ''.join(path(f'M{xx+sg*12} {yy} h{sg*length} M{xx} {yy+sy*12} v{sy*length}',color,2) for xx,sg in [(x,-1),(x+w,1)] for yy,sy in [(y,-1),(y+h,1)])
def cursor(x,y,scale=1):
 return f'<g transform="translate({x} {y}) scale({scale})">'+f'<path d="M0 0 L2 52 L15 39 L27 64 L38 58 L25 34 L44 33 Z" fill="{K}" stroke="{P}" stroke-width="3"/></g>'

def poster(index,x,y,w,h,uid):
 # Same native geometry and font positions as campaign04's original poster.
 cx=480+[-9,7,0,13,-5,4][index%6];cy=560+[0,15,-11,4,-14,9][index%6];rr=[305,298,320,288,311,302][index%6]
 out=[f'<g id="poster-{uid}" transform="translate({x} {y}) scale({w/960} {h/1220})">',rect(0,0,960,1220,P),rect(54,54,852,1112,'none','#d4d6d0',2)]
 for k,col in enumerate([R,G,B]):out.append(f'<circle cx="{cx+(k-1)*38}" cy="{cy+(k-1)*16}" r="{rr-k*14}" fill="{col}"/>')
 ax=cx+rr*math.cos(math.radians(15));ay=cy+rr*math.sin(math.radians(15));bx=cx+rr*math.cos(math.radians(190));by=cy+rr*math.sin(math.radians(190))
 out.append(f'<path d="M{cx} {cy} L{ax} {ay} A{rr} {rr} 0 0 1 {bx} {by} Z" fill="{K}"/>')
 out.append(f'<circle cx="{cx}" cy="{cy}" r="145" fill="{P}"/>');out.append(rect(cx-30,cy-365,60,730,P))
 for off in range(-190,191,20):out.append(path(f'M140 {cy+off} L815 {cy+off-145}','#404143',2))
 out.append(path('M54 899 L906 899',K,2))
 # Poster microtype is graphic texture, not the panel's essential copy.
 out.extend([text(74,82,'AFTERIMAGE',40,'sans',K,essential=False),text(72,928,'Almost',110,'serif',K,essential=False),text(72,1032,'there.',110,'serif',K,essential=False),text(832,1050,f'{index+1:02}',28,'mono',K,essential=False),'</g>'])
 return ''.join(out)

def furniture(n):
 out=[text(72,56,'LOOKSi / PHOTO + VIDEO',27,'mono',M,essential=False),text(1008,56,f'{n:02} / 06',27,'mono',M,'end',False),path('M72 1260 H1008','#36383b',1),text(72,1280,'A CREATOR’S RITUAL',24,'mono',M,essential=False)]
 for x,col in [(954,R),(974,G),(994,B)]:out.append(f'<circle cx="{x}" cy="1300" r="5" fill="{col}"/>')
 return out

def slide(n):
 out=furniture(n)
 if n==1:
  out.extend([text(70,156,'The last',132),text(70,310,'tweak.',132),path('M76 473 L622 447',R,7),text(594,437,'Again.',108,'italic',R)])
  out.append('<g transform="rotate(5 737 840)">'+poster(3,510,562,420,534,'cover')+'</g>')
  out.extend([text(72,860,'ONE FINAL',34,'mono',M),text(72,910,'TINY CHANGE.',34,'mono',M),text(72,1138,'You know how this goes.',58,'serif'),cursor(905,1030,.85)])
 elif n==2:
  out.extend([text(72,154,'A little',112),text(72,279,'right.',112),text(700,328,'…or left.',68,'italic',R)])
  out.extend([path('M540 455 V1140','#36383b',2),rect(310,478,450,572,'none','#45494b',2),poster(0,326,478,450,572,'right'),marks(326,478,450,572),cursor(757,976,.85)])
  out.extend([path('M112 746 H290 M112 746 l34 -24 M112 746 l34 24',R,5),path('M806 746 H970 M970 746 l-34 -24 M970 746 l-34 24',G,5),text(72,1130,'Just checking.',70,'italic'),text(1008,1145,'+12 px',42,'mono',G,'end')])
 elif n==3:
  out.extend([text(72,150,'Maybe another',94),text(72,259,'crop.',118),'<defs><clipPath id="crop-window"><rect x="72" y="455" width="936" height="610"/></clipPath></defs>'])
  out.append('<g clip-path="url(#crop-window)">'+poster(2,-20,302,1120,1424,'crop')+'</g>')
  out.extend([marks(72,455,936,610,P,38),path('M75 442 V420 H290 M1008 1098 V1150 H790',R,6),text(72,1130,'or… this?',88,'italic',R),cursor(970,1010,1)])
 elif n==4:
  out.extend([text(72,150,'A few options.',98),text(72,276,'Yes, they’re different.',68,'italic',R)])
  for i in range(6):
   x=84+(i%3)*316;y=410+(i//3)*403
   out.append(poster(i,x,y,280,356,f'option-{i}'))
   out.append(text(x+140,y+363,f'ALMOST {i+1:02}',25,'mono',M,'middle',False))
  out.extend([f'<ellipse cx="224" cy="991" rx="165" ry="205" transform="rotate(-8 224 991)" fill="none" stroke="{R}" stroke-width="5"/>',cursor(285,1070,.8)])
 elif n==5:
  out.extend([text(72,148,'One more',112,'italic',R),text(72,270,'pixel.',134,'italic',R),poster(3,265,465,450,572,'pixel'),marks(265,465,450,572),cursor(698,958,.95)])
  out.extend([path('M730 977 Q843 968 875 1011',R,3),rect(780,1003,210,210,K,'#686c6d',2)])
  for k in range(1,7):
   out.append(path(f'M{780+30*k} 1003 V1213','#303438',1));out.append(path(f'M780 {1003+30*k} H990','#303438',1))
  out.append(rect(870,1093,30,30,R))
  out.extend([text(885,1043,'1 px',42,'mono',P,'middle'),text(72,1125,'You get it.',76,'serif')])
 else:
  out.extend([text(72,160,'I’m building',72),text(64,255,'LOOKSi.',175),text(72,487,'Photo + video.',76),text(72,645,'For people who care',57,'serif'),text(72,710,'how it looks.',57,'serif'),path('M72 847 H1008','#45484c',2),text(72,919,'What’s your',84),text(72,1017,'“last tweak”?',96,'italic',R)])
  for x,col in [(928,R),(951,G),(974,B)]:out.append(f'<circle cx="{x}" cy="1190" r="23" fill="none" stroke="{col}" stroke-width="2"/>')
 return out

COPY=[
 {'headline':'The last tweak. Again.','body':'One final tiny change. You know how this goes.','alt':'On graphite black, large white type reads The last tweak, with a red correction adding Again. A tilted cream poster with overlapping RGB circular geometry is the recurring graphic. The lower line reads You know how this goes. This is original campaign artwork, not product output.'},
 {'headline':'A little right.','body':'…or left. Just checking. +12 px','alt':'The same original cream poster is shifted beside a vertical registration guide. Red and green arrows point in opposite directions. Large type reads A little right, then …or left. The bottom reads Just checking and +12 px. This is a graphic editing joke rather than an app interface.'},
 {'headline':'Maybe another crop.','body':'or… this?','alt':'An oversized crop of the recurring RGB poster fills a wide window, cutting through its typography and circular image. White crop marks and red alternate corners surround it. The words Maybe another crop are followed by or… this? The crop is an original campaign illustration.'},
 {'headline':'A few options.','body':'Yes, they’re different.','alt':'Six subtly different versions of the original cream RGB poster are arranged in two rows of three, labeled Almost 01 through Almost 06. A loose red ellipse selects the fourth. The large heading reads A few options, followed by Yes, they’re different.'},
 {'headline':'One more pixel.','body':'You get it.','alt':'The selected original RGB poster returns with crop marks and a cursor. A red leader points to an illustrative magnified grid with one red square and the label 1 px. Large red italic type reads One more pixel; a lower line says You get it. The grid is a joke prop and represents no product feature.'},
 {'headline':'I’m building LOOKSi.','body':'Photo + video. For people who care how it looks. What’s your “last tweak”?','alt':'A clear typographic closing panel says I’m building LOOKSi, Photo + video, and For people who care how it looks. A red italic question asks What’s your last tweak? Three thin RGB circles sign the panel.'}
]

def build():
 ROOT.mkdir(exist_ok=True);(ROOT/'qa').mkdir(exist_ok=True)
 binary=ROOT/'source/outline_svg'
 subprocess.run(['clang','-fobjc-arc','-fblocks','-framework','Foundation','-framework','CoreText','-framework','CoreGraphics',str(ROOT/'source/outline_svg.m'),'-o',str(binary)],check=True,capture_output=True)
 allqa=[];wrappers=[]
 for i,c in enumerate(COPY,1):
  TEXT_QA.clear();parts=slide(i)
  content='\n'.join(parts)
  svg=f'<svg xmlns="http://www.w3.org/2000/svg" width="1080" height="1350" viewBox="0 0 1080 1350"><title>{html.escape(c["headline"])}</title><desc>{html.escape(c["alt"])}</desc>{fonts()}{rect(0,0,W,H,K)}{content}</svg>'
  live=ROOT/f'slide-{i:02}-editable-text.svg';portable=ROOT/f'slide-{i:02}.svg'
  live.write_text(svg)
  subprocess.run([str(binary),str(live),str(portable),str(FONTS)],check=True,capture_output=True)
  qa=[dict(x,slide=i) for x in TEXT_QA if x['essential']]
  for x in qa:
   if x['left']<44 or x['right']>1038 or x['baseline']>1245:raise RuntimeError(f'Text outside safe layout: {x}')
  allqa.extend(qa)
  # Quick Look square previews otherwise crop portrait SVGs. A square wrapper
  # keeps native 1:1 coordinates; only the unused right gutter is removed.
  outlined=portable.read_text();a=outlined.index('>')+1
  if outlined.startswith('<?xml'):a=outlined.index('>',a)+1
  import re
  inner=re.sub(r'^.*?<svg\b[^>]*>','',outlined,flags=re.S).rsplit('</svg>',1)[0]
  wrapper=ROOT/'qa'/f'slide-{i:02}-render-wrapper.svg'
  wrapper.write_text('<svg xmlns="http://www.w3.org/2000/svg" width="1350" height="1350" viewBox="0 0 1350 1350">'+rect(0,0,1350,1350,K)+inner+'</svg>')
  wrappers.append(wrapper)
  import xml.etree.ElementTree as ET
  nodes=ET.fromstring(svg).findall('.//{http://www.w3.org/2000/svg}text')
  c.update(number=i,png=f'slide-{i:02}.png',svg=f'slide-{i:02}.svg',editable_text_svg=f'slide-{i:02}-editable-text.svg',on_screen_text=[n.text or '' for n in nodes])
 (ROOT/'qa/text-bounds.json').write_text(json.dumps(allqa,indent=2)+'\n')
 (ROOT/'carousel.json').write_text(json.dumps(dict(campaign_id='04',format='Instagram portrait carousel',width=W,height=H,slide_count=6,publication_status='Review-ready artwork; not published',caption='The last tweak. Again.\n\nA tiny nudge. Another crop. Six options that are definitely different.\n\nI’m building LOOKSi, a photo + video editor for professionals and hobbyists who care how it looks.\n\nWhat’s your “last tweak” — the thing you always go back for?',slides=COPY,provenance='Original procedural vector artwork matches campaign04’s recurring poster. No reference creative, stock art, app interface, app output, founder likeness or product-action claim. The editable-text SVGs embed the bundled OFL fonts; portable SVGs outline those fonts for exact rendering.',source_builder='source/build_carousel.py',render_pipeline='Native SVG geometry and live type → CoreText font outlines → Quick Look square wrapper at1350px → remove unused right gutter to1080×1350. PNG typography derives from the same outlined SVG master.',remaining='Final creative/caption/placement review only; no founder filming or product capture required.'),indent=2,ensure_ascii=False)+'\n')
 (ROOT/'qa/render-list.json').write_text(json.dumps([str(x) for x in wrappers],indent=2))
 print('Wrote six editable masters and portable SVGs. Run --render after Quick Look permits rendering.')

def render():
 wrappers=[ROOT/'qa'/f'slide-{i:02}-render-wrapper.svg' for i in range(1,7)]
 if not all(w.with_name(w.name+'.png').exists() and w.with_name(w.name+'.png').stat().st_mtime>=w.stat().st_mtime for w in wrappers):
  subprocess.run(['qlmanage','-t','-s','1350','-o',str(ROOT/'qa'),*[str(x) for x in wrappers]],check=True)
 phone=[];qa=[]
 for i,w in enumerate(wrappers,1):
  rendered=Image.open(w.with_name(w.name+'.png')).convert('RGB')
  if rendered.size!=(1350,1350):raise RuntimeError(rendered.size)
  final=rendered.crop((0,0,W,H));final.save(ROOT/f'slide-{i:02}.png')
  preview=final.resize((360,450),Image.Resampling.LANCZOS);preview.save(ROOT/'qa'/f'slide-{i:02}-phone-360.png');phone.append(preview)
  qa.append(dict(slide=i,size=list(final.size),sha256=hashlib.sha256((ROOT/f'slide-{i:02}.png').read_bytes()).hexdigest(),font_parity='PNG rasterized from portable outlined SVG, so no font substitution in export'))
 contact=Image.new('RGB',(1080,900),K)
 for i,im in enumerate(phone):contact.paste(im,((i%3)*360,(i//3)*450))
 contact.save(ROOT/'qa/phone-contact-sheet.png')
 (ROOT/'qa/export-checks.json').write_text(json.dumps(qa,indent=2)+'\n')
 print('Rendered six1080×1350 PNGs and six360px phone previews; contact sheet ready.')

if __name__=='__main__':
 import argparse
 p=argparse.ArgumentParser();p.add_argument('--render',action='store_true');a=p.parse_args()
 render() if a.render else build()
