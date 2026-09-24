#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

#include <QColor>
#include <QColorSpace>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QRect>

#include "edit_recipe_state.h"
#include "photo_render_utils.h"
#include "photo_editor/pipeline/color_adjustments.h"
#include "working_image.h"

namespace {

using photo_editor::pipeline::ColorPixel;
using photo_editor::pipeline::ColorAdjustmentState;
using photo_editor::pipeline::LocalMask;
using photo_editor::pipeline::LocalMaskSet;

enum class SourceKind {
  Procedural,
  NightRecovery,
  HighlightRollback,
  Portrait,
  Landscape,
  SaturatedColor,
};

enum class BehaviorCheck {
  None,
  NightRecovery,
  HighlightRollback,
  PortraitPreset,
  LandscapePreset,
  MonochromePreset,
  HighContrastPreset,
  SaturatedColorPreset,
};

struct GoldenCase final {
  const char* name;
  SourceKind source = SourceKind::Procedural;
  photo_editor::desktop::EditRecipeState state;
  BehaviorCheck behavior_check = BehaviorCheck::None;
  bool crop_before_render = false;
  std::uint64_t expected_hash = 0;
};

enum class MatteScene {
  Hair,
  TreeSkyline,
  ProductEdge,
  Glass,
  Smoke,
  Shadow,
};

struct MatteGoldenCase final {
  const char* name;
  LocalMask mask;
  int width = 0;
  int height = 0;
  std::vector<ColorPixel> source_pixels;
  std::uint64_t expected_hash = 0;
  std::uint64_t expected_zoomed_screenshot_hash = 0;
};

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

QImage source_image() {
  QImage image(48, 36, QImage::Format_ARGB32);
  image.setColorSpace(photo_editor::desktop::preview_color_space());
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      const auto red = 16 + ((x * 29 + y * 7) % 224);
      const auto green = 12 + ((x * 11 + y * 37) % 228);
      const auto blue = 20 + ((x * 53 + y * 19) % 216);
      image.setPixelColor(x, y, QColor(red, green, blue, 255));
    }
  }
  return image;
}

QImage night_recovery_source_image() {
  QImage image(80, 60, QImage::Format_ARGB32);
  image.setColorSpace(photo_editor::desktop::preview_color_space());
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      const auto sky_weight = std::clamp(1.0 - static_cast<double>(y) / 36.0, 0.0, 1.0);
      auto red = static_cast<int>(5.0 + sky_weight * 8.0);
      auto green = static_cast<int>(7.0 + sky_weight * 10.0);
      auto blue = static_cast<int>(12.0 + sky_weight * 18.0);

      const auto left_wall = x < 23 && y > 20;
      const auto right_wall = x > 57 && y > 14;
      if (left_wall || right_wall) {
        const auto wall_light = left_wall ? (23 - x) : (x - 57);
        red = 16 + wall_light * 2 + (y % 5);
        green = 10 + wall_light + (y % 3);
        blue = 16 + wall_light / 2;
      }
      if (x > 26 && x < 55 && y > 26) {
        red = std::min(red, 12 + (y % 4));
        green = std::min(green, 11 + (x % 3));
        blue = std::min(blue, 15 + ((x + y) % 4));
      }
      if (x > 11 && x < 28 && y > 36 && y < 53) {
        red = std::max(red, 84 + (52 - y) * 3);
        green = std::max(green, 18 + (x % 7));
        blue = std::max(blue, 112 + (x % 9));
      }
      if (x > 38 && x < 52 && y > 45 && y < 53) {
        red = std::max(red, 122 + ((x + y) % 34));
        green = std::max(green, 74 + ((x * 3) % 42));
        blue = std::max(blue, 118 + ((y * 5) % 38));
      }
      if (((x * 37 + y * 67) % 89) == 0 && y < 32) {
        red = 190;
        green = 198;
        blue = 222;
      }
      image.setPixelColor(x, y, QColor(red, green, blue, 255));
    }
  }
  return image;
}

QImage highlight_rollback_source_image() {
  QImage image(80, 60, QImage::Format_ARGB32);
  image.setColorSpace(photo_editor::desktop::preview_color_space());
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      const auto horizon = y < 30;
      auto red = horizon ? 88 + y * 3 : 40 + (y - 30) * 3;
      auto green = horizon ? 126 + y * 2 : 86 + (y - 30) * 2;
      auto blue = horizon ? 176 + y : 42 + (x % 16);
      if (y > 7 && y < 24 && x > 16 && x < 64) {
        const auto cloud_shape = ((x - 40) * (x - 40)) / 20 + ((y - 15) * (y - 15)) / 3;
        if (cloud_shape < 42) {
          red = 242 + (x % 10);
          green = 238 + (y % 12);
          blue = 226 + ((x + y) % 16);
        }
      }
      if (x > 26 && x < 44 && y > 36) {
        red = 54;
        green = 68 + (y % 8);
        blue = 48;
      }
      image.setPixelColor(x, y, QColor(red, green, blue, 255));
    }
  }
  return image;
}

