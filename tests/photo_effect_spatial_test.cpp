#include "photo_editor/pipeline/photo_effects.h"
#include "photo_editor/pipeline/photo_effect_codec.h"
#include "photo_editor/pipeline/cpu_work_scheduler.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <thread>

using namespace photo_editor::pipeline;
#define CHECK(condition, message) do { if (!(condition)) { std::cerr << message << '\n'; return 1; } } while (false)

float delta(const ColorPixel& a, const ColorPixel& b) {
  return std::max({std::abs(a.red-b.red), std::abs(a.green-b.green), std::abs(a.blue-b.blue), std::abs(a.alpha-b.alpha)});
}

int gaussian_worker_contracts() {
  // Uneven widths cover every tail length in the four-pixel convolution block.
  for (const int width : {1, 2, 3, 4, 125, 126, 127}) {
    constexpr int height = 131;
    std::vector<ColorPixel> source(static_cast<std::size_t>(width) * height);
    for (std::size_t i = 0; i < source.size(); ++i)
      source[i] = {static_cast<float>(i % 19U) / 4.0F - .25F, .3F, 2.0F,
          static_cast<float>(i % 7U) / 6.0F};
    for (const auto* type : {"gaussian_blur", "unsharp_mask", "optical_diffusion"}) {
      PhotoEffectInstance effect;
      effect.id = "workers"; effect.type_id = type;
      effect.gaussian.sigma = 12.0;
      effect.unsharp.sigma = 12.0;
      effect.diffusion.radius = 12.0;
      auto expected = source;
      CHECK(apply_photo_effect(&expected, width, height, effect).ok(), "Parallel Gaussian-family render");
      std::array<std::vector<ColorPixel>, 2> nested{source, source};
      std::array<PhotoEffectResult, 2> results;
      run_cpu_parallel_for(16, [&](int index) {
        if (index != 0 && index != 8) return;
        const auto i = static_cast<std::size_t>(index / 8);
        results[i] = apply_photo_effect(&nested[i], width, height, effect);
      });
      CHECK(results[0].ok() && results[1].ok() && nested[0] == expected && nested[1] == expected,
          "Nested scheduler callers must finish with exact Gaussian-family output");
      PhotoEffectRenderOptions limited;
      limited.max_scratch_bytes = 1;
      auto rejected = source;
      CHECK(apply_photo_effect(&rejected, width, height, effect, limited).status == PhotoEffectStatus::ResourceLimit &&
          rejected == source, "Gaussian-family resource failure must preserve source");
    }
  }
  // Request cancellation from another thread during a large render workload.
  // This covers API lifetime/publication, not a specific processing stage.
  for (const auto* type : {"gaussian_blur", "unsharp_mask", "optical_diffusion"}) {
    constexpr int width = 1024, height = 1024;
    const std::vector<ColorPixel> source(static_cast<std::size_t>(width) * height, {2.0F, .3F, -.2F, .5F});
    auto pixels = source;
    PhotoEffectInstance effect;
    effect.id = "cancel"; effect.type_id = type;
    effect.gaussian.sigma = 120.0;
    effect.unsharp.sigma = 120.0;
    effect.diffusion.radius = 120.0;
    std::atomic_bool cancel{false};
    PhotoEffectRenderOptions options;
    options.cancellation.cancellation_requested = &cancel;
    PhotoEffectResult result;
    {
      std::jthread canceller([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        cancel.store(true);
      });
      result = apply_photo_effect(&pixels, width, height, effect, options);
    }
    CHECK(result.status == PhotoEffectStatus::Cancelled && pixels == source,
        "Asynchronous Gaussian-family cancellation must preserve source and join workers");
    cancel.store(false);
    pixels = source;
    {
      std::jthread canceller([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        cancel.store(true);
      });
      result = apply_photo_effect_stack_to_private_raster(&pixels, width, height,
          std::span<const PhotoEffectInstance>(&effect, 1), options);
    }
    CHECK(result.status == PhotoEffectStatus::Cancelled,
        "Private-row cancellation must join workers before the owner discards its raster");
  }
  return 0;
}

