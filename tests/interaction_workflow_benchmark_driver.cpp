#include <algorithm>
#include <array>
#include <cstdlib>
#include <cmath>
#include <iostream>
#include <memory>
#include <numeric>
#include <vector>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QColor>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>
#include <QVariantMap>

#include "editor_preview_controller.h"
#include "edit_adjustment_patch.h"
#include "editor_preview_image_sink.h"
#include "editor_preview_texture.h"
#include "raw_image_decoder.h"
#include "observability.h"
#include "source_image_decode.h"
#include "native_preview_readback.h"

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

bool write_workflow_fixture(const QString& path, bool small = false) {
  QImage image(small ? 16 : 1600, small ? 16 : 1200, QImage::Format_ARGB32);
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
  explicit BenchmarkImageSink(bool retain_edited = false) : retain_edited_(retain_edited) {}
  void setImage(const QString& key, const QImage& image) override {
    if (retain_edited_ && key == QStringLiteral("edited")) edited_image_ = image;
    last_image_size_ = image.size();
    ++set_count_;
  }

  void clearImage(const QString& key) override {
    if (key == QStringLiteral("edited")) edited_image_ = {};
  }

  [[nodiscard]] QImage editedImage() const { return edited_image_; }
  [[nodiscard]] QSize lastImageSize() const { return last_image_size_; }
  [[nodiscard]] int setCount() const { return set_count_; }

 private:
  bool retain_edited_ = false;
  QImage edited_image_;
  QSize last_image_size_;
  int set_count_ = 0;
};

class BenchmarkPreviewTexture final : public photo_editor::desktop::EditorPreviewTexture {
 public:
  [[nodiscard]] bool available() const override { return true; }
  [[nodiscard]] const char* backendName() const override { return "interaction-fake"; }
  bool ensureNativeTexture(const QSize& size, QImage::Format format) override {
    if (!size.isValid() || size.width() <= 0 || size.height() <= 0) {
      return false;
    }
    texture_size_ = size;
    texture_format_name_ = format == QImage::Format_RGBA16FPx4 ? "rgba16float" : "bgra8unorm";
    texture_bytes_per_pixel_ = format == QImage::Format_RGBA16FPx4 ? 8 : 4;
    return true;
  }
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

RenderResult render_preview_request_for_test(const RenderRequest& request);

struct EditorPreviewControllerTestAccess {
  static bool full_render_timer_active(const EditorPreviewController& controller) {
    return controller.render_scheduler_.fullActive();
  }

  static bool full_render_pending(const EditorPreviewController& controller) {
    return controller.full_request_pending_;
  }

  static bool full_render_running(const EditorPreviewController& controller) {
    return controller.full_render_watcher_.isRunning();
  }

  static bool interactive_render_timer_active(const EditorPreviewController& controller) {
    return controller.render_scheduler_.interactiveActive();
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
           (controller.mask_worker_coordinator_.brushRasterCommitWatcher().isRunning() ? 1 : 0) +
           (controller.brush_raster_commit_active_ ? 1 : 0);
  }

  static int render_generation(const EditorPreviewController& controller) {
    return controller.preview_session_.generation();
  }

  static int last_full_preview_generation(const EditorPreviewController& controller) {
    return controller.preview_session_.lastFullGeneration();
  }

  static int interactive_render_cancelled_count(const EditorPreviewController& controller) {
    return controller.interactive_render_cancelled_count_;
  }

  static int full_render_cancelled_count(const EditorPreviewController& controller) {
    return controller.full_render_cancelled_count_;
  }

  static bool raw_decode_pyramid_resident(const EditorPreviewController& controller) {
    const auto state = controller.currentState();
    return source_image_decode_cache_contains(controller.file_path_, state, 1024) &&
           source_image_decode_cache_contains(controller.file_path_, state, 2560);
  }

  static QVariantMap last_mask_work_telemetry(const EditorPreviewController& controller) {
    return controller.last_mask_work_telemetry_;
  }

  static QImage native_pixels(EditorPreviewController& controller) {
    return read_native_preview_texture(controller.display_preview_texture_.get());
  }

  static bool redundant_viewport_starts_worker(EditorPreviewController& controller) {
    auto request = controller.currentRenderRequest(true);
    request.visible_tile_only = true;
    request.render_reason = QStringLiteral("viewport_change");
    controller.startRenderStage(true, request);
    return controller.full_render_watcher_.isRunning() || controller.full_request_pending_;
  }

  static RenderRequest interactive_request(const EditorPreviewController& controller) {
    return controller.currentRenderRequest(false);
  }

