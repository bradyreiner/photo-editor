#include <algorithm>
#include <cmath>
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
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QTemporaryDir>
#include <QThread>
#include <QVariantList>
#include <QVariantMap>

#include "editor_preview_controller.h"
#include "editor_preview_image_sink.h"
#include "canonical_composite_mask_alpha.h"
#include "scaled_mask_matte_service.h"

namespace photo_editor::desktop {

struct EditorPreviewControllerTestAccess {
  static QVariantMap last_mask_work_telemetry(const EditorPreviewController& controller) {
    return controller.last_mask_work_telemetry_;
  }

  static int active_mask_outline_cache_hit_count(const EditorPreviewController& controller) {
    return controller.mask_session_.outlineCacheHitCount();
  }

  static int active_mask_outline_cache_miss_count(const EditorPreviewController& controller) {
    return controller.mask_session_.outlineCacheMissCount();
  }

  static int active_mask_outline_full_key_count(const EditorPreviewController& controller) {
    return controller.mask_session_.outlineFullKeyCount();
  }

  static QString active_mask_outline_trace_source(const EditorPreviewController& controller) {
    return controller.active_mask_outline_trace_source_;
  }

  static QVariantMap active_mask_outline_trace_fields(const EditorPreviewController& controller) {
    return controller.active_mask_outline_trace_fields_;
  }

  static bool active_mask_outline_worker_running(
      const EditorPreviewController& controller) {
    return controller.mask_worker_coordinator_.activeMaskOutlineWatcher().isRunning();
  }

  static int active_mask_outline_worker_completed_count(
      const EditorPreviewController& controller) {
    return controller.mask_worker_coordinator_.activeMaskOutlineWorkerCompletedCount();
  }

  static int active_mask_outline_stale_result_count(
      const EditorPreviewController& controller) {
    return controller.mask_worker_coordinator_.activeMaskOutlineStaleResultCount();
  }

