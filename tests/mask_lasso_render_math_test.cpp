#include <algorithm>
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

  constexpr auto angled_width = 20;
  constexpr auto angled_height = 20;
  const std::vector<photo_editor::pipeline::CurvePoint> angled_polygon{
      {.x = 0.2, .y = 0.2},
      {.x = 0.72, .y = 0.35},
      {.x = 0.55, .y = 0.8},
      {.x = 0.25, .y = 0.65},
  };
  const auto render_angled_lasso = [&](
      const std::vector<photo_editor::pipeline::CurvePoint>& points) {
    std::vector<ColorPixel> result(
        angled_width * angled_height, base_pixel);
    ColorAdjustmentState angled_state;
    angled_state.local_masks = {
        LocalMask{
            .kind = "lasso",
            .operation = "add",
            .enabled = true,
            .exposure = 1.0,
            .feather = 0.0,
            .polygon_points = points,
        },
    };
    photo_editor::pipeline::apply_color_adjustments(
        &result, angled_width, angled_height, angled_state);
    return result;
  };
  const auto angled_pixels = render_angled_lasso(angled_polygon);
  auto reversed_angled_polygon = angled_polygon;
  std::reverse(reversed_angled_polygon.begin(), reversed_angled_polygon.end());
  const auto reversed_angled_pixels = render_angled_lasso(reversed_angled_polygon);
  const auto pixel_was_selected = [&](const std::vector<ColorPixel>& rendered, int x, int y) {
    return luminance(rendered[static_cast<std::size_t>(y * angled_width + x)]) >
           luminance(base_pixel) + 1e-4;
  };
  if (!pixel_was_selected(angled_pixels, 8, 9) ||
      pixel_was_selected(angled_pixels, 1, 9) ||
      pixel_was_selected(angled_pixels, 18, 9) ||
      pixel_was_selected(angled_pixels, 8, 1) ||
      pixel_was_selected(angled_pixels, 8, 18)) {
    return fail("angled lasso must stay within its authored bounds on both sides");
  }
  for (std::size_t index = 0; index < angled_pixels.size(); ++index) {
    if (!pixel_close(angled_pixels[index], reversed_angled_pixels[index])) {
      return fail("angled lasso coverage must be invariant under winding reversal");
    }
  }

  return EXIT_SUCCESS;
}
