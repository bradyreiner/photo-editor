#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

#include "photo_editor/pipeline/color_adjustments.h"

namespace {

using photo_editor::pipeline::ColorAdjustmentState;
using photo_editor::pipeline::ColorPixel;
using photo_editor::pipeline::LocalMask;

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

double luminance(const ColorPixel& pixel) {
  return static_cast<double>(pixel.red) * 0.2126 +
         static_cast<double>(pixel.green) * 0.7152 +
         static_cast<double>(pixel.blue) * 0.0722;
}

bool pixel_close(const ColorPixel& left, const ColorPixel& right, double tolerance = 1e-5) {
  return std::abs(static_cast<double>(left.red) - static_cast<double>(right.red)) <= tolerance &&
         std::abs(static_cast<double>(left.green) - static_cast<double>(right.green)) <= tolerance &&
         std::abs(static_cast<double>(left.blue) - static_cast<double>(right.blue)) <= tolerance &&
         std::abs(static_cast<double>(left.alpha) - static_cast<double>(right.alpha)) <= tolerance;
}

}  // namespace

int main() {
  constexpr auto width = 5;
  constexpr auto height = 5;
  const auto base_pixel = ColorPixel{
      .red = 0.24F,
      .green = 0.24F,
      .blue = 0.24F,
      .alpha = 1.0F,
  };
  std::vector<ColorPixel> pixels(width * height, base_pixel);

  ColorAdjustmentState state;
  state.local_masks = {
      LocalMask{
          .kind = "lasso",
          .operation = "add",
          .enabled = true,
          .exposure = 1.0,
          .feather = 0.0,
          .polygon_points = {
              {.x = 0.2, .y = 0.2},
              {.x = 0.8, .y = 0.2},
              {.x = 0.8, .y = 0.8},
              {.x = 0.2, .y = 0.8},
          },
      },
  };

  photo_editor::pipeline::apply_color_adjustments(&pixels, width, height, state);

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto center_x = (static_cast<double>(x) + 0.5) / static_cast<double>(width);
      const auto center_y = (static_cast<double>(y) + 0.5) / static_cast<double>(height);
      const auto expected_inside =
          center_x >= 0.2 && center_x <= 0.8 && center_y >= 0.2 && center_y <= 0.8;
      const auto& pixel = pixels[static_cast<std::size_t>(y * width + x)];
      const auto changed = luminance(pixel) > luminance(base_pixel) + 1e-4;
      if (expected_inside && !changed) {
        return fail("hard lasso fill should affect every pixel center inside the committed path");
      }
      if (!expected_inside && !pixel_close(pixel, base_pixel)) {
        return fail("hard lasso fill should not affect pixel centers outside the committed path");
      }
    }
  }

  return EXIT_SUCCESS;
}
