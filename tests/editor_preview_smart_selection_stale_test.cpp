#include <cstdlib>
#include <iostream>

#include <QColor>
#include <QCoreApplication>
#include <QEventLoop>
#include <QImage>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QThread>

#include "editor_preview_controller.h"

namespace photo_editor::desktop {

struct EditorPreviewControllerTestAccess {
  static int smart_select_preview_generation(const EditorPreviewController& controller) {
    return controller.mask_session_.smartPreviewGeneration();
  }

  static int smart_selection_job_generation(const EditorPreviewController& controller) {
    return controller.mask_session_.smartSelectionGeneration();
  }

  static int smart_selection_cancelled_count(const EditorPreviewController& controller) {
    return controller.mask_session_.smartSelectionCancelledCount();
  }

  static int mask_work_state_revision(const EditorPreviewController& controller) {
    return controller.mask_session_.workRevision();
  }

  static QVariantMap last_mask_work_telemetry(const EditorPreviewController& controller) {
    return controller.last_mask_work_telemetry_;
  }

  static QVariantList smart_select_preview_prompts(const EditorPreviewController& controller) {
    return controller.smart_select_preview_prompts_;
  }

  static int smart_select_preview_duplicate_skip_count(
      const EditorPreviewController& controller) {
    return controller.smart_select_preview_duplicate_skip_count_;
  }

  static bool smart_select_preview_request_pending(
      const EditorPreviewController& controller) {
    return controller.mask_worker_coordinator_.smartSelectPreviewPending();
  }

  static bool smart_select_preview_timer_active(
      const EditorPreviewController& controller) {
    return controller.smart_select_preview_timer_.isActive();
  }

  static bool smart_select_commit_request_pending(
      const EditorPreviewController& controller) {
    return controller.mask_worker_coordinator_.smartSelectCommitPending();
  }

  static bool smart_select_commit_pending(
      const EditorPreviewController& controller) {
    return controller.smart_select_commit_pending_;
  }

  static bool interactive_render_pending(const EditorPreviewController& controller) {
    return controller.interactive_request_pending_;
  }

  static bool full_render_pending(const EditorPreviewController& controller) {
    return controller.full_request_pending_;
  }

  static bool overlay_render_pending(const EditorPreviewController& controller) {
    return controller.overlay_request_pending_;
  }

  static int render_generation(const EditorPreviewController& controller) {
    return controller.preview_session_.generation();
  }

  static EditIntent last_edit_intent(const EditorPreviewController& controller) {
    return controller.last_edit_intent_;
  }

  static void seed_pending_asset_scoped_work(EditorPreviewController& controller) {
    controller.interactive_request_pending_ = true;
    controller.full_request_pending_ = true;
    controller.overlay_request_pending_ = true;
    controller.pending_interactive_request_ = controller.currentRenderRequest(false);
    controller.pending_full_request_ = controller.currentRenderRequest(true);
    controller.pending_overlay_request_ = controller.currentRenderRequest(false);
    controller.mask_worker_coordinator_.setPendingSmartSelectPreview({});
    controller.mask_worker_coordinator_.setPendingSmartSelectCommit({});
    controller.smart_select_commit_pending_ = true;
    controller.smart_select_commit_intent_ = QStringLiteral("selection");
    controller.smart_select_commit_create_new_ = true;
    controller.updateProcessingState();
  }

  static void cancel_pending_asset_scoped_work(EditorPreviewController& controller) {
    controller.cancelPendingAssetScopedWork();
  }
};

}  // namespace photo_editor::desktop

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

QByteArray json_for_variant(const QVariant& value) {
  return QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact);
}

