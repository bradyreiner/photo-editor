#include <cstdlib>
#include <iostream>

#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QHash>
#include <QImage>
#include <QString>
#include <QTemporaryDir>
#include <QThread>
#include <QVariantList>
#include <QVariantMap>

#include "editor_preview_controller.h"
#include "editor_preview_image_sink.h"

namespace photo_editor::desktop {

struct EditorPreviewControllerTestAccess {
  static QVariantMap last_mask_work_telemetry(const EditorPreviewController& controller) {
    return controller.last_mask_work_telemetry_;
  }

  static bool photo_layer_adjustment_render_timer_active(
      const EditorPreviewController& controller) {
    return controller.photo_layer_adjustment_render_timer_.isActive();
  }

  static int photo_layer_adjustment_render_pending_write_count(
      const EditorPreviewController& controller) {
    return controller.photo_layer_adjustment_render_pending_write_count_;
  }

  static int photo_layer_adjustment_render_last_coalesced_write_count(
      const EditorPreviewController& controller) {
    return controller.photo_layer_adjustment_render_last_coalesced_write_count_;
  }

  static qint64 photo_layer_adjustment_render_last_flush_elapsed_ms(
      const EditorPreviewController& controller) {
    return controller.photo_layer_adjustment_render_last_flush_elapsed_ms_;
  }

  static qint64 photo_layer_adjustment_render_budget_ms(
      const EditorPreviewController& controller) {
    return controller.photo_layer_adjustment_render_budget_ms_;
  }

  static bool photo_layer_adjustment_render_within_latency_budget(
      const EditorPreviewController& controller) {
    return controller.photo_layer_adjustment_render_within_latency_budget_;
  }

  static RenderRequest current_render_request(
      const EditorPreviewController& controller,
      bool full_resolution) {
    return controller.currentRenderRequest(full_resolution);
  }

  static int render_generation(const EditorPreviewController& controller) {
    return controller.render_generation_;
  }

  static void handle_render_result(
      EditorPreviewController& controller,
      bool full_resolution,
      const RenderResult& result) {
    controller.handleRenderResult(full_resolution, result);
  }
};

}  // namespace photo_editor::desktop

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

class TestImageSink final : public photo_editor::desktop::EditorPreviewImageSink {
 public:
  void setImage(const QString& key, const QImage& image) override { images_.insert(key, image); }

  void clearImage(const QString& key) override { images_.remove(key); }

  [[nodiscard]] QImage image(const QString& key) const { return images_.value(key); }

 private:
  QHash<QString, QImage> images_;
};

bool outline_points_are_image_normalized(const QVariantList& paths) {
  if (paths.isEmpty()) {
    return false;
  }
  const auto point_is_normalized = [](const QVariant& point_value) {
    const auto point = point_value.toMap();
    const auto x = point.value(QStringLiteral("x")).toDouble();
    const auto y = point.value(QStringLiteral("y")).toDouble();
    return x >= 0.0 && x <= 1.0 && y >= 0.0 && y <= 1.0;
  };
  if (paths.first().toMap().contains(QStringLiteral("x")) &&
      paths.first().toMap().contains(QStringLiteral("y"))) {
    if (paths.size() < 2) {
      return false;
    }
    for (const auto& point_value : paths) {
      if (!point_is_normalized(point_value)) {
        return false;
      }
    }
    return true;
  }
  for (const auto& path_value : paths) {
    const auto path = path_value.toList();
    if (path.size() < 2) {
      return false;
    }
    for (const auto& point_value : path) {
      if (!point_is_normalized(point_value)) {
        return false;
      }
    }
  }
  return true;
}

QString read_source_file(const QString& relative_path) {
  QFile file(QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR)).filePath(relative_path));
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return {};
  }
  return QString::fromUtf8(file.readAll());
}

bool wait_for_brush_commit(photo_editor::desktop::EditorPreviewController& controller) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < 3000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    const auto stack = controller.maskStack();
    const auto active_index = controller.activeMaskIndex();
    if (!controller.brushStrokeActive() &&
        controller.activeMaskLiveBrushPoints().isEmpty() &&
        active_index >= 0 &&
        active_index < stack.size()) {
      const auto mask = stack.at(active_index).toMap();
      if (!mask.value(QStringLiteral("brushPoints")).toList().isEmpty() &&
          !mask.value(QStringLiteral("segmentationData")).toByteArray().isEmpty() &&
          mask.value(QStringLiteral("segmentationWidth")).toInt() > 0 &&
          mask.value(QStringLiteral("segmentationHeight")).toInt() > 0) {
        return true;
      }
    }
    QThread::msleep(5);
  }
  return false;
}

bool wait_for_smart_commit(
    photo_editor::desktop::EditorPreviewController& controller,
    int expected_mask_count,
    int timeout_ms = 4000) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    if (!controller.processing() && controller.maskCount() >= expected_mask_count) {
      return true;
    }
    QThread::msleep(25);
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  return !controller.processing() && controller.maskCount() >= expected_mask_count;
}

bool wait_for_layer_adjustment_flush(
    photo_editor::desktop::EditorPreviewController& controller,
    int expected_coalesced_writes,
    int timeout_ms = 1000) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    if (!photo_editor::desktop::EditorPreviewControllerTestAccess::
            photo_layer_adjustment_render_timer_active(controller) &&
        photo_editor::desktop::EditorPreviewControllerTestAccess::
                photo_layer_adjustment_render_pending_write_count(controller) == 0 &&
        photo_editor::desktop::EditorPreviewControllerTestAccess::
                photo_layer_adjustment_render_last_coalesced_write_count(controller) ==
            expected_coalesced_writes) {
      return true;
    }
    QThread::msleep(10);
  }
  return false;
}

bool wait_for_preview_idle(
    photo_editor::desktop::EditorPreviewController& controller,
    int timeout_ms = 3000) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    if (!controller.processing()) {
      return true;
    }
    QThread::msleep(10);
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  return !controller.processing();
}

QVariantMap normalized_point(double x, double y) {
  QVariantMap point;
  point.insert(QStringLiteral("x"), x);
  point.insert(QStringLiteral("y"), y);
  return point;
}

