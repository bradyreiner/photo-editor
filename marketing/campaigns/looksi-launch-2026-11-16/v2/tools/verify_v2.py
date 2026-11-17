"""Inspect delivered creative, actual media scope and portable artifacts."""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
from html.parser import HTMLParser
from urllib.parse import urlparse,unquote
import hashlib,json,re,subprocess,xml.etree.ElementTree as ET,zipfile
from PIL import Image,ImageOps,ImageDraw,ImageFont
import numpy as np
ROOT=Path(__file__).resolve().parents[1]
J=json.loads((ROOT/'campaigns.json').read_text())
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def check_video(item):
    id,path,silent=item;duration={c['id']:c['video']['duration']for c in J['campaigns']}[id];fps=24 if id=='04' else 30
    probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_streams','-show_format','-of','json',str(path)]));v=next(s for s in probe['streams']if s['codec_type']=='video');audio=[s for s in probe['streams']if s['codec_type']=='audio']
    assert(v['width'],v['height'],v['codec_name'],v['pix_fmt'],v['r_frame_rate'])==(1080,1920,'h264','yuv420p',f'{fps}/1'),path
    assert[v.get(k)for k in['color_range','color_space','color_transfer','color_primaries']]==['tv','bt709','bt709','bt709'],(path,{k:v.get(k)for k in['color_range','color_space','color_transfer','color_primaries']})
    assert abs(float(probe['format']['duration'])-duration)<.06,(path,probe['format']['duration'])
    assert(bool(audio)and audio[0]['codec_name']=='aac'and audio[0]['sample_rate']=='48000')if not silent else not audio,path
    result=subprocess.run(['ffmpeg','-hide_banner','-v','error','-xerror','-threads','2','-i',str(path),'-progress','pipe:1','-f','null','-'],capture_output=True,text=True,check=True)
    count=int(re.findall(r'^frame=(\d+)$',result.stdout,re.M)[-1]);assert count==duration*fps,(path,count)
    return{'path':str(path.relative_to(ROOT)),'sha256':digest(path),'frames':count,'seconds':float(probe['format']['duration']),'fps':fps,'silent':silent,'decode':'passed','color_properties':['tv','bt709','bt709','bt709']}
