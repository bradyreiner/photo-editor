#!/usr/bin/env python3
"""Editable campaign graphics and motion drafts. Run with marketing/.venv/bin/python.

Original graphic design; abstract artwork is identified in artwork/provenance.json.
This renderer never reconstructs the product UI or invents editing results.
"""
from pathlib import Path
import base64
import html
import json
import math
import subprocess
import wave
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageOps

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parents[2]
FONTS = ROOT / 'artwork/fonts'
if not FONTS.exists():
    FONTS = REPO / 'apps/desktop/assets/fonts'
FONT_PATHS = {
    'sans': FONTS / 'spacegrotesk/SpaceGrotesk[wght].ttf',
    'serif': FONTS / 'instrumentserif/InstrumentSerif.ttf',
    'heavy': FONTS / 'archivoblack/ArchivoBlack-Regular.ttf',
    'mono': FONTS / 'spacemono/SpaceMono-Regular.ttf',
}
INK, PAPER, RED, GREEN, BLUE = '#050505', '#F3F3F3', '#FF3B30', '#30D158', '#3282FF'
FPS, SECONDS = 24, 18
ART = ROOT / 'artwork/additive-light.png'
DATA = [
 dict(id='01', name='A note from the builder', tone='Candid / human / curious',
      bg='#F3F3EE', fg=INK, accent=BLUE, font='sans',
      head=['I built this.', "I’d love your", 'honest take.'],
      sub=['An editor I’m building for people', 'who care about photos and video.'],
      cta='What would make it better for you?',
      shots=[('00–04', 'The person', 'Founder to camera. Eye level, natural light.'),
             ('04–12', 'The reason', 'One friction you know. One real app action.'),
             ('12–18', 'The invitation', 'Ask one useful question. Hold for a reply.')],
      need='Founder filming + one current app recording',
      motion=[['I built this.'], ['For the way', 'I want to edit.'], ['Photo. Video.', 'Room for your ideas.'], ['I’m building LOOKSi.', 'What would you change?']],
      labels=['FOUNDER SHOT TO FILM', 'FOUNDER / DEMO TO FILM', 'PRODUCT RECORDING TO ADD', 'GRAPHIC END CARD']),
 dict(id='02', name='Show the work', tone='Precise / considered / professional',
      bg='#101113', fg=PAPER, accent=GREEN, font='sans',
      head=['One source.', 'Show every', 'step.'],
      sub=['A photo + video editor in development.', 'The work should speak for itself.'],
      cta='Which step should I demonstrate next?',
      shots=[('00–04', 'Show the source', 'Original frame. Same source across the demo.'),
             ('04–12', 'Show the decision', 'One adjustment, then one real timeline action.'),
             ('12–18', 'Show the file', 'Export, open the result, invite a closer look.')],
      need='Current continuous app capture + opened export',
      motion=[['One source.'], ['A deliberate edit.'], ['Then the', 'exported result.'], ['I’m building LOOKSi.', 'Tell me your workflow.']],
      labels=['DEMO RECORDING TO ADD', 'DEMO RECORDING TO ADD', 'EXPORT RECORDING TO ADD', 'GRAPHIC END CARD']),
 dict(id='03', name='Start with your eye', tone='Welcoming / useful / encouraging',
      bg='#EDF2F7', fg='#101821', accent=BLUE, font='sans',
      head=['Your eye.', 'Your first', 'edit.'],
      sub=['A little curiosity goes a long way.', 'Let’s make something you like.'],
      cta='What part of editing feels confusing?',
      shots=[('00–04', 'Start small', 'Pick one photo. Name one thing you want to try.'),
             ('04–12', 'Make one choice', 'Show a crop and a light adjustment slowly.'),
             ('12–18', 'Keep exploring', 'Show the result. Invite a beginner question.')],
      need='Beginner-paced app capture; voice optional',
      motion=[['Like the photo.', 'Wonder about the edit?'], ['Start with', 'one choice.'], ['Then see', 'what changes.'], ['I’m building LOOKSi.', 'Come learn with me.']],
      labels=['GRAPHIC OPENING', 'TUTORIAL CAPTURE TO ADD', 'TUTORIAL CAPTURE TO ADD', 'GRAPHIC END CARD']),
 dict(id='04', name='The final final club', tone='Playful / knowing / lightly irreverent',
      bg='#080808', fg=PAPER, accent=RED, font='heavy',
      head=['final_final_', 'actuallyfinal.'],
      sub=['If you know, you know.', 'I’m building toward a better editing day.'],
      cta='Tell me your most chaotic file name.',
      shots=[('00–04', 'The recognition', 'Three revision names pile up. No app claims.'),
             ('04–12', 'The confession', '“I know this feeling. So I’m building.”'),
             ('12–18', 'The conversation', 'LOOKSi. Photo + video. Ask for file-name lore.')],
      need='Graphic animation only; no founder filming',
      motion=[['final_final.'], ['final_final_v7.'], ['I know the feeling.', 'So I’m building.'], ['LOOKSi. Photo + video.', 'Tell me your file-name lore.']],
      labels=['TYPE ANIMATION', 'TYPE ANIMATION', 'TYPE ANIMATION', 'GRAPHIC END CARD']),
 dict(id='05', name='A point of view', tone='Cinematic / editorial / personal',
      bg=INK, fg=PAPER, accent=BLUE, font='serif',
      head=['It starts', 'with how', 'you see.'],
      sub=['Photo. Video. Your point of view.', 'An editor I’m building around the work.'],
      cta='Follow the build.',
      shots=[('00–04', 'Light becomes form', 'Original optical artwork. Quiet, slow reveal.'),
             ('04–12', 'A point of view', 'Type and RGB light. A personal creative premise.'),
             ('12–18', 'The signature', 'LOOKSi. Photo + video. Follow the build.')],
      need='Original artwork + motion graphics; no filming',
      motion=[['It starts', 'with how you see.'], ['A frame.', 'A feeling.'], ['Photo. Video.', 'Your point of view.'], ['LOOKSi.', 'Follow the build.']],
      labels=['OPTICAL ARTWORK', 'OPTICAL ARTWORK', 'TYPE / LIGHT', 'GRAPHIC END CARD']),
]

