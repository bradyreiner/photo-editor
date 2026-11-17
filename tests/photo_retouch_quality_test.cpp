#include "ai_model_runtime.h"
#include "brush_input_schema.h"
#include "brush_matte_rebuild.h"
#include "local_mask_compiler.h"
#include "edit_recipe_state.h"
#include "qimage_color_conversion.h"
#include "photo_render_utils.h"
#include "decoded_image_cache.h"
#include "../engine/src/pipeline/color_mask_runtime.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QColorSpace>
#include <QtGui/qrgbafloat.h>
#include <QTemporaryDir>
#include <QBuffer>
#include <array>
#include <cmath>
#include <iostream>
#include <string_view>
using namespace photo_editor::desktop;
using namespace photo_editor::pipeline;
#define CHECK(c,m) do { if(!(c)) { std::cerr<<m<<'\n'; return 1; } } while(false)
int main(int argc,char** argv) {
  QCoreApplication app(argc,argv);
  const auto context=make_pipeline_color_context(ColorAdjustmentState{});
  EditRecipeState guide_state; guide_state.working_color_profile="Linear sRGB";
  QImage p3_guide(9,9,QImage::Format_RGBA32FPx4);
  p3_guide.setColorSpace(QColorSpace(QColorSpace::DisplayP3));
  for (int y=0;y<9;++y) for (int x=0;x<9;++x)
    reinterpret_cast<QRgbaFloat32*>(p3_guide.scanLine(y))[x]=x<5
        ? QRgbaFloat32{0.45F,0.3F,0.2F,0.6F} : QRgbaFloat32{0.2F,0.4F,0.65F,0.6F};
  const auto srgb_equivalent=convert_tagged_qimage_color_space(p3_guide,QColorSpace(QColorSpace::SRgbLinear),
      QImage::Format_RGBA32FPx4,QImageColorConversionPurpose::EvidenceMeasurement);
  const auto p3_prepared=prepare_brush_color_source_image(p3_guide,guide_state,{});
  const auto srgb_prepared=prepare_brush_color_source_image(srgb_equivalent,guide_state,{});
  CHECK(!p3_prepared.isNull() && p3_prepared.format()==QImage::Format_RGBA32FPx4 &&
      p3_prepared.colorSpace()==QColorSpace(QColorSpace::SRgbLinear),
      "Brush guide conversion must produce tagged linear sRGB float pixels");
  QVariantMap guide_mask{{"segmentationWidth",9},{"segmentationHeight",9},
      {"segmentationData",QByteArray(81,char(0))}};
  auto equivalent_mask=guide_mask;
  (void)rasterize_brush_point_into_mask(&guide_mask,&p3_prepared,0.5,0.5,0.5,1,1,0.05,
      false,true,0.05,0.25,0.5,true);
  (void)rasterize_brush_point_into_mask(&equivalent_mask,&srgb_prepared,0.5,0.5,0.5,1,1,0.05,
      false,true,0.05,0.25,0.5,true);
  CHECK(guide_mask==equivalent_mask,
      "Equivalent P3 and sRGB source colors must yield identical constrained brush alpha");
  auto edited_guide_state=guide_state; edited_guide_state.exposure=3.0; edited_guide_state.contrast=50.0;
  CHECK(prepare_brush_color_source_image(p3_guide,edited_guide_state,{})==p3_prepared,
      "Brush source guides use neutral scene exposure and Look while retaining profile characterization");
  QImage hdr_guide(2,1,QImage::Format_RGBA32FPx4);
  hdr_guide.setColorSpace(QColorSpace(QColorSpace::SRgbLinear));
  reinterpret_cast<QRgbaFloat32*>(hdr_guide.scanLine(0))[0]={1.5F,1.25F,-0.1F,0.25F};
  reinterpret_cast<QRgbaFloat32*>(hdr_guide.scanLine(0))[1]={3.0F,2.0F,1.5F,0.75F};
  const auto hdr_prepared=prepare_brush_color_source_image(hdr_guide,guide_state,{});
  CHECK(!hdr_prepared.isNull(),"Extended-range source guide conversion must succeed");
  const auto* hdr_values=reinterpret_cast<const QRgbaFloat32*>(hdr_prepared.constScanLine(0));
  CHECK(std::abs(hdr_values[0].red()-1.5F)<1e-6 && std::abs(hdr_values[0].blue()+0.1F)<1e-6 &&
      std::abs(hdr_values[1].red()-3.0F)<1e-6 && std::abs(hdr_values[0].alpha()-0.25F)<1e-6,
      "Source guides must retain HDR headroom, signed values, and alpha before edge detection");
  auto wide_hdr=hdr_guide;
  wide_hdr.setColorSpace(QColorSpace(QColorSpace::Primaries::DciP3D65,QColorSpace::TransferFunction::Linear));
  const auto wide_prepared=prepare_brush_color_source_image(wide_hdr,guide_state,{});
  CHECK(!wide_prepared.isNull(),"Wide-gamut HDR source guide conversion must succeed");
  const auto wide_pixel=reinterpret_cast<const QRgbaFloat32*>(wide_prepared.constScanLine(0))[0];
  CHECK(wide_pixel.red()>1.5F && wide_pixel.blue()<0.0F && std::abs(wide_pixel.alpha()-0.25F)<1e-6,
      "P3-to-sRGB guide conversion must retain above-white and signed out-of-gamut channels");
  auto camera_guide=hdr_guide; camera_guide.setColorSpace(QColorSpace{});
  DecodedImageMetadata camera_metadata;
  camera_metadata.valid=true; camera_metadata.is_raw=true;
  camera_metadata.pixel_encoding=DecodedPixelEncoding::CameraLinearRgb;
  camera_metadata.decoder_backend="LibRaw camera-linear fixture";
  camera_metadata.decoded_color_space_name="Camera RGB";
  camera_metadata.black_subtraction_applied=true; camera_metadata.white_normalization_applied=true;
  camera_metadata.demosaic_applied=true; camera_metadata.white_balance_applied=true;
  camera_metadata.decoder_camera_to_xyz_valid=true;
  camera_metadata.decoder_camera_to_xyz={0.62,0.24,0.14,0.18,0.72,0.10,0.02,0.12,0.86};
  camera_metadata.decoder_camera_to_xyz_reference_white="D65";
  const auto camera_prepared=prepare_brush_color_source_image(camera_guide,guide_state,camera_metadata);
  CHECK(!camera_prepared.isNull() && camera_prepared.colorSpace()==QColorSpace(QColorSpace::SRgbLinear) &&
      std::abs(reinterpret_cast<const QRgbaFloat32*>(camera_prepared.constScanLine(0))[1].red()-3.0F)>0.05,
      "Untagged camera-native guides must undergo canonical characterization before sRGB edge analysis");
  QTemporaryDir guide_files;
  CHECK(guide_files.isValid(),"Source guide cache fixture directory must exist");
  const auto guide_path=guide_files.filePath("p3-guide.png");
  CHECK(p3_guide.convertToFormat(QImage::Format_RGBA64).save(guide_path),"P3 guide cache fixture must save");
  clear_decoded_image_cache();
  QImage first_guide,reused_guide,changed_guide;
  CHECK(decode_brush_color_source_image(guide_path,&first_guide,0,guide_state),
      "Source guide cache must admit a characterized source");
  auto scene_only=guide_state;
  scene_only.exposure=2.0; scene_only.raw_exposure=1.9; scene_only.saturation=30.0;
  scene_only.mask_stack={QVariantMap{{"kind","brush"},{"brushPoints",QVariantList{}}}};
  CHECK(decode_brush_color_source_image(guide_path,&reused_guide,0,scene_only) &&
      reused_guide.cacheKey()==first_guide.cacheKey(),
      "Scene exposure, Look, and mask edits must reuse immutable characterized guide pixels across strokes");
  auto normalize_edit=guide_state; normalize_edit.raw_black_level=0.1;
  CHECK(decode_brush_color_source_image(guide_path,&changed_guide,0,normalize_edit) &&
      changed_guide.cacheKey()!=first_guide.cacheKey(),
      "RAW normalization edits must invalidate characterized guide cache identity");
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
  std::array<LocalMask, 1> clone_masks{clone};
  apply_local_clone(&pixels,w,h,context,clone_masks);
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
  const std::array<LocalMask, 1> aligned_clone_masks{aligned_clone};
  apply_local_clone(&aligned_pixels,w,h,context,aligned_clone_masks);
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
  auto erased_pixels=original;
  const std::array<LocalMask, 1> erased_clone_masks{erased_clone};
  apply_local_clone(&erased_pixels,w,h,context,erased_clone_masks);
  CHECK(erased_pixels[30*w+110]==original[30*w+110] && std::abs(erased_pixels[30*w+160].red-0.15F)<0.01,
      "Erasing clone coverage restores the source without moving other stamps");

  // Legacy clone history is an ellipse, while its persisted constrained matte
  // owns erasures/color exclusions. Neither may be replaced by a circular cap.
  auto old_clone=mask;
  auto old_a=a,old_b=b;
  for (auto* stroke:{&old_a,&old_b}) {
    auto settings=stroke->value("settings").toMap(); settings.remove("rasterVersion");
    stroke->insert("settings",settings);
  }
  old_clone.insert("brushStrokes",QVariantList{old_a,old_b});
  old_clone.insert("feather",0.05);
  old_clone=mask_with_rebuilt_brush_matte(old_clone,QSize(w,h),"legacy-authority");
  auto authoritative=old_clone.value("segmentationData").toByteArray();
  authoritative[30*w+110]=0; // A source-aware exclusion at a geometrically solid center.
  old_clone.insert("segmentationData",authoritative);
  old_clone.insert("brushAutoMask",true);
  const auto compiled_old=compile_pipeline_local_mask(old_clone,
      LocalMaskRasterPolicy::RebuildEditableAtOutputResolution,QSize(w,h));
  CHECK(compiled_old.segmentation_mask[30*w+110]==0 &&
      compiled_old.segmentation_mask[30*w+122]==255,
      "Source-aware legacy coverage is authoritative at render resolution");
  auto old_pixels=original;
  const std::array<LocalMask, 1> compiled_old_masks{compiled_old};
  apply_local_clone(&old_pixels,w,h,context,compiled_old_masks);
  CHECK(old_pixels[30*w+110]==original[30*w+110] &&
      std::abs(old_pixels[30*w+122].red-0.21F)<1e-6 &&
      std::abs(old_pixels[30*w+172].red-0.21F)<1e-6,
      "Legacy ellipse wings retain each clone source while committed exclusions remain untouched");

  // The canonical crop compiler must preserve the exact original pixel pitch,
  // including off-crop dab centers and frozen legacy crop bases.
  auto source_mask=mask; source_mask.insert("coordinateSpace","source");
  const QRectF crop_area(0.5,0.0,0.4,1.0);
  const auto compiled_crop=compile_pipeline_local_mask(source_mask,
      LocalMaskRasterPolicy::RebuildEditableAtOutputResolution,QSize(81,h),crop_area);
  for (int y=0;y<h;++y) for (int x=0;x<81;++x)
    CHECK(compiled_crop.segmentation_mask[static_cast<std::size_t>(y*81+x)]==
        clone.segmentation_mask[static_cast<std::size_t>(y*w+x+100)],
        "Source-normalized brush crop must equal the corresponding full-resolution alpha pixels x="<<x<<" y="<<y<<
        " actual="<<int(compiled_crop.segmentation_mask[static_cast<std::size_t>(y*81+x)])<<
        " expected="<<int(clone.segmentation_mask[static_cast<std::size_t>(y*w+x+100)]));
  for (const auto crop:{QRect(110,0,1,h),QRect(0,30,w,1),QRect(110,30,1,1),QRect(0,0,1,1)}) {
    const QRectF line_area(static_cast<double>(crop.x())/(w-1),
        static_cast<double>(crop.y())/(h-1),static_cast<double>(crop.width()-1)/(w-1),
        static_cast<double>(crop.height()-1)/(h-1));
    for (const auto& history:{source_mask,old_clone}) {
      const auto full=compile_pipeline_local_mask(history,
          LocalMaskRasterPolicy::RebuildEditableAtOutputResolution,QSize(w,h));
      const auto line=compile_pipeline_local_mask(history,
          LocalMaskRasterPolicy::RebuildEditableAtOutputResolution,crop.size(),line_area,QSizeF(w-1,h-1));
      for (int y=0;y<crop.height();++y) for (int x=0;x<crop.width();++x)
        CHECK(line.segmentation_mask[static_cast<std::size_t>(y*crop.width()+x)]==
            full.segmentation_mask[static_cast<std::size_t>((y+crop.y())*w+x+crop.x())],
            "One-pixel source crops preserve exact editable and authoritative alpha");
      for (const auto& stroke:line.clone_strokes)
        CHECK(std::isfinite(stroke.source_x) && std::isfinite(stroke.source_y) &&
            std::isfinite(stroke.offset_x) && std::isfinite(stroke.offset_y),
            "Collapsed clone coordinate axes must remain finite");
    }
  }
  auto frozen=source_mask;
  frozen.insert("coordinateSpace","source-area");
  frozen.insert("coordinateArea",QVariantMap{{"x",0.5},{"y",0.0},{"width",0.4},{"height",1.0}});
  const auto frozen_compiled=compile_pipeline_local_mask(frozen,
      LocalMaskRasterPolicy::RebuildEditableAtOutputResolution,QSize(w,h),crop_area);
  CHECK(frozen_compiled.segmentation_mask==clone.segmentation_mask,
      "A frozen source-area basis must preserve its original stroke raster when the crop stays unchanged");

  // Compare actual cropped engine execution with a full-source operation.
  // Clone's source at x=30 lies outside the x=100..180 crop; all retouch tools
  // must share that full-source context, not operate on a stretched crop.
  for (const auto mode:{"clone","heal","remove","red-eye","skin-cleanup","frequency-smoothing","patch"}) {
    auto contextual_mask=source_mask;
    contextual_mask.insert("retouchMode",mode);
    contextual_mask.insert("removeStrength",100.0);
    auto full_mask=compile_pipeline_local_mask(contextual_mask,
        LocalMaskRasterPolicy::RebuildEditableAtOutputResolution,QSize(w,h));
    const auto crop_mask=compile_pipeline_local_mask(contextual_mask,
        LocalMaskRasterPolicy::RebuildEditableAtOutputResolution,QSize(81,h),crop_area);
    if (std::string_view(mode)=="patch") {
      full_mask.inpaint_patch_width=w; full_mask.inpaint_patch_height=h;
      full_mask.inpaint_patch_pixels.assign(w*h,ColorPixel{0.1F,0.4F,0.7F,1.0F});
      full_mask.inpaint_color_match=0.0; full_mask.inpaint_seam_feather=0.0;
    }
    ColorAdjustmentState full_state; full_state.retouch_masks={full_mask};
    auto full_pixels=original;
    CHECK(apply_color_layers_stage(&full_pixels,w,h,full_state),
        "Full-source retouch reference must render");
    auto source_context=std::make_shared<RetouchSourceContext>();
    source_context->pixels=original; source_context->masks={full_mask};
    source_context->width=w; source_context->height=h;
    source_context->crop_x=100; source_context->crop_y=0;
    source_context->crop_width=81; source_context->crop_height=h;
    ColorAdjustmentState cropped_state;
    cropped_state.retouch_masks={crop_mask}; cropped_state.retouch_source_context=source_context;
    std::vector<ColorPixel> crop_pixels(81*h);
    for (int y=0;y<h;++y) for (int x=0;x<81;++x)
      crop_pixels[static_cast<std::size_t>(y*81+x)]=original[static_cast<std::size_t>(y*w+x+100)];
    CHECK(apply_color_layers_stage(&crop_pixels,81,h,cropped_state),
        "Cropped retouch source adapter must render");
    for (int y=0;y<h;++y) for (int x=0;x<81;++x)
      CHECK(crop_pixels[static_cast<std::size_t>(y*81+x)]==
          full_pixels[static_cast<std::size_t>(y*w+x+100)],
          "Cropped retouch must equal exact full-source operation pixels for "<<mode);
    if (std::string_view(mode)=="clone")
      CHECK(std::abs(crop_pixels[30*81+60].red-0.15F)<1e-6,
          "A cropped clone must still access its source outside the crop");
  }
  for (const auto kind:{"radial","rectangle","linear","polygon","subject"}) {
    QVariantMap selection{{"kind",kind},{"coordinateSpace","source"},{"operation","add"},
        {"centerX",0.7},{"centerY",0.4},{"radiusX",0.2},{"radiusY",0.3},
        {"rotation",27.0},{"position",0.6},{"feather",0.1},
        {"polygonPoints",QVariantList{QVariantMap{{"x",0.4},{"y",0.2}},
            QVariantMap{{"x",0.9},{"y",0.3}},QVariantMap{{"x",0.6},{"y",0.8}}}},
        {"segmentationWidth",w},{"segmentationHeight",h},
        {"segmentationData",authoritative}};
    const auto full_selection=compile_pipeline_local_mask(selection,
        LocalMaskRasterPolicy::UseStoredRaster,QSize(w,h));
    const auto crop_selection=compile_pipeline_local_mask(selection,
        LocalMaskRasterPolicy::UseStoredRaster,QSize(81,h),crop_area);
    for (int y=0;y<h;y+=5) for (int x=0;x<81;x+=5) {
      const auto full_weight=local_mask_weight(static_cast<double>(x+100)/200,
          static_cast<double>(y)/100,original[static_cast<std::size_t>(y*w+x+100)],context,full_selection);
      const auto crop_weight=local_mask_weight(static_cast<double>(x)/80,
          static_cast<double>(y)/100,original[static_cast<std::size_t>(y*w+x+100)],context,crop_selection);
      CHECK(std::abs(full_weight-crop_weight)<1e-12,
          "Source-coordinate geometry/segmentation must sample identical coverage under crop for "<<kind);
    }
  }
  // Independent rectangular-area oracle: fine committed coverage and erased
  // holes must contribute to thumbnails even if no output center hits them.
  constexpr int aw=64,ah=32,ow=9,oh=7;
  QByteArray fine_alpha(aw*ah,char(0));
  for (int y=0;y<ah;++y) for (int x=0;x<aw;++x)
    fine_alpha[y*aw+x]=char((x%8==1 || (y%5==2 && x%3!=0)) ? 255 : 0);
  for (const auto flag:{"brushAutoMask","brushProtectEdges"}) for (const auto version:{1,2}) {
    auto constrained=source_mask;
    constrained.insert("segmentationWidth",aw); constrained.insert("segmentationHeight",ah);
    constrained.insert("segmentationData",fine_alpha); constrained.insert(flag,true);
    if (version==1) constrained.remove("brushStrokes");
    const auto reduced=compile_pipeline_local_mask(constrained,
        LocalMaskRasterPolicy::RebuildEditableAtOutputResolution,QSize(ow,oh));
    CHECK(reduced.segmentation_width==ow && reduced.segmentation_height==oh,
        "Committed source-aware matte must materialize at the execution raster size");
    for (int y=0;y<oh;++y) for (int x=0;x<ow;++x) {
      std::int64_t sum=0;
      for (int sy=0;sy<ah;++sy) for (int sx=0;sx<aw;++sx) {
        const auto overlap_x=std::max(0,std::min((x+1)*aw,(sx+1)*ow)-std::max(x*aw,sx*ow));
        const auto overlap_y=std::max(0,std::min((y+1)*ah,(sy+1)*oh)-std::max(y*ah,sy*oh));
        sum+=static_cast<unsigned char>(fine_alpha.at(sy*aw+sx))*overlap_x*overlap_y;
      }
      const auto expected=(sum+aw*ah/2)/(aw*ah);
      CHECK(reduced.segmentation_mask[static_cast<std::size_t>(y*ow+x)]==expected,
          "Source-aware legacy/new alpha minification must integrate every painted and erased source pixel");
    }
  }
  QImage baked_patch(8,4,QImage::Format_ARGB32);
  baked_patch.setColorSpace(QColorSpace(QColorSpace::SRgb)); baked_patch.fill(QColor(32,224,64));
  QByteArray baked_bytes; QBuffer baked_buffer(&baked_bytes); baked_buffer.open(QIODevice::WriteOnly);
  CHECK(baked_patch.save(&baked_buffer,"PNG"),"Baked patch coordinate fixture must encode");
  QByteArray baked_alpha(16*8,char(0));
  for (int y=2;y<6;++y) for (int x=4;x<12;++x) baked_alpha[y*16+x]=char(255);
  const QVariantMap baked{{"kind","brush"},{"toolRole","retouch"},{"coordinateSpace","source"},
      {"retouchMode","remove"},{"committed",true},{"removeStrength",100.0},
      {"segmentationWidth",16},{"segmentationHeight",8},{"segmentationData",baked_alpha},
      {"inpaintPatchData",baked_bytes},{"inpaintPatchX",4},{"inpaintPatchY",2},
      {"inpaintPatchWidth",8},{"inpaintPatchHeight",4},{"inpaintColorMatch",0.0},{"inpaintSeamFeather",0.0}};
  const auto full_baked=compile_pipeline_local_mask(baked,
      LocalMaskRasterPolicy::UseStoredRaster,QSize(32,16));
  CHECK(full_baked.inpaint_source_width==16 && full_baked.inpaint_source_height==8 &&
      full_baked.segmentation_width==32 && full_baked.segmentation_height==16,
      "Materializing alpha must retain the baked patch's original coordinate grid");
  const ColorPixel baked_background{0.1F,0.1F,0.1F,1.0F};
  std::vector<ColorPixel> baked_full(32*16,baked_background);
  const std::array<LocalMask, 1> full_baked_masks{full_baked};
  apply_local_inpaint_patches(&baked_full,32,16,context,full_baked_masks);
  CHECK(baked_full[0]==baked_background && baked_full[8*32+16]!=baked_background,
      "Scaled baked patch must affect its original support without touching outside pixels");
  for (const auto crop:{QRect(8,4,16,8),QRect(10,5,1,1)}) {
    const auto local_baked=compile_pipeline_local_mask(baked,LocalMaskRasterPolicy::UseStoredRaster,crop.size(),
        QRectF(static_cast<double>(crop.x())/31,static_cast<double>(crop.y())/15,
            static_cast<double>(crop.width()-1)/31,static_cast<double>(crop.height()-1)/15),QSizeF(31,15));
    std::vector<ColorPixel> baked_crop(static_cast<std::size_t>(crop.width()*crop.height()),baked_background);
    const std::array<LocalMask, 1> local_baked_masks{local_baked};
    apply_local_inpaint_patches(&baked_crop,crop.width(),crop.height(),context,local_baked_masks);
    for (int y=0;y<crop.height();++y) for (int x=0;x<crop.width();++x)
      CHECK(baked_crop[static_cast<std::size_t>(y*crop.width()+x)]==
          baked_full[static_cast<std::size_t>((y+crop.y())*32+x+crop.x())],
          "Cropped and one-pixel baked patch execution must retain exact full-source placement and coverage");
  }
  clone.clone_strokes.clear(); clone.brush_points={BrushPoint{.x=0.55,.y=0.3,.size=0.08}};
  clone.clone_source_x=0; clone.clone_offset_x=-0.55;
  pixels=original;
  clone_masks[0]=clone;
  apply_local_clone(&pixels,w,h,context,clone_masks);
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
  const std::array<LocalMask, 1> fill_masks{fill};
  apply_local_inpaint_patches(&pixels,w,h,context,fill_masks);
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
  const auto reference=convert_tagged_qimage_color_space(request.source_image,
      QColorSpace(QColorSpace::SRgb), QImage::Format_RGBA64,
      QImageColorConversionPurpose::EvidenceMeasurement);
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