int streamed_tile_contracts() {
  constexpr int width = 513, height = 257;
  std::vector<ColorPixel> input(static_cast<std::size_t>(width) * height);
  std::vector<float> mask(input.size());
  for (std::size_t i = 0; i < input.size(); ++i) {
    input[i] = {static_cast<float>(i % 17U) / 4 - .2F, .3F, 1.7F,
        static_cast<float>(i % 7U) / 6};
    mask[i] = static_cast<float>(i % 9U) / 8;
  }
  const auto original = input;
  for (const auto edge : {PhotoEffectEdgeMode::Transparent, PhotoEffectEdgeMode::Extend, PhotoEffectEdgeMode::Reflect}) {
    PhotoEffectInstance effect;
    effect.id = "streamed"; effect.gaussian = {2.0, edge};
    effect.amount = .73; effect.mask_ref = "mask";
    const std::vector effects{effect};
    const std::vector<PhotoEffectMaskBinding> masks{{"mask", mask}};
    auto expected = input, actual = input;
    CHECK(apply_photo_effect_stack_tiled(&expected, width, height, effects, {}, masks, 32).ok(),
        "Reference tiled effect must render");
    for (const int tile_size : {4, 32, 128, 512}) {
      auto owned = input;
      CHECK(apply_photo_effect_stack_to_private_raster(&owned, width, height, effects,
          {}, masks, tile_size).ok() && owned == expected,
          "Delayed rows must preserve exact source halos across ring wrap and edge modes");
    }
    const std::vector<float> invalid_coverage(1, 1);
    const std::vector<PhotoEffectMaskBinding> invalid_masks{{"mask", invalid_coverage}};
    PhotoEffectRenderOptions invalid_budget;
    invalid_budget.max_scratch_bytes = 1;
    CHECK(apply_photo_effect_stack_tiled(&actual, width, height, effects, invalid_budget,
        invalid_masks, 32).status == PhotoEffectStatus::Invalid && actual == input,
        "Validate mask bindings before output allocation or scratch admission");
    std::vector<int> visits(input.size());
    PhotoEffectRenderOptions bounded;
    bounded.max_scratch_bytes = 1024 * 1024; // Less than one complete output.
    auto owned = input;
    CHECK(apply_photo_effect_stack_to_private_raster(&owned, width, height, effects,
        bounded, masks, 32).ok() && owned == expected,
        "Private raster must render within less than one full output of scratch");
    auto row_budget = bounded;
    row_budget.max_scratch_bytes = static_cast<std::size_t>(width) * 38 * sizeof(ColorPixel);
    owned = input;
    CHECK(apply_photo_effect_stack_to_private_raster(&owned, width, height, effects,
        row_budget, masks, 32).status == PhotoEffectStatus::ResourceLimit && owned == input,
        "Pending rows and tile scratch must share the same memory budget");
    CHECK(apply_photo_effect_stack_tiled(&actual, width, height, effects, bounded, masks, 32).status ==
        PhotoEffectStatus::ResourceLimit && actual == input, "Transactional API must retain its output budget");
    const auto result = render_photo_effect_tiles(input, width, height, effects,
        [&](const PhotoEffectTileView& tile) {
          for (int y = 0; y < tile.height; ++y)
            for (int x = 0; x < tile.width; ++x) {
              const auto target = static_cast<std::size_t>(tile.y + y) * width +
                  static_cast<std::size_t>(tile.x + x);
              actual[target] = tile.pixels[static_cast<std::size_t>(y) *
                  static_cast<std::size_t>(tile.stride) + static_cast<std::size_t>(x)];
              ++visits[target];
            }
          return true;
        }, bounded, masks, 32);
    CHECK(result.ok() && actual == expected && input == original &&
        std::all_of(visits.begin(), visits.end(), [](int count) { return count == 1; }),
        "Streamed tiles must exactly match reference with one visit per pixel");
    int emitted = 0;
    CHECK(render_photo_effect_tiles(input, width, height, effects,
        [&](const auto&) { return ++emitted < 2; }, bounded, masks, 32).status == PhotoEffectStatus::Invalid &&
        emitted == 2 && input == original, "Sink rejection must stop delivery without changing input");
    std::atomic_bool cancelled{false};
    bounded.cancellation.cancellation_requested = &cancelled;
    emitted = 0;
    CHECK(render_photo_effect_tiles(input, width, height, effects,
        [&](const auto&) { ++emitted; cancelled.store(true); return true; }, bounded, masks, 32).status ==
        PhotoEffectStatus::Cancelled && emitted == 1 && input == original,
        "Cancellation after a delivered tile must stop delivery and preserve input");
    bounded = {};
    bounded.max_scratch_bytes = 1;
    emitted = 0;
    CHECK(render_photo_effect_tiles(input, width, height, effects,
        [&](const auto&) { ++emitted; return true; }, bounded, masks, 32).status ==
        PhotoEffectStatus::ResourceLimit && emitted == 0, "Tile scratch budget must remain enforced");
  }
  int emitted = 0;
  CHECK(render_photo_effect_tiles(input, width, height, {},
      [&](const auto&) { ++emitted; return true; }).status == PhotoEffectStatus::Identity && emitted == 0,
      "Identity must not deliver tiles");
  return 0;
}

int retained_tile_convolution_contracts() {
  constexpr int width = 43, height = 29;
  std::vector<ColorPixel> source(width * height);
  std::vector<float> mask(source.size());
  for (std::size_t i = 0; i < source.size(); ++i) {
    source[i] = {static_cast<float>(i % 31U) / 6.0F - .4F,
        static_cast<float>(i % 13U) / 7.0F, 1.5F,
        static_cast<float>(i % 7U) / 6.0F};
    mask[i] = static_cast<float>(i % 5U) / 4.0F;
  }
  const std::vector bindings{PhotoEffectMaskBinding{"selection", mask}};
  std::size_t cases = 0;
  for (const auto* type : {"gaussian_blur", "unsharp_mask", "optical_diffusion"}) {
    for (const auto edge : {PhotoEffectEdgeMode::Transparent, PhotoEffectEdgeMode::Extend,
             PhotoEffectEdgeMode::Reflect}) {
      if (std::string_view(type) == "optical_diffusion" && edge != PhotoEffectEdgeMode::Extend) continue;
      for (const double sigma : {.35, 3.1}) {
        for (const bool anisotropic : {false, true}) {
          PhotoEffectInstance effect;
          effect.id = "retained"; effect.type_id = type; effect.amount = .73;
          effect.mask_ref = "selection";
          effect.gaussian = {sigma, edge};
          effect.unsharp = {sigma, 1.2, .01, edge};
          effect.diffusion.radius = sigma;
          PhotoEffectRenderOptions options;
          options.scale_x = anisotropic ? .5 : 1.0;
          options.scale_y = anisotropic ? 1.75 : 1.0;
          const std::vector effects{effect};
          auto whole = source;
          CHECK(apply_photo_effect_stack(&whole, width, height, effects, options, bindings).ok(),
              "Retained convolution reference failed");
          for (const int tile_size : {7, 16, 64}) {
            auto tiled = source;
            CHECK(apply_photo_effect_stack_tiled(&tiled, width, height, effects, options, bindings, tile_size).ok(),
                "Retained convolution tile failed");
            CHECK(tiled == whole, "Retained convolution must preserve every RGB and alpha float");
            auto private_raster = source;
            CHECK(apply_photo_effect_stack_to_private_raster(&private_raster, width, height,
                effects, options, bindings, tile_size).ok(), "Private retained convolution failed");
            CHECK(private_raster == whole, "Private retained rows must match whole-image convolution");
            ++cases;
          }
        }
      }
    }
  }
  std::cout << "retained_tile_exact_cases=" << cases << '\n';
  return 0;
}

