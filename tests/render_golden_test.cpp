#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include <QColor>
#include <QColorSpace>
#include <QCoreApplication>
#include <QImage>

#include "edit_recipe_state.h"
#include "photo_render_utils.h"
#include "working_image.h"

namespace {

struct GoldenCase final {
  const char* name;
  photo_editor::desktop::EditRecipeState state;
  bool crop_before_render = false;
  std::uint64_t expected_hash = 0;
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

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  const std::array<GoldenCase, 4> cases = {{
      {.name = "neutral", .state = base_state(), .expected_hash = 6502822787310648520ULL},
      {.name = "tone_color", .state = tone_color_state(), .expected_hash = 15712138992483774888ULL},
      {.name = "crop_tone_color",
       .state = crop_state(),
       .crop_before_render = true,
       .expected_hash = 16465380009320025163ULL},
      {.name = "detail", .state = detail_state(), .expected_hash = 6825727587387937462ULL},
  }};

  auto mismatch_count = 0;
  for (const auto& test_case : cases) {
    auto input = source_image();
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

    const auto actual_hash = image_hash(rendered);
    if (actual_hash != test_case.expected_hash) {
      std::cerr << "golden hash mismatch for " << test_case.name << ": expected "
                << test_case.expected_hash << ", actual " << actual_hash << '\n';
      ++mismatch_count;
    }
  }

  if (mismatch_count > 0) {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
