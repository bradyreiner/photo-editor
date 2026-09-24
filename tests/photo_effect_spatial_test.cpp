#include "photo_editor/pipeline/photo_effects.h"
#include "photo_editor/pipeline/photo_effect_codec.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <iostream>
#include <limits>

using namespace photo_editor::pipeline;
#define CHECK(condition, message) do { if (!(condition)) { std::cerr << message << '\n'; return 1; } } while (false)

float delta(const ColorPixel& a, const ColorPixel& b) {
  return std::max({std::abs(a.red-b.red), std::abs(a.green-b.green), std::abs(a.blue-b.blue), std::abs(a.alpha-b.alpha)});
}

int main() {
  PhotoEffectInstance motion; motion.id = "motion"; motion.type_id = "motion_blur";
  motion.motion.distance = 5;
  // Five uniform horizontal samples of an impulse form a five-pixel line.
  std::vector<ColorPixel> impulse(81, {0,0,0,1}); impulse[40] = {1,1,1,1};
  auto horizontal = impulse;
  CHECK(apply_photo_effect(&horizontal,9,9,motion).ok(), "Motion admission");
  for (int y=0;y<9;++y) for (int x=0;x<9;++x)
    CHECK(std::abs(horizontal[static_cast<std::size_t>(y*9+x)].red - (y==4 && x>=2 && x<=6 ? 0.2F : 0.0F)) < 1e-6F,
        "Horizontal impulse must have uniform five-pixel support");
  motion.motion.distance = 0.5;
  auto subpixel = impulse;
  CHECK(apply_photo_effect(&subpixel,9,9,motion).ok() && subpixel[40].red < 1 && subpixel[39].red > 0,
      "Subpixel motion must not silently become identity");
  motion.motion.distance = 5;
  motion.motion.angle = 90;
  auto vertical=impulse;
  CHECK(apply_photo_effect(&vertical,9,9,motion).ok(), "Vertical motion admission");
  for (int y=0;y<9;++y) for (int x=0;x<9;++x)
    CHECK(std::abs(vertical[static_cast<std::size_t>(y*9+x)].red-horizontal[static_cast<std::size_t>(x*9+y)].red)<1e-6F,
        "Rotating by 90 degrees must transpose the kernel");
  // Hidden RGB cannot leak through transparency; opaque red spreads red coverage.
  auto transparent=std::vector<ColorPixel>(81,{0,100,0,0}); transparent[40]={2,0,0,1};
  motion.motion.angle=0;
  CHECK(apply_photo_effect(&transparent,9,9,motion).ok(), "Transparent motion");
  CHECK(std::abs(transparent[40].red-2)<1e-6F && transparent[40].green==0 && std::abs(transparent[40].alpha-0.2F)<1e-6F,
      "Motion must filter premultiplied HDR without hidden-color fringes");
  PhotoEffectInstance sharp; sharp.id="sharp"; sharp.type_id="unsharp_mask"; sharp.unsharp.sigma=1.25;
  // Constant straight color remains constant across an alpha edge.
  std::vector<ColorPixel> constant(81,{2,-0.25F,0.5F,0});
  for(std::size_t i=0;i<constant.size();++i) constant[i].alpha=static_cast<float>(i%7)/6;
  const auto original_constant=constant;
  CHECK(apply_photo_effect(&constant,9,9,sharp).ok(),"Unsharp constant color");
  for(std::size_t i=0;i<constant.size();++i)
    CHECK(delta(constant[i],original_constant[i])<1e-6F,"Unsharp must preserve alpha and constant HDR color");
  auto sharpened=impulse;
  CHECK(apply_photo_effect(&sharpened,9,9,sharp).ok(),"Unsharp impulse");
  CHECK(sharpened[40].red>1 && sharpened[39].red<0 && sharpened[40].alpha==1,
      "Unsharp must enhance detail without clipping HDR or negative undershoot");
  sharp.unsharp.threshold=1;
  sharpened=impulse;
  CHECK(apply_photo_effect(&sharpened,9,9,sharp).ok(),"Unsharp threshold");
  for(std::size_t i=0;i<impulse.size();++i) CHECK(delta(sharpened[i],impulse[i])==0,"Threshold must suppress weaker detail");
  // Compare independent whole-frame execution with small padded tiles, fractional
  // document scaling, all edge policies, soft coverage, HDR and hidden color.
  constexpr int width=47,height=31;
  std::vector<ColorPixel> source(width*height);
  std::vector<float> coverage(source.size());
  for(std::size_t i=0;i<source.size();++i) {
    source[i]={static_cast<float>(i%19)/7-0.3F,static_cast<float>(i%11)/8,0.2F,static_cast<float>(i%5)/4};
    coverage[i]=i%3==0 ? 0.0F : static_cast<float>(i%7)/6;
  }
  for(const auto edge:{PhotoEffectEdgeMode::Transparent,PhotoEffectEdgeMode::Extend,PhotoEffectEdgeMode::Reflect}) {
    motion.motion={9.5,37.25,edge}; motion.amount=0.65; motion.mask_ref="selection";
    sharp.unsharp={1.75,1.35,0.03,edge}; sharp.mask_ref="selection";
    std::vector effects{motion,sharp};
    const auto encoded=serialize_photo_effect_stack(effects);
    CHECK(encoded,"Spatial effects serialize");
    const auto decoded=deserialize_photo_effect_stack(*encoded);
    CHECK(decoded && *decoded==effects,"Spatial parameters roundtrip exactly");
    PhotoEffectRenderOptions options; options.scale_x=0.7;options.scale_y=1.2;
    const std::vector<PhotoEffectMaskBinding> masks{{"selection",coverage}};
    auto whole=source,tiled=source;
    CHECK(apply_photo_effect_stack(&whole,width,height,effects,options,masks).ok(),"Whole-frame stack");
    CHECK(apply_photo_effect_stack_tiled(&tiled,width,height,effects,options,masks,7).ok(),"Padded tiled stack");
    float worst=0;
    for(std::size_t i=0;i<whole.size();++i) {
      worst=std::max(worst,delta(whole[i],tiled[i]));
      if(coverage[i]==0) CHECK(delta(whole[i],source[i])==0,"Zero mask must preserve pixels exactly");
    }
    CHECK(worst<2e-6F,"Spatial stack must agree across tile boundaries");
    std::cout << "edge=" << static_cast<int>(edge) << " tiled_max_error=" << worst << '\n';
  }
  for(auto effect:{motion,sharp}) {
    effect.mask_ref.clear();
    auto pixels=source;
    if (effect.type_id == "motion_blur") {
      PhotoEffectRenderOptions one_buffer;
      one_buffer.max_scratch_bytes = source.size() * sizeof(ColorPixel);
      CHECK(apply_photo_effect(&pixels,width,height,effect,one_buffer).ok(),
          "Motion blur must fit a single transactional output buffer");
      pixels = source;
    }
    PhotoEffectRenderOptions limited; limited.max_scratch_bytes=1;
    CHECK(apply_photo_effect(&pixels,width,height,effect,limited).status==PhotoEffectStatus::ResourceLimit,
        "Budget exhaustion must fail transactionally");
    for(std::size_t i=0;i<source.size();++i) CHECK(delta(pixels[i],source[i])==0,"Resource failure cannot publish partial output");
    std::atomic_bool cancelled{true};
    PhotoEffectRenderOptions stopped;
    stopped.cancellation.cancellation_requested = &cancelled;
    CHECK(apply_photo_effect(&pixels,width,height,effect,stopped).status==PhotoEffectStatus::Cancelled,
        "Cancellation must reject publication");
    for(std::size_t i=0;i<source.size();++i) CHECK(delta(pixels[i],source[i])==0,"Cancelled output remains exact");
    effect.algorithm_version=99;effect.opaque_parameters="future exact parameters";
    const auto encoded=serialize_photo_effect_stack(std::vector{effect});
    CHECK(encoded,"Unknown versions remain preservable");
    const auto decoded=deserialize_photo_effect_stack(*encoded);
    CHECK(decoded && decoded->front()==effect,"Unknown envelope must survive");
    CHECK(apply_photo_effect(&pixels,width,height,effect).status==PhotoEffectStatus::Unsupported,"Unknown version cannot render");
  }
  motion.motion.angle=std::numeric_limits<double>::quiet_NaN(); motion.enabled=false;
  CHECK(!serialize_photo_effect_stack(std::vector{motion}),"Disabled invalid motion settings cannot persist");
  sharp.unsharp.strength=5.1;
  CHECK(!serialize_photo_effect_stack(std::vector{sharp}),"Invalid sharpening strength cannot persist");
  return 0;
}