def main():
    videos=[];stills=[];vectors=[];slides=0
    for c in J['campaigns']:
        id=c['id'];folder=ROOT/f'campaign{id}'
        for key,path in c['assets'].items():assert(ROOT/path).exists(),(id,key,path)
        for name in['hero','hook-b','hook-c']:
            for suffix in['','-silent']:
                p=folder/f'videos/{name}{suffix}.mp4';assert p.exists(),p;videos.append((id,p,bool(suffix)))
        for sub in['images','storyboard','carousel','carousels']:
            for p in sorted((folder/sub).glob('*.png')):
                if 'phone'in p.name:continue
                with Image.open(p)as im:
                    if sub in['carousel','carousels'] or p.name=='cover.png':assert im.size==(1080,1350),(p,im.size)
                    assert bool(im.info.get('icc_profile')),(p,'missing sRGB profile')
                    stills.append({'path':str(p.relative_to(ROOT)),'size':list(im.size),'icc_profile':True})
                if sub in['carousel','carousels']:slides+=1
            for p in sorted((folder/sub).glob('*.svg')):
                node=ET.parse(p).getroot()
                if '-editable-text'not in p.stem:
                    assert not any(n.tag.endswith('text')for n in node.iter()),(p,'portable SVG retains unresolved live type')
                    vectors.append(str(p.relative_to(ROOT)))
    with ThreadPoolExecutor(max_workers=3)as pool:video_checks=list(pool.map(check_video,videos))
    assert slides==31,slides
    source=json.loads((ROOT/'media/provenance.json').read_text())
    for item in source['records']:assert digest(ROOT/item['path'])==item['sha256'],item['path']
    a=ROOT/'media/exports/baseline/nepal-mountain-working-copy-full-resolution.png';b=ROOT/'media/exports/finish/nepal-mountain-working-copy-full-resolution-2.png'
    x=np.array(Image.open(a));y=np.array(Image.open(b));assert x.shape==y.shape and np.any(x!=y)
    receipt=json.loads(b.with_suffix(b.suffix+'.integrity.json').read_text());assert receipt['valid']and digest(b)==receipt['sha256']
    board_manifest=json.loads((ROOT/'media/boards/delivery-manifest.json').read_text())
    assert len(board_manifest['outputs'])==6
    for out in board_manifest['outputs']:assert digest(ROOT/'media/boards'/out['fileName'])==out['sha256'],out['fileName']
    class Links(HTMLParser):
        def __init__(self):super().__init__();self.targets=[]
        def handle_starttag(self,tag,attrs):
            for k,v in attrs:
                if k in['href','src','poster']:self.targets.append(v)
    parser=Links();parser.feed((ROOT/'index.html').read_text());local_links=0
    for link in parser.targets:
        u=urlparse(link)
        if u.scheme or link.startswith('#'):continue
        assert(ROOT/unquote(u.path)).exists(),link;local_links+=1
    maincopy=(ROOT/'copybook.md').read_text();assert not re.search(r'\\u[0-9a-fA-F]{4}|\\n',maincopy)
    for c in J['campaigns']:
        assert not c['founder_capture']['required_for_primary']
        assert 'Undo'not in' '.join(c['reasons_to_believe'])or c['id']=='03'
        assert c['video']['duration']=={'01':32,'02':42,'03':33,'04':24,'05':36}[c['id']]
    from pypdf import PdfReader
    pdf=PdfReader(ROOT/'creative-direction-book.pdf');assert len(pdf.pages)==20
    with zipfile.ZipFile(ROOT/'production-tracker.xlsx')as z:assert z.testzip()is None
    report={'primary_campaigns':5,'core_deliverables':15,'primary_films':5,'alternate_hook_films':10,'silent_masters':15,'carousel_slides':slides,'profiled_still_files':len(stills),'portable_vector_files':len(vectors),'director_book_pages':20,'source_records_hash_checked':len(source['records']),'local_review_links_checked':local_links,'native_photo_pair':{'same_size':list(Image.open(a).size),'different_pixel_values':int(np.count_nonzero(x!=y)),'chosen_sha256':digest(b),'treatment':'5900K,+12%contrast,+0.15EV combined endpoint'},'all_video_decodes':'passed','proof_boundary':'Native output/dated captures, not current continuous source-to-export proof; no independent display/color accuracy qualification.','browser_layout':'Local file protocol blocked by browser tool; layouts reviewed through image/PDF renders, no policy workaround.','videos':video_checks,'stills':stills}
    (ROOT/'qa/final-verification.json').write_text(json.dumps(report,indent=2));print(json.dumps({k:v for k,v in report.items()if k not in['videos','stills']}))
    # Decode actual primary streams at representative timestamps for final phone review.
    for c in J['campaigns']:
        id=c['id'];times={'01':[1,5,13,21,29],'02':[1,7,14,22,36],'03':[1,6,10,13,16,21,30],'04':[1,5,9,12.8,17,22],'05':[1,6,11,18,25,32]}[id]
        sheet=Image.new('RGB',(360*len(times),640),'#141719')
        for i,t in enumerate(times):
            p=ROOT/f'qa/final-{id}-{t:04.1f}.png';subprocess.run(['ffmpeg','-v','error','-y','-ss',str(t),'-i',str(ROOT/f'campaign{id}/videos/hero.mp4'),'-frames:v','1','-vf','scale=360:640',str(p)],check=True)
            sheet.paste(Image.open(p).convert('RGB'),(i*360,0))
        sheet.save(ROOT/f'qa/final-{id}-phone-contact.jpg',quality=95)
if __name__=='__main__':main()
