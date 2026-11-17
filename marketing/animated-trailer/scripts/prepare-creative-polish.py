"""Curate existing camera previews and render matching native recipe assets.

No original file is edited. Generated shoe assets are deliberately not processed
here; they remain separate compositing artwork from the previous revision.
"""
from pathlib import Path
import hashlib,json,subprocess,shlex
from PIL import Image,ImageOps,ImageDraw
root=Path(__file__).resolve().parents[1];repo=root.parents[1]
media=root/'public/media/creative-polish-v2';out=root/'out/creative-polish-v2'
media.mkdir(exist_ok=True);out.mkdir(exist_ok=True)
chosen=['IMG_4653','IMG_4748','IMG_4779','IMG_4788','IMG_4867','IMG_4969','IMG_5053','IMG_5071','IMG_5077','IMG_5106','IMG_5125','IMG_5203']
sources=[]
for name in chosen:
    source=repo/f'marketing/renders/nepal-review/photos/{name}.jpg'
    im=ImageOps.exif_transpose(Image.open(source)).convert('RGB');im.thumbnail((1800,1800),Image.Resampling.LANCZOS);im.save(media/f'{name}.jpg',quality=97,subsampling=0)
    sources.append({'id':name,'camera_original':f'/Volumes/Brady SSD/{name}.CR2','asset':f'{name}.jpg','source_preview_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'scope':'Camera embedded preview selected for gallery/artwork; not a native edit result.'})
(out/'camera-selection.json').write_text(json.dumps(sources,indent=2)+'\n')
board=Image.new('RGB',(1600,1100),'#0c1118');draw=ImageDraw.Draw(board)
for i,name in enumerate(chosen):
    im=Image.open(media/f'{name}.jpg');im.thumbnail((385,300));x=i%4*400;y=i//4*360;board.paste(im,(x+(390-im.width)//2,y));draw.text((x+15,y+310),name,fill='white')
board.save(out/'curated-camera-selection.jpg')

# The same production-native source/link recipe as the existing verified tools.
original=(root/'tools/studio-color.cpp').read_text().replace('#include <QJsonObject>','#include <QJsonObject>\n#include <QJsonArray>')
marker=' ColorAdjustmentState base;'
addition='''
 ColorAdjustmentState base;
 if(argc==4&&std::string(argv[3])=="curve-sequence"){
  QJsonArray frames,xs;for(int j=0;j<=128;j++)xs.append(j/128.);
  for(int i=0;i<=16;i++){double u=i/16.;ColorAdjustmentState s;s.input_transform.exposure=1.4;s.highlights=-35;s.shadows=35;s.input_transform.temperature=16;s.master_curve_points={{0,0},{.22,.22-.06*std::min(1.,2*u)},{.55,.55+.05*std::max(0.,2*u-1.)},{1,1}};if(!render("curve-"+std::to_string(i),s))return 22;QJsonArray ys;for(int j=0;j<=128;j++)ys.append(evaluate_tone_curve(j/128.,s));frames.append(ys);}
  QFile f(QString::fromLocal8Bit(argv[2])+"/curve.json");if(!f.open(QIODevice::WriteOnly))return 23;f.write(QJsonDocument(QJsonObject{{"x",xs},{"frames",frames}}).toJson());return 0;
 }
 if(argc==4&&std::string(argv[3])=="orange"){
  for(int i=0;i<=8;i++){double u=i/8.;ColorAdjustmentState s;s.input_transform.exposure=.35;s.highlights=-18;s.shadows=20;s.color_mixer_channel="Oranges";s.hue_adjustment=-8*u;s.saturation_adjustment=16*u;s.luminance_adjustment=8*u;if(!render("mix-"+std::to_string(i),s))return 20;}
  return 0;
 }
 if(argc==4&&std::string(argv[3])=="detail"){
  auto crop=full.copy(std::max(0,int(full.width()*.57)-480),std::max(0,int(full.height()*.59)-480),960,960);
  w=crop.width();h=crop.height();source.clear();for(int y=0;y<h;y++){const auto row=reinterpret_cast<const QRgba64*>(crop.constScanLine(y));for(int x=0;x<w;x++)source.push_back({row[x].red()/65535.f,row[x].green()/65535.f,row[x].blue()/65535.f,1});}
  for(int i=0;i<=8;i++){double u=i/8.;ColorAdjustmentState s;s.input_transform.exposure=1.4;s.highlights=-35;s.shadows=35;s.input_transform.temperature=16;s.master_curve_points={{0,0},{.22,.16},{.55,.6},{1,1}};s.sharpening=32*u;s.luminance_nr=24*u;s.color_nr=18*u;if(!render("detail-"+std::to_string(i),s))return 21;}
  return 0;
 }
'''
assert original.count(marker)==1
tool_source=root/'tools/creative-polish-color.cpp';tool_source.write_text(original.replace(marker,addition))
build=repo/'build/dev-macos-debug'
line=subprocess.check_output(['ninja','-C',str(build),'-t','commands','color_pipeline_test'],text=True).splitlines()[-1]
args=[x for x in shlex.split(line) if x not in [':','&&']]
ix=next(i for i,x in enumerate(args) if x.endswith('color_pipeline_test.cpp.o'));args[ix]=str(tool_source);args[args.index('-o')+1]=str(out/'native-color')
args[1:1]=['-std=c++20','-O2','-I'+str(repo/'engine/include'),'-I'+str(repo/'apps/desktop/src'),'-I/opt/homebrew/include','-F/opt/homebrew/lib','-I/opt/homebrew/lib/QtCore.framework/Headers','-I/opt/homebrew/lib/QtGui.framework/Headers']
args+=['-framework','QtGui','-framework','QtCore','-L/opt/homebrew/opt/libraw/lib','-lraw_r','-Wl,-rpath,/opt/homebrew/lib']
subprocess.run(args,cwd=build,check=True,capture_output=True)
def native(source,dest,mode):
    dest.mkdir(exist_ok=True)
    subprocess.run([str(out/'native-color'),str(source),str(dest),mode],check=True)
native(Path('/Volumes/Brady SSD/IMG_4869.CR2'),media/'flower','orange')
native(root/'capture/desktop-source/IMG_4969.CR2',media/'detail','detail')
native(root/'capture/desktop-source/IMG_4969.CR2',media/'curve','curve-sequence')
curve=json.loads((media/'curve/curve.json').read_text());curve={'x':[round(x,8) for x in curve['x']],'frames':[[round(y,8) for y in frame] for frame in curve['frames']]};(root/'src/creative-launch/recipe-data.ts').write_text('export const nativeCurve = '+json.dumps(curve,separators=(',',':'))+';\n')
for name in ['wedding-walk','wedding-embrace','wedding-play']:
    dest=media/name;dest.mkdir(exist_ok=True)
    subprocess.run([str(root/'out/rebuild/full/native-wedding-color'),str(media/f'{name}.jpg'),str(dest),'batch'],check=True)
receipt={'engine_binary':str(out/'native-color'),'sha256':hashlib.sha256((out/'native-color').read_bytes()).hexdigest(),'flower':{'source':'IMG_4869.CR2','channel':'Oranges','exposure':.35,'highlights':-18,'shadows':20,'hue':[0,-8],'saturation':[0,16],'luminance':[0,8]},'detail':{'source':'IMG_4969.CR2','native_crop':[960,960],'center_fraction':[.57,.59],'sharpening':[0,32],'luminance_nr':[0,24],'color_nr':[0,18]},'subject':{'base':'studio/color/4-8.png','result':'studio/subject-lift.png','mask':'studio/subject.png','exposure_delta':.55},'scope':'Production native CPU adjustment functions and source-derived masks. No current native UI performance or full workflow qualification claim.'}
(out/'matching-recipes.json').write_text(json.dumps(receipt,indent=2)+'\n')
print('Prepared curated source frames and matching native edit plates.',flush=True)