QImage portrait_source_image() {
  QImage image(72, 72, QImage::Format_ARGB32);
  image.setColorSpace(photo_editor::desktop::preview_color_space());
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      auto red = 34 + (x % 11);
      auto green = 42 + (y % 13);
      auto blue = 52 + ((x + y) % 17);
      const auto dx = static_cast<double>(x - 36) / 21.0;
      const auto dy = static_cast<double>(y - 34) / 27.0;
      if (dx * dx + dy * dy < 1.0) {
        red = 166 + ((x * 3 + y) % 24);
        green = 105 + ((x + y * 2) % 22);
        blue = 78 + ((x * 5 + y) % 18);
      }
      if (y > 17 && y < 26 && x > 22 && x < 50) {
        red = std::min(red, 72);
        green = std::min(green, 54);
        blue = std::min(blue, 44);
      }
      image.setPixelColor(x, y, QColor(red, green, blue, 255));
    }
  }
  return image;
}

QImage landscape_source_image() {
  QImage image(88, 58, QImage::Format_ARGB32);
  image.setColorSpace(photo_editor::desktop::preview_color_space());
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      auto red = 48 + y;
      auto green = 86 + y / 2;
      auto blue = 158 + y;
      if (y > 22) {
        red = 36 + (x % 17);
        green = 92 + ((x * 5 + y * 3) % 78);
        blue = 42 + (y % 23);
      }
      if (y > 34 && x > 50) {
        red = 86 + ((x + y) % 34);
        green = 66 + ((x * 2) % 26);
        blue = 38 + (y % 18);
      }
      if (x > 8 && x < 32 && y > 27 && y < 45) {
        red = 18 + (x % 11);
        green = 68 + (y % 17);
        blue = 30 + ((x + y) % 9);
      }
      image.setPixelColor(x, y, QColor(red, green, blue, 255));
    }
  }
  return image;
}

QImage saturated_color_source_image() {
  QImage image(84, 54, QImage::Format_ARGB32);
  image.setColorSpace(photo_editor::desktop::preview_color_space());
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      auto red = 32 + (x % 24);
      auto green = 36 + (y % 22);
      auto blue = 42 + ((x + y) % 26);
      if (x < 28) {
        red = 176 + ((x * 3 + y) % 62);
        green = 26 + (y % 18);
        blue = 24 + (x % 16);
      } else if (x < 56) {
        red = 28 + (x % 17);
        green = 156 + ((x + y * 2) % 76);
        blue = 42 + (y % 20);
      } else {
        red = 30 + (y % 18);
        green = 72 + (x % 26);
        blue = 172 + ((x * 2 + y) % 64);
      }
      if (y > 34) {
        red = std::min(255, red + 28);
        green = std::min(255, green + 18);
        blue = std::min(255, blue + 24);
      }
      image.setPixelColor(x, y, QColor(red, green, blue, 255));
    }
  }
  return image;
}

QImage image_for_source(SourceKind source) {
  switch (source) {
    case SourceKind::Procedural:
      return source_image();
    case SourceKind::NightRecovery:
      return night_recovery_source_image();
    case SourceKind::HighlightRollback:
      return highlight_rollback_source_image();
    case SourceKind::Portrait:
      return portrait_source_image();
    case SourceKind::Landscape:
      return landscape_source_image();
    case SourceKind::SaturatedColor:
      return saturated_color_source_image();
  }
  return source_image();
}

photo_editor::desktop::EditRecipeState base_state() {
  photo_editor::desktop::EditRecipeState state;
  state.working_color_profile = QStringLiteral("Linear sRGB");
  state.preview_color_space = QStringLiteral("sRGB");
  state.output_color_space = QStringLiteral("sRGB");
  state.render_intent = QStringLiteral("Relative Colorimetric");
  return state;
}

photo_editor::desktop::EditRecipeState tone_color_state() {
  auto state = base_state();
  state.exposure = 0.45;
  state.contrast = 18.0;
  state.highlights = -12.0;
  state.shadows = 15.0;
  state.whites = 6.0;
  state.blacks = -9.0;
  state.temperature = 9.0;
  state.tint = -4.0;
  state.vibrance = 14.0;
  state.saturation = -5.0;
  state.curve_darks = -6.0;
  state.curve_lights = 8.0;
  return state;
}

photo_editor::desktop::EditRecipeState crop_state() {
  auto state = tone_color_state();
  state.crop_rect_x = 0.25;
  state.crop_rect_y = 0.125;
  state.crop_rect_width = 0.5;
  state.crop_rect_height = 0.75;
  return state;
}

photo_editor::desktop::EditRecipeState detail_state() {
  auto state = base_state();
  state.texture = 18.0;
  state.clarity = 11.0;
  state.sharpening = 22.0;
  state.sharpen_masking = 15.0;
  state.luminance_nr = 8.0;
  state.color_nr = 7.0;
  return state;
}

photo_editor::desktop::EditRecipeState night_recovery_state() {
  auto state = base_state();
  state.exposure = 1.25;
  state.brightness = 18.0;
  state.highlights = -42.0;
  state.shadows = 78.0;
  state.whites = 10.0;
  state.blacks = -8.0;
  state.texture = 10.0;
  state.clarity = 12.0;
  state.dehaze = 8.0;
  state.vibrance = 14.0;
  return state;
}

photo_editor::desktop::EditRecipeState highlight_rollback_state() {
  auto state = base_state();
  state.exposure = -0.12;
  state.highlights = -72.0;
  state.shadows = 24.0;
  state.whites = -32.0;
  state.blacks = -4.0;
  state.contrast = 8.0;
  return state;
}

