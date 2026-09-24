#pragma once

#include <QImage>
#include <QByteArray>
#include <QString>
#include <QVector>
#include <QtTypes>

#include "vision_segmentation.h"
#include "ai_inpaint_control.h"
#include <memory>

namespace photo_editor::desktop {

struct AiModelBundleVerification final {
  bool ok = false;
  bool manifest_available = false;
  bool inpaint_artifact_verified = false;
  QString root;
  QString manifest_path;
  QString inpaint_model_path;
  QString inpaint_sha256;
  qint64 inpaint_bytes = 0;
  int artifact_count = 0;
  QString error_code;
  QString error_message;
};

struct AiModelRuntimeStatus final {
  QString model_root;
  QString summary;
  QString diagnostics;
  QString smart_selection_runtime_label;
  QString inpaint_runtime_label;
  QString bundle_verification_error_code;
  QString bundle_verification_error;
  bool smart_selection_available = false;
  bool inpaint_available = false;
  bool bundle_verified = false;
  bool inpaint_artifact_verified = false;
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
  std::shared_ptr<AiInpaintControl> control;
};

struct AiInpaintResult final {
  bool cancelled = false;
  bool success = false;
  QString error_message;
  QString model_name;
  QString runtime_provider;
  QString acceleration_warning;
  QString alternate_model_name;
  QString candidate_selection;
  QString prompt;
  QString quality_mode;
  int seed = 1;
  bool fallback = false;
  QString region_kind;
  bool edge_continuation = false;
  bool texture_synthesis = false;
  bool texture_exemplar = false;
  bool alternate_candidate_evaluated = false;
  double texture_strength = 0.0;
  double texture_residual_mean = 0.0;
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
void shutdown_ai_model_runtime();
[[nodiscard]] AiModelBundleVerification verify_ai_model_bundle(
    const QString& model_root);
[[nodiscard]] SegmentationResult generate_ai_smart_selection_mask(
    const QImage& source_image,
    const QVector<SmartSelectPrompt>& prompts);
[[nodiscard]] AiInpaintResult run_ai_inpaint(const AiInpaintRequest& request);

}  // namespace photo_editor::desktop
