#!/usr/bin/env python3
"""Inspect delivered codecs, decode every frame, and build review evidence."""
from pathlib import Path
from html.parser import HTMLParser
import importlib.util
import hashlib, json, subprocess, wave
import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT=Path(__file__).resolve().parents[1]
QA=ROOT/'qa';QA.mkdir(exist_ok=True)
report={'videos':{},'audio':{},'visual_review_files':[]}
for name in ['hero','hook-b','hook-c']:
    for suffix in ['', '-silent']:
        p=ROOT/f'videos/{name}{suffix}.mp4'
        raw=subprocess.run(['ffprobe','-v','error','-count_frames','-show_entries','format=duration:stream=codec_name,width,height,r_frame_rate,nb_read_frames,start_time,duration,color_range,color_space,color_transfer,color_primaries,pix_fmt','-of','json',str(p)],capture_output=True,text=True,check=True)
        info=json.loads(raw.stdout)
        video=next(v for v in info['streams'] if v['codec_name']=='h264')
        assert (video['width'],video['height'],video['r_frame_rate'],int(video['nb_read_frames']))==(1080,1920,'24/1',576),info
        assert (video['color_range'],video['color_space'],video['color_transfer'],video['color_primaries'],video['pix_fmt'])==('tv','bt709','bt709','bt709','yuv420p'),info
        assert abs(float(info['format']['duration'])-24)<.05,info
        audio=[v for v in info['streams'] if v['codec_name']=='aac']
        assert bool(audio)==(suffix==''),info
        decoded=subprocess.run(['ffmpeg','-hide_banner','-v','error','-i',str(p),'-f','null','-'],capture_output=True,text=True,check=True)
        assert not decoded.stderr.strip(),decoded.stderr
        info['full_decode']='passed';info['bytes']=p.stat().st_size
        info['sha256']=hashlib.sha256(p.read_bytes()).hexdigest()
        report['videos'][p.name]=info
    contact=QA/f'{name}-decoded-contact.png'
    subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-y','-i',str(ROOT/f'videos/{name}.mp4'),'-vf','fps=1/2,scale=270:480,tile=6x2','-frames:v','1',str(contact)],check=True)
    report['visual_review_files'].append(str(contact.relative_to(ROOT)))
    for at in [0,2.5,3.125,12.8,17.2,21.5]:
        target=QA/f'{name}-decoded-{at:06.3f}.png'
        subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-y','-ss',str(at),'-i',str(ROOT/f'videos/{name}.mp4'),'-frames:v','1',str(target)],check=True)
    Image.open(QA/f'{name}-decoded-21.500.png').resize((360,640),Image.Resampling.LANCZOS).save(QA/f'{name}-phone-end.png')

# A single continuous master is used for every hook; there are no concatenated audio seams.
wav=ROOT/'audio/original-sound-design.wav'
with wave.open(str(wav),'rb') as f:
    channels=f.getnchannels();sr=f.getframerate();samples=np.frombuffer(f.readframes(f.getnframes()),dtype='<i2').reshape(-1,channels)/32768
assert channels==2 and sr==48000
report['audio']['sample_rate']=sr;report['audio']['channels']=channels
report['audio']['duration']=len(samples)/sr
report['audio']['sample_peak_dbfs']=float(20*np.log10(np.max(np.abs(samples))))
boundaries={}
for at in [3,7,11,14,16,18]:
    start=round((at-.01)*sr);end=round((at+.01)*sr)
    boundaries[str(at)]=float(np.max(np.abs(np.diff(samples[start:end],axis=0))))
report['audio']['max_adjacent_sample_step_at_scene_cuts']=boundaries
analysis=subprocess.run(['ffmpeg','-hide_banner','-i',str(wav),'-af','loudnorm=I=-18:TP=-1.5:LRA=8:print_format=json','-f','null','-'],capture_output=True,text=True,check=True)
measurement=json.loads(analysis.stderr[analysis.stderr.rfind('{'):analysis.stderr.rfind('}')+1])
report['audio']['measured_integrated_lufs']=float(measurement['input_i'])
report['audio']['measured_true_peak_dbtp']=float(measurement['input_tp'])
report['audio']['measured_loudness_range_lu']=float(measurement['input_lra'])
assert report['audio']['measured_true_peak_dbtp'] < -1.4
encoded=subprocess.run(['ffmpeg','-hide_banner','-i',str(ROOT/'videos/hero.mp4'),'-vn','-af','loudnorm=I=-18:TP=-1.5:LRA=8:print_format=json','-f','null','-'],capture_output=True,text=True,check=True)
aac=json.loads(encoded.stderr[encoded.stderr.rfind('{'):encoded.stderr.rfind('}')+1])
report['audio']['encoded_aac_measurement']={'integrated_lufs':float(aac['input_i']),'true_peak_dbtp':float(aac['input_tp']),'loudness_range_lu':float(aac['input_lra'])}
assert report['audio']['encoded_aac_measurement']['true_peak_dbtp'] < -1

# Confirm the delivered image sizes, editable vector geometry and live text sources.
for path,expected in [('images/cover.png',(1080,1350)),('storyboard/board.png',(2400,1500))]:
    im=Image.open(ROOT/path)
    assert im.size==expected
    assert im.info.get('icc_profile'),f'Missing authored-graphic sRGB profile: {path}'
