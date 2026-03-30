#include "vision_segmentation.h"

#include <QByteArray>
#include <QtGlobal>

namespace photo_editor::desktop {

SegmentationResult generate_subject_mask(const QImage& /*source_image*/) {
  SegmentationResult result;
  result.error_message = QStringLiteral("AI subject masking is only available on macOS 14+");
  return result;
}

ForegroundInstanceMap generate_foreground_instance_map(const QImage& source_image) {
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION")) {
    ForegroundInstanceMap result;
    if (source_image.isNull() || source_image.width() < 2 || source_image.height() < 2) {
      result.error_message = QStringLiteral("Invalid source image");
      return result;
    }

    result.width = 4;
    result.height = 4;
    result.instance_data = QByteArray(16, '\0');
    for (int y = 0; y < result.height; ++y) {
      result.instance_data[y * result.width + 0] = static_cast<char>(1);
      result.instance_data[y * result.width + 1] = static_cast<char>(1);
      result.instance_data[y * result.width + 2] = static_cast<char>(2);
      result.instance_data[y * result.width + 3] = static_cast<char>(3);
    }
    result.success = true;
    return result;
  }

  ForegroundInstanceMap result;
  result.error_message = QStringLiteral("AI object masking is only available on macOS 14+");
  return result;
}

SegmentationResult generate_object_mask_for_instance(
    const QImage& source_image,
    int instance_id) {
  SegmentationResult result;
  const auto instance_map = generate_foreground_instance_map(source_image);
  if (!instance_map.success) {
    result.error_message = instance_map.error_message;
    return result;
  }

  if (instance_id <= 0) {
    result.error_message = QStringLiteral("No selectable object at that point");
    return result;
  }

  result.width = instance_map.width;
  result.height = instance_map.height;
  result.mask_data = QByteArray(result.width * result.height, '\0');
  for (int index = 0; index < instance_map.instance_data.size() &&
                      index < result.mask_data.size();
       ++index) {
    if (static_cast<unsigned char>(instance_map.instance_data.at(index)) ==
        static_cast<unsigned char>(instance_id)) {
      result.mask_data[index] = static_cast<char>(0xff);
    }
  }
  result.success = result.mask_data.contains(static_cast<char>(0xff));
  if (!result.success) {
    result.error_message = QStringLiteral("No selectable object at that point");
  }
  return result;
}

SegmentationResult generate_object_mask(
    const QImage& source_image,
    double normalized_x,
    double /*normalized_y*/) {
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION")) {
    const auto clamped_x = qBound(0.0, normalized_x, 1.0);
    const auto instance_id = clamped_x < 0.33 ? 1 : (clamped_x < 0.66 ? 2 : 3);
    return generate_object_mask_for_instance(source_image, instance_id);
  }

  SegmentationResult result;
  result.error_message = QStringLiteral("AI object masking is only available on macOS 14+");
  return result;
}

bool vision_segmentation_available() {
  return qEnvironmentVariableIsSet("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION");
}

}  // namespace photo_editor::desktop
