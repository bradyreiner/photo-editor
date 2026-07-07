#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <iostream>
#include <memory>
#include <numeric>
#include <vector>

#include <QCoreApplication>
#include <QColor>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>
#include <QVariantMap>

#include "editor_preview_controller.h"
#include "editor_preview_image_sink.h"
#include "editor_preview_texture.h"
#include "observability.h"

namespace {

int fail(const QString& message) {
  std::cerr << message.toStdString() << '\n';
  return EXIT_FAILURE;
}

QString option_value(const QStringList& arguments, const QString& name) {
  const auto index = arguments.indexOf(name);
  if (index < 0 || index + 1 >= arguments.size()) {
    return {};
  }
  return arguments.at(index + 1).trimmed();
}

int option_int(const QStringList& arguments, const QString& name, int fallback) {
  const auto value = option_value(arguments, name);
  if (value.isEmpty()) {
    return fallback;
  }
  auto ok = false;
  const auto parsed = value.toInt(&ok);
  return ok ? parsed : fallback;
}

bool write_workflow_fixture(const QString& path) {
  QImage image(1600, 1200, QImage::Format_ARGB32);
  for (int y = 0; y < image.height(); ++y) {
    const auto yn = static_cast<double>(y) / static_cast<double>(std::max(1, image.height() - 1));
    for (int x = 0; x < image.width(); ++x) {
      const auto xn = static_cast<double>(x) / static_cast<double>(std::max(1, image.width() - 1));
      const auto ridge = std::sin((xn * 8.0 + yn * 5.0) * 3.14159265358979323846);
      image.setPixelColor(
          x,
          y,
          QColor::fromRgb(
              std::clamp(static_cast<int>(48 + xn * 156 + ridge * 18), 0, 255),
              std::clamp(static_cast<int>(72 + yn * 132 - ridge * 12), 0, 255),
              std::clamp(static_cast<int>(92 + (1.0 - xn) * 94 + yn * 28), 0, 255)));
    }
  }
  return image.save(path);
}

struct SampleSummary final {
  qint64 elapsed_ms = 0;
  double avg_ms = 0.0;
  double p50_ms = 0.0;
  double p90_ms = 0.0;
  double p95_ms = 0.0;
  double p99_ms = 0.0;
  double max_ms = 0.0;
};

double percentile(std::vector<double> sorted_samples, double percentile_value) {
  if (sorted_samples.empty()) {
    return 0.0;
  }
  std::sort(sorted_samples.begin(), sorted_samples.end());
  if (sorted_samples.size() == 1) {
    return sorted_samples.front();
  }
  const auto normalized = std::clamp(percentile_value, 0.0, 100.0) / 100.0;
  const auto scaled_index = normalized * static_cast<double>(sorted_samples.size() - 1);
  const auto lower_index = static_cast<std::size_t>(std::floor(scaled_index));
  const auto upper_index = std::min(lower_index + 1, sorted_samples.size() - 1);
  const auto fraction = scaled_index - static_cast<double>(lower_index);
  return sorted_samples.at(lower_index) +
         (sorted_samples.at(upper_index) - sorted_samples.at(lower_index)) * fraction;
}

SampleSummary summarize_samples(std::vector<double> samples) {
  SampleSummary summary;
  if (samples.empty()) {
    return summary;
  }
  const auto total = std::accumulate(samples.cbegin(), samples.cend(), 0.0);
  summary.elapsed_ms = static_cast<qint64>(total);
  summary.avg_ms = total / static_cast<double>(samples.size());
  summary.p50_ms = percentile(samples, 50.0);
  summary.p90_ms = percentile(samples, 90.0);
  summary.p95_ms = percentile(samples, 95.0);
  summary.p99_ms = percentile(samples, 99.0);
  summary.max_ms = *std::max_element(samples.cbegin(), samples.cend());
  return summary;
}

class BenchmarkImageSink final : public photo_editor::desktop::EditorPreviewImageSink {
 public:
  void setImage(const QString& key, const QImage& image) override {
    Q_UNUSED(key);
    last_image_size_ = image.size();
    ++set_count_;
  }

