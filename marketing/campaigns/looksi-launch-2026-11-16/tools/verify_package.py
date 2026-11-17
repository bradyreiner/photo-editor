#!/usr/bin/env python3
"""Validate the actual campaign deliverables, not product capability."""
from pathlib import Path
from html.parser import HTMLParser
from urllib.parse import urlparse,unquote
import hashlib
import json
import subprocess
import xml.etree.ElementTree as ET
from PIL import Image,ImageDraw,ImageFont,ImageOps

ROOT=Path(__file__).resolve().parents[1]
class Links(HTMLParser):
    def __init__(self):super().__init__();self.targets=[]
    def handle_starttag(self,tag,attrs):
        for key,value in attrs:
            if key in ['href','src','poster']:self.targets.append(value)

def verify():
    manifest=[]
    for id in ['01','02','03','04','05']:
        for kind,folder,size in [('board','boards',(1920,1080)),('image','images',(1080,1350))]:
            path=ROOT/folder/f'{id}-{kind}.png'
            with Image.open(path) as im:assert im.size==size,(path,im.size)
            svg=path.with_suffix('.svg')
            node=ET.parse(svg).getroot()
            assert int(node.attrib['width'])==size[0]
            assert int(node.attrib['height'])==size[1]
            for asset in [path,svg]:
                manifest.append({'path':str(asset.relative_to(ROOT)),'bytes':asset.stat().st_size,'sha256':hashlib.sha256(asset.read_bytes()).hexdigest()})
        video=ROOT/'videos'/f'{id}-video.mp4'
        info=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_streams','-show_format','-of','json',str(video)]))
        v=next(s for s in info['streams']if s['codec_type']=='video')
        a=next(s for s in info['streams']if s['codec_type']=='audio')
        assert (v['width'],v['height'],v['codec_name'],v['pix_fmt'],v['r_frame_rate'])==(1080,1920,'h264','yuv420p','24/1')
        assert a['codec_name']=='aac' and a['sample_rate']=='48000'
        assert abs(float(info['format']['duration'])-18)<.05
        subprocess.run(['ffmpeg','-v','error','-xerror','-i',str(video),'-f','null','-'],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
        manifest.append({'path':str(video.relative_to(ROOT)),'bytes':video.stat().st_size,'sha256':hashlib.sha256(video.read_bytes()).hexdigest(),'dimensions':[1080,1920],'fps':24,'duration_seconds':float(info['format']['duration']),'codecs':['h264','aac'],'full_decode':'passed'})
    link_count=0
    for doc in ['index.html','production-playbook.html']:
        parser=Links();parser.feed((ROOT/doc).read_text())
        for target in parser.targets:
            url=urlparse(target)
            if url.scheme or target.startswith('#'):continue
            dest=ROOT/unquote(url.path)
            assert dest.exists(),(doc,target)
            link_count+=1
    copy=json.loads((ROOT/'copy.json').read_text())
    timing=json.loads((ROOT/'shot-timing.json').read_text())
    for c,t in zip(copy['campaigns'],timing['campaigns']):
        assert c['id']==t['id']
        assert c['title']==t['name']
        assert c['video_duration_seconds']==18
        for authored,rendered in zip(c['video_script'],t['shots']):
            assert authored['start']==rendered['start'] and authored['end']==rendered['end']
            assert authored['on_screen'].splitlines()==rendered['screen_text'],(c['id'],authored['on_screen'],rendered['screen_text'])
    # A contact sheet makes all board layouts reviewable together.
    sheet=Image.new('RGB',(1920,1296),'#202020')
    for i,id in enumerate(['01','02','03','04','05']):
        thumb=ImageOps.contain(Image.open(ROOT/'boards'/f'{id}-board.png'),(960,540))
        x=(i%2)*960;y=(i//2)*432
        thumb=ImageOps.contain(thumb,(768,432))
        sheet.paste(thumb,(x,y))
    sheet.save(ROOT/'qa/all-boards.jpg',quality=95)
    report={'deliverables':15,'editable_svg_files':10,'local_html_links_checked':link_count,'rendered_copy_timing_agreement':'passed','video_full_decode':'all 5 passed','browser_rendering':'not performed; file protocol blocked by browser tool policy; no workaround used','product_execution_proof':'not asserted; 01–03 retain clearly labeled capture requirements','manifest':manifest}
    (ROOT/'qa/package-verification.json').write_text(json.dumps(report,indent=2))
    print(json.dumps({k:v for k,v in report.items()if k!='manifest'}))
if __name__=='__main__':verify()
