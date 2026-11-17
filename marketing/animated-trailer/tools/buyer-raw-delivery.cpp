#include "photo_editor/pipeline/color_adjustments.h"
#include <libraw/libraw.h>
#include <QCoreApplication>
#include <QImage>
#include <QColorSpace>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <iostream>
#include <algorithm>
using namespace photo_editor::pipeline;
int main(int argc,char**argv){
 QCoreApplication app(argc,argv);if(argc<3||argc>4)return 2;
 LibRaw raw;raw.imgdata.params.output_bps=16;raw.imgdata.params.output_color=1;raw.imgdata.params.use_camera_wb=1;raw.imgdata.params.no_auto_bright=1;raw.imgdata.params.gamm[0]=1;raw.imgdata.params.gamm[1]=1;
 if(raw.open_file(argv[1])||raw.unpack()||raw.dcraw_process())return 3;
 int error=0;auto image=raw.dcraw_make_mem_image(&error);if(!image||error||image->colors!=3||image->bits!=16)return 4;
 const auto data=reinterpret_cast<const quint16*>(image->data);QImage full(image->width,image->height,QImage::Format_RGBA64);
 for(int y=0;y<full.height();y++){auto row=reinterpret_cast<QRgba64*>(full.scanLine(y));for(int x=0;x<full.width();x++){const auto i=(y*full.width()+x)*3;row[x]=QRgba64::fromRgba64(data[i],data[i+1],data[i+2],65535);}}
 const auto original=full.size();auto raster=full;
 int w=raster.width(),h=raster.height();const int preview_w=w,preview_h=h;std::vector<ColorPixel> source;source.reserve(w*h);
 for(int y=0;y<h;y++){const auto row=reinterpret_cast<const QRgba64*>(raster.constScanLine(y));for(int x=0;x<w;x++)source.push_back({row[x].red()/65535.f,row[x].green()/65535.f,row[x].blue()/65535.f,1});}
 QDir().mkpath(argv[2]);
 auto render=[&](const std::string& name,ColorAdjustmentState state){auto p=source;state.color_pipeline.photographic_rendering_model=PhotographicRenderingModel::Aces2HellwigJmhV1;
 state.color_pipeline.working_profile=working_color_profile_from_name("Linear sRGB");state.color_pipeline.preview_output_profile=output_color_profile_from_name("sRGB");state.color_pipeline.preview_display_target=DisplayTarget{};
 if(!apply_color_adjustments(&p,w,h,state))return false;
 if(!apply_color_photographic_output_stage(&p,w,h,state.color_pipeline,GamutWarningTarget::PreviewDisplay,Aces2OutputLutQuality::Final))return false;
 if(!apply_color_publish_stage(&p,w,h,state.color_pipeline.preview_display_target))return false;
 QImage result(w,h,QImage::Format_RGBA64);result.setColorSpace(QColorSpace(QColorSpace::SRgb));
 for(int y=0;y<h;y++){auto row=reinterpret_cast<QRgba64*>(result.scanLine(y));for(int x=0;x<w;x++){auto a=p[y*w+x];auto quant=[](float v){return quint16(std::clamp(int(std::lround(v*65535)),0,65535));};row[x]=QRgba64::fromRgba64(quant(a.red),quant(a.green),quant(a.blue),65535);}}
 const QString base=QString::fromLocal8Bit(argv[2])+"/camera-finish";
 bool a=result.save(base+".tiff","TIFF");bool b=result.convertToFormat(QImage::Format_RGB888).save(base+".jpg","JPEG",96);
 std::cout<<"TIFF "<<a<<" JPEG "<<b<<" "<<w<<"x"<<h<<std::endl;return a&&b;};
 ColorAdjustmentState state;state.input_transform.exposure=1.4;state.highlights=-35;state.shadows=35;state.input_transform.temperature=16;state.master_curve_points={{0,0},{.22,.16},{.55,.6},{1,1}};
 bool ok=render("camera-finish",state);LibRaw::dcraw_clear_mem(image);return ok?0:10;
}
