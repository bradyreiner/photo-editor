#include "vision_segmentation.h"
#include "prompt_mask_engine.h"

#include <QByteArray>
#include <QtGlobal>

namespace photo_editor::desktop {

namespace {

SegmentationResult fake_foreground_mask(const QImage& source_image) {
  SegmentationResult result;
  if (source_image.isNull() || source_image.width() < 2 || source_image.height() < 2) {
    result.error_message = QStringLiteral("Invalid source image");
    return result;
  }

  result.width = 8;
  result.height = 8;
  result.mask_data = QByteArray(result.width * result.height, '\0');
  for (int y = 2; y <= 5; ++y) {
    for (int x = 2; x <= 5; ++x) {
      result.mask_data[y * result.width + x] = static_cast<char>(0xff);
    }
  }
  result.success = true;
  return result;
}

SegmentationResult inverted_mask(SegmentationResult result) {
  if (!result.success) {
    return result;
  }
  for (auto index = 0; index < result.mask_data.size(); ++index) {
    const auto value = static_cast<unsigned char>(result.mask_data.at(index));
    result.mask_data[index] = static_cast<char>(0xff - value);
  }
  return result;
}

SegmentationResult fake_sky_mask(const QImage& source_image) {
  SegmentationResult result;
  if (source_image.isNull() || source_image.width() < 2 || source_image.height() < 2) {
    result.error_message = QStringLiteral("Invalid source image");
    return result;
  }

  result.width = 8;
  result.height = 8;
  result.mask_data = QByteArray(result.width * result.height, '\0');
  for (int y = 0; y < 3; ++y) {
    for (int x = 0; x < result.width; ++x) {
      result.mask_data[y * result.width + x] = static_cast<char>(0xff);
    }
  }
  result.success = true;
  return result;
}

QString normalized_semantic_kind(const QString& semantic_kind) {
  const auto normalized = semantic_kind.trimmed().toLower();
  return normalized == QStringLiteral("person") ? QStringLiteral("people") : normalized;
}

}  // namespace

SegmentationResult generate_subject_mask(const QImage& source_image) {
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION")) {
    return fake_foreground_mask(source_image);
  }

  SegmentationResult result;
  result.error_message = QStringLiteral("AI subject masking is only available on macOS 14+");
  return result;
}

SegmentationResult generate_semantic_mask(const QImage& source_image, const QString& semantic_kind) {
  const auto kind = normalized_semantic_kind(semantic_kind);
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION")) {
    if (kind == QStringLiteral("sky")) {
      return fake_sky_mask(source_image);
    }
    if (kind == QStringLiteral("background")) {
      return inverted_mask(fake_foreground_mask(source_image));
    }
    return fake_foreground_mask(source_image);
  }

  SegmentationResult result;
  result.error_message = QStringLiteral("AI semantic masking is only available on macOS 14+");
  return result;
}

ForegroundInstanceMap generate_foreground_instance_map(const QImage& source_image) {
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION")) {
    ForegroundInstanceMap result;
    if (source_image.isNull() || source_image.width() < 2 || source_image.height() < 2) {
      result.error_message = QStringLiteral("Invalid source image");
      return result;
    }

    result.width = 8;
    result.height = 8;
    result.instance_data = QByteArray(result.width * result.height, '\0');
    for (int y = 2; y <= 5; ++y) {
      result.instance_data[y * result.width + 1] = static_cast<char>(1);
      result.instance_data[y * result.width + 2] = static_cast<char>(1);
      result.instance_data[y * result.width + 3] = static_cast<char>(2);
      result.instance_data[y * result.width + 4] = static_cast<char>(2);
      result.instance_data[y * result.width + 5] = static_cast<char>(3);
      result.instance_data[y * result.width + 6] = static_cast<char>(3);
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
  return generate_object_mask_for_instances(source_image, QVector<int>{instance_id});
}

SegmentationResult generate_object_mask_for_instances(
    const QImage& source_image,
    const QVector<int>& instance_ids) {
  SegmentationResult result;
  const auto instance_map = generate_foreground_instance_map(source_image);
  if (!instance_map.success) {
    result.error_message = instance_map.error_message;
    return result;
  }

  QVector<int> normalized_ids;
  normalized_ids.reserve(instance_ids.size());
  for (const auto instance_id : instance_ids) {
    if (instance_id > 0 && !normalized_ids.contains(instance_id)) {
      normalized_ids.append(instance_id);
    }
  }

  if (normalized_ids.isEmpty()) {
    result.error_message = QStringLiteral("No selectable object at that point");
    return result;
  }

  result.width = instance_map.width;
  result.height = instance_map.height;
  result.mask_data = QByteArray(result.width * result.height, '\0');
  for (int index = 0; index < instance_map.instance_data.size() &&
                      index < result.mask_data.size();
       ++index) {
    const auto instance_value =
        static_cast<int>(static_cast<unsigned char>(instance_map.instance_data.at(index)));
    if (normalized_ids.contains(instance_value)) {
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
    double normalized_y) {
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION")) {
    QVector<SmartSelectPrompt> prompts;
    prompts.append(SmartSelectPrompt{
        .x = qBound(0.0, normalized_x, 1.0),
        .y = qBound(0.0, normalized_y, 1.0),
        .negative = false,
    });
    return generate_smart_select_mask(source_image, prompts);
  }

  SegmentationResult result;
  result.error_message = QStringLiteral("AI object masking is only available on macOS 14+");
  return result;
}

SegmentationResult generate_smart_select_mask(
    const QImage& source_image,
    const QVector<SmartSelectPrompt>& prompts) {
  QVector<prompt_mask_engine::PromptPoint> normalized_prompts;
  normalized_prompts.reserve(prompts.size());
  for (const auto& prompt : prompts) {
    normalized_prompts.push_back(prompt_mask_engine::PromptPoint{
        .x = prompt.x,
        .y = prompt.y,
        .negative = prompt.negative,
    });
  }

  const auto result = prompt_mask_engine::generate_mask(source_image, normalized_prompts);
  return SegmentationResult{
      .mask_data = result.mask_data,
      .width = result.width,
      .height = result.height,
      .success = result.success,
      .error_message = result.error_message,
  };
}

bool vision_segmentation_available() {
  return qEnvironmentVariableIsSet("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION");
}

}  // namespace photo_editor::desktop
