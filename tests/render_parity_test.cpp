#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <utility>

#include <QBuffer>
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

constexpr int kPreviewExportParityTolerance = 2;
constexpr int kEncodedExportTolerance = 1;

template <typename Working>
concept ConvertsWorkingImageWithoutTarget = requires(const Working& working) {
  photo_editor::desktop::qimage_from_working_image(working);
};

template <typename Working>
concept ConvertsWorkingImageRectWithoutTarget = requires(
    const Working& working,
    const QRect& rect) {
  photo_editor::desktop::qimage_from_working_image_rect(working, rect);
};

static_assert(
    !ConvertsWorkingImageWithoutTarget<photo_editor::desktop::WorkingImage>,
    "working-buffer conversion must require an explicit target color space");

static_assert(
    !ConvertsWorkingImageRectWithoutTarget<photo_editor::desktop::WorkingImage>,
    "working-buffer rect conversion must require an explicit target color space");

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
  state.grading_midtones_hue = 8.0 / 360.0;
  state.grading_midtones_saturation = 5.0;
  return state;
}

photo_editor::desktop::EditRecipeState gpu_compatible_state() {
  auto state = deterministic_state();
  state.exposure = 0.0;
  state.contrast = 0.0;
  state.highlights = 0.0;
  state.shadows = 0.0;
  state.whites = 0.0;
  state.blacks = 0.0;
  state.brightness = 0.0;
  state.filmic_shoulder = 0.0;
  state.filmic_toe = 0.0;
  state.adaptive_contrast = 0.0;
  state.levels_black_point = 0.0;
  state.levels_gamma = 0.0;
  state.levels_white_point = 0.0;
  state.zone_deep_shadows = 0.0;
  state.zone_shadows = 0.0;
  state.zone_midtones = 0.0;
  state.zone_lights = 0.0;
  state.zone_highlights = 0.0;
  state.vibrance = 0.0;
  state.saturation = 0.0;
  state.hue_adjustment = 0.0;
  state.saturation_adjustment = 0.0;
  state.luminance_adjustment = 0.0;
  state.mixer_channels = {};
  state.rgb_channel_mixer = {};
  state.rgb_channel_mixer.red_from_red = 100.0;
  state.rgb_channel_mixer.green_from_green = 100.0;
  state.rgb_channel_mixer.blue_from_blue = 100.0;
  state.black_and_white_enabled = false;
  state.black_and_white_mixer = {};
  state.color_replacement_enabled = false;
  state.color_replacement_strength = 0.0;
  state.skin_tone_protection_enabled = false;
  state.skin_tone_protection_strength = 0.0;
  state.color_harmony_enabled = false;
  state.color_harmony_strength = 0.0;
  state.curve_shadows = 0.0;
  state.curve_darks = 0.0;
  state.curve_lights = 0.0;
  state.curve_highlights = 0.0;
  state.master_curve_points.clear();
  state.luma_curve_points.clear();
  state.grading_shadows_saturation = 0.0;
  state.grading_midtones_saturation = 0.0;
  state.grading_highlights_saturation = 0.0;
  state.grading_shadows_luminance = 0.0;
  state.grading_midtones_luminance = 0.0;
  state.grading_highlights_luminance = 0.0;
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
      const auto red_delta = std::abs(lhs.red() - rhs.red());
      const auto green_delta = std::abs(lhs.green() - rhs.green());
      const auto blue_delta = std::abs(lhs.blue() - rhs.blue());
      const auto pixel_delta = std::max({red_delta, green_delta, blue_delta});
      if (pixel_delta > max_delta) {
        max_delta = pixel_delta;
      }
    }
  }
  return max_delta;
}

QImage normalized_nearest_resample(const QImage& image, const QSize& target_size) {
  if (image.isNull() || target_size.isEmpty()) {
    return {};
  }
  QImage result(target_size, image.format());
  result.setColorSpace(image.colorSpace());
  for (int y = 0; y < result.height(); ++y) {
    const auto source_y =
        result.height() > 1
            ? static_cast<int>(std::lround(
                  static_cast<double>(y) * static_cast<double>(image.height() - 1) /
                  static_cast<double>(result.height() - 1)))
            : 0;
    for (int x = 0; x < result.width(); ++x) {
      const auto source_x =
          result.width() > 1
              ? static_cast<int>(std::lround(
                    static_cast<double>(x) * static_cast<double>(image.width() - 1) /
                    static_cast<double>(result.width() - 1)))
              : 0;
      result.setPixelColor(x, y, image.pixelColor(source_x, source_y));
    }
  }
  return result;
}

QByteArray normalized_mask_fixture(const QSize& size) {
  QByteArray mask(size.width() * size.height(), char(0));
  for (int y = 0; y < size.height(); ++y) {
    const auto normalized_y =
        size.height() > 1 ? static_cast<double>(y) / static_cast<double>(size.height() - 1) : 0.0;
    for (int x = 0; x < size.width(); ++x) {
      const auto normalized_x =
          size.width() > 1 ? static_cast<double>(x) / static_cast<double>(size.width() - 1) : 0.0;
      if (normalized_x >= 0.45 && normalized_x <= 1.0 &&
          normalized_y >= 0.25 && normalized_y <= 0.80) {
        mask[y * size.width() + x] = char(0xff);
      }
    }
  }
  return mask;
}

bool write_png(const QImage& image, const QString& path) {
  QImageWriter writer(path, "png");
  writer.setCompression(0);
  return writer.write(image);
}

bool write_image_as(const QImage& image, const QString& path, const char* format) {
  QImageWriter writer(path, format);
  if (std::string_view(format) == "jpg" || std::string_view(format) == "jpeg") {
    writer.setQuality(100);
  } else if (std::string_view(format) == "png") {
    writer.setCompression(0);
  }
  return writer.write(image);
}

QByteArray png_bytes(const QImage& image) {
  QByteArray bytes;
  QBuffer buffer(&bytes);
  if (!buffer.open(QIODevice::WriteOnly)) {
    return {};
  }
  QImageWriter writer(&buffer, "png");
  writer.setCompression(0);
  if (!writer.write(image)) {
    return {};
  }
  return bytes;
}

QImage deterministic_refinement_source_image() {
  QImage image(8, 8, QImage::Format_ARGB32);
  image.setColorSpace(photo_editor::desktop::preview_color_space());
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      image.setPixelColor(
          x,
          y,
          QColor(42 + x * 19, 36 + y * 18, 54 + ((x * 31 + y * 13) % 150), 255));
    }
  }
  return image;
}

