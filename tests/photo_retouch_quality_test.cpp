#include "ai_model_runtime.h"
#include "brush_input_schema.h"
#include "local_mask_compiler.h"
#include "edit_recipe_state.h"
#include "../engine/src/pipeline/color_mask_runtime.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QColorSpace>
#include <cmath>
#include <iostream>
using namespace photo_editor::desktop;
using namespace photo_editor::pipeline;
#define CHECK(c,m) do { if(!(c)) { std::cerr<<m<<'\n'; return 1; } } while(false)
int main(int argc,char** argv) {
  QCoreApplication app(argc,argv);
  const auto context=make_pipeline_color_context(ColorAdjustmentState{});
  QVariantMap mask{{"id","clone-proof"},{"kind","brush"},{"toolRole","retouch"},
      {"retouchMode","clone"},{"operation","add"},{"committed",true},{"cloneSourceSet",true},
      {"cloneSourceX",0.15},{"cloneSourceY",0.3},{"cloneAligned",false},
      {"brushSize",0.08},{"brushHardness",1.0},{"brushFlow",1.0},{"feather",0.0}};
  BrushPreviewPoint first; first.x=0.55; first.y=0.3; first.size=0.08; first.flow=1; first.opacity=1;
  auto second=first; second.x=0.8;
  const auto a=brush_stroke_variant(mask,{first},1),b=brush_stroke_variant(mask,{second},2);
  mask.insert("brushPoints",QVariantList{brush_point_variant(first),brush_point_variant(second)});
  mask.insert("brushStrokes",QVariantList{a,b});
  mask.insert("cloneOffsetX",-0.65); mask.insert("cloneOffsetY",0.0);
  EditRecipeState recipe; recipe.mask_stack={mask}; recipe.active_mask_index=0;
  const auto reopened=parse_edit_recipe_json(serialize_edit_recipe_json(recipe));
  auto clone=compile_pipeline_local_mask(reopened.mask_stack[0].toMap(),LocalMaskRasterPolicy::RebuildEditableAtOutputResolution,QSize(201,101));
  CHECK(clone.clone_strokes.size()==2,"Per-stroke clone source transforms survive recipe reopen");
  CHECK(std::abs(clone.clone_strokes[0].offset_x+0.4)<1e-9 && std::abs(clone.clone_strokes[1].offset_x+0.65)<1e-9,
      "Non-aligned stamps retain separate origins");
  constexpr int w=201,h=101;
  std::vector<ColorPixel> original(w*h);
  for(int y=0;y<h;++y) for(int x=0;x<w;++x) original[std::size_t(y*w+x)]={float(x)/200,0.3F,0.1F,1};
  auto pixels=original;
  apply_local_clone(&pixels,w,h,context,{clone});
  CHECK(std::abs(pixels[30*w+110].red-0.15F)<0.01 && std::abs(pixels[30*w+160].red-0.15F)<0.01,
      "The second stamp must not shift the first stamp's source: "<<pixels[30*w+110].red<<", "<<pixels[30*w+160].red<<" enabled="<<clone.enabled<<" kind="<<clone.kind<<" source="<<clone.clone_source_set);
  auto aligned = mask;
  aligned.insert("cloneAligned", true);
  aligned.insert("cloneOffsetX", -0.4);
  aligned.insert("brushStrokes", QVariantList{brush_stroke_variant(aligned,{first},1),
      brush_stroke_variant(aligned,{second},2)});
  const auto aligned_clone = compile_pipeline_local_mask(aligned,
      LocalMaskRasterPolicy::RebuildEditableAtOutputResolution,QSize(w,h));
  auto aligned_pixels = original;
  apply_local_clone(&aligned_pixels,w,h,context,{aligned_clone});
  CHECK(std::abs(aligned_pixels[30*w+110].red-0.15F)<0.01 &&
        std::abs(aligned_pixels[30*w+160].red-0.4F)<0.01,
        "Aligned cloning carries the source offset across strokes");
  auto legacy = mask;
  legacy.remove("brushStrokes");
  CHECK(compile_pipeline_local_mask(legacy).clone_strokes.empty(),
        "Legacy clone recipes retain their existing global-source behavior");
  auto erased=mask; auto erase_point=first; erase_point.erase=true;
  erased.insert("brushPoints",QVariantList{brush_point_variant(first),brush_point_variant(second),brush_point_variant(erase_point)});
  erased.insert("brushStrokes",QVariantList{a,b,brush_stroke_variant(mask,{erase_point},3)});
  auto erased_clone=compile_pipeline_local_mask(erased,LocalMaskRasterPolicy::RebuildEditableAtOutputResolution,QSize(w,h));
  auto erased_pixels=original;apply_local_clone(&erased_pixels,w,h,context,{erased_clone});
  CHECK(erased_pixels[30*w+110]==original[30*w+110] && std::abs(erased_pixels[30*w+160].red-0.15F)<0.01,
      "Erasing clone coverage restores the source without moving other stamps");
  clone.clone_strokes.clear(); clone.brush_points={BrushPoint{.x=0.55,.y=0.3,.size=0.08}};
  clone.clone_source_x=0; clone.clone_offset_x=-0.55;
  pixels=original; apply_local_clone(&pixels,w,h,context,{clone});
  CHECK(pixels[30*w+100]==original[30*w+100] && pixels[30*w+115]!=original[30*w+115],
      "Out-of-image clone samples must preserve the edge while valid samples still paint");

  LocalMask fill; fill.kind="brush"; fill.retouch_mode="heal"; fill.remove_strength=100;
  fill.segmentation_width=w; fill.segmentation_height=h; fill.segmentation_mask.resize(w*h,0);
  fill.inpaint_patch_width=w; fill.inpaint_patch_height=h; fill.inpaint_patch_pixels.assign(w*h,ColorPixel{0.7F,0.5F,0.2F,1});
  fill.inpaint_seam_feather=8; fill.inpaint_color_match=0;
  fill.heal_texture_blend=1; fill.heal_color_blend=1;
  for(int y=20;y<80;++y) for(int x=40;x<160;++x) if(x<90 || x>110 || y<40 || y>60)
    fill.segmentation_mask[std::size_t(y*w+x)]=255;
  auto hdr_original=original;
  for(auto& pixel:hdr_original) {pixel.green=1.123456F;pixel.blue=-0.023456F;}
  pixels=hdr_original;
  QElapsedTimer seam_timer;seam_timer.start();
  apply_local_inpaint_patches(&pixels,w,h,context,{fill});
  CHECK(pixels[30*w+70]!=hdr_original[30*w+70],"Healing changes the selected interior");
  for(std::size_t i=0;i<pixels.size();++i) if(fill.segmentation_mask[i]==0)
    CHECK(pixels[i]==hdr_original[i],"Healing preserves every outside pixel and interior hole, including HDR precision");
  std::cout<<"seam_ms="<<seam_timer.elapsed()<<'\n';
  for (int y=0;y<h;++y) for (int x=0;x<w;++x) {
    const auto index=static_cast<std::size_t>(y*w+x);
    if (!fill.segmentation_mask[index]) continue;
    double nearest=8;
    for (int dy=-8;dy<=8;++dy) for (int dx=-8;dx<=8;++dx) {
      if (x+dx>=0 && x+dx<w && y+dy>=0 && y+dy<h &&
          fill.segmentation_mask[static_cast<std::size_t>((y+dy)*w+x+dx)]==0)
        nearest=std::min(nearest,std::hypot(double(dx),double(dy)));
    }
    const double t=nearest/8, alpha=t*t*(3-2*t);
    const double expected=std::lerp(double(hdr_original[index].red),0.7,alpha);
    CHECK(std::abs(pixels[index].red-expected)<1e-6,
          "Linear-time healing feather must match the scalar distance reference");
  }

  AiInpaintRequest request;
  request.source_image=QImage(1600,1200,QImage::Format_RGBA64);
  request.source_image.setColorSpace(QColorSpace(QColorSpace::SRgbLinear));
  request.source_image.fill(QColor::fromRgbF(0.25,0.5,0.125));
  request.mask_width=1600;request.mask_height=1200;request.mask_data=QByteArray(1600*1200,'\0');
  for(int y=500;y<580;++y) for(int x=700;x<850;++x) request.mask_data[y*1600+x]=char(255);
  request.retouch_mode="patch";request.patch_source_set=true;request.quality_mode="final";
  auto patch=run_ai_inpaint(request);
  CHECK(patch.success && patch.patch_image.depth()==64,"Source-region patch retains 16-bit full-resolution output");
  const auto reference=request.source_image.convertedToColorSpace(QColorSpace(QColorSpace::SRgb),QImage::Format_RGBA64);
  CHECK(std::abs(patch.patch_image.pixelColor(0,0).redF()-reference.pixelColor(0,0).redF())<0.0001,
      "Linear/wide-gamut source must be converted before tagging a patch sRGB");
  request.control=std::make_shared<AiInpaintControl>();request.control->cancel();
  patch=run_ai_inpaint(request);
  CHECK(patch.cancelled && !patch.success && !patch.fallback,"Cancelled inpaint must never start fallback generation");
  int interrupts=0;request.control->setInterrupt([&]{++interrupts;});
  CHECK(interrupts==1,"Cancellation before inference registration still interrupts the run");
  request.control->setInterrupt({}); request.control->cancel();
  CHECK(interrupts==1,"Cleared inference callbacks cannot outlive their run options");
  std::cout<<"Clone origins, image boundaries, healing locality, high-resolution patch color and cancellation passed\n";
  return 0;
}