for folder in ['boards', 'images', 'videos', 'audio', 'qa']:
    (ROOT / folder).mkdir(parents=True, exist_ok=True)

FC = {}
def font(size, kind='sans'):
    key = (round(size), kind)
    if key not in FC:
        FC[key] = ImageFont.truetype(str(FONT_PATHS[kind]), round(size))
    return FC[key]

def rgb(value):
    return tuple(bytes.fromhex(value.lstrip('#')))

class Canvas:
    """A single layout writes raster pixels and editable SVG elements together."""
    def __init__(self, w, h, bg):
        self.w, self.h = w, h
        self.im = Image.new('RGB', (w, h), bg)
        self.d = ImageDraw.Draw(self.im)
        self.parts = [f'<rect width="{w}" height="{h}" fill="{bg}"/>']

    def rect(self, box, fill, outline=None, width=1, radius=0):
        x, y, x2, y2 = box
        self.d.rounded_rectangle(box, radius=radius, fill=fill, outline=outline, width=width)
        self.parts.append(f'<rect x="{x}" y="{y}" width="{x2-x}" height="{y2-y}" rx="{radius}" fill="{fill or "none"}" stroke="{outline or "none"}" stroke-width="{width}"/>')

    def line(self, xy, fill, width=1):
        self.d.line(xy, fill=fill, width=width)
        self.parts.append(f'<polyline points="{" ".join(f"{xy[i]},{xy[i+1]}" for i in range(0,len(xy),2))}" fill="none" stroke="{fill}" stroke-width="{width}"/>')

    def circle(self, x, y, r, fill):
        self.d.ellipse((x-r, y-r, x+r, y+r), fill=fill)
        self.parts.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="{fill}"/>')

    def text(self, xy, text, size, fill, kind='sans'):
        x, y = xy
        f = font(size, kind)
        if x + f.getlength(text) > self.w-8:
            raise ValueError(f'Text exceeds canvas: {text}')
        self.d.text((x, y), text, font=f, fill=fill, anchor='lt')
        self.parts.append(f'<text x="{x}" y="{y}" font-family="{kind}" font-size="{round(size)}" dominant-baseline="text-before-edge" fill="{fill}">{html.escape(text)}</text>')

    def artwork(self, box):
        x, y, x2, y2 = map(round, box)
        art = Image.open(ART).convert('RGB')
        self.im.paste(ImageOps.fit(art, (x2-x, y2-y)), (x, y))
        self.d = ImageDraw.Draw(self.im)
        b64 = base64.b64encode(ART.read_bytes()).decode()
        self.parts.append(f'<svg x="{x}" y="{y}" width="{x2-x}" height="{y2-y}" viewBox="0 0 {x2-x} {y2-y}"><image width="{x2-x}" height="{y2-y}" preserveAspectRatio="xMidYMid slice" href="data:image/png;base64,{b64}"/></svg>')

    def save(self, stem):
        self.im.save(stem.with_suffix('.png'))
        faces = ''.join(f'@font-face{{font-family:{k};src:url(data:font/ttf;base64,{base64.b64encode(v.read_bytes()).decode()})}}' for k,v in FONT_PATHS.items())
        stem.with_suffix('.svg').write_text(f'<svg xmlns="http://www.w3.org/2000/svg" width="{self.w}" height="{self.h}" viewBox="0 0 {self.w} {self.h}"><style>{faces}</style>{"".join(self.parts)}</svg>')