photo_editor::desktop::EditRecipeState clean_portrait_preset_state() {
  auto state = base_state();
  state.exposure = 0.18;
  state.contrast = 10.0;
  state.highlights = -18.0;
  state.shadows = 16.0;
  state.whites = 8.0;
  state.blacks = -8.0;
  state.texture = -8.0;
  state.clarity = -6.0;
  state.sharpening = 22.0;
  state.sharpen_masking = 58.0;
  state.vibrance = 8.0;
  state.mixer_channels[0].saturation = -6.0;
  state.mixer_channels[1].saturation = 5.0;
  state.mixer_channels[1].luminance = 8.0;
  return state;
}

photo_editor::desktop::EditRecipeState landscape_pop_preset_state() {
  auto state = base_state();
  state.exposure = 0.08;
  state.contrast = 18.0;
  state.highlights = -28.0;
  state.shadows = 18.0;
  state.whites = 14.0;
  state.blacks = -14.0;
  state.texture = 18.0;
  state.clarity = 20.0;
  state.dehaze = 14.0;
  state.vibrance = 22.0;
  state.mixer_channels[3].saturation = 12.0;
  state.mixer_channels[5].saturation = 16.0;
  state.mixer_channels[5].luminance = -8.0;
  return state;
}

photo_editor::desktop::EditRecipeState monochrome_preset_state() {
  auto state = base_state();
  state.exposure = 0.12;
  state.contrast = 20.0;
  state.highlights = -18.0;
  state.shadows = 14.0;
  state.blacks = -18.0;
  state.black_and_white_enabled = true;
  state.black_and_white_mixer[0] = 12.0;
  state.black_and_white_mixer[2] = 8.0;
  state.black_and_white_mixer[5] = -10.0;
  return state;
}

photo_editor::desktop::EditRecipeState high_contrast_preset_state() {
  auto state = base_state();
  state.exposure = -0.05;
  state.contrast = 58.0;
  state.highlights = -24.0;
  state.shadows = -18.0;
  state.whites = 32.0;
  state.blacks = -36.0;
  state.filmic_shoulder = 16.0;
  state.filmic_toe = 18.0;
  return state;
}

photo_editor::desktop::EditRecipeState saturated_color_preset_state() {
  auto state = base_state();
  state.contrast = 12.0;
  state.vibrance = 42.0;
  state.saturation = 18.0;
  state.mixer_channels[0].saturation = 18.0;
  state.mixer_channels[3].saturation = 16.0;
  state.mixer_channels[5].saturation = 20.0;
  state.mixer_channels[5].luminance = -8.0;
  return state;
}

std::uint64_t fnv1a_append(std::uint64_t hash, std::uint64_t value) {
constexpr std::uint64_t prime = 1099511628211ULL;
  for (int i = 0; i < 8; ++i) {
    hash ^= (value >> (i * 8)) & 0xFFULL;
    hash *= prime;
  }
  return hash;
}

std::uint64_t image_hash(const QImage& image) {
  constexpr std::uint64_t offset = 14695981039346656037ULL;
  constexpr std::uint64_t prime = 1099511628211ULL;
  auto hash = offset;
  hash = fnv1a_append(hash, static_cast<std::uint64_t>(image.width()));
  hash = fnv1a_append(hash, static_cast<std::uint64_t>(image.height()));
  hash = fnv1a_append(hash, static_cast<std::uint64_t>(image.format()));
  for (int y = 0; y < image.height(); ++y) {
    const auto* scanline = image.constScanLine(y);
    for (qsizetype index = 0; index < image.bytesPerLine(); ++index) {
      hash ^= static_cast<std::uint64_t>(scanline[index]);
      hash *= prime;
    }
  }
  return hash;
}

constexpr qint64 kPreviewRefinementBudgetMs = 250;
constexpr qint64 kFinalRefinementBudgetMs = 1500;
constexpr int kPreviewRefinementScale = 2;
constexpr int kFinalRefinementScale = 4;

uint8_t matte_alpha(double value) {
  return static_cast<uint8_t>(std::clamp(std::lround(value), 0L, 255L));
}

template <typename AlphaFn>
std::vector<uint8_t> raster_matte(int width, int height, AlphaFn alpha_fn) {
  std::vector<uint8_t> matte(static_cast<std::size_t>(width * height), 0);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      matte[static_cast<std::size_t>(y * width + x)] = matte_alpha(alpha_fn(x, y));
    }
  }
  return matte;
}

