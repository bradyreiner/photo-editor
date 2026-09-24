#include "segmentation_mask_ops.h"

#include "edit_recipe_mask_schema.h"

#include <algorithm>
#include <cmath>

namespace photo_editor::desktop {

int selected_pixel_count_for_mask_data(const QByteArray& mask_data) {
  return static_cast<int>(std::count_if(
      mask_data.cbegin(), mask_data.cend(), [](char value) {
        return static_cast<unsigned char>(value) != 0U;
      }));
}

QByteArray union_segmentation_masks(
    const QByteArray& existing_data,
    int existing_width,
    int existing_height,
    const QByteArray& incoming_data,
    int incoming_width,
    int incoming_height) {
  if (!valid_segmentation_payload(
          incoming_data, incoming_width, incoming_height)) {
    return existing_data;
  }
  if (!valid_segmentation_payload(
          existing_data, existing_width, existing_height) ||
      existing_width != incoming_width || existing_height != incoming_height) {
    return incoming_data;
  }

  QByteArray merged(existing_data);
  for (int index = 0; index < merged.size(); ++index) {
    merged[index] = static_cast<char>(std::max(
        static_cast<unsigned char>(merged.at(index)),
        static_cast<unsigned char>(incoming_data.at(index))));
  }
  return merged;
}

QByteArray subtract_segmentation_mask(
    const QByteArray& existing_data,
    int existing_width,
    int existing_height,
    const QByteArray& incoming_data,
    int incoming_width,
    int incoming_height) {
  if (!valid_segmentation_payload(
          existing_data, existing_width, existing_height) ||
      !valid_segmentation_payload(
          incoming_data, incoming_width, incoming_height) ||
      existing_width != incoming_width || existing_height != incoming_height) {
    return existing_data;
  }

  QByteArray merged(existing_data);
  for (int index = 0; index < merged.size(); ++index) {
    const auto existing =
        static_cast<int>(static_cast<unsigned char>(merged.at(index)));
    const auto incoming =
        static_cast<int>(static_cast<unsigned char>(incoming_data.at(index)));
    merged[index] = static_cast<char>(std::max(0, existing - incoming));
  }
  return merged;
}

QByteArray resample_segmentation_mask(
    const QByteArray& source_data,
    int source_width,
    int source_height,
    int target_width,
    int target_height) {
  if (!valid_segmentation_payload(source_data, source_width, source_height) ||
      target_width <= 0 || target_height <= 0) {
    return {};
  }
  if (source_width == target_width && source_height == target_height) {
    return source_data;
  }

  QByteArray target_data(target_width * target_height, '\0');
  for (int y = 0; y < target_height; ++y) {
    const auto source_y =
        target_height > 1
            ? static_cast<double>(y) * static_cast<double>(source_height - 1) /
                  static_cast<double>(target_height - 1)
            : 0.0;
    const auto y0 = std::clamp(
        static_cast<int>(std::floor(source_y)), 0, source_height - 1);
    const auto y1 = std::min(source_height - 1, y0 + 1);
    const auto fy = source_y - static_cast<double>(y0);
    for (int x = 0; x < target_width; ++x) {
      const auto source_x =
          target_width > 1
              ? static_cast<double>(x) *
                    static_cast<double>(source_width - 1) /
                    static_cast<double>(target_width - 1)
              : 0.0;
      const auto x0 = std::clamp(
          static_cast<int>(std::floor(source_x)), 0, source_width - 1);
      const auto x1 = std::min(source_width - 1, x0 + 1);
      const auto fx = source_x - static_cast<double>(x0);
      const auto sample = [&](int sample_x, int sample_y) {
        return static_cast<double>(static_cast<unsigned char>(
            source_data.at(sample_y * source_width + sample_x)));
      };
      const auto top = std::lerp(sample(x0, y0), sample(x1, y0), fx);
      const auto bottom = std::lerp(sample(x0, y1), sample(x1, y1), fx);
      target_data[y * target_width + x] =
          static_cast<char>(std::lround(std::lerp(top, bottom, fy)));
    }
  }
  return target_data;
}

}  // namespace photo_editor::desktop