  static void inject_mask_failure(EditorPreviewController& controller, const QString& source) {
    RenderResult result;
    result.file_path = source;
    result.generation = controller.preview_session_.generation();
    result.dirty_domain = QStringLiteral("mask_rasterization");
    result.dirty_stages = {QStringLiteral("mask_rasterization")};
    result.render_mode = QStringLiteral("live_preview");
    controller.handleRenderResult(false, result);
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

bool wait_for_all_preview_idle(
    photo_editor::desktop::EditorPreviewController& controller,
    int timeout_ms) {
  auto elapsed_ms = 0;
  while (elapsed_ms < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    if (!photo_editor::desktop::EditorPreviewControllerTestAccess::
             interactive_render_timer_active(controller) &&
        !photo_editor::desktop::EditorPreviewControllerTestAccess::
             interactive_render_running(controller) &&
        !photo_editor::desktop::EditorPreviewControllerTestAccess::
             interactive_render_pending(controller) &&
        !photo_editor::desktop::EditorPreviewControllerTestAccess::
             photo_layer_adjustment_render_timer_active(controller) &&
        !photo_editor::desktop::EditorPreviewControllerTestAccess::
             full_render_timer_active(controller) &&
        !photo_editor::desktop::EditorPreviewControllerTestAccess::
             full_render_running(controller) &&
        !photo_editor::desktop::EditorPreviewControllerTestAccess::
             full_render_pending(controller) &&
        photo_editor::desktop::EditorPreviewControllerTestAccess::
                pending_brush_commit_count(controller) == 0) {
      return true;
    }
    QThread::msleep(10);
    elapsed_ms += 10;
  }
  return false;
}

bool wait_for_preview_revision_after(
    photo_editor::desktop::EditorPreviewController& controller,
    int previous_revision,
    int timeout_ms) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    if (controller.displayPreviewNativeRevision() > previous_revision) {
      return true;
    }
    QThread::yieldCurrentThread();
  }
  return controller.displayPreviewNativeRevision() > previous_revision;
}

bool wait_for_raw_decode_pyramid(
    photo_editor::desktop::EditorPreviewController& controller,
    int timeout_ms) {
  auto elapsed_ms = 0;
  while (elapsed_ms < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    if (photo_editor::desktop::EditorPreviewControllerTestAccess::
            raw_decode_pyramid_resident(controller)) {
      return true;
    }
    QThread::msleep(1);
    ++elapsed_ms;
  }
  return photo_editor::desktop::EditorPreviewControllerTestAccess::
      raw_decode_pyramid_resident(controller);
}

bool wait_for_color_transform_prewarm(
    photo_editor::desktop::EditorPreviewController& controller,
    int timeout_ms) {
  auto elapsed_ms = 0;
  while (elapsed_ms < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    const auto prewarm = controller.colorTransformDiagnostics()
                             .value(QStringLiteral("transformPrewarm"))
                             .toMap();
    if (prewarm.value(QStringLiteral("pending")).toInt() == 0) {
      return true;
    }
    QThread::msleep(1);
    ++elapsed_ms;
  }
  return controller.colorTransformDiagnostics()
             .value(QStringLiteral("transformPrewarm"))
             .toMap()
             .value(QStringLiteral("pending"))
             .toInt() == 0;
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
  const bool initial_selection_retry = arguments.contains(QStringLiteral("--initial-selection-retry"));
  const bool global_color_preview = arguments.contains(QStringLiteral("--global-color-preview"));
  const bool global_mask_preview = arguments.contains(QStringLiteral("--global-mask-preview")) || global_color_preview;
  const bool local_opacity = arguments.contains(QStringLiteral("--local-opacity"));
  const bool local_expansion = arguments.contains(QStringLiteral("--local-expansion"));
  const bool local_temperature = arguments.contains(QStringLiteral("--local-temperature")) ||
      local_opacity || local_expansion || global_mask_preview;
  QTemporaryDir local_runtime;
  if (local_temperature || initial_selection_retry) {
    if (!local_runtime.isValid()) return fail(QStringLiteral("Cannot isolate local-mask benchmark state."));
    qputenv("PHOTO_EDITOR_APP_DATA_DIR", local_runtime.path().toUtf8());
    qputenv("PHOTO_EDITOR_CACHE_DIR", local_runtime.filePath("cache").toUtf8());
    qputenv("PHOTO_EDITOR_DATABASE_URL", "postgresql://127.0.0.1:1/framehouse_mask_proof");
    qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, local_runtime.filePath("preferences"));
  }
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
    if (!write_workflow_fixture(source_path, arguments.contains(QStringLiteral("--small-fixture")))) {
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

  BenchmarkImageSink image_sink(initial_selection_retry);
  photo_editor::desktop::EditorPreviewController controller;
  controller.setImageProvider(&image_sink);
  controller.setPreviewTargetEdge(2560);
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_BENCHMARK_SCOPES_VISIBLE")) {
    controller.setScopesVisible(qEnvironmentVariableIntValue("PHOTO_EDITOR_BENCHMARK_SCOPES_VISIBLE") != 0);
  }
  const auto native_preview = qEnvironmentVariableIntValue("PHOTO_EDITOR_BENCHMARK_NATIVE_PREVIEW") == 1;
  if (local_temperature && !native_preview)
    return fail(QStringLiteral("Local-mask interaction proof requires a real native preview texture."));
  std::unique_ptr<photo_editor::desktop::EditorPreviewTexture> preview_texture;
  if (native_preview) {
    preview_texture = photo_editor::desktop::EditorPreviewTexture::create();
    if (!preview_texture || !preview_texture->available() ||
        !preview_texture->ensureNativeTexture(QSize(1, 1), QImage::Format_ARGB32) ||
        !preview_texture->supportsNativeOutputSurface()) {
      return fail(QStringLiteral("Native-preview benchmark requires an available GPU texture backend."));
    }
  } else if (!initial_selection_retry) {
    preview_texture = std::make_unique<BenchmarkPreviewTexture>();
  }
  std::cout << "preview_texture_backend=" << (preview_texture ? preview_texture->backendName() : "image-provider") << '\n';
  std::cout << "scopes_visible=" << controller.scopesVisible() << '\n';
  photo_editor::desktop::EditorPreviewControllerTestAccess::set_display_preview_texture(
      controller, std::move(preview_texture));

