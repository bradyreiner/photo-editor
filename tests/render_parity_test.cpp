#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

#include <QColorSpace>
#include <QCoreApplication>
#include <QImage>
#include <QImageWriter>
#include <QVariantMap>
#include <QTemporaryDir>
#include <QtGui/qrgbafloat.h>

#include "edit_recipe_state.h"
#include "photo_render_utils.h"
#include "working_image.h"

namespace {

constexpr int kSkipTest = 77;

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

QImage deterministic_source_image() {
  QImage image(64, 48, QImage::Format_ARGB32);
  image.setColorSpace(photo_editor::desktop::preview_color_space());
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      image.setPixelColor(
          x,
          y,
          QColor(
              24 + ((x * 151 + y * 17) % 196),
              20 + ((x * 23 + y * 131) % 204),
              18 + ((x * 59 + y * 47) % 210),
              255));
    }
  }
  return image;
}

photo_editor::desktop::EditRecipeState deterministic_state() {
  photo_editor::desktop::EditRecipeState state;
  state.working_color_profile = QStringLiteral("Linear sRGB");
  state.preview_color_space = QStringLiteral("sRGB");
  state.output_color_space = QStringLiteral("sRGB");
  state.exposure = 0.35;
  state.contrast = 14.0;
  state.highlights = -8.0;
  state.shadows = 11.0;
  state.whites = 4.0;
  state.blacks = -5.0;
  state.brightness = 9.0;
  state.filmic_shoulder = 12.0;
  state.filmic_toe = -7.0;
  state.adaptive_contrast = 18.0;
  state.levels_black_point = 3.0;
  state.levels_gamma = 8.0;
  state.levels_white_point = 4.0;
  state.zone_deep_shadows = -5.0;
  state.zone_shadows = 6.0;
  state.zone_midtones = 7.0;
  state.zone_lights = -4.0;
  state.zone_highlights = 5.0;
  state.temperature = 7.0;
  state.tint = -3.0;
  state.vibrance = 16.0;
  state.saturation = -6.0;
  state.color_mixer_model = QStringLiteral("HSV");
  state.hsl_channel = QStringLiteral("Reds");
  state.hue_adjustment = 9.0;
  state.saturation_adjustment = 7.0;
  state.luminance_adjustment = 5.0;
  state.mixer_channels[0].selective_cyan = 11.0;
  state.mixer_channels[0].selective_magenta = -6.0;
  state.mixer_channels[0].selective_yellow = 8.0;
  state.mixer_channels[0].selective_black = 5.0;
  state.rgb_channel_mixer.red_from_red = 96.0;
  state.rgb_channel_mixer.red_from_green = 7.0;
  state.rgb_channel_mixer.green_from_red = 4.0;
  state.rgb_channel_mixer.green_from_green = 101.0;
  state.rgb_channel_mixer.blue_from_red = -3.0;
  state.rgb_channel_mixer.blue_from_blue = 105.0;
  state.black_and_white_enabled = true;
  state.black_and_white_mixer[0] = 9.0;
  state.black_and_white_mixer[5] = -7.0;
  state.color_replacement_enabled = true;
  state.color_replacement_source_hue = 0.0;
  state.color_replacement_target_hue = 0.34;
  state.color_replacement_range = 40.0;
  state.color_replacement_strength = 30.0;
  state.skin_tone_protection_enabled = true;
  state.skin_tone_line_hue = 0.08;
  state.skin_tone_protection_range = 34.0;
  state.skin_tone_protection_strength = 45.0;
  state.color_harmony_enabled = true;
  state.color_harmony_mode = QStringLiteral("Split Complementary");
  state.color_harmony_base_hue = 0.04;
  state.color_harmony_strength = 24.0;
  state.curve_shadows = -4.0;
  state.curve_lights = 5.0;
  state.luma_curve_points = {
      QVariantMap{{QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.0}},
      QVariantMap{{QStringLiteral("x"), 0.5}, {QStringLiteral("y"), 0.56}},
      QVariantMap{{QStringLiteral("x"), 1.0}, {QStringLiteral("y"), 1.0}},
  };
  state.grading_midtones_hue = 8.0;
  state.grading_midtones_saturation = 5.0;
  return state;
}