  static bool update_active_mask_definition(
      EditorPreviewController& controller,
      const QVariantMap& mask) {
    return controller.updateActiveMaskDefinition(mask);
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

  static bool full_render_requested(const EditorPreviewController& controller) {
    return controller.render_scheduler_.fullActive() ||
           controller.full_request_pending_ ||
           controller.full_render_watcher_.isRunning();
  }

  static RenderRequest current_render_request(
      const EditorPreviewController& controller,
      bool full_resolution) {
    return controller.currentRenderRequest(full_resolution);
  }

  static int render_generation(const EditorPreviewController& controller) {
    return controller.preview_session_.generation();
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

bool wait_for_active_mask_outline_worker(
    photo_editor::desktop::EditorPreviewController& controller,
    int minimum_completed_count,
    int timeout_ms = 120000) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    if (photo_editor::desktop::EditorPreviewControllerTestAccess::
            active_mask_outline_worker_completed_count(controller) >= minimum_completed_count &&
        !photo_editor::desktop::EditorPreviewControllerTestAccess::
            active_mask_outline_worker_running(controller)) {
      return true;
    }
    QThread::msleep(5);
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  return photo_editor::desktop::EditorPreviewControllerTestAccess::
             active_mask_outline_worker_completed_count(controller) >= minimum_completed_count &&
         !photo_editor::desktop::EditorPreviewControllerTestAccess::
             active_mask_outline_worker_running(controller);
}

QVariantMap settled_range_outline_trace(photo_editor::desktop::EditorPreviewController& controller) {
  const auto completed = photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_outline_worker_completed_count(controller);
  const auto trace = controller.activeMaskOutlineTrace();
  if (trace.value(QStringLiteral("outline_trace_source")).toString() == QStringLiteral("worker-pending") &&
      !wait_for_active_mask_outline_worker(controller, completed + 1, 10000)) return {};
  return controller.activeMaskOutlineTrace();
}

QVariantList settled_outline_paths(photo_editor::desktop::EditorPreviewController& controller) {
  if (settled_range_outline_trace(controller).isEmpty()) return {};
  return controller.activeMaskOutlinePaths();
}

QString computed_outline_source(const QVariantMap& trace) {
  return trace.value(QStringLiteral("outline_worker_source"),
      trace.value(QStringLiteral("outline_trace_source"))).toString();
}

QVariantMap normalized_point(double x, double y) {
  QVariantMap point;
  point.insert(QStringLiteral("x"), x);
  point.insert(QStringLiteral("y"), y);
  return point;
}

bool normalized_point_lists_match(
    const QVariantList& actual_points,
    const QVariantList& expected_points,
    double tolerance = 1e-9) {
  if (actual_points.size() != expected_points.size()) {
    return false;
  }
  for (int index = 0; index < expected_points.size(); ++index) {
    const auto actual = actual_points.at(index).toMap();
    const auto expected = expected_points.at(index).toMap();
    if (std::abs(actual.value(QStringLiteral("x")).toDouble() -
                 expected.value(QStringLiteral("x")).toDouble()) > tolerance ||
        std::abs(actual.value(QStringLiteral("y")).toDouble() -
                 expected.value(QStringLiteral("y")).toDouble()) > tolerance) {
      return false;
    }
  }
  return true;
}

QRectF normalized_points_bounds(const QVariantList& point_values) {
  auto min_x = 1.0;
  auto min_y = 1.0;
  auto max_x = 0.0;
  auto max_y = 0.0;
  auto point_count = 0;
  const auto accumulate = [&](const QVariantMap& point) {
    if (!point.contains(QStringLiteral("x")) || !point.contains(QStringLiteral("y"))) {
      return;
    }
    const auto x = point.value(QStringLiteral("x")).toDouble();
    const auto y = point.value(QStringLiteral("y")).toDouble();
    min_x = std::min(min_x, x);
    min_y = std::min(min_y, y);
    max_x = std::max(max_x, x);
    max_y = std::max(max_y, y);
    ++point_count;
  };
  for (const auto& point_value : point_values) {
    accumulate(point_value.toMap());
  }
  if (point_count == 0) {
    return {};
  }
  return QRectF(QPointF(min_x, min_y), QPointF(max_x, max_y)).normalized();
}

QRectF outline_bounds(const QVariantList& outline_paths) {
  QVariantList flattened_points;
  for (const auto& path_value : outline_paths) {
    const auto point = path_value.toMap();
    if (!point.isEmpty() && point.contains(QStringLiteral("x"))) {
      flattened_points.append(path_value);
      continue;
    }
    for (const auto& nested_value : path_value.toList()) {
      flattened_points.append(nested_value);
    }
  }
  return normalized_points_bounds(flattened_points);
}

int outline_path_count(const QVariantList& outline_paths) {
  if (outline_paths.isEmpty()) {
    return 0;
  }
  const auto first_point = outline_paths.first().toMap();
  if (!first_point.isEmpty() && first_point.contains(QStringLiteral("x"))) {
    return 1;
  }
  auto count = 0;
  for (const auto& path_value : outline_paths) {
    if (path_value.toList().size() >= 3) {
      ++count;
    }
  }
  return count;
}

double max_outline_segment_length(const QVariantList& outline_paths) {
  auto max_segment = 0.0;
  const auto point_distance = [](const QVariantMap& left, const QVariantMap& right) {
    const auto dx = left.value(QStringLiteral("x")).toDouble() -
                    right.value(QStringLiteral("x")).toDouble();
    const auto dy = left.value(QStringLiteral("y")).toDouble() -
                    right.value(QStringLiteral("y")).toDouble();
    return std::hypot(dx, dy);
  };
  const auto accumulate_path = [&](const QVariantList& path) {
    if (path.size() < 2) {
      return;
    }
    for (auto index = 0; index < path.size(); ++index) {
      const auto current = path.at(index).toMap();
      const auto next = path.at((index + 1) % path.size()).toMap();
      if (current.contains(QStringLiteral("x")) && next.contains(QStringLiteral("x"))) {
        max_segment = std::max(max_segment, point_distance(current, next));
      }
    }
  };
  if (!outline_paths.isEmpty() && outline_paths.first().toMap().contains(QStringLiteral("x"))) {
    accumulate_path(outline_paths);
    return max_segment;
  }
  for (const auto& path_value : outline_paths) {
    accumulate_path(path_value.toList());
  }
  return max_segment;
}

bool bounds_match(const QRectF& actual, const QRectF& expected, double tolerance) {
  return actual.isValid() && expected.isValid() &&
         std::abs(actual.left() - expected.left()) <= tolerance &&
         std::abs(actual.top() - expected.top()) <= tolerance &&
         std::abs(actual.right() - expected.right()) <= tolerance &&
         std::abs(actual.bottom() - expected.bottom()) <= tolerance;
}

QRectF alpha_mask_bounds(
    const QByteArray& mask_data,
    int width,
    int height,
    unsigned char threshold = 96) {
  if (mask_data.isEmpty() || width <= 0 || height <= 0 || mask_data.size() < width * height) {
    return {};
  }

  auto min_x = width;
  auto min_y = height;
  auto max_x = -1;
  auto max_y = -1;
  for (auto y = 0; y < height; ++y) {
    for (auto x = 0; x < width; ++x) {
      if (static_cast<unsigned char>(mask_data.at(y * width + x)) < threshold) {
        continue;
      }
      min_x = std::min(min_x, x);
      min_y = std::min(min_y, y);
      max_x = std::max(max_x, x);
      max_y = std::max(max_y, y);
    }
  }
  if (max_x < min_x || max_y < min_y) {
    return {};
  }

  return QRectF(
      QPointF(
          static_cast<double>(min_x) / static_cast<double>(width),
          static_cast<double>(min_y) / static_cast<double>(height)),
      QPointF(
          static_cast<double>(max_x + 1) / static_cast<double>(width),
          static_cast<double>(max_y + 1) / static_cast<double>(height)));
}

double outline_tolerance_for_image_pixels(int width, int height, double pixels = 0.5) {
  return pixels / static_cast<double>(std::max(1, std::min(width, height)));
}

QString outline_bounds_detail(const QString& label, const QRectF& outline, const QRectF& expected) {
  return QStringLiteral("%1 outline bounds mismatch: outline=(%2,%3 %4x%5) expected=(%6,%7 %8x%9)")
      .arg(label)
      .arg(outline.x(), 0, 'f', 4)
      .arg(outline.y(), 0, 'f', 4)
      .arg(outline.width(), 0, 'f', 4)
      .arg(outline.height(), 0, 'f', 4)
      .arg(expected.x(), 0, 'f', 4)
      .arg(expected.y(), 0, 'f', 4)
      .arg(expected.width(), 0, 'f', 4)
      .arg(expected.height(), 0, 'f', 4);
}

bool has_final_alpha_outline_contract(
    const QVariantMap& trace,
    const QString& expected_provider) {
  const auto source = trace.value(QStringLiteral("outline_trace_source")).toString();
  return (source == QStringLiteral("vector-path") ||
          source == QStringLiteral("clean-canvas") ||
          source == QStringLiteral("refinement-clean-outline") ||
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
  const auto single_brush_source_path =
      runtime_root.filePath(QStringLiteral("single-brush-outline-source.png"));
  if (!source.scaled(96, 64).save(single_brush_source_path)) {
    return fail("single brush outline source image should be writable");
  }
  photo_editor::desktop::EditorPreviewController single_brush_outline_controller;
  single_brush_outline_controller.loadAsset(
      QStringLiteral("outline-single-brush-raster"),
      single_brush_source_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  single_brush_outline_controller.startSelectionBrush();
  single_brush_outline_controller.setActiveMaskBrushSize(0.18);
  single_brush_outline_controller.addBrushPoint(0.44, 0.52);
  const auto single_brush_mask_before =
      single_brush_outline_controller.maskStack()
          .at(single_brush_outline_controller.activeMaskIndex()).toMap();
  const auto single_brush_alpha_before =
      photo_editor::desktop::evaluate_canonical_composite_mask_alpha(
          source.scaled(96, 64),
          QVariantList{photo_editor::desktop::mask_with_scaled_segmentation_matte(
              single_brush_mask_before, QSize(96, 64), QStringLiteral("active-outline"))},
          1.0,
          {1.0, 1.0, 1.0});
  const auto single_brush_before_expand =
      single_brush_outline_controller.activeMaskOutlinePaths();
  const auto single_brush_before_trace =
      single_brush_outline_controller.activeMaskOutlineTrace();
  single_brush_outline_controller.setActiveMaskSelectionExpand(55.0);
  const auto single_brush_mask_after =
      single_brush_outline_controller.maskStack()
          .at(single_brush_outline_controller.activeMaskIndex()).toMap();
  const auto single_brush_alpha_after =
      photo_editor::desktop::evaluate_canonical_composite_mask_alpha(
          source.scaled(96, 64),
          QVariantList{photo_editor::desktop::mask_with_scaled_segmentation_matte(
              single_brush_mask_after, QSize(96, 64), QStringLiteral("active-outline"))},
          1.0,
          {1.0, 1.0, 1.0});
  const auto single_brush_after_expand =
      single_brush_outline_controller.activeMaskOutlinePaths();
  const auto single_brush_after_trace =
      single_brush_outline_controller.activeMaskOutlineTrace();
  if (single_brush_outline_controller.activeMaskKind() != QStringLiteral("brush") ||
      single_brush_outline_controller.activeMaskToolRole() != QStringLiteral("selection") ||
      single_brush_before_expand.isEmpty() ||
      single_brush_after_expand.isEmpty() ||
      single_brush_before_expand == single_brush_after_expand) {
    const auto detail =
        QStringLiteral(
            "single brush raster outline should respond immediately to selection expansion "
            "(before=%1 after=%2 beforeSource=%3 afterSource=%4 beforeRevision=%5 afterRevision=%6 "
            "beforeHash=%7 afterHash=%8 beforeAlphaHash=%9 afterAlphaHash=%10)")
            .arg(single_brush_before_expand.size())
            .arg(single_brush_after_expand.size())
            .arg(single_brush_before_trace.value(QStringLiteral("outline_trace_source")).toString())
            .arg(single_brush_after_trace.value(QStringLiteral("outline_trace_source")).toString())
            .arg(single_brush_before_trace.value(QStringLiteral("mask_work_state_revision")).toInt())
            .arg(single_brush_after_trace.value(QStringLiteral("mask_work_state_revision")).toInt())
            .arg(single_brush_before_trace.value(QStringLiteral("mask_alpha_trace_hash")).toString())
            .arg(single_brush_after_trace.value(QStringLiteral("mask_alpha_trace_hash")).toString())
            .arg(single_brush_alpha_before.alpha_hash)
            .arg(single_brush_alpha_after.alpha_hash)
            .toUtf8();
    return fail(detail.constData());
  }
  QImage full_resolution_source(800, 600, QImage::Format_ARGB32);
  full_resolution_source.fill(QColor(82, 106, 138, 255));
  const auto full_resolution_source_path =
      runtime_root.filePath(QStringLiteral("outline-full-resolution-source.png"));
  if (!full_resolution_source.save(full_resolution_source_path)) {
    return fail("full-resolution outline source image should be writable");
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
  controller.setMaskPreviewMode(QStringLiteral("outline"));
  if (controller.maskPreviewMode() != QStringLiteral("outline") ||
      controller.maskPreviewDisplayMode() != QStringLiteral("outline")) {
    return fail("outline should be a normal one-color boundary display mode");
  }
  controller.setMaskPreviewMode(QStringLiteral("grayscale-alpha"));
  if (controller.maskPreviewMode() != QStringLiteral("grayscale") ||
      controller.maskPreviewDisplayMode() != QStringLiteral("grayscale-alpha")) {
    return fail("grayscale alpha should use the public matte display name");
  }
  controller.setMaskPreviewMode(QStringLiteral("isolated-mask"));
  if (controller.maskPreviewMode() != QStringLiteral("on-black") ||
      controller.maskPreviewDisplayMode() != QStringLiteral("isolated-mask")) {
    return fail("isolated mask should use the public isolated display name");
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
  const auto mask_view_source =
      read_source_file(QStringLiteral("apps/desktop/qml/workspaces/MaskView.qml"));
  const auto studio_workspace_source =
      read_source_file(QStringLiteral("apps/desktop/qml/workspaces/StudioWorkspace.qml")) +
      read_source_file(QStringLiteral("apps/desktop/qml/components/StudioImageViewport.qml")) +
      read_source_file(QStringLiteral("apps/desktop/qml/components/StudioPixelGridOverlay.qml")) +
      read_source_file(QStringLiteral("apps/desktop/qml/components/StudioDiagnosticOverlayStack.qml")) +
      read_source_file(QStringLiteral("apps/desktop/qml/components/StudioViewportLoupe.qml")) +
      read_source_file(QStringLiteral("apps/desktop/qml/components/StudioViewportInputCommandSurface.qml")) +
      read_source_file(QStringLiteral("apps/desktop/qml/components/StudioAdjustmentSlider.qml"));
  const auto layer_row_source =
      read_source_file(QStringLiteral("apps/desktop/qml/components/LayerRow.qml"));
  const auto coordinate_mapper_source =
      read_source_file(QStringLiteral("apps/desktop/qml/components/CanvasCoordinateMapper.qml"));
  const auto shared_overlay_source =
      read_source_file(QStringLiteral("apps/desktop/qml/workspaces/SharedMaskOverlay.qml"));
  const auto canvas_overlay_source =
      read_source_file(QStringLiteral("apps/desktop/qml/workspaces/MaskCanvasOverlay.qml"));
  const auto image_viewport_source =
      read_source_file(QStringLiteral("apps/desktop/qml/components/StudioImageViewport.qml"));
  const auto mask_inspector_sections_source =
      read_source_file(QStringLiteral("apps/desktop/qml/workspaces/MaskInspectorSections.qml"));
  const auto controller_source =
      read_source_file(QStringLiteral("apps/desktop/src/editor_preview_controller.cpp"));
  const auto controller_header_source =
      read_source_file(QStringLiteral("apps/desktop/src/editor_preview_controller.h"));
  const auto edit_render_policy_source =
      read_source_file(QStringLiteral("apps/desktop/src/edit_render_policy.h"));
  const auto texture_item_header_source =
      read_source_file(QStringLiteral("apps/desktop/src/editor_preview_texture_item.h"));
  const auto texture_item_source =
      read_source_file(QStringLiteral("apps/desktop/src/editor_preview_texture_item.cpp"));
  const auto thumbnail_queue_service_header_source =
      read_source_file(QStringLiteral("apps/desktop/src/thumbnail_refresh_queue_service.h"));
  const auto thumbnail_queue_service_source =
      read_source_file(QStringLiteral("apps/desktop/src/thumbnail_refresh_queue_service.cpp"));
  const auto thumbnail_worker_renderer_factory_source =
      read_source_file(QStringLiteral("apps/desktop/src/thumbnail_worker_renderer_factory.cpp"));
  const auto thumbnail_presentation_service_source =
      read_source_file(QStringLiteral("apps/desktop/src/thumbnail_presentation_service.cpp"));
  const auto thumbnail_cache_session_source =
      read_source_file(QStringLiteral("apps/desktop/src/thumbnail_cache_session.h"));
  const auto color_layer_runtime_source =
      read_source_file(QStringLiteral("engine/src/pipeline/color_layer_runtime.cpp"));
  const auto photo_layer_adjustment_recipe_source =
      read_source_file(QStringLiteral("apps/desktop/src/photo_layer_adjustment_recipe.cpp"));
  if (canvas_overlay_source.isEmpty() ||
      !canvas_overlay_source.contains(QStringLiteral("pointerCoordinateMappingContract")) ||
      !canvas_overlay_source.contains(QStringLiteral("mask-canvas-pointer-mapper:v1|CanvasCoordinateMapper|crop-rotation-aware|all-mask-inputs")) ||
      !canvas_overlay_source.contains(QStringLiteral("CanvasCoordinateMapper {\n        id: maskCoordinateMapper")) ||
      !canvas_overlay_source.contains(QStringLiteral("function imagePointFromItem(")) ||
      !canvas_overlay_source.contains(QStringLiteral("maskCoordinateMapper.viewToImagePoint")) ||
      !canvas_overlay_source.contains(QStringLiteral("function previewOutlinePoint(normalizedX, normalizedY)")) ||
      !canvas_overlay_source.contains(QStringLiteral("const point = root.viewPointForImage(normalizedX, normalizedY)")) ||
      !canvas_overlay_source.contains(QStringLiteral("const firstPoint = root.previewOutlinePoint(")) ||
      !canvas_overlay_source.contains(QStringLiteral("return Math.max(0.75, Number(logicalPixels || 1.0))")) ||
      !canvas_overlay_source.contains(QStringLiteral("return Math.max(1.0, Number(logicalPixels || 1.0))")) ||
      !canvas_overlay_source.contains(QStringLiteral("outline-scale-buckets:v1|power-of-two-ceil|precision-never-below-display-scale")) ||
      !canvas_overlay_source.contains(QStringLiteral("Math.pow(2.0, Math.ceil(Math.log(safeScale) / Math.LN2))")) ||
      !shared_overlay_source.contains(QStringLiteral("outline-scale-buckets:v1|power-of-two-ceil|precision-never-below-display-scale")) ||
      !shared_overlay_source.contains(QStringLiteral("Math.pow(2.0, Math.ceil(Math.log(safeScale) / Math.LN2))")) ||
      canvas_overlay_source.contains(QStringLiteral("root.previewPointX(root.pointXValue(")) ||
      canvas_overlay_source.contains(QStringLiteral("root.previewPointY(root.pointYValue(")) ||
      !canvas_overlay_source.contains(QStringLiteral("function scheduleSmartSelectPreviewFromItem(")) ||
      mask_view_source.isEmpty() ||
      !mask_view_source.contains(QStringLiteral("pointerCoordinateMappingContract")) ||
      !mask_view_source.contains(QStringLiteral("mask-view-pointer-mapper:v1|CanvasCoordinateMapper|crop-rotation-aware|all-mask-inputs")) ||
      !mask_view_source.contains(QStringLiteral("CanvasCoordinateMapper {\n        id: maskCoordinateMapper")) ||
      !mask_view_source.contains(QStringLiteral("function imagePointFromItem(")) ||
      !mask_view_source.contains(QStringLiteral("maskCoordinateMapper.viewToImagePoint")) ||
      canvas_overlay_source.contains(QStringLiteral("mouse.x / previewItem.width")) ||
      canvas_overlay_source.contains(QStringLiteral("mouse.y / previewItem.height")) ||
      canvas_overlay_source.contains(QStringLiteral("x / brushOverlay.width")) ||
      canvas_overlay_source.contains(QStringLiteral("y / brushOverlay.height")) ||
      canvas_overlay_source.contains(QStringLiteral("mouse.x / brushOverlay.width")) ||
      canvas_overlay_source.contains(QStringLiteral("mouse.y / brushOverlay.height")) ||
      mask_view_source.contains(QStringLiteral("mouse.x / previewItem.width")) ||
      mask_view_source.contains(QStringLiteral("mouse.y / previewItem.height")) ||
      mask_view_source.contains(QStringLiteral("x / brushOverlay.width")) ||
      mask_view_source.contains(QStringLiteral("y / brushOverlay.height")) ||
      mask_view_source.contains(QStringLiteral("mouse.x / brushOverlay.width")) ||
      mask_view_source.contains(QStringLiteral("mouse.y / brushOverlay.height")) ||
      mask_view_source.contains(QStringLiteral("mouse.y / linearOverlay.height")) ||
      mask_view_source.contains(QStringLiteral("point.position.x / Math.max(1, previewItem.width)")) ||
      mask_view_source.contains(QStringLiteral("point.position.y / Math.max(1, previewItem.height)")) ||
      mask_view_source.contains(QStringLiteral("eventPoint.position.x / Math.max(1, previewItem.width)")) ||
      mask_view_source.contains(QStringLiteral("eventPoint.position.y / Math.max(1, previewItem.height)"))) {
    return fail("MaskCanvasOverlay and MaskView input and outline drawing should use CanvasCoordinateMapper instead of direct local width/height math");
  }
  if (!studio_workspace_source.contains(QStringLiteral("layer-mask-activation-latency:v2|panel-first:true|outline-trace:next-event-loop|delay-ms:0")) ||
      !studio_workspace_source.contains(QStringLiteral("id: maskOutlineActivationTimer\n        interval: 0")) ||
      !studio_workspace_source.contains(QStringLiteral("root.selectedEditTargetMaskSetId().length > 0")) ||
      !studio_workspace_source.contains(QStringLiteral("masked: editorPreview.hasActiveMask")) ||
      !controller_header_source.contains(QStringLiteral("activeMaskOutlinePaths READ activeMaskOutlinePaths NOTIFY maskOverlayChanged")) ||
      !controller_header_source.contains(QStringLiteral("activeMaskOutlineDisplayScale READ activeMaskOutlineDisplayScale WRITE setActiveMaskOutlineDisplayScale NOTIFY maskOverlayChanged"))) {
    return fail("Studio Layers should keep selection outlines visible, next-tick responsive, and isolated from broad controls notifications");
  }
  if (!shared_overlay_source.contains(QStringLiteral("final-alpha-contour|overlay-fill|marching-ants:v2")) ||
      !shared_overlay_source.contains(QStringLiteral("property bool selectionFillVisible: false")) ||
      !shared_overlay_source.contains(QStringLiteral("if (!root.selectionFillVisible ||")) ||
      !shared_overlay_source.contains(QStringLiteral("root.fillOutlinePaths(ctx, outlinePaths)")) ||
      !canvas_overlay_source.contains(QStringLiteral("final-alpha-contour|overlay-fill|marching-ants:v2")) ||
      !canvas_overlay_source.contains(QStringLiteral("property bool selectionFillVisible: false")) ||
      !canvas_overlay_source.contains(QStringLiteral("return root.selectionFillVisible &&")) ||
      !canvas_overlay_source.contains(QStringLiteral("root.fillOutlinePaths(")) ||
      !image_viewport_source.contains(QStringLiteral("selectionFillVisible: workspaceContext.editInspectorTab === \"layers\"")) ||
      !studio_workspace_source.contains(QStringLiteral("editorPreview.requestLayerFullResolutionPreview()")) ||
      !controller_header_source.contains(QStringLiteral("Q_INVOKABLE bool requestLayerFullResolutionPreview();")) ||
      !mask_inspector_sections_source.contains(QStringLiteral("text: \"Marching Ants\"")) ||
      !studio_workspace_source.contains(QStringLiteral("onClicked: editorPreview.maskPreviewMode = \"marching-ants\"")) ||
      !studio_workspace_source.contains(QStringLiteral("onClicked: root.selectMaskSetAndLayer(root.activeMaskSetData(), true, false)")) ||
      !studio_workspace_source.contains(QStringLiteral("value: root.clamp(editorPreview.activeMaskSelectionSmooth, 0.0, 100.0)")) ||
      !studio_workspace_source.contains(QStringLiteral("value: root.clamp(editorPreview.activeMaskSelectionFineEdge, 0.0, 100.0)")) ||
      !controller_source.contains(QStringLiteral("mask_def = mask_with_neutralized_selection_refinement(mask_def);")) ||
      !controller_source.contains(QStringLiteral("mask_def.insert(QStringLiteral(\"feather\"), 0.0);"))) {
    return fail("Selection UX should keep fill exclusive to Layers, retain marching ants elsewhere, restore max preview for layer editing, and keep generated masks neutral");
  }
  if (!canvas_overlay_source.contains(QStringLiteral("function scheduleSmartSelectPreviewFromItem(item, itemX, itemY, force) {\n        const point = imagePointFromItem(item, itemX, itemY)")) ||
      !canvas_overlay_source.contains(QStringLiteral("function beginSmartSelectStroke(mouseX, mouseY) {\n        const point = normalizedStrokePoint(mouseX, mouseY)\n        editorPreview.beginSmartSelectStroke(point.x, point.y")) ||
      !canvas_overlay_source.contains(QStringLiteral("function updateSmartSelectStroke(mouseX, mouseY) {\n        const point = normalizedStrokePoint(mouseX, mouseY)\n        editorPreview.updateSmartSelectStroke(point.x, point.y")) ||
      !canvas_overlay_source.contains(QStringLiteral("const point = normalizedStrokePoint(mouse.x, mouse.y)\n        if (addNegativePrompt)")) ||
      !canvas_overlay_source.contains(QStringLiteral("root.scheduleSmartSelectPreviewFromItem(previewItem, mouseX, mouseY, false)")) ||
      !canvas_overlay_source.contains(QStringLiteral("root.scheduleSmartSelectPreviewFromItem(previewItem, mouse.x, mouse.y, false)")) ||
      canvas_overlay_source.contains(QStringLiteral("beginSmartSelectStroke(mouse.x /")) ||
      canvas_overlay_source.contains(QStringLiteral("updateSmartSelectStroke(mouse.x /")) ||
      canvas_overlay_source.contains(QStringLiteral("scheduleSmartSelectPreview(mouse.x")) ||
      !mask_view_source.contains(QStringLiteral("function beginSmartSelectStroke(mouseX, mouseY) {\n        const point = normalizedStrokePoint(previewItem, mouseX, mouseY)")) ||
      !mask_view_source.contains(QStringLiteral("function updateSmartSelectStroke(mouseX, mouseY) {\n        const point = normalizedStrokePoint(previewItem, mouseX, mouseY)")) ||
      !mask_view_source.contains(QStringLiteral("const imagePoint = root.normalizedStrokePoint(previewItem, point.position.x, point.position.y)\n                                    if (imagePoint.valid)\n                                        editorPreview.scheduleSmartSelectPreview(imagePoint.x, imagePoint.y)")) ||
      !mask_view_source.contains(QStringLiteral("root.beginSmartSelectStroke(parent.smartPointerX, parent.smartPointerY)")) ||
      !mask_view_source.contains(QStringLiteral("root.updateSmartSelectStroke(parent.smartPointerX, parent.smartPointerY)")) ||
      mask_view_source.contains(QStringLiteral("scheduleSmartSelectPreview(point.position.x")) ||
      !studio_workspace_source.contains(QStringLiteral("function normalizedPointForViewport(x, y) {\n        const point = viewportCoordinateMapper.viewToImagePoint(x, y)")) ||
      !studio_workspace_source.contains(QStringLiteral("if (!previewContext.hasActiveMask || kind === \"object\") {\n            const objectMode = workspaceContext.applyObjectPickModeForModifierState(mouse.modifiers)")) ||
      !studio_workspace_source.contains(QStringLiteral("previewContext.smartSelectSubtractAt(point.x, point.y, !previewContext.hasActiveMask)")) ||
      !studio_workspace_source.contains(QStringLiteral("previewContext.smartSelectAddAt(point.x, point.y, !previewContext.hasActiveMask)")) ||
      studio_workspace_source.contains(QStringLiteral("smartSelectAddAt(mouse.x")) ||
      studio_workspace_source.contains(QStringLiteral("smartSelectSubtractAt(mouse.x"))) {
    return fail("Smart/Object selection hover, prompt, stroke, and one-click commit paths should all use mapper-backed normalized image coordinates");
  }
  if (studio_workspace_source.isEmpty() ||
      !studio_workspace_source.contains(QStringLiteral("pixelInspectionContract: \"pixel-inspection:v5|one-to-one:one-source-pixel-per-device-pixel|grid-threshold:8-logical-px|full-resolution-from-one-to-one-up:true|full-resolution-visible-coverage-required:true|crop-output-mapped-to-source:true|cursor-anchored:true|crisp-source-pixels:true|proxy-fallback:false|adaptive-source-pixel|max:source-pixel-4x-min-6400|wheel:exponential-delta\"")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property real sourcePixelInspectionZoom: inspectionPixelWidth > 0")) ||
      !studio_workspace_source.contains(QStringLiteral("pixelGridThreshold * inspectionPixelWidth / Math.max(1, basePaintedContentWidth)")) ||
      !studio_workspace_source.contains(QStringLiteral("pixelGridThreshold * inspectionPixelHeight / Math.max(1, basePaintedContentHeight)")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property real maxInspectionZoom: Math.max(")) ||
      !studio_workspace_source.contains(QStringLiteral("sourcePixelInspectionZoom * deepInspectionZoomMultiplier")) ||
      !studio_workspace_source.contains(QStringLiteral("minimumDeepInspectionZoom")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property var inspectionZoomLevels: [1.0, 2.0, 4.0, 8.0, 16.0, 32.0, 64.0, 128.0, maxInspectionZoom]")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property real pixelGridThreshold: 8.0")) ||
	      !studio_workspace_source.contains(QStringLiteral("readonly property bool pixelGridFullResolutionBacked: nativePreviewActive &&")) ||
	      !studio_workspace_source.contains(QStringLiteral("previewContext.displayPreviewNativeViewportFullResolution")) ||
	      !studio_workspace_source.contains(QStringLiteral("readonly property string pixelGridRenderContract: \"pixel-grid-render:v6|scenegraph-lines:true|viewport-bounded:true|visible-lines-only:true|no-raster-canvas:true|content-aligned:true|source-backed-required:true|visible-region-coverage-required:true|crop-output-coordinates:true\"")) ||
      !studio_workspace_source.contains(QStringLiteral("sourcePixelScreenWidth >= pixelGridThreshold")) ||
      !studio_workspace_source.contains(QStringLiteral("sourcePixelScreenHeight >= pixelGridThreshold")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property bool pixelGridVisible: pixelGridRequested &&")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property bool highCostInspectionOverlaysVisible: inspectionDisplayReady")) ||
      !studio_workspace_source.contains(QStringLiteral("pixel-inspection-performance:v5|full-resolution-visible-tile-required:true|proxy-display-while-inspecting:false|viewport-publisher-service:true|signature-dedup:true|event-loop-coalesced:true|source-patch-refresh:immediate")) ||
      !studio_workspace_source.contains(QStringLiteral("previewContext.schedulePreviewViewportPublication(")) ||
      studio_workspace_source.contains(QStringLiteral("publishedPreviewViewportSettleTimer")) ||
      studio_workspace_source.contains(QStringLiteral("lastPublishedPreviewViewportSignature")) ||
      !studio_workspace_source.contains(QStringLiteral("function nextInspectionZoom()")) ||
      !studio_workspace_source.contains(QStringLiteral("function previousInspectionZoom()")) ||
      !studio_workspace_source.contains(QStringLiteral("function wheelZoomFactor(wheel)")) ||
	      !studio_workspace_source.contains(QStringLiteral("workspaceContext.clamp(requestedZoom, 0.25, safeMaxZoom)")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property real oneToOneZoom: Math.max(")) ||
	      !studio_workspace_source.contains(QStringLiteral("onPixelInspectionRequestedChanged: schedulePublishedPreviewViewportUpdate(true)")) ||
	      !studio_workspace_source.contains(QStringLiteral("function schedulePreviewChangedViewportUpdate()")) ||
	      !studio_workspace_source.contains(QStringLiteral("function onPreviewChanged() { viewport.schedulePreviewChangedViewportUpdate() }")) ||
	      !controller_source.contains(QStringLiteral("preview.render.visible_tile_result_skipped")) ||
	      !studio_workspace_source.contains(QStringLiteral("readonly property real basePaintedContentX: contentMargin")) ||
		      !studio_workspace_source.contains(QStringLiteral("contentX: viewport.basePaintedContentX + viewport.panX")) ||
		      !studio_workspace_source.contains(QStringLiteral("contentWidth: viewport.basePaintedContentWidth")) ||
	      !studio_workspace_source.contains(QStringLiteral("smooth: !viewport.pixelInspectionActive")) ||
      !studio_workspace_source.contains(QStringLiteral("crispPixels: viewport.pixelInspectionActive")) ||
      !studio_workspace_source.contains(QStringLiteral("loupeVisible: workspaceContext.loupeEnabled && viewport.loupeEnabled &&\n                      viewport.highCostInspectionOverlaysVisible")) ||
      !studio_workspace_source.contains(QStringLiteral("focusPeakingEnabled: workspaceContext.focusPeakingOverlayEnabled && viewport.focusPeakingOverlayEnabled")) ||
	      !studio_workspace_source.contains(QStringLiteral("id: pixelGridOverlay")) ||
	      !studio_workspace_source.contains(QStringLiteral("anchors.fill: parent")) ||
	      !studio_workspace_source.contains(QStringLiteral("readonly property bool gridGeometryValid:")) ||
	      !studio_workspace_source.contains(QStringLiteral("readonly property int verticalStartIndex:")) ||
		      studio_workspace_source.contains(QStringLiteral("id: pixelGridPaintTimer")) ||
	      !studio_workspace_source.contains(QStringLiteral("readonly property real stepX: pixelGridOverlay.sourcePixelScreenWidth")) ||
	      !studio_workspace_source.contains(QStringLiteral("readonly property real visibleLeft: Math.max(0, contentLeft)")) ||
	      !studio_workspace_source.contains(QStringLiteral("Math.floor((visibleLeft - contentLeft) / stepX)")) ||
	      !studio_workspace_source.contains(QStringLiteral("stepX >= pixelGridOverlay.pixelGridThreshold")) ||
      studio_workspace_source.contains(QStringLiteral("id: pixelGridOverlay\n            objectName: viewport.objectName.length > 0 ? viewport.objectName + \"PixelGridOverlay\" : \"pixelGridOverlay\"\n            x: viewport.paintedContentX")) ||
      !studio_workspace_source.contains(QStringLiteral("previewContext.schedulePreviewViewportPublication(")) ||
      !studio_workspace_source.contains(QStringLiteral("editViewport.nextInspectionZoom()")) ||
      controller_header_source.isEmpty() ||
      !controller_header_source.contains(QStringLiteral("Q_INVOKABLE void schedulePreviewViewportPublication(")) ||
      !controller_header_source.contains(QStringLiteral("Q_PROPERTY(bool displayPreviewNativeFullResolution READ displayPreviewNativeFullResolution NOTIFY previewChanged)")) ||
      !controller_source.contains(QStringLiteral("visible_render_tiles_for_request(")) ||
      !controller_source.contains(QStringLiteral("queued_request.visible_tile_only &&\n      queued_request.tile_visible_region_active")) ||
      !controller_source.contains(QStringLiteral("display_preview_texture_->ensureNativeTexture(")) ||
      !controller_source.contains(QStringLiteral("preview_viewport_patch_max_edge_ > 0")) ||
      !controller_source.contains(QStringLiteral("source_pixel_inspection_request")) ||
      texture_item_header_source.isEmpty() ||
      !texture_item_header_source.contains(QStringLiteral("Q_PROPERTY(bool crispPixels READ crispPixels WRITE setCrispPixels NOTIFY crispPixelsChanged)")) ||
      texture_item_source.isEmpty() ||
      !texture_item_source.contains(QStringLiteral("node->setFiltering(crisp_pixels_ ? QSGTexture::Nearest : QSGTexture::Linear)"))) {
    return fail("StudioWorkspace pixel inspection should expose adaptive source-pixel zoom, threshold-gated grid, and crisp provider/native preview filtering");
  }
  const auto mask_pointer_press_index =
      studio_workspace_source.indexOf(QStringLiteral("if (viewport.handleMaskPointerPressed(mouse)"));
  const auto inspector_sampling_index =
      studio_workspace_source.indexOf(QStringLiteral("workspace.beginPixelInspectorSampling()"));
  if (mask_pointer_press_index < 0 || inspector_sampling_index < 0 ||
      mask_pointer_press_index > inspector_sampling_index ||
      !studio_workspace_source.contains(QStringLiteral("viewport.updatePixelInspectorAt(mouse.x, mouse.y)")) ||
      !studio_workspace_source.contains(QStringLiteral("visible: root.pixelInspectorSamplingActive && inspector && inspector.active === true && root.editTool !== \"crop\""))) {
    return fail("StudioWorkspace pixel inspector should show coordinates/colors without intercepting brush or mask pointer presses");
  }
  if (!studio_workspace_source.contains(QStringLiteral("function startPixelBrushSource(operationName)")) ||
      !studio_workspace_source.contains(QStringLiteral("function startPixelSelectionSource(operationName)")) ||
      !studio_workspace_source.contains(QStringLiteral("function pixelBrushToolReady()")) ||
      !studio_workspace_source.contains(QStringLiteral("function pixelSelectToolReady()")) ||
      !studio_workspace_source.contains(QStringLiteral("function pixelExactToolReady()")) ||
      !studio_workspace_source.contains(QStringLiteral("root.editTool = \"pixel-brush\"")) ||
      !studio_workspace_source.contains(QStringLiteral("label: \"Pixel Brush Add\"")) ||
      !studio_workspace_source.contains(QStringLiteral("label: \"Pixel Brush Erase\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskSourcePixelBrushAddButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskSourcePixelBrushEraseButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("return \"Pixel Brush\"")) ||
      !studio_workspace_source.contains(QStringLiteral("root.maskOperationLabel(root.maskCreationOperation) + \" / snapped source pixels\"")) ||
      !studio_workspace_source.contains(QStringLiteral("previewContext.applyActiveSelectionPixel(point.x, point.y, operation)"))) {
    return fail("StudioWorkspace Pixel Brush should expose Add/Erase operation state and write mapped source pixels through the controller");
  }
  if (!controller_source.contains(QStringLiteral("fallback_metadata.insert(QStringLiteral(\"smartFallback\"), true);")) ||
      !controller_source.contains(QStringLiteral("Created an editable rough selection fallback for refinement.")) ||
      !mask_inspector_source.contains(QStringLiteral("function smartSelectionFallbackState(mask)")) ||
      !mask_inspector_source.contains(QStringLiteral("metadata.smartFallback === true ? \"Editable preview\" : \"\"")) ||
      !mask_inspector_source.contains(QStringLiteral("const fallbackState = root.smartSelectionFallbackState(mask)")) ||
      !studio_workspace_source.contains(QStringLiteral("function activeMaskData()")) ||
      !studio_workspace_source.contains(QStringLiteral("function smartSelectionFallbackState(mask)")) ||
      !studio_workspace_source.contains(QStringLiteral("metadata.smartFallback === true ? \"Editable preview\" : \"\"")) ||
      !studio_workspace_source.contains(QStringLiteral("const fallbackState = root.smartSelectionFallbackState(root.activeMaskData())")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string fallbackState: root.smartSelectionFallbackState(root.activeMaskData())")) ||
      !studio_workspace_source.contains(QStringLiteral("selectedMaskHeader.fallbackState.length > 0"))) {
    return fail("Smart selection rough fallback masks should expose a visible editable preview state in mask status UI");
  }
  const auto snap_close_branch_index =
      studio_workspace_source.indexOf(QStringLiteral("workspaceContext.completeActivePolygonIfSnappingToFirst("));
  const auto append_after_snap_index =
      studio_workspace_source.indexOf(QStringLiteral("previewContext.appendActivePolygonPoint(point.x, point.y)"));
	  if (!studio_workspace_source.contains(QStringLiteral("function completeActivePolygonIfSnappingToFirst(normalizedX, normalizedY, canvasWidth, canvasHeight)")) ||
	      !studio_workspace_source.contains(QStringLiteral("return root.completeActivePolygonSelection()")) ||
	      snap_close_branch_index < 0 ||
	      append_after_snap_index < 0 ||
	      snap_close_branch_index > append_after_snap_index ||
	      !studio_workspace_source.contains(QStringLiteral("workspaceContext.completeActivePolygonIfSnappingToFirst(\n                        point.x, point.y, viewport.paintedContentWidth, viewport.paintedContentHeight))\n                return true"))) {
	    return fail("Polygon snap-close should complete before appendActivePolygonPoint can add a duplicate closing point");
	  }
	  if (!studio_workspace_source.contains(QStringLiteral("function lassoPathStateText(mask)")) ||
	      !studio_workspace_source.contains(QStringLiteral("return \"Raw path\"")) ||
	      !studio_workspace_source.contains(QStringLiteral("return \"Simplified path\"")) ||
	      !studio_workspace_source.contains(QStringLiteral("return \"Smoothed path\"")) ||
	      !studio_workspace_source.contains(QStringLiteral("root.lassoPathStateText(root.activeMaskData()) + \" / \"")) ||
	      !studio_workspace_source.contains(QStringLiteral("objectName: \"lassoPathStateLabel\"")) ||
	      !studio_workspace_source.contains(QStringLiteral("text: root.lassoPathStateText(root.activeMaskData())")) ||
	      !studio_workspace_source.contains(QStringLiteral("const lassoState = root.lassoPathStateText(data)")) ||
	      !studio_workspace_source.contains(QStringLiteral("Math.round(points) + \" points / \" + Math.round(rawPoints) + \" drawn\""))) {
	    return fail("Studio lasso UI should visibly distinguish raw, simplified, and smoothed lasso path states");
	  }
	  const auto normal_flow_index = mask_inspector_source.indexOf(QStringLiteral("normalMaskPreviewModeFlow"));
  const auto diagnostics_toggle_index =
      mask_inspector_source.indexOf(QStringLiteral("maskPreviewDiagnosticsToggleButton"));
  const auto diagnostic_flow_index =
      mask_inspector_source.indexOf(QStringLiteral("diagnosticMaskPreviewModeFlow"));
  const auto color_overlay_index =
      mask_inspector_source.indexOf(QStringLiteral("maskPreviewOverlayButton"));
  const auto outline_index =
      mask_inspector_source.indexOf(QStringLiteral("maskPreviewOutlineButton"));
  const auto marching_ants_index =
      mask_inspector_source.indexOf(QStringLiteral("maskPreviewMarchingAntsButton"));
  const auto hidden_index =
      mask_inspector_source.indexOf(QStringLiteral("maskPreviewNoneButton"));
  const auto color_on_bw_index =
      mask_inspector_source.indexOf(QStringLiteral("maskPreviewColorOnBwButton"));
  const auto grayscale_alpha_index =
      mask_inspector_source.indexOf(QStringLiteral("maskPreviewMatteButton"));
  const auto isolated_mask_index =
      mask_inspector_source.indexOf(QStringLiteral("maskPreviewOnBlackButton"));
  if (mask_inspector_source.isEmpty() ||
      studio_workspace_source.isEmpty() ||
      coordinate_mapper_source.isEmpty() ||
      shared_overlay_source.isEmpty() ||
      canvas_overlay_source.isEmpty() ||
      controller_source.isEmpty() ||
      controller_header_source.isEmpty() ||
      thumbnail_queue_service_header_source.isEmpty() ||
      thumbnail_queue_service_source.isEmpty() ||
      thumbnail_worker_renderer_factory_source.isEmpty() ||
      thumbnail_cache_session_source.isEmpty() ||
      color_layer_runtime_source.isEmpty() ||
      normal_flow_index < 0 ||
      diagnostics_toggle_index < 0 ||
      diagnostic_flow_index < 0 ||
      color_overlay_index < normal_flow_index ||
      color_overlay_index > diagnostics_toggle_index ||
      outline_index < normal_flow_index ||
      outline_index > diagnostics_toggle_index ||
      marching_ants_index < normal_flow_index ||
      marching_ants_index > diagnostics_toggle_index ||
      hidden_index < normal_flow_index ||
      hidden_index > diagnostics_toggle_index ||
      color_on_bw_index < diagnostic_flow_index ||
      grayscale_alpha_index < diagnostic_flow_index ||
      isolated_mask_index < diagnostic_flow_index ||
      !mask_inspector_source.contains(QStringLiteral("text: \"Color Overlay\"")) ||
      !mask_inspector_source.contains(QStringLiteral("text: \"Outline\"")) ||
      !mask_inspector_source.contains(QStringLiteral("text: \"Marching Ants\"")) ||
      !mask_inspector_source.contains(QStringLiteral("text: \"Hidden\"")) ||
      !mask_inspector_source.contains(QStringLiteral("text: \"Grayscale Alpha\"")) ||
      !mask_inspector_source.contains(QStringLiteral("text: \"Isolated Mask\"")) ||
      !mask_inspector_source.contains(QStringLiteral("maskPreviewOverlayOpacitySlider")) ||
      !mask_inspector_source.contains(QStringLiteral("maskPreviewOverlayColorSwatch")) ||
      !mask_inspector_source.contains(QStringLiteral("maskPreviewOverlayColorButton")) ||
      !mask_inspector_source.contains(QStringLiteral("maskPreviewOverlayColorResetButton")) ||
      !mask_inspector_source.contains(QStringLiteral("rubylith-overlay-preset:v1|display-mode:overlay|color:#ff2f5f|single-color:true|separate-from-diagnostics:true")) ||
      !mask_inspector_source.contains(QStringLiteral("maskPreviewRubylithOverlayPresetButton")) ||
      !mask_inspector_source.contains(QStringLiteral("editorPreview.maskPreviewOverlayColor = \"#ff2f5f\"")) ||
      !mask_inspector_source.contains(QStringLiteral("maskPreviewShowActiveOnlyButton")) ||
      !mask_inspector_source.contains(QStringLiteral("editorPreview.maskPreviewShowAll = false")) ||
      !mask_inspector_source.contains(QStringLiteral("maskPreviewShowAllButton")) ||
      !mask_inspector_source.contains(QStringLiteral("editorPreview.maskPreviewShowAll = !editorPreview.maskPreviewShowAll")) ||
      !mask_inspector_source.contains(QStringLiteral("maskPreviewShowInvertedButton")) ||
      !mask_inspector_source.contains(QStringLiteral("editorPreview.activeMaskSelectionInverted = !editorPreview.activeMaskSelectionInverted")) ||
      !mask_inspector_source.contains(QStringLiteral("text: root.diagnosticMaskPreviewModesVisible ? \"Hide Matte Diagnostics\" : \"Matte Diagnostics\"")) ||
      !mask_inspector_source.contains(QStringLiteral("visible: editRoot.developerDiagnosticsEnabled === true")) ||
      !mask_inspector_source.contains(QStringLiteral("enabled: editorPreview.hasAsset && editRoot.developerDiagnosticsEnabled === true")) ||
      !mask_inspector_source.contains(QStringLiteral("developer-diagnostics-menu:v2|developer-mode-gated:true|normal-mask-modes:separate|matte-debug:collapsed|runtime-detail:collapsed|raw-alpha:diagnostic-only|confidence:false-color-focus:diagnostic-only|default-visible:false")) ||
      !mask_inspector_source.contains(QStringLiteral("readonly property string diagnosticsMenuContract: root.developerDiagnosticsMenuContract")) ||
      !mask_inspector_source.contains(QStringLiteral("mask-layer-parameter-terminology:v1|layer-opacity:composite-layer-result|mask-density:alpha-strength-before-gating|mask-opacity:mask-source-contribution|brush-flow:per-stamp-paint-rate|brush-opacity:pressure-or-source-opacity|overlay-opacity:visualization-only")) ||
      !mask_inspector_source.contains(QStringLiteral("adjustment-control-target-support:v1|targets:base-image,adjustment-layer,photo-layer,mask|default:base-image+adjustment-layer|mask-controls:mask-only|tool-controls:mask-source-only")) ||
      !mask_inspector_source.contains(QStringLiteral("function maskControlTargetSupportSignature(")) ||
      !mask_inspector_source.contains(QStringLiteral("readonly property string targetSupportContract: root.adjustmentControlTargetSupportContract")) ||
      !mask_inspector_source.contains(QStringLiteral("readonly property string targetSupport: \"mask\"")) ||
      !mask_inspector_source.contains(QStringLiteral("readonly property string targetSupportSignature: root.maskControlTargetSupportSignature(targetSupport)")) ||
      !mask_inspector_source.contains(QStringLiteral("label: \"MASK OPACITY\"")) ||
      !mask_inspector_source.contains(QStringLiteral("label: \"MASK DENSITY\"")) ||
      !mask_inspector_source.contains(QStringLiteral("readonly property string parameterRole: \"overlay-opacity\"")) ||
      !mask_inspector_source.contains(QStringLiteral("readonly property string parameterRole: \"mask-opacity\"")) ||
      !mask_inspector_source.contains(QStringLiteral("readonly property string parameterRole: \"mask-density\"")) ||
      !mask_inspector_source.contains(QStringLiteral("readonly property string parameterRole: \"brush-flow\"")) ||
      !mask_inspector_source.contains(QStringLiteral("readonly property string parameterRole: \"brush-opacity\"")) ||
      !mask_view_source.contains(QStringLiteral("mask-layer-parameter-terminology:v1|layer-opacity:composite-layer-result|mask-density:alpha-strength-before-gating|mask-opacity:mask-source-contribution|brush-flow:per-stamp-paint-rate|brush-opacity:pressure-or-source-opacity|overlay-opacity:visualization-only")) ||
      !mask_view_source.contains(QStringLiteral("readonly property string parameterRole: \"mask-opacity\"")) ||
      !mask_view_source.contains(QStringLiteral("readonly property string parameterRole: \"mask-density\"")) ||
      !mask_view_source.contains(QStringLiteral("readonly property string parameterRole: \"brush-flow\"")) ||
      !studio_workspace_source.contains(QStringLiteral("editorPreview.maskPreviewMode = \"marching-ants\"")) ||
      !studio_workspace_source.contains(QStringLiteral("function photoLayerStateBadges(")) ||
      !studio_workspace_source.contains(QStringLiteral("function photoLayerStateBadgeSignature(")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerStateBadgeRow\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerStateBadge\"")) ||
      !studio_workspace_source.contains(QStringLiteral("layer-state-badges:v1|global:base-row")) ||
      !studio_workspace_source.contains(QStringLiteral("accessibleDescription: baseLayerSummary + \" / Global edits target / Locked bottom layer / \" + baseLayerMetadataSummary")) ||
      !studio_workspace_source.contains(QStringLiteral("mask-layer-parameter-terminology:v1|layer-opacity:composite-layer-result|mask-density:alpha-strength-before-gating|mask-opacity:mask-source-contribution|brush-flow:per-stamp-paint-rate|brush-opacity:pressure-or-source-opacity|overlay-opacity:visualization-only")) ||
      !studio_workspace_source.contains(QStringLiteral("adjustment-control-target-support:v1|targets:base-image,adjustment-layer,photo-layer,mask|default:base-image+adjustment-layer|mask-controls:mask-only|tool-controls:mask-source-only")) ||
      !studio_workspace_source.contains(QStringLiteral("target-aware-adjustment-command-router:v3|commands:numeric,string,boolean,preset,reset,curve")) ||
      !studio_workspace_source.contains(QStringLiteral("target-routing-matrix:v1|families:raw,white-balance,light,color,color-grading,tone-curve,presence,detail,lens-transform,crop,preset")) ||
      !studio_workspace_source.contains(QStringLiteral("function normalizedAdjustmentControlTargets(")) ||
      !studio_workspace_source.contains(QStringLiteral("function adjustmentControlTargetSupportSignature(")) ||
      !studio_workspace_source.contains(QStringLiteral("function adjustmentControlSupportsActiveTarget(")) ||
      !studio_workspace_source.contains(QStringLiteral("function adjustmentControlCanWriteActiveTarget(")) ||
      !studio_workspace_source.contains(QStringLiteral("function targetRoutingMatrixSignature(")) ||
      !studio_workspace_source.contains(QStringLiteral("function targetAwareAdjustmentCommandSignature(")) ||
      !studio_workspace_source.contains(QStringLiteral("function routePrimaryAdjustmentValueCommand(")) ||
      !studio_workspace_source.contains(QStringLiteral("function routePrimaryAdjustmentStringCommand(")) ||
      !studio_workspace_source.contains(QStringLiteral("function routePrimaryAdjustmentBoolCommand(")) ||
      !studio_workspace_source.contains(QStringLiteral("function routePrimaryAdjustmentResetCommand(")) ||
      !studio_workspace_source.contains(QStringLiteral("function routePrimaryPresetCommand(")) ||
      !studio_workspace_source.contains(QStringLiteral("function layerLocalPresetApplicationSignature(")) ||
      !studio_workspace_source.contains(QStringLiteral("return root.routePrimaryAdjustmentValueCommand(adjustment, value, \"base-image,adjustment-layer\")")) ||
      !studio_workspace_source.contains(QStringLiteral("return root.routePrimaryAdjustmentResetCommand(groupName, \"base-image,adjustment-layer\")")) ||
      !studio_workspace_source.contains(QStringLiteral("return root.routePrimaryPresetCommand(name, \"base-image,adjustment-layer\")")) ||
      !studio_workspace_source.contains(QStringLiteral("layer-local-preset-apply:v1")) ||
      !studio_workspace_source.contains(QStringLiteral("uses-applyPresetToPhotoLayer:true")) ||
      !studio_workspace_source.contains(QStringLiteral("uses-applyPresetWithIntensity-only-for-base:true")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string layerLocalPresetApplyContract: root.layerLocalPresetApplicationSignature()")) ||
      !studio_workspace_source.contains(QStringLiteral("property string targetSupport: \"base-image,adjustment-layer\"")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property bool supportsActiveEditTarget: workspace.adjustmentControlCanWriteActiveTarget(targetSupport)")) ||
      !studio_workspace_source.contains(QStringLiteral("targetSupport: \"mask\"")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string targetSupport: \"adjustment-layer,photo-layer\"")) ||
      !studio_workspace_source.contains(QStringLiteral("label: \"Mask Density\"")) ||
      !studio_workspace_source.contains(QStringLiteral("label: \"Mask Opacity\"")) ||
      !studio_workspace_source.contains(QStringLiteral("label: \"Brush Flow\"")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string parameterRole: \"layer-opacity\"")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string parameterRole: \"mask-density\"")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string parameterRole: \"mask-opacity\"")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string parameterRole: \"brush-flow\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerMaskOpacitySlider\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerMaskDensitySlider\"")) ||
      !studio_workspace_source.contains(QStringLiteral("editorPreview.setPhotoLayerMaskOpacity(layerIndex, value / 100.0)")) ||
      !studio_workspace_source.contains(QStringLiteral("editorPreview.setPhotoLayerMaskDensity(layerIndex, value / 100.0)")) ||
      !studio_workspace_source.contains(QStringLiteral("function layerKeyboardNavigationSignature(")) ||
      !studio_workspace_source.contains(QStringLiteral("layer-keyboard-navigation:v3|arrows:nudge-in-layer-tools|up-down:select-otherwise|enter:edit|f2:rename|space:visibility|shift:range-select|modifier-up-down:reorder|escape:base-focus")) ||
      !studio_workspace_source.contains(QStringLiteral("function selectedPhotoLayerBatchTargets(")) ||
      !studio_workspace_source.contains(QStringLiteral("function selectedPhotoLayerBatchMenuItems(")) ||
      !studio_workspace_source.contains(QStringLiteral("function handleSelectedPhotoLayerBatchAction(")) ||
      !studio_workspace_source.contains(QStringLiteral("selected-layer-batch-actions:v1|visibility:true|lock:true|delete:true|duplicate:true|opacity:true|blend:true|paste-adjustments:true|paste-global:true")) ||
      !studio_workspace_source.contains(QStringLiteral("function layerDuplicatePolicySignature(")) ||
      !studio_workspace_source.contains(QStringLiteral("layer-duplicate-policy:v1|with-masks:duplicatePhotoLayerWithMasks|without-masks:duplicatePhotoLayerWithoutMasks|locked:blocked|batch:both")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string layerDuplicatePolicyContract: root.layerDuplicatePolicySignature()")) ||
      !studio_workspace_source.contains(QStringLiteral("layers-only-focus-mode:v1|tab:layers|global-panels:hidden|layer-stack:true|mask-stack:true|layer-local-controls:true|restore:previous-inspector-tab|target:layer-or-mask")) ||
      !studio_workspace_source.contains(QStringLiteral("function layersOnlyFocusModeSignature(")) ||
      !studio_workspace_source.contains(QStringLiteral("function setLayersOnlyFocusMode(")) ||
      !studio_workspace_source.contains(QStringLiteral("function toggleLayersOnlyFocusMode(")) ||
      !studio_workspace_source.contains(QStringLiteral("key: \"layers-focus\"")) ||
      !studio_workspace_source.contains(QStringLiteral("label: \"Layers Focus\"")) ||
      !studio_workspace_source.contains(QStringLiteral("root.toggleLayersOnlyFocusMode()")) ||
      !studio_workspace_source.contains(QStringLiteral("root.editInspectorTab = \"layers\"")) ||
      !studio_workspace_source.contains(QStringLiteral("key: \"duplicate-with-masks\"")) ||
      !studio_workspace_source.contains(QStringLiteral("key: \"duplicate-without-masks\"")) ||
      !studio_workspace_source.contains(QStringLiteral("key: \"duplicate-selected-without-masks\"")) ||
      !studio_workspace_source.contains(QStringLiteral("editorPreview.duplicatePhotoLayerWithMasks(index)")) ||
      !studio_workspace_source.contains(QStringLiteral("editorPreview.duplicatePhotoLayerWithoutMasks(index)")) ||
      !studio_workspace_source.contains(QStringLiteral("function photoLayerBlendModesForLayer(")) ||
      !studio_workspace_source.contains(QStringLiteral("function photoLayerBlendModeSupportSignature(")) ||
      !studio_workspace_source.contains(QStringLiteral("photo-layer-blend-mode-support:v1")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string blendModeSupportContract: root.photoLayerBlendModeSupportSignature(layerData)")) ||
      !studio_workspace_source.contains(QStringLiteral("enabled: !layerLocked && root.photoLayerBlendModeControlsEnabled(layerData)")) ||
      !studio_workspace_source.contains(QStringLiteral("model: root.photoLayerBlendLabelsForLayer(layerData)")) ||
      !studio_workspace_source.contains(QStringLiteral("root.photoLayerBlendModeAtForLayer(layerData, index)")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"selectedPhotoLayerBatchActionsButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("root.handleSelectedPhotoLayerBatchAction(")) ||
      !studio_workspace_source.contains(QStringLiteral("function handlePhotoLayerRowKey(event, layerIndex, layer)")) ||
      !studio_workspace_source.contains(QStringLiteral("root.handlePhotoLayerRowKey(event, layerIndex, layerData)")) ||
      !studio_workspace_source.contains(QStringLiteral("Keys.priority: Keys.BeforeItem")) ||
      !layer_row_source.contains(QStringLiteral("thumbnailDprContract: \"layer-row-thumbnail-dpr:v1|source-size:screen-aware|base-image:true|display:\"")) ||
      !layer_row_source.contains(QStringLiteral("readonly property real thumbnailDevicePixelRatio: Math.max(1.0, Number(Screen.devicePixelRatio || 1.0))")) ||
      !layer_row_source.contains(QStringLiteral("sourceSize.width: root.screenAwareThumbnailSourceSize")) ||
      !layer_row_source.contains(QStringLiteral("sourceSize.height: root.screenAwareThumbnailSourceSize")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string baseLayerThumbnailDprContract: thumbnailDprContract")) ||
      !studio_workspace_source.contains(QStringLiteral("thumbnailSourceSize: root.thumbnailSourceSizeForDisplay(thumbnailZoneSize, 96)")) ||
      !studio_workspace_source.contains(QStringLiteral("layer-hierarchy-drop-targets:v2|before:")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerDropTargetBefore\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerDropTargetAfter\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerHierarchyDropHighlight\"")) ||
      !studio_workspace_source.contains(QStringLiteral("function photoLayerGroupDropTargetAtPointer(sourceIndex, pointerItem, pointerX, pointerY, rowsRepeater)")) ||
      !studio_workspace_source.contains(QStringLiteral("function finishPhotoLayerDrag(sourceIndex, sourceRow, pointerItem, pointerX, pointerY, dragProxy, rowsRepeater)")) ||
      !studio_workspace_source.contains(QStringLiteral("function movePhotoLayerPreservingSelection(")) ||
      !studio_workspace_source.contains(QStringLiteral("onClicked: root.movePhotoLayerPreservingSelection(layerIndex, -1)")) ||
      !studio_workspace_source.contains(QStringLiteral("onClicked: root.movePhotoLayerPreservingSelection(layerIndex, 1)")) ||
      !studio_workspace_source.contains(QStringLiteral("property bool developerDiagnosticsEnabled: false")) ||
      !studio_workspace_source.contains(QStringLiteral("developer-diagnostics-menu:v3|image-inspection:user-accessible|advanced-details:developer-mode-gated|canvas-overlays:collapsed|focus:false-color:gamut:zone:diagnostic-only|hidden-during-mask-work:true|hidden-during-primary-hud:true|default-visible:false")) ||
      !studio_workspace_source.contains(QStringLiteral("tooltip: \"Inspect clipping, focus, gamut, and tonal zones.\"")) ||
      !studio_workspace_source.contains(QStringLiteral("visible: root.diagnosticsPanelOpen && !root.maskWorkActive && !editViewport.primaryCanvasHudActive")) ||
      !studio_workspace_source.contains(QStringLiteral("CanvasCoordinateMapper")) ||
      !studio_workspace_source.contains(QStringLiteral("viewportCoordinateMapper.viewToImagePoint(x, y)")) ||
      !coordinate_mapper_source.contains(QStringLiteral("coordinate-transform:v1|view-to-image:true|image-to-view:true|crop:true|rotation:true|zoom-pan:true|painted-image-rect:true|fit-fill:true|device-pixel-ratio:true|roundtrip:true|clamp:true")) ||
      !coordinate_mapper_source.contains(QStringLiteral("function imageToViewPoint(")) ||
      !coordinate_mapper_source.contains(QStringLiteral("function viewToImagePoint(")) ||
      !coordinate_mapper_source.contains(QStringLiteral("function imageToViewRect(")) ||
      !coordinate_mapper_source.contains(QStringLiteral("function viewToImageRect(")) ||
      !coordinate_mapper_source.contains(QStringLiteral("function devicePixelRectForViewRect(")) ||
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
      !controller_source.contains(QStringLiteral("duplicatePhotoLayerWithMasks")) ||
      !controller_source.contains(QStringLiteral("duplicatePhotoLayerWithoutMasks")) ||
      !controller_source.contains(QStringLiteral("duplicateMaskPolicy")) ||
      !controller_source.contains(QStringLiteral("clear_photo_layer_mask_links")) ||
      !controller_source.contains(QStringLiteral("photo_layer_blend_mode_supported")) ||
      !controller_source.contains(QStringLiteral("kind == QStringLiteral(\"group\")")) ||
      !controller_source.contains(QStringLiteral("mode == QStringLiteral(\"soft-light\")")) ||
      !controller_source.contains(QStringLiteral("setPhotoLayerMaskOpacity")) ||
      !controller_source.contains(QStringLiteral("setPhotoLayerMaskDensity")) ||
      !controller_source.contains(QStringLiteral("Layer mask opacity change")) ||
      !controller_source.contains(QStringLiteral("Layer mask density change")) ||
      !controller_source.contains(QStringLiteral("active_mask_opacity")) ||
      !controller_source.contains(QStringLiteral("active_mask_density")) ||
      !controller_source.contains(QStringLiteral("active_mask_brush_flow")) ||
      !controller_source.contains(QStringLiteral("mask_preview_overlay_opacity_")) ||
      !color_layer_runtime_source.contains(QStringLiteral("layer.mask_density")) ||
      !color_layer_runtime_source.contains(QStringLiteral("layer.mask_opacity")) ||
      !color_layer_runtime_source.contains(QStringLiteral("photo-layer-effect-detail-order:v1|layer-recipe:tone->curves->color->effects-detail|effects-detail:before-mask-opacity-blend|mask-opacity-density:alpha-only|global-spatial-detail:after-layer-composite")) ||
      !color_layer_runtime_source.contains(QStringLiteral("has_photo_layer_effect_detail_adjustments")) ||
      !color_layer_runtime_source.contains(QStringLiteral("has_photo_layer_spatial_effect_detail_adjustments")) ||
      !color_layer_runtime_source.contains(QStringLiteral("PhotoLayerEffectDetailContext")) ||
      !color_layer_runtime_source.contains(QStringLiteral("guided_luminance_blur_into(")) ||
      !color_layer_runtime_source.contains(QStringLiteral("apply_photo_layer_effect_detail_adjustments")) ||
      !controller_source.contains(QStringLiteral("createAdjustmentLayerFromCurrentGlobalEdits")) ||
      !controller_source.contains(QStringLiteral("bakePhotoLayerAdjustmentsToGlobal")) ||
      !controller_source.contains(QStringLiteral("edit_state_with_photo_layer_adjustment_recipe")) ||
      !controller_source.contains(QStringLiteral("Bake adjustment layer edits to Base Image")) ||
      !photo_layer_adjustment_recipe_source.contains(QStringLiteral("LayerAdjustmentRecipe:v1|mirrors-global-edit-recipe:true|flat-fields:compat|nested:adjustments.colorMixer,colorGrading,lut|copy-paste-sync-bake:true|future-engine-model:true")) ||
      !photo_layer_adjustment_recipe_source.contains(QStringLiteral("layer-adjustment-recipe-compat:v1|read:legacy-flat-fields+adjustments-map|write:legacy-flat-fields+adjustments-map|preserve-existing-projects:true|migration-target:LayerAdjustmentRecipe")) ||
      !photo_layer_adjustment_recipe_source.contains(QStringLiteral("layer_adjustment_recipe_schema_contract()")) ||
      !photo_layer_adjustment_recipe_source.contains(QStringLiteral("layer_adjustment_recipe_compatibility_contract()")) ||
      !photo_layer_adjustment_recipe_source.contains(QStringLiteral("photo_layer_adjustment_recipe_keys")) ||
      !controller_source.contains(QStringLiteral("replace_photo_layer_adjustment_recipe")) ||
      !controller_source.contains(QStringLiteral("{QStringLiteral(\"commit_deferred\"), true}")) ||
      !controller_source.contains(QStringLiteral("{QStringLiteral(\"async_worker\"), true}")) ||
      !controller_source.contains(QStringLiteral("{QStringLiteral(\"raster_worker_thread\"), result.worker_thread}")) ||
      !controller_source.contains(QStringLiteral("thumbnail_cache_session_.maskDirty()")) ||
      !thumbnail_cache_session_source.contains(QStringLiteral("class ThumbnailCacheSession final")) ||
      !thumbnail_presentation_service_source.contains(
          QStringLiteral("mask_thumbnail_refresh_deferred")) ||
      !controller_source.contains(QStringLiteral("emit controlsChanged();")) ||
      !controller_source.contains(QStringLiteral("runQueuedThumbnailRefresh")) ||
      !thumbnail_queue_service_header_source.contains(QStringLiteral("layerThumbnailServiceOwnershipContract")) ||
      !thumbnail_queue_service_header_source.contains(QStringLiteral("layer_thumbnail_service_ownership_contract()")) ||
      !thumbnail_worker_renderer_factory_source.contains(QStringLiteral("thumbnailColorManaged")) ||
      !thumbnail_worker_renderer_factory_source.contains(QStringLiteral("layer-thumbnail-active-color-management:v2|adjustment:shared-preview-render|render-mode:layer_thumbnail|fallback:fail-closed")) ||
      !thumbnail_queue_service_header_source.contains(QStringLiteral("layer-thumbnail-service:v1|requests:ThumbnailRefreshQueueRequest|cancellation:atomic|cache:thumbnail-entry-cache|status:thumbnailRefreshQueueFields|invalidation:thumbnail_invalidation_key|independent-main-preview:true")) ||
      !thumbnail_queue_service_header_source.contains(QStringLiteral("struct ThumbnailRefreshQueueRequest final")) ||
      !thumbnail_queue_service_header_source.contains(QStringLiteral("std::shared_ptr<std::atomic_bool> cancellation")) ||
      !thumbnail_queue_service_header_source.contains(QStringLiteral("QString thumbnail_invalidation_key(")) ||
      !thumbnail_queue_service_header_source.contains(QStringLiteral("QString thumbnail_entry_cache_key(")) ||
      !thumbnail_queue_service_source.contains(QStringLiteral("thumbnailColorManaged")) ||
      !thumbnail_queue_service_source.contains(QStringLiteral("layer-thumbnail-active-color-management:v2|adjustment:shared-preview-render|adjustment-fallback:fail-closed|non-adjustment:raster-explicit")) ||
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
      !controller_source.contains(QStringLiteral("preview_session_.advanceGeneration()")) ||
      !controller_source.contains(QStringLiteral("observability_active_target")) ||
      !controller_source.contains(QStringLiteral("observability_active_layer_id")) ||
      !controller_source.contains(QStringLiteral("observability_active_mask_id")) ||
      !controller_source.contains(QStringLiteral("observability_active_mask_set_id")) ||
      !controller_source.contains(QStringLiteral("cancelled_render_count")) ||
      !controller_source.contains(QStringLiteral("stale_preview_fallback")) ||
      !controller_source.contains(QStringLiteral("render_failure_status_for_stage")) ||
      !controller_source.contains(QStringLiteral("Preview failed: %1.")) ||
      !thumbnail_queue_service_source.contains(QStringLiteral("thumbnail_id")) ||
      !controller_header_source.contains(QStringLiteral("QString active_target")) ||
      !controller_header_source.contains(QStringLiteral("QString active_layer_id")) ||
      !controller_header_source.contains(QStringLiteral("QString active_mask_id")) ||
      !controller_header_source.contains(QStringLiteral("QString active_mask_set_id")) ||
      !controller_header_source.contains(QStringLiteral("Q_INVOKABLE bool duplicatePhotoLayerWithMasks(int index);")) ||
      !controller_header_source.contains(QStringLiteral("Q_INVOKABLE bool duplicatePhotoLayerWithoutMasks(int index);")) ||
      !controller_header_source.contains(QStringLiteral("Q_INVOKABLE bool setPhotoLayerMaskOpacity(int index, double opacity);")) ||
      !controller_header_source.contains(QStringLiteral("Q_INVOKABLE bool setPhotoLayerMaskDensity(int index, double density);")) ||
      !controller_header_source.contains(QStringLiteral("Q_INVOKABLE bool createAdjustmentLayerFromCurrentGlobalEdits(const QString& name = {});")) ||
      !controller_header_source.contains(QStringLiteral("Q_INVOKABLE bool bakePhotoLayerAdjustmentsToGlobal(int index);")) ||
      !controller_header_source.contains(QStringLiteral("int cancelled_render_count")) ||
      !controller_header_source.contains(QStringLiteral("QString stale_preview_fallback")) ||
	      !controller_source.contains(QStringLiteral("{QStringLiteral(\"frame_budget_ms\"), kFrameCoalescedPreviewRenderBudgetMs}")) ||
	      !controller_source.contains(QStringLiteral("preview_viewport_patch_max_edge_ > 0")) ||
	      !controller_source.contains(QStringLiteral("? preview_viewport_patch_max_edge_")) ||
	      !edit_render_policy_source.contains(QStringLiteral("kMinimumInteractivePreviewEdge = 512")) ||
	      !edit_render_policy_source.contains(QStringLiteral("kDefaultInteractivePreviewEdge = 2560")) ||
	      !controller_source.contains(QStringLiteral("constexpr int kAssetSelectionPreviewEdge = 2048")) ||
	      !controller_source.contains(QStringLiteral("constexpr int kExpensiveInteractivePreviewEdge = 2560")) ||
	      !controller_source.contains(QStringLiteral("constexpr int kDraftInteractivePreviewEdge = 1600")) ||
	      !studio_workspace_source.contains(QStringLiteral("selection-window-performance:v1|rail:labels-first|activation:panel-first|quality-boost:deferred|settled-floor:3072|interactive-feedback:immediate")) ||
	      !studio_workspace_source.contains(QStringLiteral("const settledMaskQuality = workspaceContext.maskWorkActive && workspaceContext.maskPreviewQualityBoostReady")) ||
	      !studio_workspace_source.contains(QStringLiteral("previewContext.setPreviewPresentationMetrics(")) ||
	      studio_workspace_source.contains(QStringLiteral("const supersample = settledMaskQuality ? 2.5 : 2.0")) ||
	      studio_workspace_source.contains(QStringLiteral("const qualityFloor = settledMaskQuality ? 3072 : 2560")) ||
	      !edit_render_policy_source.contains(QStringLiteral("metrics.settled_mask_quality ? 2.5 : 2.0")) ||
	      !edit_render_policy_source.contains(QStringLiteral("metrics.settled_mask_quality ? 3072.0 : 2560.0")) ||
	      controller_source.contains(QStringLiteral("std::max(\n          interactive_preview_edge_for_state(currentState(), previewTargetEdge()),\n          kExpensiveInteractivePreviewEdge)")) ||
      !controller_source.contains(QStringLiteral("updateActiveBrushLivePreview(sampled_points);\n    emit activeBrushStrokeChanged();\n    return;")) ||
      !controller_source.contains(QStringLiteral("schedulePendingBrushCommit(std::move(commit));")) ||
      !controller_source.contains(QStringLiteral("normalized-image-region|dependent-stage-propagation:v1")) ||
      !controller_source.contains(QStringLiteral("dirty_region_valid")) ||
      !controller_source.contains(QStringLiteral("dirty_region_area")) ||
      !controller_source.contains(QStringLiteral("last_render_graph_resolution_.dirty_stages")) ||
      !controller_source.contains(QStringLiteral("QStringLiteral(\"layers_composite\")")) ||
      !controller_source.contains(QStringLiteral("QStringLiteral(\"overlay_composite\")")) ||
      !controller_source.contains(QStringLiteral("QStringLiteral(\"encode_disk\")")) ||
      !controller_source.contains(QStringLiteral("QStringLiteral(\"histogram_scope\")")) ||
      !controller_source.contains(QStringLiteral("dirty_region_for_brush_points(sampled_points)")) ||
      !controller_source.contains(QStringLiteral("submitMaskRasterizationEditIntent(\n        QStringLiteral(\"brush_stroke\")")) ||
      !controller_header_source.contains(QStringLiteral("Q_PROPERTY(bool scopesVisible READ scopesVisible WRITE setScopesVisible")) ||
      !controller_source.contains(QStringLiteral("mask-scope-visible-and-settled:v1")) ||
      !controller_source.contains(QStringLiteral("preview.histogram.skipped")) ||
      !controller_source.contains(QStringLiteral("preview.thumbnail.contained_failure")) ||
      !controller_source.contains(QStringLiteral("mask_scope_dirty")) ||
      !controller_source.contains(QStringLiteral("settled_for_histogram")) ||
      !controller_source.contains(QStringLiteral("(!mask_scope_dirty || result.full_resolution || scopesVisible())")) ||
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
  if (!wait_for_preview_idle(commit_controller, 4000)) {
    return fail("deferred brush stroke preview should settle before validating refreshed thumbnails");
  }
  const auto brush_commit_telemetry =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_mask_work_telemetry(
          commit_controller);
  if (brush_commit_telemetry.value(QStringLiteral("operation")).toString() !=
          QStringLiteral("brush_rasterization") ||
      brush_commit_telemetry.value(QStringLiteral("cursor_budget_ms")).toLongLong() != 16 ||
      brush_commit_telemetry.value(QStringLiteral("live_stroke_budget_ms")).toLongLong() != 33 ||
      brush_commit_telemetry.value(QStringLiteral("preview_matte_budget_ms")).toLongLong() != 100 ||
      brush_commit_telemetry.value(QStringLiteral("brush_point_count")).toInt() < 2 ||
      brush_commit_telemetry.value(QStringLiteral("cursor_elapsed_ms")).toLongLong() < 0 ||
      brush_commit_telemetry.value(QStringLiteral("live_stroke_elapsed_ms")).toLongLong() < 0 ||
      brush_commit_telemetry.value(QStringLiteral("preview_matte_elapsed_ms")).toLongLong() < 0 ||
      brush_commit_telemetry.value(QStringLiteral("elapsed_ms")).toLongLong() < 0 ||
      brush_commit_telemetry.value(QStringLiteral("raster_target_width")).toInt() <= 0 ||
      brush_commit_telemetry.value(QStringLiteral("raster_target_height")).toInt() <= 0 ||
      brush_commit_telemetry.value(QStringLiteral("dirty_region_pixels")).toDouble() <= 0.0 ||
      brush_commit_telemetry.value(QStringLiteral("cache_bytes")).toLongLong() < 0 ||
      !brush_commit_telemetry.value(QStringLiteral("cursor_within_latency_budget")).toBool() ||
      !brush_commit_telemetry.value(QStringLiteral("live_stroke_within_latency_budget")).toBool() ||
      !brush_commit_telemetry.value(QStringLiteral("preview_matte_within_latency_budget")).toBool()) {
    return fail("brush commit telemetry should meet cursor, live-stroke, and preview-matte latency gates for small strokes");
  }
  auto thumbnail_sets_after_commit = commit_controller.maskSets();
  QElapsedTimer thumbnail_commit_timer;
  thumbnail_commit_timer.start();
  while (thumbnail_commit_timer.elapsed() < 6000 &&
         (thumbnail_sets_after_commit.isEmpty() ||
          thumbnail_sets_after_commit.constFirst()
                  .toMap()
                  .value(QStringLiteral("async_job_kind"))
                  .toString() != QStringLiteral("mask_thumbnail"))) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QThread::msleep(25);
    thumbnail_sets_after_commit = commit_controller.maskSets();
  }
  const auto thumbnail_after_commit = thumbnail_sets_after_commit.isEmpty()
      ? QVariantMap()
      : thumbnail_sets_after_commit.constFirst().toMap();
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
  auto large_selection_mask =
      layer_coalesce_controller
          .maskStack()
          .at(layer_coalesce_controller.activeMaskIndex())
          .toMap();
  constexpr auto large_selection_edge = 2048;
  large_selection_mask.insert(QStringLiteral("kind"), QStringLiteral("object"));
  large_selection_mask.insert(
      QStringLiteral("segmentationData"),
      QByteArray(large_selection_edge * large_selection_edge, char(0xff)));
  large_selection_mask.insert(QStringLiteral("segmentationWidth"), large_selection_edge);
  large_selection_mask.insert(QStringLiteral("segmentationHeight"), large_selection_edge);
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::update_active_mask_definition(
          layer_coalesce_controller,
          large_selection_mask) ||
      !wait_for_preview_idle(layer_coalesce_controller, 5000)) {
    return fail("large selection latency fixture should install and settle before layer adjustment");
  }
  layer_coalesce_controller.beginCoalescedEdit();
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
  QElapsedTimer layer_binding_timer;
  layer_binding_timer.start();
  for (auto index = 0; index < 8; ++index) {
    (void)layer_coalesce_controller.photoLayers();
    (void)layer_coalesce_controller.maskSets();
  }
  if (layer_binding_timer.elapsed() > 50) {
    return fail("layer and mask bindings must not hash or rebuild large selection thumbnails on the slider interaction path");
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
  layer_coalesce_controller.endCoalescedEdit();
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_requested(
          layer_coalesce_controller)) {
    return fail("a completed layer-local adjustment should queue a max-preview restoration after immediate feedback");
  }
  QElapsedTimer layer_request_timer;
  layer_request_timer.start();
  const auto layer_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::current_render_request(
          layer_coalesce_controller,
          false);
  if (layer_request_timer.elapsed() > 100) {
    std::cerr << "large-mask request diagnostics: elapsed="
              << layer_request_timer.elapsed() << " ms\n";
    return fail("large-mask layer render request construction must stay under the 100 ms visible-feedback budget");
  }
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

  const auto trace = settled_range_outline_trace(controller);
  const auto outline_paths = controller.activeMaskOutlinePaths();
  if (outline_paths.isEmpty() ||
      !outline_points_are_image_normalized(outline_paths) ||
      computed_outline_source(trace) !=
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

  photo_editor::desktop::EditorPreviewController async_outline_controller;
  async_outline_controller.loadAsset(
      QStringLiteral("outline-async-full-resolution-proof"),
      full_resolution_source_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  async_outline_controller.setMaskPreviewMode(QStringLiteral("overlay"));
  async_outline_controller.setMaskFullResolutionPreview(true);
  async_outline_controller.startSelectionBrush(false);
  async_outline_controller.setActiveMaskBrushSize(0.18);
  async_outline_controller.beginActiveBrushStroke();
  async_outline_controller.addBrushPoint(0.38, 0.46);
  async_outline_controller.addBrushPoint(0.58, 0.62);
  async_outline_controller.endActiveBrushStroke();
  if (!wait_for_brush_commit(async_outline_controller)) {
    return fail("async outline fixture should commit brush replay data before tracing");
  }
  auto async_outline_mask =
      async_outline_controller.maskStack().at(async_outline_controller.activeMaskIndex()).toMap();
  async_outline_mask.insert(QStringLiteral("segmentationData"), QByteArray(2 * 2, char(0xff)));
  async_outline_mask.insert(QStringLiteral("segmentationWidth"), 2);
  async_outline_mask.insert(QStringLiteral("segmentationHeight"), 2);
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::update_active_mask_definition(
          async_outline_controller,
          async_outline_mask)) {
    return fail("async outline fixture should install stale low-resolution matte data");
  }
  if (!wait_for_preview_idle(async_outline_controller, 5000)) {
    return fail("async outline fixture should settle unrelated preview rendering before timing outline admission");
  }
  const auto async_outline_completed_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          active_mask_outline_worker_completed_count(async_outline_controller);
  QElapsedTimer async_outline_schedule_timer;
  async_outline_schedule_timer.start();
  const auto temporary_async_outline = async_outline_controller.activeMaskOutlinePaths();
  const auto async_outline_schedule_elapsed_ms = async_outline_schedule_timer.elapsed();
  const auto async_outline_pending_trace =
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_outline_trace_fields(
          async_outline_controller);
  if (async_outline_schedule_elapsed_ms > 33 ||
      temporary_async_outline.isEmpty() ||
      async_outline_pending_trace.value(QStringLiteral("outline_trace_source")).toString() !=
          QStringLiteral("worker-pending") ||
      !async_outline_pending_trace.value(QStringLiteral("outline_worker_pending")).toBool() ||
      async_outline_pending_trace.value(QStringLiteral("outline_execution_mode")).toString() !=
          QStringLiteral("async_worker") ||
      !wait_for_active_mask_outline_worker(
          async_outline_controller,
          async_outline_completed_before + 1)) {
    const auto detail =
        QStringLiteral(
            "full-resolution outline should return temporary feedback within 33 ms and finish asynchronously "
            "(elapsed=%1 paths=%2 source=%3 pending=%4 mode=%5 running=%6 completed=%7 expected-completed=%8 "
            "mask-ms=%9 cache-ms=%10 masks-ms=%11 size-ms=%12 launch-ms=%13 availability-ms=%14 fallback-ms=%15)")
            .arg(async_outline_schedule_elapsed_ms)
            .arg(temporary_async_outline.size())
            .arg(async_outline_pending_trace.value(QStringLiteral("outline_trace_source")).toString())
            .arg(async_outline_pending_trace.value(QStringLiteral("outline_worker_pending")).toBool())
            .arg(async_outline_pending_trace.value(QStringLiteral("outline_execution_mode")).toString())
            .arg(photo_editor::desktop::EditorPreviewControllerTestAccess::
                     active_mask_outline_worker_running(async_outline_controller))
            .arg(photo_editor::desktop::EditorPreviewControllerTestAccess::
                     active_mask_outline_worker_completed_count(async_outline_controller))
            .arg(async_outline_completed_before + 1)
            .arg(async_outline_pending_trace.value(QStringLiteral("outline_admission_mask_elapsed_ms")).toLongLong())
            .arg(async_outline_pending_trace.value(QStringLiteral("outline_admission_cache_elapsed_ms")).toLongLong())
            .arg(async_outline_pending_trace.value(QStringLiteral("outline_admission_masks_elapsed_ms")).toLongLong())
            .arg(async_outline_pending_trace.value(QStringLiteral("outline_admission_source_size_elapsed_ms")).toLongLong())
            .arg(async_outline_pending_trace.value(QStringLiteral("outline_admission_worker_launch_elapsed_ms")).toLongLong())
            .arg(async_outline_pending_trace.value(QStringLiteral("outline_admission_availability_elapsed_ms")).toLongLong())
            .arg(async_outline_pending_trace.value(QStringLiteral("outline_admission_fallback_elapsed_ms")).toLongLong())
            .toUtf8();
    return fail(detail.constData());
  }
  const auto settled_async_outline = async_outline_controller.activeMaskOutlinePaths();
  const auto async_outline_trace =
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_outline_trace_fields(
          async_outline_controller);
  if (settled_async_outline.isEmpty() ||
      async_outline_trace.value(QStringLiteral("outline_trace_source")).toString() !=
          QStringLiteral("cache-hit") ||
      async_outline_trace.value(QStringLiteral("outline_worker_source")).toString() !=
          QStringLiteral("composite-alpha") ||
      async_outline_trace.value(QStringLiteral("mask_alpha_provider")).toString() !=
          QStringLiteral("brush-stroke-replay") ||
      async_outline_trace.value(QStringLiteral("mask_alpha_raster_width")).toInt() !=
          full_resolution_source.width() ||
      async_outline_trace.value(QStringLiteral("mask_alpha_raster_height")).toInt() !=
          full_resolution_source.height() ||
      async_outline_trace.value(QStringLiteral("outline_worker_thread")).toString().isEmpty() ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              active_mask_outline_stale_result_count(async_outline_controller) != 0) {
    const auto detail =
        QStringLiteral(
            "async outline should publish exact full-resolution final alpha through its cache "
            "(paths=%1 source=%2 worker-source=%3 provider=%4 raster=%5x%6 expected=%7x%8 thread=%9 stale=%10 "
            "kind=%11 operation=%12 source-points=%13 source-strokes=%14 replay-points=%15 local-points=%16 local-seg=%17:%18x%19 max=%20 "
            "alpha-valid=%21 alpha-cache=%22 alpha-bytes=%23 alpha-max=%24)")
            .arg(settled_async_outline.size())
            .arg(async_outline_trace.value(QStringLiteral("outline_trace_source")).toString())
            .arg(async_outline_trace.value(QStringLiteral("outline_worker_source")).toString())
            .arg(async_outline_trace.value(QStringLiteral("mask_alpha_provider")).toString())
            .arg(async_outline_trace.value(QStringLiteral("mask_alpha_raster_width")).toInt())
            .arg(async_outline_trace.value(QStringLiteral("mask_alpha_raster_height")).toInt())
            .arg(full_resolution_source.width())
            .arg(full_resolution_source.height())
            .arg(async_outline_trace.value(QStringLiteral("outline_worker_thread")).toString())
            .arg(photo_editor::desktop::EditorPreviewControllerTestAccess::
                     active_mask_outline_stale_result_count(async_outline_controller))
            .arg(async_outline_trace.value(QStringLiteral("outline_worker_mask_kind")).toString())
            .arg(async_outline_trace.value(QStringLiteral("outline_worker_mask_operation")).toString())
            .arg(async_outline_trace.value(QStringLiteral("outline_worker_source_brush_point_count")).toInt())
            .arg(async_outline_trace.value(QStringLiteral("outline_worker_source_brush_stroke_count")).toInt())
            .arg(async_outline_trace.value(QStringLiteral("outline_worker_scaled_replay_point_count")).toInt())
            .arg(async_outline_trace.value(QStringLiteral("outline_worker_local_brush_point_count")).toInt())
            .arg(async_outline_trace.value(QStringLiteral("outline_worker_local_segmentation_bytes")).toLongLong())
            .arg(async_outline_trace.value(QStringLiteral("outline_worker_local_segmentation_width")).toInt())
            .arg(async_outline_trace.value(QStringLiteral("outline_worker_local_segmentation_height")).toInt())
            .arg(async_outline_trace.value(QStringLiteral("outline_worker_local_segmentation_max")).toInt())
            .arg(async_outline_trace.value(QStringLiteral("outline_worker_alpha_valid")).toBool())
            .arg(async_outline_trace.value(QStringLiteral("outline_worker_alpha_cache_hit")).toBool())
            .arg(async_outline_trace.value(QStringLiteral("outline_worker_alpha_bytes")).toLongLong())
            .arg(async_outline_trace.value(QStringLiteral("outline_worker_alpha_max")).toInt())
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
  const auto live_brush_trace = parity_controller.activeMaskOutlineTrace();
	  if (parity_controller.activeMaskOutlineQualityTier() != QStringLiteral("live-vector") ||
	      live_brush_trace.value(QStringLiteral("outline_display_state_contract")).toString() !=
	          QStringLiteral("outline-display-state:v1|live|cached|final-alpha|fallback|unavailable") ||
	      live_brush_trace.value(QStringLiteral("outline_feedback_lifecycle_contract")).toString() !=
	          QStringLiteral("outline-feedback-lifecycle:v1|temporary-live|settled-final-alpha|fallback|unavailable") ||
	      live_brush_trace.value(QStringLiteral("outline_display_state")).toString() !=
	          QStringLiteral("live") ||
	      live_brush_trace.value(QStringLiteral("outline_feedback_lifecycle")).toString() !=
	          QStringLiteral("temporary-live") ||
	      !live_brush_trace.value(QStringLiteral("outline_temporary_feedback")).toBool() ||
	      live_brush_trace.value(QStringLiteral("outline_quality_tier")).toString() !=
	          QStringLiteral("live-vector")) {
	    return fail("brush outline should report live-vector while the stroke is still being sampled");
	  }
  parity_controller.endActiveBrushStroke();
  if (!wait_for_brush_commit(parity_controller)) {
    return fail("brush alpha parity fixture should settle before tracing final outline");
  }
  const auto brush_mask =
      parity_controller.maskStack().at(parity_controller.activeMaskIndex()).toMap();
  const auto brush_alpha_trace = settled_range_outline_trace(parity_controller);
  const auto brush_alpha_paths = parity_controller.activeMaskOutlinePaths();
  if (brush_alpha_paths.isEmpty() ||
      !outline_points_are_image_normalized(brush_alpha_paths) ||
      parity_controller.activeMaskOutlineQualityTier() !=
          QStringLiteral("settled-alpha") ||
      brush_mask.value(QStringLiteral("segmentationData")).toByteArray().isEmpty() ||
      brush_mask.value(QStringLiteral("segmentationWidth")).toInt() != source.width() ||
      brush_mask.value(QStringLiteral("segmentationHeight")).toInt() != source.height() ||
      !has_final_alpha_outline_contract(
          brush_alpha_trace,
          QStringLiteral("brush-stroke-replay")) ||
      computed_outline_source(brush_alpha_trace) !=
          QStringLiteral("composite-alpha") ||
	      brush_alpha_trace.value(QStringLiteral("outline_display_state")).toString() !=
	          QStringLiteral("cached") ||
	      brush_alpha_trace.value(QStringLiteral("outline_feedback_lifecycle")).toString() !=
	          QStringLiteral("settled-final-alpha") ||
	      brush_alpha_trace.value(QStringLiteral("outline_temporary_feedback")).toBool() ||
	      brush_alpha_trace.value(QStringLiteral("outline_quality_tier")).toString() !=
	          QStringLiteral("settled-alpha") ||
      brush_alpha_trace.value(QStringLiteral("mask_alpha_raster_width")).toInt() !=
          source.width() ||
      brush_alpha_trace.value(QStringLiteral("mask_alpha_raster_height")).toInt() !=
          source.height()) {
    const auto detail =
        QStringLiteral(
            "brush outline should move from live sampled points to the settled final alpha "
            "(tier=%1 source=%2 provider=%3 alpha=%4 raster=%5x%6 expected=%7x%8)")
            .arg(parity_controller.activeMaskOutlineQualityTier())
            .arg(brush_alpha_trace.value(QStringLiteral("outline_trace_source")).toString())
            .arg(brush_alpha_trace.value(QStringLiteral("mask_alpha_provider")).toString())
            .arg(brush_mask.value(QStringLiteral("segmentationData")).toByteArray().size())
            .arg(brush_alpha_trace.value(QStringLiteral("mask_alpha_raster_width")).toInt())
            .arg(brush_alpha_trace.value(QStringLiteral("mask_alpha_raster_height")).toInt())
            .arg(source.width())
            .arg(source.height())
            .toUtf8();
    return fail(detail.constData());
  }
  QVariantMap known_alpha_mask;
  known_alpha_mask.insert(QStringLiteral("kind"), QStringLiteral("alpha"));
  known_alpha_mask.insert(QStringLiteral("toolRole"), QStringLiteral("selection"));
  known_alpha_mask.insert(QStringLiteral("operation"), QStringLiteral("add"));
  known_alpha_mask.insert(QStringLiteral("selectionExpand"), 0.0);
  known_alpha_mask.insert(QStringLiteral("selectionSmooth"), 0.0);
  known_alpha_mask.insert(QStringLiteral("selectionFineEdge"), 0.0);
  known_alpha_mask.insert(QStringLiteral("selectionEdgeContrast"), 0.0);
  known_alpha_mask.insert(QStringLiteral("selectionDecontaminate"), 0.0);
  known_alpha_mask.insert(QStringLiteral("maskSetSelectionExpand"), 0.0);
  known_alpha_mask.insert(QStringLiteral("maskSetSelectionSmooth"), 0.0);
  known_alpha_mask.insert(QStringLiteral("maskSetSelectionFineEdge"), 0.0);
  known_alpha_mask.insert(QStringLiteral("maskSetSelectionEdgeContrast"), 0.0);
  known_alpha_mask.insert(QStringLiteral("maskSetSelectionDecontaminate"), 0.0);
  known_alpha_mask.insert(QStringLiteral("maskSetSelectionInvert"), false);
  constexpr auto known_alpha_width = 16;
  constexpr auto known_alpha_height = 16;
  QByteArray known_alpha_data(known_alpha_width * known_alpha_height, char(0));
  for (auto y = 4; y <= 10; ++y) {
    for (auto x = 3; x <= 8; ++x) {
      known_alpha_data[y * known_alpha_width + x] = char(0xff);
    }
  }
  known_alpha_mask.insert(QStringLiteral("segmentationData"), known_alpha_data);
  known_alpha_mask.insert(QStringLiteral("segmentationWidth"), known_alpha_width);
  known_alpha_mask.insert(QStringLiteral("segmentationHeight"), known_alpha_height);
  photo_editor::desktop::EditorPreviewController known_alpha_controller;
  known_alpha_controller.loadAsset(
      QStringLiteral("outline-known-alpha-bounds"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  known_alpha_controller.addMask(QStringLiteral("linear"));
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::update_active_mask_definition(
          known_alpha_controller,
          known_alpha_mask)) {
    return fail("known alpha outline bounds fixture should install a raster source mask");
  }
  known_alpha_controller.setMaskPreviewMode(QStringLiteral("overlay"));
  const auto known_alpha_paths = settled_outline_paths(known_alpha_controller);
  const auto known_alpha_trace =
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_outline_trace_fields(
          known_alpha_controller);
  const auto known_alpha_bounds =
      alpha_mask_bounds(known_alpha_data, known_alpha_width, known_alpha_height);
  const auto known_alpha_outline_bounds = outline_bounds(known_alpha_paths);
  const auto known_alpha_tolerance =
      outline_tolerance_for_image_pixels(known_alpha_width, known_alpha_height);
  if (!bounds_match(known_alpha_outline_bounds, known_alpha_bounds, known_alpha_tolerance) ||
      std::abs(
          known_alpha_trace.value(QStringLiteral("outline_trace_tolerance_normalized")).toDouble() -
          known_alpha_tolerance) > 1e-9 ||
      known_alpha_trace.value(QStringLiteral("outline_trace_tolerance_pixels")).toDouble() != 0.5) {
    const auto detail =
        QStringLiteral("%1 tolerance=%2 traceTolerance=%3")
            .arg(outline_bounds_detail(
                QStringLiteral("known raster final alpha"),
                known_alpha_outline_bounds,
                known_alpha_bounds))
            .arg(known_alpha_tolerance)
            .arg(known_alpha_trace.value(QStringLiteral("outline_trace_tolerance_normalized")).toDouble())
            .toUtf8();
    return fail(detail.constData());
  }

  QVariantMap silhouette_subject_mask = known_alpha_mask;
  silhouette_subject_mask.insert(QStringLiteral("kind"), QStringLiteral("subject"));
  constexpr auto silhouette_width = 64;
  constexpr auto silhouette_height = 96;
  QByteArray silhouette_data(silhouette_width * silhouette_height, char(0));
  const auto fill_silhouette_row = [&](int y, int left, int right) {
    for (auto x = left; x <= right; ++x) {
      silhouette_data[y * silhouette_width + x] = char(0xff);
    }
  };
  for (auto y = 6; y <= 21; ++y) {
    const auto inset = std::abs(14 - y) / 3;
    fill_silhouette_row(y, 22 + inset, 41 - inset);
  }
  for (auto y = 22; y <= 29; ++y) {
    fill_silhouette_row(y, 28, 35);
  }
  for (auto y = 30; y <= 70; ++y) {
    const auto half_width = 10 + std::min(8, (y - 30) / 4);
    fill_silhouette_row(y, 32 - half_width, 32 + half_width);
  }
  for (auto y = 71; y <= 92; ++y) {
    fill_silhouette_row(y, 16, 28);
    fill_silhouette_row(y, 36, 48);
  }
  silhouette_subject_mask.insert(QStringLiteral("segmentationData"), silhouette_data);
  silhouette_subject_mask.insert(QStringLiteral("segmentationWidth"), silhouette_width);
  silhouette_subject_mask.insert(QStringLiteral("segmentationHeight"), silhouette_height);
  photo_editor::desktop::EditorPreviewController silhouette_controller;
  silhouette_controller.loadAsset(
      QStringLiteral("outline-subject-silhouette-no-scanline-boxes"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  silhouette_controller.addMask(QStringLiteral("linear"));
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::update_active_mask_definition(
          silhouette_controller,
          silhouette_subject_mask)) {
    return fail("subject silhouette fixture should install a raster source mask");
  }
  silhouette_controller.setMaskPreviewMode(QStringLiteral("overlay"));
  const auto silhouette_paths = settled_outline_paths(silhouette_controller);
  const auto silhouette_bounds = outline_bounds(silhouette_paths);
  if (outline_path_count(silhouette_paths) != 1 ||
      silhouette_bounds.height() < 0.82 ||
      silhouette_bounds.width() < 0.45) {
    const auto detail =
        QStringLiteral(
            "one connected subject alpha must produce one continuous exterior contour, not scanline rectangles (paths=%1 bounds=%2,%3 %4x%5 maxSegment=%6)")
            .arg(outline_path_count(silhouette_paths))
            .arg(silhouette_bounds.x(), 0, 'f', 3)
            .arg(silhouette_bounds.y(), 0, 'f', 3)
            .arg(silhouette_bounds.width(), 0, 'f', 3)
            .arg(silhouette_bounds.height(), 0, 'f', 3)
            .arg(max_outline_segment_length(silhouette_paths), 0, 'f', 3)
            .toUtf8();
    return fail(detail.constData());
  }

  QVariantMap corner_touching_subject_mask = known_alpha_mask;
  corner_touching_subject_mask.insert(QStringLiteral("kind"), QStringLiteral("subject"));
  constexpr auto corner_touch_width = 24;
  constexpr auto corner_touch_height = 24;
  QByteArray corner_touch_data(corner_touch_width * corner_touch_height, char(0));
  for (auto y = 3; y <= 10; ++y) {
    for (auto x = 3; x <= 10; ++x) {
      corner_touch_data[y * corner_touch_width + x] = char(0xff);
    }
  }
  for (auto y = 11; y <= 18; ++y) {
    for (auto x = 11; x <= 18; ++x) {
      corner_touch_data[y * corner_touch_width + x] = char(0xff);
    }
  }
  corner_touching_subject_mask.insert(QStringLiteral("segmentationData"), corner_touch_data);
  corner_touching_subject_mask.insert(QStringLiteral("segmentationWidth"), corner_touch_width);
  corner_touching_subject_mask.insert(QStringLiteral("segmentationHeight"), corner_touch_height);
  photo_editor::desktop::EditorPreviewController corner_touch_controller;
  corner_touch_controller.loadAsset(
      QStringLiteral("outline-corner-touching-subject"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  corner_touch_controller.addMask(QStringLiteral("linear"));
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::update_active_mask_definition(
          corner_touch_controller,
          corner_touching_subject_mask)) {
    return fail("corner-touching subject fixture should install a raster source mask");
  }
  corner_touch_controller.setMaskPreviewMode(QStringLiteral("overlay"));
  const auto corner_touch_paths = corner_touch_controller.activeMaskOutlinePaths();
  const auto corner_touch_trace =
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_outline_trace_fields(
          corner_touch_controller);
  if (corner_touch_paths.isEmpty() ||
      max_outline_segment_length(corner_touch_paths) > 0.36 ||
      corner_touch_trace.value(QStringLiteral("outline_trace_source")).toString() !=
          QStringLiteral("direct-mask")) {
    const auto detail =
        QStringLiteral(
            "corner-touching subject alpha should trace without long stitched chords "
            "(paths=%1 maxSegment=%2 source=%3 simplify=%4 in=%5 out=%6)")
            .arg(outline_path_count(corner_touch_paths))
            .arg(max_outline_segment_length(corner_touch_paths), 0, 'f', 4)
            .arg(corner_touch_trace.value(QStringLiteral("outline_trace_source")).toString())
            .arg(corner_touch_trace.value(QStringLiteral("outline_simplification_contract")).toString())
            .arg(corner_touch_trace.value(QStringLiteral("outline_simplification_input_point_count")).toInt())
            .arg(corner_touch_trace.value(QStringLiteral("outline_simplification_output_point_count")).toInt())
            .toUtf8();
    return fail(detail.constData());
  }

  QVariantMap untraceable_subject_mask = known_alpha_mask;
  untraceable_subject_mask.insert(QStringLiteral("kind"), QStringLiteral("subject"));
  untraceable_subject_mask.insert(QStringLiteral("segmentationData"), QByteArray(16, char(0xff)));
  untraceable_subject_mask.insert(QStringLiteral("segmentationWidth"), 16);
  untraceable_subject_mask.insert(QStringLiteral("segmentationHeight"), 1);
  photo_editor::desktop::EditorPreviewController untraceable_subject_controller;
  untraceable_subject_controller.loadAsset(
      QStringLiteral("outline-untraceable-subject-no-rectangle"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  untraceable_subject_controller.addMask(QStringLiteral("linear"));
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::update_active_mask_definition(
          untraceable_subject_controller,
          untraceable_subject_mask)) {
    return fail("untraceable subject fixture should install a raster source mask");
  }
  untraceable_subject_controller.setMaskPreviewMode(QStringLiteral("overlay"));
  const auto untraceable_subject_paths =
      untraceable_subject_controller.activeMaskOutlinePaths();
  const auto untraceable_subject_trace =
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_outline_trace_fields(
          untraceable_subject_controller);
  if (!untraceable_subject_paths.isEmpty() ||
      untraceable_subject_trace.value(QStringLiteral("outline_trace_source")).toString() !=
          QStringLiteral("direct-mask")) {
    const auto detail =
        QStringLiteral(
            "semantic masks should not fabricate a rectangle when alpha tracing is unavailable "
            "(paths=%1 source=%2)")
            .arg(outline_path_count(untraceable_subject_paths))
            .arg(untraceable_subject_trace.value(QStringLiteral("outline_trace_source")).toString())
            .toUtf8();
    return fail(detail.constData());
  }

  QVariantMap refined_subject_outline_mask = corner_touching_subject_mask;
  refined_subject_outline_mask.insert(QStringLiteral("selectionFineEdge"), 100.0);
  refined_subject_outline_mask.insert(QStringLiteral("selectionEdgeContrast"), 65.0);
  photo_editor::desktop::EditorPreviewController refined_subject_outline_controller;
  refined_subject_outline_controller.loadAsset(
      QStringLiteral("outline-refined-subject-source-free"),
      source_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  refined_subject_outline_controller.addMask(QStringLiteral("linear"));
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::update_active_mask_definition(
          refined_subject_outline_controller,
          refined_subject_outline_mask)) {
    return fail("refined subject outline fixture should install a raster source mask");
  }
  refined_subject_outline_controller.setMaskRefinementHighQualityPreview(true);
  refined_subject_outline_controller.setMaskFullResolutionPreview(true);
  refined_subject_outline_controller.setMaskPreviewMode(QStringLiteral("overlay"));
  const auto refined_subject_paths =
      settled_outline_paths(refined_subject_outline_controller);
  const auto refined_subject_trace =
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_outline_trace_fields(
          refined_subject_outline_controller);
  if (refined_subject_paths.isEmpty() ||
      computed_outline_source(refined_subject_trace) !=
          QStringLiteral("composite-alpha") ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_outline_full_key_count(
          refined_subject_outline_controller) != 1 ||
      max_outline_segment_length(refined_subject_paths) > 0.36) {
    const auto detail =
        QStringLiteral(
            "refined subject outlines should trace the canonical source-aware alpha for overlay and zoom "
            "(source=%1 fullKey=%2 paths=%3 maxSegment=%4 elapsed=%5)")
            .arg(refined_subject_trace.value(QStringLiteral("outline_trace_source")).toString())
            .arg(photo_editor::desktop::EditorPreviewControllerTestAccess::
                     active_mask_outline_full_key_count(refined_subject_outline_controller))
            .arg(outline_path_count(refined_subject_paths))
            .arg(max_outline_segment_length(refined_subject_paths), 0, 'f', 4)
            .arg(refined_subject_trace.value(QStringLiteral("outline_trace_elapsed_ms")).toLongLong())
            .toUtf8();
    return fail(detail.constData());
  }

  QVariantMap jagged_alpha_mask = known_alpha_mask;
  constexpr auto jagged_alpha_width = 64;
  constexpr auto jagged_alpha_height = 64;
  QByteArray jagged_alpha_data(jagged_alpha_width * jagged_alpha_height, char(0));
  for (auto y = 8; y < 56; ++y) {
    const auto left = 10 + ((y / 2) % 2 == 0 ? 0 : 7);
    const auto right = 42 + ((y / 3) % 2 == 0 ? 8 : 0);
    for (auto x = left; x <= right; ++x) {
      jagged_alpha_data[y * jagged_alpha_width + x] = char(0xff);
    }
  }
  jagged_alpha_mask.insert(QStringLiteral("segmentationData"), jagged_alpha_data);
  jagged_alpha_mask.insert(QStringLiteral("segmentationWidth"), jagged_alpha_width);
  jagged_alpha_mask.insert(QStringLiteral("segmentationHeight"), jagged_alpha_height);
  photo_editor::desktop::EditorPreviewController jagged_alpha_controller;
  jagged_alpha_controller.loadAsset(
      QStringLiteral("outline-zoom-adaptive-alpha"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  jagged_alpha_controller.addMask(QStringLiteral("linear"));
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::update_active_mask_definition(
          jagged_alpha_controller,
          jagged_alpha_mask)) {
    return fail("jagged alpha outline fixture should install a raster source mask");
  }
  jagged_alpha_controller.setMaskPreviewMode(QStringLiteral("overlay"));
  const auto base_jagged_paths = settled_outline_paths(jagged_alpha_controller);
  const auto base_jagged_trace =
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_outline_trace_fields(
          jagged_alpha_controller);
  auto outline_scale_controls_notifications = 0;
  auto outline_scale_overlay_notifications = 0;
  QObject::connect(
      &jagged_alpha_controller,
      &photo_editor::desktop::EditorPreviewController::controlsChanged,
      [&outline_scale_controls_notifications]() { ++outline_scale_controls_notifications; });
  QObject::connect(
      &jagged_alpha_controller,
      &photo_editor::desktop::EditorPreviewController::maskOverlayChanged,
      [&outline_scale_overlay_notifications]() { ++outline_scale_overlay_notifications; });
  jagged_alpha_controller.setActiveMaskOutlineDisplayScale(4.0);
  if (outline_scale_controls_notifications != 0 || outline_scale_overlay_notifications != 1) {
    return fail("outline zoom changes should repaint only the mask overlay without invalidating broad layer/control bindings");
  }
  const auto zoomed_jagged_paths = settled_outline_paths(jagged_alpha_controller);
  const auto zoomed_jagged_trace =
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_outline_trace_fields(
          jagged_alpha_controller);
  const auto base_output_points =
      base_jagged_trace.value(QStringLiteral("outline_simplification_output_point_count")).toInt();
  const auto zoomed_output_points =
      zoomed_jagged_trace.value(QStringLiteral("outline_simplification_output_point_count")).toInt();
  if (base_jagged_paths.isEmpty() ||
      zoomed_jagged_paths.isEmpty() ||
      zoomed_jagged_trace.value(QStringLiteral("outline_display_scale")).toDouble() != 4.0 ||
      zoomed_jagged_trace.value(QStringLiteral("outline_trace_tolerance_pixels")).toDouble() >=
          base_jagged_trace.value(QStringLiteral("outline_trace_tolerance_pixels")).toDouble() ||
      zoomed_output_points < base_output_points) {
    const auto detail =
        QStringLiteral(
            "zoomed mask outlines should recompute from final alpha with a lower simplification tolerance "
            "(baseTolerance=%1 zoomTolerance=%2 basePoints=%3 zoomPoints=%4 scale=%5)")
            .arg(base_jagged_trace.value(QStringLiteral("outline_trace_tolerance_pixels")).toDouble())
            .arg(zoomed_jagged_trace.value(QStringLiteral("outline_trace_tolerance_pixels")).toDouble())
            .arg(base_output_points)
            .arg(zoomed_output_points)
            .arg(zoomed_jagged_trace.value(QStringLiteral("outline_display_scale")).toDouble())
            .toUtf8();
    return fail(detail.constData());
  }

  photo_editor::desktop::EditorPreviewController outline_cache_controller;
  outline_cache_controller.loadAsset(
      QStringLiteral("outline-cache-focused-proof"),
      source_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  outline_cache_controller.setMaskPreviewMode(QStringLiteral("overlay"));
  outline_cache_controller.startSelectionBrush(false);
  outline_cache_controller.setActiveMaskBrushSize(0.16);
  outline_cache_controller.beginActiveBrushStroke();
  outline_cache_controller.addBrushPoint(0.34, 0.42);
  outline_cache_controller.addBrushPoint(0.58, 0.62);
  outline_cache_controller.endActiveBrushStroke();
  if (!wait_for_brush_commit(outline_cache_controller)) {
    return fail("outline cache fixture should settle brush alpha before tracing");
  }
  const auto cache_hits_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          active_mask_outline_cache_hit_count(outline_cache_controller);
  const auto cache_misses_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          active_mask_outline_cache_miss_count(outline_cache_controller);
  const auto full_keys_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          active_mask_outline_full_key_count(outline_cache_controller);
  const auto completed_before_cache = photo_editor::desktop::EditorPreviewControllerTestAccess::
      active_mask_outline_worker_completed_count(outline_cache_controller);
  (void)outline_cache_controller.activeMaskOutlinePaths();
  if (!wait_for_active_mask_outline_worker(outline_cache_controller, completed_before_cache + 1))
    return fail("outline cache fixture must publish its asynchronous final alpha");
  const auto first_cached_outline = outline_cache_controller.activeMaskOutlinePaths();
  const auto first_cache_trace =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          active_mask_outline_trace_fields(outline_cache_controller);
  const auto second_cached_outline = outline_cache_controller.activeMaskOutlinePaths();
  const auto second_cache_trace =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          active_mask_outline_trace_fields(outline_cache_controller);
  if (first_cached_outline.isEmpty() ||
      second_cached_outline != first_cached_outline ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              active_mask_outline_cache_miss_count(outline_cache_controller) !=
          cache_misses_before + 1 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              active_mask_outline_cache_hit_count(outline_cache_controller) !=
          cache_hits_before + 2 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              active_mask_outline_full_key_count(outline_cache_controller) !=
          full_keys_before + 1 ||
      first_cache_trace.value(QStringLiteral("outline_trace_source")).toString() !=
          QStringLiteral("cache-hit") ||
	      first_cache_trace.value(QStringLiteral("outline_display_state")).toString() !=
	          QStringLiteral("cached") ||
	      first_cache_trace.value(QStringLiteral("outline_feedback_lifecycle")).toString() !=
	          QStringLiteral("settled-final-alpha") ||
	      first_cache_trace.value(QStringLiteral("outline_temporary_feedback")).toBool() ||
	      second_cache_trace.value(QStringLiteral("outline_trace_source")).toString() !=
	          QStringLiteral("cache-hit") ||
	      second_cache_trace.value(QStringLiteral("outline_display_state")).toString() !=
	          QStringLiteral("cached") ||
	      second_cache_trace.value(QStringLiteral("outline_feedback_lifecycle")).toString() !=
	          QStringLiteral("settled-final-alpha") ||
	      second_cache_trace.value(QStringLiteral("outline_temporary_feedback")).toBool() ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              active_mask_outline_trace_source(outline_cache_controller) !=
          QStringLiteral("cache-hit") ||
      first_cache_trace.value(QStringLiteral("mask_alpha_provider")).toString() !=
          QStringLiteral("brush-stroke-replay") ||
      first_cache_trace.value(QStringLiteral("mask_source_id")).toString().isEmpty() ||
      first_cache_trace.value(QStringLiteral("mask_source_operation")).toString() !=
          QStringLiteral("add") ||
      !first_cache_trace.value(QStringLiteral("mask_source_revision_hash"))
           .toString()
           .startsWith(QStringLiteral("sha1:")) ||
      first_cache_trace.value(QStringLiteral("mask_alpha_raster_width")).toInt() !=
          source.width() ||
      first_cache_trace.value(QStringLiteral("mask_alpha_raster_height")).toInt() !=
          source.height() ||
      !first_cache_trace.value(QStringLiteral("mask_alpha_trace_hash"))
           .toString()
           .startsWith(QStringLiteral("sha1:")) ||
      second_cache_trace.value(QStringLiteral("mask_alpha_trace_hash")).toString() !=
          first_cache_trace.value(QStringLiteral("mask_alpha_trace_hash")).toString() ||
      second_cache_trace.value(QStringLiteral("mask_alpha_trace_signature")).toString() !=
          first_cache_trace.value(QStringLiteral("mask_alpha_trace_signature")).toString()) {
    return fail("outline cache should key final-alpha traces by source id, revision, raster size, operation, and provider");
  }
  const auto misses_after_cache_hit =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          active_mask_outline_cache_miss_count(outline_cache_controller);
  const auto full_keys_after_cache_hit =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          active_mask_outline_full_key_count(outline_cache_controller);
  outline_cache_controller.setMaskRefinementPreviewMode(QStringLiteral("original"));
  const auto refinement_outline = settled_outline_paths(outline_cache_controller);
  const auto refinement_trace =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          active_mask_outline_trace_fields(outline_cache_controller);
  if (refinement_outline.isEmpty() ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              active_mask_outline_cache_miss_count(outline_cache_controller) !=
          misses_after_cache_hit + 1 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              active_mask_outline_full_key_count(outline_cache_controller) !=
          full_keys_after_cache_hit + 1 ||
      computed_outline_source(refinement_trace) !=
          QStringLiteral("composite-alpha")) {
    return fail("outline cache should miss and recompute when refinement settings change");
  }

  photo_editor::desktop::EditorPreviewController fallback_outline_controller;
  fallback_outline_controller.loadAsset(
      QStringLiteral("outline-fallback-missing-source"),
      runtime_root.filePath(QStringLiteral("missing-outline-source.png")),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  fallback_outline_controller.setMaskPreviewMode(QStringLiteral("overlay"));
  fallback_outline_controller.startSelectionBrush(false);
  fallback_outline_controller.setActiveMaskBrushSize(0.16);
  fallback_outline_controller.addBrushPoint(0.42, 0.52);
  const auto fallback_trace = settled_range_outline_trace(fallback_outline_controller);
	  if (fallback_trace.value(QStringLiteral("outline_display_state")).toString() !=
	          QStringLiteral("fallback") ||
	      fallback_trace.value(QStringLiteral("outline_feedback_lifecycle")).toString() !=
	          QStringLiteral("fallback") ||
	      fallback_trace.value(QStringLiteral("outline_temporary_feedback")).toBool() ||
	      !computed_outline_source(fallback_trace)
           .contains(QStringLiteral("fallback")) ||
      fallback_trace.value(QStringLiteral("outline_quality_tier")).toString() !=
          fallback_outline_controller.activeMaskOutlineQualityTier()) {
    return fail("outline trace metadata should identify fallback outlines when source alpha cannot be decoded");
  }

  parity_controller.removeActiveMask();
  parity_controller.startPolygonSelection();
  parity_controller.appendActivePolygonPoint(0.18, 0.20);
  parity_controller.appendActivePolygonPoint(0.76, 0.24);
  parity_controller.appendActivePolygonPoint(0.66, 0.74);
  parity_controller.appendActivePolygonPoint(0.24, 0.68);
  const auto polygon_mask =
      parity_controller.maskStack().at(parity_controller.activeMaskIndex()).toMap();
  const auto polygon_alpha_trace = settled_range_outline_trace(parity_controller);
  const auto polygon_alpha_paths = parity_controller.activeMaskOutlinePaths();
  if (polygon_alpha_paths.isEmpty() ||
      !outline_points_are_image_normalized(polygon_alpha_paths) ||
      !has_final_alpha_outline_contract(
          polygon_alpha_trace,
          QStringLiteral("procedural-geometry")) ||
      (computed_outline_source(polygon_alpha_trace) != QStringLiteral("vector-path") &&
       computed_outline_source(polygon_alpha_trace) != QStringLiteral("composite-alpha")) ||
      polygon_mask.value(QStringLiteral("shapeAlphaSourceContract")).toString() !=
          QStringLiteral("editable-path-plus-alpha-tile-cache-v1") ||
      polygon_mask.value(QStringLiteral("shapeAlphaSourceOfTruth")).toString() !=
          QStringLiteral("polygonPoints") ||
      !polygon_mask.value(QStringLiteral("shapeAlphaSourceSignature"))
           .toString()
           .startsWith(QStringLiteral("sha1:"))) {
    std::cerr << "Polygon trace source=" << polygon_alpha_trace.value(QStringLiteral("outline_trace_source")).toString().toStdString()
              << " provider=" << polygon_alpha_trace.value(QStringLiteral("mask_alpha_provider")).toString().toStdString()
              << " masks=" << parity_controller.maskCount()
              << " paths=" << polygon_alpha_paths.size()
              << " shapeSource=" << polygon_mask.value(QStringLiteral("shapeAlphaSourceOfTruth")).toString().toStdString()
              << " contract=" << polygon_mask.value(QStringLiteral("shapeAlphaSourceContract")).toString().toStdString()
              << " signature=" << polygon_mask.value(QStringLiteral("shapeAlphaSourceSignature")).toString().left(12).toStdString() << '\n';
    return fail("polygon outline should stay tied to editable polygon alpha and final procedural geometry");
  }

  parity_controller.setActiveMaskSelectionInverted(true);
  const auto inverted_polygon_trace = settled_range_outline_trace(parity_controller);
  const auto inverted_polygon_paths = parity_controller.activeMaskOutlinePaths();
  const auto inverted_polygon_bounds = outline_bounds(inverted_polygon_paths);
  if (computed_outline_source(inverted_polygon_trace) != QStringLiteral("composite-alpha") ||
      inverted_polygon_paths.size() != 2 ||
      !bounds_match(inverted_polygon_bounds, QRectF(0, 0, 1, 1), 0.015)) {
    return fail("inverted polygon outlines must show the image boundary and unselected polygon hole");
  }
  parity_controller.setActiveMaskSelectionInverted(false);

  parity_controller.removeActiveMask();
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
  const auto lasso_alpha_trace = settled_range_outline_trace(parity_controller);
  const auto lasso_alpha_paths = parity_controller.activeMaskOutlinePaths();
  const auto lasso_outline_bounds = outline_bounds(lasso_alpha_paths);
  const auto lasso_final_points = lasso_mask.value(QStringLiteral("polygonPoints")).toList();
  const auto lasso_final_bounds = normalized_points_bounds(lasso_final_points);
  if (lasso_alpha_paths.isEmpty() ||
      !outline_points_are_image_normalized(lasso_alpha_paths) ||
      !has_final_alpha_outline_contract(
          lasso_alpha_trace,
          QStringLiteral("procedural-geometry")) ||
      (computed_outline_source(lasso_alpha_trace) != QStringLiteral("vector-path") &&
       computed_outline_source(lasso_alpha_trace) != QStringLiteral("composite-alpha")) ||
      lasso_mask.value(QStringLiteral("shapeAlphaSourceContract")).toString() !=
          QStringLiteral("editable-path-plus-alpha-tile-cache-v1") ||
      lasso_mask.value(QStringLiteral("shapeAlphaSourceOfTruth")).toString() !=
          QStringLiteral("lassoRawPoints+polygonPoints") ||
      lasso_mask.value(QStringLiteral("shapeAlphaRawPointCount")).toInt() !=
          lasso_raw_points.size() ||
      !normalized_point_lists_match(
          lasso_mask.value(QStringLiteral("lassoRawPoints")).toList(),
          lasso_raw_points) ||
      !lasso_mask.value(QStringLiteral("lassoSimplified")).toBool() ||
      lasso_mask.value(QStringLiteral("lassoSmoothed")).toBool() ||
      lasso_mask.value(QStringLiteral("lassoSimplifiedPointCount")).toInt() !=
          lasso_final_points.size() ||
      !bounds_match(lasso_outline_bounds, lasso_final_bounds, 0.035) ||
      !lasso_mask.value(QStringLiteral("shapeAlphaSourceSignature"))
           .toString()
           .startsWith(QStringLiteral("sha1:"))) {
    const auto detail =
        QStringLiteral(
            "lasso outline should stay tied to editable lasso alpha and final procedural geometry "
            "(outline=%1,%2 %3x%4 final=%5,%6 %7x%8)")
            .arg(lasso_outline_bounds.x())
            .arg(lasso_outline_bounds.y())
            .arg(lasso_outline_bounds.width())
            .arg(lasso_outline_bounds.height())
            .arg(lasso_final_bounds.x())
            .arg(lasso_final_bounds.y())
            .arg(lasso_final_bounds.width())
            .arg(lasso_final_bounds.height())
            .toUtf8();
    return fail(detail.constData());
  }
  photo_editor::desktop::EditorPreviewController simplify_lasso_controller;
  simplify_lasso_controller.startLassoSelection(false);
  const QVariantList reducible_lasso_raw_points{
      normalized_point(0.12, 0.12),
      normalized_point(0.22, 0.12),
      normalized_point(0.34, 0.12),
      normalized_point(0.34, 0.26),
      normalized_point(0.34, 0.42),
      normalized_point(0.22, 0.42),
      normalized_point(0.12, 0.42),
      normalized_point(0.12, 0.26),
  };
  for (const auto& point_value : reducible_lasso_raw_points) {
    const auto point = point_value.toMap();
    simplify_lasso_controller.appendActivePolygonPoint(
        point.value(QStringLiteral("x")).toDouble(),
        point.value(QStringLiteral("y")).toDouble());
  }
  if (!simplify_lasso_controller.closeActiveLassoPath(0.012)) {
    return fail("lasso close should simplify and smooth a reducible raw path");
  }
  const auto simplified_lasso_mask =
      simplify_lasso_controller.maskStack().at(simplify_lasso_controller.activeMaskIndex()).toMap();
  if (!simplified_lasso_mask.value(QStringLiteral("lassoSimplified")).toBool() ||
      simplified_lasso_mask.value(QStringLiteral("lassoSmoothed")).toBool() ||
      !normalized_point_lists_match(
          simplified_lasso_mask.value(QStringLiteral("lassoRawPoints")).toList(),
          reducible_lasso_raw_points) ||
      normalized_point_lists_match(
          simplified_lasso_mask.value(QStringLiteral("polygonPoints")).toList(),
          reducible_lasso_raw_points)) {
    return fail("lasso close should simplify editable geometry without mutating raw samples");
  }

  photo_editor::desktop::EditorPreviewController duplicate_lasso_controller;
  duplicate_lasso_controller.startLassoSelection(false);
  duplicate_lasso_controller.appendActivePolygonPoint(0.40, 0.40);
  duplicate_lasso_controller.appendActivePolygonPoint(0.4001, 0.4001);
  const auto duplicate_guard_mask =
      duplicate_lasso_controller.maskStack().at(duplicate_lasso_controller.activeMaskIndex()).toMap();
  if (duplicate_guard_mask.value(QStringLiteral("polygonPoints")).toList().size() != 1 ||
      duplicate_guard_mask.value(QStringLiteral("lassoRawPoints")).toList().size() != 1 ||
      duplicate_guard_mask.value(QStringLiteral("lassoRawPointCount")).toInt() != 1) {
    return fail("near-duplicate lasso samples should not mutate polygon points or raw lasso path");
  }

  photo_editor::desktop::EditorPreviewController lasso_smoke_controller;
  lasso_smoke_controller.loadAsset(
      QStringLiteral("lasso-responsiveness-smoke"),
      source_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  QElapsedTimer lasso_smoke_timer;
  lasso_smoke_timer.start();
  const auto lasso_smoke_initial_count = lasso_smoke_controller.maskStack().size();
  lasso_smoke_controller.beginCoalescedEdit();
  lasso_smoke_controller.startLassoSelection(false);
  for (const auto& point_value : lasso_raw_points) {
    const auto point = point_value.toMap();
    lasso_smoke_controller.appendActivePolygonPoint(
        point.value(QStringLiteral("x")).toDouble(),
        point.value(QStringLiteral("y")).toDouble());
  }
  if (lasso_smoke_controller.activeMaskKind() != QStringLiteral("lasso") ||
      lasso_smoke_controller.activeMaskOperation() != QStringLiteral("add") ||
      lasso_smoke_controller.activeMaskPolygonPointCount() != lasso_raw_points.size() ||
      !lasso_smoke_controller.closeActiveLassoPath()) {
    return fail("lasso smoke should start, drag, and close an additive lasso immediately");
  }
  lasso_smoke_controller.endCoalescedEdit();
  const auto lasso_smoke_committed_count = lasso_smoke_controller.maskStack().size();
  if (lasso_smoke_committed_count != lasso_smoke_initial_count + 1 ||
      !lasso_smoke_controller.canUndo()) {
    return fail("lasso smoke should commit one undoable lasso after close");
  }
  lasso_smoke_controller.undo();
  if (lasso_smoke_controller.maskStack().size() != lasso_smoke_initial_count ||
      !lasso_smoke_controller.canRedo()) {
    return fail("lasso smoke undo should remove the coalesced lasso without stalling");
  }
  lasso_smoke_controller.redo();
  if (lasso_smoke_controller.maskStack().size() != lasso_smoke_committed_count ||
      lasso_smoke_controller.activeMaskKind() != QStringLiteral("lasso")) {
    return fail("lasso smoke redo should restore the committed lasso without stalling");
  }
  const auto cancel_count_before = lasso_smoke_controller.maskStack().size();
  lasso_smoke_controller.startLassoSelection(false);
  lasso_smoke_controller.appendActivePolygonPoint(0.18, 0.20);
  lasso_smoke_controller.appendActivePolygonPoint(0.48, 0.22);
  lasso_smoke_controller.appendActivePolygonPoint(0.30, 0.58);
  lasso_smoke_controller.clearActivePolygonPoints();
  lasso_smoke_controller.removeActiveMask();
  if (lasso_smoke_controller.maskStack().size() != cancel_count_before ||
      lasso_smoke_controller.activeMaskKind() != QStringLiteral("lasso")) {
    return fail("lasso smoke cancel should clear and remove only the unfinished lasso");
  }
  lasso_smoke_controller.startLassoSelection(true);
  lasso_smoke_controller.appendActivePolygonPoint(0.22, 0.24);
  lasso_smoke_controller.appendActivePolygonPoint(0.72, 0.28);
  lasso_smoke_controller.appendActivePolygonPoint(0.58, 0.72);
  if (lasso_smoke_controller.activeMaskOperation() != QStringLiteral("subtract") ||
      !lasso_smoke_controller.closeActiveLassoPath()) {
    return fail("lasso smoke subtract should use the same responsive draw-and-close path");
  }
  const auto subtract_lasso =
      lasso_smoke_controller.maskStack().at(lasso_smoke_controller.activeMaskIndex()).toMap();
  if (subtract_lasso.value(QStringLiteral("operation")).toString() != QStringLiteral("subtract") ||
      !subtract_lasso.value(QStringLiteral("lassoClosed")).toBool()) {
    return fail("lasso smoke subtract should commit as a closed subtract lasso");
  }
  if (lasso_smoke_timer.elapsed() > 250) {
    const auto detail =
        QStringLiteral("lasso start/drag/close/cancel/undo/subtract smoke should stay responsive (elapsed=%1ms)")
            .arg(lasso_smoke_timer.elapsed())
            .toUtf8();
    return fail(detail.constData());
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
      smart_alpha_trace.value(QStringLiteral("outline_trace_source")).toString() !=
          QStringLiteral("clean-canvas") ||
      smart_mask.value(QStringLiteral("segmentationData")).toByteArray().isEmpty() ||
      smart_mask.value(QStringLiteral("segmentationWidth")).toInt() != smart_source.width() ||
      smart_mask.value(QStringLiteral("segmentationHeight")).toInt() != smart_source.height() ||
      smart_alpha_trace.value(QStringLiteral("mask_alpha_raster_width")).toInt() !=
          smart_source.width() ||
      smart_alpha_trace.value(QStringLiteral("mask_alpha_raster_height")).toInt() !=
          smart_source.height() ||
      std::abs(smart_mask.value(QStringLiteral("feather")).toDouble()) > 1e-9 ||
      std::abs(smart_mask.value(QStringLiteral("selectionSmooth")).toDouble()) > 1e-9 ||
      std::abs(smart_mask.value(QStringLiteral("selectionFineEdge")).toDouble()) > 1e-9 ||
      std::abs(smart_mask.value(QStringLiteral("selectionFeather")).toDouble()) > 1e-9 ||
      smart_mask.value(QStringLiteral("generatedAlphaSourceContract")).toString() !=
          QStringLiteral("generated-alpha-plus-metadata-v1") ||
      smart_mask.value(QStringLiteral("generatedAlphaTargetType")).toString() !=
          QStringLiteral("object") ||
      !smart_mask.value(QStringLiteral("generatedAlphaSourceSignature"))
           .toString()
           .startsWith(QStringLiteral("sha1:"))) {
    const auto detail =
        QStringLiteral(
            "smart-selection outline should be traced from the committed object raster alpha used by final rendering "
            "(kind=%1 source=%2 provider=%3 alpha=%4 raster=%5x%6 expected=%7x%8 contract=%9 target=%10)")
            .arg(smart_controller.activeMaskKind())
            .arg(smart_alpha_trace.value(QStringLiteral("outline_trace_source")).toString())
            .arg(smart_alpha_trace.value(QStringLiteral("mask_alpha_provider")).toString())
            .arg(smart_mask.value(QStringLiteral("segmentationData")).toByteArray().size())
            .arg(smart_alpha_trace.value(QStringLiteral("mask_alpha_raster_width")).toInt())
            .arg(smart_alpha_trace.value(QStringLiteral("mask_alpha_raster_height")).toInt())
            .arg(smart_source.width())
            .arg(smart_source.height())
            .arg(smart_mask.value(QStringLiteral("generatedAlphaSourceContract")).toString())
            .arg(smart_mask.value(QStringLiteral("generatedAlphaTargetType")).toString())
            .toUtf8();
    return fail(detail.constData());
  }
  const auto smart_alpha_bounds = alpha_mask_bounds(
      smart_mask.value(QStringLiteral("segmentationData")).toByteArray(),
      smart_mask.value(QStringLiteral("segmentationWidth")).toInt(),
      smart_mask.value(QStringLiteral("segmentationHeight")).toInt());
  const auto smart_outline_bounds = outline_bounds(smart_alpha_paths);
  const auto smart_outline_tolerance = outline_tolerance_for_image_pixels(
      smart_mask.value(QStringLiteral("segmentationWidth")).toInt(),
      smart_mask.value(QStringLiteral("segmentationHeight")).toInt());
  if (!bounds_match(smart_outline_bounds, smart_alpha_bounds, smart_outline_tolerance) ||
      std::abs(
          smart_alpha_trace.value(QStringLiteral("outline_trace_tolerance_normalized")).toDouble() -
          smart_outline_tolerance) > 1e-9 ||
      smart_alpha_trace.value(QStringLiteral("outline_trace_tolerance_pixels")).toDouble() != 0.5) {
    const auto detail =
        QStringLiteral("%1 tolerance=%2 traceTolerance=%3")
            .arg(outline_bounds_detail(
                QStringLiteral("smart object final alpha"),
                smart_outline_bounds,
                smart_alpha_bounds))
            .arg(smart_outline_tolerance)
            .arg(smart_alpha_trace.value(QStringLiteral("outline_trace_tolerance_normalized")).toDouble())
            .toUtf8();
    return fail(detail.constData());
  }

  const auto smart_layer_index = smart_controller.ensureActiveSelectionAdjustmentLayer();
  const auto smart_curve_points = QVariantList{
      normalized_point(0.0, 0.0),
      normalized_point(0.45, 0.58),
      normalized_point(1.0, 1.0),
  };
  if (smart_layer_index < 0 || smart_layer_index >= smart_controller.photoLayers().size()) {
    return fail("a committed smart selection should expose its linked adjustment layer immediately");
  }
  const auto smart_linked_layer = smart_controller.photoLayers().at(smart_layer_index).toMap();
  if (!smart_linked_layer.value(QStringLiteral("selectionScoped")).toBool() ||
      smart_linked_layer.value(QStringLiteral("selectionMaskSetId")).toString() !=
          smart_mask.value(QStringLiteral("maskSetId")).toString() ||
      !smart_controller.setPhotoLayerCurvePoints(
          smart_layer_index,
          QStringLiteral("RGB"),
          smart_curve_points) ||
      !photo_editor::desktop::EditorPreviewControllerTestAccess::
          photo_layer_adjustment_render_timer_active(smart_controller)) {
    return fail("Smart Select -> Edit Layer -> tone curve should route to the selection-scoped layer and schedule immediate visible feedback");
  }
  const auto smart_curve_layer = smart_controller.photoLayers().at(smart_layer_index).toMap();
  if (smart_curve_layer.value(QStringLiteral("masterCurvePoints")).toList().size() != 3 ||
      !wait_for_layer_adjustment_flush(smart_controller, 1) ||
      !photo_editor::desktop::EditorPreviewControllerTestAccess::
          photo_layer_adjustment_render_within_latency_budget(smart_controller)) {
    return fail("selection-scoped tone curves should persist and flush inside the 16 ms adjustment scheduling budget");
  }

  const auto range_source_path = runtime_root.filePath(QStringLiteral("outline-range-source.png"));
  QImage range_source(32, 32, QImage::Format_ARGB32);
  for (int y = 0; y < range_source.height(); ++y) {
    for (int x = 0; x < range_source.width(); ++x) {
      range_source.setPixelColor(
          x,
          y,
          x < 16 ? QColor(214, 42 + y, 48 + (x % 4))
                 : QColor(42 + (y % 4), 92, 214));
    }
  }
  if (!range_source.save(range_source_path)) {
    return fail("range outline source image should be writable");
  }

  photo_editor::desktop::EditorPreviewController color_range_controller;
  color_range_controller.loadAsset(
      QStringLiteral("outline-alpha-parity-color-range"),
      range_source_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  color_range_controller.setMaskPreviewMode(QStringLiteral("overlay"));
  color_range_controller.addMask(QStringLiteral("color"));
  if (!color_range_controller.sampleActiveColorRangeAt(0.25, 0.50)) {
    return fail("color range outline fixture should sample source pixels");
  }
  color_range_controller.setActiveMaskHueRange(1.0);
  color_range_controller.setActiveMaskSaturationMin(0.0);
  color_range_controller.setActiveMaskSaturationMax(1.0);
  color_range_controller.setActiveMaskRangeFeather(0.0);
  const auto color_range_trace = settled_range_outline_trace(color_range_controller);
  const auto color_range_paths = color_range_controller.activeMaskOutlinePaths();
  if (color_range_paths.isEmpty() ||
      !outline_points_are_image_normalized(color_range_paths) ||
      color_range_controller.activeMaskOutlineQualityTier() !=
          QStringLiteral("settled-alpha") ||
      !has_final_alpha_outline_contract(
          color_range_trace,
          QStringLiteral("procedural-geometry")) ||
      color_range_trace.value(QStringLiteral("outline_worker_source"), color_range_trace.value(QStringLiteral("outline_trace_source"))).toString() !=
          QStringLiteral("composite-alpha") ||
      color_range_trace.value(QStringLiteral("mask_source_kind")).toString() !=
          QStringLiteral("color") ||
      color_range_trace.value(QStringLiteral("mask_alpha_raster_width")).toInt() !=
          range_source.width() ||
      color_range_trace.value(QStringLiteral("mask_alpha_raster_height")).toInt() !=
          range_source.height()) {
    const auto detail =
        QStringLiteral(
            "color range outline should be traced from final procedural range alpha "
            "(paths=%1 tier=%2 source=%3 provider=%4 kind=%5 raster=%6x%7 expected=%8x%9)")
            .arg(color_range_paths.size())
            .arg(color_range_controller.activeMaskOutlineQualityTier())
            .arg(color_range_trace.value(QStringLiteral("outline_trace_source")).toString())
            .arg(color_range_trace.value(QStringLiteral("mask_alpha_provider")).toString())
            .arg(color_range_trace.value(QStringLiteral("mask_source_kind")).toString())
            .arg(color_range_trace.value(QStringLiteral("mask_alpha_raster_width")).toInt())
            .arg(color_range_trace.value(QStringLiteral("mask_alpha_raster_height")).toInt())
            .arg(range_source.width())
            .arg(range_source.height())
            .toUtf8();
    return fail(detail.constData());
  }
  if (!color_range_controller.applyActiveSelectionPixel(0.75, 0.50, QStringLiteral("add"))) {
    return fail("color range composite outline fixture should accept exact pixel add");
  }
  const auto color_composite_trace = settled_range_outline_trace(color_range_controller);
  const auto color_composite_paths = color_range_controller.activeMaskOutlinePaths();
  if (color_composite_paths.isEmpty() ||
      !outline_points_are_image_normalized(color_composite_paths) ||
      !has_final_alpha_outline_contract(
          color_composite_trace,
          QStringLiteral("composite")) ||
      color_composite_trace.value(QStringLiteral("outline_worker_source"), color_composite_trace.value(QStringLiteral("outline_trace_source"))).toString() !=
          QStringLiteral("composite-alpha") ||
      color_composite_trace.value(QStringLiteral("mask_source_kind")).toString() !=
          QStringLiteral("composite") ||
      color_composite_trace.value(QStringLiteral("mask_source_count")).toInt() != 2 ||
      color_composite_trace.value(QStringLiteral("mask_alpha_raster_width")).toInt() !=
          range_source.width() ||
      color_composite_trace.value(QStringLiteral("mask_alpha_raster_height")).toInt() !=
          range_source.height()) {
    return fail("range plus exact-pixel selection group outline should be traced from composite final alpha");
  }

  photo_editor::desktop::EditorPreviewController luminance_range_controller;
  luminance_range_controller.loadAsset(
      QStringLiteral("outline-alpha-parity-luminance-range"),
      range_source_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  luminance_range_controller.setMaskPreviewMode(QStringLiteral("overlay"));
  luminance_range_controller.addMask(QStringLiteral("luminance"));
  if (!luminance_range_controller.beginActiveLuminanceRangeSample(0.10, 0.10) ||
      !luminance_range_controller.commitActiveLuminanceRangeSample(0.44, 0.44)) {
    return fail("luminance range outline fixture should sample a source patch");
  }
  luminance_range_controller.setActiveMaskLuminanceMin(0.0);
  luminance_range_controller.setActiveMaskLuminanceMax(1.0);
  luminance_range_controller.setActiveMaskRangeFeather(0.0);
  const auto luminance_range_trace = settled_range_outline_trace(luminance_range_controller);
  const auto luminance_range_paths = luminance_range_controller.activeMaskOutlinePaths();
  if (luminance_range_paths.isEmpty() ||
      !outline_points_are_image_normalized(luminance_range_paths) ||
      luminance_range_controller.activeMaskOutlineQualityTier() !=
          QStringLiteral("settled-alpha") ||
      !has_final_alpha_outline_contract(
          luminance_range_trace,
          QStringLiteral("procedural-geometry")) ||
      luminance_range_trace.value(QStringLiteral("outline_worker_source"), luminance_range_trace.value(QStringLiteral("outline_trace_source"))).toString() !=
          QStringLiteral("composite-alpha") ||
      luminance_range_trace.value(QStringLiteral("mask_source_kind")).toString() !=
          QStringLiteral("luminance") ||
      luminance_range_trace.value(QStringLiteral("mask_alpha_raster_width")).toInt() !=
          range_source.width() ||
      luminance_range_trace.value(QStringLiteral("mask_alpha_raster_height")).toInt() !=
          range_source.height()) {
    const auto detail =
        QStringLiteral(
            "luminance range outline should be traced from final procedural range alpha "
            "(paths=%1 tier=%2 source=%3 provider=%4 kind=%5 raster=%6x%7 expected=%8x%9)")
            .arg(luminance_range_paths.size())
            .arg(luminance_range_controller.activeMaskOutlineQualityTier())
            .arg(luminance_range_trace.value(QStringLiteral("outline_trace_source")).toString())
            .arg(luminance_range_trace.value(QStringLiteral("mask_alpha_provider")).toString())
            .arg(luminance_range_trace.value(QStringLiteral("mask_source_kind")).toString())
            .arg(luminance_range_trace.value(QStringLiteral("mask_alpha_raster_width")).toInt())
            .arg(luminance_range_trace.value(QStringLiteral("mask_alpha_raster_height")).toInt())
            .arg(range_source.width())
            .arg(range_source.height())
            .toUtf8();
    return fail(detail.constData());
  }

  outline_cache_controller.setMaskPreviewMode(QStringLiteral("overlay"));
  const auto outline_before_subtract_brush = settled_outline_paths(outline_cache_controller);
  outline_cache_controller.startSelectionBrush(true);
  const auto outline_after_subtract_brush = settled_outline_paths(outline_cache_controller);
  if (outline_before_subtract_brush.isEmpty() ||
      outline_after_subtract_brush.isEmpty() ||
      outline_cache_controller.activeMaskKind() != QStringLiteral("brush") ||
      outline_cache_controller.activeMaskOperation() != QStringLiteral("subtract") ||
      outline_cache_controller.activeMaskToolRole() != QStringLiteral("selection")) {
    return fail("switching to a subtract brush should preserve the active selection set outline");
  }

  return EXIT_SUCCESS;
}
