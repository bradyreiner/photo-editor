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
  QString diagnostics;
  QString smart_selection_runtime_label;
  QString inpaint_runtime_label;
  bool smart_selection_available = false;
  bool inpaint_available = false;
};

struct AiInpaintRequest final {
  QImage source_image;
  QByteArray mask_data;
  int mask_width = 0;
  int mask_height = 0;
  QString retouch_mode;
  QString prompt;
  QString quality_mode;
  int seed = 1;
  double heal_texture_blend = 0.68;
  double heal_color_blend = 0.46;
  bool patch_source_set = false;
  double patch_source_x = 0.5;
  double patch_source_y = 0.5;
  double patch_source_width = 0.18;
  double patch_source_height = 0.18;
};

struct AiInpaintResult final {
  bool success = false;
  QString error_message;
  QString model_name;
  QString prompt;
  QString quality_mode;
  int seed = 1;
  bool fallback = false;
  QString region_kind;
  bool edge_continuation = false;
  bool texture_synthesis = false;
  double texture_strength = 0.0;
  double region_coverage = 0.0;
  int region_x = 0;
  int region_y = 0;
  int region_width = 0;
  int region_height = 0;
  QImage patch_image;
  QVector<QImage> candidate_images;
  int candidate_index = 0;
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
