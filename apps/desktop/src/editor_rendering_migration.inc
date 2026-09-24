#include "editor_preview_controller.h"

#include <QDateTime>

#include "photo_editor/pipeline/output_rendering.h"

namespace photo_editor::desktop {

QString EditorPreviewController::renderingUpgradeTargetModel() const {
  return QStringLiteral("aces2-ocio-v1");
}

bool EditorPreviewController::renderingUpgradeAvailable() const {
  return hasAsset() && !rendering_upgrade_preview_active_ &&
         photographic_rendering_model_ != renderingUpgradeTargetModel() &&
         photo_editor::pipeline::aces2_official_output_transform_available();
}

bool EditorPreviewController::renderingUpgradePreviewActive() const {
  return rendering_upgrade_preview_active_;
}

QString EditorPreviewController::renderingUpgradeStatus() const {
  if (rendering_upgrade_preview_active_) {
    return QStringLiteral("previewing");
  }
  if (!hasAsset()) {
    return QStringLiteral("no-asset");
  }
  if (photographic_rendering_model_ == renderingUpgradeTargetModel()) {
    return QStringLiteral("current");
  }
  if (!photo_editor::pipeline::aces2_official_output_transform_available()) {
    return QStringLiteral("official-processor-unavailable");
  }
  return QStringLiteral("available");
}

QVariantMap EditorPreviewController::renderingUpgradeEvidence() const {
  return rendering_upgrade_evidence_;
}

bool EditorPreviewController::startRenderingUpgradePreview() {
  const auto customer_edit_scope = customer_edit_access_.enter();
  if (!customer_edit_scope) return false;
  if (rendering_upgrade_preview_active_) {
    return true;
  }
  if (!renderingUpgradeAvailable()) {
    return false;
  }
  rendering_upgrade_source_model_ = photographic_rendering_model_;
  rendering_upgrade_evidence_.clear();
  rendering_upgrade_previous_compare_mode_ = compare_mode_;
  rendering_upgrade_preview_active_ = true;
  if (!compare_mode_) {
    compare_mode_ = true;
    emit compareModeChanged();
  }
  emit controlsChanged();
  submitDisplayTransformEditIntent(QStringLiteral("rendering_upgrade_preview"));
  return true;
}

bool EditorPreviewController::applyRenderingUpgrade() {
  const auto customer_edit_scope = customer_edit_access_.enter();
  if (!customer_edit_scope) return false;
  if (!rendering_upgrade_preview_active_ ||
      rendering_upgrade_evidence_.isEmpty() ||
      !photo_editor::pipeline::aces2_official_output_transform_available()) {
    return false;
  }
  rememberStateForUndo(QStringLiteral("Upgrade rendering to ACES 2"));
  photographic_rendering_model_ = renderingUpgradeTargetModel();
  renderer_transition_look_ =
      QStringLiteral("preserve-legacy-direct-luminance-v1");
  const auto restore_compare_mode = rendering_upgrade_previous_compare_mode_;
  rendering_upgrade_preview_active_ = false;
  rendering_upgrade_previous_compare_mode_ = false;
  rendering_upgrade_source_model_.clear();
  rendering_upgrade_evidence_.insert(
      QStringLiteral("migrationPending"),
      true);
  rendering_upgrade_evidence_.insert(
      QStringLiteral("approvalTime"),
      QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
  if (compare_mode_ != restore_compare_mode) {
    compare_mode_ = restore_compare_mode;
    emit compareModeChanged();
  }
  persistCurrentState();
  if (!qEnvironmentVariableIsSet(
          "PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE")) {
    persist_timer_.stop();
    flushPersistedState();
  }
  emit controlsChanged();
  submitDisplayTransformEditIntent(QStringLiteral("rendering_upgrade_apply"));
  scheduleThumbnailRefreshQueue(QStringLiteral("rendering_upgrade_apply"));
  return true;
}

void EditorPreviewController::cancelRenderingUpgradePreview() {
  if (!rendering_upgrade_preview_active_) {
    return;
  }
  const auto restore_compare_mode = rendering_upgrade_previous_compare_mode_;
  rendering_upgrade_preview_active_ = false;
  rendering_upgrade_previous_compare_mode_ = false;
  rendering_upgrade_source_model_.clear();
  rendering_upgrade_evidence_.clear();
  if (compare_mode_ != restore_compare_mode) {
    compare_mode_ = restore_compare_mode;
    emit compareModeChanged();
  }
  emit controlsChanged();
  if (hasAsset()) {
    submitDisplayTransformEditIntent(QStringLiteral("rendering_upgrade_cancel"));
  }
}

}  // namespace photo_editor::desktop