std::vector<ColorPixel> matte_source_pixels(int width, int height, MatteScene scene) {
  std::vector<ColorPixel> pixels(static_cast<std::size_t>(width * height));
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      ColorPixel pixel{0.18F, 0.18F, 0.20F, 1.0F};
      switch (scene) {
        case MatteScene::Hair:
          pixel = (y < 3 || x < 3 || x > width - 4)
                      ? ColorPixel{0.20F, 0.24F, 0.30F, 1.0F}
                      : ColorPixel{0.72F, 0.44F, 0.31F, 1.0F};
          if ((x + y * 2) % 5 == 0 || (x > 5 && x < 10 && y < 9)) {
            pixel = ColorPixel{0.045F, 0.032F, 0.026F, 1.0F};
          }
          break;
        case MatteScene::TreeSkyline:
          pixel = y < 6 ? ColorPixel{0.36F, 0.56F, 0.88F, 1.0F}
                        : ColorPixel{0.10F, 0.29F, 0.09F, 1.0F};
          if (y >= 4 && ((x * 7 + y * 3) % 11) < 4) {
            pixel = ColorPixel{0.05F, 0.18F, 0.045F, 1.0F};
          }
          break;
        case MatteScene::ProductEdge:
          pixel = (x >= 5 && x <= 13 && y >= 2 && y <= 9)
                      ? ColorPixel{0.82F, 0.18F, 0.11F, 1.0F}
                      : ColorPixel{0.58F, 0.60F, 0.63F, 1.0F};
          break;
        case MatteScene::Glass:
          pixel = ColorPixel{0.18F + static_cast<float>(x) * 0.02F,
                             0.26F + static_cast<float>(y) * 0.015F,
                             0.38F + static_cast<float>((x + y) % 5) * 0.03F,
                             1.0F};
          break;
        case MatteScene::Smoke:
          pixel = ColorPixel{0.12F + static_cast<float>(y) * 0.02F,
                             0.13F + static_cast<float>(x % 4) * 0.03F,
                             0.15F + static_cast<float>(y % 5) * 0.025F,
                             1.0F};
          break;
        case MatteScene::Shadow:
          pixel = ColorPixel{0.56F + static_cast<float>(x % 3) * 0.025F,
                             0.50F + static_cast<float>(y) * 0.012F,
                             0.42F + static_cast<float>((x + y) % 4) * 0.02F,
                             1.0F};
          break;
      }
      pixels[static_cast<std::size_t>(y * width + x)] = pixel;
    }
  }
  return pixels;
}

QColor color_from_linear_pixel(const ColorPixel& pixel) {
  return QColor::fromRgbF(
      static_cast<float>(photo_editor::pipeline::clamp_unit(photo_editor::pipeline::linear_to_srgb(pixel.red))),
      static_cast<float>(photo_editor::pipeline::clamp_unit(photo_editor::pipeline::linear_to_srgb(pixel.green))),
      static_cast<float>(photo_editor::pipeline::clamp_unit(photo_editor::pipeline::linear_to_srgb(pixel.blue))),
      static_cast<float>(photo_editor::pipeline::clamp_unit(pixel.alpha)));
}

QImage image_from_pixels(const std::vector<ColorPixel>& pixels, int width, int height) {
  QImage image(width, height, QImage::Format_ARGB32);
  image.setColorSpace(photo_editor::desktop::preview_color_space());
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      image.setPixelColor(x, y, color_from_linear_pixel(pixels[static_cast<std::size_t>(y * width + x)]));
    }
  }
  return image;
}

QImage matte_image(const LocalMask& mask) {
  QImage image(mask.segmentation_width, mask.segmentation_height, QImage::Format_ARGB32);
  image.setColorSpace(photo_editor::desktop::preview_color_space());
  for (int y = 0; y < mask.segmentation_height; ++y) {
    for (int x = 0; x < mask.segmentation_width; ++x) {
      const auto alpha = mask.segmentation_mask[static_cast<std::size_t>(y * mask.segmentation_width + x)];
      image.setPixelColor(x, y, QColor::fromRgb(alpha, alpha, alpha));
    }
  }
  return image;
}

std::vector<ColorPixel> refined_effect_pixels(const MatteGoldenCase& test_case) {
  auto pixels = test_case.source_pixels;
  LocalMaskSet set;
  set.id = "zoomed-edge-qa-set";
  set.name = "Zoomed Edge QA";
  set.enabled = true;
  set.exposure = 1.35;
  set.temperature = 28.0;
  set.tint = -14.0;
  set.sources = {test_case.mask};

  ColorAdjustmentState state;
  state.color_pipeline.photographic_rendering_model =
      photo_editor::pipeline::PhotographicRenderingModel::
          LegacyOklchV1;
  state.local_mask_sets = {set};
  photo_editor::pipeline::apply_color_adjustments(&pixels, test_case.width, test_case.height, state);
  return pixels;
}

void blit_zoomed_panel(QImage* target, int origin_x, const QImage& source, int zoom) {
  for (int y = 0; y < source.height(); ++y) {
    for (int x = 0; x < source.width(); ++x) {
      const auto color = source.pixelColor(x, y);
      for (int dy = 0; dy < zoom; ++dy) {
        for (int dx = 0; dx < zoom; ++dx) {
          target->setPixelColor(origin_x + x * zoom + dx, y * zoom + dy, color);
        }
      }
    }
  }
}

QImage zoomed_edge_qa_screenshot(const MatteGoldenCase& test_case) {
  constexpr int kZoom = 6;
  constexpr int kGap = 3;
  const auto source_panel = image_from_pixels(test_case.source_pixels, test_case.width, test_case.height);
  const auto matte_panel = matte_image(test_case.mask);
  const auto refined_panel = image_from_pixels(refined_effect_pixels(test_case), test_case.width, test_case.height);
  const auto panel_width = test_case.width * kZoom;
  const auto panel_height = test_case.height * kZoom;
  QImage screenshot(panel_width * 3 + kGap * 2, panel_height, QImage::Format_ARGB32);
  screenshot.setColorSpace(photo_editor::desktop::preview_color_space());
  screenshot.fill(QColor::fromRgb(18, 22, 26));
  blit_zoomed_panel(&screenshot, 0, source_panel, kZoom);
  blit_zoomed_panel(&screenshot, panel_width + kGap, matte_panel, kZoom);
  blit_zoomed_panel(&screenshot, (panel_width + kGap) * 2, refined_panel, kZoom);
  return screenshot;
}