  if (initial_selection_retry) {
    using Access = photo_editor::desktop::EditorPreviewControllerTestAccess;
    QElapsedTimer proof_timer;
    proof_timer.start();
    if (arguments.contains(QStringLiteral("--warm-first"))) {
      auto warmer = std::make_unique<photo_editor::desktop::EditorPreviewController>();
      warmer->loadAsset(QStringLiteral("initial-selection-cache-warm"), source_path,
          QString(), QString(), QString(), QString(), QString());
      if (!wait_for_all_preview_idle(*warmer, timeout_ms))
        return fail(QStringLiteral("Cannot warm the initial-selection disk cache."));
    }
    controller.loadAsset(QStringLiteral("initial-selection-retry"), source_path,
        QString(), QString(), QString(), QString(), QString());
    // Start the tool before processing any of the initial render events.
    controller.setMaskPreviewMode(QStringLiteral("overlay"));
    controller.startSelectionBrush(false);
    if (!wait_for_all_preview_idle(controller, timeout_ms))
      return fail(QStringLiteral("Initial selection preview did not settle."));
    const auto pixels = [&] {
      return (native_preview ? Access::native_pixels(controller) : image_sink.editedImage())
          .convertToFormat(QImage::Format_RGBA8888);
    };
    const auto before = pixels();
    const auto source = controller.displayPreviewSource();
    const auto active_index = controller.activeMaskIndex();
    std::cout << "initial_native_available=" << controller.displayPreviewNativeAvailable() << '\n'
              << "initial_provider_available=" << !image_sink.editedImage().isNull() << '\n';
    if (before.isNull() || !controller.previewFailureDetail().isEmpty())
      return fail(QStringLiteral("Starting a selection during load did not publish usable pixels."));
    const auto tool_preserved = [&] {
      return controller.activeMaskKind() == QStringLiteral("brush") &&
          controller.activeMaskToolRole() == QStringLiteral("selection") &&
          controller.activeMaskIndex() == active_index &&
          controller.maskPreviewMode() == QStringLiteral("overlay");
    };
    Access::inject_mask_failure(controller, source_path);
    if (controller.previewFailureStage() != QStringLiteral("mask_alpha") ||
        pixels() != before || controller.displayPreviewSource() != source || !tool_preserved())
      return fail(QStringLiteral("Mask render failure lost the last-good photograph or tool context."));
    const auto retry_accepted = controller.retryPreviewRender();
    const auto retry_idle = wait_for_all_preview_idle(controller, timeout_ms);
    const auto after = pixels();
    int max_delta = 0;
    if (!after.isNull() && after.size() == before.size()) {
      for (int y = 0; y < before.height(); ++y)
        for (int x = 0; x < before.width() * 4; ++x)
          max_delta = std::max(max_delta, std::abs(int(before.constScanLine(y)[x]) - int(after.constScanLine(y)[x])));
    } else max_delta = 255;
    std::cout << "retry_accepted=" << retry_accepted << " idle=" << retry_idle
              << " failure=" << controller.previewFailureDetail().toStdString()
              << " tool_preserved=" << tool_preserved()
              << " before=" << before.width() << 'x' << before.height()
              << " after=" << after.width() << 'x' << after.height()
              << " max_delta=" << max_delta
              << " full_generation=" << Access::last_full_preview_generation(controller)
              << " generation=" << Access::render_generation(controller) << '\n';
    if (!retry_accepted || !retry_idle ||
        !controller.previewFailureDetail().isEmpty() || !tool_preserved() || after != before ||
        Access::last_full_preview_generation(controller) != Access::render_generation(controller))
      return fail(QStringLiteral("Retry did not restore current full-quality pixels and tool context."));
    const auto idle_generation = Access::render_generation(controller);
    controller.startSelectionBrush(false);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    if (Access::render_generation(controller) != idle_generation || pixels() != after)
      return fail(QStringLiteral("Starting a selection on an idle photograph must not rerender or change its pixels."));
    std::cout << "initial_selection_retry=passed\n";
    const auto elapsed = proof_timer.elapsed();
    log_stage(QStringLiteral("initial_selection_retry"), elapsed, 0,
        {{"native", native_preview}, {"maxDelta", max_delta},
         {"width", after.width()}, {"height", after.height()},
         {"toolPreserved", true}, {"idleStartPreserved", true},
         {"fullGeneration", Access::last_full_preview_generation(controller)}});
    log_summary(QStringLiteral("initial_selection_retry"), summarize_samples({double(elapsed)}), 1);
    return EXIT_SUCCESS;
  }