def signature(c, x, y, fg, size=31):
    c.text((x, y), 'LOOKSi', size, fg)
    for i, color in enumerate([RED, GREEN, BLUE]):
        c.circle(x + size*3.8 + i*10, y+size*.55, 5, color)

def lines(c, texts, x, y, size, fg, kind='sans', leading=1.03):
    for i, text in enumerate(texts):
        c.text((x, y+i*size*leading), text, size, fg, kind)

def fit_size(texts, size, width, kind):
    while max(font(size, kind).getlength(t) for t in texts) > width:
        size -= 1
    return size

def motif(c, p, x, y, w, h, t=0):
    idx = p['id']
    if idx == '01':
        for j, (word, color) in enumerate(zip(['PHOTO', 'VIDEO', 'YOU.'], [RED, GREEN, BLUE])):
            yy = y+j*h/3
            c.line((x, yy, x+w, yy), '#C6C9C9', 1)
            c.text((x, yy+21), f'0{j+1}', 18, color, 'mono')
            c.text((x+80, yy+12), word, round(h/4), p['fg'])
        c.line((x, y+h, x+w, y+h), '#C6C9C9', 1)
    elif idx == '02':
        for j, word in enumerate(['SOURCE', 'DECISION', 'RESULT']):
            yy = y+j*h/3
            c.rect((x, yy, x+w, yy+h/3-16), '#191C20', '#353A40', 2)
            c.text((x+25, yy+21), f'0{j+1}', 19, GREEN, 'mono')
            c.text((x+105, yy+19), word, round(h/12), PAPER)
            c.line((x+w-70, yy+38, x+w-30, yy+38), GREEN, 3)
    elif idx == '03':
        for j, word in enumerate(['Pick a photo.', 'Try one change.', 'See what you like.']):
            yy = y+j*h/3
            c.circle(x+33, yy+35, 29, BLUE)
            c.text((x+24, yy+20), str(j+1), 28, PAPER)
            c.text((x+91, yy+19), word, round(h/13), p['fg'])
    elif idx == '04':
        size = fit_size(['final_final_v7'], round(w*.11), w-86, 'mono')
        for j, word in enumerate(['final', 'final_final', 'final_final_v7']):
            yy = y+j*h/3
            offset = 12*math.sin(t*1.3+j) if t else j*10
            c.rect((x+offset, yy, x+w-24+offset, yy+h/3-14), '#191919', '#484848', 2)
            c.text((x+27+offset, yy+26), word, size, RED if j==2 else PAPER, 'mono')
    else:
        c.artwork((x, y, x+w, y+h))

def poster(p):
    c = Canvas(1080, 1350, p['bg'])
    if p['id']=='05':
        c.artwork((0, 0, 1080, 1350))
        c.rect((0,1050,1080,1350),INK)
    signature(c, 66, 58, p['fg'])
    c.text((66, 135), p['tone'].upper(), 17, p['fg'], 'mono')
    size = fit_size(p['head'], 126 if p['id']!='04' else 105, 950, p['font'])
    lines(c, p['head'], 62, 205, size, p['fg'], p['font'])
    if p['id']!='05':
        motif(c, p, 66, 690, 946, 330)
    lines(c, p['sub'], 66, 1088, 31, p['fg'], leading=1.38)
    c.line((66, 1210, 1014, 1210), p['accent'], 3)
    c.text((66, 1240), p['cta'], 25, p['fg'])
    c.save(ROOT / 'images' / f"{p['id']}-image")
    return c.im