MatteGoldenCase scaled_matte_case(const MatteGoldenCase& test_case, int scale) {
  auto scaled = test_case;
  scaled.width = test_case.width * scale;
  scaled.height = test_case.height * scale;
  scaled.expected_hash = 0;
  scaled.expected_zoomed_screenshot_hash = 0;
  scaled.source_pixels.assign(static_cast<std::size_t>(scaled.width * scaled.height), ColorPixel{});
  scaled.mask.segmentation_width = scaled.width;
  scaled.mask.segmentation_height = scaled.height;
  scaled.mask.segmentation_mask.assign(static_cast<std::size_t>(scaled.width * scaled.height), 0);
  for (int y = 0; y < scaled.height; ++y) {
    const auto source_y = y / scale;
    for (int x = 0; x < scaled.width; ++x) {
      const auto source_x = x / scale;
      const auto scaled_index = static_cast<std::size_t>(y * scaled.width + x);
      const auto source_index = static_cast<std::size_t>(source_y * test_case.width + source_x);
      scaled.source_pixels[scaled_index] = test_case.source_pixels[source_index];
      scaled.mask.segmentation_mask[scaled_index] = test_case.mask.segmentation_mask[source_index];
    }
  }
  return scaled;
}

struct RefinementBudgetMeasurement final {
  qint64 elapsed_ms = 0;
  std::uint64_t hash_accumulator = 0;
};

RefinementBudgetMeasurement measure_refinement_budget(
    const std::array<MatteGoldenCase, 6>& test_cases,
    int scale) {
  QElapsedTimer timer;
  timer.start();
  std::uint64_t hash_accumulator = 0;
  for (const auto& test_case : test_cases) {
    const auto scaled = scaled_matte_case(test_case, scale);
    hash_accumulator ^= photo_editor::pipeline::refined_local_mask_hash(
        scaled.mask,
        scaled.width,
        scaled.height,
        scaled.source_pixels);
  }
  return RefinementBudgetMeasurement{
      .elapsed_ms = timer.elapsed(),
      .hash_accumulator = hash_accumulator,
  };
}

LocalMask matte_golden_mask(const std::string& id, const std::string& kind, int width, int height) {
  LocalMask mask;
  mask.id = id;
  mask.name = id;
  mask.kind = kind;
  mask.operation = "add";
  mask.enabled = true;
  mask.segmentation_width = width;
  mask.segmentation_height = height;
  return mask;
}

double pixel_luminance(const QColor& color) {
  return 0.2126 * color.redF() + 0.7152 * color.greenF() + 0.0722 * color.blueF();
}

double color_spread(const QColor& color) {
  return std::max({
      std::abs(static_cast<double>(color.redF()) - static_cast<double>(color.greenF())),
      std::abs(static_cast<double>(color.greenF()) - static_cast<double>(color.blueF())),
      std::abs(static_cast<double>(color.redF()) - static_cast<double>(color.blueF())),
  });
}

double average_luminance(const QImage& image, QRect region) {
  region = region.intersected(image.rect());
  if (region.isEmpty()) {
    return 0.0;
  }
  double total = 0.0;
  for (int y = region.top(); y <= region.bottom(); ++y) {
    for (int x = region.left(); x <= region.right(); ++x) {
      total += pixel_luminance(image.pixelColor(x, y));
    }
  }
  return total / static_cast<double>(region.width() * region.height());
}

QColor average_color(const QImage& image, QRect region) {
  region = region.intersected(image.rect());
  if (region.isEmpty()) {
    return QColor();
  }
  double red = 0.0;
  double green = 0.0;
  double blue = 0.0;
  for (int y = region.top(); y <= region.bottom(); ++y) {
    for (int x = region.left(); x <= region.right(); ++x) {
      const auto color = image.pixelColor(x, y);
      red += color.redF();
      green += color.greenF();
      blue += color.blueF();
    }
  }
  const auto scale = 1.0 / static_cast<double>(region.width() * region.height());
  return QColor::fromRgbF(
      static_cast<float>(red * scale),
      static_cast<float>(green * scale),
      static_cast<float>(blue * scale),
      1.0F);
}

double luminance_range(const QImage& image) {
  auto minimum = 1.0;
  auto maximum = 0.0;
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      const auto value = pixel_luminance(image.pixelColor(x, y));
      minimum = std::min(minimum, value);
      maximum = std::max(maximum, value);
    }
  }
  return maximum - minimum;
}