int multi_effect_retained_contracts() {
  constexpr int width = 43, height = 29;
  std::vector<ColorPixel> source(width * height);
  std::vector<float> mask(source.size());
  for (std::size_t i = 0; i < source.size(); ++i) {
    source[i] = {static_cast<float>(i % 29U) / 5.0F - .3F, .4F, 1.5F,
        static_cast<float>(i % 7U) / 6.0F};
    mask[i] = static_cast<float>(i % 5U) / 4.0F;
  }
  const std::vector bindings{PhotoEffectMaskBinding{"selection", mask}};
  std::size_t cases = 0;
  for (int recipe = 0; recipe < 6; ++recipe) {
    for (const auto edge : {PhotoEffectEdgeMode::Transparent, PhotoEffectEdgeMode::Extend,
             PhotoEffectEdgeMode::Reflect}) {
      for (const bool anisotropic : {false, true}) {
        PhotoEffectInstance gaussian;
        gaussian.id = "g"; gaussian.type_id = "gaussian_blur"; gaussian.amount = .73;
        gaussian.mask_ref = "selection"; gaussian.gaussian = {3.1, edge};
        auto diffusion = gaussian;
        diffusion.id = "d"; diffusion.type_id = "optical_diffusion"; diffusion.diffusion.radius = 2.7;
        auto unsharp = gaussian;
        unsharp.id = "u"; unsharp.type_id = "unsharp_mask"; unsharp.unsharp = {1.3, 1.2, .01, edge};
        auto box = gaussian;
        box.id = "b"; box.type_id = "box_blur"; box.box = {3, edge};
        std::vector<PhotoEffectInstance> effects;
        if (recipe == 0) effects = {gaussian, diffusion};
        if (recipe == 1) effects = {diffusion, gaussian};
        if (recipe == 2) effects = {unsharp, diffusion};
        if (recipe == 3) effects = {gaussian, unsharp, diffusion};
        if (recipe == 4) {
          unsharp.enabled = false;
          auto disabled = box; disabled.amount = 0.0;
          effects = {unsharp, gaussian, diffusion, disabled};
        }
        if (recipe == 5) effects = {gaussian, box};
        PhotoEffectRenderOptions options;
        options.scale_x = anisotropic ? .5 : 1.0;
        options.scale_y = anisotropic ? 1.75 : 1.0;
        auto whole = source;
        CHECK(apply_photo_effect_stack(&whole, width, height, effects, options, bindings).ok(),
            "Multi-effect reference failed");
        for (const int tile_size : {7, 16, 64}) {
          auto tiled = source, private_raster = source;
          CHECK(apply_photo_effect_stack_tiled(&tiled, width, height, effects, options, bindings, tile_size).ok(),
              "Multi-effect retained tile failed");
          CHECK(apply_photo_effect_stack_to_private_raster(&private_raster, width, height,
              effects, options, bindings, tile_size).ok(), "Multi-effect private tile failed");
          CHECK(tiled == whole && private_raster == whole,
              "Multi-effect retained regions must preserve every RGB and alpha float");
          ++cases;
        }
      }
    }
  }
  {
    PhotoEffectInstance first;
    first.id = "first"; first.type_id = "gaussian_blur"; first.gaussian.sigma = 2.0;
    first.mask_ref = "selection";
    auto second = first;
    second.id = "second"; second.type_id = "optical_diffusion"; second.diffusion.radius = 3.0;
    const std::vector effects{first, second};
    auto invalid_mask = mask;
    invalid_mask.back() = std::numeric_limits<float>::quiet_NaN();
    const std::vector invalid_bindings{PhotoEffectMaskBinding{"selection", invalid_mask}};
    auto actual = source;
    CHECK(apply_photo_effect_stack_tiled(&actual, width, height, effects, {}, invalid_bindings, 7).status ==
        PhotoEffectStatus::Invalid && actual == source, "Invalid later-tile masks must reject the transaction");
    PhotoEffectRenderOptions options;
    options.max_scratch_bytes = 1;
    CHECK(apply_photo_effect_stack_tiled(&actual, width, height, effects, options, bindings, 7).status ==
        PhotoEffectStatus::ResourceLimit && actual == source, "Multi-effect crop must retain the scratch budget");
    options = {};
    std::atomic_bool cancelled{true};
    options.cancellation.cancellation_requested = &cancelled;
    CHECK(apply_photo_effect_stack_tiled(&actual, width, height, effects, options, bindings, 7).status ==
        PhotoEffectStatus::Cancelled && actual == source, "Cancelled multi-effect render must preserve input");
    cancelled.store(false);
    int emitted = 0;
    CHECK(render_photo_effect_tiles(source, width, height, effects, [&](const auto&) {
      ++emitted; cancelled.store(true); return true;
    }, options, bindings, 7).status == PhotoEffectStatus::Cancelled && emitted == 1,
        "Cancellation after a multi-effect tile must stop delivery");
    auto invalid_effects = effects;
    invalid_effects.back().algorithm_version = 999;
    CHECK(!apply_photo_effect_stack_tiled(&actual, width, height, invalid_effects, {}, bindings, 7).ok() &&
        actual == source, "Unsupported later operator must reject the whole transaction");
  }
  // Discarded halo values must not cause a later diffusion preparation overflow.
  std::vector<ColorPixel> hdr(91 * 73);
  for (std::size_t i = 0; i < hdr.size(); ++i)
    hdr[i] = {static_cast<float>(i % 19U) / 18.0F * 3e38F, .1F, .2F, 1.0F};
  PhotoEffectInstance gaussian;
  gaussian.id = "g"; gaussian.type_id = "gaussian_blur";
  gaussian.gaussian = {3.0, PhotoEffectEdgeMode::Extend};
  auto diffusion = gaussian;
  diffusion.id = "d"; diffusion.type_id = "optical_diffusion";
  diffusion.diffusion.radius = 2.0; diffusion.diffusion.warmth = 1.0; diffusion.diffusion.strength = .01;
  const std::vector effects{gaussian, diffusion};
  auto whole = hdr, tiled = hdr;
  CHECK(apply_photo_effect_stack(&whole, 91, 73, effects).ok(), "HDR multi-effect reference failed");
  CHECK(apply_photo_effect_stack_tiled(&tiled, 91, 73, effects, {}, {}, 16).ok(),
      "Discarded halo must not overflow a later preparation stage");
  CHECK(tiled == whole, "HDR retained region floats changed");
  std::cout << "multi_effect_exact_cases=" << cases << " hdr_halo_exact=1\n";
  return 0;
}

