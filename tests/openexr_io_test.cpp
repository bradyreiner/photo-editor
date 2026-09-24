#include "photo_editor/io/openexr_io.h"

#include "photo_editor/pipeline/color_space.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

[[nodiscard]] bool approximately_equal(
    float left,
    float right,
    float tolerance) {
  return std::abs(left - right) <= tolerance;
}

}  // namespace

int main() {
  if (!photo_editor::io::openexr_backend_available()) {
    std::cout << "SKIP: OpenEXR backend unavailable\n";
    return 77;
  }
  const auto temp_root = std::filesystem::temp_directory_path();
  const auto half_path = temp_root / "photo-editor-openexr-half-test.exr";
  const auto float_path = temp_root / "photo-editor-openexr-float-test.exr";
  const auto offset_path = temp_root / "photo-editor-openexr-offset-test.exr";
  std::error_code remove_error;
  std::filesystem::remove(half_path, remove_error);
  std::filesystem::remove(float_path, remove_error);
  std::filesystem::remove(offset_path, remove_error);

  constexpr int kWidth = 3;
  constexpr int kHeight = 2;
  const std::vector<float> pixels = {
      -0.25F, 0.0F, 0.18F, 0.10F,
      1.0F, 2.0F, 4.0F, 0.25F,
      16.0F, 8.0F, 0.5F, 0.50F,
      0.01F, 0.02F, 0.03F, 0.75F,
      32.0F, 64.0F, 128.0F, 0.90F,
      0.4F, 0.3F, 0.2F, 1.0F,
  };
  const auto color_space =
      photo_editor::pipeline::make_linear_prophoto_rgb_color_space();
  const auto write_and_validate = [&](const std::filesystem::path& path,
                                      bool half_float) {
    std::string failure_reason;
    const auto wrote = photo_editor::io::write_openexr_rgba(
        path.string(),
        pixels,
        photo_editor::io::OpenExrWriteOptions{
            .width = kWidth,
            .height = kHeight,
            .row_stride_floats = static_cast<std::size_t>(kWidth) * 4U,
            .half_float = half_float,
            .include_alpha = true,
            .color_space = color_space,
            .white_luminance_nits = 100.0F,
            .rendering_model = "scene-linear-working-v1",
            .transform_provenance = "openexr-io-test:v1",
        },
        &failure_reason);
    if (!wrote) {
      std::cerr << "OpenEXR write failed: " << failure_reason << '\n';
      return false;
    }
    const auto inspection = photo_editor::io::inspect_openexr(path.string());
    if (!inspection.valid || inspection.width != kWidth ||
        inspection.height != kHeight || !inspection.has_alpha ||
        inspection.channel_type != (half_float ? "half" : "float") ||
        inspection.compression != "zip" ||
        inspection.color_space_name != "Linear ProPhoto RGB" ||
        inspection.transfer_function != "linear" ||
        inspection.rendering_model != "scene-linear-working-v1" ||
        inspection.transform_provenance != "openexr-io-test:v1" ||
        !approximately_equal(inspection.white_luminance_nits, 100.0F, 1.0e-6F) ||
        inspection.rgba_pixels.size() != pixels.size()) {
      std::cerr << "OpenEXR metadata/readback contract failed: "
                << inspection.failure_reason << '\n';
      return false;
    }
    const auto expected_chromaticities =
        photo_editor::io::openexr_chromaticities_for_color_space(color_space);
    if (!approximately_equal(
            inspection.chromaticities.red_x,
            expected_chromaticities.red_x,
            1.0e-6F) ||
        !approximately_equal(
            inspection.chromaticities.white_x,
            expected_chromaticities.white_x,
            1.0e-6F)) {
      std::cerr << "OpenEXR chromaticities did not survive artifact readback\n";
      return false;
    }
    const auto tolerance = half_float ? 0.065F : 1.0e-6F;
    for (std::size_t index = 0U; index < pixels.size(); ++index) {
      const auto scale = std::max(1.0F, std::abs(pixels[index]));
      if (std::abs(inspection.rgba_pixels[index] - pixels[index]) >
          tolerance * scale) {
        std::cerr << "OpenEXR pixel readback mismatch at component " << index
                  << ": expected=" << pixels[index]
                  << " actual=" << inspection.rgba_pixels[index] << '\n';
        return false;
      }
    }
    return true;
  };
  if (!write_and_validate(half_path, true) ||
      !write_and_validate(float_path, false)) {
    return EXIT_FAILURE;
  }

  std::string failure_reason;
  if (!photo_editor::io::write_openexr_rgba(
          offset_path.string(),
          pixels,
          photo_editor::io::OpenExrWriteOptions{
              .width = kWidth,
              .height = kHeight,
              .data_window_min_x = -17,
              .data_window_min_y = 23,
              .row_stride_floats = static_cast<std::size_t>(kWidth) * 4U,
              .half_float = false,
              .include_alpha = true,
              .color_space = color_space,
          },
          &failure_reason)) {
    std::cerr << "OpenEXR offset-window write failed: " << failure_reason << '\n';
    return EXIT_FAILURE;
  }
  const auto offset_inspection =
      photo_editor::io::inspect_openexr(offset_path.string());
  if (!offset_inspection.valid || offset_inspection.width != kWidth ||
      offset_inspection.height != kHeight ||
      offset_inspection.rgba_pixels != pixels) {
    std::cerr << "OpenEXR import must normalize non-zero data windows\n";
    return EXIT_FAILURE;
  }

  auto nonlinear = color_space;
  nonlinear.transfer_function = photo_editor::pipeline::TransferFunction::Srgb;
  if (photo_editor::io::write_openexr_rgba(
          half_path.string(),
          pixels,
          photo_editor::io::OpenExrWriteOptions{
              .width = kWidth,
              .height = kHeight,
              .row_stride_floats = static_cast<std::size_t>(kWidth) * 4U,
              .color_space = nonlinear,
          },
          &failure_reason) ||
      failure_reason != "invalid_openexr_write_options") {
    std::cerr << "OpenEXR writer must reject mislabeled nonlinear pixels\n";
    return EXIT_FAILURE;
  }

  if (photo_editor::io::write_openexr_rgba(
          half_path.string(),
          pixels,
          photo_editor::io::OpenExrWriteOptions{
              .width = kWidth,
              .height = kHeight,
              .row_stride_floats = static_cast<std::size_t>(kWidth) * 4U,
              .half_float = false,
              .color_space = photo_editor::pipeline::make_acescct_color_space(),
          },
          &failure_reason) ||
      failure_reason != "invalid_openexr_write_options") {
    std::cerr << "OpenEXR writer must not invent a proprietary ACEScct interchange container\n";
    return EXIT_FAILURE;
  }

  auto non_finite_pixels = pixels;
  non_finite_pixels[3] = std::numeric_limits<float>::quiet_NaN();
  if (photo_editor::io::write_openexr_rgba(
          half_path.string(),
          non_finite_pixels,
          photo_editor::io::OpenExrWriteOptions{
              .width = kWidth,
              .height = kHeight,
              .row_stride_floats = static_cast<std::size_t>(kWidth) * 4U,
              .color_space = color_space,
          },
          &failure_reason) ||
      failure_reason != "openexr_pixel_buffer_contains_non_finite_values") {
    std::cerr << "OpenEXR writer must reject non-finite pixel samples\n";
    return EXIT_FAILURE;
  }

  std::filesystem::remove(half_path, remove_error);
  std::filesystem::remove(float_path, remove_error);
  std::filesystem::remove(offset_path, remove_error);
  return EXIT_SUCCESS;
}