bool has_final_alpha_outline_contract(
    const QVariantMap& trace,
    const QString& expected_provider) {
  const auto source = trace.value(QStringLiteral("outline_trace_source")).toString();
  return (source == QStringLiteral("vector-path") ||
          source == QStringLiteral("clean-canvas") ||
          source == QStringLiteral("composite-alpha") ||
          source == QStringLiteral("cache-hit")) &&
         trace.value(QStringLiteral("mask_alpha_provider_contract")).toString() ==
             QStringLiteral("shared-mask-alpha-provider-v1") &&
         trace.value(QStringLiteral("mask_alpha_provider")).toString() == expected_provider &&
         trace.value(QStringLiteral("outline_coordinate_space")).toString() ==
             QStringLiteral("image-normalized") &&
         trace.value(QStringLiteral("outline_coordinate_contract")).toString() ==
             QStringLiteral("image-normalized|crop-aware|points-clamped-0-1:v1") &&
         trace.value(QStringLiteral("outline_alpha_source_contract")).toString() ==
             QStringLiteral("same-final-alpha-as-layer-mask:v1") &&
         trace.value(QStringLiteral("outline_alpha_source_provider")).toString() ==
             expected_provider &&
         trace.value(QStringLiteral("mask_alpha_trace_hash"))
             .toString()
             .startsWith(QStringLiteral("sha1:")) &&
         !trace.value(QStringLiteral("mask_alpha_trace_signature")).toString().isEmpty() &&
         trace.value(QStringLiteral("outline_trace_path_count")).toInt() > 0;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  QTemporaryDir runtime_root;
  if (!runtime_root.isValid()) {
    return fail("outline trace runtime directory should be available");
  }

  QImage source(120, 80, QImage::Format_ARGB32);
  for (int y = 0; y < source.height(); ++y) {
    for (int x = 0; x < source.width(); ++x) {
      source.setPixelColor(
          x,
          y,
          QColor(28 + (x % 90), 42 + (y % 70), 86 + ((x + y) % 80), 255));
    }
  }
  const auto source_path = runtime_root.filePath(QStringLiteral("outline-source.png"));
  if (!source.save(source_path)) {
    return fail("outline trace source image should be writable");
  }

  photo_editor::desktop::EditorPreviewController controller;
  controller.setMaskPreviewMode(QStringLiteral("color overlay"));
  if (controller.maskPreviewMode() != QStringLiteral("overlay") ||
      controller.maskPreviewDisplayMode() != QStringLiteral("overlay")) {
    return fail("color overlay should be a normal solid overlay display mode");
  }
  controller.setMaskPreviewMode(QStringLiteral("marching ants"));
  if (controller.maskPreviewMode() != QStringLiteral("marching-ants") ||
      controller.maskPreviewDisplayMode() != QStringLiteral("marching-ants")) {
    return fail("marching ants should be a normal user-facing display mode");
  }
  controller.setMaskPreviewMode(QStringLiteral("hidden"));
  if (controller.maskPreviewMode() != QStringLiteral("none") ||
      controller.maskPreviewDisplayMode() != QStringLiteral("hidden")) {
    return fail("hidden should be the normal display name for disabled mask previews");
  }
  controller.setMaskPreviewMode(QStringLiteral("edge-view"));
  if (controller.maskPreviewMode() != QStringLiteral("overlay") ||
      controller.maskPreviewDisplayMode() != QStringLiteral("overlay")) {
    return fail("diagnostic edge aliases should normalize away from normal display cycling");
  }

  const auto mask_inspector_source =
      read_source_file(QStringLiteral("apps/desktop/qml/workspaces/MaskInspectorSections.qml"));
  const auto edit_view_source =
      read_source_file(QStringLiteral("apps/desktop/qml/workspaces/EditView.qml"));
  const auto studio_workspace_source =
      read_source_file(QStringLiteral("apps/desktop/qml/workspaces/StudioWorkspace.qml"));
  const auto shared_overlay_source =
      read_source_file(QStringLiteral("apps/desktop/qml/workspaces/SharedMaskOverlay.qml"));
  const auto canvas_overlay_source =
      read_source_file(QStringLiteral("apps/desktop/qml/workspaces/MaskCanvasOverlay.qml"));
  const auto controller_source =
      read_source_file(QStringLiteral("apps/desktop/src/editor_preview_controller.cpp"));
  const auto controller_header_source =
      read_source_file(QStringLiteral("apps/desktop/src/editor_preview_controller.h"));
  const auto normal_flow_index = mask_inspector_source.indexOf(QStringLiteral("normalMaskPreviewModeFlow"));
  const auto diagnostics_toggle_index =
      mask_inspector_source.indexOf(QStringLiteral("maskPreviewDiagnosticsToggleButton"));
  const auto diagnostic_flow_index =
      mask_inspector_source.indexOf(QStringLiteral("diagnosticMaskPreviewModeFlow"));
  const auto color_overlay_index =
      mask_inspector_source.indexOf(QStringLiteral("maskPreviewOverlayButton"));
  const auto marching_ants_index =
      mask_inspector_source.indexOf(QStringLiteral("maskPreviewMarchingAntsButton"));
  const auto color_on_bw_index =
      mask_inspector_source.indexOf(QStringLiteral("maskPreviewColorOnBwButton"));
  if (mask_inspector_source.isEmpty() ||
      edit_view_source.isEmpty() ||
      studio_workspace_source.isEmpty() ||
      shared_overlay_source.isEmpty() ||
      canvas_overlay_source.isEmpty() ||
      controller_source.isEmpty() ||
      controller_header_source.isEmpty() ||
      normal_flow_index < 0 ||
      diagnostics_toggle_index < 0 ||
      diagnostic_flow_index < 0 ||
      color_overlay_index < normal_flow_index ||
      color_overlay_index > diagnostics_toggle_index ||
      marching_ants_index < normal_flow_index ||
      marching_ants_index > diagnostics_toggle_index ||
      color_on_bw_index < diagnostic_flow_index ||
      !mask_inspector_source.contains(QStringLiteral("text: \"Color Overlay\"")) ||
      !mask_inspector_source.contains(QStringLiteral("text: \"Marching Ants\"")) ||
      !mask_inspector_source.contains(QStringLiteral("maskPreviewOverlayOpacitySlider")) ||
      !mask_inspector_source.contains(QStringLiteral("maskPreviewOverlayColorSwatch")) ||
      !mask_inspector_source.contains(QStringLiteral("maskPreviewOverlayColorButton")) ||
      !mask_inspector_source.contains(QStringLiteral("maskPreviewOverlayColorResetButton")) ||
      !mask_inspector_source.contains(QStringLiteral("maskPreviewShowActiveOnlyButton")) ||
      !mask_inspector_source.contains(QStringLiteral("editorPreview.maskPreviewShowAll = false")) ||
      !mask_inspector_source.contains(QStringLiteral("maskPreviewShowAllButton")) ||
      !mask_inspector_source.contains(QStringLiteral("editorPreview.maskPreviewShowAll = !editorPreview.maskPreviewShowAll")) ||
      !mask_inspector_source.contains(QStringLiteral("maskPreviewShowInvertedButton")) ||
      !mask_inspector_source.contains(QStringLiteral("editorPreview.activeMaskSelectionInverted = !editorPreview.activeMaskSelectionInverted")) ||
      !mask_inspector_source.contains(QStringLiteral("text: root.diagnosticMaskPreviewModesVisible ? \"Hide Matte Diagnostics\" : \"Matte Diagnostics\"")) ||
      !mask_inspector_source.contains(QStringLiteral("visible: root.diagnosticMaskPreviewModesVisible")) ||
      edit_view_source.contains(QStringLiteral("editorPreview.maskPreviewMode = \"color-on-bw\"")) ||
      edit_view_source.contains(QStringLiteral("editorPreview.maskPreviewMode = \"image-on-bw\"")) ||
      !edit_view_source.contains(QStringLiteral("editorPreview.maskPreviewMode = \"marching-ants\"")) ||
      !studio_workspace_source.contains(QStringLiteral("if (editorPreview.maskPreviewDisplayMode !== \"marching-ants\")")) ||
      !shared_overlay_source.contains(QStringLiteral("liveOverlayPerformanceContract")) ||
      !shared_overlay_source.contains(QStringLiteral("no-preview-render-block:v1")) ||
      !shared_overlay_source.contains(QStringLiteral("activeBrushPainting && liveBrushPoints.length > 0")) ||
      !shared_overlay_source.contains(QStringLiteral("activeBrushPainting")) ||
      !shared_overlay_source.contains(QStringLiteral("? liveBrushPreviewSource")) ||
      !canvas_overlay_source.contains(QStringLiteral("liveOverlayPerformanceContract")) ||
      !canvas_overlay_source.contains(QStringLiteral("no-preview-render-block:v1")) ||
      !canvas_overlay_source.contains(QStringLiteral("active && (editorPreview.brushStrokeActive || liveBrushPoints.length > 0)")) ||
      !shared_overlay_source.contains(QStringLiteral("editorPreview.maskPreviewMode === \"marching-ants\"")) ||
      !canvas_overlay_source.contains(QStringLiteral("editorPreview.maskPreviewMode !== \"marching-ants\"")) ||
      !controller_source.contains(QStringLiteral("QtConcurrent::run([request]() { return compute_brush_raster_commit_result(request); })")) ||
      !controller_source.contains(QStringLiteral("{QStringLiteral(\"commit_deferred\"), true}")) ||
      !controller_source.contains(QStringLiteral("{QStringLiteral(\"async_worker\"), true}")) ||
      !controller_source.contains(QStringLiteral("{QStringLiteral(\"raster_worker_thread\"), result.worker_thread}")) ||
      !controller_source.contains(QStringLiteral("mask_thumbnail_cache_dirty_")) ||
      !controller_source.contains(QStringLiteral("mask_thumbnail_refresh_deferred")) ||
      !controller_source.contains(QStringLiteral("emit controlsChanged();")) ||
      !controller_source.contains(QStringLiteral("runQueuedThumbnailRefresh")) ||
      !controller_source.contains(QStringLiteral("kFrameCoalescedPreviewRenderBudgetMs = 16")) ||
      !controller_source.contains(QStringLiteral("kLayerAdjustmentRenderDebounceMs = kFrameCoalescedPreviewRenderBudgetMs")) ||
      !controller_source.contains(QStringLiteral("kLayerAdjustmentSliderDragLatencyBudgetMs")) ||
      !controller_source.contains(QStringLiteral("photo_layer_adjustment_render_last_coalesced_write_count_")) ||
      !controller_source.contains(QStringLiteral("preview.render.coalesced")) ||
      !controller_source.contains(QStringLiteral("coalesced_request_count")) ||
      !controller_source.contains(QStringLiteral("can_merge_commit")) ||
      !controller_source.contains(QStringLiteral("coalesced_stroke_count")) ||
      !controller_source.contains(QStringLiteral("photo_layer_adjustment_render_invalidated_active_preview_")) ||
      !controller_source.contains(QStringLiteral("photo_layer_adjustment_superseded_preview_count_")) ||
      !controller_source.contains(QStringLiteral("preview.render.superseded_by_layer_adjustment")) ||
      !controller_source.contains(QStringLiteral("latest_render_generation_->store(render_generation_")) ||
      !controller_source.contains(QStringLiteral("observability_active_target")) ||
      !controller_source.contains(QStringLiteral("observability_active_layer_id")) ||
      !controller_source.contains(QStringLiteral("observability_active_mask_id")) ||
      !controller_source.contains(QStringLiteral("observability_active_mask_set_id")) ||
      !controller_source.contains(QStringLiteral("cancelled_render_count")) ||
      !controller_source.contains(QStringLiteral("stale_preview_fallback")) ||
      !controller_source.contains(QStringLiteral("render_failure_status_for_stage")) ||
      !controller_source.contains(QStringLiteral("Preview failed: %1.")) ||
      !controller_source.contains(QStringLiteral("thumbnail_id")) ||
      !controller_header_source.contains(QStringLiteral("QString active_target")) ||
      !controller_header_source.contains(QStringLiteral("QString active_layer_id")) ||
      !controller_header_source.contains(QStringLiteral("QString active_mask_id")) ||
      !controller_header_source.contains(QStringLiteral("QString active_mask_set_id")) ||
      !controller_header_source.contains(QStringLiteral("int cancelled_render_count")) ||
      !controller_header_source.contains(QStringLiteral("QString stale_preview_fallback")) ||
      !controller_source.contains(QStringLiteral("{QStringLiteral(\"frame_budget_ms\"), kFrameCoalescedPreviewRenderBudgetMs}")) ||
      !controller_source.contains(QStringLiteral("preview_viewport_patch_max_edge_ > 0")) ||
      !controller_source.contains(QStringLiteral("? preview_viewport_patch_max_edge_")) ||
      !controller_source.contains(QStringLiteral("kMinimumInteractivePreviewEdge")) ||
      controller_source.contains(QStringLiteral("std::max(\n          interactive_preview_edge_for_state(currentState(), previewTargetEdge()),\n          kExpensiveInteractivePreviewEdge)")) ||
      !controller_source.contains(QStringLiteral("updateActiveBrushLivePreview(sampled_points);\n    emit activeBrushStrokeChanged();\n    return;")) ||
      !controller_source.contains(QStringLiteral("schedulePendingBrushCommit(std::move(commit));")) ||
      !controller_source.contains(QStringLiteral("normalized-image-region|dependent-stage-propagation:v1")) ||
      !controller_source.contains(QStringLiteral("dirty_region_valid")) ||
      !controller_source.contains(QStringLiteral("dirty_region_area")) ||
      !controller_source.contains(QStringLiteral("QStringLiteral(\"mask_rasterization\"),\n      QStringLiteral(\"mask_composition\"),\n      QStringLiteral(\"local_adjustments\")")) ||
      !controller_source.contains(QStringLiteral("QStringLiteral(\"layers_composite\")")) ||
      !controller_source.contains(QStringLiteral("QStringLiteral(\"overlay_composite\")")) ||
      !controller_source.contains(QStringLiteral("QStringLiteral(\"encode_disk\")")) ||
      !controller_source.contains(QStringLiteral("QStringLiteral(\"histogram_scope\")")) ||
      !controller_source.contains(QStringLiteral("dirty_region_for_brush_points(sampled_points)")) ||
      !controller_source.contains(QStringLiteral("submitMaskRasterizationEditIntent(\n        QStringLiteral(\"brush_stroke\")")) ||
      !controller_header_source.contains(QStringLiteral("Q_PROPERTY(bool scopesVisible READ scopesVisible WRITE setScopesVisible")) ||
      !controller_source.contains(QStringLiteral("mask-scope-visible-and-settled:v1")) ||
      !controller_source.contains(QStringLiteral("preview.histogram.skipped")) ||
      !controller_source.contains(QStringLiteral("mask_scope_dirty")) ||
      !controller_source.contains(QStringLiteral("settled_for_histogram")) ||
      !controller_source.contains(QStringLiteral("(!mask_scope_dirty || result.full_resolution || scopes_visible_)")) ||
      !studio_workspace_source.contains(QStringLiteral("property: \"scopesVisible\"")) ||
      !studio_workspace_source.contains(QStringLiteral("root.editInspectorTab === \"scopes\"")) ||
      !studio_workspace_source.contains(QStringLiteral("root.editInspectorPanelVisible"))) {
    return fail("mask preview UI should expose solid overlay and marching ants normally while gating diagnostic matte modes");
  }

  photo_editor::desktop::EditorPreviewController failure_controller;
  TestImageSink failure_sink;
  failure_controller.setImageProvider(&failure_sink);
  failure_controller.loadAsset(
      QStringLiteral("stage-specific-failure-contract-asset"),
      source_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  failure_controller.setMaskPreviewMode(QStringLiteral("overlay"));
  if (!wait_for_preview_idle(failure_controller) ||
      failure_sink.image(QStringLiteral("edited")).isNull()) {
    return fail("stage-specific failure fixture should publish a last-good preview before injection");
  }
  failure_controller.startSelectionBrush(false);
  if (!wait_for_preview_idle(failure_controller, 4000) ||
      failure_controller.activeMaskKind() != QStringLiteral("brush") ||
      failure_controller.activeMaskToolRole() != QStringLiteral("selection") ||
      failure_controller.maskPreviewMode() != QStringLiteral("overlay")) {
    return fail("stage-specific failure fixture should keep an active brush mask before injection");
  }
  const auto failure_last_good_source = failure_controller.displayPreviewSource();
  const auto failure_active_mask_index = failure_controller.activeMaskIndex();
  auto mask_alpha_failure = photo_editor::desktop::RenderResult{};
  mask_alpha_failure.file_path = source_path;
  mask_alpha_failure.generation =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
          failure_controller);
  mask_alpha_failure.dirty_domain = QStringLiteral("mask_rasterization");
  mask_alpha_failure.dirty_stages = {QStringLiteral("mask_rasterization")};
  mask_alpha_failure.render_mode = QStringLiteral("live_preview");
  mask_alpha_failure.mask_alpha_trace_fields.insert(
      QStringLiteral("mask_alpha_trace_hash"),
      QStringLiteral("sha1:focused-mask-alpha-failure"));
  photo_editor::desktop::EditorPreviewControllerTestAccess::handle_render_result(
      failure_controller,
      false,
      mask_alpha_failure);
  if (failure_controller.status() != QStringLiteral("Preview failed: Mask alpha.") ||
      failure_controller.previewFailureStage() != QStringLiteral("mask_alpha") ||
      !failure_controller.previewFailureDetail().contains(
          QStringLiteral("sha1:focused-mask-alpha-failure")) ||
      failure_controller.displayPreviewSource() != failure_last_good_source ||
      failure_controller.activeMaskKind() != QStringLiteral("brush") ||
      failure_controller.activeMaskToolRole() != QStringLiteral("selection") ||
      failure_controller.activeMaskIndex() != failure_active_mask_index ||
      failure_controller.maskPreviewMode() != QStringLiteral("overlay")) {
    return fail("stage-specific mask-alpha failure should keep last good canvas and active brush tool context");
  }
  if (!failure_controller.retryPreviewRender() ||
      !wait_for_preview_idle(failure_controller, 4000) ||
      failure_controller.activeMaskKind() != QStringLiteral("brush") ||
      failure_controller.activeMaskToolRole() != QStringLiteral("selection") ||
      failure_controller.activeMaskIndex() != failure_active_mask_index ||
      failure_controller.maskPreviewMode() != QStringLiteral("overlay")) {
    return fail("preview retry should keep the active brush mask tool context");
  }
  struct StageFailureCase {
    QString failure_stage;
    QString dirty_domain;
    QStringList dirty_stages;
    QString expected_stage;
    QString expected_status;
    QString detail_token;
  };
  const StageFailureCase stage_failure_cases[] = {
      {
          QStringLiteral("outline_trace"),
          QStringLiteral("overlay_composite"),
          {QStringLiteral("outline_trace")},
          QStringLiteral("outline_trace"),
          QStringLiteral("Preview failed: Outline trace."),
          QStringLiteral("sha1:focused-outline-trace-failure"),
      },
      {
          QString(),
          QStringLiteral("export_preview"),
          {QStringLiteral("export_preview")},
          QStringLiteral("export_preview"),
          QStringLiteral("Preview failed: Export preview."),
          QStringLiteral("sha1:focused-export-preview-failure"),
      },
      {
          QString(),
          QStringLiteral("inpaint_patch"),
          {QStringLiteral("retouch_patch_application")},
          QStringLiteral("retouch_patch"),
          QStringLiteral("Preview failed: Retouch patch."),
          QStringLiteral("sha1:focused-inpaint-patch-failure"),
      },
      {
          QString(),
          QString(),
          {},
          QStringLiteral("render_pipeline"),
          QStringLiteral("Preview failed: Render pipeline."),
          QStringLiteral("no-stage-fallback"),
      },
  };
  for (const auto& failure_case : stage_failure_cases) {
    const auto case_last_good_source = failure_controller.displayPreviewSource();
    auto stage_failure = photo_editor::desktop::RenderResult{};
    stage_failure.file_path = source_path;
    stage_failure.generation =
        photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
            failure_controller);
    stage_failure.failure_stage = failure_case.failure_stage;
    stage_failure.failure_detail = failure_case.detail_token;
    stage_failure.dirty_domain = failure_case.dirty_domain;
    stage_failure.dirty_stages = failure_case.dirty_stages;
    stage_failure.render_mode = QStringLiteral("settled_preview");
    stage_failure.interaction_mode = QStringLiteral("interactive");
    photo_editor::desktop::EditorPreviewControllerTestAccess::handle_render_result(
        failure_controller,
        false,
        stage_failure);
    if (failure_controller.status() != failure_case.expected_status ||
        failure_controller.previewFailureStage() != failure_case.expected_stage ||
        !failure_controller.previewFailureDetail().contains(failure_case.detail_token) ||
        failure_controller.displayPreviewSource() != case_last_good_source ||
        failure_controller.activeMaskKind() != QStringLiteral("brush") ||
        failure_controller.activeMaskToolRole() != QStringLiteral("selection") ||
        failure_controller.activeMaskIndex() != failure_active_mask_index ||
        failure_controller.maskPreviewMode() != QStringLiteral("overlay")) {
      std::cerr << "Stage failure mismatch: expectedStage="
                << failure_case.expected_stage.toStdString()
                << " status=" << failure_controller.status().toStdString()
                << " stage=" << failure_controller.previewFailureStage().toStdString()
                << " detail=" << failure_controller.previewFailureDetail().toStdString()
                << "\n";
      return fail("stage-specific preview failure should keep last good canvas and active brush context");
    }
    auto recovery = photo_editor::desktop::RenderResult{};
    recovery.file_path = source_path;
    recovery.generation =
        photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
            failure_controller);
    recovery.success = true;
    recovery.status = QStringLiteral("Preview ready.");
    recovery.render_reason = QStringLiteral("focused_stage_failure_recovery");
    recovery.render_mode = QStringLiteral("live_preview");
    recovery.interaction_mode = QStringLiteral("draft_interactive");
    recovery.display_preview_source = case_last_good_source;
    recovery.preview_image = QImage(12, 8, QImage::Format_RGB32);
    recovery.preview_image.fill(QColor(18, 30, 36));
    recovery.source_width = recovery.preview_image.width();
    recovery.source_height = recovery.preview_image.height();
    recovery.output_width = recovery.preview_image.width();
    recovery.output_height = recovery.preview_image.height();
    photo_editor::desktop::EditorPreviewControllerTestAccess::handle_render_result(
        failure_controller,
        false,
        recovery);
    if (!failure_controller.previewFailureStage().isEmpty() ||
        !failure_controller.previewFailureDetail().isEmpty() ||
        failure_controller.status() != QStringLiteral("Preview ready.") ||
        failure_controller.activeMaskKind() != QStringLiteral("brush") ||
        failure_controller.activeMaskToolRole() != QStringLiteral("selection") ||
        failure_controller.activeMaskIndex() != failure_active_mask_index ||
        failure_controller.maskPreviewMode() != QStringLiteral("overlay")) {
      return fail("successful stage failure recovery should clear failure state without losing the active brush context");
    }
  }

  photo_editor::desktop::EditorPreviewController live_controller;
  live_controller.loadAsset(
      QStringLiteral("live-brush-preview-contract-asset"),
      source_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  live_controller.setMaskPreviewMode(QStringLiteral("overlay"));
  live_controller.startSelectionBrush(false);
  live_controller.beginActiveBrushStroke();
  live_controller.addBrushPoint(0.32, 0.46);
  if (!live_controller.brushStrokeActive() ||
      live_controller.activeMaskLiveBrushPoints().isEmpty() ||
      !live_controller.activeMaskPreviewSource().isEmpty() ||
      live_controller.activeMaskLiveBrushPreviewElapsedMs() < 0 ||
      live_controller.activeMaskLiveBrushPreviewElapsedMs() > 33 ||
      !live_controller.activeMaskLiveBrushPreviewWithinLatencyBudget() ||
      live_controller.activeMaskLiveBrushPreviewBackend().isEmpty() ||
      live_controller.activeMaskLiveBrushPreviewBackend() == QStringLiteral("none")) {
    return fail("live brush drag should show active live points immediately without waiting for a raster preview source");
  }
  live_controller.setScopesVisible(false);
  if (live_controller.scopesVisible()) {
    return fail("scope visibility gate should be controllable by the workspace");
  }
  live_controller.setScopesVisible(true);
  if (!live_controller.scopesVisible()) {
    return fail("scope visibility gate should restore visible scope updates");
  }
  live_controller.endActiveBrushStroke();

  photo_editor::desktop::EditorPreviewController commit_controller;
  commit_controller.loadAsset(
      QStringLiteral("deferred-brush-commit-contract-asset"),
      source_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  commit_controller.setMaskPreviewMode(QStringLiteral("overlay"));
  commit_controller.startSelectionBrush(false);
  commit_controller.beginActiveBrushStroke();
  commit_controller.addBrushPoint(0.28, 0.36);
  commit_controller.addBrushPoint(0.68, 0.62);
  commit_controller.endActiveBrushStroke();
  if (!wait_for_brush_commit(commit_controller)) {
    return fail("deferred brush stroke commit should settle into stored brush alpha");
  }
  const auto brush_commit_telemetry =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_mask_work_telemetry(
          commit_controller);
  if (brush_commit_telemetry.value(QStringLiteral("operation")).toString() !=
          QStringLiteral("brush_rasterization") ||
      brush_commit_telemetry.value(QStringLiteral("cursor_budget_ms")).toLongLong() != 16 ||
      brush_commit_telemetry.value(QStringLiteral("live_stroke_budget_ms")).toLongLong() != 33 ||
      brush_commit_telemetry.value(QStringLiteral("preview_matte_budget_ms")).toLongLong() != 100 ||
      brush_commit_telemetry.value(QStringLiteral("cursor_elapsed_ms")).toLongLong() < 0 ||
      brush_commit_telemetry.value(QStringLiteral("live_stroke_elapsed_ms")).toLongLong() < 0 ||
      brush_commit_telemetry.value(QStringLiteral("preview_matte_elapsed_ms")).toLongLong() < 0 ||
      !brush_commit_telemetry.value(QStringLiteral("cursor_within_latency_budget")).toBool() ||
      !brush_commit_telemetry.value(QStringLiteral("live_stroke_within_latency_budget")).toBool() ||
      !brush_commit_telemetry.value(QStringLiteral("preview_matte_within_latency_budget")).toBool()) {
    return fail("brush commit telemetry should meet cursor, live-stroke, and preview-matte latency gates for small strokes");
  }
  const auto thumbnail_after_commit = commit_controller.maskSets().isEmpty()
      ? QVariantMap()
      : commit_controller.maskSets().constFirst().toMap();
  const auto thumbnail_sources_after_commit =
      thumbnail_after_commit.value(QStringLiteral("sources")).toList();
  const auto thumbnail_source_after_commit = thumbnail_sources_after_commit.isEmpty()
      ? QVariantMap()
      : thumbnail_sources_after_commit.constFirst().toMap();
  if (thumbnail_after_commit.value(QStringLiteral("thumbnailSource")).toString().isEmpty() ||
      thumbnail_after_commit.value(QStringLiteral("async_job_kind")).toString() !=
          QStringLiteral("mask_thumbnail") ||
      thumbnail_after_commit.value(QStringLiteral("async_job_stage")).toString() !=
          QStringLiteral("thumbnail") ||
      !thumbnail_after_commit.value(QStringLiteral("mask_alpha_trace_hash"))
           .toString()
           .startsWith(QStringLiteral("sha1:")) ||
      thumbnail_after_commit.value(QStringLiteral("mask_alpha_provider")).toString() !=
          QStringLiteral("composite") ||
      thumbnail_source_after_commit.value(QStringLiteral("mask_alpha_provider")).toString() !=
          QStringLiteral("brush-stroke-replay") ||
      !thumbnail_source_after_commit.value(QStringLiteral("mask_alpha_trace_hash"))
           .toString()
           .startsWith(QStringLiteral("sha1:"))) {
    return fail("deferred brush stroke commit should refresh mask thumbnail metadata after alpha settles");
  }
  const auto stroke_count_after_first_commit =
      commit_controller
          .maskStack()
          .at(commit_controller.activeMaskIndex())
          .toMap()
          .value(QStringLiteral("brushStrokes"))
          .toList()
          .size();
  commit_controller.beginActiveBrushStroke();
  commit_controller.addBrushPoint(0.18, 0.18);
  commit_controller.endActiveBrushStroke();
  commit_controller.beginActiveBrushStroke();
  commit_controller.addBrushPoint(0.82, 0.22);
  commit_controller.endActiveBrushStroke();
  if (!wait_for_brush_commit(commit_controller)) {
    return fail("rapid queued brush strokes should settle through the coalesced async commit path");
  }
  const auto stroke_count_after_coalesced_commit =
      commit_controller
          .maskStack()
          .at(commit_controller.activeMaskIndex())
          .toMap()
          .value(QStringLiteral("brushStrokes"))
          .toList()
          .size();
  if (stroke_count_after_coalesced_commit != stroke_count_after_first_commit + 2) {
    return fail("coalesced brush commits should preserve separate stroke history entries");
  }

  photo_editor::desktop::EditorPreviewController layer_coalesce_controller;
  layer_coalesce_controller.loadAsset(
      QStringLiteral("layer-frame-coalesce-contract-asset"),
      source_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  layer_coalesce_controller.addMask(QStringLiteral("linear"));
  if (layer_coalesce_controller.photoLayerCount() != 1 ||
      !layer_coalesce_controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("exposure"), 0.40) ||
      !layer_coalesce_controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("tint"), -12.0) ||
      !layer_coalesce_controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("brightness"), 18.0)) {
    return fail("rapid layer-local adjustment writes should update the active adjustment layer");
  }
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::
          photo_layer_adjustment_render_timer_active(layer_coalesce_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              photo_layer_adjustment_render_pending_write_count(layer_coalesce_controller) != 3 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              photo_layer_adjustment_render_budget_ms(layer_coalesce_controller) != 16) {
    return fail("rapid layer-local adjustment writes should enter a 16 ms coalesced slider latency gate");
  }
  const auto coalesced_layer = layer_coalesce_controller.photoLayers().constFirst().toMap();
  if (coalesced_layer.value(QStringLiteral("exposure")).toDouble() != 0.40 ||
      coalesced_layer.value(QStringLiteral("tint")).toDouble() != -12.0 ||
      coalesced_layer.value(QStringLiteral("brightness")).toDouble() != 18.0) {
    return fail("rapid layer-local adjustment writes should keep the latest layer values while render submission is coalesced");
  }
  if (!wait_for_layer_adjustment_flush(layer_coalesce_controller, 3) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              photo_layer_adjustment_render_last_flush_elapsed_ms(layer_coalesce_controller) < 0 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              photo_layer_adjustment_render_last_flush_elapsed_ms(layer_coalesce_controller) >
          photo_editor::desktop::EditorPreviewControllerTestAccess::
              photo_layer_adjustment_render_budget_ms(layer_coalesce_controller) ||
      !photo_editor::desktop::EditorPreviewControllerTestAccess::
          photo_layer_adjustment_render_within_latency_budget(layer_coalesce_controller)) {
    return fail("layer-local slider coalescing should flush within the 16 ms latency budget");
  }
  const auto layer_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::current_render_request(
          layer_coalesce_controller,
          false);
  const auto active_layer = layer_coalesce_controller.photoLayers().constFirst().toMap();
  const auto active_mask =
      layer_coalesce_controller
          .maskStack()
          .at(layer_coalesce_controller.activeMaskIndex())
          .toMap();
  if (layer_request.active_target != QStringLiteral("selection_adjustment_layer") ||
      layer_request.active_layer_id != active_layer.value(QStringLiteral("id")).toString() ||
      layer_request.active_mask_id != active_mask.value(QStringLiteral("id")).toString() ||
      layer_request.active_mask_set_id !=
          active_mask.value(QStringLiteral("maskSetId")).toString() ||
      layer_request.cancelled_render_count < 0 ||
      !layer_request.stale_preview_fallback.isEmpty()) {
    std::cerr
        << QStringLiteral(
               "active request fields: target=%1 layer=%2/%3 mask=%4/%5 set=%6/%7 cancelled=%8 stale=%9 dirty=%10\n")
               .arg(layer_request.active_target,
                    layer_request.active_layer_id,
                    active_layer.value(QStringLiteral("id")).toString(),
                    layer_request.active_mask_id,
                    active_mask.value(QStringLiteral("id")).toString(),
                    layer_request.active_mask_set_id,
                    active_mask.value(QStringLiteral("maskSetId")).toString(),
                    QString::number(layer_request.cancelled_render_count),
                    layer_request.stale_preview_fallback,
                    layer_request.dirty_domain)
               .toStdString();
    return fail("render requests should carry active target, layer id, mask id, mask set id, cancel count, and stale fallback observability fields");
  }

  controller.loadAsset(
      QStringLiteral("outline-trace-contract-asset"),
      source_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  controller.setMaskPreviewMode(QStringLiteral("overlay"));
  controller.setMaskFullResolutionPreview(true);
  controller.startSelectionBrush(false);
  if (controller.maskPreviewShowAll()) {
    return fail("mask preview should default to active-mask-only display");
  }
  controller.setMaskPreviewShowAll(true);
  if (!controller.maskPreviewShowAll()) {
    return fail("mask preview show-all control should enable all-mask display");
  }
  controller.setMaskPreviewShowAll(false);
  if (controller.maskPreviewShowAll()) {
    return fail("mask preview active-only control should disable all-mask display");
  }
  controller.setActiveMaskSelectionInverted(true);
  if (!controller.activeMaskSelectionInverted()) {
    return fail("mask preview inverted control should toggle active selection inversion");
  }
  controller.setActiveMaskSelectionInverted(false);
  if (controller.activeMaskSelectionInverted()) {
    return fail("mask preview normal control should restore non-inverted selection display");
  }
  controller.setActiveMaskBrushSize(0.18);
  controller.addBrushPoint(0.46, 0.54);

  const auto trace = controller.activeMaskOutlineTrace();
  const auto outline_paths = controller.activeMaskOutlinePaths();
  if (outline_paths.isEmpty() ||
      !outline_points_are_image_normalized(outline_paths) ||
      trace.value(QStringLiteral("outline_trace_source")).toString() !=
          QStringLiteral("composite-alpha") ||
      trace.value(QStringLiteral("mask_alpha_provider")).toString() !=
          QStringLiteral("brush-stroke-replay") ||
      trace.value(QStringLiteral("outline_coordinate_space")).toString() !=
          QStringLiteral("image-normalized") ||
      trace.value(QStringLiteral("outline_coordinate_contract")).toString() !=
          QStringLiteral("image-normalized|crop-aware|points-clamped-0-1:v1") ||
      trace.value(QStringLiteral("outline_alpha_source_contract")).toString() !=
          QStringLiteral("same-final-alpha-as-layer-mask:v1") ||
      trace.value(QStringLiteral("outline_alpha_source_provider")).toString() !=
          QStringLiteral("brush-stroke-replay") ||
      trace.value(QStringLiteral("outline_simplification_contract")).toString() !=
          QStringLiteral("rdp-closed-path|pixel-tolerance-bounded:v1") ||
      trace.value(QStringLiteral("outline_simplification_tolerance_pixels")).toDouble() <= 0.0 ||
      trace.value(QStringLiteral("outline_simplification_input_point_count")).toInt() <= 0 ||
      trace.value(QStringLiteral("outline_simplification_output_point_count")).toInt() <= 0 ||
      trace.value(QStringLiteral("outline_simplification_output_point_count")).toInt() >
          trace.value(QStringLiteral("outline_simplification_input_point_count")).toInt() ||
      !trace.value(QStringLiteral("outline_simplification_within_tolerance")).toBool() ||
      trace.value(QStringLiteral("outline_simplification_max_error_pixels")).toDouble() >
          trace.value(QStringLiteral("outline_simplification_tolerance_pixels")).toDouble() + 1e-6 ||
      trace.value(QStringLiteral("mask_alpha_raster_width")).toInt() != source.width() ||
      trace.value(QStringLiteral("mask_alpha_raster_height")).toInt() != source.height()) {
    const auto detail =
        QStringLiteral(
            "active brush outline trace should use final brush alpha in image-normalized coordinates "
            "(paths=%1 source=%2 provider=%3 coordinate=%4 contract=%5 alpha=%6 alphaProvider=%7 simplify=%8 tolerance=%9 maxError=%10 in=%11 out=%12 raster=%13x%14 expected=%15x%16)")
            .arg(outline_paths.size())
            .arg(trace.value(QStringLiteral("outline_trace_source")).toString())
            .arg(trace.value(QStringLiteral("mask_alpha_provider")).toString())
            .arg(trace.value(QStringLiteral("outline_coordinate_space")).toString())
            .arg(trace.value(QStringLiteral("outline_coordinate_contract")).toString())
            .arg(trace.value(QStringLiteral("outline_alpha_source_contract")).toString())
            .arg(trace.value(QStringLiteral("outline_alpha_source_provider")).toString())
            .arg(trace.value(QStringLiteral("outline_simplification_contract")).toString())
            .arg(trace.value(QStringLiteral("outline_simplification_tolerance_pixels")).toDouble())
            .arg(trace.value(QStringLiteral("outline_simplification_max_error_pixels")).toDouble())
            .arg(trace.value(QStringLiteral("outline_simplification_input_point_count")).toInt())
            .arg(trace.value(QStringLiteral("outline_simplification_output_point_count")).toInt())
            .arg(trace.value(QStringLiteral("mask_alpha_raster_width")).toInt())
            .arg(trace.value(QStringLiteral("mask_alpha_raster_height")).toInt())
            .arg(source.width())
            .arg(source.height())
            .toUtf8();
    return fail(detail.constData());
  }

  photo_editor::desktop::EditorPreviewController parity_controller;
  parity_controller.loadAsset(
      QStringLiteral("outline-alpha-parity-brush"),
      source_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  parity_controller.setMaskPreviewMode(QStringLiteral("overlay"));
  parity_controller.startSelectionBrush(false);
  parity_controller.setActiveMaskBrushSize(0.14);
  parity_controller.beginActiveBrushStroke();
  parity_controller.addBrushPoint(0.28, 0.32);
  parity_controller.addBrushPoint(0.62, 0.66);
  parity_controller.endActiveBrushStroke();
  if (!wait_for_brush_commit(parity_controller)) {
    return fail("brush alpha parity fixture should settle before tracing final outline");
  }
  const auto brush_alpha_trace = parity_controller.activeMaskOutlineTrace();
  const auto brush_alpha_paths = parity_controller.activeMaskOutlinePaths();
  if (brush_alpha_paths.isEmpty() ||
      !outline_points_are_image_normalized(brush_alpha_paths) ||
      !has_final_alpha_outline_contract(
          brush_alpha_trace,
          QStringLiteral("brush-stroke-replay")) ||
      brush_alpha_trace.value(QStringLiteral("outline_trace_source")).toString() !=
          QStringLiteral("composite-alpha")) {
    return fail("brush outline should be traced from the same settled brush alpha used by the final mask render");
  }

  parity_controller.startPolygonSelection();
  parity_controller.appendActivePolygonPoint(0.18, 0.20);
  parity_controller.appendActivePolygonPoint(0.76, 0.24);
  parity_controller.appendActivePolygonPoint(0.66, 0.74);
  parity_controller.appendActivePolygonPoint(0.24, 0.68);
  const auto polygon_mask =
      parity_controller.maskStack().at(parity_controller.activeMaskIndex()).toMap();
  const auto polygon_alpha_trace = parity_controller.activeMaskOutlineTrace();
  const auto polygon_alpha_paths = parity_controller.activeMaskOutlinePaths();
  if (polygon_alpha_paths.isEmpty() ||
      !outline_points_are_image_normalized(polygon_alpha_paths) ||
      !has_final_alpha_outline_contract(
          polygon_alpha_trace,
          QStringLiteral("procedural-geometry")) ||
      polygon_alpha_trace.value(QStringLiteral("outline_trace_source")).toString() !=
          QStringLiteral("vector-path") ||
      polygon_mask.value(QStringLiteral("shapeAlphaSourceContract")).toString() !=
          QStringLiteral("editable-path-plus-alpha-tile-cache-v1") ||
      polygon_mask.value(QStringLiteral("shapeAlphaSourceOfTruth")).toString() !=
          QStringLiteral("polygonPoints") ||
      !polygon_mask.value(QStringLiteral("shapeAlphaSourceSignature"))
           .toString()
           .startsWith(QStringLiteral("sha1:"))) {
    return fail("polygon outline should stay tied to editable polygon alpha and final procedural geometry");
  }

  parity_controller.startLassoSelection(false);
  const QVariantList lasso_raw_points{
      normalized_point(0.20, 0.26),
      normalized_point(0.34, 0.18),
      normalized_point(0.60, 0.24),
      normalized_point(0.72, 0.48),
      normalized_point(0.56, 0.74),
      normalized_point(0.28, 0.70),
  };
  for (const auto& point_value : lasso_raw_points) {
    const auto point = point_value.toMap();
    parity_controller.appendActivePolygonPoint(
        point.value(QStringLiteral("x")).toDouble(),
        point.value(QStringLiteral("y")).toDouble());
  }
  if (!parity_controller.closeActiveLassoPath()) {
    return fail("lasso alpha parity fixture should close before tracing final outline");
  }
  const auto lasso_mask =
      parity_controller.maskStack().at(parity_controller.activeMaskIndex()).toMap();
  const auto lasso_alpha_trace = parity_controller.activeMaskOutlineTrace();
  const auto lasso_alpha_paths = parity_controller.activeMaskOutlinePaths();
  if (lasso_alpha_paths.isEmpty() ||
      !outline_points_are_image_normalized(lasso_alpha_paths) ||
      !has_final_alpha_outline_contract(
          lasso_alpha_trace,
          QStringLiteral("procedural-geometry")) ||
      lasso_alpha_trace.value(QStringLiteral("outline_trace_source")).toString() !=
          QStringLiteral("vector-path") ||
      lasso_mask.value(QStringLiteral("shapeAlphaSourceContract")).toString() !=
          QStringLiteral("editable-path-plus-alpha-tile-cache-v1") ||
      lasso_mask.value(QStringLiteral("shapeAlphaSourceOfTruth")).toString() !=
          QStringLiteral("lassoRawPoints+polygonPoints") ||
      lasso_mask.value(QStringLiteral("shapeAlphaRawPointCount")).toInt() !=
          lasso_raw_points.size() ||
      !lasso_mask.value(QStringLiteral("shapeAlphaSourceSignature"))
           .toString()
           .startsWith(QStringLiteral("sha1:"))) {
    return fail("lasso outline should stay tied to editable lasso alpha and final procedural geometry");
  }

  const auto smart_source_path = runtime_root.filePath(QStringLiteral("outline-smart-source.png"));
  QImage smart_source(96, 96, QImage::Format_ARGB32);
  smart_source.fill(Qt::white);
  for (int y = 38; y <= 57; ++y) {
    for (int x = 10; x <= 25; ++x) {
      smart_source.setPixelColor(x, y, QColor(214, 78, 78));
    }
    for (int x = 40; x <= 55; ++x) {
      smart_source.setPixelColor(x, y, QColor(74, 118, 212));
    }
  }
  if (!smart_source.save(smart_source_path)) {
    return fail("smart alpha parity source image should be writable");
  }
  photo_editor::desktop::EditorPreviewController smart_controller;
  smart_controller.loadAsset(
      QStringLiteral("outline-alpha-parity-smart"),
      smart_source_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  smart_controller.setMaskPreviewMode(QStringLiteral("overlay"));
  smart_controller.smartSelectObjectAt(0.18, 0.5, true);
  if (!wait_for_smart_commit(smart_controller, 1)) {
    return fail("smart alpha parity fixture should commit before tracing final outline");
  }
  const auto smart_mask = smart_controller.maskStack().at(smart_controller.activeMaskIndex()).toMap();
  const auto smart_alpha_trace = smart_controller.activeMaskOutlineTrace();
  const auto smart_alpha_paths = smart_controller.activeMaskOutlinePaths();
  if (smart_controller.activeMaskKind() != QStringLiteral("object") ||
      smart_alpha_paths.isEmpty() ||
      !outline_points_are_image_normalized(smart_alpha_paths) ||
      !has_final_alpha_outline_contract(
          smart_alpha_trace,
          QStringLiteral("raster-payload")) ||
      smart_mask.value(QStringLiteral("generatedAlphaSourceContract")).toString() !=
          QStringLiteral("generated-alpha-plus-metadata-v1") ||
      smart_mask.value(QStringLiteral("generatedAlphaTargetType")).toString() !=
          QStringLiteral("object") ||
      !smart_mask.value(QStringLiteral("generatedAlphaSourceSignature"))
           .toString()
           .startsWith(QStringLiteral("sha1:"))) {
    return fail("smart-selection outline should be traced from the committed object raster alpha used by final rendering");
  }

  return EXIT_SUCCESS;
}