def board(p, image):
    c = Canvas(1920, 1080, p['bg'])
    signature(c, 56, 44, p['fg'], 30)
    c.text((1440, 52), f"DIRECTION {p['id']} / CREATIVE REVIEW", 15, p['fg'], 'mono')
    c.text((56, 128), p['name'], 67, p['fg'], p['font'])
    c.text((59, 215), p['tone'], 26, p['fg'])
    # A type / composition study, not an imitation of a product screen.
    c.rect((56, 290, 676, 990), p['fg'])
    c.rect((58, 292, 674, 988), p['bg'])
    size = fit_size(p['head'], 69, 535, p['font'])
    if p['id']=='05':
        c.artwork((58,292,674,988))
    lines(c,p['head'],90,335,size,p['fg'],p['font'])
    if p['id']!='05':
        motif(c,p,90,640,547,270)
    c.text((740, 294), '18 SECOND FILM / THREE STORY BEATS', 18, p['accent'], 'mono')
    for j,(time,title,detail) in enumerate(p['shots']):
        yy = 351+j*145
        c.line((740,yy-13,1850,yy-13), '#9A9A9A', 1)
        c.text((740, yy+8), time, 21, p['accent'], 'mono')
        c.text((920, yy+4), title, 35, p['fg'])
        c.text((920, yy+57), detail, 24, p['fg'])
    c.text((740, 810), 'PRODUCTION NEEDS', 17, p['accent'], 'mono')
    c.text((740, 851), p['need'], 26, p['fg'])
    c.text((740, 918), 'DELIVERABLES', 17, p['accent'], 'mono')
    c.text((740, 958), '4:5 image  /  9:16 video  /  editable direction board', 24, p['fg'])
    c.save(ROOT/'boards'/f"{p['id']}-board")
    return c.im

def ease(x):
    x = max(0, min(1, x))
    return 1-(1-x)**3

def motion_frame(p, t):
    c = Canvas(1080,1920,p['bg'])
    if p['id']=='05':
        # Layout transform of the unchanged original artwork, not a generated product result.
        drift = round(50*ease(t/SECONDS))
        c.artwork((-drift,0,1080+drift,1920))
        c.rect((0,1360,1080,1920),INK)
    signature(c,76,174,p['fg'],38)
    beat = min(3,int(t/4.5))
    local = t-beat*4.5
    c.text((76,270), f"{p['id']} / {p['name'].upper()}",18,p['fg'],'mono')
    shift = round(60*(1-ease(local/.5)))
    texts = p['motion'][beat]
    size = fit_size(texts, 128 if p['id']!='04' else 105, 928, p['font'])
    lines(c,texts,72,405+shift,size,p['fg'],p['font'],1.1)
    if p['id']!='05':
        motif(c,p,76,900,928,360,t)
    label = p['labels'][beat]
    pending = 'TO ' in label
    if pending:
        c.rect((76,1375,1004,1480), '#25282C' if p['bg'].startswith('#1') else '#DEDFDA')
        c.text((98,1395), 'ANIMATIC / '+label,20, PAPER if p['id']=='02' else INK,'mono')
        c.text((98,1430),'Shot direction and script in campaign briefs.',22,PAPER if p['id']=='02' else INK)
    else:
        c.text((76,1405),'PHOTO + VIDEO / IN DEVELOPMENT',23,p['fg'],'mono')
    if beat==3:
        c.text((76,1530),'For professionals and hobbyists.',28,p['fg'])
    c.line((76,1640,1004,1640), '#7C7C7C',2)
    c.line((76,1640,76+928*(t/SECONDS),1640),p['accent'],5)
    c.text((76,1675),'FOLLOW THE BUILD',22,p['fg'],'mono')
    return c.im

def audio():
    rate=48000
    t=np.arange(rate*SECONDS)/rate
    signal=np.zeros(len(t))
    # Original sample-free sine synthesis, intentionally quiet beneath future dialogue.
    for start in [0,4.5,9,13.5]:
        local=t-start
        env=np.where(local>=0,np.exp(-np.maximum(local,0)/1.6),0)
        env*=np.minimum(np.maximum(local,0)/.07,1)
        for f in [130.81,196.00,261.63]:
            signal+=.035*np.sin(2*np.pi*f*local)*env
    signal*=np.minimum(t/.1,1)*np.minimum((SECONDS-t)/.6,1)
    data=np.stack([signal,signal],axis=1)
    path=ROOT/'audio/original-pulse.wav'
    with wave.open(str(path),'wb') as wav:
        wav.setnchannels(2); wav.setsampwidth(2);wav.setframerate(rate)
        wav.writeframes((data*32767).astype('<i2').tobytes())
    return path

