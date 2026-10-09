#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/color_fill_layer_program.h"
#include "photo_editor/pipeline/color_transform.h"
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
#include "photo_editor/pipeline/metal/metal_pipeline_backend.h"
#endif
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <string_view>
#include <utility>

using namespace photo_editor::pipeline;
namespace {
ColorPixel reference_over(ColorPixel base, ColorPixel source, double opacity, bool multiply) {
  const double sa = source.alpha * opacity, ba = base.alpha;
  if (sa == 0) return base;
  const auto alpha = sa + ba * (1 - sa);
  const auto channel = [&](double b, double s) {
    const double blend = multiply ? b * s : s;
    return static_cast<float>((sa * (1 - ba) * s + sa * ba * blend + (1 - sa) * ba * b) / alpha);
  };
  return {channel(base.red, source.red), channel(base.green, source.green),
      channel(base.blue, source.blue), static_cast<float>(alpha)};
}
double difference(ColorPixel a, ColorPixel b) {
  return std::max({std::abs(a.red - b.red), std::abs(a.green - b.green),
      std::abs(a.blue - b.blue), std::abs(a.alpha - b.alpha)});
}
ColorPixel transform_pixel(ColorPixel pixel, const ColorMatrix3& matrix) {
  const auto rgb = multiply_matrix3_vector(matrix,
      {static_cast<double>(pixel.red), static_cast<double>(pixel.green), static_cast<double>(pixel.blue)});
  return {static_cast<float>(rgb[0]), static_cast<float>(rgb[1]), static_cast<float>(rgb[2]), pixel.alpha};
}
}
int main(int argc, char** argv) {
  const bool native = argc == 2 && std::string_view(argv[1]) == "--metal";
  if (argc > 1 && !native) return 1;
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
  std::unique_ptr<metal::MetalPipelineBackend> backend;
  if (native) {
    backend = std::make_unique<metal::MetalPipelineBackend>();
    if (!backend->available()) { std::cerr << backend->unavailable_reason(); return 1; }
  }
#else
  if (native) return 1;
#endif
  const auto check_gpu = [&](const ColorAdjustmentState& state, ColorPixel base, ColorPixel expected) {
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
    if (backend) {
      const auto descriptor = RenderSurfaceDescriptor{.pixel_encoding = RenderPixelEncoding::SceneLinearFloat,
          .color_domain = RenderColorDomain::WorkingScene, .range = RenderRangeModel::SceneExtended,
          .alpha = RenderAlphaModel::Straight, .color_space_identity = state.color_pipeline.working_profile.color_space.name,
          .transfer_identity = "linear", .width = 1, .height = 1};
      const std::vector<ColorPixel> input{base};
      ColorAdjustmentTelemetry telemetry;
      const auto surface = backend->publish_masks_and_color_lut_surface(input, 1, 1, state, {}, descriptor,
          &telemetry, {}, 1.0, false);
      std::vector<ColorPixel> actual;
      if (!surface || !surface->copy_pixels(&actual) || actual.size() != 1 || difference(actual[0], expected) > 2e-6) {
        std::cerr << "Native alpha composite mismatch: " << telemetry.gpu_fallback_reason << '\n'; return false;
      }
    }
#else
    (void)state; (void)base; (void)expected;
#endif
    return true;
  };
  bool ok = true;
  int cases = 0, failures = 0;
  double worst = 0;
  for (const auto* profile : {"Linear sRGB", "Linear Display P3", "Linear Rec.2020", "Linear ProPhoto RGB"}) {
    for (float coverage : {0.0F, 0.25F, 1.0F}) for (float base_alpha : {0.0F, 0.25F, 1.0F})
        for (bool clipped_top : {false, true}) {
      ColorAdjustmentState state;
      state.color_pipeline.working_profile = working_color_profile_from_name(profile);
      PhotoLayer erase;
      erase.id = "base-erasure"; erase.kind = "pixel"; erase.erase_base_image = true;
      erase.pixel_width = erase.pixel_height = 1;
      erase.pixels = {{1.0F, 1.0F, 1.0F, coverage}};
      const ColorPixel base{0.15F, 0.35F, 0.65F, base_alpha};
      auto expected = base;
      expected.alpha *= 1.0F - coverage;
      if (coverage > 1e-6F && expected.alpha <= 1e-6F) expected = {0.0F, 0.0F, 0.0F, 0.0F};
      state.photo_layers = {erase};
      if (clipped_top) {
        PhotoLayer top; top.id = "clipped-top"; top.kind = "fill"; top.clipping_mask = true;
        top.fill_color = {0.8F, 0.4F, 0.2F, 0.5F};
        state.photo_layers.push_back(top);
        expected = reference_over(expected, top.fill_color, expected.alpha, false);
      }
      auto pixels = std::vector<ColorPixel>{base}, retained = pixels;
      const auto program = compile_fill_layer_program(state, 1, 1);
      if (!apply_color_layers_stage(&pixels, 1, 1, state) || !program.ready() ||
          !apply_compiled_fill_layer_program(&retained, program) || difference(pixels[0], expected) > 2e-6 ||
          difference(retained[0], expected) > 2e-6 || !check_gpu(state, base, expected)) {
        std::cerr << "Base erasure alpha mismatch coverage=" << coverage << " base=" << base_alpha
                  << " clipped=" << clipped_top << '\n';
        return 1;
      }
    }
    // No hidden backdrop RGB may contaminate any blend mode when alpha is zero.
    for (const auto* mode : {"normal", "multiply", "screen", "difference", "lighten", "darken", "overlay",
        "soft-light", "hard-light", "linear-light", "hue", "saturation", "color", "luminosity"}) {
      ColorAdjustmentState state;
      state.color_pipeline.working_profile = working_color_profile_from_name(profile);
      PhotoLayer layer; layer.kind = "fill"; layer.id = "transparent-backdrop";
      layer.fill_color = {0.8F, 0.4F, 0.2F, 0.5F}; layer.opacity = 0.5; layer.blend_mode = mode;
      state.photo_layers = {layer};
      const ColorPixel base{4.0F, -1.0F, 7.0F, 0}, expected{0.8F, 0.4F, 0.2F, 0.25F};
      auto pixels = std::vector<ColorPixel>{base}, retained = pixels;
      const auto program = compile_fill_layer_program(state, 1, 1);
      if (!apply_color_layers_stage(&pixels, 1, 1, state) || !program.ready() ||
          !apply_compiled_fill_layer_program(&retained, program) || difference(pixels[0], expected) > 2e-6 ||
          difference(retained[0], expected) > 2e-6 || !check_gpu(state, base, expected)) return 1;
    }
    // Alpha composition must not clamp scene-linear negative or HDR channels.
    for (float ba : {0.0F, 0.25F, 1.0F}) {
      ColorAdjustmentState state;
      state.color_pipeline.working_profile = working_color_profile_from_name(profile);
      PhotoLayer layer; layer.kind = "fill"; layer.id = "hdr-alpha"; layer.fill_color = {-0.25F, 2.0F, 4.0F, 0.5F};
      state.photo_layers = {layer};
      const ColorPixel base{0.5F, 0.25F, 0.125F, ba};
      const auto expected = reference_over(base, layer.fill_color, 1, false);
      auto pixels = std::vector<ColorPixel>{base}, retained = pixels;
      const auto program = compile_fill_layer_program(state, 1, 1);
      if (!apply_color_layers_stage(&pixels, 1, 1, state) || !program.ready() ||
          !apply_compiled_fill_layer_program(&retained, program) || difference(pixels[0], expected) > 2e-6 ||
          difference(retained[0], expected) > 2e-6 || !check_gpu(state, base, expected)) return 1;
    }
    for (const auto* kind : {"fill", "pixel"}) for (bool multiply : {false, true})
      for (float ba : {0.0F, 0.25F, 0.75F, 1.0F}) for (float sa : {0.0F, 0.25F, 0.5F, 1.0F})
        for (double opacity : {0.0, 0.25, 0.5, 1.0}) for (int mask : {-1, 0, 128, 255}) {
      ColorAdjustmentState state;
      state.color_pipeline.working_profile = working_color_profile_from_name(profile);
      PhotoLayer layer; layer.kind = kind; layer.id = "alpha"; layer.opacity = opacity;
      layer.blend_mode = multiply ? "multiply" : "normal";
      const ColorPixel base{0.15F, 0.35F, 0.65F, ba}, source{0.8F, 0.4F, 0.2F, sa};
      layer.fill_color = source; layer.pixel_width = layer.pixel_height = 1; layer.pixels = {source};
      double mask_weight = 1;
      if (mask >= 0) {
        layer.mask_width = layer.mask_height = 1; layer.mask_data = {static_cast<std::uint8_t>(mask)};
        layer.mask_density = 0.8; layer.mask_opacity = 0.75;
        mask_weight = std::min(mask / 255.0, 0.8) * 0.75;
      }
      state.photo_layers = {layer};
      const auto expected = reference_over(base, source, opacity * mask_weight, multiply);
      auto legacy = std::vector<ColorPixel>{base}, retained = legacy;
      const auto program = compile_fill_layer_program(state, 1, 1);
      if (!apply_color_layers_stage(&legacy, 1, 1, state) || !program.ready() ||
          !apply_compiled_fill_layer_program(&retained, program)) return 1;
      const auto delta = std::max(difference(legacy[0], expected), difference(retained[0], expected));
      worst = std::max(worst, delta); ++cases;
      if (delta > 2e-6) { ++failures; ok = false; }
      if (!check_gpu(state, base, expected)) return 1;
    }
    for (const auto* mode : {"normal", "multiply", "screen", "overlay", "color"}) {
      ColorAdjustmentState state;
      state.color_pipeline.working_profile = working_color_profile_from_name(profile);
      PhotoLayer adjustment; adjustment.kind = "adjustment"; adjustment.id = "adjustment";
      adjustment.opacity = 0.5; adjustment.exposure = 1; adjustment.blend_mode = mode;
      state.photo_layers = {adjustment};
      auto pixels = std::vector<ColorPixel>{{0.15F, 0.35F, 0.65F, 0.25F}};
      auto retained = pixels;
      const auto program = compile_fill_layer_program(state, 1, 1);
      if (!apply_color_layers_stage(&pixels, 1, 1, state) || !program.ready() ||
          !apply_compiled_fill_layer_program(&retained, program)) return 1;
      if (pixels[0].alpha != 0.25F || retained[0].alpha != 0.25F) {
        std::cerr << profile << '/' << mode << " adjustment altered alpha: " << pixels[0].alpha << '/' << retained[0].alpha << '\n';
        ok = false;
      }
      if (!check_gpu(state, {0.15F, 0.35F, 0.65F, 0.25F}, retained[0])) return 1;
    }
  }
  {
    ColorAdjustmentState glass_state;
    PhotoLayer glass;
    glass.id = "glass-text"; glass.kind = "text"; glass.x = 1; glass.y = 1;
    glass.pixel_width = 3; glass.pixel_height = 3;
    glass.pixels.assign(9, ColorPixel{0.9F, 0.9F, 0.9F, 0.0F});
    glass.pixels[4] = {0.9F, 0.9F, 0.9F, 1.0F};
    glass.pixels[1] = {0.9F, 0.9F, 0.9F, 0.55F};
    glass.text_material = "frosted_glass";
    glass.text_material_tint = {0.3F, 0.5F, 0.7F, 1.0F};
    glass.text_material_blur = 1.0;
    glass.text_material_transmission = 0.9;
    ColorAdjustmentState accelerated_state;
    accelerated_state.photo_layers = {glass};
    if (compile_fill_layer_program(accelerated_state, 5, 5).ready()) {
      std::cerr << "Native flattened layer kernels must reject material text until they implement it\n";
      return 1;
    }
    PhotoLayer top;
    top.id = "top-pixel"; top.kind = "pixel"; top.pixel_width = 5; top.pixel_height = 5;
    top.pixels.assign(25, ColorPixel{0.95F, 0.1F, 0.2F, 1.0F});
    std::vector<ColorPixel> lower_a, lower_b;
    for (int i = 0; i < 25; ++i) {
      const auto value = static_cast<float>(i) / 24.0F;
      const auto curved = value * value;
      lower_a.push_back({curved, 0.2F + curved * 0.2F, 0.8F - curved * 0.5F, 1.0F});
      lower_b.push_back({1.0F - curved, 0.8F, 0.1F + curved * 0.7F, 1.0F});
    }
    glass_state.photo_layers = {glass};
    auto glass_pixels_a = lower_a, glass_pixels_b = lower_b;
    if (!apply_color_layers_stage(&glass_pixels_a, 5, 5, glass_state) ||
        !apply_color_layers_stage(&glass_pixels_b, 5, 5, glass_state)) return 1;
    glass_state.photo_layers = {glass, top};
    auto stacked_a = lower_a, stacked_b = lower_b;
    if (!apply_color_layers_stage(&stacked_a, 5, 5, glass_state) ||
        !apply_color_layers_stage(&stacked_b, 5, 5, glass_state)) return 1;
    const auto lower_stack_sampled = difference(glass_pixels_a[12], glass_pixels_b[12]) > 0.05;
    const auto no_glyph_change_outside = difference(glass_pixels_a[0], lower_a[0]) < 1e-6;
    const auto upper_layer_remains_on_top = difference(stacked_a[12], top.pixels[12]) < 1e-6;
    if (!lower_stack_sampled || !no_glyph_change_outside || !upper_layer_remains_on_top) {
      std::cerr << "Frosted text must sample the lower stack, remain bounded to glyphs, and preserve upper order\n";
      return 1;
    }

    std::array<std::vector<ColorPixel>, 3> material_results;
    const std::array<std::string_view, 3> materials{"frosted_glass", "smoked_plastic", "soft_emboss"};
    for (std::size_t index = 0; index < materials.size(); ++index) {
      ColorAdjustmentState material_state;
      auto material_layer = glass;
      material_layer.text_material = materials[index];
      material_layer.text_material_blur = index == 0 ? 2.0 : 0.0;
      material_layer.text_material_depth = 8.0;
      material_state.photo_layers = {material_layer};
      material_results[index] = lower_a;
      if (!apply_color_layers_stage(&material_results[index], 5, 5, material_state)) return 1;
      if (!std::all_of(material_results[index].begin(), material_results[index].end(), [](const auto& pixel) {
            return std::isfinite(pixel.red) && std::isfinite(pixel.green) && std::isfinite(pixel.blue) &&
                std::isfinite(pixel.alpha);
          })) return 1;
    }
    if (difference(material_results[0][12], material_results[1][12]) < 1e-4 ||
        difference(material_results[1][12], material_results[2][12]) < 1e-4 ||
        difference(material_results[0][12], material_results[2][12]) < 1e-4) {
      std::cerr << "Frosted glass, smoked plastic, and soft emboss must render distinct material responses: "
          << material_results[0][12].red << ',' << material_results[0][12].green << ',' << material_results[0][12].blue << " / "
          << material_results[1][12].red << ',' << material_results[1][12].green << ',' << material_results[1][12].blue << " / "
          << material_results[2][12].red << ',' << material_results[2][12].green << ',' << material_results[2][12].blue << '\n';
      return 1;
    }
    auto sharp_glass = glass;
    sharp_glass.text_material_blur = 0;
    ColorAdjustmentState sharp_glass_state;
    sharp_glass_state.photo_layers = {sharp_glass};
    auto sharp_glass_result = lower_a;
    if (!apply_color_layers_stage(&sharp_glass_result, 5, 5, sharp_glass_state) ||
        difference(sharp_glass_result[12], material_results[0][12]) < 1e-4) {
      std::cerr << "Frosted-glass blur must change the sampled lower-stack result\n";
      return 1;
    }
    auto emboss_low = glass;
    emboss_low.text_material = "soft_emboss";
    emboss_low.text_material_depth = 2;
    emboss_low.text_material_light_angle = -90;
    auto emboss_high = emboss_low;
    emboss_high.text_material_depth = 14;
    emboss_high.text_material_light_angle = 90;
    ColorAdjustmentState emboss_low_state, emboss_high_state;
    emboss_low_state.photo_layers = {emboss_low};
    emboss_high_state.photo_layers = {emboss_high};
    auto emboss_low_result = lower_a, emboss_high_result = lower_a;
    if (!apply_color_layers_stage(&emboss_low_result, 5, 5, emboss_low_state) ||
        !apply_color_layers_stage(&emboss_high_result, 5, 5, emboss_high_state) ||
        difference(emboss_low_result[7], emboss_high_result[7]) < 1e-4) {
      std::cerr << "Soft-emboss depth and light angle must alter glyph-edge relief\n";
      return 1;
    }

    const std::array<std::string_view, 10> metal_materials{
        "polished_chrome", "brushed_silver", "warm_gold", "copper", "dark_steel",
        "champagne_gold", "rose_gold", "antique_brass", "titanium", "black_chrome"};
    PhotoLayer metal;
    metal.id = "metal-title";
    metal.kind = "text";
    metal.x = 2;
    metal.y = 2;
    metal.pixel_width = 9;
    metal.pixel_height = 5;
    metal.text_material_tint = {0.62F, 0.59F, 0.53F, 1.0F};
    metal.pixels.assign(static_cast<std::size_t>(metal.pixel_width * metal.pixel_height),
        ColorPixel{0.7F, 0.7F, 0.7F, 0.0F});
    for (int y = 1; y <= 3; ++y) {
      for (int x = 1; x <= 7; ++x) {
        const auto edge = x == 1 || x == 7 || y == 1 || y == 3;
        metal.pixels[static_cast<std::size_t>(y * metal.pixel_width + x)] =
            {0.7F, 0.7F, 0.7F, edge ? 0.55F : 1.0F};
      }
    }
    std::array<std::vector<ColorPixel>, 10> metal_results;
    for (std::size_t material_index = 0; material_index < metal_materials.size(); ++material_index) {
      auto metal_layer = metal;
      metal_layer.text_material = std::string(metal_materials[material_index]);
      metal_layer.text_material_transmission = 0.84;
      metal_layer.text_material_edge_softness = 1.2;
      metal_layer.text_material_depth = 8.0;
      ColorAdjustmentState metal_state;
      metal_state.photo_layers = {metal_layer};
      auto metal_pixels = std::vector<ColorPixel>(117, ColorPixel{0.045F, 0.05F, 0.06F, 1.0F});
      if (compile_fill_layer_program(metal_state, 13, 9).ready() ||
          !apply_color_layers_stage(&metal_pixels, 13, 9, metal_state)) {
        std::cerr << "Metal text must stay on the material-aware compositor: "
            << metal_materials[material_index] << '\n';
        return 1;
      }
      if (!std::all_of(metal_pixels.begin(), metal_pixels.end(), [](const auto& pixel) {
            return std::isfinite(pixel.red) && std::isfinite(pixel.green) &&
                std::isfinite(pixel.blue) && std::isfinite(pixel.alpha);
          })) return 1;
      metal_results[material_index] = std::move(metal_pixels);
    }
    const auto interior_middle = static_cast<std::size_t>(4 * 13 + 6);
    const auto interior_right = static_cast<std::size_t>(4 * 13 + 8);
    const auto reflection_varies = difference(metal_results[0][interior_middle],
        metal_results[0][interior_right]) > 1e-3;
    for (std::size_t first = 0; first < metal_results.size(); ++first) {
      for (std::size_t second = first + 1; second < metal_results.size(); ++second) {
        const bool distinct = std::equal(metal_results[first].cbegin(), metal_results[first].cend(),
            metal_results[second].cbegin(), [](const auto& a, const auto& b) {
              return difference(a, b) < 1e-5;
            });
        if (distinct) {
          std::cerr << "Every metal finish should have a distinct reflection response: "
              << metal_materials[first] << " / " << metal_materials[second] << '\n';
          return 1;
        }
      }
    }
    // Check the renderer's front-face metal hue against published conductor
    // colors. Filament Table 5 lists these samples in sRGB; their expected
    // linear F0 values below are the IEC 61966-2-1 decoded triples. Chromium's
    // linear value is from Hoffman's SIGGRAPH 2014 table.
    struct PublishedMetalF0 final {
      std::string_view material;
      std::array<double, 3> linear_f0;
    };
    const std::array<PublishedMetalF0, 6> published_metal_f0s{{
        {"polished_chrome", {0.549, 0.556, 0.554}},
        {"brushed_silver", {0.933107, 0.911408, 0.807346}},
        {"warm_gold", {1.0, 0.692071, 0.284647}},
        {"copper", {0.933107, 0.507079, 0.342392}},
        {"dark_steel", {0.554222, 0.570482, 0.570482}},
        {"titanium", {0.538236, 0.491905, 0.433880}},
    }};
    const auto reference_sample = static_cast<std::size_t>(4 * 13 + 6);
    for (const auto& published : published_metal_f0s) {
      auto reference_layer = metal;
      reference_layer.text_material = std::string(published.material);
      reference_layer.text_material_tint = {1.0F, 1.0F, 1.0F, 1.0F};
      reference_layer.text_material_transmission = 1.0;
      ColorAdjustmentState reference_state;
      reference_state.color_pipeline.working_profile = working_color_profile_from_name("Linear sRGB");
      reference_state.photo_layers = {reference_layer};
      std::vector<ColorPixel> reference_pixels(117, ColorPixel{0.045F, 0.05F, 0.06F, 1.0F});
      if (!apply_color_layers_stage(&reference_pixels, 13, 9, reference_state)) return 1;
      const auto& front_face = reference_pixels[reference_sample];
      if (front_face.green <= 1e-6F || front_face.red <= 0.0F || front_face.blue <= 0.0F) return 1;
      const auto expected_red_green = published.linear_f0[0] / published.linear_f0[1];
      const auto expected_blue_green = published.linear_f0[2] / published.linear_f0[1];
      const auto actual_red_green = static_cast<double>(front_face.red / front_face.green);
      const auto actual_blue_green = static_cast<double>(front_face.blue / front_face.green);
      const auto relative_error = [](double actual, double expected) {
        return std::abs(actual - expected) / expected;
      };
      if (relative_error(actual_red_green, expected_red_green) > 0.12 ||
          relative_error(actual_blue_green, expected_blue_green) > 0.12) {
        std::cerr << "Metal text front-face hue should follow its published conductor F0: "
            << published.material << " expected ratios=" << expected_red_green << ','
            << expected_blue_green << " actual ratios=" << actual_red_green << ','
            << actual_blue_green << '\n';
        return 1;
      }
    }
    auto dark_chrome = metal;
    dark_chrome.text_material = "polished_chrome";
    dark_chrome.text_material_edge_softness = 4.0;
    dark_chrome.text_material_depth = 0.0;
    dark_chrome.text_material_light_angle = -90.0;
    ColorAdjustmentState dark_chrome_state;
    dark_chrome_state.photo_layers = {dark_chrome};
    auto light_chrome = dark_chrome;
    light_chrome.text_material_depth = 32.0;
    light_chrome.text_material_light_angle = 90.0;
    ColorAdjustmentState light_chrome_state;
    light_chrome_state.photo_layers = {light_chrome};
    auto dark_chrome_pixels = std::vector<ColorPixel>(117, ColorPixel{0.045F, 0.05F, 0.06F, 1.0F});
    auto light_chrome_pixels = dark_chrome_pixels;
    if (!apply_color_layers_stage(&dark_chrome_pixels, 13, 9, dark_chrome_state) ||
        !apply_color_layers_stage(&light_chrome_pixels, 13, 9, light_chrome_state) ||
        !reflection_varies || difference(dark_chrome_pixels[static_cast<std::size_t>(3 * 13 + 6)],
            light_chrome_pixels[static_cast<std::size_t>(3 * 13 + 6)]) < 1e-3 ||
        std::abs(dark_chrome_pixels[static_cast<std::size_t>(3 * 13 + 6)].alpha -
            light_chrome_pixels[static_cast<std::size_t>(3 * 13 + 6)].alpha) > 1e-6) {
      std::cerr << "Metal text must show directional, spatially varying reflections and editable relief without changing glyph alpha\n";
      return 1;
    }

    PhotoLayer fresnel_glass;
    fresnel_glass.id = "fresnel-glass";
    fresnel_glass.kind = "text";
    fresnel_glass.x = 1;
    fresnel_glass.y = 1;
    fresnel_glass.pixel_width = 5;
    fresnel_glass.pixel_height = 3;
    fresnel_glass.pixels.assign(15, ColorPixel{1.0F, 1.0F, 1.0F, 0.0F});
    for (int x = 1; x <= 3; ++x)
      fresnel_glass.pixels[static_cast<std::size_t>(fresnel_glass.pixel_width + x)].alpha = 1.0F;
    fresnel_glass.text_material = "frosted_glass";
    fresnel_glass.text_material_tint = {0.0F, 0.0F, 0.0F, 1.0F};
    fresnel_glass.text_material_blur = 0.0;
    fresnel_glass.text_material_transmission = 0.0;
    fresnel_glass.text_material_depth = 8.0;
    ColorAdjustmentState fresnel_state;
    fresnel_state.photo_layers = {fresnel_glass};
    auto fresnel_pixels = std::vector<ColorPixel>(35, ColorPixel{0.0F, 0.0F, 0.0F, 0.0F});
    if (!apply_color_layers_stage(&fresnel_pixels, 7, 5, fresnel_state)) return 1;
    const auto glass_edge = fresnel_pixels[static_cast<std::size_t>(2 * 7 + 2)];
    const auto glass_face = fresnel_pixels[static_cast<std::size_t>(2 * 7 + 3)];
    if (glass_edge.alpha != 1.0F || glass_face.alpha != 1.0F ||
        glass_edge.red <= glass_face.red + 0.01F) {
      std::cerr << "Dielectric Fresnel reflection should increase at the beveled grazing-angle glyph edge without changing alpha\n";
      return 1;
    }

    auto refracting_glass = fresnel_glass;
    refracting_glass.text_material_transmission = 1.0;
    refracting_glass.text_material_depth = 8.0;
    auto flat_glass = refracting_glass;
    flat_glass.text_material_depth = 0.0;
    ColorAdjustmentState refracting_state, flat_state;
    refracting_state.photo_layers = {refracting_glass};
    flat_state.photo_layers = {flat_glass};
    std::vector<ColorPixel> gradient_backdrop;
    for (int y = 0; y < 5; ++y)
      for (int x = 0; x < 7; ++x)
        gradient_backdrop.push_back({static_cast<float>(x) / 6.0F, 0.2F, 0.1F, 1.0F});
    auto refracted_pixels = gradient_backdrop;
    auto flat_pixels = gradient_backdrop;
    if (!apply_color_layers_stage(&refracted_pixels, 7, 5, refracting_state) ||
        !apply_color_layers_stage(&flat_pixels, 7, 5, flat_state)) return 1;
    const auto refraction_sample = static_cast<std::size_t>(2 * 7 + 2);
    if (std::abs(refracted_pixels[refraction_sample].red - flat_pixels[refraction_sample].red) < 0.1F ||
        std::abs(refracted_pixels[refraction_sample].alpha - flat_pixels[refraction_sample].alpha) > 1e-6F) {
      std::cerr << "Glass depth should refract the lower-stack edge sample while preserving the glyph alpha silhouette\n";
      return 1;
    }

    // Metal F0 profiles are authored in canonical linear sRGB. The same text
    // tint and backdrop should therefore render the same when converted back
    // from any supported working space, including the default ProPhoto space.
    const ColorPixel canonical_material_tint{0.62F, 0.59F, 0.53F, 1.0F};
    const ColorPixel canonical_material_backdrop{0.045F, 0.05F, 0.06F, 1.0F};
    ColorPixel reference_gold_pixel{};
    bool has_reference_gold_pixel = false;
    for (const auto* profile_name : {
            "Linear sRGB", "Linear Display P3", "Linear Rec.2020", "Linear ProPhoto RGB"}) {
      ColorAdjustmentState metal_state;
      metal_state.color_pipeline.working_profile = working_color_profile_from_name(profile_name);
      const auto working_space = metal_state.color_pipeline.working_profile.color_space;
      const auto linear_srgb_to_working = compile_builtin_color_transform(
          make_linear_srgb_color_space(), working_space).matrix;
      const auto working_to_linear_srgb = compile_builtin_color_transform(
          working_space, make_linear_srgb_color_space()).matrix;
      auto metal_layer = metal;
      metal_layer.text_material = "warm_gold";
      metal_layer.text_material_tint = transform_pixel(canonical_material_tint, linear_srgb_to_working);
      for (auto& pixel : metal_layer.pixels) {
        const auto alpha = pixel.alpha;
        pixel = transform_pixel({0.7F, 0.7F, 0.7F, alpha}, linear_srgb_to_working);
      }
      metal_state.photo_layers = {metal_layer};
      std::vector<ColorPixel> pixels(117, transform_pixel(
          canonical_material_backdrop, linear_srgb_to_working));
      if (!apply_color_layers_stage(&pixels, 13, 9, metal_state)) return 1;
      const auto reference_pixel = transform_pixel(
          pixels[static_cast<std::size_t>(4 * 13 + 6)], working_to_linear_srgb);
      if (!std::isfinite(reference_pixel.red) || !std::isfinite(reference_pixel.green) ||
          !std::isfinite(reference_pixel.blue) ||
          (has_reference_gold_pixel && difference(reference_pixel, reference_gold_pixel) > 5e-5)) {
        std::cerr << "Metal text color should be stable across supported working spaces; profile="
            << profile_name << " reference=" << reference_pixel.red << ',' << reference_pixel.green
            << ',' << reference_pixel.blue << " baseline=" << reference_gold_pixel.red << ','
            << reference_gold_pixel.green << ',' << reference_gold_pixel.blue << '\n';
        return 1;
      }
      reference_gold_pixel = reference_pixel;
      has_reference_gold_pixel = true;
    }

    auto small_material = metal;
    small_material.text_material = "brushed_silver";
    small_material.text_material_transmission = 0.84;
    small_material.text_material_edge_softness = 1.0;
    small_material.text_material_depth = 4.0;
    PhotoLayer large_material = small_material;
    large_material.x *= 2;
    large_material.y *= 2;
    large_material.pixel_width *= 2;
    large_material.pixel_height *= 2;
    large_material.text_material_edge_softness *= 2.0;
    large_material.text_material_depth *= 2.0;
    large_material.pixels.assign(static_cast<std::size_t>(large_material.pixel_width * large_material.pixel_height),
        ColorPixel{0.7F, 0.7F, 0.7F, 0.0F});
    for (int y = 0; y < small_material.pixel_height; ++y) {
      for (int x = 0; x < small_material.pixel_width; ++x) {
        const auto pixel = small_material.pixels[static_cast<std::size_t>(y * small_material.pixel_width + x)];
        for (int dy = 0; dy < 2; ++dy)
          for (int dx = 0; dx < 2; ++dx)
            large_material.pixels[static_cast<std::size_t>((y * 2 + dy) * large_material.pixel_width + x * 2 + dx)] = pixel;
      }
    }
    ColorAdjustmentState small_material_state, large_material_state;
    small_material_state.photo_layers = {small_material};
    large_material_state.photo_layers = {large_material};
    std::vector<ColorPixel> small_material_pixels(13U * 9U, ColorPixel{0.0F, 0.0F, 0.0F, 0.0F});
    std::vector<ColorPixel> large_material_pixels(26U * 18U, ColorPixel{0.0F, 0.0F, 0.0F, 0.0F});
    if (!apply_color_layers_stage(&small_material_pixels, 13, 9, small_material_state) ||
        !apply_color_layers_stage(&large_material_pixels, 26, 18, large_material_state)) return 1;
    const auto small_edge = small_material_pixels[static_cast<std::size_t>(3 * 13 + 3)];
    const auto large_edge = large_material_pixels[static_cast<std::size_t>(6 * 26 + 6)];
    if (difference(small_edge, large_edge) > 0.01 ||
        std::abs(small_edge.alpha - small_material.pixels[static_cast<std::size_t>(1 * 9 + 1)].alpha) > 1e-6F ||
        std::abs(large_edge.alpha - small_edge.alpha) > 1e-6F) {
      std::cerr << "Metal reflection and glyph coverage should remain stable when text material raster resolution doubles\n";
      return 1;
    }
  }
  std::cout << "Independent straight-alpha layer cases=" << cases << " failures=" << failures << " worst=" << worst
            << " native=" << native << '\n';
  return ok ? 0 : 1;
}