bool check_behavior(
    const GoldenCase& test_case,
    const QImage& input,
    const QImage& rendered) {
  switch (test_case.behavior_check) {
    case BehaviorCheck::None:
      return true;
    case BehaviorCheck::NightRecovery: {
      const auto before_shadow = average_luminance(input, QRect(30, 34, 18, 16));
      const auto after_shadow = average_luminance(rendered, QRect(30, 34, 18, 16));
      const auto after_highlight = average_luminance(rendered, QRect(39, 46, 12, 6));
      if (after_shadow < before_shadow + 0.045 || after_shadow > 0.72 || after_highlight < 0.50) {
        std::cerr << test_case.name << " should recover dark detail without flattening highlights; "
                  << "shadow before=" << before_shadow << " shadow after=" << after_shadow
                  << " highlight after=" << after_highlight << '\n';
        return false;
      }
      return true;
    }
    case BehaviorCheck::HighlightRollback: {
      const auto before_cloud = average_luminance(input, QRect(24, 10, 32, 12));
      const auto after_cloud = average_luminance(rendered, QRect(24, 10, 32, 12));
      const auto before_ground = average_luminance(input, QRect(18, 38, 20, 16));
      const auto after_ground = average_luminance(rendered, QRect(18, 38, 20, 16));
      if (after_cloud > before_cloud - 0.08 || after_ground < before_ground - 0.04) {
        std::cerr << test_case.name << " should pull bright highlight detail while holding mids; "
                  << "cloud before=" << before_cloud << " cloud after=" << after_cloud
                  << " ground before=" << before_ground << " ground after=" << after_ground
                  << '\n';
        return false;
      }
      return true;
    }
    case BehaviorCheck::PortraitPreset: {
      const auto before_skin = average_color(input, QRect(25, 30, 22, 20));
      const auto after_skin = average_color(rendered, QRect(25, 30, 22, 20));
      const auto before_luma = pixel_luminance(before_skin);
      const auto after_luma = pixel_luminance(after_skin);
      const auto before_warmth = before_skin.redF() - before_skin.greenF();
      const auto after_warmth = after_skin.redF() - after_skin.greenF();
      if (after_luma < before_luma || std::abs(after_warmth - before_warmth) > 0.08) {
        std::cerr << test_case.name << " should brighten skin without a strong hue drift; "
                  << "luma before=" << before_luma << " luma after=" << after_luma
                  << " warmth before=" << before_warmth << " warmth after=" << after_warmth
                  << '\n';
        return false;
      }
      return true;
    }
    case BehaviorCheck::LandscapePreset: {
      const auto before_foliage = average_luminance(input, QRect(12, 30, 18, 12));
      const auto after_foliage = average_luminance(rendered, QRect(12, 30, 18, 12));
      const auto before_sky = average_luminance(input, QRect(10, 5, 60, 12));
      const auto after_sky = average_luminance(rendered, QRect(10, 5, 60, 12));
      if (after_foliage < 0.18 || after_sky > before_sky + 0.12) {
        std::cerr << test_case.name << " should keep landscape foliage usable without blowing the sky; "
                  << "foliage before=" << before_foliage << " foliage after=" << after_foliage
                  << " sky before=" << before_sky << " sky after=" << after_sky << '\n';
        return false;
      }
      return true;
    }
    case BehaviorCheck::MonochromePreset: {
      const auto midtone = average_color(rendered, QRect(8, 8, 30, 20));
      const auto bright = average_color(rendered, QRect(24, 8, 18, 18));
      if (color_spread(midtone) > 0.012 || color_spread(bright) > 0.012) {
        std::cerr << test_case.name << " should render achromatic output; "
                  << "midtone spread=" << color_spread(midtone)
                  << " bright spread=" << color_spread(bright) << '\n';
        return false;
      }
      return true;
    }
    case BehaviorCheck::HighContrastPreset: {
      const auto before_contrast = luminance_range(input);
      const auto after_contrast = luminance_range(rendered);
      const auto before_mid = average_luminance(input, QRect(12, 12, 24, 18));
      const auto after_mid = average_luminance(rendered, QRect(12, 12, 24, 18));
      if (after_contrast < before_contrast + 0.008 ||
          std::abs(after_mid - before_mid) > 0.16) {
        std::cerr << test_case.name << " should increase tonal separation; "
                  << "contrast before=" << before_contrast
                  << " contrast after=" << after_contrast
                  << " mid before=" << before_mid
                  << " mid after=" << after_mid << '\n';
        return false;
      }
      return true;
    }
    case BehaviorCheck::SaturatedColorPreset: {
      const auto before_red = average_color(input, QRect(3, 8, 20, 28));
      const auto after_red = average_color(rendered, QRect(3, 8, 20, 28));
      const auto before_blue = average_color(input, QRect(60, 8, 20, 28));
      const auto after_blue = average_color(rendered, QRect(60, 8, 20, 28));
      if (color_spread(after_red) < color_spread(before_red) + 0.025 ||
          color_spread(after_blue) < color_spread(before_blue) + 0.020) {
        std::cerr << test_case.name << " should increase useful color separation; "
                  << "red before=" << color_spread(before_red)
                  << " red after=" << color_spread(after_red)
                  << " blue before=" << color_spread(before_blue)
                  << " blue after=" << color_spread(after_blue) << '\n';
        return false;
      }
      return true;
    }
  }
  return true;
}