bool wait_for_preview_idle(
    photo_editor::desktop::EditorPreviewController& controller,
    int timeout_ms = 3000) {
  auto elapsed_ms = 0;
  while (elapsed_ms < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    if (!controller.processing()) {
      return true;
    }
    QThread::msleep(25);
    elapsed_ms += 25;
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  return !controller.processing();
}

bool wait_for_smart_cancel(
    const photo_editor::desktop::EditorPreviewController& controller,
    int cancelled_count_before,
    int timeout_ms = 3000) {
  auto elapsed_ms = 0;
  while (elapsed_ms < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    if (photo_editor::desktop::EditorPreviewControllerTestAccess::
            smart_selection_cancelled_count(controller) > cancelled_count_before) {
      return true;
    }
    QThread::msleep(25);
    elapsed_ms += 25;
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  return photo_editor::desktop::EditorPreviewControllerTestAccess::
             smart_selection_cancelled_count(controller) > cancelled_count_before;
}

bool wait_for_mask_count(
    photo_editor::desktop::EditorPreviewController& controller,
    int expected_count,
    int timeout_ms = 3000) {
  auto elapsed_ms = 0;
  while (elapsed_ms < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    if (controller.maskCount() == expected_count && !controller.processing()) {
      return true;
    }
    QThread::msleep(25);
    elapsed_ms += 25;
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  return controller.maskCount() == expected_count && !controller.processing();
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  qputenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION", "1");

  QTemporaryDir runtime_root;
  if (!runtime_root.isValid()) {
    return fail("smart stale runtime directory should be available");
  }

  const auto source_path =
      runtime_root.filePath(QStringLiteral("smart-stale-source.png"));
  QImage source(96, 96, QImage::Format_ARGB32);
  source.fill(Qt::white);
  for (int y = 38; y <= 57; ++y) {
    for (int x = 10; x <= 25; ++x) {
      source.setPixelColor(x, y, QColor(214, 78, 78));
    }
    for (int x = 40; x <= 55; ++x) {
      source.setPixelColor(x, y, QColor(74, 118, 212));
    }
  }
  if (!source.save(source_path)) {
    return fail("smart stale source image should be writable");
  }
  const auto scheduled_source_path =
      runtime_root.filePath(QStringLiteral("smart-scheduled-source.png"));
  if (!source.save(scheduled_source_path)) {
    return fail("scheduled smart source image should be writable");
  }

  photo_editor::desktop::EditorPreviewController stale_preview_controller;
  stale_preview_controller.loadAsset(
      QStringLiteral("asset-stale-smart-preview-focused"),
      source_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(stale_preview_controller)) {
    return fail("stale smart preview controller should finish its initial preview");
  }
  const auto stale_preview_generation_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          smart_select_preview_generation(stale_preview_controller);
  const auto stale_preview_cancelled_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          smart_selection_cancelled_count(stale_preview_controller);
  stale_preview_controller.scheduleSmartSelectPreview(0.18, 0.5);
  const auto stale_preview_request_generation =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          smart_select_preview_generation(stale_preview_controller);
  stale_preview_controller.setExposure(0.35);
  if (!wait_for_smart_cancel(
          stale_preview_controller,
          stale_preview_cancelled_before)) {
    return fail("smart hover preview should cancel after edit state changes before publish");
  }
  const auto stale_preview_telemetry =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          last_mask_work_telemetry(stale_preview_controller);
  if (stale_preview_request_generation != stale_preview_generation_before + 1 ||
      stale_preview_controller.smartSelectPreviewActive() ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              smart_selection_cancelled_count(stale_preview_controller) !=
          stale_preview_cancelled_before + 1 ||
      stale_preview_telemetry.value(QStringLiteral("operation")).toString() !=
          QStringLiteral("smart_selection_preview") ||
      !stale_preview_telemetry.value(QStringLiteral("cancelled")).toBool() ||
      stale_preview_telemetry.value(QStringLiteral("cancellation_reason")).toString() !=
          QStringLiteral("stale_result") ||
      stale_preview_telemetry.value(QStringLiteral("stage")).toString() !=
          QStringLiteral("preview_publish") ||
      stale_preview_telemetry.value(QStringLiteral("job_generation")).toInt() !=
          stale_preview_request_generation ||
      stale_preview_telemetry.value(QStringLiteral("async_job_kind")).toString() !=
          QStringLiteral("smart_selection_preview") ||
      stale_preview_telemetry.value(QStringLiteral("async_job_stage")).toString() !=
          QStringLiteral("preview_publish") ||
      stale_preview_telemetry.value(QStringLiteral("async_job_asset_id")).toString() !=
          QStringLiteral("asset-stale-smart-preview-focused") ||
      stale_preview_telemetry.value(QStringLiteral("async_job_file_path")).toString() !=
          source_path ||
      !stale_preview_telemetry.value(QStringLiteral("async_job_state_signature"))
           .toString()
           .startsWith(QStringLiteral("sha1:"))) {
    return fail("smart hover preview should reject stale publish results with async job provenance");
  }

  photo_editor::desktop::EditorPreviewController stale_commit_controller;
  stale_commit_controller.loadAsset(
      QStringLiteral("asset-stale-smart-commit-focused"),
      source_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(stale_commit_controller)) {
    return fail("stale smart commit controller should finish its initial preview");
  }
  const auto stale_commit_cancelled_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          smart_selection_cancelled_count(stale_commit_controller);
  stale_commit_controller.smartSelectObjectAt(0.18, 0.5, true);
  const auto stale_commit_request_generation =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          smart_selection_job_generation(stale_commit_controller);
  if (!stale_commit_controller.processing() ||
      stale_commit_controller.maskCount() != 0) {
    return fail("smart commit should be pending before a mask-stack change invalidates it");
  }
  stale_commit_controller.startSelectionBrush(false);
  const auto revision_after_brush =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          mask_work_state_revision(stale_commit_controller);
  if (!wait_for_smart_cancel(
          stale_commit_controller,
          stale_commit_cancelled_before)) {
    return fail("smart commit should cancel after the active mask stack changes before publish");
  }
  const auto stale_commit_telemetry =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          last_mask_work_telemetry(stale_commit_controller);
  if (stale_commit_controller.maskCount() != 1 ||
      stale_commit_controller.activeMaskKind() != QStringLiteral("brush") ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              mask_work_state_revision(stale_commit_controller) != revision_after_brush ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              smart_selection_cancelled_count(stale_commit_controller) !=
          stale_commit_cancelled_before + 1 ||
      stale_commit_telemetry.value(QStringLiteral("operation")).toString() !=
          QStringLiteral("smart_selection") ||
      !stale_commit_telemetry.value(QStringLiteral("cancelled")).toBool() ||
      stale_commit_telemetry.value(QStringLiteral("cancellation_reason")).toString() !=
          QStringLiteral("stale_result") ||
      stale_commit_telemetry.value(QStringLiteral("stage")).toString() !=
          QStringLiteral("async_commit_publish") ||
      stale_commit_telemetry.value(QStringLiteral("job_generation")).toInt() !=
          stale_commit_request_generation ||
      stale_commit_telemetry.value(QStringLiteral("async_job_kind")).toString() !=
          QStringLiteral("smart_selection") ||
      stale_commit_telemetry.value(QStringLiteral("async_job_stage")).toString() !=
          QStringLiteral("async_commit_publish") ||
      stale_commit_telemetry.value(QStringLiteral("async_job_asset_id")).toString() !=
          QStringLiteral("asset-stale-smart-commit-focused") ||
      stale_commit_telemetry.value(QStringLiteral("async_job_file_path")).toString() !=
          source_path ||
      !stale_commit_telemetry.value(QStringLiteral("async_job_state_signature"))
           .toString()
           .startsWith(QStringLiteral("sha1:")) ||
      !stale_commit_controller.status().contains(QStringLiteral("canceled"))) {
    return fail("smart commit should reject stale async publish results after mask-stack changes");
  }

  photo_editor::desktop::EditorPreviewController asset_switch_cancel_controller;
  asset_switch_cancel_controller.loadAsset(
      QStringLiteral("asset-switch-cancel-focused"),
      source_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(asset_switch_cancel_controller)) {
    return fail("asset switch cancellation proof should start from an idle preview");
  }
  const auto asset_switch_generation_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
          asset_switch_cancel_controller);
  photo_editor::desktop::EditorPreviewControllerTestAccess::seed_pending_asset_scoped_work(
      asset_switch_cancel_controller);
  if (!asset_switch_cancel_controller.processing()) {
    return fail("asset switch cancellation proof should seed pending render and smart selection work");
  }
  photo_editor::desktop::EditorPreviewControllerTestAccess::cancel_pending_asset_scoped_work(
      asset_switch_cancel_controller);
  if (asset_switch_cancel_controller.processing() ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
          asset_switch_cancel_controller) <= asset_switch_generation_before ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_pending(
          asset_switch_cancel_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_pending(
          asset_switch_cancel_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::overlay_render_pending(
          asset_switch_cancel_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          smart_select_preview_request_pending(asset_switch_cancel_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          smart_select_commit_request_pending(asset_switch_cancel_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          smart_select_commit_pending(asset_switch_cancel_controller)) {
    return fail("asset-scoped cancellation should invalidate stale renders and clear queued smart selection work before the next image loads");
  }

  photo_editor::desktop::EditorPreviewController scheduled_preview_controller;
  scheduled_preview_controller.loadAsset(
      QStringLiteral("asset-smart-scheduled-focused"),
      scheduled_source_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(scheduled_preview_controller)) {
    return fail("scheduled smart preview controller should finish its initial preview");
  }
  const auto scheduled_generation_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          smart_select_preview_generation(scheduled_preview_controller);
  const auto scheduled_duplicate_skips_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          smart_select_preview_duplicate_skip_count(scheduled_preview_controller);
  scheduled_preview_controller.scheduleSmartSelectPreview(0.18, 0.5);
  const auto scheduled_generation_after_first =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          smart_select_preview_generation(scheduled_preview_controller);
  if (scheduled_generation_after_first != scheduled_generation_before + 1 ||
      !photo_editor::desktop::EditorPreviewControllerTestAccess::
          smart_select_preview_request_pending(scheduled_preview_controller) ||
      !photo_editor::desktop::EditorPreviewControllerTestAccess::
          smart_select_preview_timer_active(scheduled_preview_controller)) {
    return fail("scheduled smart hover preview should debounce through a pending timer before publishing");
  }
  scheduled_preview_controller.scheduleSmartSelectPreview(0.18, 0.5);
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::
              smart_select_preview_generation(scheduled_preview_controller) !=
          scheduled_generation_after_first ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              smart_select_preview_duplicate_skip_count(scheduled_preview_controller) !=
          scheduled_duplicate_skips_before + 1 ||
      !photo_editor::desktop::EditorPreviewControllerTestAccess::
          smart_select_preview_request_pending(scheduled_preview_controller) ||
      !photo_editor::desktop::EditorPreviewControllerTestAccess::
          smart_select_preview_timer_active(scheduled_preview_controller)) {
    return fail("scheduled smart hover preview should skip duplicate pending hover requests");
  }
  scheduled_preview_controller.rejectSmartSelectPreview();
  if (scheduled_preview_controller.smartSelectPreviewActive() ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          smart_select_preview_request_pending(scheduled_preview_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          smart_select_preview_timer_active(scheduled_preview_controller) ||
      !scheduled_preview_controller.status().contains(QStringLiteral("canceled"))) {
    return fail("scheduled smart hover preview should be cancelable before publish");
  }

  photo_editor::desktop::EditorPreviewController prompt_parity_controller;
  prompt_parity_controller.loadAsset(
      QStringLiteral("asset-smart-prompt-parity-focused"),
      source_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(prompt_parity_controller)) {
    return fail("smart prompt parity controller should finish its initial preview");
  }
  prompt_parity_controller.updateSmartSelectPreview(0.18, 0.5);
  const auto hover_prompts =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          smart_select_preview_prompts(prompt_parity_controller);
  if (!prompt_parity_controller.smartSelectPreviewActive() ||
      hover_prompts.size() != 1) {
    return fail("smart hover preview should publish one normalized prompt for the target point");
  }
  const auto hover_prompt = hover_prompts.constFirst().toMap();
  if (hover_prompt.value(QStringLiteral("type")).toString() !=
          QStringLiteral("positive-point") ||
      hover_prompt.value(QStringLiteral("negative")).toBool() ||
      hover_prompt.value(QStringLiteral("x")).toDouble() != 0.18 ||
      hover_prompt.value(QStringLiteral("y")).toDouble() != 0.5) {
    return fail("smart hover preview should store the exact normalized prompt coordinate");
  }
  prompt_parity_controller.smartSelectObjectAt(0.18, 0.5, true);
  if (!wait_for_mask_count(prompt_parity_controller, 1)) {
    return fail("smart prompt parity commit should publish an object mask");
  }
  const auto smart_commit_intent =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_edit_intent(
          prompt_parity_controller);
  const auto committed_mask =
      prompt_parity_controller.maskStack().at(prompt_parity_controller.activeMaskIndex()).toMap();
  const auto committed_metadata =
      committed_mask.value(QStringLiteral("generatedMaskMetadata")).toMap();
  const auto committed_metadata_prompts =
      committed_metadata.value(QStringLiteral("smartSelectionPrompts")).toList();
  if (committed_mask.value(QStringLiteral("generatedAlphaWidth")).toInt() !=
          committed_mask.value(QStringLiteral("segmentationWidth")).toInt() ||
      committed_mask.value(QStringLiteral("generatedAlphaHeight")).toInt() !=
          committed_mask.value(QStringLiteral("segmentationHeight")).toInt() ||
      committed_mask.value(QStringLiteral("generatedAlphaSourceImageVersion")).toString().isEmpty() ||
      committed_mask.value(QStringLiteral("generatedAlphaModelName")).toString().isEmpty() ||
      committed_metadata.value(QStringLiteral("runtime")).toString().isEmpty() ||
      committed_mask.value(QStringLiteral("generatedAlphaPromptType")).toString() !=
          QStringLiteral("semantic:object") ||
      committed_metadata.value(QStringLiteral("promptType")).toString() !=
          QStringLiteral("semantic:object") ||
      committed_mask.value(QStringLiteral("generatedAlphaCoordinateSpaceContract")).toString() !=
          QStringLiteral("source-normalized-prompts-to-source-alpha-raster:v1") ||
      committed_mask.value(QStringLiteral("generatedAlphaSourceCoordinateSpace")).toString() !=
          QStringLiteral("source-image-pixels") ||
      committed_mask.value(QStringLiteral("generatedAlphaPromptCoordinateSpace")).toString() !=
          QStringLiteral("source-normalized-0-1") ||
      committed_mask.value(QStringLiteral("generatedAlphaRasterCoordinateSpace")).toString() !=
          QStringLiteral("source-alpha-raster") ||
      committed_metadata.value(QStringLiteral("coordinateSpaceContract")).toString() !=
          QStringLiteral("source-normalized-prompts-to-source-alpha-raster:v1") ||
      committed_metadata.value(QStringLiteral("sourceCoordinateSpace")).toString() !=
          QStringLiteral("source-image-pixels") ||
      committed_metadata.value(QStringLiteral("promptCoordinateSpace")).toString() !=
          QStringLiteral("source-normalized-0-1") ||
      committed_metadata.value(QStringLiteral("rasterCoordinateSpace")).toString() !=
          QStringLiteral("source-alpha-raster")) {
    return fail("committed smart object selection should store raster, source revision, runtime, prompt, and coordinate-space metadata");
  }
  if (smart_commit_intent.operation != QStringLiteral("smart_selection") ||
      smart_commit_intent.dirty_domain != QStringLiteral("mask_rasterization") ||
      smart_commit_intent.render_reason != QStringLiteral("mask_commit") ||
      !smart_commit_intent.schedule_full_resolution) {
    return fail("committed smart selections should schedule the high-quality mask render instead of a preview-only mask update");
  }
  if (committed_metadata_prompts != hover_prompts ||
      committed_mask.value(QStringLiteral("generatedAlphaPromptType")).toString() !=
          QStringLiteral("semantic:object")) {
    const auto detail =
        QStringLiteral(
            "smart hover and committed object selection should persist identical normalized prompt coordinates "
            "(hover=%1 generated=%2 promptType=%3)")
            .arg(QString::fromUtf8(json_for_variant(hover_prompts)))
            .arg(QString::fromUtf8(json_for_variant(committed_metadata_prompts)))
            .arg(committed_mask.value(QStringLiteral("generatedAlphaPromptType")).toString())
            .toUtf8();
    return fail(detail.constData());
  }

  qunsetenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION");
  return EXIT_SUCCESS;
}
