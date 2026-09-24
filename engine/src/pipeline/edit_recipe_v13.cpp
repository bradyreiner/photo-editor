#include "photo_editor/pipeline/edit_recipe_v13.h"
#include "photo_editor/pipeline/photo_effect_codec.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <set>
#include <sstream>
#include <type_traits>

namespace photo_editor::pipeline {
namespace {

constexpr std::string_view kRecipeMagic =
    "photo-editor-edit-recipe-v13.2";
constexpr std::string_view kExportSpaceRecipeMagic =
    "photo-editor-edit-recipe-v13.3";
constexpr std::string_view kEffectsRecipeMagic =
    "photo-editor-edit-recipe-v13.4";
constexpr std::string_view kPreviousRecipeMagic =
    "photo-editor-edit-recipe-v13.1";
constexpr std::string_view kLegacyRecipeMagic =
    "photo-editor-edit-recipe-v13";

void add_error(
    RecipeValidationResult* result,
    RecipeErrorCode code,
    std::string path,
    std::string message) {
  result->errors.push_back(RecipeError{
      .code = code,
      .path = std::move(path),
      .message = std::move(message),
  });
}

void validate_finite_range(
    RecipeValidationResult* result,
    double value,
    double minimum,
    double maximum,
    std::string path) {
  if (!std::isfinite(value)) {
    add_error(
        result,
        RecipeErrorCode::NonFiniteValue,
        std::move(path),
        "value must be finite");
    return;
  }
  if (value < minimum || value > maximum) {
    add_error(
        result,
        RecipeErrorCode::ValueOutOfRange,
        std::move(path),
        "value is outside the supported range");
  }
}

void validate_identifier(
    RecipeValidationResult* result,
    const AssetIdentifier& identifier,
    std::string path) {
  if (identifier.value.empty()) {
    add_error(
        result,
        RecipeErrorCode::MissingIdentifier,
        std::move(path),
        "identifier must not be empty");
  }
}

void validate_scene(
    RecipeValidationResult* result,
    const SceneRecipeV13& scene,
    const std::string& path) {
  validate_finite_range(
      result,
      scene.exposure_ev,
      -20.0,
      20.0,
      path + ".exposure_ev");
}

void validate_tone(
    RecipeValidationResult* result,
    const ToneRecipeV13& tone,
    const std::string& path) {
  validate_finite_range(result, tone.contrast, -1.0, 1.0, path + ".contrast");
  validate_finite_range(result, tone.highlights, -1.0, 1.0, path + ".highlights");
  validate_finite_range(result, tone.shadows, -1.0, 1.0, path + ".shadows");
  validate_finite_range(result, tone.whites, -1.0, 1.0, path + ".whites");
  validate_finite_range(result, tone.blacks, -1.0, 1.0, path + ".blacks");
  validate_finite_range(result, tone.brightness, -1.0, 1.0, path + ".brightness");
  validate_finite_range(result, tone.hdr_local_tone_mapping, -1.0, 1.0, path + ".hdr_local_tone_mapping");
  validate_finite_range(result, tone.filmic_shoulder, -1.0, 1.0, path + ".filmic_shoulder");
  validate_finite_range(result, tone.filmic_toe, -1.0, 1.0, path + ".filmic_toe");
  validate_finite_range(result, tone.adaptive_contrast, -1.0, 1.0, path + ".adaptive_contrast");
  validate_finite_range(result, tone.levels_black_point, -1.0, 1.0, path + ".levels_black_point");
  validate_finite_range(result, tone.levels_gamma, -1.0, 1.0, path + ".levels_gamma");
  validate_finite_range(result, tone.levels_white_point, -1.0, 1.0, path + ".levels_white_point");
  validate_finite_range(result, tone.zone_deep_shadows, -1.0, 1.0, path + ".zone_deep_shadows");
  validate_finite_range(result, tone.zone_shadows, -1.0, 1.0, path + ".zone_shadows");
  validate_finite_range(result, tone.zone_midtones, -1.0, 1.0, path + ".zone_midtones");
  validate_finite_range(result, tone.zone_lights, -1.0, 1.0, path + ".zone_lights");
  validate_finite_range(result, tone.zone_highlights, -1.0, 1.0, path + ".zone_highlights");
}

void validate_look(
    RecipeValidationResult* result,
    const LookRecipeV13& look,
    const std::string& path) {
  validate_finite_range(result, look.temperature, -1.0, 1.0, path + ".temperature");
  validate_finite_range(result, look.tint, -1.0, 1.0, path + ".tint");
  validate_finite_range(result, look.vibrance, -1.0, 1.0, path + ".vibrance");
  validate_finite_range(result, look.saturation, -1.0, 1.0, path + ".saturation");
  validate_finite_range(result, look.hue_shift, -1.0, 1.0, path + ".hue_shift");
  validate_finite_range(
      result, look.saturation_shift, -1.0, 1.0, path + ".saturation_shift");
  validate_finite_range(result, look.colorize_hue, 0.0, 1.0, path + ".colorize_hue");
  validate_finite_range(
      result, look.colorize_strength, -1.0, 1.0, path + ".colorize_strength");
  for (std::size_t index = 0; index < look.mixer_channels.size(); ++index) {
    const auto channel_path =
        path + ".mixer_channels[" + std::to_string(index) + "]";
    const auto& channel = look.mixer_channels[index];
    validate_finite_range(result, channel.hue, -1.0, 1.0, channel_path + ".hue");
    validate_finite_range(result, channel.saturation, -1.0, 1.0, channel_path + ".saturation");
    validate_finite_range(result, channel.luminance, -1.0, 1.0, channel_path + ".luminance");
    validate_finite_range(result, channel.selective_cyan, -1.0, 1.0, channel_path + ".selective_cyan");
    validate_finite_range(result, channel.selective_magenta, -1.0, 1.0, channel_path + ".selective_magenta");
    validate_finite_range(result, channel.selective_yellow, -1.0, 1.0, channel_path + ".selective_yellow");
    validate_finite_range(result, channel.selective_black, -1.0, 1.0, channel_path + ".selective_black");
  }
  const auto& rgb = look.rgb_channel_mixer;
  const std::array<std::pair<double, std::string>, 9> rgb_values = {{
      {rgb.red_from_red, ".red_from_red"},
      {rgb.red_from_green, ".red_from_green"},
      {rgb.red_from_blue, ".red_from_blue"},
      {rgb.green_from_red, ".green_from_red"},
      {rgb.green_from_green, ".green_from_green"},
      {rgb.green_from_blue, ".green_from_blue"},
      {rgb.blue_from_red, ".blue_from_red"},
      {rgb.blue_from_green, ".blue_from_green"},
      {rgb.blue_from_blue, ".blue_from_blue"},
  }};
  for (const auto& [value, suffix] : rgb_values) {
    validate_finite_range(result, value, -4.0, 4.0, path + ".rgb_channel_mixer" + suffix);
  }
  for (std::size_t index = 0; index < look.black_and_white_mixer.size(); ++index) {
    validate_finite_range(
        result, look.black_and_white_mixer[index], -1.0, 1.0,
        path + ".black_and_white_mixer[" + std::to_string(index) + "]");
  }
  validate_finite_range(result, look.color_replacement_source_hue, 0.0, 1.0, path + ".color_replacement_source_hue");
  validate_finite_range(result, look.color_replacement_target_hue, 0.0, 1.0, path + ".color_replacement_target_hue");
  validate_finite_range(result, look.color_replacement_range, 0.0, 1.0, path + ".color_replacement_range");
  validate_finite_range(result, look.color_replacement_softness, 0.0, 1.0, path + ".color_replacement_softness");
  validate_finite_range(result, look.color_replacement_strength, -1.0, 1.0, path + ".color_replacement_strength");
  validate_finite_range(result, look.skin_tone_line_hue, 0.0, 1.0, path + ".skin_tone_line_hue");
  validate_finite_range(result, look.skin_tone_protection_range, 0.0, 1.0, path + ".skin_tone_protection_range");
  validate_finite_range(result, look.skin_tone_protection_strength, -1.0, 1.0, path + ".skin_tone_protection_strength");
  validate_finite_range(result, look.color_harmony_base_hue, 0.0, 1.0, path + ".color_harmony_base_hue");
  validate_finite_range(result, look.color_harmony_strength, -1.0, 1.0, path + ".color_harmony_strength");
  const auto validate_grade = [&](const ColorGradingZoneV13& grade, const std::string& grade_path) {
    validate_finite_range(result, grade.hue, 0.0, 1.0, grade_path + ".hue");
    validate_finite_range(result, grade.saturation, -1.0, 1.0, grade_path + ".saturation");
    validate_finite_range(result, grade.luminance, -1.0, 1.0, grade_path + ".luminance");
  };
  validate_grade(look.grading_shadows, path + ".grading_shadows");
  validate_grade(look.grading_midtones, path + ".grading_midtones");
  validate_grade(look.grading_highlights, path + ".grading_highlights");
  validate_finite_range(result, look.grading_amount, 0.0, 1.0, path + ".grading_amount");
  validate_finite_range(result, look.grading_balance, -1.0, 1.0, path + ".grading_balance");
  for (std::size_t index = 0; index < look.luts.size(); ++index) {
    const auto lut_path =
        path + ".luts[" + std::to_string(index) + "]";
    const auto& lut = look.luts[index];
    validate_identifier(result, lut.payload_asset, lut_path + ".payload_asset");
    validate_finite_range(
        result, lut.strength, 0.0, 1.0, lut_path + ".strength");
    if (lut.input_color_space.empty() || lut.output_color_space.empty()) {
      add_error(
          result,
          RecipeErrorCode::MissingIdentifier,
          lut_path + ".color_space",
          "creative LUT input and output color spaces must be explicit");
    }
    for (std::size_t channel = 0; channel < 3U; ++channel) {
      validate_finite_range(
          result,
          lut.domain_min[channel],
          -65504.0,
          65504.0,
          lut_path + ".domain_min[" + std::to_string(channel) + "]");
      validate_finite_range(
          result,
          lut.domain_max[channel],
          -65504.0,
          65504.0,
          lut_path + ".domain_max[" + std::to_string(channel) + "]");
      if (lut.domain_min[channel] >= lut.domain_max[channel]) {
        add_error(
            result,
            RecipeErrorCode::ValueOutOfRange,
            lut_path + ".domain",
            "creative LUT domain minimum must be below its maximum");
      }
    }
  }
}

void validate_layer_geometry(
    RecipeValidationResult* result,
    const GeometryRecipeV13& geometry,
    const std::string& path) {
  validate_finite_range(
      result, geometry.rotation_degrees, -360.0, 360.0,
      path + ".rotation_degrees");
  validate_finite_range(
      result, geometry.lens_distortion, -1.0, 1.0,
      path + ".lens_distortion");
  validate_finite_range(
      result, geometry.lens_vignette, -1.0, 1.0,
      path + ".lens_vignette");
  validate_finite_range(
      result, geometry.chromatic_aberration, -1.0, 1.0,
      path + ".chromatic_aberration");
  validate_finite_range(
      result, geometry.perspective_vertical, -1.0, 1.0,
      path + ".perspective_vertical");
  validate_finite_range(
      result, geometry.perspective_horizontal, -1.0, 1.0,
      path + ".perspective_horizontal");
  validate_finite_range(result, geometry.scale, 0.01, 8.0, path + ".scale");
  validate_finite_range(result, geometry.skew_x, -1.0, 1.0, path + ".skew_x");
  validate_finite_range(result, geometry.skew_y, -1.0, 1.0, path + ".skew_y");
  validate_finite_range(
      result, geometry.crop_zoom, 0.01, 8.0, path + ".crop_zoom");
  validate_finite_range(
      result, geometry.liquify_center_x, 0.0, 1.0,
      path + ".liquify_center_x");
  validate_finite_range(
      result, geometry.liquify_center_y, 0.0, 1.0,
      path + ".liquify_center_y");
  validate_finite_range(
      result, geometry.liquify_radius, 0.0, 1.0,
      path + ".liquify_radius");
  validate_finite_range(
      result, geometry.liquify_strength, -1.0, 1.0,
      path + ".liquify_strength");
}

template <typename Enum>
void write_enum(std::ostream& stream, Enum value) {
  stream << static_cast<int>(
                static_cast<std::underlying_type_t<Enum>>(value))
         << ' ';
}

void write_string(std::ostream& stream, const std::string& value) {
  stream << std::quoted(value) << ' ';
}

void write_bool(std::ostream& stream, bool value) {
  stream << (value ? 1 : 0) << ' ';
}

void write_optional_identifier(
    std::ostream& stream,
    const std::optional<AssetIdentifier>& value) {
  write_bool(stream, value.has_value());
  if (value.has_value()) {
    write_string(stream, value->value);
  }
}

void write_optional_double(
    std::ostream& stream,
    const std::optional<double>& value) {
  write_bool(stream, value.has_value());
  if (value.has_value()) {
    stream << *value << ' ';
  }
}

void write_scene(std::ostream& stream, const SceneRecipeV13& scene) {
  stream << scene.exposure_ev << ' ';
}

void write_tone(std::ostream& stream, const ToneRecipeV13& tone) {
  stream << tone.contrast << ' ' << tone.highlights << ' ' << tone.shadows << ' '
         << tone.whites << ' ' << tone.blacks << ' ' << tone.brightness << ' '
         << tone.hdr_local_tone_mapping << ' ' << tone.filmic_shoulder << ' '
         << tone.filmic_toe << ' ' << tone.adaptive_contrast << ' '
         << tone.levels_black_point << ' ' << tone.levels_gamma << ' '
         << tone.levels_white_point << ' ' << tone.zone_deep_shadows << ' '
         << tone.zone_shadows << ' ' << tone.zone_midtones << ' '
         << tone.zone_lights << ' ' << tone.zone_highlights << ' ';
}

void write_look(std::ostream& stream, const LookRecipeV13& look) {
  write_enum(stream, look.renderer);
  stream << look.temperature << ' ' << look.tint << ' ' << look.vibrance << ' '
         << look.saturation << ' ' << look.hue_shift << ' '
         << look.saturation_shift << ' ' << look.colorize_hue << ' '
         << look.colorize_strength << ' ';
  write_enum(stream, look.mixer_model);
  for (const auto& channel : look.mixer_channels) {
    stream << channel.hue << ' ' << channel.saturation << ' '
           << channel.luminance << ' ' << channel.selective_cyan << ' '
           << channel.selective_magenta << ' ' << channel.selective_yellow
           << ' ' << channel.selective_black << ' ';
  }
  const auto& rgb = look.rgb_channel_mixer;
  stream << rgb.red_from_red << ' ' << rgb.red_from_green << ' '
         << rgb.red_from_blue << ' ' << rgb.green_from_red << ' '
         << rgb.green_from_green << ' ' << rgb.green_from_blue << ' '
         << rgb.blue_from_red << ' ' << rgb.blue_from_green << ' '
         << rgb.blue_from_blue << ' ';
  write_bool(stream, look.black_and_white_enabled);
  for (const auto value : look.black_and_white_mixer) {
    stream << value << ' ';
  }
  write_bool(stream, look.color_replacement_enabled);
  stream << look.color_replacement_source_hue << ' '
         << look.color_replacement_target_hue << ' '
         << look.color_replacement_range << ' '
         << look.color_replacement_softness << ' '
         << look.color_replacement_strength << ' ';
  write_bool(stream, look.color_replacement_preserve_luminance);
  write_bool(stream, look.color_replacement_preserve_saturation);
  write_bool(stream, look.skin_tone_protection_enabled);
  stream << look.skin_tone_line_hue << ' '
         << look.skin_tone_protection_range << ' '
         << look.skin_tone_protection_strength << ' ';
  write_bool(stream, look.color_harmony_enabled);
  write_enum(stream, look.color_harmony_mode);
  stream << look.color_harmony_base_hue << ' '
         << look.color_harmony_strength << ' ';
  const auto write_grade = [&](const ColorGradingZoneV13& grade) {
    stream << grade.hue << ' ' << grade.saturation << ' '
           << grade.luminance << ' ';
  };
  write_grade(look.grading_shadows);
  write_grade(look.grading_midtones);
  write_grade(look.grading_highlights);
  stream << look.grading_amount << ' ' << look.grading_balance << ' ';
  stream << look.luts.size() << ' ';
  for (const auto& lut : look.luts) {
    write_string(stream, lut.payload_asset.value);
    write_bool(stream, lut.enabled);
    stream << lut.strength << ' ';
    write_string(stream, lut.name);
    for (const auto value : lut.domain_min) {
      stream << value << ' ';
    }
    for (const auto value : lut.domain_max) {
      stream << value << ' ';
    }
    write_string(stream, lut.input_color_space);
    write_string(stream, lut.output_color_space);
  }
}

void write_curve(
    std::ostream& stream,
    const std::vector<CurvePointV13>& points) {
  stream << points.size() << ' ';
  for (const auto& point : points) {
    stream << point.x << ' ' << point.y << ' ';
  }
}

void write_detail(std::ostream& stream, const DetailRecipeV13& detail) {
  stream << detail.texture << ' ' << detail.clarity << ' '
         << detail.dehaze << ' ' << detail.sharpening << ' '
         << detail.sharpen_radius << ' ' << detail.sharpen_detail << ' '
         << detail.sharpen_masking << ' '
         << detail.luminance_noise_reduction << ' '
         << detail.chroma_noise_reduction << ' ';
}

void write_geometry(
    std::ostream& stream,
    const GeometryRecipeV13& geometry) {
  stream << geometry.crop_x << ' ' << geometry.crop_y << ' '
         << geometry.crop_width << ' ' << geometry.crop_height << ' '
         << geometry.rotation_degrees << ' ' << geometry.lens_distortion << ' '
         << geometry.lens_vignette << ' ' << geometry.chromatic_aberration
         << ' ' << geometry.perspective_vertical << ' '
         << geometry.perspective_horizontal << ' ' << geometry.scale << ' '
         << geometry.skew_x << ' ' << geometry.skew_y << ' '
         << geometry.crop_zoom << ' ';
  write_enum(stream, geometry.upright_mode);
  stream << geometry.liquify_center_x << ' ' << geometry.liquify_center_y
         << ' ' << geometry.liquify_radius << ' '
         << geometry.liquify_strength << ' ';
  write_bool(stream, geometry.content_aware_crop_fill);
}

void write_mask_geometry(
    std::ostream& stream,
    const MaskGeometryV13& geometry) {
  stream << geometry.feather << ' ' << geometry.position << ' '
         << geometry.center_x << ' ' << geometry.center_y << ' '
         << geometry.radius_x << ' ' << geometry.radius_y << ' '
         << geometry.rotation_degrees << ' ' << geometry.range_min << ' '
         << geometry.range_max << ' ';
}

bool read_bool(std::istream& stream, bool* value) {
  int encoded = 0;
  if (!(stream >> encoded) || (encoded != 0 && encoded != 1)) {
    return false;
  }
  *value = encoded == 1;
  return true;
}

template <typename Enum>
bool read_enum(std::istream& stream, Enum* value, int maximum) {
  int encoded = 0;
  if (!(stream >> encoded) || encoded < 0 || encoded > maximum) {
    return false;
  }
  *value = static_cast<Enum>(encoded);
  return true;
}

bool read_string(std::istream& stream, std::string* value) {
  return static_cast<bool>(stream >> std::quoted(*value));
}

bool read_optional_identifier(
    std::istream& stream,
    std::optional<AssetIdentifier>* value) {
  bool present = false;
  if (!read_bool(stream, &present)) {
    return false;
  }
  if (!present) {
    value->reset();
    return true;
  }
  AssetIdentifier identifier;
  if (!read_string(stream, &identifier.value)) {
    return false;
  }
  *value = std::move(identifier);
  return true;
}

bool read_optional_double(
    std::istream& stream,
    std::optional<double>* value) {
  bool present = false;
  if (!read_bool(stream, &present)) {
    return false;
  }
  if (!present) {
    value->reset();
    return true;
  }
  double decoded = 0.0;
  if (!(stream >> decoded)) {
    return false;
  }
  *value = decoded;
  return true;
}

bool read_scene(std::istream& stream, SceneRecipeV13* scene) {
  return static_cast<bool>(stream >> scene->exposure_ev);
}

bool read_tone(std::istream& stream, ToneRecipeV13* tone) {
  return static_cast<bool>(
      stream >> tone->contrast >> tone->highlights >> tone->shadows >>
      tone->whites >> tone->blacks >> tone->brightness >>
      tone->hdr_local_tone_mapping >> tone->filmic_shoulder >>
      tone->filmic_toe >> tone->adaptive_contrast >>
      tone->levels_black_point >> tone->levels_gamma >>
      tone->levels_white_point >> tone->zone_deep_shadows >>
      tone->zone_shadows >> tone->zone_midtones >> tone->zone_lights >>
      tone->zone_highlights);
}

bool read_look(std::istream& stream, LookRecipeV13* look) {
  if (!read_enum(stream, &look->renderer, 0) ||
      !(stream >> look->temperature >> look->tint >> look->vibrance >>
        look->saturation >> look->hue_shift >> look->saturation_shift >>
        look->colorize_hue >> look->colorize_strength) ||
      !read_enum(stream, &look->mixer_model, 3)) {
    return false;
  }
  for (auto& channel : look->mixer_channels) {
    if (!(stream >> channel.hue >> channel.saturation >> channel.luminance >>
          channel.selective_cyan >> channel.selective_magenta >>
          channel.selective_yellow >> channel.selective_black)) {
      return false;
    }
  }
  auto& rgb = look->rgb_channel_mixer;
  if (!(stream >> rgb.red_from_red >> rgb.red_from_green >>
        rgb.red_from_blue >> rgb.green_from_red >> rgb.green_from_green >>
        rgb.green_from_blue >> rgb.blue_from_red >> rgb.blue_from_green >>
        rgb.blue_from_blue) ||
      !read_bool(stream, &look->black_and_white_enabled)) {
    return false;
  }
  for (auto& value : look->black_and_white_mixer) {
    if (!(stream >> value)) {
      return false;
    }
  }
  if (!read_bool(stream, &look->color_replacement_enabled) ||
      !(stream >> look->color_replacement_source_hue >>
        look->color_replacement_target_hue >>
        look->color_replacement_range >>
        look->color_replacement_softness >>
        look->color_replacement_strength) ||
      !read_bool(stream, &look->color_replacement_preserve_luminance) ||
      !read_bool(stream, &look->color_replacement_preserve_saturation) ||
      !read_bool(stream, &look->skin_tone_protection_enabled) ||
      !(stream >> look->skin_tone_line_hue >>
        look->skin_tone_protection_range >>
        look->skin_tone_protection_strength) ||
      !read_bool(stream, &look->color_harmony_enabled) ||
      !read_enum(stream, &look->color_harmony_mode, 4) ||
      !(stream >> look->color_harmony_base_hue >>
        look->color_harmony_strength)) {
    return false;
  }
  const auto read_grade = [&](ColorGradingZoneV13* grade) {
    return static_cast<bool>(
        stream >> grade->hue >> grade->saturation >> grade->luminance);
  };
  if (!read_grade(&look->grading_shadows) ||
      !read_grade(&look->grading_midtones) ||
      !read_grade(&look->grading_highlights) ||
      !(stream >> look->grading_amount >> look->grading_balance)) {
    return false;
  }
  std::size_t lut_count = 0;
  if (!(stream >> lut_count) || lut_count > 65536U) {
    return false;
  }
  look->luts.resize(lut_count);
  for (auto& lut : look->luts) {
    if (!read_string(stream, &lut.payload_asset.value) ||
        !read_bool(stream, &lut.enabled) ||
        !(stream >> lut.strength) ||
        !read_string(stream, &lut.name)) {
      return false;
    }
    for (auto& value : lut.domain_min) {
      if (!(stream >> value)) {
        return false;
      }
    }
    for (auto& value : lut.domain_max) {
      if (!(stream >> value)) {
        return false;
      }
    }
    if (!read_string(stream, &lut.input_color_space) ||
        !read_string(stream, &lut.output_color_space)) {
      return false;
    }
  }
  return true;
}

bool read_curve(
    std::istream& stream,
    std::vector<CurvePointV13>* points) {
  std::size_t count = 0;
  if (!(stream >> count) || count > 65536U) {
    return false;
  }
  points->resize(count);
  for (auto& point : *points) {
    if (!(stream >> point.x >> point.y)) {
      return false;
    }
  }
  return true;
}

bool read_detail(std::istream& stream, DetailRecipeV13* detail) {
  return static_cast<bool>(
      stream >> detail->texture >> detail->clarity >> detail->dehaze >>
      detail->sharpening >> detail->sharpen_radius >> detail->sharpen_detail >>
      detail->sharpen_masking >> detail->luminance_noise_reduction >>
      detail->chroma_noise_reduction);
}

bool read_geometry(
    std::istream& stream,
    GeometryRecipeV13* geometry) {
  return static_cast<bool>(
             stream >> geometry->crop_x >> geometry->crop_y >>
             geometry->crop_width >> geometry->crop_height >>
             geometry->rotation_degrees >> geometry->lens_distortion >>
             geometry->lens_vignette >> geometry->chromatic_aberration >>
             geometry->perspective_vertical >>
             geometry->perspective_horizontal >> geometry->scale >>
             geometry->skew_x >> geometry->skew_y >> geometry->crop_zoom) &&
         read_enum(stream, &geometry->upright_mode, 3) &&
         static_cast<bool>(
             stream >> geometry->liquify_center_x >>
             geometry->liquify_center_y >> geometry->liquify_radius >>
             geometry->liquify_strength) &&
         read_bool(stream, &geometry->content_aware_crop_fill);
}

bool read_mask_geometry(
    std::istream& stream,
    MaskGeometryV13* geometry) {
  return static_cast<bool>(
      stream >> geometry->feather >> geometry->position >>
      geometry->center_x >> geometry->center_y >> geometry->radius_x >>
      geometry->radius_y >> geometry->rotation_degrees >>
      geometry->range_min >> geometry->range_max);
}

void set_decode_error(RecipeError* error, std::string message) {
  if (error == nullptr) {
    return;
  }
  *error = RecipeError{
      .code = RecipeErrorCode::MalformedSerialization,
      .path = "recipe",
      .message = std::move(message),
  };
}

}  // namespace

RecipeValidationResult validate_edit_recipe_v13(const EditRecipeV13& recipe) {
  RecipeValidationResult result;
  if (recipe.recipe_version != kEditRecipeV13Version) {
    add_error(
        &result,
        RecipeErrorCode::InvalidVersion,
        "recipe_version",
        "recipe version must be 13");
  }
  validate_identifier(&result, recipe.recipe_id, "recipe_id");
  validate_identifier(&result, recipe.input.source, "input.source");
  if (recipe.input.decoder_provenance.empty()) {
    add_error(
        &result,
        RecipeErrorCode::MissingIdentifier,
        "input.decoder_provenance",
        "decoder provenance must not be empty");
  }
  const bool explicit_camera_profile =
      recipe.input.camera_profile_mode == CameraProfileMode::Dcp ||
      recipe.input.camera_profile_mode == CameraProfileMode::Icc;
  if (explicit_camera_profile && !recipe.input.camera_profile.has_value()) {
    add_error(
        &result,
        RecipeErrorCode::MissingProfile,
        "input.camera_profile",
        "DCP and ICC modes require an identified profile asset");
  }
  if (recipe.input.camera_profile.has_value()) {
    validate_identifier(&result, *recipe.input.camera_profile, "input.camera_profile");
  }
  if (recipe.input.white_balance_temperature_kelvin.has_value()) {
    validate_finite_range(
        &result,
        *recipe.input.white_balance_temperature_kelvin,
        1000.0,
        100000.0,
        "input.white_balance_temperature_kelvin");
  }
  if (recipe.input.white_balance_tint.has_value()) {
    validate_finite_range(
        &result,
        *recipe.input.white_balance_tint,
        -2.0,
        2.0,
        "input.white_balance_tint");
  }
  if (recipe.input.black_level_override.has_value()) {
    validate_finite_range(
        &result,
        *recipe.input.black_level_override,
        0.0,
        1.0,
        "input.black_level_override");
  }
  if (recipe.input.white_level_override.has_value()) {
    validate_finite_range(
        &result,
        *recipe.input.white_level_override,
        0.000001,
        16.0,
        "input.white_level_override");
  }
  validate_finite_range(&result, recipe.input.hot_dead_pixel_cleanup, 0.0, 1.0, "input.hot_dead_pixel_cleanup");
  validate_finite_range(&result, recipe.input.false_color_suppression, 0.0, 1.0, "input.false_color_suppression");
  validate_finite_range(&result, recipe.input.calibration_hue, -1.0, 1.0, "input.calibration_hue");
  validate_finite_range(&result, recipe.input.calibration_saturation, -1.0, 1.0, "input.calibration_saturation");
  validate_finite_range(&result, recipe.input.red_primary_hue, -1.0, 1.0, "input.red_primary_hue");
  validate_finite_range(&result, recipe.input.red_primary_saturation, -1.0, 1.0, "input.red_primary_saturation");
  validate_finite_range(&result, recipe.input.green_primary_hue, -1.0, 1.0, "input.green_primary_hue");
  validate_finite_range(&result, recipe.input.green_primary_saturation, -1.0, 1.0, "input.green_primary_saturation");
  validate_finite_range(&result, recipe.input.blue_primary_hue, -1.0, 1.0, "input.blue_primary_hue");
  validate_finite_range(&result, recipe.input.blue_primary_saturation, -1.0, 1.0, "input.blue_primary_saturation");

  validate_scene(&result, recipe.scene, "scene");
  validate_tone(&result, recipe.tone, "tone");
  validate_look(&result, recipe.look, "look");

  const auto validate_curve =
      [&](const std::vector<CurvePointV13>& points,
          const std::string& path_base) {
        double previous_x = -1.0;
        for (std::size_t index = 0; index < points.size(); ++index) {
          const auto& point = points[index];
          const auto path =
              path_base + "[" + std::to_string(index) + "]";
          validate_finite_range(&result, point.x, 0.0, 1.0, path + ".x");
          validate_finite_range(&result, point.y, 0.0, 1.0, path + ".y");
          if (std::isfinite(point.x) && point.x <= previous_x) {
            add_error(
                &result,
                RecipeErrorCode::InvalidCurve,
                path + ".x",
                "curve x coordinates must be strictly increasing");
          }
          previous_x = point.x;
        }
      };
  validate_curve(recipe.curves.master, "curves.master");
  validate_curve(recipe.curves.luma, "curves.luma");
  validate_curve(recipe.curves.red, "curves.red");
  validate_curve(recipe.curves.green, "curves.green");
  validate_curve(recipe.curves.blue, "curves.blue");

  std::set<std::string> identifiers;
  for (std::size_t index = 0; index < recipe.masks.size(); ++index) {
    const auto& mask_set = recipe.masks[index];
    const auto set_path = "masks[" + std::to_string(index) + "]";
    validate_identifier(&result, mask_set.id, set_path + ".id");
    if (!identifiers.insert(mask_set.id.value).second) {
      add_error(
          &result,
          RecipeErrorCode::DuplicateIdentifier,
          set_path + ".id",
          "mask and layer identifiers must be unique");
    }
    std::set<std::string> source_identifiers;
    for (std::size_t source_index = 0;
         source_index < mask_set.sources.size();
         ++source_index) {
      const auto& source = mask_set.sources[source_index];
      const auto path =
          set_path + ".sources[" + std::to_string(source_index) + "]";
      validate_identifier(&result, source.id, path + ".id");
      if (!source_identifiers.insert(source.id.value).second ||
          !identifiers.insert(source.id.value).second) {
        add_error(
            &result,
            RecipeErrorCode::DuplicateIdentifier,
            path + ".id",
            "mask source identifiers must be globally unique");
      }
      validate_finite_range(
          &result, source.opacity, 0.0, 1.0, path + ".opacity");
      validate_finite_range(
          &result, source.density, 0.0, 1.0, path + ".density");
      validate_finite_range(
          &result,
          source.geometry.feather,
          0.0,
          1.0,
          path + ".geometry.feather");
      validate_finite_range(
          &result,
          source.geometry.position,
          0.0,
          1.0,
          path + ".geometry.position");
      validate_finite_range(
          &result,
          source.geometry.center_x,
          0.0,
          1.0,
          path + ".geometry.center_x");
      validate_finite_range(
          &result,
          source.geometry.center_y,
          0.0,
          1.0,
          path + ".geometry.center_y");
      validate_finite_range(
          &result,
          source.geometry.radius_x,
          0.000001,
          1.0,
          path + ".geometry.radius_x");
      validate_finite_range(
          &result,
          source.geometry.radius_y,
          0.000001,
          1.0,
          path + ".geometry.radius_y");
      validate_finite_range(
          &result,
          source.geometry.rotation_degrees,
          -360.0,
          360.0,
          path + ".geometry.rotation_degrees");
      validate_finite_range(
          &result,
          source.geometry.range_min,
          0.0,
          1.0,
          path + ".geometry.range_min");
      validate_finite_range(
          &result,
          source.geometry.range_max,
          0.0,
          1.0,
          path + ".geometry.range_max");
      if (source.geometry.range_min > source.geometry.range_max) {
        add_error(
            &result,
            RecipeErrorCode::ValueOutOfRange,
            path + ".geometry.range",
            "mask range minimum must not exceed its maximum");
      }
      if (source.parent.has_value() &&
          source_identifiers.find(source.parent->value) ==
              source_identifiers.end()) {
        add_error(
            &result,
            RecipeErrorCode::MissingIdentifier,
            path + ".parent",
            "mask parent must reference an earlier source in the same set");
      }
      const bool payload_required =
          source.kind == MaskSourceKind::BrushRaster ||
          source.kind == MaskSourceKind::Lasso ||
          source.kind == MaskSourceKind::Subject ||
          source.kind == MaskSourceKind::Sky ||
          source.kind == MaskSourceKind::Object ||
          source.kind == MaskSourceKind::DepthRange ||
          source.kind == MaskSourceKind::RetouchPatch;
      if (payload_required && !source.payload_asset.has_value()) {
        add_error(
            &result,
            RecipeErrorCode::MissingIdentifier,
            path + ".payload_asset",
            "raster, vector, AI, depth, and retouch masks require immutable payload identity");
      }
      if (source.source_revision.has_value()) {
        validate_identifier(
            &result, *source.source_revision, path + ".source_revision");
      }
      if (source.payload_asset.has_value()) {
        validate_identifier(
            &result, *source.payload_asset, path + ".payload_asset");
      }
      if (source.output.has_value()) {
        validate_identifier(&result, *source.output, path + ".output");
      }
    }
  }
  std::set<std::string> layer_identifiers;
  for (std::size_t index = 0; index < recipe.layers.size(); ++index) {
    const auto& layer = recipe.layers[index];
    const auto path = "layers[" + std::to_string(index) + "]";
    validate_identifier(&result, layer.id, path + ".id");
    if (!identifiers.insert(layer.id.value).second) {
      add_error(
          &result,
          RecipeErrorCode::DuplicateIdentifier,
          path + ".id",
          "mask and layer identifiers must be unique");
    }
    layer_identifiers.insert(layer.id.value);
    if (!layer.effects.empty()) {
      std::string effect_error;
      if (!serialize_photo_effect_stack(layer.effects, &effect_error)) {
        add_error(&result, RecipeErrorCode::MalformedSerialization,
            path + ".effects", effect_error);
      }
    }
    validate_finite_range(&result, layer.opacity, 0.0, 1.0, path + ".opacity");
    validate_scene(&result, layer.scene, path + ".scene");
    validate_tone(&result, layer.tone, path + ".tone");
    validate_curve(layer.curves.master, path + ".curves.master");
    validate_curve(layer.curves.luma, path + ".curves.luma");
    validate_curve(layer.curves.red, path + ".curves.red");
    validate_curve(layer.curves.green, path + ".curves.green");
    validate_curve(layer.curves.blue, path + ".curves.blue");
    validate_look(&result, layer.look, path + ".look");
    validate_finite_range(
        &result, layer.detail.texture, -1.0, 1.0, path + ".detail.texture");
    validate_finite_range(
        &result, layer.detail.clarity, -1.0, 1.0, path + ".detail.clarity");
    validate_finite_range(
        &result, layer.detail.dehaze, -1.0, 1.0, path + ".detail.dehaze");
    validate_finite_range(
        &result, layer.detail.sharpening, 0.0, 1.0, path + ".detail.sharpening");
    validate_finite_range(
        &result, layer.detail.sharpen_radius, 0.1, 5.0, path + ".detail.sharpen_radius");
    validate_finite_range(
        &result, layer.detail.sharpen_detail, 0.0, 1.0, path + ".detail.sharpen_detail");
    validate_finite_range(
        &result, layer.detail.sharpen_masking, 0.0, 1.0, path + ".detail.sharpen_masking");
    validate_finite_range(
        &result, layer.detail.luminance_noise_reduction, 0.0, 1.0,
        path + ".detail.luminance_noise_reduction");
    validate_finite_range(
        &result, layer.detail.chroma_noise_reduction, 0.0, 1.0,
        path + ".detail.chroma_noise_reduction");
    validate_layer_geometry(&result, layer.geometry, path + ".geometry");
    const auto known_mask_target =
        [&](const AssetIdentifier& identifier) {
          for (const auto& mask_set : recipe.masks) {
            if (mask_set.id == identifier) {
              return true;
            }
            for (const auto& source : mask_set.sources) {
              if (source.id == identifier ||
                  (source.output.has_value() &&
                   *source.output == identifier)) {
                return true;
              }
            }
          }
          return false;
        };
    if (layer.mask.has_value() &&
        !known_mask_target(*layer.mask)) {
      add_error(
          &result,
          RecipeErrorCode::MissingIdentifier,
          path + ".mask",
          "layer references an unknown mask");
    }
    if (layer.parent.has_value() &&
        (layer.parent->value == layer.id.value ||
         layer_identifiers.find(layer.parent->value) ==
             layer_identifiers.end())) {
      add_error(
          &result,
          RecipeErrorCode::MissingIdentifier,
          path + ".parent",
          "layer references an unknown parent");
    }
    if (layer.kind != LayerKind::Adjustment &&
        layer.kind != LayerKind::Group &&
        !layer.content_asset.has_value()) {
      add_error(
          &result,
          RecipeErrorCode::MissingIdentifier,
          path + ".content_asset",
          "pixel, text, and fill layers require immutable content identity");
    }
  }

  validate_finite_range(&result, recipe.detail.texture, -1.0, 1.0, "detail.texture");
  validate_finite_range(&result, recipe.detail.clarity, -1.0, 1.0, "detail.clarity");
  validate_finite_range(&result, recipe.detail.dehaze, -1.0, 1.0, "detail.dehaze");
  validate_finite_range(&result, recipe.detail.sharpening, 0.0, 1.0, "detail.sharpening");
  validate_finite_range(
      &result, recipe.detail.sharpen_radius, 0.1, 5.0, "detail.sharpen_radius");
  validate_finite_range(
      &result, recipe.detail.sharpen_detail, 0.0, 1.0, "detail.sharpen_detail");
  validate_finite_range(
      &result, recipe.detail.sharpen_masking, 0.0, 1.0, "detail.sharpen_masking");
  validate_finite_range(
      &result,
      recipe.detail.luminance_noise_reduction,
      0.0,
      1.0,
      "detail.luminance_noise_reduction");
  validate_finite_range(
      &result,
      recipe.detail.chroma_noise_reduction,
      0.0,
      1.0,
      "detail.chroma_noise_reduction");
  validate_finite_range(&result, recipe.geometry.crop_x, 0.0, 1.0, "geometry.crop_x");
  validate_finite_range(&result, recipe.geometry.crop_y, 0.0, 1.0, "geometry.crop_y");
  validate_finite_range(&result, recipe.geometry.crop_width, 0.000001, 1.0, "geometry.crop_width");
  validate_finite_range(&result, recipe.geometry.crop_height, 0.000001, 1.0, "geometry.crop_height");
  validate_finite_range(
      &result,
      recipe.geometry.rotation_degrees,
      -360.0,
      360.0,
      "geometry.rotation_degrees");
  validate_finite_range(&result, recipe.geometry.lens_distortion, -1.0, 1.0, "geometry.lens_distortion");
  validate_finite_range(&result, recipe.geometry.lens_vignette, -1.0, 1.0, "geometry.lens_vignette");
  validate_finite_range(&result, recipe.geometry.chromatic_aberration, -1.0, 1.0, "geometry.chromatic_aberration");
  validate_finite_range(&result, recipe.geometry.perspective_vertical, -1.0, 1.0, "geometry.perspective_vertical");
  validate_finite_range(&result, recipe.geometry.perspective_horizontal, -1.0, 1.0, "geometry.perspective_horizontal");
  validate_finite_range(&result, recipe.geometry.scale, 0.01, 8.0, "geometry.scale");
  validate_finite_range(&result, recipe.geometry.skew_x, -1.0, 1.0, "geometry.skew_x");
  validate_finite_range(&result, recipe.geometry.skew_y, -1.0, 1.0, "geometry.skew_y");
  validate_finite_range(&result, recipe.geometry.crop_zoom, 0.01, 8.0, "geometry.crop_zoom");
  validate_finite_range(&result, recipe.geometry.liquify_center_x, 0.0, 1.0, "geometry.liquify_center_x");
  validate_finite_range(&result, recipe.geometry.liquify_center_y, 0.0, 1.0, "geometry.liquify_center_y");
  validate_finite_range(&result, recipe.geometry.liquify_radius, 0.0, 1.0, "geometry.liquify_radius");
  validate_finite_range(&result, recipe.geometry.liquify_strength, -1.0, 1.0, "geometry.liquify_strength");
  if (recipe.geometry.crop_x + recipe.geometry.crop_width > 1.0000001 ||
      recipe.geometry.crop_y + recipe.geometry.crop_height > 1.0000001) {
    add_error(
        &result,
        RecipeErrorCode::ValueOutOfRange,
        "geometry.crop",
        "crop must remain inside normalized source bounds");
  }
  validate_finite_range(
      &result,
      recipe.output.peak_luminance_nits,
      1.0,
      10000.0,
      "output.peak_luminance_nits");
  if (static_cast<std::uint8_t>(recipe.output.export_color_space) > 5) {
    add_error(&result, RecipeErrorCode::ValueOutOfRange,
        "output.export_color_space", "unsupported export color space");
  }
  validate_finite_range(
      &result,
      recipe.output.black_luminance_nits,
      0.0,
      10.0,
      "output.black_luminance_nits");
  validate_finite_range(
      &result, recipe.output.source_black_point, 0.0, 1.0,
      "output.source_black_point");
  validate_finite_range(
      &result, recipe.output.output_black_point, 0.0, 1.0,
      "output.output_black_point");
  if (recipe.output.device_icc_profile.has_value()) {
    validate_identifier(
        &result,
        *recipe.output.device_icc_profile,
        "output.device_icc_profile");
  }
  if (recipe.proof.mode == ProofMode::Icc && !recipe.proof.profile.has_value()) {
    add_error(
        &result,
        RecipeErrorCode::MissingProfile,
        "proof.profile",
        "ICC proof mode requires an identified profile asset");
  }
  if (recipe.proof.profile.has_value()) {
    validate_identifier(&result, *recipe.proof.profile, "proof.profile");
  }
  return result;
}

EditRecipeV13 normalize_edit_recipe_v13(const EditRecipeV13& recipe) {
  auto normalized = recipe;
  const auto normalize_zero = [](double* value) {
    if (*value == 0.0) {
      *value = 0.0;
    }
  };
  const auto normalize_optional_zero =
      [&](std::optional<double>* value) {
        if (value->has_value()) {
          normalize_zero(&value->value());
        }
      };
  const auto normalize_scene = [&](SceneRecipeV13* scene) {
    normalize_zero(&scene->exposure_ev);
  };
  const auto normalize_tone = [&](ToneRecipeV13* tone) {
    normalize_zero(&tone->contrast);
    normalize_zero(&tone->highlights);
    normalize_zero(&tone->shadows);
    normalize_zero(&tone->whites);
    normalize_zero(&tone->blacks);
    normalize_zero(&tone->brightness);
    normalize_zero(&tone->hdr_local_tone_mapping);
    normalize_zero(&tone->filmic_shoulder);
    normalize_zero(&tone->filmic_toe);
    normalize_zero(&tone->adaptive_contrast);
    normalize_zero(&tone->levels_black_point);
    normalize_zero(&tone->levels_gamma);
    normalize_zero(&tone->levels_white_point);
    normalize_zero(&tone->zone_deep_shadows);
    normalize_zero(&tone->zone_shadows);
    normalize_zero(&tone->zone_midtones);
    normalize_zero(&tone->zone_lights);
    normalize_zero(&tone->zone_highlights);
  };
  const auto normalize_look = [&](LookRecipeV13* look) {
    normalize_zero(&look->temperature);
    normalize_zero(&look->tint);
    normalize_zero(&look->vibrance);
    normalize_zero(&look->saturation);
    normalize_zero(&look->hue_shift);
    normalize_zero(&look->saturation_shift);
    normalize_zero(&look->colorize_hue);
    normalize_zero(&look->colorize_strength);
    for (auto& channel : look->mixer_channels) {
      normalize_zero(&channel.hue);
      normalize_zero(&channel.saturation);
      normalize_zero(&channel.luminance);
      normalize_zero(&channel.selective_cyan);
      normalize_zero(&channel.selective_magenta);
      normalize_zero(&channel.selective_yellow);
      normalize_zero(&channel.selective_black);
    }
    auto& rgb = look->rgb_channel_mixer;
    normalize_zero(&rgb.red_from_red);
    normalize_zero(&rgb.red_from_green);
    normalize_zero(&rgb.red_from_blue);
    normalize_zero(&rgb.green_from_red);
    normalize_zero(&rgb.green_from_green);
    normalize_zero(&rgb.green_from_blue);
    normalize_zero(&rgb.blue_from_red);
    normalize_zero(&rgb.blue_from_green);
    normalize_zero(&rgb.blue_from_blue);
    for (auto& value : look->black_and_white_mixer) {
      normalize_zero(&value);
    }
    normalize_zero(&look->color_replacement_source_hue);
    normalize_zero(&look->color_replacement_target_hue);
    normalize_zero(&look->color_replacement_range);
    normalize_zero(&look->color_replacement_softness);
    normalize_zero(&look->color_replacement_strength);
    normalize_zero(&look->skin_tone_line_hue);
    normalize_zero(&look->skin_tone_protection_range);
    normalize_zero(&look->skin_tone_protection_strength);
    normalize_zero(&look->color_harmony_base_hue);
    normalize_zero(&look->color_harmony_strength);
    const auto normalize_grade = [&](ColorGradingZoneV13* grade) {
      normalize_zero(&grade->hue);
      normalize_zero(&grade->saturation);
      normalize_zero(&grade->luminance);
    };
    normalize_grade(&look->grading_shadows);
    normalize_grade(&look->grading_midtones);
    normalize_grade(&look->grading_highlights);
    normalize_zero(&look->grading_amount);
    normalize_zero(&look->grading_balance);
    for (auto& lut : look->luts) {
      normalize_zero(&lut.strength);
      for (auto& value : lut.domain_min) {
        normalize_zero(&value);
      }
      for (auto& value : lut.domain_max) {
        normalize_zero(&value);
      }
    }
  };
  const auto normalize_detail = [&](DetailRecipeV13* detail) {
    normalize_zero(&detail->texture);
    normalize_zero(&detail->clarity);
    normalize_zero(&detail->dehaze);
    normalize_zero(&detail->sharpening);
    normalize_zero(&detail->sharpen_radius);
    normalize_zero(&detail->sharpen_detail);
    normalize_zero(&detail->sharpen_masking);
    normalize_zero(&detail->luminance_noise_reduction);
    normalize_zero(&detail->chroma_noise_reduction);
  };
  const auto normalize_geometry = [&](GeometryRecipeV13* geometry) {
    normalize_zero(&geometry->crop_x);
    normalize_zero(&geometry->crop_y);
    normalize_zero(&geometry->crop_width);
    normalize_zero(&geometry->crop_height);
    normalize_zero(&geometry->rotation_degrees);
    normalize_zero(&geometry->lens_distortion);
    normalize_zero(&geometry->lens_vignette);
    normalize_zero(&geometry->chromatic_aberration);
    normalize_zero(&geometry->perspective_vertical);
    normalize_zero(&geometry->perspective_horizontal);
    normalize_zero(&geometry->scale);
    normalize_zero(&geometry->skew_x);
    normalize_zero(&geometry->skew_y);
    normalize_zero(&geometry->crop_zoom);
    normalize_zero(&geometry->liquify_center_x);
    normalize_zero(&geometry->liquify_center_y);
    normalize_zero(&geometry->liquify_radius);
    normalize_zero(&geometry->liquify_strength);
  };
  normalize_optional_zero(&normalized.input.white_balance_temperature_kelvin);
  normalize_optional_zero(&normalized.input.white_balance_tint);
  normalize_optional_zero(&normalized.input.black_level_override);
  normalize_optional_zero(&normalized.input.white_level_override);
  normalize_zero(&normalized.input.hot_dead_pixel_cleanup);
  normalize_zero(&normalized.input.false_color_suppression);
  normalize_zero(&normalized.input.calibration_hue);
  normalize_zero(&normalized.input.calibration_saturation);
  normalize_zero(&normalized.input.red_primary_hue);
  normalize_zero(&normalized.input.red_primary_saturation);
  normalize_zero(&normalized.input.green_primary_hue);
  normalize_zero(&normalized.input.green_primary_saturation);
  normalize_zero(&normalized.input.blue_primary_hue);
  normalize_zero(&normalized.input.blue_primary_saturation);
  normalize_scene(&normalized.scene);
  normalize_tone(&normalized.tone);
  const auto normalize_curve =
      [&](std::vector<CurvePointV13>* points) {
        for (auto& point : *points) {
          normalize_zero(&point.x);
          normalize_zero(&point.y);
        }
      };
  normalize_curve(&normalized.curves.master);
  normalize_curve(&normalized.curves.luma);
  normalize_curve(&normalized.curves.red);
  normalize_curve(&normalized.curves.green);
  normalize_curve(&normalized.curves.blue);
  normalize_look(&normalized.look);
  for (auto& mask_set : normalized.masks) {
    for (auto& source : mask_set.sources) {
      normalize_zero(&source.opacity);
      normalize_zero(&source.density);
      normalize_zero(&source.geometry.feather);
      normalize_zero(&source.geometry.position);
      normalize_zero(&source.geometry.center_x);
      normalize_zero(&source.geometry.center_y);
      normalize_zero(&source.geometry.radius_x);
      normalize_zero(&source.geometry.radius_y);
      normalize_zero(&source.geometry.rotation_degrees);
      normalize_zero(&source.geometry.range_min);
      normalize_zero(&source.geometry.range_max);
    }
  }
  for (auto& layer : normalized.layers) {
    normalize_zero(&layer.opacity);
    normalize_scene(&layer.scene);
    normalize_tone(&layer.tone);
    normalize_curve(&layer.curves.master);
    normalize_curve(&layer.curves.luma);
    normalize_curve(&layer.curves.red);
    normalize_curve(&layer.curves.green);
    normalize_curve(&layer.curves.blue);
    normalize_look(&layer.look);
    normalize_detail(&layer.detail);
    normalize_geometry(&layer.geometry);
  }
  normalize_detail(&normalized.detail);
  normalize_geometry(&normalized.geometry);
  normalize_zero(&normalized.output.peak_luminance_nits);
  normalize_zero(&normalized.output.black_luminance_nits);
  normalize_zero(&normalized.output.source_black_point);
  normalize_zero(&normalized.output.output_black_point);
  return normalized;
}

std::string serialize_edit_recipe_v13(const EditRecipeV13& source_recipe) {
  const auto recipe = normalize_edit_recipe_v13(source_recipe);
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream << std::setprecision(std::numeric_limits<double>::max_digits10);
  // Keep established sRGB payload/cache bytes stable. Only a non-default
  // delivery space needs the additive v13.3 extension.
  const bool has_effects = std::any_of(recipe.layers.begin(), recipe.layers.end(),
      [](const auto& layer) { return !layer.effects.empty(); });
  write_string(stream, std::string(has_effects ? kEffectsRecipeMagic :
      recipe.output.export_color_space == ExportColorSpaceV13::Srgb ? kRecipeMagic : kExportSpaceRecipeMagic));
  stream << recipe.recipe_version << ' ';
  write_string(stream, recipe.recipe_id.value);
  write_string(stream, recipe.input.source.value);
  write_enum(stream, recipe.input.decoder);
  write_string(stream, recipe.input.decoder_provenance);
  write_enum(stream, recipe.input.camera_profile_mode);
  write_optional_identifier(stream, recipe.input.camera_profile);
  write_optional_double(stream, recipe.input.white_balance_temperature_kelvin);
  write_optional_double(stream, recipe.input.white_balance_tint);
  write_optional_double(stream, recipe.input.black_level_override);
  write_optional_double(stream, recipe.input.white_level_override);
  write_bool(stream, recipe.input.input_transform_enabled);
  write_enum(stream, recipe.input.highlight_reconstruction);
  write_enum(stream, recipe.input.chromatic_adaptation);
  write_enum(stream, recipe.input.demosaic_quality);
  stream << recipe.input.hot_dead_pixel_cleanup << ' '
         << recipe.input.false_color_suppression << ' ';
  write_bool(stream, recipe.input.highlight_recovery_enabled);
  write_enum(stream, recipe.input.camera_matching_look);
  stream << recipe.input.calibration_hue << ' '
         << recipe.input.calibration_saturation << ' '
         << recipe.input.red_primary_hue << ' '
         << recipe.input.red_primary_saturation << ' '
         << recipe.input.green_primary_hue << ' '
         << recipe.input.green_primary_saturation << ' '
         << recipe.input.blue_primary_hue << ' '
         << recipe.input.blue_primary_saturation << ' ';
  write_scene(stream, recipe.scene);
  write_tone(stream, recipe.tone);
  write_enum(stream, recipe.curves.interpolation);
  write_curve(stream, recipe.curves.master);
  write_curve(stream, recipe.curves.luma);
  write_curve(stream, recipe.curves.red);
  write_curve(stream, recipe.curves.green);
  write_curve(stream, recipe.curves.blue);
  write_look(stream, recipe.look);
  stream << recipe.masks.size() << ' ';
  for (const auto& mask_set : recipe.masks) {
    write_string(stream, mask_set.id.value);
    write_bool(stream, mask_set.enabled);
    stream << mask_set.sources.size() << ' ';
    for (const auto& source : mask_set.sources) {
      write_string(stream, source.id.value);
      write_enum(stream, source.kind);
      write_enum(stream, source.operation);
      write_optional_identifier(stream, source.parent);
      write_bool(stream, source.enabled);
      write_enum(stream, source.blend_mode);
      stream << source.opacity << ' ' << source.density << ' ';
      write_bool(stream, source.inverted);
      write_optional_identifier(stream, source.output);
      write_bool(stream, source.reusable_output);
      write_enum(stream, source.coordinate_space);
      write_enum(stream, source.sampling);
      write_optional_identifier(stream, source.source_revision);
      write_optional_identifier(stream, source.payload_asset);
      write_mask_geometry(stream, source.geometry);
    }
  }
  stream << recipe.layers.size() << ' ';
  for (const auto& layer : recipe.layers) {
    write_string(stream, layer.id.value);
    write_enum(stream, layer.kind);
    write_bool(stream, layer.enabled);
    stream << layer.opacity << ' ';
    write_enum(stream, layer.blend_mode);
    write_optional_identifier(stream, layer.mask);
    write_optional_identifier(stream, layer.parent);
    write_optional_identifier(stream, layer.content_asset);
    write_scene(stream, layer.scene);
    write_tone(stream, layer.tone);
    write_enum(stream, layer.curves.interpolation);
    write_curve(stream, layer.curves.master);
    write_curve(stream, layer.curves.luma);
    write_curve(stream, layer.curves.red);
    write_curve(stream, layer.curves.green);
    write_curve(stream, layer.curves.blue);
    write_look(stream, layer.look);
    write_detail(stream, layer.detail);
    write_geometry(stream, layer.geometry);
    if (has_effects) {
      const auto payload = serialize_photo_effect_stack(layer.effects);
      if (!payload) return {};
      write_string(stream, *payload);
    }
  }
  write_detail(stream, recipe.detail);
  stream << recipe.geometry.crop_x << ' ' << recipe.geometry.crop_y << ' '
         << recipe.geometry.crop_width << ' ' << recipe.geometry.crop_height << ' '
         << recipe.geometry.rotation_degrees << ' '
         << recipe.geometry.lens_distortion << ' '
         << recipe.geometry.lens_vignette << ' '
         << recipe.geometry.chromatic_aberration << ' '
         << recipe.geometry.perspective_vertical << ' '
         << recipe.geometry.perspective_horizontal << ' '
         << recipe.geometry.scale << ' ' << recipe.geometry.skew_x << ' '
         << recipe.geometry.skew_y << ' ' << recipe.geometry.crop_zoom << ' ';
  write_enum(stream, recipe.geometry.upright_mode);
  stream << recipe.geometry.liquify_center_x << ' '
         << recipe.geometry.liquify_center_y << ' '
         << recipe.geometry.liquify_radius << ' '
         << recipe.geometry.liquify_strength << ' ';
  write_bool(stream, recipe.geometry.content_aware_crop_fill);
  write_enum(stream, recipe.output.rendering_target);
  write_enum(stream, recipe.output.renderer_transition_look);
  write_optional_identifier(stream, recipe.output.device_icc_profile);
  stream << recipe.output.peak_luminance_nits << ' '
         << recipe.output.black_luminance_nits << ' ';
  write_enum(stream, recipe.output.render_intent);
  write_bool(stream, recipe.output.gamut_mapping_enabled);
  write_enum(stream, recipe.output.gamut_mapping_policy);
  write_bool(stream, recipe.output.black_point_compensation);
  stream << recipe.output.source_black_point << ' '
         << recipe.output.output_black_point << ' ';
  write_enum(stream, recipe.proof.mode);
  write_optional_identifier(stream, recipe.proof.profile);
  write_bool(stream, recipe.proof.simulate_paper);
  write_bool(stream, recipe.proof.simulate_ink_black);
  write_bool(stream, recipe.proof.black_point_compensation);
  if (has_effects || recipe.output.export_color_space != ExportColorSpaceV13::Srgb) {
    write_enum(stream, recipe.output.export_color_space);
  }
  return stream.str();
}

std::optional<EditRecipeV13> deserialize_edit_recipe_v13(
    std::string_view serialized,
    RecipeError* error) {
  std::istringstream stream{std::string(serialized)};
  stream.imbue(std::locale::classic());
  EditRecipeV13 recipe;
  std::string magic;
  std::size_t mask_count = 0;
  std::size_t layer_count = 0;
  if (!read_string(stream, &magic) ||
      (magic != kEffectsRecipeMagic && magic != kRecipeMagic && magic != kExportSpaceRecipeMagic && magic != kPreviousRecipeMagic &&
       magic != kLegacyRecipeMagic)) {
    set_decode_error(error, "invalid recipe serialization identity");
    return std::nullopt;
  }
  const auto legacy_input_layout = magic == kLegacyRecipeMagic;
  const auto has_renderer_transition_look = magic == kRecipeMagic || magic == kExportSpaceRecipeMagic || magic == kEffectsRecipeMagic;
  if (!(stream >> recipe.recipe_version) ||
      !read_string(stream, &recipe.recipe_id.value) ||
      !read_string(stream, &recipe.input.source.value) ||
      !read_enum(stream, &recipe.input.decoder, 1) ||
      !read_string(stream, &recipe.input.decoder_provenance) ||
      !read_enum(stream, &recipe.input.camera_profile_mode, 3) ||
      !read_optional_identifier(stream, &recipe.input.camera_profile) ||
      !read_optional_double(stream, &recipe.input.white_balance_temperature_kelvin) ||
      !read_optional_double(stream, &recipe.input.white_balance_tint) ||
      !read_optional_double(stream, &recipe.input.black_level_override) ||
      !read_optional_double(stream, &recipe.input.white_level_override) ||
      !read_bool(stream, &recipe.input.input_transform_enabled) ||
      !read_enum(stream, &recipe.input.highlight_reconstruction, 2) ||
      !read_enum(stream, &recipe.input.chromatic_adaptation, 1) ||
      !read_enum(stream, &recipe.input.demosaic_quality, 2)) {
    set_decode_error(error, "invalid recipe header or input data");
    return std::nullopt;
  }
  if (legacy_input_layout) {
    double decode_boost = 0.0;
    double decode_shadow_boost = 1.0;
    double raw_detail = 0.0;
    double chroma_noise_reduction = 0.0;
    double moire_reduction = 0.0;
    bool input_gamut_mapping_enabled = false;
    bool lens_correction_enabled = true;
    if (!(stream >> decode_boost >> decode_shadow_boost >> raw_detail >>
          chroma_noise_reduction >> moire_reduction >>
          recipe.input.hot_dead_pixel_cleanup >>
          recipe.input.false_color_suppression) ||
        !read_bool(stream, &recipe.input.highlight_recovery_enabled) ||
        !read_bool(stream, &input_gamut_mapping_enabled) ||
        !read_bool(stream, &lens_correction_enabled)) {
      set_decode_error(error, "invalid legacy v13 RAW input data");
      return std::nullopt;
    }
    (void)lens_correction_enabled;
    if (decode_boost != 0.0 || decode_shadow_boost != 1.0 ||
        raw_detail != 0.0 || chroma_noise_reduction != 0.0 ||
        moire_reduction != 0.0 || input_gamut_mapping_enabled) {
      set_decode_error(
          error,
          "legacy v13 recipe contains retired RAW controls with no pixel semantics");
      return std::nullopt;
    }
  } else if (!(stream >> recipe.input.hot_dead_pixel_cleanup >>
               recipe.input.false_color_suppression) ||
             !read_bool(stream, &recipe.input.highlight_recovery_enabled)) {
    set_decode_error(error, "invalid v13.1 RAW input data");
    return std::nullopt;
  }
  if (!read_enum(stream, &recipe.input.camera_matching_look, 5) ||
      !(stream >> recipe.input.calibration_hue >>
        recipe.input.calibration_saturation >>
        recipe.input.red_primary_hue >>
        recipe.input.red_primary_saturation >>
        recipe.input.green_primary_hue >>
        recipe.input.green_primary_saturation >>
        recipe.input.blue_primary_hue >>
        recipe.input.blue_primary_saturation) ||
      !read_scene(stream, &recipe.scene) ||
      !read_tone(stream, &recipe.tone) ||
      !read_enum(stream, &recipe.curves.interpolation, 1) ||
      !read_curve(stream, &recipe.curves.master) ||
      !read_curve(stream, &recipe.curves.luma) ||
      !read_curve(stream, &recipe.curves.red) ||
      !read_curve(stream, &recipe.curves.green) ||
      !read_curve(stream, &recipe.curves.blue)) {
    set_decode_error(error, "invalid recipe section data");
    return std::nullopt;
  }
  if (!read_look(stream, &recipe.look) ||
      !(stream >> mask_count) || mask_count > 65536U) {
    set_decode_error(error, "invalid look or mask data");
    return std::nullopt;
  }
  recipe.masks.resize(mask_count);
  for (auto& mask_set : recipe.masks) {
    std::size_t source_count = 0;
    if (!read_string(stream, &mask_set.id.value) ||
        !read_bool(stream, &mask_set.enabled) ||
        !(stream >> source_count) || source_count > 65536U) {
      set_decode_error(error, "invalid mask data");
      return std::nullopt;
    }
    mask_set.sources.resize(source_count);
    for (auto& source : mask_set.sources) {
      if (!read_string(stream, &source.id.value) ||
          !read_enum(stream, &source.kind, 10) ||
          !read_enum(stream, &source.operation, 2) ||
          !read_optional_identifier(stream, &source.parent) ||
          !read_bool(stream, &source.enabled) ||
          !read_enum(stream, &source.blend_mode, 6) ||
          !(stream >> source.opacity >> source.density) ||
          !read_bool(stream, &source.inverted) ||
          !read_optional_identifier(stream, &source.output) ||
          !read_bool(stream, &source.reusable_output) ||
          !read_enum(stream, &source.coordinate_space, 1) ||
          !read_enum(stream, &source.sampling, 2) ||
          !read_optional_identifier(stream, &source.source_revision) ||
          !read_optional_identifier(stream, &source.payload_asset) ||
          !read_mask_geometry(stream, &source.geometry)) {
        set_decode_error(error, "invalid mask source data");
        return std::nullopt;
      }
    }
  }
  if (!(stream >> layer_count) || layer_count > 65536U) {
    set_decode_error(error, "invalid layer count");
    return std::nullopt;
  }
  recipe.layers.resize(layer_count);
  for (auto& layer : recipe.layers) {
    if (!read_string(stream, &layer.id.value) ||
        !read_enum(stream, &layer.kind, 4) ||
        !read_bool(stream, &layer.enabled) ||
        !(stream >> layer.opacity) ||
        !read_enum(stream, &layer.blend_mode, 6) ||
        !read_optional_identifier(stream, &layer.mask) ||
        !read_optional_identifier(stream, &layer.parent) ||
        !read_optional_identifier(stream, &layer.content_asset) ||
        !read_scene(stream, &layer.scene) ||
        !read_tone(stream, &layer.tone) ||
        !read_enum(stream, &layer.curves.interpolation, 1) ||
        !read_curve(stream, &layer.curves.master) ||
        !read_curve(stream, &layer.curves.luma) ||
        !read_curve(stream, &layer.curves.red) ||
        !read_curve(stream, &layer.curves.green) ||
        !read_curve(stream, &layer.curves.blue) ||
        !read_look(stream, &layer.look) ||
        !read_detail(stream, &layer.detail) ||
        !read_geometry(stream, &layer.geometry)) {
      set_decode_error(error, "invalid layer data");
      return std::nullopt;
    }
    if (magic == kEffectsRecipeMagic) {
      std::string payload;
      if (!read_string(stream, &payload)) {
        set_decode_error(error, "missing layer effect envelope");
        return std::nullopt;
      }
      std::string effect_error;
      auto effects = deserialize_photo_effect_stack(payload, &effect_error);
      if (!effects) {
        set_decode_error(error, effect_error);
        return std::nullopt;
      }
      layer.effects = std::move(*effects);
    }
  }
  if (!read_detail(stream, &recipe.detail) ||
      !(stream >> recipe.geometry.crop_x >> recipe.geometry.crop_y >>
        recipe.geometry.crop_width >> recipe.geometry.crop_height >>
        recipe.geometry.rotation_degrees >> recipe.geometry.lens_distortion >>
        recipe.geometry.lens_vignette >>
        recipe.geometry.chromatic_aberration >>
        recipe.geometry.perspective_vertical >>
        recipe.geometry.perspective_horizontal >> recipe.geometry.scale >>
        recipe.geometry.skew_x >> recipe.geometry.skew_y >>
        recipe.geometry.crop_zoom) ||
      !read_enum(stream, &recipe.geometry.upright_mode, 3) ||
      !(stream >> recipe.geometry.liquify_center_x >>
        recipe.geometry.liquify_center_y >>
        recipe.geometry.liquify_radius >>
        recipe.geometry.liquify_strength) ||
      !read_bool(stream, &recipe.geometry.content_aware_crop_fill) ||
      !read_enum(stream, &recipe.output.rendering_target, 4) ||
      (has_renderer_transition_look &&
       !read_enum(stream, &recipe.output.renderer_transition_look, 1)) ||
      !read_optional_identifier(stream, &recipe.output.device_icc_profile) ||
      !(stream >> recipe.output.peak_luminance_nits >>
        recipe.output.black_luminance_nits) ||
      !read_enum(stream, &recipe.output.render_intent, 3) ||
      !read_bool(stream, &recipe.output.gamut_mapping_enabled) ||
      !read_enum(stream, &recipe.output.gamut_mapping_policy, 2) ||
      !read_bool(stream, &recipe.output.black_point_compensation) ||
      !(stream >> recipe.output.source_black_point >>
        recipe.output.output_black_point) ||
      !read_enum(stream, &recipe.proof.mode, 1) ||
      !read_optional_identifier(stream, &recipe.proof.profile) ||
      !read_bool(stream, &recipe.proof.simulate_paper) ||
      !read_bool(stream, &recipe.proof.simulate_ink_black) ||
      !read_bool(stream, &recipe.proof.black_point_compensation)) {
    set_decode_error(error, "invalid detail, geometry, output, or proof data");
    return std::nullopt;
  }
  if ((magic == kExportSpaceRecipeMagic || magic == kEffectsRecipeMagic) &&
      !read_enum(stream, &recipe.output.export_color_space, 5)) {
    set_decode_error(error, "invalid export color space");
    return std::nullopt;
  }
  stream >> std::ws;
  if (!stream.eof()) {
    set_decode_error(error, "unexpected trailing recipe data");
    return std::nullopt;
  }
  const auto validation = validate_edit_recipe_v13(recipe);
  if (!validation.ok()) {
    if (error != nullptr) {
      *error = validation.errors.front();
    }
    return std::nullopt;
  }
  return recipe;
}

std::string edit_recipe_v13_cache_key(const EditRecipeV13& recipe) {
  const auto serialized = serialize_edit_recipe_v13(recipe);
  std::uint64_t hash = 14695981039346656037ULL;
  for (const char character : serialized) {
    hash ^= static_cast<std::uint64_t>(
        static_cast<unsigned char>(character));
    hash *= 1099511628211ULL;
  }
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream << "edit-recipe-v13-fnv1a64-" << std::hex << std::setfill('0')
         << std::setw(16) << hash;
  return stream.str();
}

}  // namespace photo_editor::pipeline
