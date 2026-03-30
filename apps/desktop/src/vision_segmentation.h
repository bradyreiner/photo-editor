#pragma once

#include <QByteArray>
#include <QImage>
#include <QSize>

namespace photo_editor::desktop {

struct SegmentationResult final {
  QByteArray mask_data;
  int width = 0;
  int height = 0;
  bool success = false;
  QString error_message;
};

struct ForegroundInstanceMap final {
  QByteArray instance_data;
  int width = 0;
  int height = 0;
  bool success = false;
  QString error_message;
};

[[nodiscard]] SegmentationResult generate_subject_mask(const QImage& source_image);
[[nodiscard]] ForegroundInstanceMap generate_foreground_instance_map(const QImage& source_image);
[[nodiscard]] SegmentationResult generate_object_mask_for_instance(
    const QImage& source_image,
    int instance_id);
[[nodiscard]] SegmentationResult generate_object_mask(
    const QImage& source_image,
    double normalized_x,
    double normalized_y);

[[nodiscard]] bool vision_segmentation_available();

}  // namespace photo_editor::desktop
