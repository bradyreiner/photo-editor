#include "../engine/src/pipeline/color_mask_runtime.h"

#include <array>
#include <cmath>
#include <iostream>
#include <utility>
#include <vector>

using namespace photo_editor::pipeline;

#define CHECK(condition, message) \
  do { \
    if (!(condition)) { \
      std::cerr << "line " << __LINE__ << ": " << message << '\n'; \
      return 1; \
    } \
  } while (false)

namespace {

constexpr int kWidth = 9;
constexpr int kHeight = 9;
constexpr std::size_t kCenter = 4U * kWidth + 4U;

constexpr std::array<ColorPixel, 3> kSceneRgb{
    ColorPixel{-0.12F, 0.25F, 0.90F, 0.6F},
    ColorPixel{0.18F, 0.44F, 0.72F, 0.6F},
    ColorPixel{2.50F, -0.20F, 0.40F, 0.6F},
};

constexpr std::array<double, 3> kAlphaValues{0.0, 0.6, 1.0};
constexpr std::array<std::pair<double, double>, 5> kHealBlends{
    std::pair<double, double>{0.0, 0.0},
    std::pair<double, double>{0.0, 1.0},
    std::pair<double, double>{1.0, 0.0},
    std::pair<double, double>{1.0, 1.0},
    std::pair<double, double>{0.5, 0.5},
};

[[nodiscard]] bool close(float actual, float expected, float tolerance = 1.0e-5F) {
  return std::abs(actual - expected) <= tolerance;
}

[[nodiscard]] bool same_rgb(const ColorPixel& actual, const ColorPixel& expected) {
  return close(actual.red, expected.red) && close(actual.green, expected.green) &&
         close(actual.blue, expected.blue);
}

[[nodiscard]] bool all_finite(const std::vector<ColorPixel>& pixels) {
  for (const auto& pixel : pixels) {
    if (!std::isfinite(pixel.red) || !std::isfinite(pixel.green) ||
        !std::isfinite(pixel.blue) || !std::isfinite(pixel.alpha)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::vector<ColorPixel> constant_image(ColorPixel color) {
  return std::vector<ColorPixel>(static_cast<std::size_t>(kWidth * kHeight), color);
}

}  // namespace

int main() {
  // A constant scene field has no texture or color difference for healing to
  // introduce. Every blend pair should preserve its signed/HDR RGB exactly;
  // only the explicitly covered pixels may be touched, and alpha is invariant.
  for (const auto& scene_rgb : kSceneRgb) {
    for (const auto alpha : kAlphaValues) {
      const auto input_pixel = ColorPixel{scene_rgb.red, scene_rgb.green, scene_rgb.blue,
                                          static_cast<float>(alpha)};
      const auto original = constant_image(input_pixel);
      std::vector<double> partial_mask(original.size(), 0.0);
      partial_mask[kCenter] = 1.0;
      partial_mask[kCenter + 1U] = 0.5;

      for (const auto& [texture_blend, color_blend] : kHealBlends) {
        auto pixels = original;
        apply_local_removal(&pixels, kWidth, kHeight, partial_mask, 0.0,
                            RemovalQuality::Final, texture_blend, color_blend);
        CHECK(all_finite(pixels), "Removal must keep finite output for signed, SDR, and HDR fields");
        for (std::size_t index = 0; index < pixels.size(); ++index) {
          CHECK(pixels[index].alpha == original[index].alpha,
                "Removal must preserve alpha exactly, including zero and partial alpha");
          if (partial_mask[index] <= 1.0e-4) {
            CHECK(pixels[index] == original[index],
                  "Removal must leave every unmasked pixel unchanged");
          } else {
            CHECK(same_rgb(pixels[index], original[index]),
                  "Constant-field removal must preserve negative and above-one scene RGB at every blend");
          }
        }
      }
    }
  }

  // Empty coverage is a no-op. Full coverage without any known source context
  // also retains the input instead of clipping it merely because heal blending
  // selects the separated reconstruction branch.
  for (const auto& [texture_blend, color_blend] : kHealBlends) {
    const auto input_pixel = ColorPixel{2.5F, -0.2F, 0.4F, 0.6F};
    const auto original = constant_image(input_pixel);
    std::vector<double> empty_mask(original.size(), 0.0);
    std::vector<double> full_mask(original.size(), 1.0);

    auto empty_pixels = original;
    apply_local_removal(&empty_pixels, kWidth, kHeight, empty_mask, 0.0,
                        RemovalQuality::Final, texture_blend, color_blend);
    CHECK(empty_pixels == original, "Empty removal coverage must be an exact no-op");

    auto full_pixels = original;
    apply_local_removal(&full_pixels, kWidth, kHeight, full_mask, 0.0,
                        RemovalQuality::Final, texture_blend, color_blend);
    CHECK(all_finite(full_pixels), "Full removal coverage must keep finite output");
    for (std::size_t index = 0; index < full_pixels.size(); ++index) {
      CHECK(same_rgb(full_pixels[index], original[index]),
            "Full coverage without known context must not clip scene RGB");
      CHECK(full_pixels[index].alpha == original[index].alpha,
            "Full-coverage removal must preserve alpha");
    }
  }

  // The baked patch route shares separated healing with local removal. A patch
  // identical to its constant source should remain identical in signed/HDR
  // working RGB for alpha 0, partial alpha, and opaque pixels.
  const auto context = make_pipeline_color_context(ColorAdjustmentState{});
  for (const auto& scene_rgb : kSceneRgb) {
    for (const auto alpha : kAlphaValues) {
      const auto input_pixel = ColorPixel{scene_rgb.red, scene_rgb.green, scene_rgb.blue,
                                          static_cast<float>(alpha)};
      const auto original = constant_image(input_pixel);

      for (const auto& [texture_blend, color_blend] : kHealBlends) {
        LocalMask patch;
        patch.kind = "brush";
        patch.retouch_mode = "heal";
        patch.remove_strength = 100.0;
        patch.heal_texture_blend = texture_blend;
        patch.heal_color_blend = color_blend;
        patch.segmentation_width = kWidth;
        patch.segmentation_height = kHeight;
        patch.segmentation_mask.assign(original.size(), 0U);
        patch.segmentation_mask[kCenter] = 255U;
        patch.inpaint_patch_x = 0;
        patch.inpaint_patch_y = 0;
        patch.inpaint_patch_width = kWidth;
        patch.inpaint_patch_height = kHeight;
        patch.inpaint_patch_pixels = original;
        patch.inpaint_blend_amount = 1.0;
        patch.inpaint_seam_feather = 0.0;
        patch.inpaint_color_match = 0.0;

        auto pixels = original;
        const std::array<LocalMask, 1> patch_batch{patch};
        apply_local_inpaint_patches(&pixels, kWidth, kHeight, context, patch_batch);
        CHECK(all_finite(pixels), "Patch healing must keep finite signed/HDR output");
        for (std::size_t index = 0; index < pixels.size(); ++index) {
          CHECK(pixels[index].alpha == original[index].alpha,
                "Patch healing must preserve alpha exactly");
          if (index != kCenter) {
            CHECK(pixels[index] == original[index],
                  "Patch healing must leave every unmasked pixel unchanged");
          } else {
            CHECK(same_rgb(pixels[index], original[index]),
                  "Constant patch healing must preserve negative and above-one RGB at every blend");
          }
        }
      }
    }
  }

  std::cout << "HDR/signed retouch invariants passed\n";
  return 0;
}