  void clearImage(const QString& key) override {
    Q_UNUSED(key);
  }

  [[nodiscard]] QSize lastImageSize() const { return last_image_size_; }
  [[nodiscard]] int setCount() const { return set_count_; }

 private:
  QSize last_image_size_;
  int set_count_ = 0;
};

class BenchmarkPreviewTexture final : public photo_editor::desktop::EditorPreviewTexture {
 public:
  [[nodiscard]] bool available() const override { return true; }
  [[nodiscard]] const char* backendName() const override { return "interaction-fake"; }
  bool upload(const QImage& image) override {
    if (image.isNull()) {
      return false;
    }
    texture_size_ = image.size();
    texture_format_name_ = image.format() == QImage::Format_RGBA16FPx4 ? "rgba16float" : "bgra8unorm";
    texture_bytes_per_pixel_ = image.format() == QImage::Format_RGBA16FPx4 ? 8 : 4;
    ++upload_count_;
    return true;
  }
  bool uploadRegion(const QImage& image, const QRect& region) override {
    if (image.isNull() || !texture_size_.isValid() || region.isEmpty()) {
      return false;
    }
    ++partial_upload_count_;
    return true;
  }
  bool uploadPatch(const QImage& image, const QRect& destination_region) override {
    if (image.isNull() || !texture_size_.isValid() || destination_region.isEmpty()) {
      return false;
    }
    ++partial_upload_count_;
    ++patch_upload_count_;
    return true;
  }
  void clear() override { texture_size_ = {}; }
  [[nodiscard]] bool hasNativeTexture() const override { return texture_size_.isValid(); }
  [[nodiscard]] quint64 nativeTextureHandle() const override { return hasNativeTexture() ? 1 : 0; }
  [[nodiscard]] QSize nativeTextureSize() const override { return texture_size_; }
  [[nodiscard]] const char* nativeTextureFormatName() const override {
    return hasNativeTexture() ? texture_format_name_ : "none";
  }
  [[nodiscard]] int nativeTextureBytesPerPixel() const override {
    return hasNativeTexture() ? texture_bytes_per_pixel_ : 0;
  }
  [[nodiscard]] int uploadCount() const { return upload_count_; }
  [[nodiscard]] int partialUploadCount() const { return partial_upload_count_; }
  [[nodiscard]] int patchUploadCount() const { return patch_upload_count_; }

 private:
  QSize texture_size_;
  const char* texture_format_name_ = "none";
  int texture_bytes_per_pixel_ = 0;
  int upload_count_ = 0;
  int partial_upload_count_ = 0;
  int patch_upload_count_ = 0;
};

}  // namespace

namespace photo_editor::desktop {

struct EditorPreviewControllerTestAccess {
  static bool full_render_timer_active(const EditorPreviewController& controller) {
    return controller.full_render_timer_.isActive();
  }

  static bool full_render_pending(const EditorPreviewController& controller) {
    return controller.full_request_pending_;
  }

  static bool full_render_running(const EditorPreviewController& controller) {
    return controller.full_render_watcher_.isRunning();
  }

  static bool interactive_render_timer_active(const EditorPreviewController& controller) {
    return controller.interactive_render_timer_.isActive();
  }

  static bool interactive_render_running(const EditorPreviewController& controller) {
    return controller.interactive_render_watcher_.isRunning();
  }

  static bool interactive_render_pending(const EditorPreviewController& controller) {
    return controller.interactive_request_pending_;
  }

  static bool photo_layer_adjustment_render_timer_active(
      const EditorPreviewController& controller) {
    return controller.photo_layer_adjustment_render_timer_.isActive();
  }

  static int pending_brush_commit_count(const EditorPreviewController& controller) {
    return static_cast<int>(controller.pending_brush_commits_.size()) +
           (controller.pending_brush_commit_scheduled_ ? 1 : 0) +
           (controller.brush_raster_commit_watcher_.isRunning() ? 1 : 0) +
           (controller.brush_raster_commit_active_ ? 1 : 0);
  }

  static QVariantMap last_mask_work_telemetry(const EditorPreviewController& controller) {
    return controller.last_mask_work_telemetry_;
  }