def video(p, audio_path):
    path=ROOT/'videos'/f"{p['id']}-video.mp4"
    cmd=['ffmpeg','-hide_banner','-loglevel','error','-y','-f','rawvideo','-pix_fmt','rgb24','-s','1080x1920','-r',str(FPS),'-i','pipe:0','-i',str(audio_path),'-c:v','libx264','-preset','fast','-crf','19','-pix_fmt','yuv420p','-c:a','aac','-b:a','192k','-t',str(SECONDS),'-movflags','+faststart','-color_primaries','bt709','-color_trc','bt709','-colorspace','bt709',str(path)]
    proc=subprocess.Popen(cmd,stdin=subprocess.PIPE)
    frames=[]
    for n in range(FPS*SECONDS):
        im=motion_frame(p,n/FPS)
        proc.stdin.write(im.tobytes())
        if n in [24,132,240,348]:
            frames.append(ImageOps.contain(im,(270,480)))
    proc.stdin.close()
    if proc.wait()!=0:
        raise RuntimeError(f'ffmpeg failed for {path}')
    strip=Image.new('RGB',(1080,480),INK)
    for j,im in enumerate(frames):strip.paste(im,(270*j,0))
    strip.save(ROOT/'qa'/f"{p['id']}-video-contact.jpg",quality=92)
    print(f'Encoded {path.name}',flush=True)

