#include "photo_editor/pipeline/color_adjustments.h"
#include <libraw/libraw.h>
#include <QCoreApplication>
#include <QImage>
#include <QColorSpace>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
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
 const auto original=full.size();auto raster=full.scaled(1440,1440,Qt::KeepAspectRatio,Qt::SmoothTransformation);
 int w=raster.width(),h=raster.height();const int preview_w=w,preview_h=h;std::vector<ColorPixel> source;source.reserve(w*h);
 for(int y=0;y<h;y++){const auto row=reinterpret_cast<const QRgba64*>(raster.constScanLine(y));for(int x=0;x<w;x++)source.push_back({row[x].red()/65535.f,row[x].green()/65535.f,row[x].blue()/65535.f,1});}
 QDir().mkpath(argv[2]);
 auto render=[&](const std::string& name,ColorAdjustmentState state){auto p=source;state.color_pipeline.photographic_rendering_model=PhotographicRenderingModel::Aces2HellwigJmhV1;
 state.color_pipeline.working_profile=working_color_profile_from_name("Linear sRGB");state.color_pipeline.preview_output_profile=output_color_profile_from_name("sRGB");state.color_pipeline.preview_display_target=DisplayTarget{};
 if(!apply_color_adjustments(&p,w,h,state))return false;
 if(!apply_color_photographic_output_stage(&p,w,h,state.color_pipeline,GamutWarningTarget::PreviewDisplay,Aces2OutputLutQuality::Final))return false;
 if(!apply_color_publish_stage(&p,w,h,state.color_pipeline.preview_display_target))return false;
 QImage out(w,h,QImage::Format_RGB888);out.setColorSpace(QColorSpace(QColorSpace::SRgb));for(int y=0;y<h;y++){auto row=out.scanLine(y);for(int x=0;x<w;x++){auto a=p[y*w+x];row[x*3]=std::clamp(int(std::lround(a.red*255)),0,255);row[x*3+1]=std::clamp(int(std::lround(a.green*255)),0,255);row[x*3+2]=std::clamp(int(std::lround(a.blue*255)),0,255);}}
 bool saved=out.save(QString::fromLocal8Bit(argv[2])+"/"+QString::fromStdString(name)+".png");std::cout<<name<<" "<<saved<<std::endl;return saved;};
 ColorAdjustmentState base;
 if(argc==4&&std::string(argv[3])=="batch"){if(!render("batch-before",base))return 12;base.input_transform.exposure=.8;if(!render("batch-after",base))return 13;return 0;}

 if(argc==3)for(int stage=0;stage<5;stage++)for(int i=0;i<=8;i++){
  double u=i/8.;ColorAdjustmentState s=base;
  if(stage>=1)s.input_transform.exposure=1.4;
  if(stage>=2){s.highlights=-35;s.shadows=35;}
  if(stage>=3)s.input_transform.temperature=16;
  if(stage>=4)s.master_curve_points={{0,0},{.22,.16},{.55,.6},{1,1}};
  if(stage==0)s.input_transform.exposure=1.4*u;
  if(stage==1){s.highlights=-35*u;s.shadows=35*u;}
  if(stage==2)s.input_transform.temperature=16*u;
  if(stage==3)s.master_curve_points={{0,0},{.22,.22-.06*u},{.55,.55+.05*u},{1,1}};
  if(stage==4){s.color_mixer_channel="Blues";s.hue_adjustment=-14*u;s.saturation_adjustment=18*u;s.luminance_adjustment=-6*u;}
  if(!render(std::to_string(stage)+"-"+std::to_string(i),s))return 5;
 }
 ColorAdjustmentState subject;subject.input_transform.exposure=1.95;subject.input_transform.temperature=16;subject.highlights=-35;subject.shadows=35;subject.master_curve_points={{0,0},{.22,.16},{.55,.6},{1,1}};subject.color_mixer_channel="Blues";subject.hue_adjustment=-14;subject.saturation_adjustment=18;subject.luminance_adjustment=-6;if(!render("subject-global",subject))return 10;
 // Native-pixel crop from the full-resolution decoded sensor raster.
 auto crop=full.copy(std::max(0,int(full.width()*.57)-480),std::max(0,int(full.height()*.59)-480),960,960);
 w=crop.width();h=crop.height();source.clear();for(int y=0;y<h;y++){const auto row=reinterpret_cast<const QRgba64*>(crop.constScanLine(y));for(int x=0;x<w;x++)source.push_back({row[x].red()/65535.f,row[x].green()/65535.f,row[x].blue()/65535.f,1});}
 ColorAdjustmentState detail;detail.input_transform.exposure=1.4;detail.input_transform.temperature=16;detail.shadows=35;detail.highlights=-35;detail.sharpening=28;detail.master_curve_points={{0,0},{.22,.16},{.55,.6},{1,1}};if(!render("native-detail",detail))return 7;detail.input_transform.exposure=1.95;if(!render("native-detail-lift",detail))return 11;
 // Cube derived from the same native engine for source-footage grading.
 w=33;h=33*33;source.clear();for(int b=0;b<33;b++)for(int g=0;g<33;g++)for(int r=0;r<33;r++){auto dec=[](float x){return x<=.04045f?x/12.92f:std::pow((x+.055f)/1.055f,2.4f);};source.push_back({dec(r/32.f),dec(g/32.f),dec(b/32.f),1});}
 ColorAdjustmentState film;film.input_transform.exposure=.25;film.contrast=14;film.vibrance=12;if(!render("lut-look",film))return 8;film.black_and_white_enabled=true;if(!render("lut-mono",film))return 9;
 QFile receipt(QString::fromLocal8Bit(argv[2])+"/engine.json");if(!receipt.open(QIODevice::WriteOnly))return 6;receipt.write(QJsonDocument(QJsonObject{{"source",QString::fromLocal8Bit(argv[1])},{"rawWidth",original.width()},{"rawHeight",original.height()},{"previewWidth",preview_w},{"previewHeight",preview_h},{"decode","Actual CR2 / LibRaw camera WB / linear sRGB"},{"renderer","Looksy native CPU color adjustments, ACES2 Hellwig JMh output, sRGB publication"},{"scope","Native engine plates for animated UI. Not a recording or performance/accuracy qualification."}}).toJson());
 LibRaw::dcraw_clear_mem(image);return 0;
}
