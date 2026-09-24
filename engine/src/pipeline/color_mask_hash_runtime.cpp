#include "color_mask_runtime.h"

#include "color_math_runtime.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace photo_editor::pipeline {

namespace {

[[nodiscard]] std::uint64_t hash_append(
    std::uint64_t hash,
    std::uint64_t value) {
  constexpr std::uint64_t kPrime = 1099511628211ULL;
  for (int byte = 0; byte < 8; ++byte) {
    hash ^= (value >> (byte * 8)) & 0xFFULL;
    hash *= kPrime;
  }
  return hash;
}

[[nodiscard]] std::uint64_t hash_append_string(
    std::uint64_t hash,
    std::string_view value) {
  constexpr std::uint64_t kPrime = 1099511628211ULL;
  for (const auto character : value) {
    hash ^= static_cast<unsigned char>(character);
    hash *= kPrime;
  }
  return hash_append(hash, static_cast<std::uint64_t>(value.size()));
}

template <typename WeightEvaluator>
[[nodiscard]] std::uint64_t refined_mask_hash(
    std::string_view schema,
    int width,
    int height,
    std::span<const ColorPixel> source_pixels,
    WeightEvaluator&& evaluate_weight) {
  constexpr std::uint64_t kOffset = 14695981039346656037ULL;
  const auto safe_width = std::max(width, 0);
  const auto safe_height = std::max(height, 0);
  auto hash = hash_append_string(kOffset, schema);
  hash = hash_append(hash, static_cast<std::uint64_t>(safe_width));
  hash = hash_append(hash, static_cast<std::uint64_t>(safe_height));
  if (safe_width <= 0 || safe_height <= 0) {
    return hash;
  }

  const auto pixel_count =
      static_cast<std::size_t>(safe_width) * static_cast<std::size_t>(safe_height);
  const auto has_source_pixels = source_pixels.size() == pixel_count;
  auto source_copy = std::vector<ColorPixel>{};
  if (has_source_pixels) {
    source_copy.assign(source_pixels.begin(), source_pixels.end());
  }
  const EdgeAwareMatteContext source_context{
      .pixels = has_source_pixels ? &source_copy : nullptr,
      .width = has_source_pixels ? safe_width : 0,
      .height = has_source_pixels ? safe_height : 0,
  };
  const auto* source_context_ptr = has_source_pixels ? &source_context : nullptr;
  const auto context = make_pipeline_color_context(ColorAdjustmentState{});
  constexpr auto kNeutralPixel = ColorPixel{0.5F, 0.5F, 0.5F, 1.0F};
  for (int y = 0; y < safe_height; ++y) {
    for (int x = 0; x < safe_width; ++x) {
      const auto index = static_cast<std::size_t>(y * safe_width + x);
      const auto& pixel = has_source_pixels ? source_copy[index] : kNeutralPixel;
      const auto normalized_x = safe_width > 1
                                    ? static_cast<double>(x) /
                                          static_cast<double>(safe_width - 1)
                                    : 0.5;
      const auto normalized_y = safe_height > 1
                                    ? static_cast<double>(y) /
                                          static_cast<double>(safe_height - 1)
                                    : 0.5;
      const auto weight = evaluate_weight(
          normalized_x, normalized_y, pixel, context, source_context_ptr);
      hash = hash_append(
          hash,
          static_cast<std::uint64_t>(
              std::llround(clamp_unit(weight) * 65535.0)));
    }
  }
  return hash;
}

}  // namespace

std::uint64_t refined_local_mask_hash(
    const LocalMask& mask,
    int width,
    int height,
    std::span<const ColorPixel> source_pixels) {
  return refined_mask_hash(
      "photo-editor-refined-local-mask-v1",
      width,
      height,
      source_pixels,
      [&mask](double x, double y, const ColorPixel& pixel,
              const PipelineColorContext& context,
              const EdgeAwareMatteContext* source_context) {
        return local_mask_weight(x, y, pixel, context, mask, source_context);
      });
}

std::uint64_t refined_local_mask_set_hash(
    const LocalMaskSet& mask_set,
    int width,
    int height,
    std::span<const ColorPixel> source_pixels) {
  return refined_mask_hash(
      "photo-editor-refined-local-mask-set-v1",
      width,
      height,
      source_pixels,
      [&mask_set](double x, double y, const ColorPixel& pixel,
                  const PipelineColorContext& context,
                  const EdgeAwareMatteContext* source_context) {
        return local_mask_set_weight(
            x, y, pixel, context, mask_set, source_context);
      });
}

}  // namespace photo_editor::pipeline