  if (local_temperature) {
    // Exercise the real controller's mask setter, scheduler and native texture
    // publication. This measures handoff, not an exposed QML frame or photons.
    using Access = photo_editor::desktop::EditorPreviewControllerTestAccess;
    const auto control = local_opacity ? QStringLiteral("opacity") :
        local_expansion ? QStringLiteral("expansion") : QStringLiteral("temperature");
    const auto stage_prefix = QStringLiteral("local_") + control;
    const auto idle = [&] {
      return !Access::interactive_render_timer_active(controller) &&
          !Access::interactive_render_running(controller) &&
          !Access::interactive_render_pending(controller) &&
          !Access::photo_layer_adjustment_render_timer_active(controller) &&
          !Access::full_render_timer_active(controller) &&
          !Access::full_render_running(controller) &&
          !Access::full_render_pending(controller) &&
          Access::pending_brush_commit_count(controller) == 0;
    };
    const auto await = [&](const auto& ready) {
      QElapsedTimer deadline; deadline.start();
      while (!ready() && deadline.elapsed() < timeout_ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(1);
      }
      return ready();
    };
    photo_editor::desktop::EditRecipeState state;
    state.photographic_rendering_model = QStringLiteral("aces2-ocio-v1");
    const bool cropped = arguments.contains(QStringLiteral("--local-crop"));
    if (cropped) {
      state.crop_rect_x = .1; state.crop_rect_y = .1;
      state.crop_rect_width = .8; state.crop_rect_height = .8;
    }
    QByteArray alpha(256, '\0');
    for (int y = 0; y < 16; ++y) for (int x = 0; x < 8; ++x) alpha[y * 16 + x] = char(0xff);
    state.mask_stack = {QVariantMap{{"id", "local-temperature-benchmark"}, {"kind", "background"},
        {"enabled", true}, {"operation", "add"}, {"temperature", 12.0}, {"opacity", 1.0},
        {"density", 1.0}, {"feather", 0.0}, {"segmentationWidth", 16}, {"segmentationHeight", 16},
        {"segmentationData", alpha}}};
    state.active_mask_index = 0;
    if (global_color_preview) {
      state.mask_stack.clear();
      state.active_mask_index = -1;
    }
    const auto recipe_file = option_value(arguments, QStringLiteral("--recipe-file"));
    if (!recipe_file.isEmpty()) {
      QFile input(recipe_file);
      if (!global_color_preview || !input.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("Recipe-file proof requires a readable recipe and global color mode."));
      state = photo_editor::desktop::parse_edit_recipe_json(QString::fromUtf8(input.readAll()));
    }
    controller.loadAsset(QStringLiteral("interaction-local-temperature"), source_path, QString(),
        QStringLiteral("Benchmark Camera"), QString(), QString(),
        photo_editor::desktop::serialize_adjustment_patch_json(state,
            photo_editor::desktop::all_adjustment_groups(),
            photo_editor::desktop::AdjustmentPatchScope::AssetBound));
    if (!await([&] { return idle() && controller.displayPreviewNativeRevision() > 0; }))
      return fail(QStringLiteral("Local-mask initial render failed: ") + controller.previewFailureDetail());
    if (!global_color_preview) controller.setActiveMaskIndex(0);
    if (!await(idle) || (!global_color_preview && std::abs(controller.activeMaskTemperature() - 12.0) > 1e-9))
      return fail(QStringLiteral("The benchmark mask was not selected with its original temperature."));
    std::vector<double> admission, feedback, settled;
    QImage previous_pixels;
    const auto output = option_value(arguments, QStringLiteral("--local-output"));
    if (!output.isEmpty() && (!QFileInfo(output).isAbsolute() ||
        !QDir(output).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden).isEmpty() ||
        !QDir().mkpath(output))) return fail(QStringLiteral("Choose an empty absolute local-output directory."));
    if (global_mask_preview) {
      QImage first_pixels;
      bool full_racing_first_feedback = false;
      const auto revision = controller.displayPreviewNativeRevision();
      const auto connection = QObject::connect(&controller,
          &photo_editor::desktop::EditorPreviewController::previewChanged, &controller, [&] {
            if (first_pixels.isNull() && controller.displayPreviewNativeRevision() > revision &&
                Access::last_full_preview_generation(controller) != Access::render_generation(controller)) {
              full_racing_first_feedback = Access::full_render_running(controller);
              first_pixels = Access::native_pixels(controller).convertToFormat(QImage::Format_RGBA8888);
            }
          });
      const auto color_control = option_value(arguments, QStringLiteral("--color-control"));
      if (global_color_preview && color_control == QStringLiteral("curve")) {
        controller.setActiveCurvePoints({QVariantMap{{"x", 0.0}, {"y", 0.0}},
            QVariantMap{{"x", 0.2}, {"y", 0.3}}, QVariantMap{{"x", 0.7}, {"y", 0.65}},
            QVariantMap{{"x", 1.0}, {"y", 1.0}}});
      } else if (global_color_preview && color_control == QStringLiteral("saturation")) {
        controller.setSaturation(35.0);
      } else if (global_color_preview && color_control == QStringLiteral("temperature")) {
        controller.setTemperature(25.0);
      } else if (global_color_preview && color_control == QStringLiteral("tint")) {
        controller.setTint(-25.0);
      } else {
        controller.setExposure(.25);
      }
      auto expected_request = Access::interactive_request(controller);
      const bool has_full_work = Access::full_render_timer_active(controller) ||
          Access::full_render_pending(controller) || Access::full_render_running(controller);
      const bool finished = await([&] {
        return idle() && Access::last_full_preview_generation(controller) == Access::render_generation(controller);
      });
      QObject::disconnect(connection);
      if (!expected_request.draft_interactive || !has_full_work || !finished || first_pixels.isNull())
        return fail(QStringLiteral("Global slider must publish a real draft and reach current full quality."));
      if (arguments.contains(QStringLiteral("--require-draft-first")) && full_racing_first_feedback)
        return fail(QStringLiteral("Full rendering competed with first draft publication."));
      const auto minimum_draft_edge = option_int(arguments, QStringLiteral("--minimum-draft-edge"), 0);
      if (std::max(first_pixels.width(), first_pixels.height()) < minimum_draft_edge)
        return fail(QStringLiteral("Interactive draft missed the requested resolution floor."));
      // Reference the same decode/output dimensions without the draft reduction.
      // Comparing full-size pixels downscaled after rendering would conflate
      // demosaic/resampling changes with missing local edits.
      expected_request.draft_interactive = false;
      expected_request.render_mode = QStringLiteral("settled_preview");
      expected_request.use_in_memory_preview = true;
      expected_request.require_cpu_preview_image = true;
      expected_request.allow_native_srgb_surface = false;
      expected_request.allow_native_device_surface = false;
      expected_request.recipe_cache_identity.clear();
      expected_request.adjusted_recipe_cache_identity.clear();
      const auto reference = photo_editor::desktop::render_preview_request_for_test(expected_request);
      const auto expected = reference.preview_image.convertToFormat(QImage::Format_RGBA8888);
      expected_request.state.mask_stack.clear();
      expected_request.state.active_mask_index = -1;
      const auto control_result = photo_editor::desktop::render_preview_request_for_test(expected_request);
      const auto control = control_result.preview_image.convertToFormat(QImage::Format_RGBA8888);
      const auto maximum_delta = [](const QImage& a, const QImage& b) {
        if (a.isNull() || b.isNull() || a.size() != b.size()) return 256;
        int delta = 0;
        for (int y = 0; y < a.height(); ++y)
          for (int x = 0; x < a.width() * 4; ++x)
            delta = std::max(delta, std::abs(int(a.constScanLine(y)[x]) - int(b.constScanLine(y)[x])));
        return delta;
      };
      const auto draft_delta = maximum_delta(first_pixels, expected);
      if (!expected.isNull() && first_pixels.size() == expected.size()) {
        for (int region = 0; region < 4; ++region) {
          const QRect bounds(region * expected.width() / 4, 0,
                             expected.width() / 4, expected.height());
          std::cout << "draft_region=" << region << " max_delta="
                    << maximum_delta(first_pixels.copy(bounds), expected.copy(bounds)) << '\n';
        }
      }
      int changed = 0, protected_changed = 0;
      if (!reference.success || !control_result.success || control.size() != expected.size() || expected.isNull())
        return fail(QStringLiteral("Cannot render matched-resolution masked/unmasked references."));
      for (int y = 0; y < expected.height(); ++y)
        for (int x = 0; x < expected.width(); ++x) {
          if (x < expected.width() / 4 && expected.pixel(x, y) != control.pixel(x, y)) ++changed;
          if (x >= expected.width() * 3 / 4 && expected.pixel(x, y) != control.pixel(x, y)) ++protected_changed;
        }
      const auto final_pixels = Access::native_pixels(controller).convertToFormat(QImage::Format_RGBA8888);
      if (arguments.contains(QStringLiteral("--viewport-cache-proof"))) {
        controller.setPreviewViewport(0.25, 0.25, 0.5, 0.5, 4096);
        if (!controller.displayPreviewNativeViewportFullResolution() ||
            Access::redundant_viewport_starts_worker(controller))
          return fail(QStringLiteral("A resident viewport redundantly starts rendering."));
        auto viewport = Access::interactive_request(controller);
        viewport.full_resolution = true;
        viewport.overlay_only = false;
        viewport.preview_overlay_rect = {};
        viewport.preview_overlay_max_edge = 0;
        viewport.max_edge = 0;
        viewport.draft_interactive = false;
        viewport.render_mode = QStringLiteral("max_preview_visible_tile");
        viewport.render_reason = QStringLiteral("viewport_change");
        viewport.dirty_domain = QStringLiteral("publish_texture");
        viewport.tile_size = 256;
        viewport.tile_visible_region_active = true;
        viewport.tile_visible_region = QRectF(0.25, 0.25, 0.5, 0.5);
        viewport.full_invalidation = false;
        viewport.use_in_memory_preview = true;
        viewport.allow_texture_patch_only = true;
        viewport.allow_native_srgb_surface = false;
        viewport.allow_native_device_surface = false;
        viewport.recipe_cache_identity.clear();
        viewport.adjusted_recipe_cache_identity.clear();
        for (int sample = 0; sample < 3; ++sample) {
          const auto patch = photo_editor::desktop::render_preview_request_for_test(viewport);
          const auto pixels = patch.texture_upload_image.convertToFormat(QImage::Format_RGBA8888);
          const auto expected_patch = final_pixels.copy(patch.texture_upload_rect);
          const auto delta = maximum_delta(pixels, expected_patch);
          std::cout << "viewport_cache_sample=" << sample << " elapsed_ms=" << patch.total_elapsed_ms
                    << " cache_hit=" << patch.adjusted_cache_hit << " delta=" << delta << std::endl;
          if (!patch.success || !patch.adjusted_cache_hit || delta > 1)
            return fail(QStringLiteral("Cached viewport patch differs from the full native preview."));
        }
      }
      const auto export_path = local_runtime.filePath("global-mask-export.png");
      QElapsedTimer export_timer;
      export_timer.start();
      if (!controller.exportFlattenedPhoto(export_path)) return fail(QStringLiteral("Global-mask export failed."));
      log_stage(QStringLiteral("flattened_export_to_file"), export_timer.elapsed(), 0);
      const auto exported = QImage(export_path).convertToFormat(QImage::Format_RGBA8888);
      const auto export_delta = maximum_delta(final_pixels, exported);
      std::cout << "global_mask_draft=" << first_pixels.width() << 'x' << first_pixels.height()
                << " draft_reference_max_delta=" << draft_delta << " masked_changed=" << changed
                << " protected_changed=" << protected_changed << " final_export_max_delta=" << export_delta
                << " full_generation=" << Access::last_full_preview_generation(controller)
                << " generation=" << Access::render_generation(controller) << '\n';
      if (!output.isEmpty() &&
          (!first_pixels.save(QDir(output).filePath("draft.png")) ||
           !expected.save(QDir(output).filePath("matched-reference.png")) ||
           !final_pixels.save(QDir(output).filePath("full.png"))))
        return fail(QStringLiteral("Cannot save global-mask pixel evidence."));
      if (draft_delta > 2 || export_delta > 2 || (!global_color_preview && (changed == 0 || protected_changed != 0)))
        return fail(QStringLiteral("Global slider dropped or changed local edits between draft, settled and export."));
      std::cout << (global_color_preview ? "global_color_preview=passed\n" : "global_mask_preview=passed\n");
      return EXIT_SUCCESS;
    }
    for (int iteration = 0; iteration < iterations; ++iteration) {
      const auto revision = controller.displayPreviewNativeRevision();
      QElapsedTimer elapsed; elapsed.start();
      qint64 published_ms = -1;
      QSize published_size;
      const auto connection = QObject::connect(&controller,
          &photo_editor::desktop::EditorPreviewController::previewChanged, &controller, [&] {
            if (published_ms < 0 && controller.displayPreviewNativeRevision() > revision) {
              published_ms = elapsed.elapsed();
              published_size = controller.displayPreviewNativeSize();
            }
          });
      const auto value = local_opacity ? .8 - double(iteration % 3) * .2 :
          // A 16-pixel mask gives dilation radii 1, 2, 3. Keep the largest
          // radius left of the protected rightmost quarter of the image.
          local_expansion ? 5.0 + double(iteration % 3) * 5.0 :
          12.25 + double(iteration) * .25;
      controller.beginCoalescedEdit();
      if (local_opacity) controller.setActiveMaskOpacity(value);
      else if (local_expansion) controller.setActiveMaskSelectionExpand(value);
      else controller.setActiveMaskTemperature(value);
      const auto admission_ms = elapsed.elapsed();
      const bool published = await([&] { return published_ms >= 0; });
      controller.endCoalescedEdit();
      const bool finished = published && await([&] {
        const auto full_size = QSize(qRound(controller.sourceImageWidth() * (cropped ? .8 : 1.0)),
            qRound(controller.sourceImageHeight() * (cropped ? .8 : 1.0)));
        return idle() && Access::last_full_preview_generation(controller) == Access::render_generation(controller) &&
            controller.displayPreviewNativeSize() == full_size;
      });
      const auto settled_ms = elapsed.elapsed();
      QObject::disconnect(connection);
      const auto actual_value = local_opacity ? controller.activeMaskOpacity() :
          local_expansion ? controller.activeMaskSelectionExpand() : controller.activeMaskTemperature();
      if (!finished || !controller.previewFailureDetail().isEmpty() ||
          std::abs(actual_value - value) > 1e-9)
        return fail(QStringLiteral("Local-mask render did not publish and settle: ") + controller.previewFailureDetail());
      admission.push_back(double(admission_ms));
      feedback.push_back(double(published_ms));
      settled.push_back(double(settled_ms));
      // Read back the actual published texture only after timing. A changed
      // recipe or native revision alone does not prove that edits reached pixels.
      const auto native_pixels = Access::native_pixels(controller).convertToFormat(QImage::Format_RGBA8888);
      if (native_pixels.isNull()) return fail(QStringLiteral("Cannot read back the final native preview."));
      if (!previous_pixels.isNull()) {
        if (native_pixels.size() != previous_pixels.size() || native_pixels == previous_pixels)
          return fail(QStringLiteral("Local mask edit must visibly change each final native image."));
        for (int y = 0; y < native_pixels.height(); ++y)
          for (int x = native_pixels.width() * 3 / 4; x < native_pixels.width(); ++x)
            if (native_pixels.pixel(x, y) != previous_pixels.pixel(x, y))
              return fail(QStringLiteral("Local mask edit changed protected pixels."));
      }
      previous_pixels = native_pixels;
      if (iteration == iterations - 1 && (local_opacity || local_expansion)) {
        const auto export_path = local_runtime.filePath(QStringLiteral("settled-export.png"));
        if (!controller.exportFlattenedPhoto(export_path))
          return fail(QStringLiteral("Cannot export the settled local-mask photograph."));
        const auto exported = QImage(export_path).convertToFormat(QImage::Format_RGBA8888);
        if (exported.isNull() || exported.size() != native_pixels.size())
          return fail(QStringLiteral("Local-mask native/export dimensions differ."));
        int max_delta = 0;
        for (int y = 0; y < exported.height(); ++y) {
          const auto* expected = exported.constScanLine(y);
          const auto* actual = native_pixels.constScanLine(y);
          for (int x = 0; x < exported.width() * 4; ++x)
            max_delta = std::max(max_delta, std::abs(int(expected[x]) - int(actual[x])));
        }
        std::cout << "local_mask_native_export_max_delta=" << max_delta << '\n';
        if (max_delta > 2)
          return fail(QStringLiteral("Settled native mask output differs from flattened export by more than 2/255."));
      }
      if (!output.isEmpty() && (iteration == 0 || iteration == iterations - 1) &&
          !native_pixels.save(QDir(output).filePath(QString("native-%1.png").arg(iteration))))
        return fail(QStringLiteral("Cannot save native pixel proof."));
      const auto hash = QCryptographicHash::hash(QByteArrayView(
          reinterpret_cast<const char*>(native_pixels.constBits()), native_pixels.sizeInBytes()),
          QCryptographicHash::Sha256).toHex();
      const QVariantMap fields{{"control", control}, {"value", value}, {"nativeTexture", true},
          {"cropped", cropped},
          {"nativeRgbaSha256", QString::fromLatin1(hash)}, {"protectedChangedPixels", 0},
          {"firstWidth", published_size.width()}, {"firstHeight", published_size.height()},
          {"settledWidth", controller.displayPreviewNativeSize().width()},
          {"settledHeight", controller.displayPreviewNativeSize().height()},
          {"generation", Access::render_generation(controller)},
          {"fullGeneration", Access::last_full_preview_generation(controller)}};
      log_stage(stage_prefix + QStringLiteral("_setter_admission"), admission_ms, iteration, fields);
      log_stage(stage_prefix + QStringLiteral("_to_native_handoff"), published_ms, iteration, fields);
      log_stage(stage_prefix + QStringLiteral("_to_all_idle"), settled_ms, iteration, fields);
    }
    log_summary(stage_prefix + QStringLiteral("_setter_admission"), summarize_samples(admission), iterations);
    log_summary(stage_prefix + QStringLiteral("_to_native_handoff"), summarize_samples(feedback), iterations);
    log_summary(stage_prefix + QStringLiteral("_to_all_idle"), summarize_samples(settled), iterations);
    std::cout << "local_mask_control=" << control.toStdString() << '\n'
              << "local_mask_iterations=" << iterations << '\n'
              << "local_mask_native_handoff_p50_ms=" << summarize_samples(feedback).p50_ms << '\n'
              << "local_mask_all_idle_p50_ms=" << summarize_samples(settled).p50_ms << '\n';
    return EXIT_SUCCESS;
  }

  std::vector<double> open_samples;
  std::vector<double> brush_page_samples;
  std::vector<double> first_feedback_samples;
  std::vector<double> stroke_commit_samples;
  std::vector<double> tool_draft_samples;
  std::vector<double> tool_visible_samples;
  std::vector<double> tool_settled_samples;
  std::vector<double> total_samples;
  open_samples.reserve(static_cast<std::size_t>(iterations));
  brush_page_samples.reserve(static_cast<std::size_t>(iterations));
  first_feedback_samples.reserve(static_cast<std::size_t>(iterations));
  stroke_commit_samples.reserve(static_cast<std::size_t>(iterations));
  tool_draft_samples.reserve(static_cast<std::size_t>(iterations));
  tool_visible_samples.reserve(static_cast<std::size_t>(iterations));
  tool_settled_samples.reserve(static_cast<std::size_t>(iterations));
  total_samples.reserve(static_cast<std::size_t>(iterations));

  QElapsedTimer suite_timer;
  suite_timer.start();

  controller.loadAsset(
      QStringLiteral("interaction-global-exposure"),
      source_path,
      QString(),
      QStringLiteral("Benchmark Camera"),
      QStringLiteral("Jul 4, 2026"),
      QStringLiteral("Benchmark 35mm"),
      QString());
  if (!wait_for_all_preview_idle(controller, timeout_ms)) {
    return fail(QStringLiteral("Timed out warming the resident RAW before exposure timing."));
  }
  if (photo_editor::desktop::raw_image_supported(source_path) &&
      !wait_for_raw_decode_pyramid(controller, timeout_ms)) {
    return fail(QStringLiteral("Timed out warming the resident RAW decode pyramid."));
  }
  if (!wait_for_color_transform_prewarm(controller, timeout_ms)) {
    return fail(QStringLiteral("Timed out warming the resident color transforms."));
  }
  controller.setExposure(0.01);
  if (!wait_for_all_preview_idle(controller, timeout_ms)) {
    return fail(QStringLiteral("Timed out warming the global exposure render path."));
  }
  QElapsedTimer* active_feedback_timer = nullptr;
  auto feedback_revision_floor = 0;
  auto feedback_published_ms = qint64{-1};
  const auto feedback_connection = QObject::connect(
      &controller,
      &photo_editor::desktop::EditorPreviewController::previewChanged,
      &controller,
      [&]() {
        if (active_feedback_timer != nullptr && feedback_published_ms < 0 &&
            controller.displayPreviewNativeRevision() > feedback_revision_floor) {
          feedback_published_ms = active_feedback_timer->elapsed();
        }
      });
  for (int iteration = 0; iteration < iterations; ++iteration) {
    const auto revision_before_exposure = controller.displayPreviewNativeRevision();
    QElapsedTimer draft_timer;
    draft_timer.start();
    active_feedback_timer = &draft_timer;
    feedback_revision_floor = revision_before_exposure;
    feedback_published_ms = -1;
    controller.setExposure(std::min(0.90, 0.10 + 0.01 * static_cast<double>(iteration + 1)));
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    const auto draft_scheduled_ms = draft_timer.elapsed();
    tool_draft_samples.push_back(static_cast<double>(draft_scheduled_ms));
    log_stage(
        QStringLiteral("global_exposure_to_draft_scheduled"),
        draft_scheduled_ms,
        iteration,
        {
            {QStringLiteral("asset_id"), QStringLiteral("interaction-global-exposure")},
            {QStringLiteral("preview_target_edge"), controller.previewTargetEdge()},
        });
    if (!wait_for_preview_revision_after(
            controller, revision_before_exposure, timeout_ms)) {
      return fail(QStringLiteral("Timed out waiting for global exposure draft feedback."));
    }
    active_feedback_timer = nullptr;
    const auto visible_ms =
        feedback_published_ms >= 0 ? feedback_published_ms : draft_timer.elapsed();
    tool_visible_samples.push_back(static_cast<double>(visible_ms));
    log_stage(
        QStringLiteral("global_exposure_to_first_feedback"),
        visible_ms,
        iteration,
        {
            {QStringLiteral("asset_id"), QStringLiteral("interaction-global-exposure")},
            {QStringLiteral("preview_target_edge"), controller.previewTargetEdge()},
        });
    if (!wait_for_visible_preview_idle(controller, timeout_ms).ready) {
      return fail(QStringLiteral("Timed out waiting for settled 2560 px exposure output."));
    }
    const auto settled_ms = draft_timer.elapsed();
    tool_settled_samples.push_back(static_cast<double>(settled_ms));
    log_stage(
        QStringLiteral("global_exposure_to_settled_2560"),
        settled_ms,
        iteration,
        {
            {QStringLiteral("asset_id"), QStringLiteral("interaction-global-exposure")},
            {QStringLiteral("preview_target_edge"), controller.previewTargetEdge()},
        });
    if (!wait_for_all_preview_idle(controller, timeout_ms)) {
      return fail(QStringLiteral("Timed out waiting for full-resolution exposure refinement."));
    }
    log_stage(
        QStringLiteral("global_exposure_to_full_resolution"),
        draft_timer.elapsed(),
        iteration,
        {
            {QStringLiteral("asset_id"), QStringLiteral("interaction-global-exposure")},
        });
  }
  QObject::disconnect(feedback_connection);

  const auto interactive_cancelled_before_burst =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          interactive_render_cancelled_count(controller);
  const auto full_cancelled_before_burst =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          full_render_cancelled_count(controller);
  controller.setExposure(0.31);
  controller.setExposure(0.32);
  controller.setExposure(0.33);
  if (!wait_for_all_preview_idle(controller, timeout_ms)) {
    return fail(QStringLiteral("Timed out settling the superseded exposure burst."));
  }
  const auto burst_generation =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(controller);
  const auto burst_full_generation =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          last_full_preview_generation(controller);
  const auto burst_latest_published =
      qFuzzyCompare(controller.exposure() + 1.0, 1.33) &&
      burst_full_generation == burst_generation;
  photo_editor::desktop::observability::logInfo(
      photoEditorPerfLog(),
      QStringLiteral("benchmark.interaction_workflow.supersession"),
      {
          {QStringLiteral("requested_edit_count"), 3},
          {QStringLiteral("final_exposure"), controller.exposure()},
          {QStringLiteral("current_generation"), burst_generation},
          {QStringLiteral("published_full_generation"), burst_full_generation},
          {QStringLiteral("interactive_cancelled_delta"),
           photo_editor::desktop::EditorPreviewControllerTestAccess::
                   interactive_render_cancelled_count(controller) -
               interactive_cancelled_before_burst},
          {QStringLiteral("full_cancelled_delta"),
           photo_editor::desktop::EditorPreviewControllerTestAccess::
                   full_render_cancelled_count(controller) -
               full_cancelled_before_burst},
          {QStringLiteral("latest_generation_published"), burst_latest_published},
      });
  if (!burst_latest_published) {
    return fail(QStringLiteral("Superseded exposure burst published a stale generation."));
  }

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

    total_samples.push_back(static_cast<double>(total_timer.elapsed()));
    log_stage(
        QStringLiteral("total"),
        total_timer.elapsed(),
        iteration,
        {
            {QStringLiteral("asset_id"), asset_id},
        });
  }

  auto resident_bytes_after_cycle = std::array<qint64, 2>{};
  for (auto cycle = 0; cycle < 2; ++cycle) {
    for (auto asset_index = 0; asset_index < 4; ++asset_index) {
      controller.loadAsset(
          QStringLiteral("interaction-memory-%1").arg(asset_index),
          source_path,
          QString(),
          QStringLiteral("Benchmark Camera"),
          QStringLiteral("Jul 4, 2026"),
          QStringLiteral("Benchmark 35mm"),
          QString());
      if (!wait_for_all_preview_idle(controller, timeout_ms)) {
        return fail(
            QStringLiteral("Timed out measuring the preview memory plateau."));
      }
    }
    resident_bytes_after_cycle[static_cast<std::size_t>(cycle)] =
        controller.colorTransformDiagnostics()
            .value(QStringLiteral("previewMemory"))
            .toMap()
            .value(QStringLiteral("trackedResidentBytes"))
            .toLongLong();
  }
  const auto memory_plateau_passed =
      resident_bytes_after_cycle[1] <= resident_bytes_after_cycle[0];
  photo_editor::desktop::observability::logInfo(
      photoEditorPerfLog(),
      QStringLiteral("benchmark.interaction_workflow.memory_plateau"),
      {
          {QStringLiteral("asset_count"), 4},
          {QStringLiteral("cycle_count"), 2},
          {QStringLiteral("first_cycle_resident_bytes"),
           resident_bytes_after_cycle[0]},
          {QStringLiteral("second_cycle_resident_bytes"),
           resident_bytes_after_cycle[1]},
          {QStringLiteral("within_budget"), memory_plateau_passed},
      });
  if (!memory_plateau_passed) {
    return fail(
        QStringLiteral("Repeated preview cycles did not reach a bounded memory plateau."));
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
  const auto tool_draft_summary = summarize_samples(tool_draft_samples);
  const auto tool_visible_summary = summarize_samples(tool_visible_samples);
  const auto tool_settled_summary = summarize_samples(tool_settled_samples);
  log_summary(
      QStringLiteral("global_exposure_to_draft_scheduled"),
      tool_draft_summary,
      iterations);
  log_summary(
      QStringLiteral("global_exposure_to_first_feedback"),
      tool_visible_summary,
      iterations);
  log_summary(
      QStringLiteral("global_exposure_to_settled_2560"),
      tool_settled_summary,
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
          {QStringLiteral("tool_draft_p95_ms"), tool_draft_summary.p95_ms},
          {QStringLiteral("tool_visible_p95_ms"), tool_visible_summary.p95_ms},
          {QStringLiteral("tool_settled_p95_ms"), tool_settled_summary.p95_ms},
          {QStringLiteral("total_p95_ms"), summarize_samples(total_samples).p95_ms},
      });

  std::cout << "interaction_workflow_iterations=" << iterations << '\n';
  std::cout << "global_exposure_first_feedback_p50_ms="
            << tool_visible_summary.p50_ms << '\n';
  std::cout << "global_exposure_first_feedback_p95_ms="
            << tool_visible_summary.p95_ms << '\n';
  std::cout << "global_exposure_settled_2560_p50_ms="
            << tool_settled_summary.p50_ms << '\n';
  std::cout << "global_exposure_settled_2560_p95_ms="
            << tool_settled_summary.p95_ms << '\n';
  std::cout << "memory_plateau_first_cycle_bytes="
            << resident_bytes_after_cycle[0] << '\n';
  std::cout << "memory_plateau_second_cycle_bytes="
            << resident_bytes_after_cycle[1] << '\n';
  return EXIT_SUCCESS;
}