  static void set_display_preview_texture(
      EditorPreviewController& controller,
      std::unique_ptr<EditorPreviewTexture> texture) {
    controller.display_preview_texture_ = std::move(texture);
    controller.display_preview_native_revision_ = 0;
    controller.display_preview_native_rendered_cache_key_.clear();
  }
};

}  // namespace photo_editor::desktop

namespace {

struct PreviewIdleWaitResult final {
  bool ready = false;
  int elapsed_ms = 0;
  int interactive_timer_ms = 0;
  int interactive_running_ms = 0;
  int interactive_pending_ms = 0;
  int layer_adjustment_timer_ms = 0;
  bool final_interactive_timer_active = false;
  bool final_interactive_running = false;
  bool final_interactive_pending = false;
  bool final_layer_adjustment_timer_active = false;
  bool final_full_timer_active = false;
  bool final_full_running = false;
  bool final_full_pending = false;
};

PreviewIdleWaitResult wait_for_visible_preview_idle(
    photo_editor::desktop::EditorPreviewController& controller,
    int timeout_ms) {
  PreviewIdleWaitResult result;
  auto elapsed_ms = 0;
  while (elapsed_ms < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    const auto interactive_timer_active =
        photo_editor::desktop::EditorPreviewControllerTestAccess::
            interactive_render_timer_active(controller);
    const auto interactive_running =
        photo_editor::desktop::EditorPreviewControllerTestAccess::
            interactive_render_running(controller);
    const auto interactive_pending =
        photo_editor::desktop::EditorPreviewControllerTestAccess::
            interactive_render_pending(controller);
    const auto layer_adjustment_timer_active =
        photo_editor::desktop::EditorPreviewControllerTestAccess::
            photo_layer_adjustment_render_timer_active(controller);
    if (!interactive_timer_active &&
        !interactive_running &&
        !interactive_pending &&
        !layer_adjustment_timer_active) {
      result.ready = true;
      result.elapsed_ms = elapsed_ms;
      result.final_full_timer_active =
          photo_editor::desktop::EditorPreviewControllerTestAccess::
              full_render_timer_active(controller);
      result.final_full_running =
          photo_editor::desktop::EditorPreviewControllerTestAccess::
              full_render_running(controller);
      result.final_full_pending =
          photo_editor::desktop::EditorPreviewControllerTestAccess::
              full_render_pending(controller);
      return result;
    }
    QThread::msleep(25);
    elapsed_ms += 25;
    result.elapsed_ms = elapsed_ms;
    if (interactive_timer_active) {
      result.interactive_timer_ms += 25;
    }
    if (interactive_running) {
      result.interactive_running_ms += 25;
    }
    if (interactive_pending) {
      result.interactive_pending_ms += 25;
    }
    if (layer_adjustment_timer_active) {
      result.layer_adjustment_timer_ms += 25;
    }
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  result.final_interactive_timer_active =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          interactive_render_timer_active(controller);
  result.final_interactive_running =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          interactive_render_running(controller);
  result.final_interactive_pending =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          interactive_render_pending(controller);
  result.final_layer_adjustment_timer_active =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          photo_layer_adjustment_render_timer_active(controller);
  result.final_full_timer_active =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          full_render_timer_active(controller);
  result.final_full_running =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          full_render_running(controller);
  result.final_full_pending =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          full_render_pending(controller);
  result.ready =
      !result.final_interactive_timer_active &&
      !result.final_interactive_running &&
      !result.final_interactive_pending &&
      !result.final_layer_adjustment_timer_active;
  return result;
}

bool wait_for_brush_commit(
    photo_editor::desktop::EditorPreviewController& controller,
    int timeout_ms) {
  auto elapsed_ms = 0;
  while (elapsed_ms < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    if (photo_editor::desktop::EditorPreviewControllerTestAccess::pending_brush_commit_count(
            controller) == 0) {
      return true;
    }
    QThread::msleep(25);
    elapsed_ms += 25;
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  return photo_editor::desktop::EditorPreviewControllerTestAccess::pending_brush_commit_count(
             controller) == 0;
}

void log_stage(
    const QString& stage,
    qint64 elapsed_ms,
    int iteration,
    const QVariantMap& extra = {}) {
  auto fields = extra;
  fields.insert(QStringLiteral("workflow"), QStringLiteral("interaction"));
  fields.insert(QStringLiteral("stage"), stage);
  fields.insert(QStringLiteral("iteration"), iteration);
  fields.insert(QStringLiteral("elapsed_ms"), elapsed_ms);
  photo_editor::desktop::observability::logInfo(
      photoEditorPerfLog(),
      QStringLiteral("benchmark.interaction_workflow.stage_completed"),
      fields);
}

void log_summary(
    const QString& stage,
    const SampleSummary& summary,
    int iterations,
    const QVariantMap& extra = {}) {
  auto fields = extra;
  fields.insert(QStringLiteral("workflow"), QStringLiteral("interaction"));
  fields.insert(QStringLiteral("stage"), stage);
  fields.insert(QStringLiteral("iterations"), iterations);
  fields.insert(QStringLiteral("elapsed_ms"), summary.p50_ms);
  fields.insert(QStringLiteral("elapsed_total_ms"), summary.elapsed_ms);
  fields.insert(QStringLiteral("avg_ms"), summary.avg_ms);
  fields.insert(QStringLiteral("p50_ms"), summary.p50_ms);
  fields.insert(QStringLiteral("p90_ms"), summary.p90_ms);
  fields.insert(QStringLiteral("p95_ms"), summary.p95_ms);
  fields.insert(QStringLiteral("p99_ms"), summary.p99_ms);
  fields.insert(QStringLiteral("max_ms"), summary.max_ms);
  photo_editor::desktop::observability::logInfo(
      photoEditorPerfLog(),
      QStringLiteral("benchmark.interaction_workflow.stage_summary"),
      fields);
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const auto arguments = app.arguments();
  const auto iterations = std::max(1, option_int(arguments, QStringLiteral("--iterations"), 5));
  const auto timeout_ms =
      std::max(1000, option_int(arguments, QStringLiteral("--timeout-ms"), 15000));
  auto source_path = option_value(arguments, QStringLiteral("--source"));

  QTemporaryDir fixture_dir;
  if (source_path.isEmpty()) {
    if (!fixture_dir.isValid()) {
      return fail(QStringLiteral("Unable to create interaction workflow fixture directory."));
    }
    source_path = fixture_dir.filePath(QStringLiteral("interaction-workflow-source.png"));
    if (!write_workflow_fixture(source_path)) {
      return fail(QStringLiteral("Unable to create interaction workflow fixture image."));
    }
  } else if (!QFileInfo::exists(source_path)) {
    return fail(QStringLiteral("Interaction workflow source does not exist: %1").arg(source_path));
  }

  photo_editor::desktop::observability::initialize();
  photo_editor::desktop::observability::logInfo(
      photoEditorPerfLog(),
      QStringLiteral("benchmark.interaction_workflow.started"),
      {
          {QStringLiteral("workflow"), QStringLiteral("interaction")},
          {QStringLiteral("elapsed_ms"), 0},
          {QStringLiteral("iterations"), iterations},
          {QStringLiteral("source"), source_path},
      });

  BenchmarkImageSink image_sink;
  photo_editor::desktop::EditorPreviewController controller;
  controller.setImageProvider(&image_sink);
  controller.setPreviewTargetEdge(960);
  photo_editor::desktop::EditorPreviewControllerTestAccess::set_display_preview_texture(
      controller,
      std::make_unique<BenchmarkPreviewTexture>());

  std::vector<double> open_samples;
  std::vector<double> brush_page_samples;
  std::vector<double> first_feedback_samples;
  std::vector<double> stroke_commit_samples;
  std::vector<double> tool_draft_samples;
  std::vector<double> total_samples;
  open_samples.reserve(static_cast<std::size_t>(iterations));
  brush_page_samples.reserve(static_cast<std::size_t>(iterations));
  first_feedback_samples.reserve(static_cast<std::size_t>(iterations));
  stroke_commit_samples.reserve(static_cast<std::size_t>(iterations));
  tool_draft_samples.reserve(static_cast<std::size_t>(iterations));
  total_samples.reserve(static_cast<std::size_t>(iterations));

  QElapsedTimer suite_timer;
  suite_timer.start();
  for (int iteration = 0; iteration < iterations; ++iteration) {
    QElapsedTimer total_timer;
    total_timer.start();
    const auto asset_id = QStringLiteral("interaction-workflow-%1").arg(iteration + 1);

    QElapsedTimer open_timer;
    open_timer.start();
    controller.loadAsset(
        asset_id,
        source_path,
        QString(),
        QStringLiteral("Benchmark Camera"),
        QStringLiteral("Jul 4, 2026"),
        QStringLiteral("Benchmark 35mm"),
        QString());
    const auto open_idle_wait = wait_for_visible_preview_idle(controller, timeout_ms);
    if (!open_idle_wait.ready) {
      return fail(QStringLiteral("Timed out opening benchmark photo."));
    }
    open_samples.push_back(static_cast<double>(open_timer.elapsed()));
    log_stage(
        QStringLiteral("open_photo_to_first_preview"),
        open_timer.elapsed(),
        iteration,
        {
            {QStringLiteral("asset_id"), asset_id},
            {QStringLiteral("image_width"), image_sink.lastImageSize().width()},
            {QStringLiteral("image_height"), image_sink.lastImageSize().height()},
            {QStringLiteral("image_publish_count"), image_sink.setCount()},
            {QStringLiteral("idle_wait_ms"), open_idle_wait.elapsed_ms},
            {QStringLiteral("idle_wait_interactive_timer_ms"),
             open_idle_wait.interactive_timer_ms},
            {QStringLiteral("idle_wait_interactive_running_ms"),
             open_idle_wait.interactive_running_ms},
            {QStringLiteral("idle_wait_interactive_pending_ms"),
             open_idle_wait.interactive_pending_ms},
            {QStringLiteral("idle_wait_layer_adjustment_timer_ms"),
             open_idle_wait.layer_adjustment_timer_ms},
            {QStringLiteral("idle_final_full_timer_active"),
             open_idle_wait.final_full_timer_active},
            {QStringLiteral("idle_final_full_running"),
             open_idle_wait.final_full_running},
            {QStringLiteral("idle_final_full_pending"),
             open_idle_wait.final_full_pending},
        });

    QElapsedTimer brush_page_timer;
    brush_page_timer.start();
    controller.startSelectionBrush(false);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    if (controller.activeMaskKind() != QStringLiteral("brush")) {
      return fail(QStringLiteral("Brush page workflow did not activate a brush mask."));
    }
    brush_page_samples.push_back(static_cast<double>(brush_page_timer.elapsed()));
    log_stage(
        QStringLiteral("brush_page_to_controls_ready"),
        brush_page_timer.elapsed(),
        iteration,
        {
            {QStringLiteral("asset_id"), asset_id},
            {QStringLiteral("brush_kind"), controller.activeMaskKind()},
            {QStringLiteral("brush_size"), controller.activeMaskBrushSize()},
            {QStringLiteral("brush_flow"), controller.activeMaskBrushFlow()},
        });

    controller.beginActiveBrushStroke();
    QElapsedTimer feedback_timer;
    feedback_timer.start();
    controller.addBrushPoint(0.31, 0.36);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    first_feedback_samples.push_back(static_cast<double>(feedback_timer.elapsed()));
    log_stage(
        QStringLiteral("brush_pointer_down_to_live_feedback"),
        feedback_timer.elapsed(),
        iteration,
        {
            {QStringLiteral("asset_id"), asset_id},
            {QStringLiteral("live_preview_elapsed_ms"), controller.activeMaskLiveBrushPreviewElapsedMs()},
            {QStringLiteral("live_preview_backend"), controller.activeMaskLiveBrushPreviewBackend()},
            {QStringLiteral("live_preview_within_latency_budget"),
             controller.activeMaskLiveBrushPreviewWithinLatencyBudget()},
        });
    controller.addBrushPoint(0.39, 0.43);
    controller.addBrushPoint(0.48, 0.51);
    controller.addBrushPoint(0.56, 0.60);
    controller.addBrushPoint(0.64, 0.67);

    QElapsedTimer commit_timer;
    commit_timer.start();
    controller.endActiveBrushStroke();
    if (!wait_for_brush_commit(controller, timeout_ms)) {
      return fail(QStringLiteral("Timed out committing benchmark brush stroke."));
    }
    stroke_commit_samples.push_back(static_cast<double>(commit_timer.elapsed()));
    const auto mask_telemetry =
        photo_editor::desktop::EditorPreviewControllerTestAccess::last_mask_work_telemetry(controller);
    log_stage(
        QStringLiteral("brush_release_to_committed_mask"),
        commit_timer.elapsed(),
        iteration,
        {
            {QStringLiteral("asset_id"), asset_id},
            {QStringLiteral("active_mask_index"), controller.activeMaskIndex()},
            {QStringLiteral("active_mask_count"), controller.maskStack().size()},
            {QStringLiteral("mask_raster_ms"),
             mask_telemetry.value(QStringLiteral("mask_raster_ms")).toLongLong()},
            {QStringLiteral("dirty_region_area"),
             mask_telemetry.value(QStringLiteral("dirty_region_area")).toDouble()},
        });

    QElapsedTimer draft_timer;
    draft_timer.start();
    controller.setExposure(iteration % 2 == 0 ? 0.26 : 0.12);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    tool_draft_samples.push_back(static_cast<double>(draft_timer.elapsed()));
    log_stage(
        QStringLiteral("tool_adjust_to_draft_scheduled"),
        draft_timer.elapsed(),
        iteration,
        {
            {QStringLiteral("asset_id"), asset_id},
            {QStringLiteral("tool"), QStringLiteral("exposure")},
        });

    total_samples.push_back(static_cast<double>(total_timer.elapsed()));
    log_stage(
        QStringLiteral("total"),
        total_timer.elapsed(),
        iteration,
        {
            {QStringLiteral("asset_id"), asset_id},
        });
  }

  log_summary(
      QStringLiteral("open_photo_to_first_preview"),
      summarize_samples(open_samples),
      iterations);
  log_summary(
      QStringLiteral("brush_page_to_controls_ready"),
      summarize_samples(brush_page_samples),
      iterations);
  log_summary(
      QStringLiteral("brush_pointer_down_to_live_feedback"),
      summarize_samples(first_feedback_samples),
      iterations);
  log_summary(
      QStringLiteral("brush_release_to_committed_mask"),
      summarize_samples(stroke_commit_samples),
      iterations);
  log_summary(
      QStringLiteral("tool_adjust_to_draft_scheduled"),
      summarize_samples(tool_draft_samples),
      iterations);
  log_summary(QStringLiteral("total"), summarize_samples(total_samples), iterations);

  photo_editor::desktop::observability::logInfo(
      photoEditorPerfLog(),
      QStringLiteral("benchmark.interaction_workflow.completed"),
      {
          {QStringLiteral("workflow"), QStringLiteral("interaction")},
          {QStringLiteral("iterations"), iterations},
          {QStringLiteral("elapsed_ms"), suite_timer.elapsed()},
          {QStringLiteral("open_photo_p95_ms"), summarize_samples(open_samples).p95_ms},
          {QStringLiteral("brush_page_p95_ms"), summarize_samples(brush_page_samples).p95_ms},
          {QStringLiteral("first_feedback_p95_ms"), summarize_samples(first_feedback_samples).p95_ms},
          {QStringLiteral("brush_commit_p95_ms"), summarize_samples(stroke_commit_samples).p95_ms},
          {QStringLiteral("tool_draft_p95_ms"), summarize_samples(tool_draft_samples).p95_ms},
          {QStringLiteral("total_p95_ms"), summarize_samples(total_samples).p95_ms},
      });

  std::cout << "interaction_workflow_iterations=" << iterations << '\n';
  return EXIT_SUCCESS;
}