QByteArray refinement_matte_data() {
  constexpr int kWidth = 8;
  constexpr int kHeight = 8;
  QByteArray matte(kWidth * kHeight, char(0));
  for (int y = 1; y <= 6; ++y) {
    for (int x = 2; x <= 5; ++x) {
      matte[y * kWidth + x] = char(0xff);
    }
  }
  for (int y = 2; y <= 5; ++y) {
    matte[y * kWidth + 1] = char(0x88);
  }
  matte[3 * kWidth + 3] = char(0);
  matte[0 * kWidth + 7] = char(0xff);
  return matte;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  const auto source = deterministic_source_image();
  const auto state = deterministic_state();
  const auto target_space = photo_editor::desktop::preview_color_space();

  QImage exact_pixel_source(4, 4, QImage::Format_ARGB32);
  exact_pixel_source.setColorSpace(target_space);
  exact_pixel_source.fill(QColor(88, 88, 88, 255));
  photo_editor::desktop::EditRecipeState exact_pixel_state;
  exact_pixel_state.working_color_profile = QStringLiteral("Linear sRGB");
  exact_pixel_state.preview_color_space = QStringLiteral("sRGB");
  exact_pixel_state.output_color_space = QStringLiteral("sRGB");
  QVariantMap exact_pixel_mask;
  exact_pixel_mask.insert(QStringLiteral("id"), QStringLiteral("exact-pixel-alpha-render"));
  exact_pixel_mask.insert(QStringLiteral("name"), QStringLiteral("Exact Pixel Alpha Render"));
  exact_pixel_mask.insert(QStringLiteral("kind"), QStringLiteral("alpha"));
  exact_pixel_mask.insert(QStringLiteral("toolRole"), QStringLiteral("selection"));
  exact_pixel_mask.insert(QStringLiteral("maskSetId"), QStringLiteral("exact-pixel-render-set"));
  exact_pixel_mask.insert(QStringLiteral("operation"), QStringLiteral("add"));
  exact_pixel_mask.insert(QStringLiteral("exposure"), 1.0);
  exact_pixel_mask.insert(QStringLiteral("feather"), 0.0);
  exact_pixel_mask.insert(QStringLiteral("selectionExpand"), 0.0);
  exact_pixel_mask.insert(QStringLiteral("selectionSmooth"), 0.0);
  exact_pixel_mask.insert(QStringLiteral("selectionFeather"), 0.0);
  exact_pixel_mask.insert(QStringLiteral("selectionBlur"), 0.0);
  exact_pixel_mask.insert(QStringLiteral("selectionGrow"), 0.0);
  exact_pixel_mask.insert(QStringLiteral("selectionShrink"), 0.0);
  exact_pixel_mask.insert(QStringLiteral("selectionDespeckle"), 0.0);
  exact_pixel_mask.insert(QStringLiteral("selectionFillHoles"), 0.0);
  exact_pixel_mask.insert(QStringLiteral("selectionRemoveIslands"), 0.0);
  QByteArray exact_pixel_alpha(16, char(0));
  exact_pixel_alpha[5] = static_cast<char>(0xff);
  exact_pixel_mask.insert(QStringLiteral("segmentationData"), exact_pixel_alpha);
  exact_pixel_mask.insert(QStringLiteral("segmentationWidth"), 4);
  exact_pixel_mask.insert(QStringLiteral("segmentationHeight"), 4);
  exact_pixel_state.mask_stack.append(exact_pixel_mask);
  const auto exact_pixel_neutral = photo_editor::desktop::render_output_image_with_backend(
      exact_pixel_source,
      photo_editor::desktop::EditRecipeState{},
      target_space,
      QImage::Format_ARGB32,
      photo_editor::desktop::RenderBackendPreference::CpuOnly);
  const auto exact_pixel_render = photo_editor::desktop::render_output_image_with_backend(
      exact_pixel_source,
      exact_pixel_state,
      target_space,
      QImage::Format_ARGB32,
      photo_editor::desktop::RenderBackendPreference::CpuOnly);
  if (exact_pixel_neutral.isNull() || exact_pixel_render.isNull()) {
    return fail("exact pixel alpha render should produce source-sized images");
  }
  const auto selected_before = exact_pixel_neutral.pixelColor(1, 1);
  const auto selected_after = exact_pixel_render.pixelColor(1, 1);
  if (selected_after.red() <= selected_before.red() + 16 ||
      selected_after.green() <= selected_before.green() + 16 ||
      selected_after.blue() <= selected_before.blue() + 16) {
    return fail("exact pixel alpha render should visibly affect the selected source pixel");
  }
  for (const auto& neighbor : {
           QPoint(0, 1),
           QPoint(2, 1),
           QPoint(1, 0),
           QPoint(1, 2),
           QPoint(0, 0),
           QPoint(2, 2),
       }) {
    const auto before = exact_pixel_neutral.pixelColor(neighbor);
    const auto after = exact_pixel_render.pixelColor(neighbor);
    if (std::abs(after.red() - before.red()) > 2 ||
        std::abs(after.green() - before.green()) > 2 ||
        std::abs(after.blue() - before.blue()) > 2) {
      return fail("exact pixel alpha render should not affect neighboring source pixels");
    }
  }

  QImage photo_brush_source(9, 9, QImage::Format_ARGB32);
  photo_brush_source.setColorSpace(target_space);
  photo_brush_source.fill(QColor(88, 88, 88, 255));
  photo_editor::desktop::EditRecipeState photo_brush_state;
  photo_brush_state.working_color_profile = QStringLiteral("Linear sRGB");
  photo_brush_state.preview_color_space = QStringLiteral("sRGB");
  photo_brush_state.output_color_space = QStringLiteral("sRGB");
  QVariantMap photo_brush_mask;
  photo_brush_mask.insert(QStringLiteral("id"), QStringLiteral("photo-brush-falloff-render"));
  photo_brush_mask.insert(QStringLiteral("name"), QStringLiteral("Photo Brush Falloff Render"));
  photo_brush_mask.insert(QStringLiteral("kind"), QStringLiteral("brush"));
  photo_brush_mask.insert(QStringLiteral("toolRole"), QStringLiteral("selection"));
  photo_brush_mask.insert(QStringLiteral("maskSetId"), QStringLiteral("photo-brush-falloff-set"));
  photo_brush_mask.insert(QStringLiteral("operation"), QStringLiteral("add"));
  photo_brush_mask.insert(QStringLiteral("exposure"), 1.0);
  photo_brush_mask.insert(QStringLiteral("brushHardness"), 0.20);
  photo_brush_mask.insert(QStringLiteral("brushBuildup"), false);
  photo_brush_mask.insert(QStringLiteral("feather"), 0.0);
  photo_brush_mask.insert(QStringLiteral("selectionExpand"), 0.0);
  photo_brush_mask.insert(QStringLiteral("selectionSmooth"), 0.0);
  photo_brush_mask.insert(QStringLiteral("selectionFeather"), 0.0);
  photo_brush_mask.insert(QStringLiteral("selectionBlur"), 0.0);
  photo_brush_mask.insert(QStringLiteral("selectionGrow"), 0.0);
  photo_brush_mask.insert(QStringLiteral("selectionShrink"), 0.0);
  photo_brush_mask.insert(QStringLiteral("selectionDespeckle"), 0.0);
  photo_brush_mask.insert(QStringLiteral("selectionFillHoles"), 0.0);
  photo_brush_mask.insert(QStringLiteral("selectionRemoveIslands"), 0.0);
  QVariantMap photo_brush_point;
  photo_brush_point.insert(QStringLiteral("x"), 0.5);
  photo_brush_point.insert(QStringLiteral("y"), 0.5);
  photo_brush_point.insert(QStringLiteral("size"), 0.45);
  photo_brush_point.insert(QStringLiteral("flow"), 1.0);
  photo_brush_point.insert(QStringLiteral("opacity"), 1.0);
  photo_brush_mask.insert(QStringLiteral("brushPoints"), QVariantList{photo_brush_point});
  photo_brush_state.mask_stack.append(photo_brush_mask);
  const auto photo_brush_neutral = photo_editor::desktop::render_output_image_with_backend(
      photo_brush_source,
      photo_editor::desktop::EditRecipeState{},
      target_space,
      QImage::Format_ARGB32,
      photo_editor::desktop::RenderBackendPreference::CpuOnly);
  const auto photo_brush_render = photo_editor::desktop::render_output_image_with_backend(
      photo_brush_source,
      photo_brush_state,
      target_space,
      QImage::Format_ARGB32,
      photo_editor::desktop::RenderBackendPreference::CpuOnly,
      nullptr,
      nullptr,
      photo_editor::desktop::RenderMaskOptions{
          .rebuild_editable_masks_at_output_resolution = true});
  if (photo_brush_neutral.isNull() || photo_brush_render.isNull()) {
    return fail("photo brush falloff render should produce source-sized images");
  }
  const auto brush_base = photo_brush_neutral.pixelColor(4, 4).red();
  const auto brush_center_delta = photo_brush_render.pixelColor(4, 4).red() - brush_base;
  const auto brush_near_delta = photo_brush_render.pixelColor(6, 4).red() -
                                photo_brush_neutral.pixelColor(6, 4).red();
  const auto brush_far_delta = photo_brush_render.pixelColor(8, 4).red() -
                               photo_brush_neutral.pixelColor(8, 4).red();
  if (brush_center_delta <= 16 ||
      brush_near_delta <= 4 ||
      brush_near_delta >= brush_center_delta - 4 ||
      std::abs(brush_far_delta) > 2) {
    const auto detail =
        QStringLiteral(
            "normal photo brush render should produce smooth falloff, unlike exact pixel selection "
            "(center=%1 near=%2 far=%3 base=%4)")
            .arg(brush_center_delta)
            .arg(brush_near_delta)
            .arg(brush_far_delta)
            .arg(brush_base)
            .toUtf8();
    return fail(detail.constData());
  }

  QImage crop_rotation_mask_source(32, 24, QImage::Format_ARGB32);
  crop_rotation_mask_source.setColorSpace(target_space);
  crop_rotation_mask_source.fill(QColor(74, 74, 74, 255));
  for (int y = 9; y <= 15; ++y) {
    for (int x = 13; x <= 19; ++x) {
      crop_rotation_mask_source.setPixelColor(x, y, QColor(220, 42, 38, 255));
    }
  }
  photo_editor::desktop::EditRecipeState crop_rotation_mask_state;
  crop_rotation_mask_state.working_color_profile = QStringLiteral("Linear sRGB");
  crop_rotation_mask_state.preview_color_space = QStringLiteral("sRGB");
  crop_rotation_mask_state.output_color_space = QStringLiteral("sRGB");
  crop_rotation_mask_state.transform_rotation = 7.5;
  crop_rotation_mask_state.crop_rect_x = 0.125;
  crop_rotation_mask_state.crop_rect_y = 0.125;
  crop_rotation_mask_state.crop_rect_width = 0.75;
  crop_rotation_mask_state.crop_rect_height = 0.75;
  QVariantMap crop_rotation_shape_mask;
  crop_rotation_shape_mask.insert(QStringLiteral("id"), QStringLiteral("crop-rotation-shape-source"));
  crop_rotation_shape_mask.insert(QStringLiteral("name"), QStringLiteral("Crop Rotation Shape Source"));
  crop_rotation_shape_mask.insert(QStringLiteral("kind"), QStringLiteral("rectangle"));
  crop_rotation_shape_mask.insert(QStringLiteral("toolRole"), QStringLiteral("selection"));
  crop_rotation_shape_mask.insert(QStringLiteral("maskSetId"), QStringLiteral("crop-rotation-shape-range-set"));
  crop_rotation_shape_mask.insert(QStringLiteral("maskSetName"), QStringLiteral("Crop Rotation Shape Range"));
  crop_rotation_shape_mask.insert(QStringLiteral("operation"), QStringLiteral("add"));
  crop_rotation_shape_mask.insert(QStringLiteral("exposure"), 0.95);
  crop_rotation_shape_mask.insert(QStringLiteral("centerX"), 0.29);
  crop_rotation_shape_mask.insert(QStringLiteral("centerY"), 0.52);
  crop_rotation_shape_mask.insert(QStringLiteral("radiusX"), 0.09);
  crop_rotation_shape_mask.insert(QStringLiteral("radiusY"), 0.17);
  crop_rotation_shape_mask.insert(QStringLiteral("rotation"), 18.0);
  crop_rotation_shape_mask.insert(QStringLiteral("feather"), 0.0);
  crop_rotation_shape_mask.insert(QStringLiteral("selectionFeather"), 0.0);
  crop_rotation_shape_mask.insert(QStringLiteral("selectionSmooth"), 0.0);
  QVariantMap crop_rotation_color_mask;
  crop_rotation_color_mask.insert(QStringLiteral("id"), QStringLiteral("crop-rotation-range-source"));
  crop_rotation_color_mask.insert(QStringLiteral("name"), QStringLiteral("Crop Rotation Range Source"));
  crop_rotation_color_mask.insert(QStringLiteral("kind"), QStringLiteral("color"));
  crop_rotation_color_mask.insert(QStringLiteral("toolRole"), QStringLiteral("selection"));
  crop_rotation_color_mask.insert(QStringLiteral("maskSetId"), QStringLiteral("crop-rotation-shape-range-set"));
  crop_rotation_color_mask.insert(QStringLiteral("maskSetName"), QStringLiteral("Crop Rotation Shape Range"));
  crop_rotation_color_mask.insert(QStringLiteral("operation"), QStringLiteral("add"));
  crop_rotation_color_mask.insert(QStringLiteral("hueCenter"), 0.0);
  crop_rotation_color_mask.insert(QStringLiteral("hueRange"), 0.50);
  crop_rotation_color_mask.insert(QStringLiteral("saturationMin"), 0.45);
  crop_rotation_color_mask.insert(QStringLiteral("saturationMax"), 1.0);
  crop_rotation_color_mask.insert(QStringLiteral("rangeFeather"), 0.0);
  crop_rotation_mask_state.mask_stack.append(crop_rotation_shape_mask);
  crop_rotation_mask_state.mask_stack.append(crop_rotation_color_mask);

  auto crop_rotation_neutral_state = crop_rotation_mask_state;
  crop_rotation_neutral_state.mask_stack.clear();
  const auto crop_rotation_neutral_full = photo_editor::desktop::render_output_image_with_backend(
      crop_rotation_mask_source,
      crop_rotation_neutral_state,
      target_space,
      QImage::Format_ARGB32,
      photo_editor::desktop::RenderBackendPreference::CpuOnly);
  const auto crop_rotation_preview_full = photo_editor::desktop::render_output_image_with_backend(
      crop_rotation_mask_source,
      crop_rotation_mask_state,
      target_space,
      QImage::Format_ARGB32,
      photo_editor::desktop::RenderBackendPreference::CpuOnly);
  const auto crop_rotation_export_full = photo_editor::desktop::render_output_image_with_backend(
      crop_rotation_mask_source,
      crop_rotation_mask_state,
      target_space,
      QImage::Format_RGBA64,
      photo_editor::desktop::RenderBackendPreference::CpuOnly);
  if (crop_rotation_neutral_full.isNull() ||
      crop_rotation_preview_full.isNull() ||
      crop_rotation_export_full.isNull()) {
    return fail("crop/rotation shape and range render proof should produce rendered images");
  }
  const auto crop_rotation_neutral =
      photo_editor::desktop::apply_crop(crop_rotation_neutral_full, crop_rotation_mask_state);
  const auto crop_rotation_preview =
      photo_editor::desktop::apply_crop(crop_rotation_preview_full, crop_rotation_mask_state);
  const auto crop_rotation_export =
      photo_editor::desktop::apply_crop(
          crop_rotation_export_full,
          crop_rotation_mask_state).convertToFormat(QImage::Format_ARGB32);
  if (crop_rotation_preview.size() != QSize(24, 18) ||
      crop_rotation_export.size() != crop_rotation_preview.size() ||
      max_channel_delta(crop_rotation_preview, crop_rotation_export) > kPreviewExportParityTolerance) {
    return fail("cropped shape/range mask preview should match full-resolution export under rotation");
  }
  const auto crop_rotation_delta_at = [&](const QImage& before, const QImage& after, const QPoint& point) {
    const auto before_color = before.pixelColor(point);
    const auto after_color = after.pixelColor(point);
    return std::max({
        std::abs(after_color.red() - before_color.red()),
        std::abs(after_color.green() - before_color.green()),
        std::abs(after_color.blue() - before_color.blue())});
  };
  const auto shape_crop_delta =
      crop_rotation_delta_at(crop_rotation_neutral, crop_rotation_preview, QPoint(5, 9));
  const auto range_crop_delta =
      crop_rotation_delta_at(crop_rotation_neutral, crop_rotation_preview, QPoint(12, 9));
  const auto unselected_crop_delta =
      crop_rotation_delta_at(crop_rotation_neutral, crop_rotation_preview, QPoint(21, 15));
  if (shape_crop_delta < 12 ||
      range_crop_delta < 12 ||
      unselected_crop_delta > kPreviewExportParityTolerance) {
    const auto detail =
        QStringLiteral(
            "shape and color range masks should stay aligned under render rotation and crop (shape=%1 range=%2 unselected=%3)")
            .arg(shape_crop_delta)
            .arg(range_crop_delta)
            .arg(unselected_crop_delta)
            .toUtf8();
    return fail(detail.constData());
  }

  const auto compiled_gpu_support = photo_editor::desktop::evaluate_gpu_pipeline_support(
      source,
      gpu_compatible_state(),
      nullptr,
      {},
      true);
  if (compiled_gpu_support.fallback_reason == QStringLiteral("metal_not_compiled")) {
    if (compiled_gpu_support.supported || compiled_gpu_support.metal_aware_cpu_engine) {
      return fail("uncompiled Metal support should report only metal_not_compiled");
    }
  } else {
    if (!compiled_gpu_support.supported ||
        compiled_gpu_support.metal_aware_cpu_engine ||
        !compiled_gpu_support.fallback_reason.isEmpty()) {
      return fail("GPU-compatible color pipeline should be reported as supported");
    }

    const auto unavailable_gpu_support = photo_editor::desktop::evaluate_gpu_pipeline_support(
        source,
        gpu_compatible_state(),
        nullptr,
        {},
        false);
    if (unavailable_gpu_support.supported ||
        unavailable_gpu_support.fallback_reason != QStringLiteral("metal_unavailable")) {
      return fail("GPU support query should report metal_unavailable when Metal is unavailable");
    }

    auto local_mask_set_state = gpu_compatible_state();
    QVariantMap local_mask_set;
    local_mask_set.insert(QStringLiteral("id"), QStringLiteral("gpu-support-local-mask"));
    local_mask_set.insert(QStringLiteral("name"), QStringLiteral("GPU Support Local Mask"));
    local_mask_set.insert(QStringLiteral("kind"), QStringLiteral("radial"));
    local_mask_set.insert(QStringLiteral("toolRole"), QStringLiteral("selection"));
    local_mask_set.insert(QStringLiteral("maskSetId"), QStringLiteral("gpu-support-mask-set"));
    local_mask_set.insert(QStringLiteral("operation"), QStringLiteral("add"));
    local_mask_set.insert(QStringLiteral("exposure"), 0.15);
    local_mask_set.insert(QStringLiteral("centerX"), 0.5);
    local_mask_set.insert(QStringLiteral("centerY"), 0.5);
    local_mask_set.insert(QStringLiteral("radiusX"), 0.35);
    local_mask_set.insert(QStringLiteral("radiusY"), 0.35);
    local_mask_set_state.mask_stack.append(local_mask_set);
    const auto local_mask_set_support = photo_editor::desktop::evaluate_gpu_pipeline_support(
        source,
        local_mask_set_state);
    if (local_mask_set_support.supported ||
        !local_mask_set_support.metal_aware_cpu_engine ||
        local_mask_set_support.fallback_reason != QStringLiteral("local_mask_sets")) {
      return fail("GPU support query should report local mask sets as a Metal-aware CPU fallback");
    }

    auto retouch_state = gpu_compatible_state();
    QVariantMap retouch_mask;
    retouch_mask.insert(QStringLiteral("id"), QStringLiteral("gpu-support-retouch"));
    retouch_mask.insert(QStringLiteral("name"), QStringLiteral("GPU Support Retouch"));
    retouch_mask.insert(QStringLiteral("kind"), QStringLiteral("brush"));
    retouch_mask.insert(QStringLiteral("toolRole"), QStringLiteral("retouch"));
    retouch_mask.insert(QStringLiteral("operation"), QStringLiteral("add"));
    retouch_mask.insert(QStringLiteral("retouchMode"), QStringLiteral("remove"));
    retouch_mask.insert(QStringLiteral("committed"), true);
    retouch_state.mask_stack.append(retouch_mask);
    if (photo_editor::desktop::evaluate_gpu_pipeline_support(source, retouch_state)
            .fallback_reason != QStringLiteral("retouch_masks")) {
      return fail("GPU support query should report retouch masks as a CPU fallback");
    }

    auto legacy_mask_state = gpu_compatible_state();
    legacy_mask_state.linear_mask_enabled = true;
    legacy_mask_state.linear_mask_exposure = 0.25;
    if (photo_editor::desktop::evaluate_gpu_pipeline_support(source, legacy_mask_state)
            .fallback_reason != QStringLiteral("legacy_local_masks")) {
      return fail("GPU support query should report legacy linear/radial masks as a CPU fallback");
    }

    QImage half_float_source(2, 1, QImage::Format_RGBA16FPx4);
    half_float_source.setColorSpace(photo_editor::desktop::preview_color_space());
    auto* half_float_scanline = reinterpret_cast<QRgbaFloat16*>(half_float_source.scanLine(0));
    half_float_scanline[0] =
        QRgbaFloat16{qfloat16(1.25F), qfloat16(0.30F), qfloat16(0.20F), qfloat16(1.0F)};
    half_float_scanline[1] =
        QRgbaFloat16{qfloat16(0.20F), qfloat16(0.30F), qfloat16(0.40F), qfloat16(1.0F)};
    if (photo_editor::desktop::evaluate_gpu_pipeline_support(half_float_source, gpu_compatible_state())
            .fallback_reason != QStringLiteral("extended_range_pixels")) {
      return fail("GPU support query should report extended-range source pixels as a CPU fallback");
    }

    auto exact_tone_state = gpu_compatible_state();
    exact_tone_state.exposure = 0.10;
    if (photo_editor::desktop::evaluate_gpu_pipeline_support(source, exact_tone_state)
            .fallback_reason != QStringLiteral("exact_global_tone")) {
      return fail("GPU support query should report exact global tone as a CPU fallback");
    }

    auto exact_post_color_state = gpu_compatible_state();
    exact_post_color_state.saturation = 12.0;
    if (photo_editor::desktop::evaluate_gpu_pipeline_support(source, exact_post_color_state)
            .fallback_reason != QStringLiteral("exact_post_color")) {
      return fail("GPU support query should report exact post-color edits as a CPU fallback");
    }

    auto cpu_stage_state = gpu_compatible_state();
    cpu_stage_state.texture = 6.0;
    if (photo_editor::desktop::evaluate_gpu_pipeline_support(source, cpu_stage_state)
            .fallback_reason != QStringLiteral("cpu_required_stages")) {
      return fail("GPU support query should report CPU-required stages as a CPU fallback");
    }

    auto wide_gamut_gpu_state = gpu_compatible_state();
    wide_gamut_gpu_state.working_color_profile = QStringLiteral("Linear ProPhoto RGB");
    const auto wide_gamut_support =
        photo_editor::desktop::evaluate_gpu_pipeline_support(source, wide_gamut_gpu_state);
    if (!wide_gamut_support.supported || !wide_gamut_support.fallback_reason.isEmpty()) {
      return fail("wide-gamut GPU-compatible recipes should be supported by the LUT-backed Metal path");
    }
  }

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
  const auto auto_preview_delta = max_channel_delta(cpu_preview, auto_preview);
  if (auto_preview_delta > kPreviewExportParityTolerance) {
    std::cerr << "auto preview max channel delta: " << auto_preview_delta << '\n';
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
  const auto hdr_local_delta = max_channel_delta(hdr_local_cpu_preview, hdr_local_auto_preview);
  if (hdr_local_delta > kPreviewExportParityTolerance) {
    std::cerr << "HDR local tone max channel delta: " << hdr_local_delta << '\n';
    return fail("auto preview should match CPU preview for HDR local tone mapping");
  }

  QImage final_mask_rebuild_source(32, 32, QImage::Format_ARGB32);
  final_mask_rebuild_source.setColorSpace(photo_editor::desktop::preview_color_space());
  final_mask_rebuild_source.fill(QColor::fromRgb(64, 64, 64));
  photo_editor::desktop::RenderEditState final_mask_rebuild_state;
  final_mask_rebuild_state.working_color_profile = QStringLiteral("Linear sRGB");
  final_mask_rebuild_state.preview_color_space = QStringLiteral("sRGB");
  final_mask_rebuild_state.output_color_space = QStringLiteral("sRGB");
  QVariantMap final_rebuild_brush;
  final_rebuild_brush.insert(QStringLiteral("id"), QStringLiteral("final-rebuild-brush"));
  final_rebuild_brush.insert(QStringLiteral("name"), QStringLiteral("Final rebuild brush"));
  final_rebuild_brush.insert(QStringLiteral("kind"), QStringLiteral("brush"));
  final_rebuild_brush.insert(QStringLiteral("toolRole"), QStringLiteral("selection"));
  final_rebuild_brush.insert(QStringLiteral("maskSetId"), QStringLiteral("final-rebuild-set"));
  final_rebuild_brush.insert(QStringLiteral("maskSetName"), QStringLiteral("Final rebuild set"));
  final_rebuild_brush.insert(QStringLiteral("operation"), QStringLiteral("add"));
  final_rebuild_brush.insert(QStringLiteral("exposure"), 1.0);
  final_rebuild_brush.insert(QStringLiteral("brushSize"), 0.18);
  final_rebuild_brush.insert(QStringLiteral("brushFlow"), 1.0);
  final_rebuild_brush.insert(QStringLiteral("brushHardness"), 0.7);
  final_rebuild_brush.insert(QStringLiteral("segmentationData"), QByteArray(4, char(0)));
  final_rebuild_brush.insert(QStringLiteral("segmentationWidth"), 2);
  final_rebuild_brush.insert(QStringLiteral("segmentationHeight"), 2);
  QVariantMap final_rebuild_point;
  final_rebuild_point.insert(QStringLiteral("x"), 0.5);
  final_rebuild_point.insert(QStringLiteral("y"), 0.5);
  final_rebuild_point.insert(QStringLiteral("size"), 0.18);
  final_rebuild_point.insert(QStringLiteral("flow"), 1.0);
  final_rebuild_brush.insert(QStringLiteral("brushPoints"), QVariantList{final_rebuild_point});
  final_mask_rebuild_state.mask_stack.append(final_rebuild_brush);

  const auto stale_preview_matte_render = photo_editor::desktop::render_output_image_with_backend(
      final_mask_rebuild_source,
      final_mask_rebuild_state,
      photo_editor::desktop::preview_color_space(),
      QImage::Format_ARGB32,
      photo_editor::desktop::RenderBackendPreference::CpuOnly);
  const auto rebuilt_final_matte_render = photo_editor::desktop::render_output_image_with_backend(
      final_mask_rebuild_source,
      final_mask_rebuild_state,
      photo_editor::desktop::preview_color_space(),
      QImage::Format_RGBA64,
      photo_editor::desktop::RenderBackendPreference::CpuOnly,
      nullptr,
      nullptr,
      photo_editor::desktop::RenderMaskOptions{
          .rebuild_editable_masks_at_output_resolution = true});
  if (stale_preview_matte_render.pixelColor(16, 16).red() >
          stale_preview_matte_render.pixelColor(0, 0).red() + 4 ||
      rebuilt_final_matte_render.pixelColor(16, 16).red() <=
          rebuilt_final_matte_render.pixelColor(0, 0).red() + 16) {
    return fail("final-resolution brush mask rebuild should ignore stale preview-resolution mattes");
  }

  const auto gpu_state = gpu_compatible_state();
  const auto cpu_full_resolution = photo_editor::desktop::render_output_image_with_backend(
      source,
      gpu_state,
      target_space,
      QImage::Format_RGBA64,
      photo_editor::desktop::RenderBackendPreference::CpuOnly);
  if (cpu_full_resolution.isNull()) {
    return fail("CPU full-resolution render should produce an image");
  }

  const auto gpu_backend_available = photo_editor::desktop::render_backend_available(
      photo_editor::desktop::RenderBackendPreference::GpuOnly);
  if (!gpu_backend_available) {
    const auto reason = photo_editor::desktop::render_backend_unavailable_reason(
        photo_editor::desktop::RenderBackendPreference::GpuOnly);
    std::cerr << "GPU render backend unavailable; skipping only CPU/Metal parity coverage on this host";
    if (!reason.isEmpty()) {
      std::cerr << ": " << reason.toStdString();
    }
    std::cerr << '\n';
  } else {
    const auto gpu_cpu_preview = photo_editor::desktop::render_output_image_with_backend(
        source,
        gpu_state,
        target_space,
        QImage::Format_ARGB32,
        photo_editor::desktop::RenderBackendPreference::CpuOnly);
    if (gpu_cpu_preview.isNull()) {
      return fail("CPU preview render should produce an image for the GPU-compatible recipe");
    }
    const auto gpu_preview = photo_editor::desktop::render_output_image_with_backend(
        source,
        gpu_state,
        target_space,
        QImage::Format_ARGB32,
        photo_editor::desktop::RenderBackendPreference::GpuOnly);
    if (gpu_preview.isNull()) {
      return fail("GPU preview render should produce an image for the deterministic recipe");
    }
    const auto gpu_preview_delta = max_channel_delta(gpu_cpu_preview, gpu_preview);
    constexpr int kGpuColorGradingPreviewTolerance = 10;
    if (gpu_preview_delta > kGpuColorGradingPreviewTolerance) {
      std::cerr << "GPU preview max channel delta: " << gpu_preview_delta << '\n';
      return fail("GPU preview render should match CPU preview render within tolerance");
    }

    const auto gpu_full_resolution = photo_editor::desktop::render_output_image_with_backend(
        source,
        gpu_state,
        target_space,
        QImage::Format_RGBA64,
        photo_editor::desktop::RenderBackendPreference::GpuOnly);
    if (gpu_full_resolution.isNull()) {
      return fail("GPU full-resolution render should produce an image");
    }
    const auto gpu_full_resolution_delta = max_channel_delta(
        cpu_full_resolution.convertToFormat(QImage::Format_ARGB32),
        gpu_full_resolution.convertToFormat(QImage::Format_ARGB32));
    if (gpu_full_resolution_delta > kGpuColorGradingPreviewTolerance) {
      std::cerr << "GPU full-resolution max channel delta: " << gpu_full_resolution_delta << '\n';
      return fail("GPU full-resolution render should match CPU full-resolution render within tolerance");
    }

    auto wide_gamut_gpu_state = gpu_compatible_state();
    wide_gamut_gpu_state.working_color_profile = QStringLiteral("Linear ProPhoto RGB");
    const auto wide_gamut_cpu_preview = photo_editor::desktop::render_output_image_with_backend(
        source,
        wide_gamut_gpu_state,
        target_space,
        QImage::Format_ARGB32,
        photo_editor::desktop::RenderBackendPreference::CpuOnly);
    const auto wide_gamut_gpu_preview = photo_editor::desktop::render_output_image_with_backend(
        source,
        wide_gamut_gpu_state,
        target_space,
        QImage::Format_ARGB32,
        photo_editor::desktop::RenderBackendPreference::GpuOnly);
    if (wide_gamut_cpu_preview.isNull() || wide_gamut_gpu_preview.isNull()) {
      return fail("wide-gamut CPU/Metal parity renders should produce images");
    }
    const auto wide_gamut_delta = max_channel_delta(wide_gamut_cpu_preview, wide_gamut_gpu_preview);
    if (wide_gamut_delta > kGpuColorGradingPreviewTolerance) {
      std::cerr << "wide-gamut GPU preview max channel delta: " << wide_gamut_delta << '\n';
      return fail("wide-gamut GPU-compatible preview should match CPU preview within tolerance");
    }

    auto local_mask_grading_state = gpu_compatible_state();
    local_mask_grading_state.grading_midtones_hue = 0.62;
    local_mask_grading_state.grading_midtones_saturation = 58.0;
    QVariantMap local_mask;
    local_mask.insert(QStringLiteral("id"), QStringLiteral("gpu-local-mask-set-source"));
    local_mask.insert(QStringLiteral("name"), QStringLiteral("GPU Local Mask Set Source"));
    local_mask.insert(QStringLiteral("kind"), QStringLiteral("radial"));
    local_mask.insert(QStringLiteral("toolRole"), QStringLiteral("selection"));
    local_mask.insert(QStringLiteral("maskSetId"), QStringLiteral("gpu-local-mask-set"));
    local_mask.insert(QStringLiteral("maskSetName"), QStringLiteral("GPU Local Mask Set"));
    local_mask.insert(QStringLiteral("operation"), QStringLiteral("add"));
    local_mask.insert(QStringLiteral("exposure"), 0.35);
    local_mask.insert(QStringLiteral("centerX"), 0.5);
    local_mask.insert(QStringLiteral("centerY"), 0.5);
    local_mask.insert(QStringLiteral("radiusX"), 0.38);
    local_mask.insert(QStringLiteral("radiusY"), 0.32);
    local_mask.insert(QStringLiteral("feather"), 0.35);
    local_mask_grading_state.mask_stack.append(local_mask);
    photo_editor::desktop::RenderBackendTelemetry local_mask_grading_telemetry;
    const auto local_mask_grading_gpu_preview =
        photo_editor::desktop::render_output_image_with_backend(
            source,
            local_mask_grading_state,
            target_space,
            QImage::Format_ARGB32,
            photo_editor::desktop::RenderBackendPreference::GpuOnly,
            nullptr,
            &local_mask_grading_telemetry);
    if (local_mask_grading_gpu_preview.isNull()) {
      return fail("GPU-only render should allow Metal-aware local mask set grading");
    }
    if (local_mask_grading_telemetry.backend != QStringLiteral("metal") ||
        !local_mask_grading_telemetry.fallback_reason.isEmpty()) {
      std::cerr << "local mask grading backend: "
                << local_mask_grading_telemetry.backend.toStdString()
                << " fallback: "
                << local_mask_grading_telemetry.fallback_reason.toStdString()
                << '\n';
      return fail("local mask set grading should be reported as Metal when GPU stages run");
    }
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
          exported.convertToFormat(QImage::Format_ARGB32)) > kEncodedExportTolerance) {
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
            kPreviewExportParityTolerance) {
          std::cerr << label << " high-risk preview should match full-resolution export render\n";
          return false;
        }
        if (max_channel_delta(neutral_preview, preview_render) < minimum_visible_delta) {
          std::cerr << label << " high-risk recipe should visibly affect the parity source\n";
          return false;
        }
        return true;
      };

  auto global_parity_case_count = 0;
  const auto verify_global_edit_parity =
      [&](const char* label, const photo_editor::desktop::EditRecipeState& high_risk_state, int minimum_visible_delta) {
        ++global_parity_case_count;
        return verify_high_risk_parity(label, high_risk_state, minimum_visible_delta);
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
  if (!verify_global_edit_parity("white balance and tone", white_balance_tone_state, 8)) {
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
  if (!verify_global_edit_parity("tone curve", tone_curve_state, 5)) {
    return EXIT_FAILURE;
  }

  auto rgb_point_curve_state = neutral_state;
  rgb_point_curve_state.master_curve_points = {
      QVariantMap{{QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.0}},
      QVariantMap{{QStringLiteral("x"), 0.24}, {QStringLiteral("y"), 0.16}},
      QVariantMap{{QStringLiteral("x"), 0.68}, {QStringLiteral("y"), 0.80}},
      QVariantMap{{QStringLiteral("x"), 1.0}, {QStringLiteral("y"), 1.0}},
  };
  if (!verify_global_edit_parity("RGB point curve", rgb_point_curve_state, 5)) {
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
  if (!verify_global_edit_parity("HSL and selective color", hsl_selective_state, 5)) {
    return EXIT_FAILURE;
  }

  auto channel_mixer_state = neutral_state;
  channel_mixer_state.rgb_channel_mixer.red_from_red = 78.0;
  channel_mixer_state.rgb_channel_mixer.red_from_green = 24.0;
  channel_mixer_state.rgb_channel_mixer.green_from_red = -18.0;
  channel_mixer_state.rgb_channel_mixer.green_from_green = 118.0;
  channel_mixer_state.rgb_channel_mixer.blue_from_green = 22.0;
  channel_mixer_state.rgb_channel_mixer.blue_from_blue = 84.0;
  if (!verify_global_edit_parity("RGB channel mixer", channel_mixer_state, 5)) {
    return EXIT_FAILURE;
  }

  auto black_and_white_state = neutral_state;
  black_and_white_state.black_and_white_enabled = true;
  black_and_white_state.black_and_white_mixer[0] = 32.0;
  black_and_white_state.black_and_white_mixer[2] = -18.0;
  black_and_white_state.black_and_white_mixer[5] = 24.0;
  if (!verify_global_edit_parity("black and white mixer", black_and_white_state, 8)) {
    return EXIT_FAILURE;
  }

  auto advanced_color_state = neutral_state;
  advanced_color_state.color_replacement_enabled = true;
  advanced_color_state.color_replacement_source_hue = 0.0;
  advanced_color_state.color_replacement_target_hue = 0.58;
  advanced_color_state.color_replacement_range = 58.0;
  advanced_color_state.color_replacement_softness = 46.0;
  advanced_color_state.color_replacement_strength = 74.0;
  advanced_color_state.color_replacement_preserve_luminance = false;
  advanced_color_state.color_replacement_preserve_saturation = false;
  advanced_color_state.color_harmony_enabled = true;
  advanced_color_state.color_harmony_mode = QStringLiteral("Triadic");
  advanced_color_state.color_harmony_base_hue = 0.07;
  advanced_color_state.color_harmony_strength = 44.0;
  if (!verify_global_edit_parity("color replacement and harmony", advanced_color_state, 5)) {
    return EXIT_FAILURE;
  }

  auto presence_grading_state = neutral_state;
  presence_grading_state.texture = 28.0;
  presence_grading_state.clarity = -18.0;
  presence_grading_state.dehaze = 24.0;
  presence_grading_state.vibrance = 38.0;
  presence_grading_state.saturation = -16.0;
  presence_grading_state.grading_shadows_hue = 224.0 / 360.0;
  presence_grading_state.grading_shadows_saturation = 18.0;
  presence_grading_state.grading_midtones_hue = 32.0 / 360.0;
  presence_grading_state.grading_midtones_saturation = 20.0;
  presence_grading_state.grading_highlights_hue = 54.0 / 360.0;
  presence_grading_state.grading_highlights_saturation = 14.0;
  if (!verify_global_edit_parity("presence and grading", presence_grading_state, 10)) {
    return EXIT_FAILURE;
  }
  if (global_parity_case_count != 8) {
    return fail("settled preview/export parity should cover every high-risk global edit group");
  }

  QImage layer_mask_source(4, 2, QImage::Format_ARGB32);
  layer_mask_source.setColorSpace(photo_editor::desktop::preview_color_space());
  layer_mask_source.fill(QColor::fromRgb(68, 72, 76));

  photo_editor::desktop::RenderEditState base_image_state;
  base_image_state.working_color_profile = QStringLiteral("Linear sRGB");
  base_image_state.preview_color_space = QStringLiteral("sRGB");
  base_image_state.output_color_space = QStringLiteral("sRGB");
  const auto base_image_preview = photo_editor::desktop::render_output_image_with_backend(
      layer_mask_source,
      base_image_state,
      photo_editor::desktop::preview_color_space(),
      QImage::Format_ARGB32,
      photo_editor::desktop::RenderBackendPreference::CpuOnly);
  const auto base_image_export_render = photo_editor::desktop::render_output_image_with_backend(
      layer_mask_source,
      base_image_state,
      photo_editor::desktop::preview_color_space(),
      QImage::Format_RGBA64,
      photo_editor::desktop::RenderBackendPreference::CpuOnly);
  if (base_image_preview.isNull() || base_image_export_render.isNull()) {
    return fail("base image parity proof should produce preview and export renders");
  }
  if (max_channel_delta(layer_mask_source, base_image_preview) > kEncodedExportTolerance ||
      max_channel_delta(
          base_image_preview,
          base_image_export_render.convertToFormat(QImage::Format_ARGB32)) >
          kEncodedExportTolerance) {
    return fail("neutral Base Image preview and full-resolution export render should match source pixels");
  }

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
          layer_mask_export_render.convertToFormat(QImage::Format_ARGB32)) > kPreviewExportParityTolerance) {
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
          layer_mask_exported.convertToFormat(QImage::Format_ARGB32)) > kEncodedExportTolerance) {
    return fail("encoded layer plus mask export should match the full-resolution render");
  }

  QImage full_color_layer_source(4, 1, QImage::Format_ARGB32);
  full_color_layer_source.setColorSpace(photo_editor::desktop::preview_color_space());
  full_color_layer_source.setPixelColor(0, 0, QColor::fromRgb(44, 64, 112));
  full_color_layer_source.setPixelColor(1, 0, QColor::fromRgb(58, 92, 128));
  full_color_layer_source.setPixelColor(2, 0, QColor::fromRgb(72, 118, 156));
  full_color_layer_source.setPixelColor(3, 0, QColor::fromRgb(86, 142, 184));
  photo_editor::desktop::RenderEditState full_color_layer_state;
  full_color_layer_state.working_color_profile = QStringLiteral("Linear sRGB");
  full_color_layer_state.preview_color_space = QStringLiteral("sRGB");
  full_color_layer_state.output_color_space = QStringLiteral("sRGB");
  QByteArray full_color_layer_mask(4, char(0));
  full_color_layer_mask[2] = char(0xff);
  full_color_layer_mask[3] = char(0xff);
  QVariantMap full_color_adjustments;
  full_color_adjustments.insert(QStringLiteral("exposure"), 0.85);
  full_color_adjustments.insert(QStringLiteral("contrast"), 32.0);
  full_color_adjustments.insert(QStringLiteral("highlights"), -18.0);
  full_color_adjustments.insert(QStringLiteral("shadows"), 24.0);
  full_color_adjustments.insert(QStringLiteral("temperature"), 28.0);
  full_color_adjustments.insert(QStringLiteral("tint"), -16.0);
  full_color_adjustments.insert(QStringLiteral("saturation"), 34.0);
  full_color_adjustments.insert(QStringLiteral("vibrance"), 22.0);
  full_color_adjustments.insert(QStringLiteral("curveLights"), 26.0);
  full_color_adjustments.insert(
      QStringLiteral("lumaCurvePoints"),
      QVariantList{
          QVariantMap{{QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.0}},
          QVariantMap{{QStringLiteral("x"), 0.45}, {QStringLiteral("y"), 0.34}},
          QVariantMap{{QStringLiteral("x"), 1.0}, {QStringLiteral("y"), 1.0}},
      });
  QVariantMap full_color_adjustment_layer;
  full_color_adjustment_layer.insert(QStringLiteral("id"), QStringLiteral("masked-full-color-layer"));
  full_color_adjustment_layer.insert(QStringLiteral("name"), QStringLiteral("Masked Full Color Layer"));
  full_color_adjustment_layer.insert(QStringLiteral("kind"), QStringLiteral("adjustment"));
  full_color_adjustment_layer.insert(QStringLiteral("enabled"), true);
  full_color_adjustment_layer.insert(QStringLiteral("opacity"), 0.90);
  full_color_adjustment_layer.insert(QStringLiteral("adjustments"), full_color_adjustments);
  full_color_adjustment_layer.insert(QStringLiteral("maskData"), full_color_layer_mask);
  full_color_adjustment_layer.insert(QStringLiteral("maskWidth"), 4);
  full_color_adjustment_layer.insert(QStringLiteral("maskHeight"), 1);
  full_color_layer_state.layers.append(full_color_adjustment_layer);
  const auto full_color_layer_preview = photo_editor::desktop::render_output_image_with_backend(
      full_color_layer_source,
      full_color_layer_state,
      photo_editor::desktop::preview_color_space(),
      QImage::Format_ARGB32,
      photo_editor::desktop::RenderBackendPreference::CpuOnly);
  const auto full_color_layer_export_render = photo_editor::desktop::render_output_image_with_backend(
      full_color_layer_source,
      full_color_layer_state,
      photo_editor::desktop::preview_color_space(),
      QImage::Format_RGBA64,
      photo_editor::desktop::RenderBackendPreference::CpuOnly);
  if (full_color_layer_preview.isNull() || full_color_layer_export_render.isNull()) {
    return fail("masked full-color adjustment layer preview/export proof should produce rendered images");
  }
  if (max_channel_delta(
          full_color_layer_preview,
          full_color_layer_export_render.convertToFormat(QImage::Format_ARGB32)) >
      kPreviewExportParityTolerance) {
    return fail("masked full-color adjustment layer preview should match full-resolution export");
  }
  if (max_channel_delta(
          full_color_layer_source.copy(0, 0, 2, 1),
          full_color_layer_preview.copy(0, 0, 2, 1)) > kPreviewExportParityTolerance) {
    return fail("masked full-color adjustment layer should not leak into unmasked pixels");
  }
  if (max_channel_delta(
          full_color_layer_source.copy(2, 0, 2, 1),
          full_color_layer_preview.copy(2, 0, 2, 1)) < 18) {
    return fail("masked full-color adjustment layer should visibly affect masked pixels");
  }

  photo_editor::desktop::RenderEditState linked_selection_layer_state;
  linked_selection_layer_state.working_color_profile = QStringLiteral("Linear sRGB");
  linked_selection_layer_state.preview_color_space = QStringLiteral("sRGB");
  linked_selection_layer_state.output_color_space = QStringLiteral("sRGB");
  QVariantMap linked_selection_mask;
  linked_selection_mask.insert(QStringLiteral("id"), QStringLiteral("linked-layer-selection-source"));
  linked_selection_mask.insert(QStringLiteral("name"), QStringLiteral("Linked Layer Source"));
  linked_selection_mask.insert(QStringLiteral("kind"), QStringLiteral("brush"));
  linked_selection_mask.insert(QStringLiteral("toolRole"), QStringLiteral("selection"));
  linked_selection_mask.insert(QStringLiteral("committed"), true);
  linked_selection_mask.insert(QStringLiteral("maskSetId"), QStringLiteral("linked-layer-mask-set"));
  linked_selection_mask.insert(QStringLiteral("maskSetName"), QStringLiteral("Linked Layer Mask"));
  linked_selection_mask.insert(QStringLiteral("operation"), QStringLiteral("add"));
  linked_selection_mask.insert(QStringLiteral("segmentationData"), full_color_layer_mask);
  linked_selection_mask.insert(QStringLiteral("segmentationWidth"), 4);
  linked_selection_mask.insert(QStringLiteral("segmentationHeight"), 1);
  linked_selection_layer_state.mask_stack.append(linked_selection_mask);
  QVariantMap linked_selection_adjustment_layer;
  linked_selection_adjustment_layer.insert(
      QStringLiteral("id"), QStringLiteral("linked-selection-full-color-layer"));
  linked_selection_adjustment_layer.insert(
      QStringLiteral("name"), QStringLiteral("Linked Selection Full Color Layer"));
  linked_selection_adjustment_layer.insert(QStringLiteral("kind"), QStringLiteral("adjustment"));
  linked_selection_adjustment_layer.insert(QStringLiteral("enabled"), true);
  linked_selection_adjustment_layer.insert(QStringLiteral("opacity"), 0.90);
  linked_selection_adjustment_layer.insert(QStringLiteral("adjustments"), full_color_adjustments);
  linked_selection_adjustment_layer.insert(QStringLiteral("selectionScoped"), true);
  linked_selection_adjustment_layer.insert(
      QStringLiteral("selectionMaskSetId"), QStringLiteral("linked-layer-mask-set"));
  linked_selection_layer_state.layers.append(linked_selection_adjustment_layer);
  const auto linked_selection_preview = photo_editor::desktop::render_output_image_with_backend(
      full_color_layer_source,
      linked_selection_layer_state,
      photo_editor::desktop::preview_color_space(),
      QImage::Format_ARGB32,
      photo_editor::desktop::RenderBackendPreference::CpuOnly);
  const auto linked_selection_export_render = photo_editor::desktop::render_output_image_with_backend(
      full_color_layer_source,
      linked_selection_layer_state,
      photo_editor::desktop::preview_color_space(),
      QImage::Format_RGBA64,
      photo_editor::desktop::RenderBackendPreference::CpuOnly);
  if (linked_selection_preview.isNull() || linked_selection_export_render.isNull()) {
    return fail("linked selection full-color layer preview/export proof should produce rendered images");
  }
  if (max_channel_delta(
          linked_selection_preview,
          linked_selection_export_render.convertToFormat(QImage::Format_ARGB32)) >
      kPreviewExportParityTolerance) {
    return fail("linked selection full-color layer preview should match full-resolution export");
  }
  if (max_channel_delta(
          full_color_layer_source.copy(0, 0, 2, 1),
          linked_selection_preview.copy(0, 0, 2, 1)) > kPreviewExportParityTolerance) {
    return fail("linked selection full-color layer should not leak into unselected pixels");
  }
  if (max_channel_delta(
          full_color_layer_source.copy(2, 0, 2, 1),
          linked_selection_preview.copy(2, 0, 2, 1)) < 18) {
    return fail("linked selection full-color layer should visibly affect selected pixels");
  }

  QImage mixed_selection_source(16, 12, QImage::Format_ARGB32);
  mixed_selection_source.setColorSpace(photo_editor::desktop::preview_color_space());
  mixed_selection_source.fill(QColor::fromRgb(74, 74, 74));
  for (int y = 1; y <= 3; ++y) {
    for (int x = 12; x <= 14; ++x) {
      mixed_selection_source.setPixelColor(x, y, QColor::fromRgb(220, 42, 38));
    }
  }

  const QString mixed_selection_set_id = QStringLiteral("mixed-selection-parity-set");
  auto mixed_source_base = [&mixed_selection_set_id](QString id, QString name, QString kind) {
    QVariantMap mask;
    mask.insert(QStringLiteral("id"), id);
    mask.insert(QStringLiteral("name"), name);
    mask.insert(QStringLiteral("kind"), kind);
    mask.insert(QStringLiteral("toolRole"), QStringLiteral("selection"));
    mask.insert(QStringLiteral("maskSetId"), mixed_selection_set_id);
    mask.insert(QStringLiteral("maskSetName"), QStringLiteral("Mixed Selection Parity"));
    mask.insert(QStringLiteral("operation"), QStringLiteral("add"));
    mask.insert(QStringLiteral("enabled"), true);
    mask.insert(QStringLiteral("feather"), 0.0);
    mask.insert(QStringLiteral("selectionFeather"), 0.0);
    mask.insert(QStringLiteral("selectionSmooth"), 0.0);
    return mask;
  };

  auto mixed_brush = mixed_source_base(
      QStringLiteral("mixed-brush-source"),
      QStringLiteral("Mixed Brush Source"),
      QStringLiteral("brush"));
  QVariantMap mixed_brush_point;
  mixed_brush_point.insert(QStringLiteral("x"), 0.25);
  mixed_brush_point.insert(QStringLiteral("y"), 0.25);
  mixed_brush_point.insert(QStringLiteral("size"), 0.10);
  mixed_brush_point.insert(QStringLiteral("flow"), 1.0);
  mixed_brush_point.insert(QStringLiteral("opacity"), 1.0);
  mixed_brush.insert(QStringLiteral("brushHardness"), 0.85);
  mixed_brush.insert(QStringLiteral("brushBuildup"), false);
  mixed_brush.insert(QStringLiteral("brushPoints"), QVariantList{mixed_brush_point});

  auto mixed_lasso = mixed_source_base(
      QStringLiteral("mixed-lasso-source"),
      QStringLiteral("Mixed Lasso Source"),
      QStringLiteral("lasso"));
  mixed_lasso.insert(
      QStringLiteral("polygonPoints"),
      QVariantList{
          QVariantMap{{QStringLiteral("x"), 0.06}, {QStringLiteral("y"), 0.64}},
          QVariantMap{{QStringLiteral("x"), 0.28}, {QStringLiteral("y"), 0.64}},
          QVariantMap{{QStringLiteral("x"), 0.28}, {QStringLiteral("y"), 0.92}},
          QVariantMap{{QStringLiteral("x"), 0.06}, {QStringLiteral("y"), 0.92}},
      });

  auto mixed_rectangle = mixed_source_base(
      QStringLiteral("mixed-rectangle-source"),
      QStringLiteral("Mixed Rectangle Source"),
      QStringLiteral("rectangle"));
  mixed_rectangle.insert(QStringLiteral("centerX"), 0.56);
  mixed_rectangle.insert(QStringLiteral("centerY"), 0.55);
  mixed_rectangle.insert(QStringLiteral("radiusX"), 0.10);
  mixed_rectangle.insert(QStringLiteral("radiusY"), 0.14);
  mixed_rectangle.insert(QStringLiteral("rotation"), 0.0);

  auto mixed_generated = mixed_source_base(
      QStringLiteral("mixed-generated-source"),
      QStringLiteral("Mixed Generated Source"),
      QStringLiteral("object"));
  QByteArray mixed_generated_alpha(16 * 12, char(0));
  for (int y = 8; y <= 10; ++y) {
    for (int x = 12; x <= 14; ++x) {
      mixed_generated_alpha[y * 16 + x] = char(0xff);
    }
  }
  mixed_generated.insert(QStringLiteral("segmentationData"), mixed_generated_alpha);
  mixed_generated.insert(QStringLiteral("segmentationWidth"), 16);
  mixed_generated.insert(QStringLiteral("segmentationHeight"), 12);
  mixed_generated.insert(
      QStringLiteral("generatedAlphaSourceContract"),
      QStringLiteral("generated-alpha-plus-metadata-v1"));
  mixed_generated.insert(QStringLiteral("generatedAlphaTargetType"), QStringLiteral("object"));

  auto mixed_color_range = mixed_source_base(
      QStringLiteral("mixed-color-range-source"),
      QStringLiteral("Mixed Color Range Source"),
      QStringLiteral("color"));
  mixed_color_range.insert(QStringLiteral("hueCenter"), 0.0);
  mixed_color_range.insert(QStringLiteral("hueRange"), 0.50);
  mixed_color_range.insert(QStringLiteral("saturationMin"), 0.45);
  mixed_color_range.insert(QStringLiteral("saturationMax"), 1.0);
  mixed_color_range.insert(QStringLiteral("rangeFeather"), 0.0);

  photo_editor::desktop::RenderEditState mixed_selection_state;
  mixed_selection_state.working_color_profile = QStringLiteral("Linear sRGB");
  mixed_selection_state.preview_color_space = QStringLiteral("sRGB");
  mixed_selection_state.output_color_space = QStringLiteral("sRGB");
  mixed_selection_state.mask_stack.append(mixed_brush);
  mixed_selection_state.mask_stack.append(mixed_lasso);
  mixed_selection_state.mask_stack.append(mixed_rectangle);
  mixed_selection_state.mask_stack.append(mixed_generated);
  mixed_selection_state.mask_stack.append(mixed_color_range);
  QVariantMap mixed_selection_adjustments;
  mixed_selection_adjustments.insert(QStringLiteral("exposure"), 0.95);
  mixed_selection_adjustments.insert(QStringLiteral("contrast"), 18.0);
  QVariantMap mixed_selection_layer;
  mixed_selection_layer.insert(QStringLiteral("id"), QStringLiteral("mixed-selection-adjustment-layer"));
  mixed_selection_layer.insert(QStringLiteral("name"), QStringLiteral("Mixed Selection Adjustment"));
  mixed_selection_layer.insert(QStringLiteral("kind"), QStringLiteral("adjustment"));
  mixed_selection_layer.insert(QStringLiteral("enabled"), true);
  mixed_selection_layer.insert(QStringLiteral("opacity"), 1.0);
  mixed_selection_layer.insert(QStringLiteral("adjustments"), mixed_selection_adjustments);
  mixed_selection_layer.insert(QStringLiteral("selectionScoped"), true);
  mixed_selection_layer.insert(
      QStringLiteral("selectionMaskSetId"),
      mixed_selection_set_id);
  mixed_selection_state.layers.append(mixed_selection_layer);

  const auto mixed_selection_working = photo_editor::desktop::working_image_from_qimage(
      mixed_selection_source,
      mixed_selection_state.working_color_profile);
  if (mixed_selection_working.isNull()) {
    return fail("mixed selection source should linearize for range-mask alpha proof");
  }
  photo_editor::pipeline::LocalMaskSet mixed_color_probe_set;
  mixed_color_probe_set.enabled = true;
  photo_editor::pipeline::LocalMask mixed_color_probe;
  mixed_color_probe.kind = "color";
  mixed_color_probe.operation = "add";
  mixed_color_probe.enabled = true;
  mixed_color_probe.hue_center = 0.0;
  mixed_color_probe.hue_range = 0.50;
  mixed_color_probe.saturation_min = 0.45;
  mixed_color_probe.saturation_max = 1.0;
  mixed_color_probe.range_feather = 0.0;
  mixed_color_probe_set.sources.push_back(mixed_color_probe);
  auto mixed_color_probe_config = photo_editor::pipeline::default_color_pipeline_config();
  mixed_color_probe_config.working_profile =
      photo_editor::pipeline::working_color_profile_from_name("Linear sRGB");
  mixed_color_probe_config.preview_output_profile =
      photo_editor::pipeline::output_color_profile_from_name("sRGB");
  mixed_color_probe_config.export_output_profile =
      photo_editor::pipeline::output_color_profile_from_name("sRGB");
  const auto mixed_color_probe_alpha = photo_editor::pipeline::rasterize_local_mask_set_alpha(
      mixed_color_probe_set,
      mixed_selection_working.width,
      mixed_selection_working.height,
      mixed_selection_working.pixels,
      mixed_color_probe_config);
  if (mixed_color_probe_alpha.size() != 16 * 12 ||
      mixed_color_probe_alpha[2 * 16 + 13] == 0 ||
      mixed_color_probe_alpha[11 * 16 + 15] != 0) {
    const auto detail =
        QStringLiteral(
            "mixed color range source should select the colored source patch without selecting neutral pixels (range=%1 neutral=%2)")
            .arg(mixed_color_probe_alpha.size() == 16 * 12 ? int(mixed_color_probe_alpha[2 * 16 + 13]) : -1)
            .arg(mixed_color_probe_alpha.size() == 16 * 12 ? int(mixed_color_probe_alpha[11 * 16 + 15]) : -1)
            .toUtf8();
    return fail(detail.constData());
  }

  const auto mixed_selection_preview = photo_editor::desktop::render_output_image_with_backend(
      mixed_selection_source,
      mixed_selection_state,
      photo_editor::desktop::preview_color_space(),
      QImage::Format_ARGB32,
      photo_editor::desktop::RenderBackendPreference::CpuOnly,
      nullptr,
      nullptr,
      photo_editor::desktop::RenderMaskOptions{
          .rebuild_editable_masks_at_output_resolution = true});
  const auto mixed_selection_export = photo_editor::desktop::render_output_image_with_backend(
      mixed_selection_source,
      mixed_selection_state,
      photo_editor::desktop::preview_color_space(),
      QImage::Format_RGBA64,
      photo_editor::desktop::RenderBackendPreference::CpuOnly,
      nullptr,
      nullptr,
      photo_editor::desktop::RenderMaskOptions{
          .rebuild_editable_masks_at_output_resolution = true});
  if (mixed_selection_preview.isNull() || mixed_selection_export.isNull()) {
    return fail("mixed selection preview/export parity proof should produce rendered images");
  }
  if (max_channel_delta(
          mixed_selection_preview,
          mixed_selection_export.convertToFormat(QImage::Format_ARGB32)) >
      kPreviewExportParityTolerance) {
    return fail("mixed brush/lasso/shape/generated/range selection preview should match full-resolution export");
  }
  const auto visible_delta_at = [&](const QPoint& point) {
    const auto before = mixed_selection_source.pixelColor(point);
    const auto after = mixed_selection_preview.pixelColor(point);
    return std::max({
        std::abs(after.red() - before.red()),
        std::abs(after.green() - before.green()),
        std::abs(after.blue() - before.blue())});
  };
  const std::array<std::pair<const char*, QPoint>, 5> mixed_selected_points{{
      {"brush", QPoint(4, 3)},
      {"lasso", QPoint(2, 9)},
      {"shape", QPoint(9, 6)},
      {"generated", QPoint(13, 9)},
      {"range", QPoint(13, 2)},
  }};
  for (const auto& [source_label, selected_point] : mixed_selected_points) {
    const auto delta = visible_delta_at(selected_point);
    if (delta < 10) {
      const auto detail =
          QStringLiteral(
              "mixed selection source should visibly affect representative brush, lasso, shape, generated, and range pixels (source=%1 x=%2 y=%3 delta=%4)")
              .arg(QString::fromLatin1(source_label))
              .arg(selected_point.x())
              .arg(selected_point.y())
              .arg(delta)
              .toUtf8();
      return fail(detail.constData());
    }
  }
  if (visible_delta_at(QPoint(15, 11)) > kPreviewExportParityTolerance) {
    return fail("mixed selection source should not leak into unselected pixels");
  }

  const QSize layer_local_full_size(10, 8);
  const QSize layer_local_preview_size(5, 4);
  QImage layer_local_full_source(layer_local_full_size, QImage::Format_ARGB32);
  layer_local_full_source.setColorSpace(photo_editor::desktop::preview_color_space());
  for (int y = 0; y < layer_local_full_source.height(); ++y) {
    for (int x = 0; x < layer_local_full_source.width(); ++x) {
      layer_local_full_source.setPixelColor(
          x,
          y,
          QColor::fromRgb(
              34 + ((x * 21 + y * 9) % 160),
              48 + ((x * 13 + y * 23) % 150),
              72 + ((x * 31 + y * 7) % 128)));
    }
  }
  const auto layer_local_preview_source =
      normalized_nearest_resample(layer_local_full_source, layer_local_preview_size);
  QVariantMap layer_local_adjustments;
  layer_local_adjustments.insert(QStringLiteral("exposure"), 0.72);
  layer_local_adjustments.insert(QStringLiteral("contrast"), 30.0);
  layer_local_adjustments.insert(QStringLiteral("highlights"), -24.0);
  layer_local_adjustments.insert(QStringLiteral("shadows"), 22.0);
  layer_local_adjustments.insert(QStringLiteral("whites"), 8.0);
  layer_local_adjustments.insert(QStringLiteral("blacks"), -7.0);
  layer_local_adjustments.insert(QStringLiteral("brightness"), 9.0);
  layer_local_adjustments.insert(QStringLiteral("filmicShoulder"), 11.0);
  layer_local_adjustments.insert(QStringLiteral("adaptiveContrast"), 14.0);
  layer_local_adjustments.insert(QStringLiteral("temperature"), 26.0);
  layer_local_adjustments.insert(QStringLiteral("tint"), -15.0);
  layer_local_adjustments.insert(QStringLiteral("vibrance"), 24.0);
  layer_local_adjustments.insert(QStringLiteral("saturation"), 18.0);
  layer_local_adjustments.insert(QStringLiteral("curveLights"), 19.0);
  layer_local_adjustments.insert(QStringLiteral("redCurvePoints"),
      QVariantList{
          QVariantMap{{QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.0}},
          QVariantMap{{QStringLiteral("x"), 0.52}, {QStringLiteral("y"), 0.64}},
          QVariantMap{{QStringLiteral("x"), 1.0}, {QStringLiteral("y"), 1.0}},
      });
  layer_local_adjustments.insert(QStringLiteral("blueCurvePoints"),
      QVariantList{
          QVariantMap{{QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.0}},
          QVariantMap{{QStringLiteral("x"), 0.58}, {QStringLiteral("y"), 0.42}},
          QVariantMap{{QStringLiteral("x"), 1.0}, {QStringLiteral("y"), 1.0}},
      });
  const auto make_layer_local_state = [&](const QSize& mask_size) {
    photo_editor::desktop::RenderEditState next_state;
    next_state.working_color_profile = QStringLiteral("Linear sRGB");
    next_state.preview_color_space = QStringLiteral("sRGB");
    next_state.output_color_space = QStringLiteral("sRGB");
    QVariantMap layer;
    layer.insert(QStringLiteral("id"), QStringLiteral("scaled-preview-full-color-layer"));
    layer.insert(QStringLiteral("name"), QStringLiteral("Scaled Preview Full Color Layer"));
    layer.insert(QStringLiteral("kind"), QStringLiteral("adjustment"));
    layer.insert(QStringLiteral("enabled"), true);
    layer.insert(QStringLiteral("opacity"), 0.88);
    layer.insert(QStringLiteral("adjustments"), layer_local_adjustments);
    layer.insert(QStringLiteral("maskData"), normalized_mask_fixture(mask_size));
    layer.insert(QStringLiteral("maskWidth"), mask_size.width());
    layer.insert(QStringLiteral("maskHeight"), mask_size.height());
    next_state.layers.append(layer);
    return next_state;
  };
  const auto layer_local_preview_state = make_layer_local_state(layer_local_preview_size);
  const auto layer_local_full_state = make_layer_local_state(layer_local_full_size);
  const auto layer_local_scaled_preview = photo_editor::desktop::render_output_image_with_backend(
      layer_local_preview_source,
      layer_local_preview_state,
      photo_editor::desktop::preview_color_space(),
      QImage::Format_ARGB32,
      photo_editor::desktop::RenderBackendPreference::CpuOnly);
  const auto layer_local_full_export = photo_editor::desktop::render_output_image_with_backend(
      layer_local_full_source,
      layer_local_full_state,
      photo_editor::desktop::preview_color_space(),
      QImage::Format_RGBA64,
      photo_editor::desktop::RenderBackendPreference::CpuOnly);
  if (layer_local_scaled_preview.isNull() || layer_local_full_export.isNull()) {
    return fail("scaled masked layer-local preview/export proof should produce rendered images");
  }
  const auto layer_local_downsampled_export = normalized_nearest_resample(
      layer_local_full_export.convertToFormat(QImage::Format_ARGB32),
      layer_local_preview_size);
  const auto layer_local_scaled_delta =
      max_channel_delta(layer_local_scaled_preview, layer_local_downsampled_export);
  if (layer_local_scaled_delta > kPreviewExportParityTolerance) {
    std::cerr << "scaled masked layer-local preview/export max channel delta: "
              << layer_local_scaled_delta << '\n';
    return fail("scaled masked layer-local preview should match full-resolution export");
  }
  if (max_channel_delta(
          layer_local_preview_source.copy(0, 0, 1, 1),
          layer_local_scaled_preview.copy(0, 0, 1, 1)) > kPreviewExportParityTolerance) {
    return fail("scaled masked layer-local preview should not leak outside its mask");
  }
  if (max_channel_delta(
          layer_local_preview_source.copy(3, 2, 1, 1),
          layer_local_scaled_preview.copy(3, 2, 1, 1)) < 12) {
    return fail("scaled masked layer-local preview should visibly affect masked pixels");
  }

  QImage lut_layer_source(2, 1, QImage::Format_ARGB32);
  lut_layer_source.setColorSpace(photo_editor::desktop::preview_color_space());
  lut_layer_source.fill(QColor::fromRgb(72, 80, 88));
  photo_editor::desktop::RenderEditState lut_layer_state;
  lut_layer_state.working_color_profile = QStringLiteral("Linear sRGB");
  lut_layer_state.preview_color_space = QStringLiteral("sRGB");
  lut_layer_state.output_color_space = QStringLiteral("sRGB");
  QVariantList red_lut_table;
  for (int index = 0; index < 8; ++index) {
    red_lut_table.append(QVariantMap{
        {QStringLiteral("red"), 1.0},
        {QStringLiteral("green"), 0.0},
        {QStringLiteral("blue"), 0.0},
        {QStringLiteral("alpha"), 1.0},
    });
  }
  QVariantMap red_lut;
  red_lut.insert(QStringLiteral("enabled"), true);
  red_lut.insert(QStringLiteral("strength"), 100.0);
  red_lut.insert(QStringLiteral("name"), QStringLiteral("Layer Red LUT"));
  red_lut.insert(QStringLiteral("size"), 2);
  red_lut.insert(QStringLiteral("table"), red_lut_table);
  red_lut.insert(QStringLiteral("domainMin"), QVariantList{0.0, 0.0, 0.0});
  red_lut.insert(QStringLiteral("domainMax"), QVariantList{1.0, 1.0, 1.0});
  red_lut.insert(QStringLiteral("inputColorSpace"), QStringLiteral("sRGB"));
  red_lut.insert(QStringLiteral("outputColorSpace"), QStringLiteral("sRGB"));
  QVariantMap lut_adjustments;
  lut_adjustments.insert(QStringLiteral("lutStack"), QVariantList{red_lut});
  QByteArray lut_layer_mask(2, char(0));
  lut_layer_mask[1] = char(0xff);
  QVariantMap lut_adjustment_layer;
  lut_adjustment_layer.insert(QStringLiteral("id"), QStringLiteral("masked-layer-lut"));
  lut_adjustment_layer.insert(QStringLiteral("name"), QStringLiteral("Masked Layer LUT"));
  lut_adjustment_layer.insert(QStringLiteral("kind"), QStringLiteral("adjustment"));
  lut_adjustment_layer.insert(QStringLiteral("enabled"), true);
  lut_adjustment_layer.insert(QStringLiteral("opacity"), 1.0);
  lut_adjustment_layer.insert(QStringLiteral("adjustments"), lut_adjustments);
  lut_adjustment_layer.insert(QStringLiteral("maskData"), lut_layer_mask);
  lut_adjustment_layer.insert(QStringLiteral("maskWidth"), 2);
  lut_adjustment_layer.insert(QStringLiteral("maskHeight"), 1);
  lut_layer_state.layers.append(lut_adjustment_layer);
  const auto lut_layer_preview = photo_editor::desktop::render_output_image_with_backend(
      lut_layer_source,
      lut_layer_state,
      photo_editor::desktop::preview_color_space(),
      QImage::Format_ARGB32,
      photo_editor::desktop::RenderBackendPreference::CpuOnly);
  const auto lut_layer_export_render = photo_editor::desktop::render_output_image_with_backend(
      lut_layer_source,
      lut_layer_state,
      photo_editor::desktop::preview_color_space(),
      QImage::Format_RGBA64,
      photo_editor::desktop::RenderBackendPreference::CpuOnly);
  if (lut_layer_preview.isNull() || lut_layer_export_render.isNull()) {
    return fail("masked layer-local LUT preview/export proof should produce rendered images");
  }
  if (max_channel_delta(
          lut_layer_preview,
          lut_layer_export_render.convertToFormat(QImage::Format_ARGB32)) >
      kPreviewExportParityTolerance) {
    return fail("masked layer-local LUT preview should match the full-resolution export render");
  }
  const auto lut_unmasked_sample = lut_layer_preview.pixelColor(0, 0);
  const auto lut_masked_sample = lut_layer_preview.pixelColor(1, 0);
  if (lut_unmasked_sample.red() > lut_unmasked_sample.green() + 20 ||
      lut_masked_sample.red() < 230 ||
      lut_masked_sample.green() > 25 ||
      lut_masked_sample.blue() > 25) {
    return fail("layer-local LUT stacks should parse from layer adjustments and respect the layer mask");
  }

  QImage retouch_patch(2, 2, QImage::Format_ARGB32);
  retouch_patch.setColorSpace(photo_editor::desktop::preview_color_space());
  retouch_patch.fill(QColor::fromRgb(32, 224, 64));
  const auto retouch_patch_data = png_bytes(retouch_patch);
  if (retouch_patch_data.isEmpty()) {
    return fail("retouch parity fixture should encode a baked patch image");
  }
  const auto decoded_retouch_patch = QImage::fromData(retouch_patch_data, "PNG");
  if (decoded_retouch_patch.isNull() || !decoded_retouch_patch.colorSpace().isValid() ||
      decoded_retouch_patch.colorSpace() != photo_editor::desktop::preview_color_space()) {
    return fail("retouch parity fixture should store a tagged sRGB patch image");
  }
  auto untagged_retouch_patch = retouch_patch;
  untagged_retouch_patch.setColorSpace(QColorSpace());
  const auto untagged_retouch_patch_data = png_bytes(untagged_retouch_patch);
  if (untagged_retouch_patch_data.isEmpty()) {
    return fail("retouch parity fixture should encode an untagged legacy patch image");
  }
  QByteArray retouch_matte_data(16, char(0));
  retouch_matte_data[5] = char(0xff);
  retouch_matte_data[6] = char(0xff);
  retouch_matte_data[9] = char(0xff);
  retouch_matte_data[10] = char(0xff);
  QImage retouch_source(4, 4, QImage::Format_ARGB32);
  retouch_source.setColorSpace(photo_editor::desktop::preview_color_space());
  retouch_source.fill(QColor::fromRgb(68, 72, 76));
  QVariantMap retouch_patch_mask;
  retouch_patch_mask.insert(QStringLiteral("id"), QStringLiteral("preview-export-retouch-patch"));
  retouch_patch_mask.insert(QStringLiteral("name"), QStringLiteral("Retouch parity patch"));
  retouch_patch_mask.insert(QStringLiteral("kind"), QStringLiteral("brush"));
  retouch_patch_mask.insert(QStringLiteral("toolRole"), QStringLiteral("retouch"));
  retouch_patch_mask.insert(QStringLiteral("operation"), QStringLiteral("add"));
  retouch_patch_mask.insert(QStringLiteral("retouchMode"), QStringLiteral("remove"));
  retouch_patch_mask.insert(QStringLiteral("committed"), true);
  retouch_patch_mask.insert(QStringLiteral("removeStrength"), 100.0);
  retouch_patch_mask.insert(QStringLiteral("segmentationData"), retouch_matte_data);
  retouch_patch_mask.insert(QStringLiteral("segmentationWidth"), 4);
  retouch_patch_mask.insert(QStringLiteral("segmentationHeight"), 4);
  retouch_patch_mask.insert(QStringLiteral("inpaintPatchData"), retouch_patch_data);
  retouch_patch_mask.insert(QStringLiteral("inpaintPatchX"), 1);
  retouch_patch_mask.insert(QStringLiteral("inpaintPatchY"), 1);
  retouch_patch_mask.insert(QStringLiteral("inpaintPatchWidth"), 2);
  retouch_patch_mask.insert(QStringLiteral("inpaintPatchHeight"), 2);
  retouch_patch_mask.insert(QStringLiteral("inpaintPatchColorSpace"), QStringLiteral("sRGB"));
  retouch_patch_mask.insert(QStringLiteral("inpaintBlendAmount"), 1.0);
  retouch_patch_mask.insert(QStringLiteral("inpaintSeamFeather"), 0.0);
  retouch_patch_mask.insert(QStringLiteral("inpaintColorMatch"), 0.0);
  auto retouch_patch_state = neutral_state;
  retouch_patch_state.mask_stack.append(retouch_patch_mask);
  const auto retouch_patch_preview = photo_editor::desktop::render_output_image_with_backend(
      retouch_source,
      retouch_patch_state,
      photo_editor::desktop::preview_color_space(),
      QImage::Format_ARGB32,
      photo_editor::desktop::RenderBackendPreference::CpuOnly);
  const auto retouch_patch_export_render = photo_editor::desktop::render_output_image_with_backend(
      retouch_source,
      retouch_patch_state,
      photo_editor::desktop::preview_color_space(),
      QImage::Format_RGBA64,
      photo_editor::desktop::RenderBackendPreference::CpuOnly);
  if (retouch_patch_preview.isNull() || retouch_patch_export_render.isNull()) {
    return fail("retouch patch preview/export proof should produce rendered images");
  }
  if (max_channel_delta(
          retouch_patch_preview,
          retouch_patch_export_render.convertToFormat(QImage::Format_ARGB32)) >
      kPreviewExportParityTolerance) {
    return fail("retouch patch preview should match the full-resolution export render");
  }
  if (retouch_patch_preview.pixelColor(1, 1).green() <=
          retouch_source.pixelColor(1, 1).green() + 80 ||
      max_channel_delta(retouch_patch_preview, retouch_source) < 60) {
    return fail("retouch patch parity case should visibly apply the baked inpaint patch");
  }
  auto legacy_retouch_patch_mask = retouch_patch_mask;
  legacy_retouch_patch_mask.insert(QStringLiteral("inpaintPatchData"), untagged_retouch_patch_data);
  auto legacy_retouch_patch_state = neutral_state;
  legacy_retouch_patch_state.mask_stack.append(legacy_retouch_patch_mask);
  const auto legacy_retouch_patch_preview = photo_editor::desktop::render_output_image_with_backend(
      retouch_source,
      legacy_retouch_patch_state,
      photo_editor::desktop::preview_color_space(),
      QImage::Format_ARGB32,
      photo_editor::desktop::RenderBackendPreference::CpuOnly);
  if (legacy_retouch_patch_preview.isNull() ||
      max_channel_delta(legacy_retouch_patch_preview, retouch_patch_preview) >
          kPreviewExportParityTolerance) {
    return fail("legacy untagged retouch patch bytes should render using the stored patch color space");
  }

  const auto refinement_source = deterministic_refinement_source_image();
  photo_editor::desktop::RenderEditState refinement_neutral_state;
  refinement_neutral_state.working_color_profile = QStringLiteral("Linear sRGB");
  refinement_neutral_state.preview_color_space = QStringLiteral("sRGB");
  refinement_neutral_state.output_color_space = QStringLiteral("sRGB");
  const auto refinement_neutral_preview = photo_editor::desktop::render_output_image_with_backend(
      refinement_source,
      refinement_neutral_state,
      photo_editor::desktop::preview_color_space(),
      QImage::Format_ARGB32,
      photo_editor::desktop::RenderBackendPreference::CpuOnly);
  if (refinement_neutral_preview.isNull()) {
    return fail("refinement preview/export neutral baseline should render");
  }

  QVariantMap refinement_mask_base;
  refinement_mask_base.insert(QStringLiteral("id"), QStringLiteral("preview-export-refine-mask"));
  refinement_mask_base.insert(QStringLiteral("name"), QStringLiteral("Refinement parity matte"));
  refinement_mask_base.insert(QStringLiteral("kind"), QStringLiteral("brush"));
  refinement_mask_base.insert(QStringLiteral("toolRole"), QStringLiteral("selection"));
  refinement_mask_base.insert(QStringLiteral("maskSetId"), QStringLiteral("preview-export-refine-set"));
  refinement_mask_base.insert(QStringLiteral("maskSetName"), QStringLiteral("Refinement parity matte"));
  refinement_mask_base.insert(QStringLiteral("operation"), QStringLiteral("add"));
  refinement_mask_base.insert(QStringLiteral("segmentationData"), refinement_matte_data());
  refinement_mask_base.insert(QStringLiteral("segmentationWidth"), 8);
  refinement_mask_base.insert(QStringLiteral("segmentationHeight"), 8);
  refinement_mask_base.insert(QStringLiteral("exposure"), 0.9);
  refinement_mask_base.insert(QStringLiteral("temperature"), 36.0);
  refinement_mask_base.insert(QStringLiteral("tint"), -18.0);

  struct RefineParityCase final {
    const char* label;
    QString key;
    QVariant value;
  };
  const RefineParityCase refinement_parity_cases[] = {
      {"source expand", QStringLiteral("selectionExpand"), QVariant(1.4)},
      {"source smooth", QStringLiteral("selectionSmooth"), QVariant(40.0)},
      {"source fine edge", QStringLiteral("selectionFineEdge"), QVariant(55.0)},
      {"source edge contrast", QStringLiteral("selectionEdgeContrast"), QVariant(45.0)},
      {"source grow", QStringLiteral("selectionGrow"), QVariant(1.0)},
      {"source shrink", QStringLiteral("selectionShrink"), QVariant(1.0)},
      {"source feather", QStringLiteral("selectionFeather"), QVariant(2.0)},
      {"source blur", QStringLiteral("selectionBlur"), QVariant(1.5)},
      {"source harden", QStringLiteral("selectionHarden"), QVariant(65.0)},
      {"source despeckle", QStringLiteral("selectionDespeckle"), QVariant(35.0)},
      {"source fill holes", QStringLiteral("selectionFillHoles"), QVariant(35.0)},
      {"source remove islands", QStringLiteral("selectionRemoveIslands"), QVariant(35.0)},
      {"source invert", QStringLiteral("selectionInvert"), QVariant(true)},
      {"source decontaminate", QStringLiteral("selectionDecontaminate"), QVariant(30.0)},
      {"source density", QStringLiteral("density"), QVariant(0.72)},
      {"source opacity", QStringLiteral("opacity"), QVariant(0.64)},
      {"mask-set expand", QStringLiteral("maskSetSelectionExpand"), QVariant(1.4)},
      {"mask-set smooth", QStringLiteral("maskSetSelectionSmooth"), QVariant(40.0)},
      {"mask-set fine edge", QStringLiteral("maskSetSelectionFineEdge"), QVariant(55.0)},
      {"mask-set edge contrast", QStringLiteral("maskSetSelectionEdgeContrast"), QVariant(45.0)},
      {"mask-set invert", QStringLiteral("maskSetSelectionInvert"), QVariant(true)},
      {"mask-set density", QStringLiteral("maskSetDensity"), QVariant(0.72)},
      {"mask-set opacity", QStringLiteral("maskSetOpacity"), QVariant(0.64)},
  };

  auto refinement_parity_case_count = 0;
  for (const auto& parity_case : refinement_parity_cases) {
    ++refinement_parity_case_count;
    auto refinement_mask = refinement_mask_base;
    refinement_mask.insert(parity_case.key, parity_case.value);

    auto refinement_state = refinement_neutral_state;
    refinement_state.mask_stack.append(refinement_mask);
    const auto preview_render = photo_editor::desktop::render_output_image_with_backend(
        refinement_source,
        refinement_state,
        photo_editor::desktop::preview_color_space(),
        QImage::Format_ARGB32,
        photo_editor::desktop::RenderBackendPreference::CpuOnly);
    const auto export_render = photo_editor::desktop::render_output_image_with_backend(
        refinement_source,
        refinement_state,
        photo_editor::desktop::preview_color_space(),
        QImage::Format_RGBA64,
        photo_editor::desktop::RenderBackendPreference::CpuOnly);
    if (preview_render.isNull() || export_render.isNull()) {
      std::cerr << parity_case.label << " refinement parity render should produce images\n";
      return EXIT_FAILURE;
    }
    const auto preview_export_delta =
        max_channel_delta(preview_render, export_render.convertToFormat(QImage::Format_ARGB32));
    if (preview_export_delta > kPreviewExportParityTolerance) {
      std::cerr << parity_case.label << " refinement preview/export max channel delta: "
                << preview_export_delta << '\n';
      return fail("refinement preview should match full-resolution export render");
    }
    if (max_channel_delta(refinement_neutral_preview, preview_render) < 4) {
      std::cerr << parity_case.label << " refinement recipe should visibly affect the source\n";
      return EXIT_FAILURE;
    }
  }
  if (refinement_parity_case_count != 23) {
    return fail("preview/export parity should cover every source and mask-set refinement control");
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

  struct OutputPathParityCase final {
    const char* label;
    QString output_profile;
    QColorSpace target_space;
    QImage::Format export_format = QImage::Format_ARGB32;
    const char* writer_format = "png";
    QString suffix;
    int encoded_tolerance = 1;
  };
  const std::array<OutputPathParityCase, 4> output_path_cases = {{
      {.label = "JPEG 8-bit sRGB",
       .output_profile = QStringLiteral("sRGB"),
       .target_space = photo_editor::desktop::preview_color_space(),
       .export_format = QImage::Format_ARGB32,
       .writer_format = "jpg",
       .suffix = QStringLiteral("jpg"),
       .encoded_tolerance = 8},
      {.label = "PNG 16-bit sRGB",
       .output_profile = QStringLiteral("sRGB"),
       .target_space = photo_editor::desktop::preview_color_space(),
       .export_format = QImage::Format_RGBA64,
       .writer_format = "png",
       .suffix = QStringLiteral("png"),
       .encoded_tolerance = 1},
      {.label = "TIFF 16-bit Display P3",
       .output_profile = QStringLiteral("Display P3"),
       .target_space = QColorSpace(QColorSpace::DisplayP3),
       .export_format = QImage::Format_RGBA64,
       .writer_format = "tiff",
       .suffix = QStringLiteral("tif"),
       .encoded_tolerance = 1},
      {.label = "TIFF 16-bit ProPhoto",
       .output_profile = QStringLiteral("ProPhoto RGB"),
       .target_space = QColorSpace(QColorSpace::ProPhotoRgb),
       .export_format = QImage::Format_RGBA64,
       .writer_format = "tiff",
       .suffix = QStringLiteral("tif"),
       .encoded_tolerance = 1},
  }};
  auto output_path_case_count = 0;
  for (const auto& output_case : output_path_cases) {
    ++output_path_case_count;
    auto output_state = gpu_compatible_state();
    output_state.working_color_profile = QStringLiteral("Linear ProPhoto RGB");
    output_state.preview_color_space = output_case.output_profile;
    output_state.output_color_space = output_case.output_profile;
    const auto preview_render = photo_editor::desktop::render_output_image_with_backend(
        source,
        output_state,
        output_case.target_space,
        QImage::Format_ARGB32,
        photo_editor::desktop::RenderBackendPreference::CpuOnly);
    const auto export_render = photo_editor::desktop::render_output_image_with_backend(
        source,
        output_state,
        output_case.target_space,
        output_case.export_format,
        photo_editor::desktop::RenderBackendPreference::CpuOnly);
    if (preview_render.isNull() || export_render.isNull()) {
      std::cerr << output_case.label << " output-path parity should render preview and export\n";
      return EXIT_FAILURE;
    }
    const auto preview_export_delta =
        max_channel_delta(preview_render, export_render.convertToFormat(QImage::Format_ARGB32));
    if (preview_export_delta > kPreviewExportParityTolerance) {
      std::cerr << output_case.label << " preview/export max channel delta: "
                << preview_export_delta << '\n';
      return fail("output-path preview should match the full-resolution export render");
    }
    const auto encoded_path = temp_dir.filePath(
        QStringLiteral("output-path-%1.%2")
            .arg(QString::fromLatin1(output_case.writer_format), output_case.suffix));
    if (!write_image_as(export_render, encoded_path, output_case.writer_format)) {
      std::cerr << output_case.label << " encoded output path should be writable\n";
      return EXIT_FAILURE;
    }
    const QImage encoded_output(encoded_path);
    if (encoded_output.isNull()) {
      std::cerr << output_case.label << " encoded output path should be readable\n";
      return EXIT_FAILURE;
    }
    if (output_case.export_format == QImage::Format_RGBA64 &&
        encoded_output.format() != QImage::Format_RGBA64) {
      std::cerr << output_case.label << " should preserve 16-bit storage on readback\n";
      return EXIT_FAILURE;
    }
    const auto encoded_delta = max_channel_delta(
        export_render.convertToFormat(QImage::Format_ARGB32),
        encoded_output.convertToFormat(QImage::Format_ARGB32));
    if (encoded_delta > output_case.encoded_tolerance) {
      std::cerr << output_case.label << " encoded max channel delta: "
                << encoded_delta << '\n';
      return fail("encoded output path should match the full-resolution export render");
    }
  }
  if (output_path_case_count != 4) {
    return fail("output path parity should cover JPEG 8-bit, PNG 16-bit, Display P3 TIFF, and wide-gamut TIFF");
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