int max_channel_delta(const QImage& left, const QImage& right) {
  if (left.size() != right.size()) {
    return 255;
  }

  auto max_delta = 0;
  for (int y = 0; y < left.height(); ++y) {
    for (int x = 0; x < left.width(); ++x) {
      const auto lhs = left.pixelColor(x, y);
      const auto rhs = right.pixelColor(x, y);
      max_delta = std::max(max_delta, std::abs(lhs.red() - rhs.red()));
      max_delta = std::max(max_delta, std::abs(lhs.green() - rhs.green()));
      max_delta = std::max(max_delta, std::abs(lhs.blue() - rhs.blue()));
    }
  }
  return max_delta;
}

bool write_png(const QImage& image, const QString& path) {
  QImageWriter writer(path, "png");
  writer.setCompression(0);
  return writer.write(image);
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  const auto source = deterministic_source_image();
  const auto state = deterministic_state();
  const auto target_space = photo_editor::desktop::preview_color_space();

  const auto cpu_preview = photo_editor::desktop::render_output_image_with_backend(
      source,
      state,
      target_space,
      QImage::Format_ARGB32,
      photo_editor::desktop::RenderBackendPreference::CpuOnly);
  if (cpu_preview.isNull()) {
    return fail("CPU preview render should produce an image");
  }

  const auto auto_preview = photo_editor::desktop::render_output_image(
      source,
      state,
      target_space,
      QImage::Format_ARGB32);
  if (auto_preview.isNull()) {
    return fail("auto preview render should produce an image");
  }
  if (max_channel_delta(cpu_preview, auto_preview) > 2) {
    return fail("auto preview render should match CPU preview render within tolerance");
  }

  auto hdr_local_tone_state = state;
  hdr_local_tone_state.hdr_local_tone_mapping = 35.0;
  const auto hdr_local_cpu_preview = photo_editor::desktop::render_output_image_with_backend(
      source,
      hdr_local_tone_state,
      target_space,
      QImage::Format_ARGB32,
      photo_editor::desktop::RenderBackendPreference::CpuOnly);
  const auto hdr_local_auto_preview = photo_editor::desktop::render_output_image(
      source,
      hdr_local_tone_state,
      target_space,
      QImage::Format_ARGB32);
  if (hdr_local_cpu_preview.isNull() || hdr_local_auto_preview.isNull()) {
    return fail("HDR local tone mapping preview should render through the CPU spatial path");
  }
  if (max_channel_delta(hdr_local_cpu_preview, hdr_local_auto_preview) > 2) {
    return fail("auto preview should match CPU preview for HDR local tone mapping");
  }

  if (!photo_editor::desktop::render_backend_available(
          photo_editor::desktop::RenderBackendPreference::GpuOnly)) {
    std::cerr << "GPU render backend unavailable; skipping CPU/Metal parity coverage on this host\n";
    return kSkipTest;
  }

  const auto gpu_preview = photo_editor::desktop::render_output_image_with_backend(
      source,
      state,
      target_space,
      QImage::Format_ARGB32,
      photo_editor::desktop::RenderBackendPreference::GpuOnly);
  if (gpu_preview.isNull()) {
    return fail("GPU preview render should produce an image for the deterministic recipe");
  }
  if (max_channel_delta(cpu_preview, gpu_preview) > 2) {
    return fail("GPU preview render should match CPU preview render within tolerance");
  }

  const auto cpu_full_resolution = photo_editor::desktop::render_output_image_with_backend(
      source,
      state,
      target_space,
      QImage::Format_RGBA64,
      photo_editor::desktop::RenderBackendPreference::CpuOnly);
  if (cpu_full_resolution.isNull()) {
    return fail("CPU full-resolution render should produce an image");
  }

  const auto gpu_full_resolution = photo_editor::desktop::render_output_image_with_backend(
      source,
      state,
      target_space,
      QImage::Format_RGBA64,
      photo_editor::desktop::RenderBackendPreference::GpuOnly);
  if (gpu_full_resolution.isNull()) {
    return fail("GPU full-resolution render should produce an image");
  }
  if (max_channel_delta(cpu_full_resolution.convertToFormat(QImage::Format_ARGB32),
                        gpu_full_resolution.convertToFormat(QImage::Format_ARGB32)) > 2) {
    return fail("GPU full-resolution render should match CPU full-resolution render within tolerance");
  }

  QTemporaryDir temp_dir;
  if (!temp_dir.isValid()) {
    return fail("temporary export directory should be created");
  }
  const auto export_path = temp_dir.filePath(QStringLiteral("parity-export.png"));
  if (!write_png(cpu_full_resolution, export_path)) {
    return fail("full-resolution export artifact should be written");
  }
  const QImage exported(export_path);
  if (exported.isNull()) {
    return fail("full-resolution export artifact should be readable");
  }
  if (max_channel_delta(
          cpu_full_resolution.convertToFormat(QImage::Format_ARGB32),
          exported.convertToFormat(QImage::Format_ARGB32)) > 1) {
    return fail("encoded full-resolution export should match the rendered full-resolution image");
  }

  photo_editor::desktop::EditRecipeState neutral_state;
  neutral_state.working_color_profile = QStringLiteral("Linear sRGB");
  neutral_state.preview_color_space = QStringLiteral("sRGB");
  neutral_state.output_color_space = QStringLiteral("sRGB");
  const auto neutral_preview = photo_editor::desktop::render_output_image_with_backend(
      source,
      neutral_state,
      target_space,
      QImage::Format_ARGB32,
      photo_editor::desktop::RenderBackendPreference::CpuOnly);
  if (neutral_preview.isNull()) {
    return fail("neutral high-risk parity baseline should render");
  }
  const auto verify_high_risk_parity =
      [&](const char* label,
          const photo_editor::desktop::EditRecipeState& high_risk_state,
          int minimum_visible_delta) {
        const auto preview_render = photo_editor::desktop::render_output_image_with_backend(
            source,
            high_risk_state,
            target_space,
            QImage::Format_ARGB32,
            photo_editor::desktop::RenderBackendPreference::CpuOnly);
        const auto export_render = photo_editor::desktop::render_output_image_with_backend(
            source,
            high_risk_state,
            target_space,
            QImage::Format_RGBA64,
            photo_editor::desktop::RenderBackendPreference::CpuOnly);
        if (preview_render.isNull() || export_render.isNull()) {
          std::cerr << label << " high-risk preview/export parity render should produce images\n";
          return false;
        }
        if (max_channel_delta(preview_render, export_render.convertToFormat(QImage::Format_ARGB32)) >
            2) {
          std::cerr << label << " high-risk preview should match full-resolution export render\n";
          return false;
        }
        if (max_channel_delta(neutral_preview, preview_render) < minimum_visible_delta) {
          std::cerr << label << " high-risk recipe should visibly affect the parity source\n";
          return false;
        }
        return true;
      };

  auto white_balance_tone_state = neutral_state;
  white_balance_tone_state.temperature = 42.0;
  white_balance_tone_state.tint = -28.0;
  white_balance_tone_state.exposure = 0.55;
  white_balance_tone_state.contrast = 22.0;
  white_balance_tone_state.highlights = -34.0;
  white_balance_tone_state.shadows = 31.0;
  white_balance_tone_state.whites = 18.0;
  white_balance_tone_state.blacks = -21.0;
  if (!verify_high_risk_parity("white balance and tone", white_balance_tone_state, 8)) {
    return EXIT_FAILURE;
  }

  auto tone_curve_state = neutral_state;
  tone_curve_state.curve_shadows = -24.0;
  tone_curve_state.curve_darks = -12.0;
  tone_curve_state.curve_lights = 18.0;
  tone_curve_state.curve_highlights = 12.0;
  tone_curve_state.luma_curve_points = {
      QVariantMap{{QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.0}},
      QVariantMap{{QStringLiteral("x"), 0.28}, {QStringLiteral("y"), 0.18}},
      QVariantMap{{QStringLiteral("x"), 0.72}, {QStringLiteral("y"), 0.84}},
      QVariantMap{{QStringLiteral("x"), 1.0}, {QStringLiteral("y"), 1.0}},
  };
  if (!verify_high_risk_parity("tone curve", tone_curve_state, 5)) {
    return EXIT_FAILURE;
  }

  auto hsl_selective_state = neutral_state;
  hsl_selective_state.color_mixer_model = QStringLiteral("HSV");
  hsl_selective_state.hsl_channel = QStringLiteral("Reds");
  hsl_selective_state.hue_adjustment = 42.0;
  hsl_selective_state.saturation_adjustment = 36.0;
  hsl_selective_state.luminance_adjustment = -18.0;
  hsl_selective_state.mixer_channels[0].selective_cyan = 34.0;
  hsl_selective_state.mixer_channels[0].selective_magenta = -28.0;
  hsl_selective_state.mixer_channels[0].selective_yellow = 26.0;
  hsl_selective_state.mixer_channels[0].selective_black = 16.0;
  if (!verify_high_risk_parity("HSL and selective color", hsl_selective_state, 5)) {
    return EXIT_FAILURE;
  }

  auto channel_mixer_state = neutral_state;
  channel_mixer_state.rgb_channel_mixer.red_from_red = 78.0;
  channel_mixer_state.rgb_channel_mixer.red_from_green = 24.0;
  channel_mixer_state.rgb_channel_mixer.green_from_red = -18.0;
  channel_mixer_state.rgb_channel_mixer.green_from_green = 118.0;
  channel_mixer_state.rgb_channel_mixer.blue_from_green = 22.0;
  channel_mixer_state.rgb_channel_mixer.blue_from_blue = 84.0;
  if (!verify_high_risk_parity("RGB channel mixer", channel_mixer_state, 5)) {
    return EXIT_FAILURE;
  }

  auto black_and_white_state = neutral_state;
  black_and_white_state.black_and_white_enabled = true;
  black_and_white_state.black_and_white_mixer[0] = 32.0;
  black_and_white_state.black_and_white_mixer[2] = -18.0;
  black_and_white_state.black_and_white_mixer[5] = 24.0;
  if (!verify_high_risk_parity("black and white mixer", black_and_white_state, 8)) {
    return EXIT_FAILURE;
  }

  auto advanced_color_state = neutral_state;
  advanced_color_state.color_replacement_enabled = true;
  advanced_color_state.color_replacement_source_hue = 0.0;
  advanced_color_state.color_replacement_target_hue = 0.58;
  advanced_color_state.color_replacement_range = 58.0;
  advanced_color_state.color_replacement_strength = 74.0;
  advanced_color_state.color_harmony_enabled = true;
  advanced_color_state.color_harmony_mode = QStringLiteral("Triadic");
  advanced_color_state.color_harmony_base_hue = 0.07;
  advanced_color_state.color_harmony_strength = 44.0;
  if (!verify_high_risk_parity("color replacement and harmony", advanced_color_state, 5)) {
    return EXIT_FAILURE;
  }

  auto presence_grading_state = neutral_state;
  presence_grading_state.texture = 28.0;
  presence_grading_state.clarity = -18.0;
  presence_grading_state.dehaze = 24.0;
  presence_grading_state.vibrance = 38.0;
  presence_grading_state.saturation = -16.0;
  presence_grading_state.grading_shadows_hue = 224.0;
  presence_grading_state.grading_shadows_saturation = 18.0;
  presence_grading_state.grading_midtones_hue = 32.0;
  presence_grading_state.grading_midtones_saturation = 20.0;
  presence_grading_state.grading_highlights_hue = 54.0;
  presence_grading_state.grading_highlights_saturation = 14.0;
  if (!verify_high_risk_parity("presence and grading", presence_grading_state, 5)) {
    return EXIT_FAILURE;
  }

  QImage layer_mask_source(4, 2, QImage::Format_ARGB32);
  layer_mask_source.setColorSpace(photo_editor::desktop::preview_color_space());
  layer_mask_source.fill(QColor::fromRgb(68, 72, 76));

  photo_editor::desktop::RenderEditState layer_mask_state;
  layer_mask_state.working_color_profile = QStringLiteral("Linear sRGB");
  layer_mask_state.preview_color_space = QStringLiteral("sRGB");
  layer_mask_state.output_color_space = QStringLiteral("sRGB");

  QByteArray local_mask_data(8, char(0));
  for (int index = 0; index < local_mask_data.size(); ++index) {
    if (index % 4 >= 2) {
      local_mask_data[index] = char(0xff);
    }
  }
  QVariantMap local_mask;
  local_mask.insert(QStringLiteral("id"), QStringLiteral("preview-export-local-mask"));
  local_mask.insert(QStringLiteral("name"), QStringLiteral("Right side lift"));
  local_mask.insert(QStringLiteral("kind"), QStringLiteral("brush"));
  local_mask.insert(QStringLiteral("toolRole"), QStringLiteral("selection"));
  local_mask.insert(QStringLiteral("maskSetId"), QStringLiteral("preview-export-mask-set"));
  local_mask.insert(QStringLiteral("maskSetName"), QStringLiteral("Right side lift"));
  local_mask.insert(QStringLiteral("operation"), QStringLiteral("add"));
  local_mask.insert(QStringLiteral("segmentationData"), local_mask_data);
  local_mask.insert(QStringLiteral("segmentationWidth"), 4);
  local_mask.insert(QStringLiteral("segmentationHeight"), 2);
  local_mask.insert(QStringLiteral("exposure"), 0.85);
  layer_mask_state.mask_stack.append(local_mask);

  QByteArray layer_mask_data(8, char(0));
  layer_mask_data[4] = char(0xff);
  layer_mask_data[5] = char(0xff);
  QVariantMap masked_fill_layer;
  masked_fill_layer.insert(QStringLiteral("id"), QStringLiteral("preview-export-fill-layer"));
  masked_fill_layer.insert(QStringLiteral("name"), QStringLiteral("Lower left red wash"));
  masked_fill_layer.insert(QStringLiteral("kind"), QStringLiteral("fill"));
  masked_fill_layer.insert(QStringLiteral("enabled"), true);
  masked_fill_layer.insert(QStringLiteral("opacity"), 0.55);
  masked_fill_layer.insert(QStringLiteral("fillColor"), QStringLiteral("#ff2020"));
  masked_fill_layer.insert(QStringLiteral("maskData"), layer_mask_data);
  masked_fill_layer.insert(QStringLiteral("maskWidth"), 4);
  masked_fill_layer.insert(QStringLiteral("maskHeight"), 2);
  layer_mask_state.layers.append(masked_fill_layer);

  const auto layer_mask_preview = photo_editor::desktop::render_output_image_with_backend(
      layer_mask_source,
      layer_mask_state,
      photo_editor::desktop::preview_color_space(),
      QImage::Format_ARGB32,
      photo_editor::desktop::RenderBackendPreference::CpuOnly);
  const auto layer_mask_export_render = photo_editor::desktop::render_output_image_with_backend(
      layer_mask_source,
      layer_mask_state,
      photo_editor::desktop::preview_color_space(),
      QImage::Format_RGBA64,
      photo_editor::desktop::RenderBackendPreference::CpuOnly);
  if (layer_mask_preview.isNull() || layer_mask_export_render.isNull()) {
    return fail("layer plus mask preview/export proof should produce rendered images");
  }
  if (max_channel_delta(
          layer_mask_preview,
          layer_mask_export_render.convertToFormat(QImage::Format_ARGB32)) > 2) {
    return fail("layer plus mask preview should match the full-resolution export render");
  }
  const auto base_sample = layer_mask_preview.pixelColor(0, 0);
  const auto local_mask_sample = layer_mask_preview.pixelColor(3, 0);
  const auto layer_mask_sample = layer_mask_preview.pixelColor(0, 1);
  const auto unmasked_layer_sample = layer_mask_preview.pixelColor(3, 1);
  if (local_mask_sample.green() <= base_sample.green() + 12) {
    return fail("selection masks should still affect the preview/export parity render");
  }
  if (layer_mask_sample.red() <= layer_mask_sample.green() + 20) {
    return fail("photo layer masks should still affect the preview/export parity render");
  }
  if (unmasked_layer_sample.red() > unmasked_layer_sample.green() + 20) {
    return fail("photo layer masks should not leak into unmasked pixels");
  }
  const auto layer_mask_export_path = temp_dir.filePath(QStringLiteral("layer-mask-parity-export.png"));
  if (!write_png(layer_mask_export_render, layer_mask_export_path)) {
    return fail("layer plus mask full-resolution export artifact should be written");
  }
  const QImage layer_mask_exported(layer_mask_export_path);
  if (layer_mask_exported.isNull()) {
    return fail("layer plus mask full-resolution export artifact should be readable");
  }
  if (max_channel_delta(
          layer_mask_export_render.convertToFormat(QImage::Format_ARGB32),
          layer_mask_exported.convertToFormat(QImage::Format_ARGB32)) > 1) {
    return fail("encoded layer plus mask export should match the full-resolution render");
  }

  auto display_p3_source = deterministic_source_image();
  display_p3_source.setColorSpace(QColorSpace(QColorSpace::DisplayP3));
  const auto display_p3_path = temp_dir.filePath(QStringLiteral("embedded-display-p3.png"));
  if (!write_png(display_p3_source, display_p3_path)) {
    return fail("embedded Display P3 source should be written");
  }

  QImage decoded_display_p3;
  if (!photo_editor::desktop::decode_source_image(display_p3_path, &decoded_display_p3, 0, state) ||
      decoded_display_p3.isNull()) {
    return fail("embedded Display P3 source should decode through the source image path");
  }
  if (decoded_display_p3.colorSpace() != QColorSpace(QColorSpace::DisplayP3)) {
    return fail("decode should preserve an embedded Display P3 profile");
  }

  auto same_samples_as_srgb = display_p3_source;
  same_samples_as_srgb.setColorSpace(QColorSpace(QColorSpace::SRgb));
  const auto rendered_from_embedded_p3 = photo_editor::desktop::render_output_image(
      display_p3_source,
      state,
      photo_editor::desktop::preview_color_space(),
      QImage::Format_ARGB32);
  const auto rendered_from_tagged_srgb = photo_editor::desktop::render_output_image(
      same_samples_as_srgb,
      state,
      photo_editor::desktop::preview_color_space(),
      QImage::Format_ARGB32);
  if (rendered_from_embedded_p3.isNull() || rendered_from_tagged_srgb.isNull()) {
    return fail("tagged source profile render comparison should produce images");
  }
  if (max_channel_delta(rendered_from_embedded_p3, rendered_from_tagged_srgb) <= 2) {
    return fail("render should honor the source image color profile instead of treating it as sRGB");
  }

  QImage sixteen_bit_source(3, 1, QImage::Format_RGBA64);
  sixteen_bit_source.setColorSpace(photo_editor::desktop::linear_srgb_color_space());
  sixteen_bit_source.setPixelColor(0, 0, QColor::fromRgbF(0.25F, 0.5F, 0.75F, 1.0F));
  sixteen_bit_source.setPixelColor(1, 0, QColor::fromRgbF(0.5F, 0.25F, 0.125F, 1.0F));
  sixteen_bit_source.setPixelColor(2, 0, QColor::fromRgbF(0.875F, 0.5F, 0.25F, 1.0F));
  const auto sixteen_bit_working = photo_editor::desktop::working_image_from_qimage(
      sixteen_bit_source,
      QStringLiteral("Linear sRGB"));
  if (sixteen_bit_working.isNull() || sixteen_bit_working.pixels.size() != 3 ||
      std::abs(sixteen_bit_working.pixels[0].red - 0.25F) > 0.005F ||
      std::abs(sixteen_bit_working.pixels[2].red - 0.875F) > 0.005F) {
    return fail("16-bit integer sources should convert into the float working buffer");
  }

  QImage half_float_source(3, 1, QImage::Format_RGBA16FPx4);
  half_float_source.setColorSpace(photo_editor::desktop::linear_srgb_color_space());
  auto* half_float_scanline = reinterpret_cast<QRgbaFloat16*>(half_float_source.scanLine(0));
  half_float_scanline[0] = QRgbaFloat16{qfloat16(0.2F), qfloat16(0.3F), qfloat16(0.4F), qfloat16(1.0F)};
  half_float_scanline[1] = QRgbaFloat16{qfloat16(1.25F), qfloat16(0.4F), qfloat16(0.2F), qfloat16(1.0F)};
  half_float_scanline[2] = QRgbaFloat16{qfloat16(0.1F), qfloat16(1.5F), qfloat16(0.2F), qfloat16(1.0F)};
  const auto half_float_working = photo_editor::desktop::working_image_from_qimage(
      half_float_source,
      QStringLiteral("Linear sRGB"));
  if (half_float_working.isNull() ||
      half_float_working.pixels[1].red < 1.20F ||
      half_float_working.pixels[2].green < 1.45F) {
    return fail("half-float sources should preserve over-range values in the float working buffer");
  }

  QImage float32_source(2, 1, QImage::Format_RGBA32FPx4);
  float32_source.setColorSpace(photo_editor::desktop::linear_srgb_color_space());
  auto* float32_scanline = reinterpret_cast<float*>(float32_source.scanLine(0));
  float32_scanline[0] = 0.125F;
  float32_scanline[1] = 0.25F;
  float32_scanline[2] = 0.5F;
  float32_scanline[3] = 1.0F;
  float32_scanline[4] = 1.75F;
  float32_scanline[5] = 0.5F;
  float32_scanline[6] = 0.25F;
  float32_scanline[7] = 1.0F;
  const auto float32_working = photo_editor::desktop::working_image_from_qimage(
      float32_source,
      QStringLiteral("Linear sRGB"));
  if (float32_working.isNull() ||
      std::abs(float32_working.pixels[0].red - 0.125F) > 0.005F ||
      float32_working.pixels[1].red < 1.70F) {
    return fail("float32 sources should preserve precision and over-range values in the working buffer");
  }

  const auto rendered_sixteen_bit = photo_editor::desktop::qimage_from_working_image(
      float32_working,
      photo_editor::desktop::linear_srgb_color_space(),
      QImage::Format_RGBA64,
      QStringLiteral("Linear sRGB"));
  if (rendered_sixteen_bit.isNull() || rendered_sixteen_bit.format() != QImage::Format_RGBA64 ||
      !rendered_sixteen_bit.colorSpace().isValid()) {
    return fail("float working buffers should render to 16-bit integer output images");
  }

  photo_editor::desktop::RenderEditState scene_linear_state;
  scene_linear_state.working_color_profile = QStringLiteral("Linear sRGB");
  scene_linear_state.preview_color_space = QStringLiteral("sRGB");
  scene_linear_state.output_color_space = QStringLiteral("sRGB");
  scene_linear_state.output_headroom = 2.0;
  QImage scene_linear_source(2, 1, QImage::Format_RGBA16FPx4);
  scene_linear_source.setColorSpace(photo_editor::desktop::linear_srgb_color_space());
  auto* scene_linear_scanline = reinterpret_cast<QRgbaFloat16*>(scene_linear_source.scanLine(0));
  scene_linear_scanline[0] =
      QRgbaFloat16{qfloat16(3.5F), qfloat16(3.5F), qfloat16(3.5F), qfloat16(1.0F)};
  scene_linear_scanline[1] =
      QRgbaFloat16{qfloat16(0.25F), qfloat16(0.25F), qfloat16(0.25F), qfloat16(1.0F)};
  const auto scene_linear_render = photo_editor::desktop::render_output_image(
      scene_linear_source,
      scene_linear_state,
      photo_editor::desktop::linear_srgb_color_space(),
      QImage::Format_RGBA16FPx4);
  if (scene_linear_render.isNull() ||
      scene_linear_render.colorSpace() != photo_editor::desktop::linear_srgb_color_space()) {
    return fail("scene-referred renders should keep a linear output color space when requested");
  }
  const auto* scene_render_scanline =
      reinterpret_cast<const QRgbaFloat16*>(scene_linear_render.constScanLine(0));
  if (static_cast<float>(scene_render_scanline[0].r) <= 1.0F ||
      static_cast<float>(scene_render_scanline[0].r) > 2.01F ||
      static_cast<float>(scene_render_scanline[1].r) >=
          static_cast<float>(scene_render_scanline[0].r)) {
    return fail("scene-referred HDR headroom should preserve over-range linear output without SDR clipping");
  }

  return EXIT_SUCCESS;
}