def hub():
    copy_path=ROOT/'copy.json'
    copy=json.loads(copy_path.read_text()) if copy_path.exists() else None
    sections=[]
    for p in DATA:
        id=p['id']
        status='Animatic: founder / product footage pending' if id in ['01','02','03'] else 'Motion draft: ready for creative review'
        sections.append(f'''<article id="campaign-{id}"><div class="eyebrow">{id} / {html.escape(p['tone'])}</div><h2>{html.escape(p['name'])}</h2><p>{html.escape(p['need'])}</p><div class="pair"><img loading="lazy" src="images/{id}-image.png" alt="{html.escape(' '.join(p['head'])+' '+ ' '.join(p['sub']))}"><video controls preload="metadata" poster="images/{id}-image.png" src="videos/{id}-video.mp4"></video></div><p class="status">{status}. 18s · 1080×1920 · 24fps · original synthesized sound · no spoken voice.</p><div class="links"><a href="boards/{id}-board.png">Direction board</a><a href="boards/{id}-board.svg" download>Editable board SVG</a><a href="images/{id}-image.png" download>Image PNG</a><a href="images/{id}-image.svg" download>Editable image SVG</a><a href="videos/{id}-video.mp4" download>Video MP4</a></div><details><summary>Storyboard + production needs</summary><img src="boards/{id}-board.png" alt="Three shot storyboard for {html.escape(p['name'])}"></details></article>''')
    sources=''
    research=ROOT/'research/release-style-research.md'
    if research.exists():sources=research.read_text()
    briefs=ROOT/'campaign-briefs.md'
    brieftext=briefs.read_text() if briefs.exists() else ''
    source_links=[]
    import re
    for title,url in re.findall(r'\[([^\]]+)\]\((https?://[^)]+)\)',sources):
        if url not in [x[1] for x in source_links]:source_links.append((title,url))
    source_html=''.join(f'<li><a href="{html.escape(url)}">{html.escape(title)}</a></li>' for title,url in source_links)
    doc=f'''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>LOOKSi / Campaign Studio</title><style>
    *{{box-sizing:border-box}}html{{scroll-behavior:smooth}}body{{margin:0;background:#0b0c0d;color:#f3f3f3;font:16px/1.6 -apple-system,BlinkMacSystemFont,sans-serif}}main{{max-width:1260px;margin:auto;padding:60px 30px}}h1{{font-size:clamp(42px,7vw,88px);line-height:1.03;letter-spacing:-3px;font-weight:500;max-width:900px}}h2{{font-size:38px;line-height:1.1;font-weight:500;margin:16px 0}}a{{color:#9bc5ff;text-underline-offset:4px}}.eyebrow{{font:12px/1.5 monospace;letter-spacing:2px;color:#bdc2c7;text-transform:uppercase}}.lede{{font-size:23px;max-width:850px;color:#d3d5d7}}.rgb{{display:flex;width:100px;height:5px;margin:25px 0}}.rgb i{{flex:1;background:#ff3b30}}.rgb i:nth-child(2){{background:#30d158}}.rgb i:nth-child(3){{background:#3282ff}}nav,.links{{display:flex;gap:14px;flex-wrap:wrap;margin:25px 0}}nav a,.links a{{border:1px solid #373c41;padding:9px 14px;border-radius:4px;text-decoration:none}}.note{{padding:22px;border-left:3px solid #3282ff;background:#15181b;max-width:1050px}}article{{border-top:1px solid #363a3e;margin-top:70px;padding-top:35px}}.pair{{display:grid;grid-template-columns:1fr 1fr;gap:28px;align-items:start;margin-top:30px}}.pair img{{width:100%;display:block}}video{{max-height:730px;width:100%;background:#050505}}details{{margin:20px 0;background:#151719;padding:18px}}summary{{cursor:pointer}}details img{{width:100%;margin-top:20px}}.status{{color:#a8becf;font-size:14px}}pre{{white-space:pre-wrap;font:15px/1.75 -apple-system,sans-serif;max-width:1050px}}footer{{color:#999;margin-top:70px}}@media(max-width:680px){{main{{padding:30px 18px}}.pair{{grid-template-columns:1fr}}h1{{letter-spacing:-1px}}}}
    </style><main><div class="eyebrow">Creative development / 16 November 2026</div><div class="rgb"><i></i><i></i><i></i></div><h1>I’m building a better way to make the work.</h1><p class="lede">Five campaign voices for LOOKSi. A photo and video editor for the way professionals and hobbyists want to create.</p><p class="note">Campaign exploration using the requested LOOKSi name and existing additive RGB palette. The application remains Looksy. These are local review assets. Campaigns 01–03 include clearly marked footage slots; 04–05 are complete graphic motion drafts. No interface or product result has been fabricated.</p><nav>{''.join(f'<a href="#campaign-{p["id"]}">{p["id"]} {html.escape(p["name"])}</a>' for p in DATA)}</nav><div class="links"><a href="production-tracker.xlsx" download>Production tracker</a><a href="campaign-briefs.md">Full briefs + scripts</a><a href="copy.json" download>Editable copy</a><a href="research/release-style-research.md">Release research</a><a href="production-playbook.html">Production + rollout plan</a></div>{''.join(sections)}<article id="copy"><h2>Scripts and captions</h2><pre>{html.escape(brieftext)}</pre></article><article id="research"><h2>Reference research</h2><p>Observed examples inform structure and tone. Their engagement is not evidence of what will work for LOOKSi. Instagram adaptations are creative recommendations where direct post inspection was unavailable.</p><ul>{source_html}</ul><details><summary>Read research notes</summary><pre>{html.escape(sources)}</pre></details></article><footer>Original design, bundled project fonts and an original AI optical artwork. The motion bed is sample-free synthesis. Provenance and shot timing accompany the source files. No posts have been published.</footer></main></html>'''
    (ROOT/'index.html').write_text(doc)

def main():
    import argparse
    parser=argparse.ArgumentParser();parser.add_argument('--stills-only',action='store_true');parser.add_argument('--hub-only',action='store_true');parser.add_argument('--video-only',choices=[p['id'] for p in DATA]);args=parser.parse_args()
    if args.video_only:
        p=next(p for p in DATA if p['id']==args.video_only)
        video(p,ROOT/'audio/original-pulse.wav')
        hub()
        return
    if not args.hub_only:
        bed=audio()
        overview=Image.new('RGB',(1620,560),'#181818')
        for j,p in enumerate(DATA):
            im=poster(p);board(p,im)
            thumb=ImageOps.contain(im,(302,410));overview.paste(thumb,(j*324+11,20))
            ImageDraw.Draw(overview).text((j*324+11,451),p['id']+' / '+p['name'],font=font(18),fill=PAPER)
            if not args.stills_only:video(p,bed)
        overview.save(ROOT/'qa/five-directions.jpg',quality=95)
        (ROOT/'shot-timing.json').write_text(json.dumps({'fps':FPS,'seconds':SECONDS,'campaigns':[{'id':p['id'],'name':p['name'],'shots':[{'start':j*4.5,'end':(j+1)*4.5,'screen_text':text,'production_label':p['labels'][j]}for j,text in enumerate(p['motion'])]}for p in DATA]},indent=2))
    hub()

if __name__=='__main__':main()
