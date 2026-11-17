"""Render a full-resolution camera RAW through Looksy's native CPU color engine.
This is file-level engine/export evidence, not a native UI workflow recording.
"""
from pathlib import Path
import subprocess,shlex,json,hashlib
r=Path(__file__).resolve().parents[1];repo=r.parents[1];out=r/'out/creative-polish-v4';dest=r/'public/media/creative-polish-v4/raw-export';dest.mkdir(parents=True,exist_ok=True)
s=(r/'tools/creative-polish-color.cpp').read_text();s=s[:s.index('\n ColorAdjustmentState base;')]
s=s.replace('auto raster=full.scaled(1440,1440,Qt::KeepAspectRatio,Qt::SmoothTransformation);','auto raster=full;')
a=s.index(' QImage out(w,h,QImage::Format_RGB888);');s=s[:a]+''' QImage result(w,h,QImage::Format_RGBA64);result.setColorSpace(QColorSpace(QColorSpace::SRgb));
 for(int y=0;y<h;y++){auto row=reinterpret_cast<QRgba64*>(result.scanLine(y));for(int x=0;x<w;x++){auto a=p[y*w+x];auto quant=[](float v){return quint16(std::clamp(int(std::lround(v*65535)),0,65535));};row[x]=QRgba64::fromRgba64(quant(a.red),quant(a.green),quant(a.blue),65535);}}
 const QString base=QString::fromLocal8Bit(argv[2])+"/camera-finish";
 bool a=result.save(base+".tiff","TIFF");bool b=result.convertToFormat(QImage::Format_RGB888).save(base+".jpg","JPEG",96);
 std::cout<<"TIFF "<<a<<" JPEG "<<b<<" "<<w<<"x"<<h<<std::endl;return a&&b;};
 ColorAdjustmentState state;state.input_transform.exposure=1.4;state.highlights=-35;state.shadows=35;state.input_transform.temperature=16;state.master_curve_points={{0,0},{.22,.16},{.55,.6},{1,1}};
 bool ok=render("camera-finish",state);LibRaw::dcraw_clear_mem(image);return ok?0:10;
}
'''
tool=r/'tools/buyer-raw-delivery.cpp';tool.write_text(s);build=repo/'build/dev-macos-debug'
line=subprocess.check_output(['ninja','-C',str(build),'-t','commands','color_pipeline_test'],text=True).splitlines()[-1];args=[x for x in shlex.split(line) if x not in [':','&&']];ix=next(i for i,x in enumerate(args) if x.endswith('color_pipeline_test.cpp.o'));args[ix]=str(tool);binary=out/'native-raw-delivery';args[args.index('-o')+1]=str(binary)
args[1:1]=['-std=c++20','-O2','-I'+str(repo/'engine/include'),'-I'+str(repo/'apps/desktop/src'),'-I/opt/homebrew/include','-F/opt/homebrew/lib','-I/opt/homebrew/lib/QtCore.framework/Headers','-I/opt/homebrew/lib/QtGui.framework/Headers'];args+=['-framework','QtGui','-framework','QtCore','-L/opt/homebrew/opt/libraw/lib','-lraw_r','-Wl,-rpath,/opt/homebrew/lib']
subprocess.run(args,cwd=build,check=True)
source=r/'capture/desktop-source/IMG_4969.CR2';subprocess.run([str(binary),str(source),str(dest)],check=True)
from PIL import Image
files=[]
for p in dest.iterdir():
 im=Image.open(p);assert im.size==(6024,4020),im.size
 files.append({'path':str(p.relative_to(r)),'width':im.width,'height':im.height,'format':im.format,'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
# TIFF BitsPerSample must establish 16-bit channels, not just filename/extension.
im=Image.open(dest/'camera-finish.tiff');bits=list(im.tag_v2[258]);assert bits==[16,16,16,16],bits
receipt={'source':'IMG_4969.CR2','source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'TIFF_bits_per_sample':bits,'recipe':{'exposure':1.4,'highlights':-35,'shadows':35,'temperature':16,'curve':[[0,0],[.22,.16],[.55,.6],[1,1]]},'files':files,'scope':'Full-resolution actual CR2 / LibRaw camera WB / linear sRGB / Looksy native CPU adjustment + ACES2 output + sRGB publication / Qt TIFF and JPEG encoders. File-level engine evidence; no new native UI workflow, speed or release-binary qualification.'}
(out/'raw-export-proof.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps(receipt,indent=2))
