#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "photo_editor/pipeline/photo_effect_types.h"

namespace photo_editor::desktop {

// Parameters follow the registered v1 field order in
// materialize_photo_effect_look_stage(). Every stage is a real, editable stack entry.
struct PhotoEffectLookStage final {
  std::string_view type;
  std::array<double, 9> values{};
};

struct PhotoEffectLook final {
  std::string_view id;
  std::string_view name;
  std::string_view category;
  std::string_view description;
  std::array<PhotoEffectLookStage, 3> stages{};
  unsigned int stage_count = 0;
};

inline constexpr PhotoEffectLookStage effect_look_stage(
    std::string_view type, double a = 0, double b = 0, double c = 0,
    double d = 0, double e = 0, double f = 0, double g = 0,
    double h = 0, double i = 0) {
  return {type, {a, b, c, d, e, f, g, h, i}};
}

inline constexpr std::array photo_effect_looks{
    PhotoEffectLook{"analog_fade", "Analog Fade", "Film", "Soft lifted print tones with a gentle highlight veil.",
      {effect_look_stage("imperfect_print", 36, .10, .01, .88), effect_look_stage("optical_diffusion", 18, .88, .22, .24, .08, .20)}, 2},
    PhotoEffectLook{"warm_35mm", "Warm 35mm", "Film", "Warm highlight bloom with subtle film plate texture.",
      {effect_look_stage("imperfect_print", 42, .08, .005, .94), effect_look_stage("optical_diffusion", 22, .82, .25, .30, .32, .12)}, 2},
    PhotoEffectLook{"portrait_glow", "Portrait Glow", "Film", "A restrained warm glow that keeps the subject readable.",
      {effect_look_stage("optical_diffusion", 28, .90, .20, .42, .24, .30), effect_look_stage("gaussian_blur", 1.1)}, 2},
    PhotoEffectLook{"cinema_bloom", "Cinema Bloom", "Film", "Warm highlight scatter with textured print detail.",
      {effect_look_stage("optical_diffusion", 34, .78, .16, .50, .52, .24), effect_look_stage("imperfect_print", 64, .04, 0, .98)}, 2},
    PhotoEffectLook{"instant_print", "Instant Print", "Film", "Paper texture and slight color plate drift.",
      {effect_look_stage("imperfect_print", 30, .28, .02, .88, .20, -.10, -.18, .12, 37)}, 1},
    PhotoEffectLook{"plate_shift", "Plate Shift", "Film", "Offset color plates with a crisp, graphic print feel.",
      {effect_look_stage("imperfect_print", 22, .12, .04, 1.0, .45, -.15, -.38, .22, 19), effect_look_stage("unsharp_mask", 1.4, .45, .02)}, 2},
    PhotoEffectLook{"lo_fi_print", "Lo-Fi Print", "Film", "Paper texture with visible color plate offsets.",
      {effect_look_stage("imperfect_print", 18, .22, .025, 1.0, .55, .12, -.48, -.08, 73), effect_look_stage("optical_diffusion", 16, .9, .2, .16, .18, .25)}, 2},
    PhotoEffectLook{"newsprint", "Newsprint", "Print", "Coarse ink texture with a compact tonal palette.",
      {effect_look_stage("posterize_linear_rgb", 7), effect_look_stage("imperfect_print", 16, .16, .06, .82, .22, .08, -.2, -.1, 11)}, 2},
    PhotoEffectLook{"dream_haze", "Dream Haze", "Optical", "Diffuse bright details while keeping shadows calm.",
      {effect_look_stage("optical_diffusion", 46, .94, .20, .40, .12, .35), effect_look_stage("gaussian_blur", .45)}, 2},
    PhotoEffectLook{"soft_focus", "Soft Focus", "Optical", "A broad gentle diffusion with low blur strength.",
      {effect_look_stage("gaussian_blur", 2.2), effect_look_stage("optical_diffusion", 24, .96, .24, .18, .05, .42)}, 2},
    PhotoEffectLook{"mist_filter", "Mist Filter", "Optical", "Subtle highlight fog with a neutral tint.",
      {effect_look_stage("optical_diffusion", 52, .90, .28, .28, 0, .22)}, 1},
    PhotoEffectLook{"golden_bloom", "Golden Bloom", "Optical", "Warm highlight bloom for late sun and practical lights.",
      {effect_look_stage("optical_diffusion", 40, .72, .18, .48, .55, .18)}, 1},
    PhotoEffectLook{"cool_bloom", "Cool Bloom", "Optical", "Cool tinted highlight scatter with protected whites.",
      {effect_look_stage("optical_diffusion", 30, .85, .22, .36, -.24, .40)}, 1},
    PhotoEffectLook{"glassy_diffusion", "Glassy Diffusion", "Optical", "A crisp highlight halo with light texture underneath.",
      {effect_look_stage("optical_diffusion", 16, .70, .12, .58, .08, .08), effect_look_stage("unsharp_mask", 1.0, .22, .03)}, 2},
    PhotoEffectLook{"panning_trail", "Panning Trail", "Motion", "A directional exposure streak for moving scenes.",
      {effect_look_stage("motion_blur", 28, 0)}, 1},
    PhotoEffectLook{"slow_shutter", "Slow Shutter", "Motion", "Long shutter trails with a soft center-weighted falloff.",
      {effect_look_stage("dragged_shutter", 42, 8, .72, .18, 0)}, 1},
    PhotoEffectLook{"diagonal_drift", "Diagonal Drift", "Motion", "A softer diagonal motion trail with highlight bias.",
      {effect_look_stage("motion_blur", 16, 34), effect_look_stage("dragged_shutter", 20, 34, .82, .12, 0)}, 2},
    PhotoEffectLook{"kinetic_echo", "Kinetic Echo", "Motion", "Layered directional blur for a graphic movement feel.",
      {effect_look_stage("motion_blur", 10, 0), effect_look_stage("dragged_shutter", 34, 0, .60, .06, 0)}, 2},
    PhotoEffectLook{"soft_shutter", "Soft Shutter", "Motion", "A gentle directional trail with center-weighted falloff.",
      {effect_look_stage("dragged_shutter", 38, -12, .70, .14, 0)}, 1},
    PhotoEffectLook{"poster_pop", "Poster Pop", "Graphic", "A bold stepped-color treatment with crisp edges.",
      {effect_look_stage("posterize_linear_rgb", 6), effect_look_stage("unsharp_mask", 1.1, .32, .01)}, 2},
    PhotoEffectLook{"color_negative", "Color Negative", "Graphic", "Invert the image and simplify it into a punchy palette.",
      {effect_look_stage("invert_linear_rgb"), effect_look_stage("posterize_linear_rgb", 9)}, 2},
    PhotoEffectLook{"block_color", "Block Color", "Graphic", "Reduced palette with softened small detail.",
      {effect_look_stage("box_blur", 3), effect_look_stage("posterize_linear_rgb", 8)}, 2},
    PhotoEffectLook{"risograph", "Risograph", "Print", "Misregistered ink plates with textured, stepped color.",
      {effect_look_stage("posterize_linear_rgb", 12), effect_look_stage("imperfect_print", 20, .24, .055, .88, .42, -.18, -.32, .14, 29)}, 2},
    PhotoEffectLook{"soft_detail", "Soft Detail", "Portrait", "Mild smoothing with a light detail lift.",
      {effect_look_stage("gaussian_blur", .8), effect_look_stage("unsharp_mask", 2.0, .58, .025)}, 2},
    PhotoEffectLook{"market_glow", "Market Glow", "Food", "A warm, restrained bloom that keeps food texture crisp.",
      {effect_look_stage("optical_diffusion", 16, .88, .24, .25, .35, .45), effect_look_stage("unsharp_mask", 1.0, .35, .02)}, 2},
    PhotoEffectLook{"glass_catchlight", "Glass Catchlight", "Product", "Fine highlight scatter and a clean edge lift for reflective objects.",
      {effect_look_stage("optical_diffusion", 20, .82, .18, .40, .05, .60), effect_look_stage("unsharp_mask", 1.3, .44, .025)}, 2},
    PhotoEffectLook{"architectural_detail", "Architectural Detail", "Architecture", "A controlled fine-detail lift for stone, brick, and clean lines.",
      {effect_look_stage("unsharp_mask", 1.4, .65, .015)}, 1},
    PhotoEffectLook{"cool_editorial_veil", "Cool Editorial Veil", "Optical", "A cool, subtle highlight veil with protected bright whites.",
      {effect_look_stage("optical_diffusion", 18, .92, .22, .24, -.28, .55)}, 1},
    PhotoEffectLook{"quiet_diffusion", "Quiet Diffusion", "Optical", "Low-strength neutral diffusion for a calm, minimal finish.",
      {effect_look_stage("optical_diffusion", 12, .96, .18, .16, 0, .75)}, 1},
    PhotoEffectLook{"pastel_plate", "Pastel Plate", "Print", "Soft paper texture with restrained color plate drift.",
      {effect_look_stage("imperfect_print", 48, .10, .015, .78, .10, -.05, -.08, .05, 107)}, 1},
    PhotoEffectLook{"limited_edition", "Limited Edition", "Print", "A reduced palette and gentle registration offset for a graphic print.",
      {effect_look_stage("posterize_linear_rgb", 6), effect_look_stage("imperfect_print", 22, .08, .04, .90, .25, -.10, -.20, .15, 211)}, 2},
    PhotoEffectLook{"city_pan", "City Pan", "Motion", "A short, restrained horizontal exposure trail for moving street scenes.",
      {effect_look_stage("motion_blur", 9, -12)}, 1},
    PhotoEffectLook{"kinetic_sweep", "Kinetic Sweep", "Motion", "A diagonal shutter drag with moderate center falloff and highlight weighting.",
      {effect_look_stage("dragged_shutter", 28, 34, .56, .32, 0)}, 1},
    PhotoEffectLook{"portrait_gauze", "Portrait Gauze", "Portrait", "Soft highlight diffusion with just enough detail to retain eyes and fabric.",
      {effect_look_stage("optical_diffusion", 30, .92, .25, .26, .10, .65), effect_look_stage("gaussian_blur", .4)}, 2},
    PhotoEffectLook{"moon_halo", "Moon Halo", "Optical", "A cool, gentle halo around bright points and night highlights.",
      {effect_look_stage("optical_diffusion", 34, .76, .22, .46, -.14, .35)}, 1},
    PhotoEffectLook{"matte_paper", "Matte Paper", "Print", "Fine paper texture with minimal ink spread and no strong color shift.",
      {effect_look_stage("imperfect_print", 24, .12, .025, .88, .05, .02, -.06, -.03, 214)}, 1},
    PhotoEffectLook{"fine_35_grain", "Fine 35 Grain", "Film", "Fine monochromatic grain with restrained density for everyday photographs.",
      {effect_look_stage("film_grain", .85, .018, -1, 351)}, 1},
    PhotoEffectLook{"color_400_grain", "Color 400 Grain", "Film", "A fine colored grain finish with a gentle print fade.",
      {effect_look_stage("film_grain", 1.2, .027, .25, 400), effect_look_stage("soft_fade", .012, .97, .96)}, 2},
    PhotoEffectLook{"silver_grain_study", "Silver Grain Study", "Film", "A nearly monochrome silver print with coarser, seeded grain.",
      {effect_look_stage("soft_fade", .008, 1.02, .05), effect_look_stage("film_grain", 1.6, .035, -1, 1600)}, 2},
    PhotoEffectLook{"dusk_emulsion", "Dusk Emulsion", "Film", "Fine grain and a quiet amber highlight tint for evening scenes.",
      {effect_look_stage("split_tone", 225, .025, 38, .035, 0), effect_look_stage("film_grain", 1.1, .022, -.65, 217)}, 2},
    PhotoEffectLook{"amber_edge_leak", "Amber Edge Leak", "Light", "A soft amber exposure entering from the left edge.",
      {effect_look_stage("light_leak", -.08, .45, 15, .28, .16, .8, 41)}, 1},
    PhotoEffectLook{"rose_corner_leak", "Rose Corner Leak", "Light", "A warm corner wash with a subtle fine-grain finish.",
      {effect_look_stage("light_leak", 1.05, -.06, -35, .32, .12, .45, 67), effect_look_stage("film_grain", .9, .014, -1, 67)}, 2},
    PhotoEffectLook{"blue_edge_leak", "Blue Edge Leak", "Light", "A cool, low-strength exposure wash entering from the right edge.",
      {effect_look_stage("light_leak", 1.12, .6, -10, .26, .10, -.7, 113)}, 1},
    PhotoEffectLook{"window_flare_veil", "Window Flare Veil", "Light", "A broad warm edge veil with restrained highlight diffusion.",
      {effect_look_stage("light_leak", -.15, -.05, 35, .48, .09, .3, 89), effect_look_stage("optical_diffusion", 16, .9, .2, .12, .12, .65)}, 2},
    PhotoEffectLook{"teal_amber_balance", "Teal Amber Balance", "Tonal", "Subtle teal shadows and warm highlights with a calm tonal transition.",
      {effect_look_stage("split_tone", 190, .055, 36, .045, -.05)}, 1},
    PhotoEffectLook{"lavender_cream", "Lavender Cream", "Tonal", "Muted lavender shadows with cream highlights and a soft matte base.",
      {effect_look_stage("split_tone", 265, .035, 48, .045, .1), effect_look_stage("soft_fade", .018, .97, .94)}, 2},
    PhotoEffectLook{"sepia_ink", "Sepia Ink", "Duotone", "A warm two-tone ink treatment for portraits and editorial studies.",
      {effect_look_stage("duotone", 24, 44, .78, .04), effect_look_stage("film_grain", .8, .012, -1, 24)}, 2},
    PhotoEffectLook{"cyan_ink_study", "Cyan Ink Study", "Duotone", "A cool blue-to-cyan two-tone print with fine paper variation.",
      {effect_look_stage("duotone", 222, 188, .85, -.05), effect_look_stage("imperfect_print", 28, .055, .008, .98, 0, 0, 0, 0, 188)}, 2},
    PhotoEffectLook{"indigo_coral_ink", "Indigo Coral Ink", "Duotone", "An expressive indigo and coral two-tone finish, softened with a small matte lift.",
      {effect_look_stage("duotone", 248, 16, .72, .08), effect_look_stage("soft_fade", .012, .98, 1)}, 2},
    PhotoEffectLook{"matte_black_finish", "Matte Black Finish", "Tonal", "Gently lifted blacks with restrained color and fine monochromatic grain.",
      {effect_look_stage("soft_fade", .025, .94, .9), effect_look_stage("film_grain", .9, .016, -1, 94)}, 2},
    PhotoEffectLook{"linen_color_fade", "Linen Color Fade", "Tonal", "An airy, low-contrast color finish with fine paper variation.",
      {effect_look_stage("soft_fade", .018, .91, .82), effect_look_stage("imperfect_print", 40, .04, 0, .98, 0, 0, 0, 0, 82)}, 2},
    PhotoEffectLook{"quiet_lens_falloff", "Quiet Lens Falloff", "Optical", "A broad feathered edge falloff that keeps the center open.",
      {effect_look_stage("vignette", .78, 0, .75, .22)}, 1}};

inline photo_editor::pipeline::PhotoEffectInstance materialize_photo_effect_look_stage(
    const PhotoEffectLookStage& stage, std::string id,
    photo_editor::pipeline::PhotoEffectInputScope input_scope) {
  using photo_editor::pipeline::PhotoEffectInstance;
  PhotoEffectInstance effect;
  effect.id = std::move(id);
  effect.type_id = std::string(stage.type);
  effect.algorithm_version = 1;
  effect.enabled = true;
  effect.amount = 1.0;
  effect.input_scope = input_scope;
  const auto& p = stage.values;
  if (stage.type == "film_grain") {
    effect.grain.size = p[0]; effect.grain.strength = p[1]; effect.grain.color = p[2];
    effect.grain.seed = static_cast<std::uint32_t>(p[3]);
  } else if (stage.type == "light_leak") {
    effect.leak.center_x = p[0]; effect.leak.center_y = p[1]; effect.leak.angle = p[2];
    effect.leak.width = p[3]; effect.leak.strength = p[4]; effect.leak.warmth = p[5];
    effect.leak.seed = static_cast<std::uint32_t>(p[6]);
  } else if (stage.type == "split_tone") {
    effect.split_tone.shadow_hue = p[0]; effect.split_tone.shadow_strength = p[1];
    effect.split_tone.highlight_hue = p[2]; effect.split_tone.highlight_strength = p[3]; effect.split_tone.balance = p[4];
  } else if (stage.type == "vignette") {
    effect.vignette.radius = p[0]; effect.vignette.roundness = p[1];
    effect.vignette.feather = p[2]; effect.vignette.strength = p[3];
  } else if (stage.type == "duotone") {
    effect.duotone.shadow_hue = p[0]; effect.duotone.highlight_hue = p[1];
    effect.duotone.strength = p[2]; effect.duotone.balance = p[3];
  } else if (stage.type == "soft_fade") {
    effect.soft_fade.lift = p[0]; effect.soft_fade.contrast = p[1]; effect.soft_fade.saturation = p[2];
  } else if (stage.type == "gaussian_blur") effect.gaussian.sigma = p[0];
  else if (stage.type == "box_blur") effect.box.radius = static_cast<int>(p[0]);
  else if (stage.type == "posterize_linear_rgb") effect.posterize_levels = static_cast<int>(p[0]);
  else if (stage.type == "motion_blur") {
    effect.motion.distance = p[0]; effect.motion.angle = p[1];
  } else if (stage.type == "dragged_shutter") {
    effect.dragged_shutter.distance = p[0]; effect.dragged_shutter.angle = p[1];
    effect.dragged_shutter.falloff = p[2]; effect.dragged_shutter.highlight_weighting = p[3];
    effect.dragged_shutter.protect_subject = p[4] >= 0.5;
  } else if (stage.type == "unsharp_mask") {
    effect.unsharp.sigma = p[0]; effect.unsharp.strength = p[1]; effect.unsharp.threshold = p[2];
  } else if (stage.type == "optical_diffusion") {
    effect.diffusion.radius = p[0]; effect.diffusion.threshold = p[1];
    effect.diffusion.knee = p[2]; effect.diffusion.strength = p[3];
    effect.diffusion.warmth = p[4]; effect.diffusion.highlight_protection = p[5];
  } else if (stage.type == "imperfect_print") {
    effect.imperfect_print.paper_scale = p[0]; effect.imperfect_print.paper_texture = p[1];
    effect.imperfect_print.ink_spread = p[2]; effect.imperfect_print.ink_density = p[3];
    effect.imperfect_print.red_offset_x = p[4]; effect.imperfect_print.red_offset_y = p[5];
    effect.imperfect_print.blue_offset_x = p[6]; effect.imperfect_print.blue_offset_y = p[7];
    effect.imperfect_print.seed = static_cast<std::uint32_t>(p[8]);
  }
  return effect;
}

}  // namespace photo_editor::desktop