int main() {
  CHECK(multi_effect_retained_contracts() == 0, "Multi-effect retained contracts");
  CHECK(retained_tile_convolution_contracts() == 0, "Retained tile contracts");
  if (streamed_tile_contracts() != 0) return 1;
  CHECK(gaussian_worker_contracts() == 0, "Gaussian worker contracts");
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
  PhotoEffectInstance shutter; shutter.id="shutter"; shutter.type_id="dragged_shutter";
  shutter.dragged_shutter={7.0,0.0,0.8,0.0,PhotoEffectEdgeMode::Extend,true};
  std::vector<ColorPixel> shutter_source(81,{0,0,0,1}); shutter_source[40]={1,1,1,1};
  std::vector<float> protected_subject(81,1.0F);
  shutter.mask_ref="subject";
  auto protected_result=shutter_source;
  CHECK(apply_photo_effect(&protected_result,9,9,shutter,{},protected_subject).ok(),
      "Dragged shutter subject protection admission");
  for(std::size_t i=0;i<protected_result.size();++i)
    CHECK(delta(protected_result[i],shutter_source[i])==0,
        "Full subject mask must preserve protected pixels exactly");
  shutter.dragged_shutter.protect_subject=false;
  auto full_effect=shutter_source;
  CHECK(apply_photo_effect(&full_effect,9,9,shutter,{},protected_subject).ok() &&
      delta(full_effect[4*9+4],shutter_source[4*9+4])>0,
      "Without protection, selection coverage retains normal effect-mask semantics");
  shutter.dragged_shutter.protect_subject=true;
  std::vector<float> feathered_subject(81,0.5F);
  auto feathered_effect=shutter_source;
  CHECK(apply_photo_effect(&feathered_effect,9,9,shutter,{},feathered_subject).ok(),
      "Dragged shutter feathered protection render");
  const auto expected_feathered=std::lerp(shutter_source[40].red,full_effect[40].red,0.5F);
  CHECK(std::abs(feathered_effect[40].red-expected_feathered)<1e-6F,
      "Soft subject-mask coverage must blend the shutter edge without a hard fringe");
  shutter.mask_ref.clear();
  shutter.dragged_shutter.falloff=0.9;
  shutter.dragged_shutter.highlight_weighting=0.0;
  auto tapered=shutter_source;
  CHECK(apply_photo_effect(&tapered,9,9,shutter).ok(),"Dragged shutter tapered render");
  CHECK(delta(tapered[4*9+4],shutter_source[4*9+4])>
      delta(tapered[0],shutter_source[0]),
      "Dragged shutter must taper toward frame edges beyond uniform motion blur");
  shutter.dragged_shutter={5.0,0.0,0.0,1.0,PhotoEffectEdgeMode::Extend,false};
  std::vector<ColorPixel> bright_impulse(81,{0,0,0,1}),dim_impulse(81,{0,0,0,1});
  bright_impulse[40]={1,1,1,1}; dim_impulse[40]={0.2F,0.2F,0.2F,1};
  CHECK(apply_photo_effect(&bright_impulse,9,9,shutter).ok() &&
      apply_photo_effect(&dim_impulse,9,9,shutter).ok(),"Dragged shutter highlight weighting");
  CHECK(bright_impulse[40].red < dim_impulse[40].red / 0.2F,
      "Highlight weighting must retain a larger fraction of highlight detail than dark detail");
  shutter.mask_ref="subject";
  shutter.dragged_shutter.protect_subject=true;
  const auto shutter_codec=serialize_photo_effect_stack(std::vector{shutter});
  CHECK(shutter_codec,"Dragged shutter codec admission");
  const auto shutter_decoded=deserialize_photo_effect_stack(*shutter_codec);
  CHECK(shutter_decoded && shutter_decoded->front()==shutter,
      "Dragged shutter falloff, highlight, and protection settings must roundtrip");
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
  shutter.mask_ref="selection";
  shutter.dragged_shutter.protect_subject=true;
  const std::vector<PhotoEffectMaskBinding> shutter_masks{{"selection",coverage}};
  PhotoEffectRenderOptions shutter_options; shutter_options.scale_x=0.8; shutter_options.scale_y=1.15;
  auto shutter_whole=source,shutter_tiled=source;
  CHECK(apply_photo_effect_stack(&shutter_whole,width,height,std::vector{shutter},shutter_options,shutter_masks).ok(),
      "Whole-frame protected shutter stack");
  CHECK(apply_photo_effect_stack_tiled(&shutter_tiled,width,height,std::vector{shutter},shutter_options,shutter_masks,7).ok(),
      "Padded-tile protected shutter stack");
  for(std::size_t i=0;i<shutter_whole.size();++i)
    CHECK(delta(shutter_whole[i],shutter_tiled[i])<2e-6F,
        "Dragged shutter subject protection must agree across tile boundaries");
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
  PhotoEffectInstance diffusion;
  diffusion.id="diffusion";
  diffusion.type_id="optical_diffusion";
  diffusion.diffusion={2.0,1.0,0.0,1.0,0.0};
  std::vector<ColorPixel> highlights(31*9,{0,0,0,1});
  highlights[4*31+15]={4,4,4,1};
  const auto original_highlights=highlights;
  CHECK(apply_photo_effect(&highlights,31,9,diffusion).ok(),"Optical diffusion admission");
  CHECK(highlights[4*31+15].red>original_highlights[4*31+15].red &&
      highlights[4*31+16].red>0 && highlights[4*31+16].red<highlights[4*31+15].red,
      "Optical diffusion scatters highlight energy with a soft falloff");
  CHECK(highlights[4*31+22].red==0 && highlights[4*31+8].red==0,
      "Optical diffusion leaves pixels outside three radii unchanged");
  auto protected_highlights=original_highlights;
  diffusion.diffusion.highlight_protection=1.0;
  CHECK(apply_photo_effect(&protected_highlights,31,9,diffusion).ok() &&
      protected_highlights[4*31+15].red < highlights[4*31+15].red &&
      protected_highlights[4*31+15].red >= original_highlights[4*31+15].red,
      "Full highlight protection preserves more of the sharp highlight core");
  diffusion.diffusion.highlight_protection=0.0;
  CHECK(highlights[4*31+15].alpha==1 && highlights[4*31+16].alpha==1,
      "Optical diffusion preserves opaque image alpha");
  // A faint translucent paint stroke below the extraction threshold must not
  // grow a second alpha silhouette just because it has source coverage.
  diffusion.diffusion={2.0,0.8,0.25,1.0,0.0};
  std::vector<ColorPixel> faint_afterglow(31*9,{0,0,0,0});
  constexpr std::size_t afterglow_row = 4U * 31U;
  const std::array<float,7> faint_coverage{0.04F,0.12F,0.24F,0.36F,0.24F,0.12F,0.04F};
  for (std::size_t x=0;x<faint_coverage.size();++x) {
    faint_afterglow[afterglow_row+12U+x]={0.72F,0.72F,0.72F,faint_coverage[x]};
  }
  const auto original_faint_afterglow=faint_afterglow;
  CHECK(apply_photo_effect(&faint_afterglow,31,9,diffusion).ok(),
      "Below-threshold transparent paint diffusion admission");
  for (std::size_t i=0;i<faint_afterglow.size();++i) {
    if (faint_afterglow[i].alpha!=original_faint_afterglow[i].alpha ||
        delta(faint_afterglow[i],original_faint_afterglow[i])>=1e-6F) {
      std::cerr << "Below-threshold transparent paint changed pixel " << i
          << " expected=" << original_faint_afterglow[i].red << ','
          << original_faint_afterglow[i].green << ',' << original_faint_afterglow[i].blue << ','
          << original_faint_afterglow[i].alpha << " actual=" << faint_afterglow[i].red << ','
          << faint_afterglow[i].green << ',' << faint_afterglow[i].blue << ',' << faint_afterglow[i].alpha
          << " delta=" << delta(faint_afterglow[i],original_faint_afterglow[i]) << '\n';
      return 1;
    }
  }

  // An extracted colored highlight on transparent paint retains a soft glow,
  // while its alpha follows the extracted light rather than the full stroke.
  std::vector<ColorPixel> transparent_afterglow(31*9,{0,0,0,0});
  for (std::size_t x=0;x<faint_coverage.size();++x) {
    transparent_afterglow[afterglow_row+12U+x]={4.0F,2.0F,0.5F,faint_coverage[x]};
  }
  CHECK(apply_photo_effect(&transparent_afterglow,31,9,diffusion).ok(),
      "Transparent Afterglow paint diffusion admission");
  const auto& outer_halo=transparent_afterglow[afterglow_row+11U];
  const auto& next_halo=transparent_afterglow[afterglow_row+10U];
  const auto& distant_halo=transparent_afterglow[afterglow_row+9U];
  CHECK(outer_halo.alpha>next_halo.alpha && next_halo.alpha>distant_halo.alpha &&
      distant_halo.alpha>0.0F &&
      outer_halo.alpha<transparent_afterglow[afterglow_row+15U].alpha,
      "Bright transparent paint keeps a gradually fading extracted-light halo");
  CHECK(outer_halo.red>outer_halo.green && outer_halo.green>outer_halo.blue,
      "Transparent highlight diffusion preserves the paint color instead of a white alpha fringe");
  for (const auto& pixel:transparent_afterglow) {
    CHECK(std::isfinite(pixel.red) && std::isfinite(pixel.green) && std::isfinite(pixel.blue) &&
        std::isfinite(pixel.alpha) && pixel.alpha>=0.0F && pixel.alpha<=1.0F &&
        std::isfinite(pixel.red*pixel.alpha) && std::isfinite(pixel.green*pixel.alpha) &&
        std::isfinite(pixel.blue*pixel.alpha),
        "Transparent diffusion publishes finite straight and premultiplied color samples");
  }

  // A fully opaque highlight inside a cutout must still derive its scattered
  // alpha from extracted light. Per-pixel opacity is not enough to select the
  // legacy fully-opaque image path.
  std::vector<ColorPixel> opaque_highlight_cutout(31*9,{0,0,0,0});
  opaque_highlight_cutout[afterglow_row+15U]={4.0F,2.0F,0.5F,1.0F};
  auto threshold_highlight_cutout=opaque_highlight_cutout;
  threshold_highlight_cutout[afterglow_row+15U]={1.0F,1.0F,1.0F,1.0F};
  CHECK(apply_photo_effect(&opaque_highlight_cutout,31,9,diffusion).ok() &&
      apply_photo_effect(&threshold_highlight_cutout,31,9,diffusion).ok(),
      "Opaque highlight cutout diffusion admission");
  const auto bright_cutout_halo=opaque_highlight_cutout[afterglow_row+14U].alpha;
  const auto threshold_cutout_halo=threshold_highlight_cutout[afterglow_row+14U].alpha;
  CHECK(bright_cutout_halo>0.0F && threshold_cutout_halo>0.0F &&
      threshold_cutout_halo<bright_cutout_halo*0.4F,
      "Opaque paint in a cutout scatters coverage according to extracted highlight intensity");
  CHECK(opaque_highlight_cutout[afterglow_row+8U].alpha==0.0F &&
      opaque_highlight_cutout[afterglow_row+22U].alpha==0.0F,
      "Transparent highlight cutout keeps pixels beyond diffusion support clear");

  // Zero extracted light must leave translucent RGB unchanged. The blend works
  // in premultiplied space internally, so this also catches accidental use of
  // straight RGB in the compositing numerator.
  diffusion.diffusion={2.0,0.8,0.25,1.0,0.0};
  std::vector<ColorPixel> dim_translucent{{0.4F,0.2F,0.1F,0.1F}};
  const auto original_dim_translucent=dim_translucent;
  CHECK(apply_photo_effect(&dim_translucent,1,1,diffusion).ok(),
      "Zero-highlight translucent diffusion admission");
  CHECK(dim_translucent[0].alpha==original_dim_translucent[0].alpha &&
      delta(dim_translucent[0],original_dim_translucent[0])<1e-6F,
      "Zero extracted glow preserves low-alpha straight RGB and coverage exactly");

  // A nearly opaque image can be evaluated as a mixture of fully opaque and
  // cutout tiles. Its diffusion result must not depend on that partition.
  constexpr std::size_t nearly_opaque_width = 47U;
  constexpr int nearly_opaque_height = 31;
  std::vector<ColorPixel> nearly_opaque_cutout(nearly_opaque_width *
      static_cast<std::size_t>(nearly_opaque_height),{0.12F,0.08F,0.06F,1.0F});
  const auto nearly_opaque_row = 15U * nearly_opaque_width;
  nearly_opaque_cutout[nearly_opaque_row+23U]={4.0F,2.0F,0.5F,1.0F};
  nearly_opaque_cutout[nearly_opaque_row+24U]={3.0F,1.0F,0.25F,0.65F};
  auto nearly_opaque_whole = nearly_opaque_cutout;
  auto nearly_opaque_tiled = nearly_opaque_cutout;
  const std::vector nearly_opaque_stack{diffusion};
  CHECK(apply_photo_effect_stack(&nearly_opaque_whole, static_cast<int>(nearly_opaque_width),
      nearly_opaque_height, nearly_opaque_stack).ok(),
      "Whole-frame nearly opaque cutout diffusion");
  CHECK(apply_photo_effect_stack_tiled(&nearly_opaque_tiled, static_cast<int>(nearly_opaque_width),
      nearly_opaque_height, nearly_opaque_stack, {}, {}, 7).ok(),
      "Tiled nearly opaque cutout diffusion");
  for (std::size_t i=0;i<nearly_opaque_whole.size();++i) {
    CHECK(delta(nearly_opaque_whole[i],nearly_opaque_tiled[i])<2e-6F,
        "Nearly opaque diffusion is independent of whole-frame and tile opacity classification");
  }

  diffusion.diffusion.strength=0;
  highlights=original_highlights;
  CHECK(apply_photo_effect(&highlights,31,9,diffusion).status==PhotoEffectStatus::Identity && highlights==original_highlights,
      "Zero optical diffusion strength is exact identity");
  diffusion.diffusion={2.0,1.0,0.0,1.0,0.8};
  highlights=original_highlights;
  CHECK(apply_photo_effect(&highlights,31,9,diffusion).ok() &&
      highlights[4*31+15].red>highlights[4*31+15].blue,
      "Positive warmth moves scattered highlight color warmer");
  diffusion.diffusion={2.0,1.0,0.0,1.0,0.0};
  diffusion.amount=0.5;
  std::vector<ColorPixel> tiled_source(47*31,{0.1F,0.1F,0.1F,1});
  tiled_source[15*47+23]={4,4,4,1};
  auto whole_diffusion=tiled_source, tiled_diffusion=tiled_source;
  const std::vector diffusion_stack{diffusion};
  CHECK(apply_photo_effect_stack(&whole_diffusion,47,31,diffusion_stack).ok(),"Whole optical diffusion stack");
  CHECK(apply_photo_effect_stack_tiled(&tiled_diffusion,47,31,diffusion_stack,{}, {},7).ok(),
      "Tiled optical diffusion stack");
  for(std::size_t i=0;i<whole_diffusion.size();++i)
    CHECK(delta(whole_diffusion[i],tiled_diffusion[i])<2e-6F,"Optical diffusion matches across padded tile edges");
  const auto diffusion_payload=serialize_photo_effect_stack(std::vector{diffusion});
  const auto reopened_diffusion=diffusion_payload ? deserialize_photo_effect_stack(*diffusion_payload) : std::nullopt;
  CHECK(reopened_diffusion && reopened_diffusion->front()==diffusion,
      "Optical diffusion parameters survive recipe encoding");
  const auto legacy_diffusion = deserialize_photo_effect_stack(
      "photo-editor-effects:v1 1\n\"diffusion\" \"optical_diffusion\" 1 1 1 0 \"\" \"2 1 0 1 0\"\n");
  CHECK(legacy_diffusion && legacy_diffusion->front().diffusion.highlight_protection==0.0,
      "Existing optical diffusion recipes default missing highlight protection to zero");
  auto extreme_color=std::vector<ColorPixel>{{std::numeric_limits<float>::max(),
      std::numeric_limits<float>::max(),std::numeric_limits<float>::max(),1}};
  diffusion.diffusion={1.0,0.0,0.0,1.0,1.0};
  const auto original_extreme=extreme_color;
  CHECK(apply_photo_effect(&extreme_color,1,1,diffusion).status==PhotoEffectStatus::Invalid &&
      extreme_color==original_extreme,"Overflowing HDR diffusion fails without partial publication");
  diffusion.diffusion.warmth=1.01;
  CHECK(!serialize_photo_effect_stack(std::vector{diffusion}),"Out-of-range optical diffusion settings cannot persist");
  diffusion.diffusion.warmth=0.0;
  diffusion.diffusion.highlight_protection=1.01;
  CHECK(!serialize_photo_effect_stack(std::vector{diffusion}),"Out-of-range highlight protection cannot persist");
  PhotoEffectInstance print;
  print.id="print";
  print.type_id="imperfect_print";
  print.imperfect_print={24.0,0.8,0.0,1.0,0.0,0.0,0.0,0.0,729U};
  std::vector<ColorPixel> paper_chart(37*19,{1.0F,1.0F,1.0F,1.0F});
  auto print_a=paper_chart, print_b=paper_chart;
  CHECK(apply_photo_effect(&print_a,37,19,print).ok(),"Imperfect print effect admission");
  CHECK(apply_photo_effect(&print_b,37,19,print).ok() && print_a==print_b,
      "Imperfect paper variation is exactly deterministic for a fixed seed");
  print.imperfect_print.seed=730U;
  auto print_other_seed=paper_chart;
  CHECK(apply_photo_effect(&print_other_seed,37,19,print).ok() && print_other_seed!=print_a,
      "Changing the paper seed changes the texture field");
  CHECK(print_a.front().alpha==1.0F && print_a.front().red!=1.0F,
      "Paper texture modulates the paper base without changing alpha");
  std::vector<ColorPixel> registered_edge(9*5,{0.0F,0.0F,0.0F,1.0F});
  registered_edge[2*9+4].red=1.0F;
  print.imperfect_print={32.0,0.0,0.0,1.0,1.0,0.0,0.0,0.0,1U};
  CHECK(apply_photo_effect(&registered_edge,9,5,print).ok() &&
      registered_edge[2*9+5].red>0.99F && registered_edge[2*9+4].red<0.01F,
      "Red plate registration shifts the synthetic edge by the requested source pixel");
  print.imperfect_print={12.0,0.65,0.3,1.2,-0.4,0.2,0.5,-0.3,98123U};
  std::vector<ColorPixel> print_source(53*35);
  for (int y=0;y<35;++y) for (int x=0;x<53;++x) {
    const auto i=static_cast<std::size_t>(y)*53U+static_cast<std::size_t>(x);
    print_source[i]={static_cast<float>(x)/53.0F,static_cast<float>(y)/35.0F,0.35F,1.0F};
  }
  auto whole_print=print_source,tiled_print=print_source;
  const std::vector print_stack{print};
  CHECK(apply_photo_effect_stack(&whole_print,53,35,print_stack).ok(),"Whole-frame imperfect print");
  CHECK(apply_photo_effect_stack_tiled(&tiled_print,53,35,print_stack,{}, {},7).ok(),
      "Tiled imperfect print with absolute document texture coordinates");
  for(std::size_t i=0;i<whole_print.size();++i)
    CHECK(delta(whole_print[i],tiled_print[i])<2e-6F,
        "Seeded paper and registered plates match across padded tile edges");
  const auto print_payload=serialize_photo_effect_stack(std::vector{print});
  const auto reopened_print=print_payload ? deserialize_photo_effect_stack(*print_payload) : std::nullopt;
  CHECK(reopened_print && reopened_print->front()==print,
      "Imperfect print parameters and seed survive recipe encoding");

  constexpr int graded_width = 29;
  constexpr int graded_height = 17;
  std::vector<ColorPixel> graded_source(static_cast<std::size_t>(graded_width) * graded_height);
  std::vector<float> graded_mask(graded_source.size());
  for (int y = 0; y < graded_height; ++y) {
    for (int x = 0; x < graded_width; ++x) {
      const auto i = static_cast<std::size_t>(y) * graded_width + static_cast<std::size_t>(x);
      const auto tone = static_cast<float>(x + y) / static_cast<float>(graded_width + graded_height);
      const auto alpha = i % 11U == 0U ? 0.0F : i % 3U == 0U ? 0.35F : 1.0F;
      graded_source[i] = {tone * 2.4F, tone * 0.82F + 0.025F, 0.7F - tone * 0.4F, alpha};
      graded_mask[i] = i % 5U == 0U ? 0.0F : static_cast<float>(i % 7U) / 6.0F;
    }
  }
  const auto original_graded = graded_source;
  std::vector<PhotoEffectInstance> new_operators;
  PhotoEffectInstance grain;
  grain.id = "grain";
  grain.type_id = "film_grain";
  grain.grain = {1.2, 0.035, -0.7, 731U};
  new_operators.push_back(grain);
  PhotoEffectInstance leak;
  leak.id = "leak";
  leak.type_id = "light_leak";
  leak.leak = {0.82, 0.54, -31.0, 0.42, 0.75, 0.82, 991U};
  new_operators.push_back(leak);
  PhotoEffectInstance split;
  split.id = "split";
  split.type_id = "split_tone";
  split.split_tone = {34.0, 0.28, 218.0, 0.16, -0.12};
  new_operators.push_back(split);
  PhotoEffectInstance vignette;
  vignette.id = "vignette";
  vignette.type_id = "vignette";
  vignette.vignette = {0.54, 0.25, 0.48, 0.82};
  new_operators.push_back(vignette);
  PhotoEffectInstance duotone;
  duotone.id = "duotone";
  duotone.type_id = "duotone";
  duotone.duotone = {252.0, 41.0, 0.74, 0.1};
  new_operators.push_back(duotone);
  PhotoEffectInstance fade;
  fade.id = "fade";
  fade.type_id = "soft_fade";
  fade.soft_fade = {0.11, 0.66, 0.72};
  new_operators.push_back(fade);

  PhotoEffectRenderOptions procedural_options;
  procedural_options.scale_x = 0.5;
  procedural_options.scale_y = 0.75;
  procedural_options.origin_x = 17;
  procedural_options.origin_y = 9;
  procedural_options.frame_width = 79;
  procedural_options.frame_height = 53;
  PhotoEffectRenderOptions underscaled_options;
  underscaled_options.scale_x = 1.0 / 8192.0;
  underscaled_options.scale_y = 1.0 / 8192.0;
  underscaled_options.origin_x = 0;
  underscaled_options.origin_y = 0;
  const std::vector<ColorPixel> unsafe_scale_source{{0.4F, 0.6F, 0.2F, 0.7F}};
  for (const auto* bounded_effect : {&grain, &print}) {
    auto preserved = unsafe_scale_source;
    CHECK(apply_photo_effect(&preserved, 1, 1, *bounded_effect, underscaled_options).status ==
        PhotoEffectStatus::Invalid && preserved == unsafe_scale_source,
        "Procedural coordinate scales below the safe document-space minimum reject transactionally");
  }
  const std::vector<PhotoEffectMaskBinding> graded_masks{{"grade-mask", graded_mask}};
  for (auto& effect : new_operators) {
    effect.amount = 0.73;
    effect.mask_ref = "grade-mask";
    auto whole = original_graded;
    auto tiled = original_graded;
    const std::vector stack{effect};
    CHECK(apply_photo_effect_stack(&whole, graded_width, graded_height, stack,
        procedural_options, graded_masks).ok(), "New photographic operator renders on a masked HDR raster");
    CHECK(apply_photo_effect_stack_tiled(&tiled, graded_width, graded_height, stack,
        procedural_options, graded_masks, 6).ok(), "New photographic operator renders through bounded tiles");
    bool changed = false;
    for (std::size_t i = 0; i < whole.size(); ++i) {
      CHECK(delta(whole[i], tiled[i]) < 1e-6F,
          "Document-space and frame-space effects are exactly stable across tile boundaries");
      CHECK(whole[i].alpha == original_graded[i].alpha,
          "Per-pixel photographic grading preserves translucent and cutout alpha");
      CHECK(std::isfinite(whole[i].red) && std::isfinite(whole[i].green) &&
          std::isfinite(whole[i].blue) && std::isfinite(whole[i].alpha),
          "Per-pixel photographic grading keeps HDR output finite");
      if (graded_mask[i] == 0.0F)
        CHECK(whole[i] == original_graded[i], "A zero effect-mask sample preserves its source pixel exactly");
      if (delta(whole[i], original_graded[i]) > 1e-6F) changed = true;
    }
    CHECK(changed, "Each added operator produces an actual rendered image change");
    auto repeated = original_graded;
    CHECK(apply_photo_effect_stack(&repeated, graded_width, graded_height, stack,
        procedural_options, graded_masks).ok() && repeated == whole,
        "Seeded or analytical operator output is deterministic across repeated renders");
  }
  grain.grain.seed += 1U;
  auto changed_seed = original_graded;
  CHECK(apply_photo_effect(&changed_seed, graded_width, graded_height, grain,
      procedural_options).ok() && changed_seed != original_graded,
      "Changing film-grain seed changes its fixed document-space texture");
  return 0;
}
