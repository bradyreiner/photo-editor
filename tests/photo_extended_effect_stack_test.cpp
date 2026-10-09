#include "photo_editor/pipeline/photo_effects.h"
#include "photo_editor/pipeline/photo_effect_codec.h"
#include "photo_editor/pipeline/photo_effect_registry.h"
#include <iostream>
#include <cmath>
#include <atomic>
#include <bit>
#include <cstdlib>
#include <chrono>
#include <algorithm>
using namespace photo_editor::pipeline;
#define CHECK(c,m) do {if(!(c)){std::cerr<<type<<": "<<m<<'\n';return 1;}}while(false)
int main() {
  constexpr int width=43,height=29;
  std::vector<ColorPixel> source(static_cast<std::size_t>(width)*height);
  std::vector<float> mask(source.size());
  for(std::size_t i=0;i<source.size();++i) {
    source[i]={static_cast<float>(i%23)/10-.1F,static_cast<float>(i%31)/12,
        static_cast<float>(i%11)/6,static_cast<float>(i%7)/6};
    mask[i]=static_cast<float>(i%5)/4;
  }
  PhotoEffectRenderOptions options;
  options.luminance_weights={.343966,.728166,-.072132};
  for(const auto& descriptor:photo_effect_registry) {
    const auto type=descriptor.type_id;
    if(!is_optical_photo_effect(type)&&!is_artistic_photo_effect(type))continue;
    PhotoEffectInstance effect;effect.id="stack-check";effect.type_id=type;effect.mask_ref="coverage";effect.amount=.65;
    effect.optical.halation.radius=1;effect.optical.anamorphic_streaks.half_length=3;
    effect.optical.starburst.radius=3;effect.optical.lens_bokeh.radius=2;
    effect.optical.tilt_shift.blur_radius=1;effect.optical.zoom_blur.radius=3;
    effect.optical.spin_blur.angle_degrees=8;effect.optical.surface_blur.radius=1;
    effect.optical.high_pass.radius=1;
    if(type=="gradient_map") effect.artistic.gradient_map.stops={{0, {-.1F,.2F,.4F}}, {.4,{.8F,.1F,.2F}}, {1,{2,.9F,.2F}}};
    if(type=="displacement_map") {
      auto& map=effect.artistic.displacement_map;map.asset_ref=std::string(64,'a');map.width=5;map.height=3;
      auto rgba=std::make_shared<std::vector<float>>(60,.5F);
      for(std::size_t i=0;i<rgba->size();i+=4) {(*rgba)[i]=.7F;(*rgba)[i+1]=.2F;(*rgba)[i+3]=1;}
      map.rgba=rgba;map.maximum_displacement=3;
    }
    for(int variant=0;variant<(type=="dither"?2:1);++variant) {
      effect.artistic.dither.mode=static_cast<ArtisticDitherMode>(variant);
      const std::array<PhotoEffectInstance,1> stack{effect};
      const std::array<PhotoEffectMaskBinding,1> bindings{{{"coverage",mask}}};
      const auto encoded=serialize_photo_effect_stack(stack);
      CHECK(encoded,"Persisted stack serializes");
      const auto decoded=deserialize_photo_effect_stack(*encoded);
      CHECK(decoded && decoded->size()==1 && decoded->front()==effect,"Every numeric/structured setting round-trips exactly");
      auto expected=source;
      auto result=apply_photo_effect_stack(&expected,width,height,stack,options,bindings);
      CHECK(result.ok(),result.error);
      auto tiled=source;
      result=apply_photo_effect_stack_tiled(&tiled,width,height,stack,options,bindings,13);
      CHECK(result.ok(),result.error);
      auto private_raster=source;
      result=apply_photo_effect_stack_to_private_raster(&private_raster,width,height,stack,options,bindings,13);
      CHECK(result.ok(),result.error);
      auto streamed=source;
      result=render_photo_effect_tiles(source,width,height,stack,[&](const PhotoEffectTileView& tile) {
        for(int y=0;y<tile.height;++y)std::copy_n(tile.pixels.data()+y*tile.stride,tile.width,
            streamed.data()+(tile.y+y)*width+tile.x);
        return true;
      },options,bindings,13);
      CHECK(result.ok(),result.error);
      for(std::size_t i=0;i<source.size();++i) {
        const auto near=[](const ColorPixel& a,const ColorPixel& b){return std::max({std::abs(a.red-b.red),std::abs(a.green-b.green),std::abs(a.blue-b.blue),std::abs(a.alpha-b.alpha)})<2e-5F;};
        CHECK(near(expected[i],tiled[i])&&near(expected[i],private_raster[i])&&near(expected[i],streamed[i]),"Whole, tiled, private and streamed pixels agree");
        if(mask[i]==0)CHECK(expected[i]==source[i],"Zero selection coverage is bit-exact identity");
      }
      auto invalid=source; auto missing=apply_photo_effect_stack(&invalid,width,height,stack,options);
      CHECK(!missing.ok()&&invalid==source,"Missing selection binding fails transactionally");
      auto limited=options;limited.max_scratch_bytes=1;
      invalid=source;
      CHECK(apply_photo_effect_stack(&invalid,width,height,stack,limited,bindings).status==PhotoEffectStatus::ResourceLimit&&invalid==source,"Scratch admission preserves source");
      std::atomic_bool cancelled{true};auto cancelled_options=options;cancelled_options.cancellation.cancellation_requested=&cancelled;
      invalid=source;
      CHECK(apply_photo_effect_stack(&invalid,width,height,stack,cancelled_options,bindings).status==PhotoEffectStatus::Cancelled&&invalid==source,"Cancellation preserves source");
    }
  }
  if (std::getenv("PHOTO_EFFECT_4K_REFERENCE")) {
    constexpr int full_width=3840,full_height=2160;
    std::vector<ColorPixel> full(static_cast<std::size_t>(full_width)*full_height);
    for(std::size_t i=0;i<full.size();++i) full[i]={static_cast<float>(i%997)/997,static_cast<float>(i%431)/431,
        static_cast<float>(i%113)/113,1};
    const auto pixel_hash=[](const ColorPixel& pixel,std::size_t index) {
      std::uint64_t value=static_cast<std::uint64_t>(index)+1469598103934665603ULL;
      for(const auto c:{pixel.red,pixel.green,pixel.blue,pixel.alpha})value=(value^std::bit_cast<std::uint32_t>(c))*1099511628211ULL;
      return value;
    };
    for(const auto type: {"zoom_blur","spin_blur","dither"}) {
      PhotoEffectInstance effect;effect.id="full-resolution";effect.type_id=type;
      effect.optical.zoom_blur.samples=4;effect.optical.spin_blur.samples=4;
      effect.artistic.dither.mode=ArtisticDitherMode::FloydSteinberg;
      const std::array<PhotoEffectInstance,1> stack{effect};
      const auto start=std::chrono::steady_clock::now();
      auto expected=full;const auto applied=apply_photo_effect_stack(&expected,full_width,full_height,stack);
      CHECK(applied.ok(),applied.error);
      std::uint64_t reference=0;for(std::size_t i=0;i<expected.size();++i)reference^=pixel_hash(expected[i],i);
      expected.clear();expected.shrink_to_fit();
      std::uint64_t actual=0;std::size_t received=0;
      const auto streamed=render_photo_effect_tiles(full,full_width,full_height,stack,[&](const PhotoEffectTileView& tile){
        for(int y=0;y<tile.height;++y)for(int x=0;x<tile.width;++x) {
          const auto index=static_cast<std::size_t>(tile.y+y)*full_width+static_cast<std::size_t>(tile.x+x);
          actual^=pixel_hash(tile.pixels[static_cast<std::size_t>(y)*static_cast<std::size_t>(tile.stride)+static_cast<std::size_t>(x)],index);
          ++received;
        }
        return true;
      });
      CHECK(streamed.ok(),streamed.error);CHECK(received==full.size()&&actual==reference,"4K streamed result equals whole-frame reference under default 512MiB scratch budget");
      std::cout<<"4K global operator "<<type<<" whole/stream exact hash="<<reference<<" elapsedMs="
          <<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count()<<'\n';
    }
  }
  std::cout<<"Twenty extended stack operators: exact codec, AP0, amount/selection coverage, tile parity, resource limits and cancellation passed\n";
}