cover=(ROOT/'images/cover.svg').read_text()
assert 'original-editable-poster' in cover and 'data-copy=' in cover
assert '<text ' in (ROOT/'images/cover-editable-text.svg').read_text()
assert '<image ' in (ROOT/'storyboard/board.svg').read_text()
report['images']={'cover':'1080x1350 PNG and editable vector SVG','storyboard':'2400x1500 PNG and SVG; 5 panels','phone_cover':'qa/cover-phone-360.png'}
class LocalLinks(HTMLParser):
    def __init__(self):super().__init__();self.targets=[]
    def handle_starttag(self,tag,attrs):
        for name,value in attrs:
            if name in ['src','href'] and value and not value.startswith(('http:','https:','#','data:')):self.targets.append(value)
links=LocalLinks();links.feed((ROOT/'review.html').read_text())
for target in links.targets:assert (ROOT/target).exists(),target
report['review_page']={'static_local_links_checked':len(links.targets),'browser_playback':'Native browser interaction not asserted by this file check.'}
spec=importlib.util.spec_from_file_location('campaign04',ROOT/'source/make_campaign.py');renderer=importlib.util.module_from_spec(spec);spec.loader.exec_module(renderer)
patches=[('black',(0,0,0)),('white',(255,255,255)),('midgray',(128,128,128)),('brand red',(255,59,48)),('brand green',(48,209,88)),('brand blue',(50,130,255)),('ink',(7,8,10)),('paper',(243,243,237))]
swatch=Image.new('RGB',(1080,1920));d=ImageDraw.Draw(swatch)
for i,(_,color) in enumerate(patches):
    xx=(i%2)*540;yy=(i//2)*480;d.rectangle((xx,yy,xx+539,yy+479),fill=color)
swatch.save(QA/'rec709-swatch-source.png',icc_profile=renderer.SRGB_PROFILE)
diagnostic=QA/'rec709-swatch.mp4'
# Lossless YUV coding isolates range/matrix conversion from production CRF quantization.
cmd=['ffmpeg','-hide_banner','-loglevel','error','-y','-f','rawvideo','-pix_fmt','rgb24','-s','1080x1920','-r','24','-i','pipe:0','-frames:v','1','-vf',renderer.VIDEO_COLOR_FILTER,'-c:v','libx264','-crf','0','-pix_fmt','yuv420p',*renderer.VIDEO_COLOR_ARGS,str(diagnostic)]
subprocess.run(cmd,input=swatch.tobytes(),check=True,capture_output=True)
inverse='scale=in_color_matrix=bt709:in_range=tv:out_range=pc:flags=accurate_rnd+full_chroma_int'
rgb=subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-i',str(diagnostic),'-frames:v','1','-vf',inverse,'-pix_fmt','rgb24','-f','rawvideo','-'],capture_output=True,check=True).stdout
yplane=subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-i',str(diagnostic),'-frames:v','1','-vf','extractplanes=y','-pix_fmt','gray','-f','rawvideo','-'],capture_output=True,check=True).stdout
rgb=np.frombuffer(rgb,dtype=np.uint8).reshape(1920,1080,3);yplane=np.frombuffer(yplane,dtype=np.uint8).reshape(1920,1080)
transport=[]
for i,(name,color) in enumerate(patches):
    xx=(i%2)*540+270;yy=(i//2)*480+240
    mean=rgb[yy-16:yy+16,xx-16:xx+16].mean(axis=(0,1));ym=float(yplane[yy-16:yy+16,xx-16:xx+16].mean())
    expected=16+219*(.2126*color[0]+.7152*color[1]+.0722*color[2])/255
    error=float(np.max(np.abs(mean-np.array(color))))
    assert error<=2,(name,error)
    assert abs(ym-expected)<=1.1,(name,ym,expected)
    transport.append({'patch':name,'source_rgb':color,'decoded_rgb_mean':mean.tolist(),'max_rgb_code_error':error,'encoded_y_mean':ym,'expected_bt709_limited_y':expected})
Image.fromarray(rgb).save(QA/'rec709-swatch-decoded.png',icc_profile=renderer.SRGB_PROFILE)
report['color_transport']={'conversion_filter':renderer.VIDEO_COLOR_FILTER,'inverse_control_filter':inverse,'metadata':renderer.VIDEO_COLOR_ARGS,'patch_results':transport,'scope':'Numerical control of explicit BT.709 matrix and limited range using lossless YUV coding and an explicitly rounded inverse to isolate conversion from production CRF quantization and decoder fast-path rounding. This is not independent display or player color-accuracy qualification.'}
report['notes']=['Full MP4 decode and native stream metadata verified.','Contact sheets and full-size decoded stills support visual review. They do not establish behavior in every social platform player.','The film uses original campaign artwork and authored audio. It is not a product capture.','All audio joins were removed by encoding complete continuous masters.']
(QA/'verification.json').write_text(json.dumps(report,indent=2))
print(json.dumps({'videos':len(report['videos']),'decode':'all passed','audio':report['audio'],'verification':str(QA/'verification.json')},indent=2))
