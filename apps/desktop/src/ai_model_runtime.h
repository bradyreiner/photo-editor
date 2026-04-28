#pragma once

#include <QImage>
#include <QByteArray>
#include <QString>
#include <QVector>

#include "vision_segmentation.h"

namespace photo_editor::desktop {

struct AiModelRuntimeStatus final {
  QString model_root;
  QString summary;
  bool smart_selection_available = false;
  bool inpaint_available = false;
};

struct AiInpaintRequest final {
  QImage source_image;
  QByteArray mask_data;
  int mask_width = 0;
  int mask_height = 0;
  QString retouch_mode;
};

struct AiInpaintResult final {
  bool success = false;
  QString error_message;
  QString model_name;
  QImage patch_image;
  int patch_x = 0;
  int patch_y = 0;
  int patch_width = 0;
  int patch_height = 0;
};

[[nodiscard]] AiModelRuntimeStatus ai_model_runtime_status();
[[nodiscard]] SegmentationResult generate_ai_smart_selection_mask(
    const QImage& source_image,
    const QVector<SmartSelectPrompt>& prompts);
[[nodiscard]] AiInpaintResult run_ai_inpaint(const AiInpaintRequest& request);

}  // namespace photo_editor::desktop
