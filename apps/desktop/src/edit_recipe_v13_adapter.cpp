#include "edit_recipe_v13_adapter.h"
#include "linked_image_source.h"
#include "photo_layer_effect_codec.h"

#include <QCryptographicHash>
#include <QJsonDocument>

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <string_view>
#include <utility>

namespace photo_editor::desktop {
namespace {

using photo_editor::pipeline::AssetIdentifier;
using photo_editor::pipeline::CameraProfileMode;
using photo_editor::pipeline::CurvePointV13;
using photo_editor::pipeline::EditRecipeV13;
using photo_editor::pipeline::LayerRecipeV13;
using photo_editor::pipeline::LayerKind;
using photo_editor::pipeline::MaskSetRecipeV13;
using photo_editor::pipeline::MaskCombineOperation;
using photo_editor::pipeline::MaskCoordinateSpace;
using photo_editor::pipeline::MaskSamplingMode;
using photo_editor::pipeline::MaskSourceKind;
using photo_editor::pipeline::MaskSourceRecipeV13;
using photo_editor::pipeline::ProofMode;
using photo_editor::pipeline::RawDecoder;

constexpr double kNeutralTolerance = 1.0e-9;

bool active(double value) {
  return std::abs(value) > kNeutralTolerance;
}

double signed_percent(double value) {
  return std::clamp(value / 100.0, -1.0, 1.0);
}

std::string utf8(const QString& value) {
  return value.toUtf8().toStdString();
}

std::optional<AssetIdentifier> profile_identifier(
    const IccProfileAssetState& profile) {
  const auto asset_id = profile.asset_id.trimmed();
  if (!asset_id.isEmpty()) {
    return AssetIdentifier{utf8(asset_id)};
  }
  const auto hash = profile.payload_sha256.trimmed();
  if (!hash.isEmpty()) {
    return AssetIdentifier{"sha256:" + utf8(hash)};
  }
  return std::nullopt;
}

std::optional<AssetIdentifier> profile_identifier(
    const DcpProfileAssetState& profile) {
  const auto asset_id = profile.asset_id.trimmed();
  if (!asset_id.isEmpty()) {
    return AssetIdentifier{utf8(asset_id)};
  }
  const auto hash = profile.payload_sha256.trimmed();
  if (!hash.isEmpty()) {
    return AssetIdentifier{"sha256:" + utf8(hash)};
  }
  return std::nullopt;
}

std::optional<AssetIdentifier> proof_profile_identifier(
    const EditRecipeState& state) {
  if (const auto custom_profile =
          profile_identifier(state.proof_icc_profile_asset);
      custom_profile.has_value()) {
    return custom_profile;
  }
  if (!state.soft_proof_enabled) {
    return std::nullopt;
  }
  auto builtin_name =
      normalized_output_color_space_name(state.soft_proof_profile)
          .trimmed()
          .toLower();
  builtin_name.replace(QLatin1Char(' '), QLatin1Char('-'));
  return AssetIdentifier{
      "builtin:icc:" + utf8(builtin_name)};
}

void add_error(
    DesktopRecipeV13AdapterResult* result,
    DesktopRecipeAdapterErrorCode code,
    std::string path,
    std::string message) {
  result->errors.push_back(DesktopRecipeAdapterError{
      .code = code,
      .path = std::move(path),
      .message = std::move(message),
  });
}

bool any_mixer_active(const EditRecipeState& state) {
  const auto channel_active = [](const EditRecipeState::MixerChannel& channel) {
    return active(channel.hue) || active(channel.saturation) ||
           active(channel.luminance) || active(channel.selective_cyan) ||
           active(channel.selective_magenta) ||
           active(channel.selective_yellow) || active(channel.selective_black);
  };
  return std::any_of(
      state.mixer_channels.begin(),
      state.mixer_channels.end(),
      channel_active);
}

bool append_curve(
    const QVariantList& points,
    std::vector<CurvePointV13>* output) {
  output->reserve(static_cast<std::size_t>(points.size()));
  for (const auto& value : points) {
    if (!value.canConvert<QVariantMap>()) {
      return false;
    }
    const auto point = value.toMap();
    bool x_ok = false;
    bool y_ok = false;
    const auto x = point.value(QStringLiteral("x")).toDouble(&x_ok);
    const auto y = point.value(QStringLiteral("y")).toDouble(&y_ok);
    if (!x_ok || !y_ok) {
      return false;
    }
    output->push_back(CurvePointV13{.x = x, .y = y});
  }
  return true;
}

std::optional<MaskSourceKind> mask_source_kind(const QString& value) {
  const auto normalized = value.trimmed().toLower();
  if (normalized == QStringLiteral("linear")) {
    return MaskSourceKind::LinearGradient;
  }
  if (normalized == QStringLiteral("radial") ||
      normalized == QStringLiteral("ellipse")) {
    return MaskSourceKind::RadialGradient;
  }
  if (normalized == QStringLiteral("brush")) {
    return MaskSourceKind::BrushRaster;
  }
  if (normalized == QStringLiteral("lasso") ||
      normalized == QStringLiteral("polygon")) {
    return MaskSourceKind::Lasso;
  }
  if (normalized == QStringLiteral("subject")) {
    return MaskSourceKind::Subject;
  }
  if (normalized == QStringLiteral("sky")) {
    return MaskSourceKind::Sky;
  }
  // Background is already an inverted foreground matte in the desktop state.
  // Carry its stored alpha as a generic semantic raster; never invert it again.
  // Imported alpha images use the same immutable raster payload transport.
  // Object is the canonical generic raster kind; it does not invoke inference.
  if (normalized == QStringLiteral("object") || normalized == QStringLiteral("background") ||
      normalized == QStringLiteral("alpha")) {
    return MaskSourceKind::Object;
  }
  if (normalized == QStringLiteral("luminance")) {
    return MaskSourceKind::LuminanceRange;
  }
  if (normalized == QStringLiteral("color")) {
    return MaskSourceKind::ColorRange;
  }
  if (normalized == QStringLiteral("depth")) {
    return MaskSourceKind::DepthRange;
  }
  if (normalized == QStringLiteral("retouch") ||
      normalized == QStringLiteral("retouch-patch")) {
    return MaskSourceKind::RetouchPatch;
  }
  return std::nullopt;
}

std::optional<MaskCombineOperation> mask_operation(const QString& value) {
  const auto normalized = value.trimmed().toLower();
  if (normalized.isEmpty() || normalized == QStringLiteral("add")) {
    return MaskCombineOperation::Add;
  }
  if (normalized == QStringLiteral("subtract")) {
    return MaskCombineOperation::Subtract;
  }
  if (normalized == QStringLiteral("intersect")) {
    return MaskCombineOperation::Intersect;
  }
  return std::nullopt;
}

std::optional<photo_editor::pipeline::BlendMode> blend_mode(
    const QString& value) {
  const auto normalized = value.trimmed().toLower();
  if (normalized.isEmpty() || normalized == QStringLiteral("normal")) {
    return photo_editor::pipeline::BlendMode::Normal;
  }
  if (normalized == QStringLiteral("luminosity")) {
    return photo_editor::pipeline::BlendMode::Luminosity;
  }
  if (normalized == QStringLiteral("color")) {
    return photo_editor::pipeline::BlendMode::Color;
  }
  if (normalized == QStringLiteral("multiply")) {
    return photo_editor::pipeline::BlendMode::Multiply;
  }
  if (normalized == QStringLiteral("screen")) {
    return photo_editor::pipeline::BlendMode::Screen;
  }
  if (normalized == QStringLiteral("overlay")) {
    return photo_editor::pipeline::BlendMode::Overlay;
  }
  if (normalized == QStringLiteral("softlight") ||
      normalized == QStringLiteral("soft light")) {
    return photo_editor::pipeline::BlendMode::SoftLight;
  }
  return std::nullopt;
}

std::optional<photo_editor::pipeline::UprightMode> upright_mode(
    const QString& value) {
  const auto normalized = value.trimmed().toLower();
  if (normalized.isEmpty() || normalized == QStringLiteral("off")) {
    return photo_editor::pipeline::UprightMode::Off;
  }
  if (normalized == QStringLiteral("vertical")) {
    return photo_editor::pipeline::UprightMode::Vertical;
  }
  if (normalized == QStringLiteral("full")) {
    return photo_editor::pipeline::UprightMode::Full;
  }
  if (normalized == QStringLiteral("guided")) {
    return photo_editor::pipeline::UprightMode::Guided;
  }
  return std::nullopt;
}

std::optional<photo_editor::pipeline::OutputRenderIntent> output_render_intent(
    const QString& value) {
  const auto normalized = value.trimmed().toLower();
  if (normalized.isEmpty() || normalized == QStringLiteral("perceptual")) {
    return photo_editor::pipeline::OutputRenderIntent::Perceptual;
  }
  if (normalized == QStringLiteral("relative colorimetric") ||
      normalized == QStringLiteral("relative-colorimetric")) {
    return photo_editor::pipeline::OutputRenderIntent::RelativeColorimetric;
  }
  if (normalized == QStringLiteral("absolute colorimetric") ||
      normalized == QStringLiteral("absolute-colorimetric")) {
    return photo_editor::pipeline::OutputRenderIntent::AbsoluteColorimetric;
  }
  if (normalized == QStringLiteral("saturation")) {
    return photo_editor::pipeline::OutputRenderIntent::Saturation;
  }
  return std::nullopt;
}

std::optional<photo_editor::pipeline::OutputGamutMappingPolicy>
output_gamut_mapping_policy(const QString& value) {
  const auto normalized = value.trimmed().toLower();
  if (normalized.isEmpty() || normalized == QStringLiteral("perceptual")) {
    return photo_editor::pipeline::OutputGamutMappingPolicy::Perceptual;
  }
  if (normalized == QStringLiteral("clip") ||
      normalized == QStringLiteral("clipping") ||
      normalized == QStringLiteral("relative-colorimetric")) {
    return photo_editor::pipeline::OutputGamutMappingPolicy::Clip;
  }
  if (normalized == QStringLiteral("preserve saturation") ||
      normalized == QStringLiteral("preserve-saturation")) {
    return photo_editor::pipeline::OutputGamutMappingPolicy::PreserveSaturation;
  }
  return std::nullopt;
}

std::optional<photo_editor::pipeline::ColorMixerModelV13>
color_mixer_model(const QString& value) {
  const auto normalized = value.trimmed().toLower();
  if (normalized.isEmpty() || normalized == QStringLiteral("oklab")) {
    return photo_editor::pipeline::ColorMixerModelV13::Oklab;
  }
  if (normalized == QStringLiteral("hsl")) {
    return photo_editor::pipeline::ColorMixerModelV13::Hsl;
  }
  if (normalized == QStringLiteral("hsv")) {
    return photo_editor::pipeline::ColorMixerModelV13::Hsv;
  }
  if (normalized == QStringLiteral("lch")) {
    return photo_editor::pipeline::ColorMixerModelV13::Lch;
  }
  return std::nullopt;
}

std::optional<photo_editor::pipeline::ColorHarmonyModeV13>
color_harmony_mode(const QString& value) {
  const auto normalized = value.trimmed().toLower();
  if (normalized.isEmpty() ||
      normalized == QStringLiteral("complementary")) {
    return photo_editor::pipeline::ColorHarmonyModeV13::Complementary;
  }
  if (normalized == QStringLiteral("analogous")) {
    return photo_editor::pipeline::ColorHarmonyModeV13::Analogous;
  }
  if (normalized == QStringLiteral("triadic")) {
    return photo_editor::pipeline::ColorHarmonyModeV13::Triadic;
  }
  if (normalized == QStringLiteral("split complementary")) {
    return photo_editor::pipeline::ColorHarmonyModeV13::SplitComplementary;
  }
  if (normalized == QStringLiteral("tetradic")) {
    return photo_editor::pipeline::ColorHarmonyModeV13::Tetradic;
  }
  return std::nullopt;
}

std::optional<std::size_t> color_mixer_channel_index(
    const QString& value) {
  static const std::array<QString, 8> kNames = {
      QStringLiteral("reds"),
      QStringLiteral("oranges"),
      QStringLiteral("yellows"),
      QStringLiteral("greens"),
      QStringLiteral("aquas"),
      QStringLiteral("blues"),
      QStringLiteral("purples"),
      QStringLiteral("magentas"),
  };
  const auto normalized = value.trimmed().toLower();
  const auto iterator = std::find(kNames.begin(), kNames.end(), normalized);
  return iterator == kNames.end()
             ? std::nullopt
             : std::optional<std::size_t>{
                   static_cast<std::size_t>(
                       std::distance(kNames.begin(), iterator))};
}

std::optional<photo_editor::pipeline::RawDemosaicQualityV13>
raw_demosaic_quality(const QString& value) {
  const auto normalized = value.trimmed().toLower();
  if (normalized == QStringLiteral("preview") ||
      normalized == QStringLiteral("draft")) {
    return photo_editor::pipeline::RawDemosaicQualityV13::Preview;
  }
  if (normalized.isEmpty() || normalized == QStringLiteral("balanced")) {
    return photo_editor::pipeline::RawDemosaicQualityV13::Balanced;
  }
  if (normalized == QStringLiteral("full") ||
      normalized == QStringLiteral("best")) {
    return photo_editor::pipeline::RawDemosaicQualityV13::Full;
  }
  return std::nullopt;
}

std::optional<photo_editor::pipeline::HighlightReconstructionModeV13>
highlight_reconstruction_mode(const QString& value) {
  const auto normalized = value.trimmed().toLower();
  if (normalized == QStringLiteral("disabled")) {
    return photo_editor::pipeline::HighlightReconstructionModeV13::Disabled;
  }
  if (normalized == QStringLiteral("camera default") ||
      normalized == QStringLiteral("camera-default")) {
    return photo_editor::pipeline::HighlightReconstructionModeV13::CameraDefault;
  }
  if (normalized.isEmpty() || normalized == QStringLiteral("enabled")) {
    return photo_editor::pipeline::HighlightReconstructionModeV13::Enabled;
  }
  return std::nullopt;
}

std::optional<photo_editor::pipeline::ChromaticAdaptationMethodV13>
chromatic_adaptation_method(const QString& value) {
  const auto normalized = value.trimmed().toLower();
  if (normalized.isEmpty() || normalized == QStringLiteral("bradford")) {
    return photo_editor::pipeline::ChromaticAdaptationMethodV13::Bradford;
  }
  if (normalized == QStringLiteral("cat02")) {
    return photo_editor::pipeline::ChromaticAdaptationMethodV13::Cat02;
  }
  return std::nullopt;
}

std::optional<photo_editor::pipeline::CameraMatchingLookV13>
camera_matching_look(const QString& value) {
  const auto normalized = value.trimmed().toLower();
  if (normalized.isEmpty() || normalized == QStringLiteral("standard")) {
    return photo_editor::pipeline::CameraMatchingLookV13::Standard;
  }
  if (normalized == QStringLiteral("neutral")) {
    return photo_editor::pipeline::CameraMatchingLookV13::Neutral;
  }
  if (normalized == QStringLiteral("vivid")) {
    return photo_editor::pipeline::CameraMatchingLookV13::Vivid;
  }
  if (normalized == QStringLiteral("portrait")) {
    return photo_editor::pipeline::CameraMatchingLookV13::Portrait;
  }
  if (normalized == QStringLiteral("landscape")) {
    return photo_editor::pipeline::CameraMatchingLookV13::Landscape;
  }
  if (normalized == QStringLiteral("monochrome") ||
      normalized == QStringLiteral("black & white")) {
    return photo_editor::pipeline::CameraMatchingLookV13::Monochrome;
  }
  return std::nullopt;
}

std::optional<AssetIdentifier> payload_for_source(
    const DesktopRecipeV13Context& context,
    const QString& source_id) {
  const auto binding = std::find_if(
      context.mask_payload_assets.begin(),
      context.mask_payload_assets.end(),
      [&](const auto& candidate) {
        return candidate.source_id == source_id;
      });
  return binding == context.mask_payload_assets.end()
             ? std::nullopt
             : std::optional<AssetIdentifier>{binding->payload_asset};
}

std::optional<LayerKind> layer_kind(const QString& value) {
  const auto normalized = value.trimmed().toLower();
  if (normalized.isEmpty() || normalized == QStringLiteral("adjustment")) {
    return LayerKind::Adjustment;
  }
  if (normalized == QStringLiteral("pixel") ||
      normalized == QStringLiteral("source") ||
      normalized == QStringLiteral("image") ||
      normalized == QStringLiteral("photo")) {
    return LayerKind::Pixel;
  }
  if (normalized == QStringLiteral("text")) {
    return LayerKind::Text;
  }
  if (normalized == QStringLiteral("fill")) {
    return LayerKind::Fill;
  }
  if (normalized == QStringLiteral("group")) {
    return LayerKind::Group;
  }
  return std::nullopt;
}

std::optional<AssetIdentifier> content_for_layer(
    const DesktopRecipeV13Context& context,
    const QString& layer_id) {
  const auto binding = std::find_if(
      context.layer_content_assets.begin(),
      context.layer_content_assets.end(),
      [&](const auto& candidate) {
        return candidate.layer_id == layer_id;
      });
  return binding == context.layer_content_assets.end()
             ? std::nullopt
             : std::optional<AssetIdentifier>{binding->content_asset};
}

std::optional<AssetIdentifier> lut_payload_for_layer(
    const DesktopRecipeV13Context& context,
    const QString& layer_id,
    std::size_t index) {
  const auto binding = std::find_if(
      context.lut_payload_assets.begin(),
      context.lut_payload_assets.end(),
      [&](const auto& candidate) {
        return candidate.layer_id == layer_id &&
               candidate.index == index;
      });
  return binding == context.lut_payload_assets.end()
             ? std::nullopt
             : std::optional<AssetIdentifier>{binding->payload_asset};
}

double map_double(
    const QVariantMap& map,
    const QString& key,
    double fallback) {
  bool ok = false;
  const auto value = map.value(key).toDouble(&ok);
  return ok ? value : fallback;
}

QVariant layer_adjustment_value(
    const QVariantMap& layer,
    const QString& key) {
  const auto adjustments =
      layer.value(QStringLiteral("adjustments")).toMap();
  return adjustments.contains(key) ? adjustments.value(key)
                                   : layer.value(key);
}

double layer_double(
    const QVariantMap& layer,
    const QString& key,
    double fallback = 0.0) {
  bool ok = false;
  const auto value = layer_adjustment_value(layer, key).toDouble(&ok);
  return ok ? value : fallback;
}

QVariantList layer_list(
    const QVariantMap& layer,
    const QString& key) {
  return layer_adjustment_value(layer, key).toList();
}

QVariantMap layer_object(
    const QVariantMap& layer,
    const QString& key) {
  return layer_adjustment_value(layer, key).toMap();
}

double object_double(
    const QVariantMap& object,
    const QString& key,
    double fallback = 0.0) {
  return map_double(object, key, fallback);
}

bool object_bool(
    const QVariantMap& object,
    const QString& key,
    bool fallback = false) {
  return object.contains(key) ? object.value(key).toBool() : fallback;
}

QString object_string(
    const QVariantMap& object,
    const QString& key,
    const QString& fallback = {}) {
  return object.contains(key) ? object.value(key).toString() : fallback;
}

bool populate_layer_look(
    const QVariantMap& source,
    photo_editor::pipeline::LookRecipeV13* look,
    DesktopRecipeV13AdapterResult* result,
    const std::string& path) {
  look->temperature =
      signed_percent(layer_double(source, QStringLiteral("temperature")));
  look->tint =
      signed_percent(layer_double(source, QStringLiteral("tint")));
  look->vibrance =
      signed_percent(layer_double(source, QStringLiteral("vibrance")));
  look->saturation =
      signed_percent(layer_double(source, QStringLiteral("saturation")));
  look->hue_shift =
      signed_percent(layer_double(source, QStringLiteral("hueShift")));
  look->saturation_shift =
      signed_percent(layer_double(source, QStringLiteral("saturationShift")));
  look->colorize_hue = std::clamp(
      layer_double(source, QStringLiteral("colorizeHue")), 0.0, 1.0);
  look->colorize_strength =
      signed_percent(layer_double(source, QStringLiteral("colorizeStrength")));

  const auto mixer = layer_object(source, QStringLiteral("colorMixer"));
  const auto canonical_model = color_mixer_model(
      object_string(
          mixer,
          QStringLiteral("model"),
          layer_adjustment_value(
              source, QStringLiteral("colorMixerModel")).toString()));
  if (!canonical_model.has_value()) {
    add_error(
        result,
        DesktopRecipeAdapterErrorCode::UnsupportedLegacyIntent,
        path + ".mixer_model",
        "layer color mixer model is not representable by v13");
    return false;
  }
  look->mixer_model = *canonical_model;

  static const std::array<QString, 8> kChannelNames = {
      QStringLiteral("Reds"),
      QStringLiteral("Oranges"),
      QStringLiteral("Yellows"),
      QStringLiteral("Greens"),
      QStringLiteral("Aquas"),
      QStringLiteral("Blues"),
      QStringLiteral("Purples"),
      QStringLiteral("Magentas"),
  };
  const auto channels = mixer.value(QStringLiteral("channels")).toMap();
  bool has_explicit_channels = false;
  for (std::size_t index = 0; index < kChannelNames.size(); ++index) {
    const auto channel = channels.value(kChannelNames[index]).toMap();
    if (channel.isEmpty()) {
      continue;
    }
    has_explicit_channels = true;
    auto& target = look->mixer_channels[index];
    target.hue = signed_percent(object_double(channel, QStringLiteral("hue")));
    target.saturation =
        signed_percent(object_double(channel, QStringLiteral("saturation")));
    target.luminance =
        signed_percent(object_double(channel, QStringLiteral("luminance")));
    target.selective_cyan =
        signed_percent(object_double(channel, QStringLiteral("selectiveCyan")));
    target.selective_magenta = signed_percent(
        object_double(channel, QStringLiteral("selectiveMagenta")));
    target.selective_yellow = signed_percent(
        object_double(channel, QStringLiteral("selectiveYellow")));
    target.selective_black =
        signed_percent(object_double(channel, QStringLiteral("selectiveBlack")));
  }
  if (!has_explicit_channels) {
    const auto selected_channel = color_mixer_channel_index(
        object_string(
            mixer,
            QStringLiteral("channel"),
            layer_adjustment_value(
                source, QStringLiteral("hslChannel")).toString()));
    const auto selected_hue = object_double(
        mixer,
        QStringLiteral("hue"),
        layer_double(source, QStringLiteral("hueAdjustment")));
    const auto selected_saturation = object_double(
        mixer,
        QStringLiteral("saturation"),
        layer_double(source, QStringLiteral("saturationAdjustment")));
    const auto selected_luminance = object_double(
        mixer,
        QStringLiteral("luminance"),
        layer_double(source, QStringLiteral("luminanceAdjustment")));
    if ((active(selected_hue) || active(selected_saturation) ||
         active(selected_luminance)) &&
        !selected_channel.has_value()) {
      add_error(
          result,
          DesktopRecipeAdapterErrorCode::AmbiguousLegacyIntent,
          path + ".mixer_channels",
          "layer selected-channel mixer intent has no valid channel");
      return false;
    }
    if (selected_channel.has_value()) {
      auto& target = look->mixer_channels[*selected_channel];
      target.hue = signed_percent(selected_hue);
      target.saturation = signed_percent(selected_saturation);
      target.luminance = signed_percent(selected_luminance);
    }
  }

  const auto rgb = mixer.value(QStringLiteral("rgbChannelMixer")).toMap();
  look->rgb_channel_mixer.red_from_red =
      object_double(rgb, QStringLiteral("redFromRed"), 100.0) / 100.0;
  look->rgb_channel_mixer.red_from_green =
      object_double(rgb, QStringLiteral("redFromGreen")) / 100.0;
  look->rgb_channel_mixer.red_from_blue =
      object_double(rgb, QStringLiteral("redFromBlue")) / 100.0;
  look->rgb_channel_mixer.green_from_red =
      object_double(rgb, QStringLiteral("greenFromRed")) / 100.0;
  look->rgb_channel_mixer.green_from_green =
      object_double(rgb, QStringLiteral("greenFromGreen"), 100.0) / 100.0;
  look->rgb_channel_mixer.green_from_blue =
      object_double(rgb, QStringLiteral("greenFromBlue")) / 100.0;
  look->rgb_channel_mixer.blue_from_red =
      object_double(rgb, QStringLiteral("blueFromRed")) / 100.0;
  look->rgb_channel_mixer.blue_from_green =
      object_double(rgb, QStringLiteral("blueFromGreen")) / 100.0;
  look->rgb_channel_mixer.blue_from_blue =
      object_double(rgb, QStringLiteral("blueFromBlue"), 100.0) / 100.0;

  const auto black_and_white =
      mixer.value(QStringLiteral("blackAndWhite")).toMap();
  look->black_and_white_enabled =
      object_bool(black_and_white, QStringLiteral("enabled"));
  const auto black_and_white_channels =
      black_and_white.value(QStringLiteral("channels")).toMap();
  for (std::size_t index = 0; index < kChannelNames.size(); ++index) {
    look->black_and_white_mixer[index] = signed_percent(
        object_double(black_and_white_channels, kChannelNames[index]));
  }

  const auto replacement =
      mixer.value(QStringLiteral("colorReplacement")).toMap();
  look->color_replacement_enabled =
      object_bool(replacement, QStringLiteral("enabled"));
  look->color_replacement_source_hue = std::clamp(
      object_double(replacement, QStringLiteral("sourceHue")), 0.0, 1.0);
  look->color_replacement_target_hue = std::clamp(
      object_double(replacement, QStringLiteral("targetHue"), 0.33), 0.0, 1.0);
  look->color_replacement_range = std::clamp(
      object_double(replacement, QStringLiteral("range"), 35.0) / 100.0,
      0.0,
      1.0);
  look->color_replacement_softness = std::clamp(
      object_double(replacement, QStringLiteral("softness"), 100.0) / 100.0,
      0.0,
      1.0);
  look->color_replacement_strength = signed_percent(
      object_double(replacement, QStringLiteral("strength")));
  look->color_replacement_preserve_luminance = object_bool(
      replacement, QStringLiteral("preserveLuminance"), true);
  look->color_replacement_preserve_saturation = object_bool(
      replacement, QStringLiteral("preserveSaturation"), true);

  const auto skin =
      mixer.value(QStringLiteral("skinToneProtection")).toMap();
  look->skin_tone_protection_enabled =
      object_bool(skin, QStringLiteral("enabled"));
  look->skin_tone_line_hue = std::clamp(
      object_double(skin, QStringLiteral("lineHue"), 0.08), 0.0, 1.0);
  look->skin_tone_protection_range = std::clamp(
      object_double(skin, QStringLiteral("range"), 30.0) / 100.0, 0.0, 1.0);
  look->skin_tone_protection_strength =
      signed_percent(object_double(skin, QStringLiteral("strength")));

  const auto harmony =
      mixer.value(QStringLiteral("colorHarmony")).toMap();
  look->color_harmony_enabled =
      object_bool(harmony, QStringLiteral("enabled"));
  const auto canonical_harmony = color_harmony_mode(
      object_string(
          harmony, QStringLiteral("mode"), QStringLiteral("Complementary")));
  if (!canonical_harmony.has_value()) {
    add_error(
        result,
        DesktopRecipeAdapterErrorCode::UnsupportedLegacyIntent,
        path + ".color_harmony_mode",
        "layer color harmony mode is not representable by v13");
    return false;
  }
  look->color_harmony_mode = *canonical_harmony;
  look->color_harmony_base_hue = std::clamp(
      object_double(harmony, QStringLiteral("baseHue")), 0.0, 1.0);
  look->color_harmony_strength =
      signed_percent(object_double(harmony, QStringLiteral("strength")));

  const auto grading = layer_object(source, QStringLiteral("colorGrading"));
  const auto populate_grade =
      [&](const QString& name,
          photo_editor::pipeline::ColorGradingZoneV13* target) {
        const auto grade = grading.value(name).toMap();
        target->hue = std::clamp(
            object_double(grade, QStringLiteral("hue")), 0.0, 1.0);
        target->saturation =
            signed_percent(object_double(grade, QStringLiteral("saturation")));
        target->luminance =
            signed_percent(object_double(grade, QStringLiteral("luminance")));
      };
  populate_grade(QStringLiteral("shadows"), &look->grading_shadows);
  populate_grade(QStringLiteral("midtones"), &look->grading_midtones);
  populate_grade(QStringLiteral("highlights"), &look->grading_highlights);
  look->grading_amount = std::clamp(
      object_double(grading, QStringLiteral("amount"), 100.0) / 100.0,
      0.0,
      1.0);
  look->grading_balance =
      signed_percent(object_double(grading, QStringLiteral("balance")));
  return true;
}

bool append_layer_luts(
    const QVariantMap& source,
    const QString& layer_id,
    const DesktopRecipeV13Context& context,
    photo_editor::pipeline::LookRecipeV13* look,
    DesktopRecipeV13AdapterResult* result,
    const std::string& path) {
  const auto adjustments =
      source.value(QStringLiteral("adjustments")).toMap();
  auto source_luts = adjustments.value(QStringLiteral("lutStack")).toList();
  if (source_luts.isEmpty() &&
      adjustments.value(QStringLiteral("lut")).canConvert<QVariantMap>()) {
    source_luts.push_back(adjustments.value(QStringLiteral("lut")));
  }
  look->luts.reserve(static_cast<std::size_t>(source_luts.size()));
  for (qsizetype index = 0; index < source_luts.size(); ++index) {
    const auto source_lut = source_luts.at(index).toMap();
    const auto payload = lut_payload_for_layer(
        context, layer_id, static_cast<std::size_t>(index));
    if (!payload.has_value()) {
      add_error(
          result,
          DesktopRecipeAdapterErrorCode::UnsupportedLegacyIntent,
          path + "[" + std::to_string(index) + "].payload_asset",
          "layer LUT payload requires an immutable v13 asset identity");
      return false;
    }
    photo_editor::pipeline::CreativeLutRecipeV13 lut;
    lut.payload_asset = *payload;
    lut.enabled =
        object_bool(source_lut, QStringLiteral("enabled"), true);
    lut.strength = std::clamp(
        object_double(source_lut, QStringLiteral("strength"), 100.0) /
            100.0,
        0.0,
        1.0);
    lut.name = utf8(
        object_string(source_lut, QStringLiteral("name")));
    const auto read_domain =
        [&](const QString& key,
            const std::array<double, 3>& fallback) {
          auto domain = fallback;
          const auto values = source_lut.value(key).toList();
          if (values.size() == 3) {
            for (std::size_t channel = 0; channel < domain.size(); ++channel) {
              bool ok = false;
              const auto value =
                  values.at(static_cast<qsizetype>(channel)).toDouble(&ok);
              if (ok) {
                domain[channel] = value;
              }
            }
          }
          return domain;
        };
    lut.domain_min = read_domain(
        QStringLiteral("domainMin"), {0.0, 0.0, 0.0});
    lut.domain_max = read_domain(
        QStringLiteral("domainMax"), {1.0, 1.0, 1.0});
    lut.input_color_space = utf8(object_string(
        source_lut,
        QStringLiteral("inputColorSpace"),
        QStringLiteral("sRGB")));
    lut.output_color_space = utf8(object_string(
        source_lut,
        QStringLiteral("outputColorSpace"),
        QStringLiteral("sRGB")));
    look->luts.push_back(std::move(lut));
  }
  return true;
}

}  // namespace

DesktopRecipeV13AdapterResult adapt_desktop_recipe_to_v13(
    const EditRecipeState& state,
    const DesktopRecipeV13Context& context) {
  DesktopRecipeV13AdapterResult result;
  if (context.recipe_id.trimmed().isEmpty()) {
    add_error(
        &result,
        DesktopRecipeAdapterErrorCode::MissingContext,
        "recipe_id",
        "desktop compatibility compilation requires a stable recipe identifier");
  }
  if (context.source_id.trimmed().isEmpty()) {
    add_error(
        &result,
        DesktopRecipeAdapterErrorCode::MissingContext,
        "input.source",
        "desktop compatibility compilation requires a stable source identifier");
  }
  if (context.decoder_provenance.trimmed().isEmpty()) {
    add_error(
        &result,
        DesktopRecipeAdapterErrorCode::MissingContext,
        "input.decoder_provenance",
        "desktop compatibility compilation requires exact decoder provenance");
  }
  if (normalized_photographic_rendering_model_name(
          state.photographic_rendering_model) !=
      QStringLiteral("aces2-ocio-v1")) {
    add_error(
        &result,
        DesktopRecipeAdapterErrorCode::UnsupportedLegacyIntent,
        "look.renderer",
        "legacy and prototype photographic renderers cannot be written as v13");
  }
  if (active(state.raw_exposure) && active(state.exposure)) {
    add_error(
        &result,
        DesktopRecipeAdapterErrorCode::AmbiguousLegacyIntent,
        "scene.exposure_ev",
        "desktop v12 has simultaneous rawExposure and exposure values");
  }
  if (active(state.raw_boost) ||
      active(state.raw_shadow_boost - 1.0) ||
      active(state.raw_detail) ||
      active(state.raw_chroma_noise_reduction) ||
      active(state.raw_moire_reduction)) {
    add_error(
        &result,
        DesktopRecipeAdapterErrorCode::UnsupportedLegacyIntent,
        "input.retired_raw_controls",
        "legacy RAW boost/detail/noise controls had no executable pixel "
        "operator and cannot be written as v13");
  }
  if (state.raw_gamut_mapping) {
    add_error(
        &result,
        DesktopRecipeAdapterErrorCode::UnsupportedLegacyIntent,
        "input.retired_raw_gamut_mapping",
        "legacy RAW input gamut mapping had no executable pixel operator and "
        "cannot be written as v13");
  }

  const auto canonical_demosaic_quality =
      raw_demosaic_quality(state.raw_demosaic_quality);
  const auto canonical_highlight_reconstruction =
      highlight_reconstruction_mode(state.highlight_reconstruction_mode);
  const auto canonical_chromatic_adaptation =
      chromatic_adaptation_method(state.chromatic_adaptation_method);
  const auto canonical_camera_matching_look =
      camera_matching_look(state.raw_camera_matching_look);
  if (!canonical_demosaic_quality.has_value() ||
      !canonical_highlight_reconstruction.has_value() ||
      !canonical_chromatic_adaptation.has_value() ||
      !canonical_camera_matching_look.has_value()) {
    add_error(
        &result,
        DesktopRecipeAdapterErrorCode::UnsupportedLegacyIntent,
        "input.raw_controls",
        "desktop RAW mode strings are not representable by v13");
  }
  if (normalized_highlight_reconstruction_model_name(
          state.highlight_reconstruction_model) !=
      QStringLiteral("camera-linear-opposed-v2")) {
    add_error(
        &result,
        DesktopRecipeAdapterErrorCode::UnsupportedLegacyIntent,
        "input.highlight_reconstruction_model",
        "legacy working-RGB highlight reconstruction cannot be written as v13");
  }
  const auto canonical_color_mixer_model =
      color_mixer_model(state.color_mixer_model);
  const auto canonical_color_harmony_mode =
      color_harmony_mode(state.color_harmony_mode);
  if (!canonical_color_mixer_model.has_value()) {
    add_error(
        &result,
        DesktopRecipeAdapterErrorCode::UnsupportedLegacyIntent,
        "look.mixer_model",
        "desktop color mixer model is not representable by v13");
  }
  if (!canonical_color_harmony_mode.has_value()) {
    add_error(
        &result,
        DesktopRecipeAdapterErrorCode::UnsupportedLegacyIntent,
        "look.color_harmony_mode",
        "desktop color harmony mode is not representable by v13");
  }
  if (!any_mixer_active(state) &&
      (active(state.hue_adjustment) ||
       active(state.saturation_adjustment) ||
       active(state.luminance_adjustment)) &&
      !color_mixer_channel_index(state.hsl_channel).has_value()) {
    add_error(
        &result,
        DesktopRecipeAdapterErrorCode::AmbiguousLegacyIntent,
        "look.mixer_channels",
        "legacy selected-channel mixer intent has no valid channel");
  }
  const auto canonical_upright_mode = upright_mode(state.upright_mode);
  if (!canonical_upright_mode.has_value()) {
    add_error(
        &result,
        DesktopRecipeAdapterErrorCode::UnsupportedLegacyIntent,
        "geometry.upright_mode",
        "desktop upright mode is not representable by v13");
  }
  const auto canonical_output_render_intent =
      output_render_intent(state.render_intent);
  if (!canonical_output_render_intent.has_value()) {
    add_error(
        &result,
        DesktopRecipeAdapterErrorCode::UnsupportedLegacyIntent,
        "output.render_intent",
        "desktop output render intent is not representable by v13");
  }
  const auto canonical_gamut_mapping_policy =
      output_gamut_mapping_policy(state.output_gamut_mapping_policy);
  if (!canonical_gamut_mapping_policy.has_value()) {
    add_error(
        &result,
        DesktopRecipeAdapterErrorCode::UnsupportedLegacyIntent,
        "output.gamut_mapping_policy",
        "desktop gamut mapping policy is not representable by v13");
  }
  if (!result.errors.empty()) {
    return result;
  }

  EditRecipeV13 recipe;
  recipe.recipe_id = AssetIdentifier{utf8(context.recipe_id.trimmed())};
  recipe.input.source = AssetIdentifier{utf8(context.source_id.trimmed())};
  recipe.input.decoder =
      context.raw_source ? RawDecoder::LibRaw : RawDecoder::Raster;
  recipe.input.decoder_provenance =
      utf8(context.decoder_provenance.trimmed());
  const auto dcp_profile = profile_identifier(state.camera_dcp_profile_asset);
  const auto icc_profile = profile_identifier(state.input_icc_profile_asset);
  if (dcp_profile.has_value()) {
    recipe.input.camera_profile_mode = CameraProfileMode::Dcp;
    recipe.input.camera_profile = dcp_profile;
  } else if (icc_profile.has_value()) {
    recipe.input.camera_profile_mode = CameraProfileMode::Icc;
    recipe.input.camera_profile = icc_profile;
  } else {
    recipe.input.camera_profile_mode = CameraProfileMode::Embedded;
  }
  if (state.raw_white_balance_override) {
    recipe.input.white_balance_temperature_kelvin =
        state.raw_white_balance_temperature;
    recipe.input.white_balance_tint =
        state.raw_white_balance_tint / 100.0;
  }
  if (active(state.raw_black_level)) {
    recipe.input.black_level_override = state.raw_black_level;
  }
  if (active(state.raw_white_level - 1.0)) {
    recipe.input.white_level_override = state.raw_white_level;
  }
  recipe.input.input_transform_enabled =
      state.raw_input_transform_enabled;
  recipe.input.highlight_reconstruction =
      *canonical_highlight_reconstruction;
  recipe.input.chromatic_adaptation =
      *canonical_chromatic_adaptation;
  recipe.input.demosaic_quality = *canonical_demosaic_quality;
  recipe.input.hot_dead_pixel_cleanup =
      std::clamp(state.raw_hot_dead_pixel_cleanup, 0.0, 1.0);
  recipe.input.false_color_suppression =
      std::clamp(state.raw_false_color_suppression, 0.0, 1.0);
  recipe.input.highlight_recovery_enabled =
      state.raw_highlight_recovery;
  recipe.input.camera_matching_look = *canonical_camera_matching_look;
  recipe.input.calibration_hue =
      signed_percent(state.raw_calibration_hue);
  recipe.input.calibration_saturation =
      signed_percent(state.raw_calibration_saturation);
  recipe.input.red_primary_hue =
      signed_percent(state.raw_red_primary_hue);
  recipe.input.red_primary_saturation =
      signed_percent(state.raw_red_primary_saturation);
  recipe.input.green_primary_hue =
      signed_percent(state.raw_green_primary_hue);
  recipe.input.green_primary_saturation =
      signed_percent(state.raw_green_primary_saturation);
  recipe.input.blue_primary_hue =
      signed_percent(state.raw_blue_primary_hue);
  recipe.input.blue_primary_saturation =
      signed_percent(state.raw_blue_primary_saturation);
  recipe.scene.exposure_ev =
      active(state.exposure) ? state.exposure : state.raw_exposure;
  recipe.tone.contrast = signed_percent(state.contrast);
  recipe.tone.highlights = signed_percent(state.highlights);
  recipe.tone.shadows = signed_percent(state.shadows);
  recipe.tone.whites = signed_percent(state.whites);
  recipe.tone.blacks = signed_percent(state.blacks);
  recipe.tone.brightness = signed_percent(state.brightness);
  recipe.tone.hdr_local_tone_mapping =
      signed_percent(state.hdr_local_tone_mapping);
  recipe.tone.filmic_shoulder = signed_percent(state.filmic_shoulder);
  recipe.tone.filmic_toe = signed_percent(state.filmic_toe);
  recipe.tone.adaptive_contrast = signed_percent(state.adaptive_contrast);
  recipe.tone.levels_black_point =
      signed_percent(state.levels_black_point);
  recipe.tone.levels_gamma = signed_percent(state.levels_gamma);
  recipe.tone.levels_white_point =
      signed_percent(state.levels_white_point);
  recipe.tone.zone_deep_shadows =
      signed_percent(state.zone_deep_shadows);
  recipe.tone.zone_shadows = signed_percent(state.zone_shadows);
  recipe.tone.zone_midtones = signed_percent(state.zone_midtones);
  recipe.tone.zone_lights = signed_percent(state.zone_lights);
  recipe.tone.zone_highlights =
      signed_percent(state.zone_highlights);
  recipe.look.temperature = signed_percent(state.temperature);
  recipe.look.tint = signed_percent(state.tint);
  recipe.look.vibrance = signed_percent(state.vibrance);
  recipe.look.saturation = signed_percent(state.saturation);
  recipe.look.mixer_model = *canonical_color_mixer_model;
  for (std::size_t index = 0; index < state.mixer_channels.size(); ++index) {
    const auto& source = state.mixer_channels[index];
    auto& target = recipe.look.mixer_channels[index];
    target.hue = signed_percent(source.hue);
    target.saturation = signed_percent(source.saturation);
    target.luminance = signed_percent(source.luminance);
    target.selective_cyan = signed_percent(source.selective_cyan);
    target.selective_magenta = signed_percent(source.selective_magenta);
    target.selective_yellow = signed_percent(source.selective_yellow);
    target.selective_black = signed_percent(source.selective_black);
  }
  if (!any_mixer_active(state)) {
    const auto selected = color_mixer_channel_index(state.hsl_channel);
    if (selected.has_value()) {
      auto& target = recipe.look.mixer_channels[*selected];
      target.hue = signed_percent(state.hue_adjustment);
      target.saturation = signed_percent(state.saturation_adjustment);
      target.luminance = signed_percent(state.luminance_adjustment);
    }
  }
  const auto normalize_mixer = [](double value) {
    return std::clamp(value / 100.0, -4.0, 4.0);
  };
  const auto& source_rgb = state.rgb_channel_mixer;
  auto& target_rgb = recipe.look.rgb_channel_mixer;
  target_rgb.red_from_red = normalize_mixer(source_rgb.red_from_red);
  target_rgb.red_from_green = normalize_mixer(source_rgb.red_from_green);
  target_rgb.red_from_blue = normalize_mixer(source_rgb.red_from_blue);
  target_rgb.green_from_red = normalize_mixer(source_rgb.green_from_red);
  target_rgb.green_from_green = normalize_mixer(source_rgb.green_from_green);
  target_rgb.green_from_blue = normalize_mixer(source_rgb.green_from_blue);
  target_rgb.blue_from_red = normalize_mixer(source_rgb.blue_from_red);
  target_rgb.blue_from_green = normalize_mixer(source_rgb.blue_from_green);
  target_rgb.blue_from_blue = normalize_mixer(source_rgb.blue_from_blue);
  recipe.look.black_and_white_enabled = state.black_and_white_enabled;
  for (std::size_t index = 0;
       index < state.black_and_white_mixer.size();
       ++index) {
    recipe.look.black_and_white_mixer[index] =
        signed_percent(state.black_and_white_mixer[index]);
  }
  recipe.look.color_replacement_enabled =
      state.color_replacement_enabled;
  recipe.look.color_replacement_source_hue =
      std::clamp(state.color_replacement_source_hue, 0.0, 1.0);
  recipe.look.color_replacement_target_hue =
      std::clamp(state.color_replacement_target_hue, 0.0, 1.0);
  recipe.look.color_replacement_range =
      std::clamp(state.color_replacement_range / 100.0, 0.0, 1.0);
  recipe.look.color_replacement_softness =
      std::clamp(state.color_replacement_softness / 100.0, 0.0, 1.0);
  recipe.look.color_replacement_strength =
      signed_percent(state.color_replacement_strength);
  recipe.look.color_replacement_preserve_luminance =
      state.color_replacement_preserve_luminance;
  recipe.look.color_replacement_preserve_saturation =
      state.color_replacement_preserve_saturation;
  recipe.look.skin_tone_protection_enabled =
      state.skin_tone_protection_enabled;
  recipe.look.skin_tone_line_hue =
      std::clamp(state.skin_tone_line_hue, 0.0, 1.0);
  recipe.look.skin_tone_protection_range =
      std::clamp(state.skin_tone_protection_range / 100.0, 0.0, 1.0);
  recipe.look.skin_tone_protection_strength =
      signed_percent(state.skin_tone_protection_strength);
  recipe.look.color_harmony_enabled = state.color_harmony_enabled;
  recipe.look.color_harmony_mode = *canonical_color_harmony_mode;
  recipe.look.color_harmony_base_hue =
      std::clamp(state.color_harmony_base_hue, 0.0, 1.0);
  recipe.look.color_harmony_strength =
      signed_percent(state.color_harmony_strength);
  recipe.look.grading_shadows = {
      .hue = std::clamp(state.grading_shadows_hue, 0.0, 1.0),
      .saturation = signed_percent(state.grading_shadows_saturation),
      .luminance = signed_percent(state.grading_shadows_luminance),
  };
  recipe.look.grading_midtones = {
      .hue = std::clamp(state.grading_midtones_hue, 0.0, 1.0),
      .saturation = signed_percent(state.grading_midtones_saturation),
      .luminance = signed_percent(state.grading_midtones_luminance),
  };
  recipe.look.grading_highlights = {
      .hue = std::clamp(state.grading_highlights_hue, 0.0, 1.0),
      .saturation = signed_percent(state.grading_highlights_saturation),
      .luminance = signed_percent(state.grading_highlights_luminance),
  };
  recipe.look.grading_amount =
      std::clamp(state.grading_amount / 100.0, 0.0, 1.0);
  recipe.look.grading_balance =
      signed_percent(state.grading_balance);
  if (!append_curve(state.master_curve_points, &recipe.curves.master) ||
      !append_curve(state.luma_curve_points, &recipe.curves.luma) ||
      !append_curve(state.red_curve_points, &recipe.curves.red) ||
      !append_curve(state.green_curve_points, &recipe.curves.green) ||
      !append_curve(state.blue_curve_points, &recipe.curves.blue)) {
    add_error(
        &result,
        DesktopRecipeAdapterErrorCode::InvalidV13Recipe,
        "curves",
        "desktop channel curve points must contain numeric x and y values");
    return result;
  }
  if (recipe.curves.master.empty() &&
      (active(state.curve_shadows) || active(state.curve_darks) ||
       active(state.curve_lights) || active(state.curve_highlights))) {
    const auto point = [](double x, double delta) {
      return CurvePointV13{
          .x = x,
          .y = std::clamp(x + (delta / 100.0) * 0.34, 0.0, 1.0),
      };
    };
    recipe.curves.master = {
        {.x = 0.0, .y = 0.0},
        point(0.20, state.curve_shadows),
        point(0.40, state.curve_darks),
        point(0.65, state.curve_lights),
        point(0.85, state.curve_highlights),
        {.x = 1.0, .y = 1.0},
    };
  }
  recipe.detail.texture = signed_percent(state.texture);
  recipe.detail.clarity = signed_percent(state.clarity);
  recipe.detail.dehaze = signed_percent(state.dehaze);
  recipe.detail.sharpening =
      std::clamp(state.sharpening / 100.0, 0.0, 1.0);
  recipe.detail.sharpen_radius =
      std::clamp(state.sharpen_radius, 0.1, 5.0);
  recipe.detail.sharpen_detail =
      std::clamp(state.sharpen_detail / 100.0, 0.0, 1.0);
  recipe.detail.sharpen_masking =
      std::clamp(state.sharpen_masking / 100.0, 0.0, 1.0);
  recipe.detail.luminance_noise_reduction =
      std::clamp(state.luminance_nr / 100.0, 0.0, 1.0);
  recipe.detail.chroma_noise_reduction =
      std::clamp(state.color_nr / 100.0, 0.0, 1.0);
  const auto append_mask_layer =
      [&](std::string id_prefix,
          MaskSourceKind kind,
          double exposure,
          double temperature,
          photo_editor::pipeline::MaskGeometryV13 geometry) {
        const auto set_id = id_prefix + "-set";
        MaskSetRecipeV13 mask_set{
            .id = AssetIdentifier{set_id},
            .enabled = true,
        };
        mask_set.sources.push_back(MaskSourceRecipeV13{
            .id = AssetIdentifier{id_prefix + "-source"},
            .kind = kind,
            .enabled = true,
            .geometry = geometry,
        });
        recipe.masks.push_back(std::move(mask_set));
        LayerRecipeV13 layer;
        layer.id = AssetIdentifier{id_prefix + "-adjustment"};
        layer.mask = AssetIdentifier{set_id};
        layer.scene.exposure_ev = exposure;
        layer.look.temperature = signed_percent(temperature);
        recipe.layers.push_back(std::move(layer));
      };
  if (state.mask_stack.isEmpty() && state.linear_mask_enabled) {
    append_mask_layer(
        "desktop-linear-mask",
        MaskSourceKind::LinearGradient,
        state.linear_mask_exposure,
        state.linear_mask_temperature,
        photo_editor::pipeline::MaskGeometryV13{
            .feather = state.linear_mask_feather,
            .position = state.linear_mask_position,
            .rotation_degrees = state.linear_mask_rotation,
        });
  }
  if (state.mask_stack.isEmpty() && state.radial_mask_enabled) {
    append_mask_layer(
        "desktop-radial-mask",
        MaskSourceKind::RadialGradient,
        state.radial_mask_exposure,
        state.radial_mask_temperature,
        photo_editor::pipeline::MaskGeometryV13{
            .feather = state.radial_mask_feather,
            .center_x = state.radial_mask_center_x,
            .center_y = state.radial_mask_center_y,
            .radius_x = state.radial_mask_radius_x,
            .radius_y = state.radial_mask_radius_y,
            .rotation_degrees = state.radial_mask_rotation,
        });
  }
  auto effective_masks = state.mask_stack;
  for (qsizetype index = 0; index < effective_masks.size();
       ++index) {
    auto mask = effective_masks.at(index).toMap();
    const auto overrides =
        mask.value(QStringLiteral("adjustmentOverrides")).toMap();
    for (auto iterator = overrides.cbegin();
         iterator != overrides.cend();
         ++iterator) {
      mask.insert(iterator.key(), iterator.value());
    }
    effective_masks[index] = mask;
  }
  const auto normalized_masks = normalize_mask_stack(effective_masks);
  for (qsizetype index = 0; index < normalized_masks.size(); ++index) {
    auto mask = normalized_masks.at(index).toMap();
    const auto adjustment_overrides =
        mask.value(QStringLiteral("adjustmentOverrides")).toMap();
    for (auto iterator = adjustment_overrides.cbegin();
         iterator != adjustment_overrides.cend();
         ++iterator) {
      mask.insert(iterator.key(), iterator.value());
    }
    const auto source_id = mask.value(QStringLiteral("id")).toString().trimmed();
    const auto kind = mask_source_kind(
        mask.value(QStringLiteral("kind")).toString());
    const auto operation = mask_operation(
        mask.value(QStringLiteral("operation")).toString());
    const auto source_blend_mode = blend_mode(
        mask.value(QStringLiteral("blendMode")).toString());
    if (source_id.isEmpty() || !kind.has_value() ||
        !operation.has_value() || !source_blend_mode.has_value()) {
      add_error(
          &result,
          DesktopRecipeAdapterErrorCode::UnsupportedLegacyIntent,
          "masks[" + std::to_string(index) + "]",
          "mask source requires a stable id and supported kind, operation, and blend mode");
      return result;
    }
    auto mask_set_id =
        mask.value(QStringLiteral("maskSetId")).toString().trimmed();
    if (mask_set_id.isEmpty()) {
      mask_set_id = source_id + QStringLiteral("-set");
    }
    auto mask_set = std::find_if(
        recipe.masks.begin(),
        recipe.masks.end(),
        [&](const auto& candidate) {
          return candidate.id.value == utf8(mask_set_id);
        });
    if (mask_set == recipe.masks.end()) {
      recipe.masks.push_back(MaskSetRecipeV13{
          .id = AssetIdentifier{utf8(mask_set_id)},
          .enabled = mask.value(
                            QStringLiteral("enabled"),
                            true)
                         .toBool(),
      });
      mask_set = std::prev(recipe.masks.end());
    }

    const auto geometry_value =
        mask.value(QStringLiteral("geometry")).toMap();
    const auto& geometry =
        geometry_value.isEmpty() ? mask : geometry_value;
    MaskSourceRecipeV13 source;
    source.id = AssetIdentifier{utf8(source_id)};
    source.kind = *kind;
    source.operation = *operation;
    const auto parent_id =
        mask.value(QStringLiteral("parentId")).toString().trimmed();
    if (!parent_id.isEmpty()) {
      source.parent = AssetIdentifier{utf8(parent_id)};
    }
    source.enabled =
        mask.value(QStringLiteral("enabled"), true).toBool();
    source.blend_mode = *source_blend_mode;
    source.opacity =
        map_double(mask, QStringLiteral("opacity"), 1.0);
    source.density =
        map_double(mask, QStringLiteral("density"), 1.0);
    source.inverted =
        mask.value(QStringLiteral("invert"), false).toBool();
    const auto output_id =
        mask.value(QStringLiteral("outputId")).toString().trimmed();
    if (!output_id.isEmpty()) {
      source.output = AssetIdentifier{utf8(output_id)};
    }
    source.reusable_output =
        mask.value(QStringLiteral("reusableOutput"), false).toBool();
    const auto coordinate_space =
        mask.value(QStringLiteral("maskCoordinateSpace"))
            .toString()
            .trimmed()
            .toLower();
    source.coordinate_space =
        coordinate_space == QStringLiteral("source-pixels")
            ? MaskCoordinateSpace::SourcePixels
            : MaskCoordinateSpace::SourceNormalized;
    const auto sampling =
        geometry.value(QStringLiteral("alphaSamplingMode"))
            .toString()
            .trimmed()
            .toLower();
    source.sampling =
        sampling == QStringLiteral("nearest-source-pixel")
            ? MaskSamplingMode::Nearest
            : (sampling == QStringLiteral("bilinear")
                   ? MaskSamplingMode::Bilinear
                   : MaskSamplingMode::Analytic);
    const auto source_revision =
        mask.value(QStringLiteral("sourceImageRevision"))
            .toString()
            .trimmed();
    if (!source_revision.isEmpty()) {
      source.source_revision =
          AssetIdentifier{utf8(source_revision)};
    }
    source.payload_asset = payload_for_source(context, source_id);
    source.geometry.feather =
        map_double(geometry, QStringLiteral("feather"), 0.0);
    source.geometry.position =
        map_double(geometry, QStringLiteral("position"), 0.5);
    source.geometry.center_x =
        map_double(geometry, QStringLiteral("centerX"), 0.5);
    source.geometry.center_y =
        map_double(geometry, QStringLiteral("centerY"), 0.5);
    source.geometry.radius_x =
        map_double(geometry, QStringLiteral("radiusX"), 0.25);
    source.geometry.radius_y =
        map_double(geometry, QStringLiteral("radiusY"), 0.25);
    source.geometry.rotation_degrees =
        map_double(geometry, QStringLiteral("rotation"), 0.0);
    source.geometry.range_min = map_double(
        geometry,
        QStringLiteral("rangeMin"),
        map_double(
            geometry,
            QStringLiteral("depthMin"),
            map_double(geometry, QStringLiteral("luminanceMin"), 0.0)));
    source.geometry.range_max = map_double(
        geometry,
        QStringLiteral("rangeMax"),
        map_double(
            geometry,
            QStringLiteral("depthMax"),
            map_double(geometry, QStringLiteral("luminanceMax"), 1.0)));
    const auto source_mask_id = source.id;
    mask_set->sources.push_back(std::move(source));

    // Desktop v12 stores effective adjustments on each flattened source after
    // merging its mask-set values with adjustmentOverrides. Preserve that
    // exact per-source result as an explicit v13 layer instead of requiring
    // every source in a set to share one ambiguous adjustment.
    LayerRecipeV13 source_layer;
    source_layer.id =
        AssetIdentifier{"desktop-mask-source-layer:" + utf8(source_id)};
    source_layer.mask = source_mask_id;
    source_layer.scene.exposure_ev =
        map_double(mask, QStringLiteral("exposure"), 0.0);
    source_layer.tone.contrast =
        signed_percent(map_double(mask, QStringLiteral("contrast"), 0.0));
    source_layer.tone.highlights =
        signed_percent(map_double(mask, QStringLiteral("highlights"), 0.0));
    source_layer.tone.shadows =
        signed_percent(map_double(mask, QStringLiteral("shadows"), 0.0));
    source_layer.tone.whites =
        signed_percent(map_double(mask, QStringLiteral("whites"), 0.0));
    source_layer.tone.blacks =
        signed_percent(map_double(mask, QStringLiteral("blacks"), 0.0));
    source_layer.look.temperature =
        signed_percent(map_double(mask, QStringLiteral("temperature"), 0.0));
    source_layer.look.tint =
        signed_percent(map_double(mask, QStringLiteral("tint"), 0.0));
    source_layer.look.vibrance =
        signed_percent(map_double(mask, QStringLiteral("vibrance"), 0.0));
    source_layer.look.saturation =
        signed_percent(map_double(mask, QStringLiteral("saturation"), 0.0));
    recipe.layers.push_back(std::move(source_layer));
  }
  for (qsizetype index = 0; index < state.layers.size(); ++index) {
    const auto source_layer = state.layers.at(index).toMap();
    const auto layer_id =
        source_layer.value(QStringLiteral("id")).toString().trimmed();
    const auto kind =
        layer_kind(source_layer.value(QStringLiteral("kind")).toString());
    const auto mode = blend_mode(
        source_layer.value(QStringLiteral("blendMode")).toString());
    if (layer_id.isEmpty() || !kind.has_value() || !mode.has_value()) {
      add_error(
          &result,
          DesktopRecipeAdapterErrorCode::UnsupportedLegacyIntent,
          "layers[" + std::to_string(index) + "]",
          "layer requires a stable id and supported kind and blend mode");
      return result;
    }
    LayerRecipeV13 layer;
    QString effect_error;
    auto effects = decode_photo_layer_effects(source_layer, &effect_error);
    if (!effects) {
      add_error(&result, DesktopRecipeAdapterErrorCode::UnsupportedLegacyIntent,
          "layers[" + std::to_string(index) + "].effectStack", utf8(effect_error));
      return result;
    }
    layer.effects = std::move(*effects);
    layer.id = AssetIdentifier{utf8(layer_id)};
    layer.kind = *kind;
    layer.enabled =
        source_layer.value(QStringLiteral("enabled"), true).toBool();
    layer.opacity =
        map_double(source_layer, QStringLiteral("opacity"), 1.0);
    layer.blend_mode = *mode;
    const auto mask_id =
        source_layer
            .value(QStringLiteral("selectionMaskSetId"))
            .toString()
            .trimmed();
    if (!mask_id.isEmpty()) {
      layer.mask = AssetIdentifier{utf8(mask_id)};
    }
    const auto parent_id =
        source_layer.value(QStringLiteral("parentId"))
            .toString()
            .trimmed();
    if (!parent_id.isEmpty()) {
      layer.parent = AssetIdentifier{utf8(parent_id)};
    }
    layer.content_asset = content_for_layer(context, layer_id);
    layer.scene.exposure_ev =
        layer_double(source_layer, QStringLiteral("exposure"));
    layer.tone.contrast = signed_percent(
        layer_double(source_layer, QStringLiteral("contrast")));
    layer.tone.highlights = signed_percent(
        layer_double(source_layer, QStringLiteral("highlights")));
    layer.tone.shadows = signed_percent(
        layer_double(source_layer, QStringLiteral("shadows")));
    layer.tone.whites = signed_percent(
        layer_double(source_layer, QStringLiteral("whites")));
    layer.tone.blacks = signed_percent(
        layer_double(source_layer, QStringLiteral("blacks")));
    layer.tone.brightness = signed_percent(
        layer_double(source_layer, QStringLiteral("brightness")));
    layer.tone.hdr_local_tone_mapping = signed_percent(
        layer_double(source_layer, QStringLiteral("hdrLocalToneMapping")));
    layer.tone.filmic_shoulder = signed_percent(
        layer_double(source_layer, QStringLiteral("filmicShoulder")));
    layer.tone.filmic_toe = signed_percent(
        layer_double(source_layer, QStringLiteral("filmicToe")));
    layer.tone.adaptive_contrast = signed_percent(
        layer_double(source_layer, QStringLiteral("adaptiveContrast")));
    layer.tone.levels_black_point = signed_percent(
        layer_double(source_layer, QStringLiteral("levelsBlackPoint")));
    layer.tone.levels_gamma = signed_percent(
        layer_double(source_layer, QStringLiteral("levelsGamma")));
    layer.tone.levels_white_point = signed_percent(
        layer_double(source_layer, QStringLiteral("levelsWhitePoint")));
    layer.tone.zone_deep_shadows = signed_percent(
        layer_double(source_layer, QStringLiteral("zoneDeepShadows")));
    layer.tone.zone_shadows = signed_percent(
        layer_double(source_layer, QStringLiteral("zoneShadows")));
    layer.tone.zone_midtones = signed_percent(
        layer_double(source_layer, QStringLiteral("zoneMidtones")));
    layer.tone.zone_lights = signed_percent(
        layer_double(source_layer, QStringLiteral("zoneLights")));
    layer.tone.zone_highlights = signed_percent(
        layer_double(source_layer, QStringLiteral("zoneHighlights")));
    if (!append_curve(
            layer_list(source_layer, QStringLiteral("masterCurvePoints")),
            &layer.curves.master) ||
        !append_curve(
            layer_list(source_layer, QStringLiteral("lumaCurvePoints")),
            &layer.curves.luma) ||
        !append_curve(
            layer_list(source_layer, QStringLiteral("redCurvePoints")),
            &layer.curves.red) ||
        !append_curve(
            layer_list(source_layer, QStringLiteral("greenCurvePoints")),
            &layer.curves.green) ||
        !append_curve(
            layer_list(source_layer, QStringLiteral("blueCurvePoints")),
            &layer.curves.blue)) {
      add_error(
          &result,
          DesktopRecipeAdapterErrorCode::InvalidV13Recipe,
          "layers[" + std::to_string(index) + "].curves",
          "layer channel curves must contain numeric x and y values");
      return result;
    }
    if (layer.curves.master.empty()) {
      const auto curve_shadows =
          layer_double(source_layer, QStringLiteral("curveShadows"));
      const auto curve_darks =
          layer_double(source_layer, QStringLiteral("curveDarks"));
      const auto curve_lights =
          layer_double(source_layer, QStringLiteral("curveLights"));
      const auto curve_highlights =
          layer_double(source_layer, QStringLiteral("curveHighlights"));
      if (active(curve_shadows) || active(curve_darks) ||
          active(curve_lights) || active(curve_highlights)) {
        const auto point = [](double x, double delta) {
          return CurvePointV13{
              .x = x,
              .y =
                  std::clamp(x + (delta / 100.0) * 0.34, 0.0, 1.0),
          };
        };
        layer.curves.master = {
            {.x = 0.0, .y = 0.0},
            point(0.20, curve_shadows),
            point(0.40, curve_darks),
            point(0.65, curve_lights),
            point(0.85, curve_highlights),
            {.x = 1.0, .y = 1.0},
        };
      }
    }
    if (!populate_layer_look(
            source_layer,
            &layer.look,
            &result,
            "layers[" + std::to_string(index) + "].look")) {
      return result;
    }
    if (!append_layer_luts(
            source_layer,
            layer_id,
            context,
            &layer.look,
            &result,
            "layers[" + std::to_string(index) + "].look.luts")) {
      return result;
    }
    layer.detail.texture = signed_percent(
        layer_double(source_layer, QStringLiteral("texture")));
    layer.detail.clarity = signed_percent(
        layer_double(source_layer, QStringLiteral("clarity")));
    layer.detail.dehaze = signed_percent(
        layer_double(source_layer, QStringLiteral("dehaze")));
    layer.detail.sharpening = std::clamp(
        layer_double(source_layer, QStringLiteral("sharpening")) / 100.0,
        0.0,
        1.0);
    layer.detail.sharpen_radius = std::clamp(
        layer_double(source_layer, QStringLiteral("sharpenRadius"), 1.0),
        0.1,
        5.0);
    layer.detail.sharpen_detail = std::clamp(
        layer_double(source_layer, QStringLiteral("sharpenDetail")) / 100.0,
        0.0,
        1.0);
    layer.detail.sharpen_masking = std::clamp(
        layer_double(source_layer, QStringLiteral("sharpenMasking")) / 100.0,
        0.0,
        1.0);
    layer.detail.luminance_noise_reduction = std::clamp(
        layer_double(source_layer, QStringLiteral("luminanceNr")) / 100.0,
        0.0,
        1.0);
    layer.detail.chroma_noise_reduction = std::clamp(
        layer_double(source_layer, QStringLiteral("colorNr")) / 100.0,
        0.0,
        1.0);
    layer.geometry.rotation_degrees =
        layer_double(source_layer, QStringLiteral("transformRotation"));
    layer.geometry.lens_distortion = signed_percent(
        layer_double(source_layer, QStringLiteral("lensDistortion")));
    layer.geometry.lens_vignette = signed_percent(
        layer_double(source_layer, QStringLiteral("lensVignette")));
    layer.geometry.chromatic_aberration = signed_percent(
        layer_double(source_layer, QStringLiteral("chromaticAberration")));
    layer.geometry.perspective_vertical = signed_percent(
        layer_double(source_layer, QStringLiteral("perspectiveVertical")));
    layer.geometry.perspective_horizontal = signed_percent(
        layer_double(source_layer, QStringLiteral("perspectiveHorizontal")));
    layer.geometry.scale = std::clamp(
        layer_double(source_layer, QStringLiteral("transformScale"), 1.0),
        0.01,
        8.0);
    layer.geometry.skew_x = signed_percent(
        layer_double(source_layer, QStringLiteral("transformSkewX")));
    layer.geometry.skew_y = signed_percent(
        layer_double(source_layer, QStringLiteral("transformSkewY")));
    layer.geometry.crop_zoom = std::clamp(
        layer_double(source_layer, QStringLiteral("cropZoom"), 1.0),
        0.01,
        8.0);
    const auto layer_upright = upright_mode(
        layer_adjustment_value(
            source_layer, QStringLiteral("uprightMode")).toString());
    if (!layer_upright.has_value()) {
      add_error(
          &result,
          DesktopRecipeAdapterErrorCode::UnsupportedLegacyIntent,
          "layers[" + std::to_string(index) + "].geometry.upright_mode",
          "layer upright mode is not representable by v13");
      return result;
    }
    layer.geometry.upright_mode = *layer_upright;
    layer.geometry.liquify_center_x = std::clamp(
        layer_double(source_layer, QStringLiteral("liquifyCenterX"), 0.5),
        0.0,
        1.0);
    layer.geometry.liquify_center_y = std::clamp(
        layer_double(source_layer, QStringLiteral("liquifyCenterY"), 0.5),
        0.0,
        1.0);
    layer.geometry.liquify_radius = std::clamp(
        layer_double(source_layer, QStringLiteral("liquifyRadius"), 0.25),
        0.0,
        1.0);
    layer.geometry.liquify_strength = signed_percent(
        layer_double(source_layer, QStringLiteral("liquifyStrength")));
    layer.geometry.content_aware_crop_fill =
        layer_adjustment_value(
            source_layer,
            QStringLiteral("contentAwareCropFill")).toBool();
    recipe.layers.push_back(std::move(layer));
  }
  recipe.geometry.crop_x = state.crop_rect_x;
  recipe.geometry.crop_y = state.crop_rect_y;
  recipe.geometry.crop_width =
      state.crop_rect_width > 0.0 ? state.crop_rect_width : 1.0;
  recipe.geometry.crop_height =
      state.crop_rect_height > 0.0 ? state.crop_rect_height : 1.0;
  recipe.geometry.rotation_degrees = state.transform_rotation;
  recipe.geometry.lens_distortion = signed_percent(state.lens_distortion);
  recipe.geometry.lens_vignette = signed_percent(state.lens_vignette);
  recipe.geometry.chromatic_aberration =
      signed_percent(state.chromatic_aberration);
  recipe.geometry.perspective_vertical =
      signed_percent(state.perspective_vertical);
  recipe.geometry.perspective_horizontal =
      signed_percent(state.perspective_horizontal);
  recipe.geometry.scale = std::clamp(state.transform_scale, 0.01, 8.0);
  recipe.geometry.skew_x = signed_percent(state.transform_skew_x);
  recipe.geometry.skew_y = signed_percent(state.transform_skew_y);
  recipe.geometry.crop_zoom = std::clamp(state.crop_zoom, 0.01, 8.0);
  recipe.geometry.upright_mode = *canonical_upright_mode;
  recipe.geometry.liquify_center_x = state.liquify_center_x;
  recipe.geometry.liquify_center_y = state.liquify_center_y;
  recipe.geometry.liquify_radius = state.liquify_radius;
  recipe.geometry.liquify_strength =
      signed_percent(state.liquify_strength);
  recipe.geometry.content_aware_crop_fill =
      state.content_aware_crop_fill;
  recipe.output.rendering_target = context.rendering_target;
  using photo_editor::pipeline::ExportColorSpaceV13;
  const auto export_space = normalized_output_color_space_name(state.output_color_space);
  if (export_space == QStringLiteral("Display P3")) recipe.output.export_color_space = ExportColorSpaceV13::DisplayP3;
  else if (export_space == QStringLiteral("Adobe RGB")) recipe.output.export_color_space = ExportColorSpaceV13::AdobeRgb;
  else if (export_space == QStringLiteral("ProPhoto RGB")) recipe.output.export_color_space = ExportColorSpaceV13::ProPhotoRgb;
  else if (export_space == QStringLiteral("Rec.709")) recipe.output.export_color_space = ExportColorSpaceV13::Rec709;
  else if (export_space == QStringLiteral("Rec.2020")) recipe.output.export_color_space = ExportColorSpaceV13::Rec2020;
  recipe.output.renderer_transition_look =
      normalized_renderer_transition_look_name(
          state.renderer_transition_look) ==
              QStringLiteral("preserve-legacy-direct-luminance-v1")
          ? photo_editor::pipeline::RendererTransitionLookV13::
                PreserveLegacyDirectLuminanceV1
          : photo_editor::pipeline::RendererTransitionLookV13::None;
  recipe.output.device_icc_profile = context.device_icc_profile;
  recipe.output.peak_luminance_nits =
      std::clamp(state.output_headroom * 100.0, 1.0, 10000.0);
  recipe.output.render_intent = *canonical_output_render_intent;
  recipe.output.gamut_mapping_enabled =
      state.output_gamut_mapping_enabled;
  recipe.output.gamut_mapping_policy =
      *canonical_gamut_mapping_policy;
  recipe.output.black_point_compensation =
      state.black_point_compensation_enabled;
  recipe.output.source_black_point =
      std::clamp(state.source_black_point, 0.0, 1.0);
  recipe.output.output_black_point =
      std::clamp(state.output_black_point, 0.0, 1.0);
  if (state.soft_proof_enabled) {
    const auto proof_profile = proof_profile_identifier(state);
    if (!proof_profile.has_value()) {
      add_error(
          &result,
          DesktopRecipeAdapterErrorCode::UnsupportedLegacyIntent,
          "proof.profile",
          "soft proofing requires an immutable ICC profile asset for v13");
      return result;
    }
    recipe.proof.mode = ProofMode::Icc;
    recipe.proof.profile = proof_profile;
    recipe.proof.simulate_paper = state.proof_simulate_paper_white;
    recipe.proof.simulate_ink_black =
        state.proof_simulate_ink_black;
    recipe.proof.black_point_compensation =
        state.black_point_compensation_enabled;
  }

  const auto validation =
      photo_editor::pipeline::validate_edit_recipe_v13(recipe);
  for (const auto& error : validation.errors) {
    add_error(
        &result,
        DesktopRecipeAdapterErrorCode::InvalidV13Recipe,
        error.path,
        error.message);
  }
  if (result.errors.empty()) {
    result.recipe =
        photo_editor::pipeline::normalize_edit_recipe_v13(recipe);
  }
  return result;
}

DesktopRenderPlanResult compile_desktop_render_plan(
    const EditRecipeState& state,
    const DesktopRecipeV13Context& adapter_context,
    const photo_editor::pipeline::RecipeCompileContext& compile_context) {
  DesktopRenderPlanResult result;
  auto adapted = adapt_desktop_recipe_to_v13(state, adapter_context);
  if (!adapted.ok()) {
    result.adapter_errors = std::move(adapted.errors);
    return result;
  }

  auto compiled = photo_editor::pipeline::RecipeCompiler{}.compile(
      *adapted.recipe, compile_context);
  if (!compiled.ok()) {
    result.compiler_errors = std::move(compiled.errors);
    return result;
  }
  result.plan = std::move(compiled.plan);
  return result;
}

DesktopRenderPlanResult compile_desktop_render_plan_with_derived_assets(
    const EditRecipeState& state,
    const DesktopRenderPlanRequest& request) {
  const auto recipe_digest =
      request.recipe_payload_digest.trimmed().isEmpty()
          ? QString::fromLatin1(
                QCryptographicHash::hash(
                    QJsonDocument(
                        build_edit_adjustment_state_object(state))
                        .toJson(QJsonDocument::Compact),
                    QCryptographicHash::Sha256)
                    .toHex())
          : request.recipe_payload_digest.trimmed();
  DesktopRecipeV13Context adapter_context{
      .recipe_id = request.recipe_id,
      .source_id = request.source_id,
      .decoder_provenance = request.decoder_provenance,
      .raw_source = request.raw_source,
      .rendering_target = request.rendering_target,
      .device_icc_profile = request.device_icc_profile,
  };
  auto compile_context = photo_editor::pipeline::RecipeCompileContext{
      .source_width = request.source_width,
      .source_height = request.source_height,
      .available_photo_effect_algorithms = {"gaussian_blur:1", "invert_linear_rgb:1", "box_blur:1", "posterize_linear_rgb:1", "motion_blur:1", "unsharp_mask:1"},
      .backend_requirement = request.backend_requirement,
      .purpose = request.purpose,
      .scope_domain = request.scope_domain,
  };
  const auto append_profile =
      [&](const std::optional<AssetIdentifier>& profile) {
        if (profile.has_value()) {
          compile_context.available_profile_assets.push_back(*profile);
        }
      };
  append_profile(profile_identifier(state.camera_dcp_profile_asset));
  append_profile(profile_identifier(state.input_icc_profile_asset));
  append_profile(proof_profile_identifier(state));
  append_profile(profile_identifier(state.preview_icc_profile_asset));
  append_profile(profile_identifier(state.export_icc_profile_asset));
  append_profile(request.device_icc_profile);

  const auto normalized_masks = normalize_mask_stack(state.mask_stack);
  for (const auto& value : normalized_masks) {
    const auto mask = value.toMap();
    const auto source_id =
        mask.value(QStringLiteral("id")).toString().trimmed();
    if (source_id.isEmpty()) {
      continue;
    }
    const auto payload = AssetIdentifier{
        "desktop-mask-payload:" + utf8(source_id) + ":" +
        utf8(recipe_digest)};
    adapter_context.mask_payload_assets.push_back(
        DesktopRecipeV13Context::MaskPayloadBinding{
            .source_id = source_id,
            .payload_asset = payload,
        });
    compile_context.available_content_assets.push_back(payload);
  }
  for (const auto& value : state.layers) {
    const auto layer = value.toMap();
    const auto layer_id =
        layer.value(QStringLiteral("id")).toString().trimmed();
    const auto kind =
        layer_kind(layer.value(QStringLiteral("kind")).toString());
    if (layer_id.isEmpty() || !kind.has_value()) {
      continue;
    }
    const auto adjustments =
        layer.value(QStringLiteral("adjustments")).toMap();
    auto luts = adjustments.value(QStringLiteral("lutStack")).toList();
    if (luts.isEmpty() &&
        adjustments.value(QStringLiteral("lut")).canConvert<QVariantMap>()) {
      luts.push_back(adjustments.value(QStringLiteral("lut")));
    }
    for (qsizetype index = 0; index < luts.size(); ++index) {
      const auto payload_bytes =
          QJsonDocument::fromVariant(luts.at(index))
              .toJson(QJsonDocument::Compact);
      const auto payload_digest = QCryptographicHash::hash(
                                      payload_bytes,
                                      QCryptographicHash::Sha256)
                                      .toHex();
      const auto payload = AssetIdentifier{
          "desktop-layer-lut:sha256:" +
          std::string(payload_digest.constData(),
                      static_cast<std::size_t>(payload_digest.size()))};
      adapter_context.lut_payload_assets.push_back(
          DesktopRecipeV13Context::LutPayloadBinding{
              .layer_id = layer_id,
              .index = static_cast<std::size_t>(index),
              .payload_asset = payload,
          });
      compile_context.available_content_assets.push_back(payload);
    }
    if (*kind == LayerKind::Adjustment || *kind == LayerKind::Group) {
      continue;
    }
    const auto content = AssetIdentifier{
        "desktop-layer-content:" + utf8(layer_id) + ":" +
        utf8(recipe_digest) + ":" + utf8(linked_photo_layer_sources_revision({value}))};
    adapter_context.layer_content_assets.push_back(
        DesktopRecipeV13Context::LayerContentBinding{
            .layer_id = layer_id,
            .content_asset = content,
        });
    compile_context.available_content_assets.push_back(content);
  }

  // The v13 recipe carries raster/brush/polygon content by immutable asset
  // identity. Do not repeatedly normalize and copy those potentially large
  // inline legacy payloads while adapting the structural recipe.
  auto adapter_state = state;
  QVariantList structural_masks;
  structural_masks.reserve(state.mask_stack.size());
  for (const auto& value : state.mask_stack) {
    auto mask = value.toMap();
    for (const auto* key : {
             "segmentationData",
             "brushPoints",
             "polygonPoints",
             "depthData",
             "inpaintPatchPixels",
         }) {
      mask.remove(QString::fromLatin1(key));
    }
    structural_masks.push_back(mask);
  }
  adapter_state.mask_stack = structural_masks;
  return compile_desktop_render_plan(
      adapter_state, adapter_context, compile_context);
}

}  // namespace photo_editor::desktop
