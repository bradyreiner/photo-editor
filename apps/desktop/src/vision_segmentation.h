#pragma once

#include <QByteArray>
#include <QImage>
#include <QSize>
#include <QVector>

namespace photo_editor::desktop {

struct SmartSelectPrompt final {
  double x = 0.5;
  double y = 0.5;
  bool negative = false;
};

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
[[nodiscard]] SegmentationResult generate_semantic_mask(
    const QImage& source_image,
    const QString& semantic_kind);
[[nodiscard]] ForegroundInstanceMap generate_foreground_instance_map(const QImage& source_image);
[[nodiscard]] SegmentationResult generate_object_mask_for_instance(
    const QImage& source_image,
    int instance_id);
[[nodiscard]] SegmentationResult generate_object_mask_for_instances(
    const QImage& source_image,
    const QVector<int>& instance_ids);
[[nodiscard]] SegmentationResult generate_object_mask(
    const QImage& source_image,
    double normalized_x,
    double normalized_y);
[[nodiscard]] SegmentationResult generate_smart_select_mask(
    const QImage& source_image,
    const QVector<SmartSelectPrompt>& prompts);

[[nodiscard]] bool vision_segmentation_available();

}  // namespace photo_editor::desktop