std::array<MatteGoldenCase, 6> matte_golden_cases() {
  auto hair = matte_golden_mask("hair-strands", "hair", 16, 12);
  hair.selection_fine_edge = 72.0;
  hair.selection_smooth = 18.0;
  hair.selection_feather = 0.8;
  hair.selection_decontaminate = 32.0;
  hair.density = 0.92;
  hair.segmentation_mask = raster_matte(hair.segmentation_width, hair.segmentation_height, [](int x, int y) {
    const auto center = 8 + ((y % 3) - 1);
    auto alpha = std::max(0, 235 - std::abs(x - center) * 74);
    if ((x == 4 || x == 11) && y > 2 && y < 11) {
      alpha = std::max(alpha, 150);
    }
    if (((x + y * 2) % 5) == 0 && y > 1) {
      alpha = std::max(alpha, 190);
    }
    return y == 0 ? 0 : alpha;
  });

  auto tree_skyline = matte_golden_mask("tree-skyline", "sky", 18, 12);
  tree_skyline.selection_fine_edge = 34.0;
  tree_skyline.selection_edge_contrast = 68.0;
  tree_skyline.selection_feather = 0.7;
  tree_skyline.selection_remove_islands = 20.0;
  tree_skyline.segmentation_mask =
      raster_matte(tree_skyline.segmentation_width, tree_skyline.segmentation_height, [](int x, int y) {
        const auto skyline = 5 + ((x * 7) % 5) - ((x % 4) == 0 ? 2 : 0);
        auto alpha = y < skyline ? 255 : 0;
        if (y >= skyline - 1 && y <= skyline + 1 && ((x * 3 + y) % 4) == 0) {
          alpha = 96;
        }
        if ((x == 3 || x == 10 || x == 14) && y < skyline + 3) {
          alpha = 0;
        }
        return alpha;
      });

  auto product_edge = matte_golden_mask("product-edge", "object", 18, 12);
  product_edge.selection_edge_contrast = 82.0;
  product_edge.selection_harden = 70.0;
  product_edge.selection_smooth = 8.0;
  product_edge.segmentation_mask =
      raster_matte(product_edge.segmentation_width, product_edge.segmentation_height, [](int x, int y) {
        if (x >= 5 && x <= 13 && y >= 2 && y <= 9 && x + y < 23) {
          return 255;
        }
        if ((x == 4 || x == 14 || y == 1 || y == 10) && x + y < 24 && x > 3 && x < 15) {
          return 128;
        }
        return 0;
      });

  auto glass = matte_golden_mask("glass-rim", "object", 16, 14);
  glass.selection_fine_edge = 44.0;
  glass.selection_smooth = 38.0;
  glass.selection_decontaminate = 18.0;
  glass.density = 0.70;
  glass.opacity = 0.86;
  glass.segmentation_mask = raster_matte(glass.segmentation_width, glass.segmentation_height, [](int x, int y) {
    const auto dx = (static_cast<double>(x) - 7.5) / 5.2;
    const auto dy = (static_cast<double>(y) - 6.5) / 4.4;
    const auto distance = dx * dx + dy * dy;
    if (distance > 1.32) {
      return 0.0;
    }
    if (distance > 0.82) {
      return 220.0;
    }
    return 82.0 + static_cast<double>((x + y) % 4) * 12.0;
  });

  auto smoke = matte_golden_mask("smoke-plume", "subject", 16, 16);
  smoke.selection_smooth = 64.0;
  smoke.selection_blur = 2.4;
  smoke.selection_feather = 1.8;
  smoke.density = 0.58;
  smoke.segmentation_mask = raster_matte(smoke.segmentation_width, smoke.segmentation_height, [](int x, int y) {
    const auto left_dx = (static_cast<double>(x) - 6.0) / 4.6;
    const auto left_dy = (static_cast<double>(y) - 10.0) / 5.0;
    const auto right_dx = (static_cast<double>(x) - 10.0) / 4.2;
    const auto right_dy = (static_cast<double>(y) - 6.0) / 4.6;
    const auto soft = std::max(0.0, 1.0 - std::min(left_dx * left_dx + left_dy * left_dy,
                                                    right_dx * right_dx + right_dy * right_dy));
    return soft * 210.0 + static_cast<double>((x * 5 + y * 3) % 17);
  });

  auto shadow = matte_golden_mask("soft-shadow", "shadow", 18, 12);
  shadow.selection_blur = 2.8;
  shadow.selection_feather = 1.4;
  shadow.density = 0.48;
  shadow.opacity = 0.78;
  shadow.segmentation_mask = raster_matte(shadow.segmentation_width, shadow.segmentation_height, [](int x, int y) {
    const auto dx = (static_cast<double>(x) - 9.0) / 7.2;
    const auto dy = (static_cast<double>(y) - 8.6) / 2.5;
    const auto falloff = std::max(0.0, 1.0 - dx * dx - dy * dy);
    return falloff * 230.0;
  });

  return {{
      {"hair_strands",
       hair,
       16,
       12,
       matte_source_pixels(16, 12, MatteScene::Hair),
       17660421164854864732ULL,
       3107601297232624157ULL},
      {"tree_skyline",
       tree_skyline,
       18,
       12,
       matte_source_pixels(18, 12, MatteScene::TreeSkyline),
       6161603932179369100ULL,
       15077245805545685793ULL},
      {"product_edge",
       product_edge,
       18,
       12,
       matte_source_pixels(18, 12, MatteScene::ProductEdge),
       5885875138717952935ULL,
       12193730823585911697ULL},
      {"glass_rim",
       glass,
       16,
       14,
       matte_source_pixels(16, 14, MatteScene::Glass),
       181018888662949072ULL,
       7016832347411649145ULL},
      {"smoke_plume",
       smoke,
       16,
       16,
       matte_source_pixels(16, 16, MatteScene::Smoke),
       2666106805782139394ULL,
       17768615609523510981ULL},
      {"soft_shadow",
       shadow,
       18,
       12,
       matte_source_pixels(18, 12, MatteScene::Shadow),
       15645853762912712584ULL,
       14679861195300750417ULL},
  }};
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  const std::array<GoldenCase, 11> cases = {{
      {.name = "neutral", .state = base_state(), .expected_hash = 6502822787310648520ULL},
      {.name = "tone_color", .state = tone_color_state(), .expected_hash = 14864096399532332633ULL},
      {.name = "crop_tone_color",
       .state = crop_state(),
       .crop_before_render = true,
        .expected_hash = 5704213030300975520ULL},
      {.name = "detail", .state = detail_state(), .expected_hash = 14959644719180171112ULL},
      {.name = "night_recovery",
       .source = SourceKind::NightRecovery,
       .state = night_recovery_state(),
       .behavior_check = BehaviorCheck::NightRecovery,
       .expected_hash = 10976563582655933464ULL},
      {.name = "highlight_rollback",
       .source = SourceKind::HighlightRollback,
       .state = highlight_rollback_state(),
       .behavior_check = BehaviorCheck::HighlightRollback,
       .expected_hash = 11086711646603629176ULL},
      {.name = "clean_portrait_preset",
       .source = SourceKind::Portrait,
       .state = clean_portrait_preset_state(),
       .behavior_check = BehaviorCheck::PortraitPreset,
       .expected_hash = 17108444552814581490ULL},
      {.name = "landscape_pop_preset",
       .source = SourceKind::Landscape,
       .state = landscape_pop_preset_state(),
       .behavior_check = BehaviorCheck::LandscapePreset,
       .expected_hash = 18209443385856626905ULL},
      {.name = "monochrome_preset",
       .state = monochrome_preset_state(),
       .behavior_check = BehaviorCheck::MonochromePreset,
       .expected_hash = 5440983362028166651ULL},
      {.name = "high_contrast_preset",
       .state = high_contrast_preset_state(),
       .behavior_check = BehaviorCheck::HighContrastPreset,
       .expected_hash = 12106686771478516758ULL},
      {.name = "saturated_color_preset",
       .source = SourceKind::SaturatedColor,
       .state = saturated_color_preset_state(),
       .behavior_check = BehaviorCheck::SaturatedColorPreset,
       .expected_hash = 4756738322349470665ULL},
  }};

  auto mismatch_count = 0;
  for (const auto& test_case : cases) {
    auto input = image_for_source(test_case.source);
    if (test_case.crop_before_render) {
      input = photo_editor::desktop::apply_crop(input, test_case.state);
    }
    const auto rendered = photo_editor::desktop::render_output_image_with_backend(
        input,
        test_case.state,
        photo_editor::desktop::preview_color_space(),
        QImage::Format_ARGB32,
        photo_editor::desktop::RenderBackendPreference::CpuOnly);
    if (rendered.isNull()) {
      return fail("golden render should produce an image");
    }

    if (!check_behavior(test_case, input, rendered)) {
      ++mismatch_count;
    }

    const auto actual_hash = image_hash(rendered);
    if (actual_hash != test_case.expected_hash) {
      std::cerr << "golden hash mismatch for " << test_case.name << ": expected "
                << test_case.expected_hash << ", actual " << actual_hash << '\n';
      ++mismatch_count;
    }
  }

  const auto matte_cases = matte_golden_cases();
  if (matte_cases.size() != 6) {
    return fail("golden matte coverage should include hair, tree skyline, product edge, glass, smoke, and shadow");
  }
  for (const auto& test_case : matte_cases) {
    if (test_case.mask.segmentation_width != test_case.width ||
        test_case.mask.segmentation_height != test_case.height ||
        test_case.mask.segmentation_mask.size() !=
            static_cast<std::size_t>(test_case.width * test_case.height) ||
        test_case.source_pixels.size() != static_cast<std::size_t>(test_case.width * test_case.height)) {
      std::cerr << test_case.name << " golden matte fixture should match its declared dimensions\n";
      ++mismatch_count;
      continue;
    }

    const auto actual_hash = photo_editor::pipeline::refined_local_mask_hash(
        test_case.mask,
        test_case.width,
        test_case.height,
        test_case.source_pixels);
    if (actual_hash != test_case.expected_hash) {
      std::cerr << "golden matte hash mismatch for " << test_case.name << ": expected "
                << test_case.expected_hash << ", actual " << actual_hash << '\n';
      ++mismatch_count;
    }

    const auto zoomed_screenshot = zoomed_edge_qa_screenshot(test_case);
    if (zoomed_screenshot.isNull()) {
      std::cerr << test_case.name << " zoomed edge QA screenshot should render\n";
      ++mismatch_count;
      continue;
    }
    const auto screenshot_hash = image_hash(zoomed_screenshot);
    if (screenshot_hash != test_case.expected_zoomed_screenshot_hash) {
      std::cerr << "zoomed edge QA screenshot hash mismatch for " << test_case.name
                << ": expected " << test_case.expected_zoomed_screenshot_hash
                << ", actual " << screenshot_hash << '\n';
      ++mismatch_count;
    }
  }

  const auto preview_refinement_budget =
      measure_refinement_budget(matte_cases, kPreviewRefinementScale);
  if (preview_refinement_budget.hash_accumulator == 0 ||
      preview_refinement_budget.elapsed_ms > kPreviewRefinementBudgetMs) {
    std::cerr << "preview refinement budget exceeded: elapsed="
              << preview_refinement_budget.elapsed_ms << "ms budget="
              << kPreviewRefinementBudgetMs << "ms\n";
    ++mismatch_count;
  }

  const auto final_refinement_budget =
      measure_refinement_budget(matte_cases, kFinalRefinementScale);
  if (final_refinement_budget.hash_accumulator == 0 ||
      final_refinement_budget.elapsed_ms > kFinalRefinementBudgetMs) {
    std::cerr << "final-resolution refinement budget exceeded: elapsed="
              << final_refinement_budget.elapsed_ms << "ms budget="
              << kFinalRefinementBudgetMs << "ms\n";
    ++mismatch_count;
  }

  if (mismatch_count > 0) {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
