#include <algorithm>
#include <array>
#include <cstdlib>
#include <cmath>
#include <iostream>
#include <memory>
#include <numeric>
#include <vector>

#include <QCoreApplication>
#include <QColor>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <QTemporaryDir>

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

bool has_option(const QStringList& arguments, const QString& name) {
  return arguments.contains(name);
}

QString read_text_file(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return QString::fromUtf8(file.readAll());
}

int max_channel_delta(const QImage& left, const QImage& right) {
  if (left.size() != right.size()) {
    return 255;
  }

  auto max_delta = 0;
  for (int y = 0; y < left.height(); ++y) {
    for (int x = 0; x < left.width(); ++x) {
      const auto lhs = left.pixelColor(x, y);
      const auto rhs = right.pixelColor(x, y);
      max_delta = std::max(max_delta, std::abs(lhs.red() - rhs.red()));
      max_delta = std::max(max_delta, std::abs(lhs.green() - rhs.green()));
      max_delta = std::max(max_delta, std::abs(lhs.blue() - rhs.blue()));
    }
  }
  return max_delta;
}

bool write_brush_tile_benchmark_source(const QString& path) {
  QImage image(2048, 2048, QImage::Format_ARGB32);
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      image.setPixelColor(
          x,
          y,
          QColor::fromRgb(
              (x + y / 3) % 256,
              (y * 2 + x / 5) % 256,
              (x * 3 + y * 2) % 256));
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
  double stddev_ms = 0.0;
  double mad_ms = 0.0;
};

double percentile(std::vector<double> sorted_samples, double percentile_value) {
  if (sorted_samples.empty()) {
    return 0.0;
  }
  std::sort(sorted_samples.begin(), sorted_samples.end());
  const auto normalized = std::clamp(percentile_value, 0.0, 100.0) / 100.0;
  const auto index = static_cast<std::size_t>(
      std::ceil(normalized * static_cast<double>(sorted_samples.size())) - 1.0);
  return sorted_samples[std::min(index, sorted_samples.size() - 1)];
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

  auto variance = 0.0;
  for (const auto sample : samples) {
    const auto delta = sample - summary.avg_ms;
    variance += delta * delta;
  }
  summary.stddev_ms = std::sqrt(variance / static_cast<double>(samples.size()));

  std::vector<double> absolute_deviations;
  absolute_deviations.reserve(samples.size());
  for (const auto sample : samples) {
    absolute_deviations.push_back(std::abs(sample - summary.p50_ms));
  }
  summary.mad_ms = percentile(std::move(absolute_deviations), 50.0);
  return summary;
}

class BenchmarkImageSink final : public photo_editor::desktop::EditorPreviewImageSink {
 public:
  void setImage(const QString& key, const QImage& image) override {
    Q_UNUSED(key);
    last_image_size_ = image.size();
  }

  void clearImage(const QString& key) override {
    Q_UNUSED(key);
  }

  [[nodiscard]] QSize lastImageSize() const { return last_image_size_; }

 private:
  QSize last_image_size_;
};

class BenchmarkPreviewTexture final : public photo_editor::desktop::EditorPreviewTexture {
 public:
  [[nodiscard]] bool available() const override { return true; }
  [[nodiscard]] const char* backendName() const override { return "benchmark-fake"; }
  bool ensureNativeTexture(const QSize& size, QImage::Format format) override {
    if (!size.isValid() || size.width() <= 0 || size.height() <= 0) {
      return false;
    }
    texture_size_ = size;
    texture_format_name_ = native_format_name_for_format(format);
    texture_bytes_per_pixel_ = native_bytes_per_pixel_for_format(format);
    return true;
  }
  bool upload(const QImage& image) override {
    if (image.isNull()) {
      return false;
    }
    texture_size_ = image.size();
    texture_format_name_ = native_format_name_for_format(image.format());
    texture_bytes_per_pixel_ = native_bytes_per_pixel_for_format(image.format());
    ++upload_count_;
    return true;
  }
  bool uploadRegion(const QImage& image, const QRect& region) override {
    if (image.isNull() || region.isEmpty() || image.size() != texture_size_) {
      return false;
    }
    ++partial_upload_count_;
    last_partial_upload_rect_ = region;
    return true;
  }
  bool uploadPatch(const QImage& image, const QRect& destination_region) override {
    if (image.isNull() || destination_region.isEmpty() ||
        destination_region.size() != image.size() || !texture_size_.isValid() ||
        destination_region.right() >= texture_size_.width() ||
        destination_region.bottom() >= texture_size_.height()) {
      return false;
    }
    ++partial_upload_count_;
    ++patch_upload_count_;
    last_patch_upload_rect_ = destination_region;
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
  [[nodiscard]] QRect lastPatchUploadRect() const { return last_patch_upload_rect_; }

 private:
  [[nodiscard]] static const char* native_format_name_for_format(QImage::Format format) {
    return format == QImage::Format_RGBA16FPx4 ? "rgba16float" : "bgra8unorm";
  }
  [[nodiscard]] static int native_bytes_per_pixel_for_format(QImage::Format format) {
    return format == QImage::Format_RGBA16FPx4 ? 8 : 4;
  }

  QSize texture_size_;
  const char* texture_format_name_ = "none";
  int texture_bytes_per_pixel_ = 0;
  int upload_count_ = 0;
  int partial_upload_count_ = 0;
  int patch_upload_count_ = 0;
  QRect last_partial_upload_rect_;
  QRect last_patch_upload_rect_;
};

}  // namespace

namespace photo_editor::desktop {

RenderResult render_preview_request_for_test(const RenderRequest& request);

struct EditorPreviewControllerTestAccess {
  static RenderRequest full_render_request(const EditorPreviewController& controller) {
    return controller.currentRenderRequest(true);
  }

  static RenderResult render_preview_request_sync(const RenderRequest& request) {
    return render_preview_request_for_test(request);
  }

  static bool full_render_running(const EditorPreviewController& controller) {
    return controller.full_render_watcher_.isRunning();
  }

  static bool render_idle(const EditorPreviewController& controller) {
    return !controller.interactive_render_timer_.isActive() &&
           !controller.full_render_timer_.isActive() &&
           !controller.interactive_render_watcher_.isRunning() &&
           !controller.full_render_watcher_.isRunning() &&
           !controller.interactive_request_pending_ &&
           !controller.full_request_pending_;
  }

  static int render_generation(const EditorPreviewController& controller) {
    return controller.render_generation_;
  }

  static int interactive_render_cancelled_count(const EditorPreviewController& controller) {
    return controller.interactive_render_cancelled_count_;
  }

  static int full_render_cancelled_count(const EditorPreviewController& controller) {
    return controller.full_render_cancelled_count_;
  }

  static int disk_preview_cache_hit_count(const EditorPreviewController& controller) {
    return controller.disk_preview_cache_hit_count_;
  }

  static qint64 last_full_render_ms(const EditorPreviewController& controller) {
    return controller.last_full_render_ms_;
  }

  static int visible_tile_prepass_schedule_count(const EditorPreviewController& controller) {
    return controller.visible_tile_prepass_schedule_count_;
  }

  static int viewport_max_preview_schedule_count(const EditorPreviewController& controller) {
    return controller.viewport_max_preview_schedule_count_;
  }

  static void set_display_preview_texture(
      EditorPreviewController& controller,
      std::unique_ptr<EditorPreviewTexture> texture) {
    controller.display_preview_texture_ = std::move(texture);
    controller.display_preview_native_revision_ = 0;
    controller.display_preview_native_rendered_cache_key_.clear();
  }

  static void start_full_render_now(EditorPreviewController& controller) {
    controller.full_render_timer_.stop();
    controller.renderPreviewStage(true);
  }
};

}  // namespace photo_editor::desktop

namespace {

bool wait_for_preview_idle(
    photo_editor::desktop::EditorPreviewController& controller,
    int timeout_ms) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    if (photo_editor::desktop::EditorPreviewControllerTestAccess::render_idle(controller)) {
      return true;
    }
    QThread::msleep(25);
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  return photo_editor::desktop::EditorPreviewControllerTestAccess::render_idle(controller);
}

bool wait_for_histogram_settled(
    photo_editor::desktop::EditorPreviewController& controller,
    int timeout_ms) {
  if (!controller.histogramUpdateActive() && controller.histogramSampleCount() > 0) {
    return true;
  }

  QElapsedTimer timer;
  timer.start();
  auto observed_histogram_work = controller.histogramUpdateActive();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    observed_histogram_work = observed_histogram_work || controller.histogramUpdateActive();
    if (!controller.histogramUpdateActive() && controller.histogramSampleCount() > 0 &&
        observed_histogram_work) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
      return true;
    }
    if (!observed_histogram_work &&
        !controller.histogramUpdateActive() &&
        photo_editor::desktop::EditorPreviewControllerTestAccess::render_idle(controller)) {
      return true;
    }
    QThread::msleep(25);
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  return !controller.histogramUpdateActive() && controller.histogramSampleCount() > 0;
}

bool wait_for_disk_preview_cache_hit(
    photo_editor::desktop::EditorPreviewController& controller,
    int previous_hit_count,
    int timeout_ms) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
    if (photo_editor::desktop::EditorPreviewControllerTestAccess::disk_preview_cache_hit_count(
            controller) > previous_hit_count) {
      return true;
    }
    QThread::msleep(1);
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
  return photo_editor::desktop::EditorPreviewControllerTestAccess::disk_preview_cache_hit_count(
             controller) > previous_hit_count;
}

bool wait_for_full_render_running(
    photo_editor::desktop::EditorPreviewController& controller,
    int timeout_ms) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    if (photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_running(controller)) {
      return true;
    }
    QThread::msleep(10);
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  return photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_running(controller);
}

int render_cancelled_count(const photo_editor::desktop::EditorPreviewController& controller) {
  return photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_cancelled_count(
             controller) +
         photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_cancelled_count(
             controller);
}

bool wait_for_render_cancellation(
    photo_editor::desktop::EditorPreviewController& controller,
    int previous_cancel_count,
    int previous_full_cancel_count,
    int superseded_generation,
    int timeout_ms) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    const auto cancel_count = render_cancelled_count(controller);
    const auto full_cancel_count =
        photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_cancelled_count(
            controller);
    const auto generation =
        photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(controller);
    if (cancel_count > previous_cancel_count &&
        full_cancel_count > previous_full_cancel_count &&
        generation > superseded_generation) {
      return true;
    }
    QThread::msleep(10);
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  return render_cancelled_count(controller) > previous_cancel_count &&
         photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_cancelled_count(
             controller) > previous_full_cancel_count &&
         photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(controller) >
             superseded_generation;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("PhotoEditorBenchmarks"));
  QCoreApplication::setApplicationName(QStringLiteral("PreviewBenchmarkDriver"));
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", QByteArrayLiteral("1"));

  const auto arguments = QCoreApplication::arguments();
  const auto corpus_path = option_value(arguments, QStringLiteral("--corpus"));
  const auto limit_value = option_value(arguments, QStringLiteral("--limit"));
  const auto timeout_value = option_value(arguments, QStringLiteral("--timeout-ms"));
  const auto iteration_value = option_value(arguments, QStringLiteral("--iterations"));
  const auto brush_tile_span_value = option_value(arguments, QStringLiteral("--brush-tile-span"));
  const auto limit = limit_value.isEmpty() ? 0 : limit_value.toInt();
  const auto timeout_ms = timeout_value.isEmpty() ? 15000 : timeout_value.toInt();
  const auto iterations = iteration_value.isEmpty() ? 1 : iteration_value.toInt();
  const auto brush_tile_span =
      brush_tile_span_value.isEmpty() ? 1 : brush_tile_span_value.toInt();
  if (iterations <= 0) {
    return fail(QStringLiteral("--iterations must be positive"));
  }
  if (brush_tile_span <= 0 || brush_tile_span > 4) {
    return fail(QStringLiteral("--brush-tile-span must be between 1 and 4"));
  }
  const auto apply_edit_sweep = has_option(arguments, QStringLiteral("--edit-sweep"));
  const auto apply_global_burst = has_option(arguments, QStringLiteral("--global-burst"));
  const auto repeat_full_cache = has_option(arguments, QStringLiteral("--repeat-full-cache"));
  const auto output_profile_toggle = has_option(arguments, QStringLiteral("--output-profile-toggle"));
  const auto rapid_supersede = has_option(arguments, QStringLiteral("--rapid-supersede"));
  const auto viewport_max_tile = has_option(arguments, QStringLiteral("--viewport-max-tile"));
  const auto viewport_pan_heavy_edit =
      has_option(arguments, QStringLiteral("--viewport-pan-heavy-edit"));
  const auto brush_tile_cache = has_option(arguments, QStringLiteral("--brush-tile-cache"));
  const auto brush_sequence = has_option(arguments, QStringLiteral("--brush-sequence"));
  const auto brush_overbroad_dirty =
      has_option(arguments, QStringLiteral("--brush-overbroad-dirty"));
  const auto brush_texture_patch_only =
      has_option(arguments, QStringLiteral("--brush-texture-patch-only"));
  const auto brush_patch_chain =
      has_option(arguments, QStringLiteral("--brush-patch-chain"));
  if (brush_sequence && (!brush_tile_cache || brush_tile_span != 1)) {
    return fail(QStringLiteral("--brush-sequence requires --brush-tile-cache with --brush-tile-span 1"));
  }
  if (brush_overbroad_dirty && (!brush_tile_cache || brush_sequence)) {
    return fail(QStringLiteral(
        "--brush-overbroad-dirty requires --brush-tile-cache without --brush-sequence"));
  }
  if (brush_texture_patch_only && !brush_tile_cache) {
    return fail(QStringLiteral(
        "--brush-texture-patch-only requires --brush-tile-cache"));
  }
  if (brush_patch_chain && (!brush_sequence || !brush_texture_patch_only)) {
    return fail(QStringLiteral(
        "--brush-patch-chain requires --brush-sequence --brush-texture-patch-only"));
  }
  if (corpus_path.isEmpty()) {
    return fail(QStringLiteral(
        "Usage: preview_benchmark_driver --corpus <manifest.json> [--limit N] [--iterations N] [--brush-tile-span N] [--edit-sweep] [--global-burst] [--repeat-full-cache] [--output-profile-toggle] [--rapid-supersede] [--viewport-max-tile] [--viewport-pan-heavy-edit] [--brush-tile-cache] [--brush-sequence] [--brush-overbroad-dirty] [--brush-texture-patch-only] [--brush-patch-chain]"));
  }
  const auto workload_count =
      (apply_edit_sweep ? 1 : 0) + (apply_global_burst ? 1 : 0) + (repeat_full_cache ? 1 : 0) +
      (output_profile_toggle ? 1 : 0) + (rapid_supersede ? 1 : 0) +
      (viewport_max_tile ? 1 : 0) + (viewport_pan_heavy_edit ? 1 : 0) +
      (brush_tile_cache ? 1 : 0);
  if (workload_count > 1) {
    return fail(QStringLiteral(
        "--edit-sweep, --global-burst, --repeat-full-cache, --output-profile-toggle, --rapid-supersede, --viewport-max-tile, --viewport-pan-heavy-edit, and --brush-tile-cache are separate benchmark workloads."));
  }
  const auto plain_corpus_smoke = workload_count == 0;

  photo_editor::desktop::observability::initialize();
  photo_editor::desktop::observability::logInfo(
      photoEditorAppLog(),
      QStringLiteral("desktop.startup.ready"),
      {
          {QStringLiteral("elapsed_ms"), 0},
          {QStringLiteral("benchmark_driver"), QStringLiteral("preview")},
      });

  QFile manifest_file(corpus_path);
  if (!manifest_file.open(QIODevice::ReadOnly)) {
    return fail(QStringLiteral("Unable to open corpus manifest: %1").arg(corpus_path));
  }
  const auto manifest_document = QJsonDocument::fromJson(manifest_file.readAll());
  if (!manifest_document.isObject()) {
    return fail(QStringLiteral("Corpus manifest must be a JSON object."));
  }
  const auto assets = manifest_document.object().value(QStringLiteral("assets")).toArray();
  if (assets.isEmpty()) {
    return fail(QStringLiteral("Corpus manifest contains no assets."));
  }

  BenchmarkImageSink image_sink;
  photo_editor::desktop::EditorPreviewController controller;
  controller.setImageProvider(&image_sink);
  controller.setPreviewTargetEdge(960);
  BenchmarkPreviewTexture* viewport_texture = nullptr;
  if (viewport_max_tile || viewport_pan_heavy_edit) {
    auto texture = std::make_unique<BenchmarkPreviewTexture>();
    viewport_texture = texture.get();
    photo_editor::desktop::EditorPreviewControllerTestAccess::set_display_preview_texture(
        controller,
        std::move(texture));
  }

  QTemporaryDir synthetic_tile_temp_dir;
  QString synthetic_tile_source_path;
  if (brush_tile_cache || viewport_max_tile || viewport_pan_heavy_edit) {
    synthetic_tile_source_path =
        synthetic_tile_temp_dir.filePath(QStringLiteral("preview-tile-source.png"));
    if (!synthetic_tile_temp_dir.isValid() ||
        !write_brush_tile_benchmark_source(synthetic_tile_source_path)) {
      return fail(QStringLiteral("Unable to create preview tile benchmark source image."));
    }
  }

  QElapsedTimer total_timer;
  total_timer.start();
  auto rendered_count = 0;
  auto rapid_supersede_cancelled_count = 0;
  for (const auto& value : assets) {
    if (!value.isObject()) {
      continue;
    }
    if (limit > 0 && rendered_count >= limit) {
      break;
    }
    const auto asset = value.toObject();
    const auto asset_id = asset.value(QStringLiteral("asset_id")).toString().trimmed();
    const auto manifest_file_path = asset.value(QStringLiteral("path")).toString().trimmed();
    const auto file_path =
        (brush_tile_cache || viewport_max_tile || viewport_pan_heavy_edit) ? synthetic_tile_source_path
                                                                          : manifest_file_path;
    const auto recipe_path = asset.value(QStringLiteral("recipe")).toString().trimmed();
    if (asset_id.isEmpty() || file_path.isEmpty()) {
      continue;
    }

    const auto recipe_json =
        recipe_path.isEmpty() || apply_global_burst || viewport_max_tile ||
                viewport_pan_heavy_edit || brush_tile_cache
            ? QString()
            : read_text_file(recipe_path);
    QElapsedTimer asset_timer;
    asset_timer.start();
    controller.loadAsset(asset_id, file_path, QString(), QString(), QString(), QString(), recipe_json);
    if (!wait_for_preview_idle(controller, timeout_ms)) {
      return fail(QStringLiteral("Preview render timed out for %1").arg(asset_id));
    }
    if (plain_corpus_smoke && !wait_for_histogram_settled(controller, timeout_ms)) {
      return fail(QStringLiteral("Preview histogram timed out for %1").arg(asset_id));
    }
    if (apply_edit_sweep) {
      QElapsedTimer edit_timer;
      edit_timer.start();
      controller.setExposure(0.35);
      controller.setContrast(18.0);
      controller.setHighlights(-24.0);
      controller.setShadows(22.0);
      controller.setVibrance(16.0);
      controller.setOutputColorSpace(QStringLiteral("Display P3"));
      if (!wait_for_preview_idle(controller, timeout_ms)) {
        return fail(QStringLiteral("Preview edit sweep timed out for %1").arg(asset_id));
      }
      photo_editor::desktop::observability::logInfo(
          photoEditorPerfLog(),
          QStringLiteral("benchmark.preview_edit_sweep.completed"),
          {
              {QStringLiteral("asset_id"), asset_id},
              {QStringLiteral("file_path"), file_path},
              {QStringLiteral("elapsed_ms"), edit_timer.elapsed()},
          });
    }
    if (apply_global_burst) {
      std::vector<double> burst_samples;
      burst_samples.reserve(static_cast<std::size_t>(iterations));
      for (int iteration = 0; iteration < iterations; ++iteration) {
        const auto exposure = (iteration % 2) == 0 ? 0.35 : 0.15;
        const auto contrast = (iteration % 2) == 0 ? 18.0 : 6.0;
        QElapsedTimer edit_timer;
        edit_timer.start();
        controller.setExposure(exposure);
        controller.setContrast(contrast);
        if (!wait_for_preview_idle(controller, timeout_ms)) {
          return fail(QStringLiteral("Preview global burst timed out for %1").arg(asset_id));
        }
        const auto edit_elapsed_ms = edit_timer.elapsed();
        burst_samples.push_back(static_cast<double>(edit_elapsed_ms));
        photo_editor::desktop::observability::logInfo(
            photoEditorPerfLog(),
            QStringLiteral("benchmark.preview_global_burst.sample_completed"),
            {
                {QStringLiteral("asset_id"), asset_id},
                {QStringLiteral("file_path"), file_path},
                {QStringLiteral("iteration"), iteration + 1},
                {QStringLiteral("iterations"), iterations},
                {QStringLiteral("elapsed_ms"), edit_elapsed_ms},
                {QStringLiteral("exposure"), exposure},
                {QStringLiteral("contrast"), contrast},
            });
      }
      const auto burst_summary = summarize_samples(burst_samples);
      photo_editor::desktop::observability::logInfo(
          photoEditorPerfLog(),
          QStringLiteral("benchmark.preview_global_burst.completed"),
          {
              {QStringLiteral("asset_id"), asset_id},
              {QStringLiteral("file_path"), file_path},
              {QStringLiteral("iterations"), iterations},
              {QStringLiteral("elapsed_ms"), burst_summary.p50_ms},
              {QStringLiteral("elapsed_total_ms"), burst_summary.elapsed_ms},
              {QStringLiteral("avg_ms"), burst_summary.avg_ms},
              {QStringLiteral("p50_ms"), burst_summary.p50_ms},
              {QStringLiteral("p90_ms"), burst_summary.p90_ms},
              {QStringLiteral("p95_ms"), burst_summary.p95_ms},
              {QStringLiteral("p99_ms"), burst_summary.p99_ms},
              {QStringLiteral("max_ms"), burst_summary.max_ms},
              {QStringLiteral("stddev_ms"), burst_summary.stddev_ms},
              {QStringLiteral("mad_ms"), burst_summary.mad_ms},
          });
    }
    if (repeat_full_cache) {
      std::vector<double> repeat_samples;
      repeat_samples.reserve(static_cast<std::size_t>(iterations));
      for (int iteration = 0; iteration < iterations; ++iteration) {
        const auto cache_hits_before_retry =
            photo_editor::desktop::EditorPreviewControllerTestAccess::disk_preview_cache_hit_count(
                controller);
        QElapsedTimer repeat_timer;
        repeat_timer.start();
        if (!controller.retryPreviewRender()) {
          return fail(QStringLiteral("Preview full cache retry was not accepted for %1").arg(asset_id));
        }
        if (!wait_for_disk_preview_cache_hit(controller, cache_hits_before_retry, timeout_ms)) {
          return fail(QStringLiteral("Preview full cache repeat timed out for %1").arg(asset_id));
        }
        const auto repeat_elapsed_ms = repeat_timer.elapsed();
        if (!wait_for_preview_idle(controller, timeout_ms)) {
          return fail(QStringLiteral("Preview full cache repeat failed to settle for %1").arg(asset_id));
        }
        repeat_samples.push_back(static_cast<double>(repeat_elapsed_ms));
        photo_editor::desktop::observability::logInfo(
            photoEditorPerfLog(),
            QStringLiteral("benchmark.preview_full_cache_repeat.sample_completed"),
            {
                {QStringLiteral("asset_id"), asset_id},
                {QStringLiteral("file_path"), file_path},
                {QStringLiteral("iteration"), iteration + 1},
                {QStringLiteral("iterations"), iterations},
                {QStringLiteral("elapsed_ms"), repeat_elapsed_ms},
            });
      }
      const auto repeat_summary = summarize_samples(repeat_samples);
      photo_editor::desktop::observability::logInfo(
          photoEditorPerfLog(),
          QStringLiteral("benchmark.preview_full_cache_repeat.completed"),
          {
              {QStringLiteral("asset_id"), asset_id},
              {QStringLiteral("file_path"), file_path},
              {QStringLiteral("iterations"), iterations},
              {QStringLiteral("elapsed_ms"), repeat_summary.p50_ms},
              {QStringLiteral("elapsed_total_ms"), repeat_summary.elapsed_ms},
              {QStringLiteral("avg_ms"), repeat_summary.avg_ms},
              {QStringLiteral("p50_ms"), repeat_summary.p50_ms},
              {QStringLiteral("p90_ms"), repeat_summary.p90_ms},
              {QStringLiteral("p95_ms"), repeat_summary.p95_ms},
              {QStringLiteral("p99_ms"), repeat_summary.p99_ms},
              {QStringLiteral("max_ms"), repeat_summary.max_ms},
              {QStringLiteral("stddev_ms"), repeat_summary.stddev_ms},
              {QStringLiteral("mad_ms"), repeat_summary.mad_ms},
          });
    }
    if (output_profile_toggle) {
      controller.setExposure(0.35);
      if (!wait_for_preview_idle(controller, timeout_ms)) {
        return fail(QStringLiteral("Preview output profile cache warmup timed out for %1").arg(asset_id));
      }
      std::vector<double> toggle_samples;
      toggle_samples.reserve(static_cast<std::size_t>(iterations));
      for (int iteration = 0; iteration < iterations; ++iteration) {
        const auto target_profile =
            (iteration % 2) == 0 ? QStringLiteral("Display P3") : QStringLiteral("sRGB");
        QElapsedTimer toggle_timer;
        toggle_timer.start();
        controller.setOutputColorSpace(target_profile);
        if (!wait_for_preview_idle(controller, timeout_ms)) {
          return fail(QStringLiteral("Preview output profile toggle timed out for %1").arg(asset_id));
        }
        const auto toggle_elapsed_ms = toggle_timer.elapsed();
        toggle_samples.push_back(static_cast<double>(toggle_elapsed_ms));
        photo_editor::desktop::observability::logInfo(
            photoEditorPerfLog(),
            QStringLiteral("benchmark.preview_output_profile_toggle.sample_completed"),
            {
                {QStringLiteral("asset_id"), asset_id},
                {QStringLiteral("file_path"), file_path},
                {QStringLiteral("iteration"), iteration + 1},
                {QStringLiteral("iterations"), iterations},
                {QStringLiteral("elapsed_ms"), toggle_elapsed_ms},
                {QStringLiteral("output_color_space"), target_profile},
            });
      }
      const auto toggle_summary = summarize_samples(toggle_samples);
      photo_editor::desktop::observability::logInfo(
          photoEditorPerfLog(),
          QStringLiteral("benchmark.preview_output_profile_toggle.completed"),
          {
              {QStringLiteral("asset_id"), asset_id},
              {QStringLiteral("file_path"), file_path},
              {QStringLiteral("iterations"), iterations},
              {QStringLiteral("elapsed_ms"), toggle_summary.p50_ms},
              {QStringLiteral("elapsed_total_ms"), toggle_summary.elapsed_ms},
              {QStringLiteral("avg_ms"), toggle_summary.avg_ms},
              {QStringLiteral("p50_ms"), toggle_summary.p50_ms},
              {QStringLiteral("p90_ms"), toggle_summary.p90_ms},
              {QStringLiteral("p95_ms"), toggle_summary.p95_ms},
              {QStringLiteral("p99_ms"), toggle_summary.p99_ms},
              {QStringLiteral("max_ms"), toggle_summary.max_ms},
              {QStringLiteral("stddev_ms"), toggle_summary.stddev_ms},
              {QStringLiteral("mad_ms"), toggle_summary.mad_ms},
          });
    }
    if (rapid_supersede) {
      std::vector<double> supersede_samples;
      supersede_samples.reserve(static_cast<std::size_t>(iterations));
      auto cancelled_delta_total = 0;
      auto full_cancelled_delta_total = 0;
      auto missed_count = 0;
      auto max_last_full_render_ms = qint64{0};
      for (int iteration = 0; iteration < iterations; ++iteration) {
        const auto stale_exposure = 0.25 + 0.001 * static_cast<double>(iteration);
        const auto final_contrast = 22.0 + 0.05 * static_cast<double>(iteration);
        photo_editor::desktop::EditorPreviewController supersede_controller;
        supersede_controller.setImageProvider(&image_sink);
        supersede_controller.setPreviewTargetEdge(4096);
        supersede_controller.loadAsset(
            asset_id,
            file_path,
            QString(),
            QString(),
            QString(),
            QString(),
            recipe_json);
        if (!wait_for_preview_idle(supersede_controller, timeout_ms)) {
          return fail(QStringLiteral("Preview rapid-supersede warmup timed out for %1").arg(asset_id));
        }

        QElapsedTimer supersede_timer;
        supersede_timer.start();
        supersede_controller.setExposure(stale_exposure);
        supersede_controller.setTexture(32.0);
        supersede_controller.setClarity(28.0);
        supersede_controller.setDehaze(12.0);
        supersede_controller.setSharpening(38.0);
        photo_editor::desktop::EditorPreviewControllerTestAccess::start_full_render_now(
            supersede_controller);
        if (!wait_for_full_render_running(supersede_controller, timeout_ms)) {
          return fail(
              QStringLiteral("Preview rapid-supersede full render did not start for %1").arg(asset_id));
        }
        const auto cancel_count_before = render_cancelled_count(supersede_controller);
        const auto full_cancel_count_before =
            photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_cancelled_count(
                supersede_controller);
        const auto superseded_generation =
            photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
                supersede_controller);
        supersede_controller.setContrast(final_contrast);
        if (!wait_for_preview_idle(supersede_controller, timeout_ms)) {
          return fail(QStringLiteral("Preview rapid supersede timed out for %1").arg(asset_id));
        }
        const auto cancel_count_after = render_cancelled_count(supersede_controller);
        const auto full_cancel_count_after =
            photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_cancelled_count(
                supersede_controller);
        const auto cancelled_delta = cancel_count_after - cancel_count_before;
        const auto full_cancelled_delta = full_cancel_count_after - full_cancel_count_before;
        const auto sample_elapsed_ms = supersede_timer.elapsed();
        const auto final_generation =
            photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
                supersede_controller);
        const auto last_full_render_ms =
            photo_editor::desktop::EditorPreviewControllerTestAccess::last_full_render_ms(
                supersede_controller);
        cancelled_delta_total += cancelled_delta;
        full_cancelled_delta_total += full_cancelled_delta;
        max_last_full_render_ms = std::max(max_last_full_render_ms, last_full_render_ms);
        if (cancelled_delta <= 0 || full_cancelled_delta <= 0) {
          ++missed_count;
        }
        supersede_samples.push_back(static_cast<double>(sample_elapsed_ms));
        photo_editor::desktop::observability::logInfo(
            photoEditorPerfLog(),
            cancelled_delta > 0 && full_cancelled_delta > 0
                ? QStringLiteral("benchmark.preview_rapid_supersede.sample_completed")
                : QStringLiteral("benchmark.preview_rapid_supersede.sample_missed"),
            {
                {QStringLiteral("asset_id"), asset_id},
                {QStringLiteral("file_path"), file_path},
                {QStringLiteral("iteration"), iteration + 1},
                {QStringLiteral("iterations"), iterations},
                {QStringLiteral("elapsed_ms"), sample_elapsed_ms},
                {QStringLiteral("superseded_generation"), superseded_generation},
                {QStringLiteral("final_generation"), final_generation},
                {QStringLiteral("stale_exposure"), stale_exposure},
                {QStringLiteral("final_contrast"), final_contrast},
                {QStringLiteral("cancelled_delta"), cancelled_delta},
                {QStringLiteral("full_cancelled_delta"), full_cancelled_delta},
                {QStringLiteral("cancelled_total"), cancel_count_after},
                {QStringLiteral("last_full_render_ms"), last_full_render_ms},
            });
      }
      const auto supersede_summary = summarize_samples(supersede_samples);
      photo_editor::desktop::observability::logInfo(
          photoEditorPerfLog(),
          missed_count == 0 ? QStringLiteral("benchmark.preview_rapid_supersede.completed")
                            : QStringLiteral("benchmark.preview_rapid_supersede.missed"),
          {
              {QStringLiteral("asset_id"), asset_id},
              {QStringLiteral("file_path"), file_path},
              {QStringLiteral("iterations"), iterations},
              {QStringLiteral("elapsed_ms"), supersede_summary.p50_ms},
              {QStringLiteral("elapsed_total_ms"), supersede_summary.elapsed_ms},
              {QStringLiteral("avg_ms"), supersede_summary.avg_ms},
              {QStringLiteral("p50_ms"), supersede_summary.p50_ms},
              {QStringLiteral("p90_ms"), supersede_summary.p90_ms},
              {QStringLiteral("p95_ms"), supersede_summary.p95_ms},
              {QStringLiteral("p99_ms"), supersede_summary.p99_ms},
              {QStringLiteral("max_ms"), supersede_summary.max_ms},
              {QStringLiteral("stddev_ms"), supersede_summary.stddev_ms},
              {QStringLiteral("mad_ms"), supersede_summary.mad_ms},
              {QStringLiteral("cancelled_delta"), cancelled_delta_total},
              {QStringLiteral("full_cancelled_delta"), full_cancelled_delta_total},
              {QStringLiteral("missed_count"), missed_count},
              {QStringLiteral("max_last_full_render_ms"), max_last_full_render_ms},
          });
      rapid_supersede_cancelled_count += cancelled_delta_total;
      if (missed_count > 0) {
        return fail(QStringLiteral("Rapid supersede workload missed %1 cancellations.").arg(missed_count));
      }
    }
    if (viewport_max_tile) {
      if (viewport_texture == nullptr) {
        return fail(QStringLiteral("Viewport max-tile benchmark texture was not initialized."));
      }
      controller.setExposure(0.20);
      if (!wait_for_preview_idle(controller, timeout_ms)) {
        return fail(QStringLiteral("Viewport max-tile cache warmup timed out for %1").arg(asset_id));
      }
      auto viewport_seed_request =
          photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_request(
              controller);
      viewport_seed_request.use_in_memory_preview = true;
      viewport_seed_request.render_reason = QStringLiteral("viewport_change");
      viewport_seed_request.dirty_domain = QStringLiteral("source_decode");
      viewport_seed_request.full_invalidation = false;
      viewport_seed_request.dirty_region = QRectF(0.0, 0.0, 1.0, 1.0);
      viewport_seed_request.state.exposure = 0.20;
      const auto viewport_seed_result =
          photo_editor::desktop::EditorPreviewControllerTestAccess::render_preview_request_sync(
              viewport_seed_request);
      if (!viewport_seed_result.success ||
          viewport_seed_result.preview_image.size() != QSize(2048, 2048) ||
          !viewport_texture->upload(viewport_seed_result.preview_image)) {
        return fail(QStringLiteral("Viewport max-tile native texture seed failed for %1").arg(asset_id));
      }
      if (!viewport_texture->hasNativeTexture() ||
          viewport_texture->nativeTextureSize() != QSize(2048, 2048)) {
        return fail(QStringLiteral("Viewport max-tile warmup did not initialize native texture."));
      }
      const auto expected_patch_rect = QRect(1536, 1536, 512, 512);
      controller.setPreviewViewport(0, 0, 0, 0, 0, false);
      const auto warmup_patch_count_before = viewport_texture->patchUploadCount();
      const auto warmup_schedule_count_before =
          photo_editor::desktop::EditorPreviewControllerTestAccess::
              viewport_max_preview_schedule_count(controller);
      const auto warmup_prepass_count_before =
          photo_editor::desktop::EditorPreviewControllerTestAccess::
              visible_tile_prepass_schedule_count(controller);
      controller.setPreviewViewport(0.75, 0.75, 0.125, 0.125, 512, true);
      if (!wait_for_preview_idle(controller, timeout_ms)) {
        return fail(QStringLiteral("Viewport max-tile warmup render timed out for %1").arg(asset_id));
      }
      if (viewport_texture->patchUploadCount() - warmup_patch_count_before != 1 ||
          photo_editor::desktop::EditorPreviewControllerTestAccess::
                  viewport_max_preview_schedule_count(controller) -
                  warmup_schedule_count_before !=
              1 ||
          photo_editor::desktop::EditorPreviewControllerTestAccess::
                  visible_tile_prepass_schedule_count(controller) -
                  warmup_prepass_count_before !=
              1 ||
          viewport_texture->lastPatchUploadRect() != expected_patch_rect) {
        return fail(QStringLiteral("Viewport max-tile warmup proof failed for %1").arg(asset_id));
      }
      std::vector<double> viewport_samples;
      viewport_samples.reserve(static_cast<std::size_t>(iterations));
      auto total_patch_count_delta = 0;
      auto total_schedule_count_delta = 0;
      auto total_prepass_count_delta = 0;
      for (int iteration = 0; iteration < iterations; ++iteration) {
        controller.setPreviewViewport(0, 0, 0, 0, 0, false);
        const auto patch_count_before = viewport_texture->patchUploadCount();
        const auto schedule_count_before =
            photo_editor::desktop::EditorPreviewControllerTestAccess::
                viewport_max_preview_schedule_count(controller);
        const auto prepass_count_before =
            photo_editor::desktop::EditorPreviewControllerTestAccess::
                visible_tile_prepass_schedule_count(controller);
        QElapsedTimer viewport_timer;
        viewport_timer.start();
        controller.setPreviewViewport(0.75, 0.75, 0.125, 0.125, 512, true);
        if (!wait_for_preview_idle(controller, timeout_ms)) {
          return fail(QStringLiteral("Viewport max-tile render timed out for %1").arg(asset_id));
        }
        const auto viewport_elapsed_ms = viewport_timer.elapsed();
        const auto patch_count_delta = viewport_texture->patchUploadCount() - patch_count_before;
        const auto schedule_count_delta =
            photo_editor::desktop::EditorPreviewControllerTestAccess::
                viewport_max_preview_schedule_count(controller) -
            schedule_count_before;
        const auto prepass_count_delta =
            photo_editor::desktop::EditorPreviewControllerTestAccess::
                visible_tile_prepass_schedule_count(controller) -
            prepass_count_before;
        if (schedule_count_delta != 1 || prepass_count_delta != 1 || patch_count_delta != 1 ||
            viewport_texture->lastPatchUploadRect() != expected_patch_rect) {
          return fail(QStringLiteral(
                          "Viewport max-tile proof failed for %1: iteration=%2 schedule_delta=%3 prepass_delta=%4 patch_delta=%5 patch_rect=%6,%7,%8,%9")
                          .arg(asset_id)
                          .arg(iteration + 1)
                          .arg(schedule_count_delta)
                          .arg(prepass_count_delta)
                          .arg(patch_count_delta)
                          .arg(viewport_texture->lastPatchUploadRect().x())
                          .arg(viewport_texture->lastPatchUploadRect().y())
                          .arg(viewport_texture->lastPatchUploadRect().width())
                          .arg(viewport_texture->lastPatchUploadRect().height()));
        }
        viewport_samples.push_back(static_cast<double>(viewport_elapsed_ms));
        total_patch_count_delta += patch_count_delta;
        total_schedule_count_delta += schedule_count_delta;
        total_prepass_count_delta += prepass_count_delta;
        photo_editor::desktop::observability::logInfo(
            photoEditorPerfLog(),
            QStringLiteral("benchmark.preview_viewport_max_tile.sample_completed"),
            {
                {QStringLiteral("asset_id"), asset_id},
                {QStringLiteral("file_path"), file_path},
                {QStringLiteral("iteration"), iteration + 1},
                {QStringLiteral("iterations"), iterations},
                {QStringLiteral("elapsed_ms"), viewport_elapsed_ms},
                {QStringLiteral("viewport_x"), 0.75},
                {QStringLiteral("viewport_y"), 0.75},
                {QStringLiteral("viewport_width"), 0.125},
                {QStringLiteral("viewport_height"), 0.125},
                {QStringLiteral("patch_upload_delta"), patch_count_delta},
                {QStringLiteral("viewport_schedule_delta"), schedule_count_delta},
                {QStringLiteral("visible_prepass_delta"), prepass_count_delta},
                {QStringLiteral("patch_x"), viewport_texture->lastPatchUploadRect().x()},
                {QStringLiteral("patch_y"), viewport_texture->lastPatchUploadRect().y()},
                {QStringLiteral("patch_width"), viewport_texture->lastPatchUploadRect().width()},
                {QStringLiteral("patch_height"), viewport_texture->lastPatchUploadRect().height()},
            });
      }
      const auto viewport_summary = summarize_samples(viewport_samples);
      photo_editor::desktop::observability::logInfo(
          photoEditorPerfLog(),
          QStringLiteral("benchmark.preview_viewport_max_tile.completed"),
          {
              {QStringLiteral("asset_id"), asset_id},
              {QStringLiteral("file_path"), file_path},
              {QStringLiteral("iterations"), iterations},
              {QStringLiteral("elapsed_ms"), viewport_summary.p50_ms},
              {QStringLiteral("elapsed_total_ms"), viewport_summary.elapsed_ms},
              {QStringLiteral("avg_ms"), viewport_summary.avg_ms},
              {QStringLiteral("p50_ms"), viewport_summary.p50_ms},
              {QStringLiteral("p90_ms"), viewport_summary.p90_ms},
              {QStringLiteral("p95_ms"), viewport_summary.p95_ms},
              {QStringLiteral("p99_ms"), viewport_summary.p99_ms},
              {QStringLiteral("max_ms"), viewport_summary.max_ms},
              {QStringLiteral("stddev_ms"), viewport_summary.stddev_ms},
              {QStringLiteral("mad_ms"), viewport_summary.mad_ms},
              {QStringLiteral("viewport_x"), 0.75},
              {QStringLiteral("viewport_y"), 0.75},
              {QStringLiteral("viewport_width"), 0.125},
              {QStringLiteral("viewport_height"), 0.125},
              {QStringLiteral("patch_upload_delta"), 1},
              {QStringLiteral("viewport_schedule_delta"), 1},
              {QStringLiteral("visible_prepass_delta"), 1},
              {QStringLiteral("patch_upload_delta_total"), total_patch_count_delta},
              {QStringLiteral("viewport_schedule_delta_total"), total_schedule_count_delta},
              {QStringLiteral("visible_prepass_delta_total"), total_prepass_count_delta},
              {QStringLiteral("patch_x"), viewport_texture->lastPatchUploadRect().x()},
              {QStringLiteral("patch_y"), viewport_texture->lastPatchUploadRect().y()},
              {QStringLiteral("patch_width"), viewport_texture->lastPatchUploadRect().width()},
              {QStringLiteral("patch_height"), viewport_texture->lastPatchUploadRect().height()},
          });
    }
    if (viewport_pan_heavy_edit) {
      if (viewport_texture == nullptr) {
        return fail(QStringLiteral("Viewport pan-heavy-edit benchmark texture was not initialized."));
      }

      controller.setExposure(0.20);
      controller.setTexture(30.0);
      controller.setClarity(28.0);
      controller.setDehaze(12.0);
      controller.setSharpening(38.0);
      if (!wait_for_preview_idle(controller, timeout_ms)) {
        return fail(QStringLiteral("Viewport pan-heavy-edit cache warmup timed out for %1").arg(asset_id));
      }

      auto viewport_seed_request =
          photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_request(
              controller);
      viewport_seed_request.use_in_memory_preview = true;
      viewport_seed_request.render_reason = QStringLiteral("viewport_change");
      viewport_seed_request.dirty_domain = QStringLiteral("source_decode");
      viewport_seed_request.full_invalidation = false;
      viewport_seed_request.dirty_region = QRectF(0.0, 0.0, 1.0, 1.0);
      const auto viewport_seed_result =
          photo_editor::desktop::EditorPreviewControllerTestAccess::render_preview_request_sync(
              viewport_seed_request);
      if (!viewport_seed_result.success ||
          viewport_seed_result.preview_image.size() != QSize(2048, 2048) ||
          !viewport_texture->upload(viewport_seed_result.preview_image)) {
        return fail(QStringLiteral("Viewport pan-heavy-edit native texture seed failed for %1").arg(asset_id));
      }
      if (!viewport_texture->hasNativeTexture() ||
          viewport_texture->nativeTextureSize() != QSize(2048, 2048)) {
        return fail(QStringLiteral("Viewport pan-heavy-edit warmup did not initialize native texture."));
      }

      constexpr auto final_state_count = std::size_t{2};
      const std::array<double, final_state_count> final_texture_values{30.281, 30.312};
      const std::array<double, final_state_count> final_contrast_values{20.037, 20.074};
      for (std::size_t state_index = 0; state_index < final_state_count; ++state_index) {
        controller.setTexture(final_texture_values[state_index]);
        controller.setContrast(final_contrast_values[state_index]);
        if (!wait_for_preview_idle(controller, timeout_ms)) {
          return fail(QStringLiteral(
                          "Viewport pan-heavy-edit final state cache warmup timed out for %1")
                          .arg(asset_id));
        }
      }

      const auto expected_patch_rect = QRect(1536, 1536, 512, 512);
      std::vector<double> viewport_edit_samples;
      viewport_edit_samples.reserve(static_cast<std::size_t>(iterations));
      auto total_patch_count_delta = 0;
      auto total_schedule_count_delta = 0;
      auto total_prepass_count_delta = 0;
      auto cancelled_delta_total = 0;
      auto full_cancelled_delta_total = 0;
      auto missed_count = 0;
      auto max_last_full_render_ms = qint64{0};
      for (int iteration = 0; iteration < iterations; ++iteration) {
        controller.setPreviewViewport(0, 0, 0, 0, 0, false);
        const auto patch_count_before = viewport_texture->patchUploadCount();
        const auto schedule_count_before =
            photo_editor::desktop::EditorPreviewControllerTestAccess::
                viewport_max_preview_schedule_count(controller);
        const auto prepass_count_before =
            photo_editor::desktop::EditorPreviewControllerTestAccess::
                visible_tile_prepass_schedule_count(controller);

        QElapsedTimer sample_timer;
        sample_timer.start();
        QElapsedTimer pan_timer;
        pan_timer.start();
        controller.setPreviewViewport(0.75, 0.75, 0.125, 0.125, 512, true);
        if (!wait_for_preview_idle(controller, timeout_ms)) {
          return fail(QStringLiteral("Viewport pan-heavy-edit pan render timed out for %1").arg(asset_id));
        }
        const auto pan_elapsed_ms = pan_timer.elapsed();

        const auto patch_count_delta = viewport_texture->patchUploadCount() - patch_count_before;
        const auto schedule_count_delta =
            photo_editor::desktop::EditorPreviewControllerTestAccess::
                viewport_max_preview_schedule_count(controller) -
            schedule_count_before;
        const auto prepass_count_delta =
            photo_editor::desktop::EditorPreviewControllerTestAccess::
                visible_tile_prepass_schedule_count(controller) -
            prepass_count_before;
        if (schedule_count_delta != 1 || prepass_count_delta != 1 || patch_count_delta != 1 ||
            viewport_texture->lastPatchUploadRect() != expected_patch_rect) {
          return fail(QStringLiteral(
                          "Viewport pan-heavy-edit tile proof failed for %1: iteration=%2 schedule_delta=%3 prepass_delta=%4 patch_delta=%5 patch_rect=%6,%7,%8,%9")
                          .arg(asset_id)
                          .arg(iteration + 1)
                          .arg(schedule_count_delta)
                          .arg(prepass_count_delta)
                          .arg(patch_count_delta)
                          .arg(viewport_texture->lastPatchUploadRect().x())
                          .arg(viewport_texture->lastPatchUploadRect().y())
                          .arg(viewport_texture->lastPatchUploadRect().width())
                          .arg(viewport_texture->lastPatchUploadRect().height()));
        }

        const auto final_state_index =
            static_cast<std::size_t>(iteration) % final_state_count;
        const auto stale_texture = final_texture_values[final_state_index];
        const auto final_contrast = final_contrast_values[final_state_index];
        controller.setTexture(stale_texture);
        photo_editor::desktop::EditorPreviewControllerTestAccess::start_full_render_now(
            controller);
        if (!wait_for_full_render_running(controller, timeout_ms)) {
          return fail(QStringLiteral("Viewport pan-heavy-edit full render did not start for %1").arg(asset_id));
        }
        const auto cancel_count_before = render_cancelled_count(controller);
        const auto full_cancel_count_before =
            photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_cancelled_count(
                controller);
        const auto superseded_generation =
            photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(controller);
        QElapsedTimer edit_timer;
        edit_timer.start();
        controller.setContrast(final_contrast);
        if (!wait_for_render_cancellation(
                controller,
                cancel_count_before,
                full_cancel_count_before,
                superseded_generation,
                timeout_ms)) {
          return fail(QStringLiteral("Viewport pan-heavy-edit supersede cancellation timed out for %1")
                          .arg(asset_id));
        }
        const auto edit_elapsed_ms = edit_timer.elapsed();
        const auto sample_elapsed_ms = sample_timer.elapsed();
        const auto cancel_count_after = render_cancelled_count(controller);
        const auto full_cancel_count_after =
            photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_cancelled_count(
                controller);
        const auto cancelled_delta = cancel_count_after - cancel_count_before;
        const auto full_cancelled_delta = full_cancel_count_after - full_cancel_count_before;
        const auto final_generation =
            photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(controller);
        const auto last_full_render_ms =
            photo_editor::desktop::EditorPreviewControllerTestAccess::last_full_render_ms(controller);
        if (cancelled_delta <= 0 || full_cancelled_delta <= 0 ||
            final_generation <= superseded_generation) {
          ++missed_count;
        }
        total_patch_count_delta += patch_count_delta;
        total_schedule_count_delta += schedule_count_delta;
        total_prepass_count_delta += prepass_count_delta;
        cancelled_delta_total += cancelled_delta;
        full_cancelled_delta_total += full_cancelled_delta;
        max_last_full_render_ms = std::max(max_last_full_render_ms, last_full_render_ms);
        viewport_edit_samples.push_back(static_cast<double>(sample_elapsed_ms));
        photo_editor::desktop::observability::logInfo(
            photoEditorPerfLog(),
            cancelled_delta > 0 && full_cancelled_delta > 0 &&
                    final_generation > superseded_generation
                ? QStringLiteral("benchmark.preview_viewport_pan_heavy_edit.sample_completed")
                : QStringLiteral("benchmark.preview_viewport_pan_heavy_edit.sample_missed"),
            {
                {QStringLiteral("asset_id"), asset_id},
                {QStringLiteral("file_path"), file_path},
                {QStringLiteral("iteration"), iteration + 1},
                {QStringLiteral("iterations"), iterations},
                {QStringLiteral("elapsed_ms"), sample_elapsed_ms},
                {QStringLiteral("pan_elapsed_ms"), pan_elapsed_ms},
                {QStringLiteral("edit_elapsed_ms"), edit_elapsed_ms},
                {QStringLiteral("viewport_x"), 0.75},
                {QStringLiteral("viewport_y"), 0.75},
                {QStringLiteral("viewport_width"), 0.125},
                {QStringLiteral("viewport_height"), 0.125},
                {QStringLiteral("patch_upload_delta"), patch_count_delta},
                {QStringLiteral("viewport_schedule_delta"), schedule_count_delta},
                {QStringLiteral("visible_prepass_delta"), prepass_count_delta},
                {QStringLiteral("patch_x"), viewport_texture->lastPatchUploadRect().x()},
                {QStringLiteral("patch_y"), viewport_texture->lastPatchUploadRect().y()},
                {QStringLiteral("patch_width"), viewport_texture->lastPatchUploadRect().width()},
                {QStringLiteral("patch_height"), viewport_texture->lastPatchUploadRect().height()},
                {QStringLiteral("superseded_generation"), superseded_generation},
                {QStringLiteral("final_generation"), final_generation},
                {QStringLiteral("stale_texture"), stale_texture},
                {QStringLiteral("final_contrast"), final_contrast},
                {QStringLiteral("cancelled_delta"), cancelled_delta},
                {QStringLiteral("full_cancelled_delta"), full_cancelled_delta},
                {QStringLiteral("last_full_render_ms"), last_full_render_ms},
            });
      }
      if (!wait_for_preview_idle(controller, timeout_ms)) {
        return fail(QStringLiteral("Viewport pan-heavy-edit final settle timed out for %1").arg(asset_id));
      }
      max_last_full_render_ms =
          std::max(max_last_full_render_ms,
                   photo_editor::desktop::EditorPreviewControllerTestAccess::last_full_render_ms(
                       controller));
      const auto viewport_edit_summary = summarize_samples(viewport_edit_samples);
      photo_editor::desktop::observability::logInfo(
          photoEditorPerfLog(),
          missed_count == 0 ? QStringLiteral("benchmark.preview_viewport_pan_heavy_edit.completed")
                            : QStringLiteral("benchmark.preview_viewport_pan_heavy_edit.missed"),
          {
              {QStringLiteral("asset_id"), asset_id},
              {QStringLiteral("file_path"), file_path},
              {QStringLiteral("iterations"), iterations},
              {QStringLiteral("elapsed_ms"), viewport_edit_summary.p50_ms},
              {QStringLiteral("elapsed_total_ms"), viewport_edit_summary.elapsed_ms},
              {QStringLiteral("avg_ms"), viewport_edit_summary.avg_ms},
              {QStringLiteral("p50_ms"), viewport_edit_summary.p50_ms},
              {QStringLiteral("p90_ms"), viewport_edit_summary.p90_ms},
              {QStringLiteral("p95_ms"), viewport_edit_summary.p95_ms},
              {QStringLiteral("p99_ms"), viewport_edit_summary.p99_ms},
              {QStringLiteral("max_ms"), viewport_edit_summary.max_ms},
              {QStringLiteral("stddev_ms"), viewport_edit_summary.stddev_ms},
              {QStringLiteral("mad_ms"), viewport_edit_summary.mad_ms},
              {QStringLiteral("viewport_x"), 0.75},
              {QStringLiteral("viewport_y"), 0.75},
              {QStringLiteral("viewport_width"), 0.125},
              {QStringLiteral("viewport_height"), 0.125},
              {QStringLiteral("patch_upload_delta"), 1},
              {QStringLiteral("viewport_schedule_delta"), 1},
              {QStringLiteral("visible_prepass_delta"), 1},
              {QStringLiteral("patch_upload_delta_total"), total_patch_count_delta},
              {QStringLiteral("viewport_schedule_delta_total"), total_schedule_count_delta},
              {QStringLiteral("visible_prepass_delta_total"), total_prepass_count_delta},
              {QStringLiteral("cancelled_delta"), cancelled_delta_total},
              {QStringLiteral("full_cancelled_delta"), full_cancelled_delta_total},
              {QStringLiteral("missed_count"), missed_count},
              {QStringLiteral("max_last_full_render_ms"), max_last_full_render_ms},
              {QStringLiteral("patch_x"), viewport_texture->lastPatchUploadRect().x()},
              {QStringLiteral("patch_y"), viewport_texture->lastPatchUploadRect().y()},
              {QStringLiteral("patch_width"), viewport_texture->lastPatchUploadRect().width()},
              {QStringLiteral("patch_height"), viewport_texture->lastPatchUploadRect().height()},
          });
      if (missed_count > 0) {
        return fail(QStringLiteral("Viewport pan-heavy-edit workload missed %1 cancellations.")
                        .arg(missed_count));
      }
    }
    if (brush_tile_cache) {
      qputenv("PHOTO_EDITOR_DISABLE_METAL_PREVIEW", QByteArrayLiteral("1"));
      QElapsedTimer seed_timer;
      seed_timer.start();
      auto seed_request =
          photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_request(
              controller);
      seed_request.use_in_memory_preview = true;
      seed_request.render_reason = QStringLiteral("edit_change");
      seed_request.dirty_domain = QStringLiteral("source_decode");
      seed_request.full_invalidation = false;
      seed_request.dirty_region = QRectF(0.0, 0.0, 1.0, 1.0);
      seed_request.state.exposure = 0.20;
      const auto seed_result =
          photo_editor::desktop::EditorPreviewControllerTestAccess::render_preview_request_sync(
              seed_request);
      const auto seed_elapsed_ms = seed_timer.elapsed();
      if (!seed_result.success || seed_result.executed_tile_count != 16 ||
          seed_result.executed_tile_pixels != 2048 * 2048) {
        return fail(QStringLiteral(
            "Brush tile cache seed failed for %1: success=%2 backend=%3 tiles=%4 pixels=%5")
                        .arg(asset_id)
                        .arg(seed_result.success)
                        .arg(seed_result.backend)
                        .arg(seed_result.executed_tile_count)
                        .arg(seed_result.executed_tile_pixels));
      }

      auto brush_mask = photo_editor::desktop::default_mask_definition(QStringLiteral("brush"), 1);
      brush_mask.insert(QStringLiteral("enabled"), true);
      brush_mask.insert(QStringLiteral("exposure"), 0.75);
      brush_mask.insert(QStringLiteral("brushSize"), 0.04);
      brush_mask.insert(QStringLiteral("brushFlow"), 1.0);
      brush_mask.insert(QStringLiteral("brushHardness"), 0.25);
      QVariantList brush_points;
      brush_points.reserve(brush_tile_span * brush_tile_span);
      for (int tile_y = 0; tile_y < brush_tile_span; ++tile_y) {
        for (int tile_x = 0; tile_x < brush_tile_span; ++tile_x) {
          QVariantMap brush_point;
          brush_point.insert(QStringLiteral("x"), 0.08 + 0.25 * static_cast<double>(tile_x));
          brush_point.insert(QStringLiteral("y"), 0.08 + 0.25 * static_cast<double>(tile_y));
          brush_point.insert(QStringLiteral("size"), 0.04);
          brush_point.insert(QStringLiteral("flow"), 1.0);
          brush_point.insert(QStringLiteral("pressure"), 1.0);
          brush_point.insert(QStringLiteral("tiltX"), 0.0);
          brush_point.insert(QStringLiteral("tiltY"), 0.0);
          brush_point.insert(QStringLiteral("erase"), false);
          brush_points.append(brush_point);
        }
      }
      brush_mask.insert(QStringLiteral("brushPoints"), brush_points);

      auto brush_request = seed_request;
      brush_request.full_invalidation = false;
      const auto brush_dirty_edge =
          brush_overbroad_dirty ? 0.50
                                : (brush_tile_span == 1
                                       ? 0.10
                                       : 0.25 * static_cast<double>(brush_tile_span));
      brush_request.dirty_region = QRectF(0.0, 0.0, brush_dirty_edge, brush_dirty_edge);
      brush_request.dirty_domain = QStringLiteral("mask_rasterization");
      brush_request.dirty_stages = QStringList{
          QStringLiteral("mask_rasterization"),
          QStringLiteral("local_adjustments"),
          QStringLiteral("display_transform"),
          QStringLiteral("publish_texture"),
      };
      brush_request.tile_padding_px = 64;
      brush_request.allow_texture_patch_only = brush_texture_patch_only;
      brush_request.state.mask_stack = QVariantList{brush_mask};
      brush_request.state.active_mask_index = 0;

      if (brush_sequence) {
        auto first_stroke_request = brush_request;
        first_stroke_request.allow_texture_patch_only = false;
        const auto first_stroke_result =
            photo_editor::desktop::EditorPreviewControllerTestAccess::render_preview_request_sync(
                first_stroke_request);
        if (!first_stroke_result.success ||
            first_stroke_result.backend != QStringLiteral("adjusted_tile_cache") ||
            first_stroke_result.executed_tile_count != 1 ||
            first_stroke_result.executed_tile_pixels != 512 * 512 ||
            first_stroke_result.preview_image.isNull() ||
            first_stroke_result.rendered_cache_key.isEmpty()) {
          return fail(QStringLiteral(
              "Brush sequence warmup render failed for %1: success=%2 backend=%3 output_tiles=%4 output_pixels=%5")
                          .arg(asset_id)
                          .arg(first_stroke_result.success)
                          .arg(first_stroke_result.backend)
                          .arg(first_stroke_result.executed_tile_count)
                          .arg(first_stroke_result.executed_tile_pixels));
        }

        QVariantMap second_brush_point;
        second_brush_point.insert(QStringLiteral("x"), 0.32);
        second_brush_point.insert(QStringLiteral("y"), 0.08);
        second_brush_point.insert(QStringLiteral("size"), 0.04);
        second_brush_point.insert(QStringLiteral("flow"), 1.0);
        second_brush_point.insert(QStringLiteral("pressure"), 1.0);
        second_brush_point.insert(QStringLiteral("tiltX"), 0.0);
        second_brush_point.insert(QStringLiteral("tiltY"), 0.0);
        second_brush_point.insert(QStringLiteral("erase"), false);
        brush_points.append(second_brush_point);
        brush_mask.insert(QStringLiteral("brushPoints"), brush_points);
        brush_request.state.mask_stack = QVariantList{brush_mask};
        brush_request.dirty_region = QRectF(0.25, 0.0, 0.10, 0.10);
        brush_request.texture_patch_base_rendered_cache_key =
            first_stroke_result.rendered_cache_key;
        if (brush_patch_chain) {
          const auto second_patch_result =
              photo_editor::desktop::EditorPreviewControllerTestAccess::render_preview_request_sync(
                  brush_request);
          if (!second_patch_result.success ||
              second_patch_result.backend != QStringLiteral("adjusted_tile_cache") ||
              second_patch_result.executed_tile_count != 1 ||
              second_patch_result.executed_tile_pixels != 512 * 512 ||
              second_patch_result.texture_upload_pixels != 512 * 512 ||
              second_patch_result.texture_patch_base_copy_pixels != 0 ||
              second_patch_result.texture_upload_image.isNull() ||
              !second_patch_result.preview_image.isNull() ||
              second_patch_result.rendered_cache_key.isEmpty()) {
            return fail(QStringLiteral(
                "Brush patch-chain second render failed for %1: success=%2 backend=%3 output_tiles=%4 output_pixels=%5")
                            .arg(asset_id)
                            .arg(second_patch_result.success)
                            .arg(second_patch_result.backend)
                            .arg(second_patch_result.executed_tile_count)
                            .arg(second_patch_result.executed_tile_pixels));
          }

          QVariantMap third_brush_point;
          third_brush_point.insert(QStringLiteral("x"), 0.56);
          third_brush_point.insert(QStringLiteral("y"), 0.08);
          third_brush_point.insert(QStringLiteral("size"), 0.04);
          third_brush_point.insert(QStringLiteral("flow"), 1.0);
          third_brush_point.insert(QStringLiteral("pressure"), 1.0);
          third_brush_point.insert(QStringLiteral("tiltX"), 0.0);
          third_brush_point.insert(QStringLiteral("tiltY"), 0.0);
          third_brush_point.insert(QStringLiteral("erase"), false);
          brush_points.append(third_brush_point);
          brush_mask.insert(QStringLiteral("brushPoints"), brush_points);
          brush_request.state.mask_stack = QVariantList{brush_mask};
          brush_request.dirty_region = QRectF(0.5, 0.0, 0.10, 0.10);
          brush_request.texture_patch_base_rendered_cache_key =
              second_patch_result.rendered_cache_key;
        }
      }

      constexpr int kBrushTileBenchmarkMaxChannelDelta = 4;
      const auto expected_dirty_tile_count =
          brush_sequence ? 1 : brush_tile_span * brush_tile_span;
      const auto expected_dirty_pixels = expected_dirty_tile_count * 512 * 512;
      std::vector<double> brush_samples;
      std::vector<double> reference_samples;
      std::vector<double> adjusted_stage_samples;
      std::vector<double> output_stage_samples;
      brush_samples.reserve(static_cast<std::size_t>(iterations));
      reference_samples.reserve(static_cast<std::size_t>(iterations));
      adjusted_stage_samples.reserve(static_cast<std::size_t>(iterations));
      output_stage_samples.reserve(static_cast<std::size_t>(iterations));
      auto max_parity_delta = 0;
      auto adjusted_stage_tiles = 0;
      auto adjusted_stage_pixels = qint64{0};
      auto output_tiles = 0;
      auto output_pixels = qint64{0};
      auto texture_upload_pixels = qint64{0};
      auto base_rendered_image_hit = false;
      auto texture_patch_base_state_hit = false;
      auto max_brush_decode_ms = qint64{0};
      for (int iteration = 0; iteration < iterations; ++iteration) {
        QElapsedTimer brush_timer;
        brush_timer.start();
        const auto brush_result =
            photo_editor::desktop::EditorPreviewControllerTestAccess::render_preview_request_sync(
                brush_request);
        const auto brush_elapsed_ms = brush_timer.elapsed();
        if (!brush_result.success ||
            brush_result.backend != QStringLiteral("adjusted_tile_cache") ||
            brush_result.adjusted_stage_tile_count != expected_dirty_tile_count ||
            (brush_texture_patch_only
                 ? (brush_result.adjusted_stage_pixels <= 0 ||
                    brush_result.adjusted_stage_pixels > expected_dirty_pixels)
                 : brush_result.adjusted_stage_pixels != expected_dirty_pixels) ||
            brush_result.executed_tile_count != expected_dirty_tile_count ||
            brush_result.executed_tile_pixels != expected_dirty_pixels ||
            (brush_texture_patch_only
                 ? (brush_result.texture_upload_pixels <= 0 ||
                    brush_result.texture_upload_pixels > expected_dirty_pixels)
                 : brush_result.texture_upload_pixels != expected_dirty_pixels) ||
            brush_result.decode_elapsed_ms != 0 ||
            (brush_texture_patch_only && brush_result.texture_patch_base_copy_pixels != 0) ||
            !brush_result.partial_texture_upload_candidate ||
            !(brush_result.base_rendered_image_hit || brush_result.texture_patch_base_state_hit) ||
            (brush_texture_patch_only
                 ? (brush_result.texture_upload_image.isNull() ||
                    !brush_result.preview_image.isNull() ||
                    brush_result.texture_upload_rect.size() != brush_result.texture_upload_image.size())
                 : brush_result.preview_image.isNull())) {
          return fail(QStringLiteral(
              "Brush tile cache optimized render failed for %1: success=%2 backend=%3 adjusted_tiles=%4 adjusted_pixels=%5 output_tiles=%6 output_pixels=%7 base_rendered=%8 patch_image=%9 preview_image=%10")
                          .arg(asset_id)
                          .arg(brush_result.success)
                          .arg(brush_result.backend)
                          .arg(brush_result.adjusted_stage_tile_count)
                          .arg(brush_result.adjusted_stage_pixels)
                          .arg(brush_result.executed_tile_count)
                          .arg(brush_result.executed_tile_pixels)
                          .arg(brush_result.base_rendered_image_hit)
                          .arg(!brush_result.texture_upload_image.isNull())
                          .arg(!brush_result.preview_image.isNull()));
        }

        auto reference_request = brush_request;
        reference_request.full_invalidation = true;
        reference_request.dirty_region = QRectF();
        reference_request.compare_mode = true;
        QElapsedTimer reference_timer;
        reference_timer.start();
        const auto reference_result =
            photo_editor::desktop::EditorPreviewControllerTestAccess::render_preview_request_sync(
                reference_request);
        const auto reference_elapsed_ms = reference_timer.elapsed();
        const auto brush_parity_image =
            brush_texture_patch_only
                ? brush_result.texture_upload_image
                : brush_result.preview_image;
        const auto reference_parity_image =
            brush_texture_patch_only
                ? reference_result.preview_image.copy(brush_result.texture_upload_rect)
                : reference_result.preview_image;
        const auto parity_delta =
            max_channel_delta(brush_parity_image, reference_parity_image);
        if (!reference_result.success || reference_result.preview_image.isNull() ||
            parity_delta > kBrushTileBenchmarkMaxChannelDelta) {
          return fail(QStringLiteral(
              "Brush tile cache parity failed for %1: reference_success=%2 delta=%3")
                          .arg(asset_id)
                          .arg(reference_result.success)
                          .arg(parity_delta));
        }

        brush_samples.push_back(static_cast<double>(brush_elapsed_ms));
        reference_samples.push_back(static_cast<double>(reference_elapsed_ms));
        adjusted_stage_samples.push_back(static_cast<double>(brush_result.adjusted_stage_elapsed_ms));
        output_stage_samples.push_back(static_cast<double>(brush_result.output_stage_elapsed_ms));
        max_parity_delta = std::max(max_parity_delta, parity_delta);
        adjusted_stage_tiles = brush_result.adjusted_stage_tile_count;
        adjusted_stage_pixels = brush_result.adjusted_stage_pixels;
        output_tiles = brush_result.executed_tile_count;
        output_pixels = brush_result.executed_tile_pixels;
        texture_upload_pixels = brush_result.texture_upload_pixels;
        base_rendered_image_hit = brush_result.base_rendered_image_hit;
        texture_patch_base_state_hit = brush_result.texture_patch_base_state_hit;
        max_brush_decode_ms = std::max(max_brush_decode_ms, brush_result.decode_elapsed_ms);
        photo_editor::desktop::observability::logInfo(
            photoEditorPerfLog(),
            QStringLiteral("benchmark.preview_brush_tile_cache.sample_completed"),
            {
                {QStringLiteral("asset_id"), asset_id},
                {QStringLiteral("file_path"), file_path},
                {QStringLiteral("iteration"), iteration + 1},
                {QStringLiteral("iterations"), iterations},
                {QStringLiteral("brush_elapsed_ms"), brush_elapsed_ms},
                {QStringLiteral("brush_decode_ms"), brush_result.decode_elapsed_ms},
                {QStringLiteral("adjusted_stage_ms"), brush_result.adjusted_stage_elapsed_ms},
                {QStringLiteral("output_stage_ms"), brush_result.output_stage_elapsed_ms},
                {QStringLiteral("reference_elapsed_ms"), reference_elapsed_ms},
                {QStringLiteral("parity_max_channel_delta"), parity_delta},
                {QStringLiteral("brush_tile_span"), brush_tile_span},
                {QStringLiteral("brush_sequence"), brush_sequence},
                {QStringLiteral("brush_patch_chain"), brush_patch_chain},
                {QStringLiteral("brush_overbroad_dirty"), brush_overbroad_dirty},
                {QStringLiteral("brush_texture_patch_only"), brush_texture_patch_only},
                {QStringLiteral("base_rendered_image_hit"), brush_result.base_rendered_image_hit},
                {QStringLiteral("texture_patch_base_state_hit"),
                 brush_result.texture_patch_base_state_hit},
                {QStringLiteral("texture_upload_pixels"), brush_result.texture_upload_pixels},
                {QStringLiteral("texture_patch_base_copy_pixels"),
                 brush_result.texture_patch_base_copy_pixels},
                {QStringLiteral("texture_upload_image"), !brush_result.texture_upload_image.isNull()},
                {QStringLiteral("partial_texture_upload_candidate"),
                 brush_result.partial_texture_upload_candidate},
            });
      }

      const auto brush_summary = summarize_samples(brush_samples);
      const auto reference_summary = summarize_samples(reference_samples);
      const auto adjusted_stage_summary = summarize_samples(adjusted_stage_samples);
      const auto output_stage_summary = summarize_samples(output_stage_samples);
      const auto speedup_ratio =
          brush_summary.p50_ms > 0.0
              ? reference_summary.p50_ms / brush_summary.p50_ms
              : reference_summary.p50_ms;
      photo_editor::desktop::observability::logInfo(
          photoEditorPerfLog(),
          QStringLiteral("benchmark.preview_brush_tile_cache.completed"),
          {
              {QStringLiteral("asset_id"), asset_id},
              {QStringLiteral("file_path"), file_path},
              {QStringLiteral("seed_elapsed_ms"), seed_elapsed_ms},
              {QStringLiteral("iterations"), iterations},
              {QStringLiteral("brush_elapsed_ms"), brush_summary.p50_ms},
              {QStringLiteral("brush_elapsed_total_ms"), brush_summary.elapsed_ms},
              {QStringLiteral("brush_avg_ms"), brush_summary.avg_ms},
              {QStringLiteral("brush_p50_ms"), brush_summary.p50_ms},
              {QStringLiteral("brush_p90_ms"), brush_summary.p90_ms},
              {QStringLiteral("brush_p95_ms"), brush_summary.p95_ms},
              {QStringLiteral("brush_p99_ms"), brush_summary.p99_ms},
              {QStringLiteral("brush_max_ms"), brush_summary.max_ms},
              {QStringLiteral("brush_max_decode_ms"), max_brush_decode_ms},
              {QStringLiteral("adjusted_stage_p50_ms"), adjusted_stage_summary.p50_ms},
              {QStringLiteral("adjusted_stage_p95_ms"), adjusted_stage_summary.p95_ms},
              {QStringLiteral("output_stage_p50_ms"), output_stage_summary.p50_ms},
              {QStringLiteral("output_stage_p95_ms"), output_stage_summary.p95_ms},
              {QStringLiteral("brush_stddev_ms"), brush_summary.stddev_ms},
              {QStringLiteral("brush_mad_ms"), brush_summary.mad_ms},
              {QStringLiteral("reference_elapsed_ms"), reference_summary.p50_ms},
              {QStringLiteral("reference_elapsed_total_ms"), reference_summary.elapsed_ms},
              {QStringLiteral("reference_avg_ms"), reference_summary.avg_ms},
              {QStringLiteral("reference_p50_ms"), reference_summary.p50_ms},
              {QStringLiteral("reference_p90_ms"), reference_summary.p90_ms},
              {QStringLiteral("reference_p95_ms"), reference_summary.p95_ms},
              {QStringLiteral("reference_p99_ms"), reference_summary.p99_ms},
              {QStringLiteral("reference_max_ms"), reference_summary.max_ms},
              {QStringLiteral("reference_stddev_ms"), reference_summary.stddev_ms},
              {QStringLiteral("reference_mad_ms"), reference_summary.mad_ms},
              {QStringLiteral("speedup_ratio"), speedup_ratio},
              {QStringLiteral("parity_max_channel_delta"), max_parity_delta},
              {QStringLiteral("seed_output_tiles"), seed_result.executed_tile_count},
              {QStringLiteral("seed_output_pixels"), seed_result.executed_tile_pixels},
              {QStringLiteral("base_exposure"), seed_request.state.exposure},
              {QStringLiteral("brush_tile_span"), brush_tile_span},
              {QStringLiteral("brush_sequence"), brush_sequence},
              {QStringLiteral("brush_patch_chain"), brush_patch_chain},
              {QStringLiteral("brush_overbroad_dirty"), brush_overbroad_dirty},
              {QStringLiteral("brush_texture_patch_only"), brush_texture_patch_only},
              {QStringLiteral("base_rendered_image_hit"), base_rendered_image_hit},
              {QStringLiteral("texture_patch_base_state_hit"), texture_patch_base_state_hit},
              {QStringLiteral("adjusted_stage_tiles"), adjusted_stage_tiles},
              {QStringLiteral("adjusted_stage_pixels"), adjusted_stage_pixels},
              {QStringLiteral("output_tiles"), output_tiles},
              {QStringLiteral("output_pixels"), output_pixels},
              {QStringLiteral("texture_upload_pixels"), texture_upload_pixels},
              {QStringLiteral("texture_patch_base_copy_pixels"), 0},
              {QStringLiteral("texture_upload_image"), brush_texture_patch_only},
              {QStringLiteral("partial_texture_upload_candidate"), true},
              {QStringLiteral("reference_output_pixels"), 2048 * 2048},
          });
    }
    ++rendered_count;
    const auto image_size = image_sink.lastImageSize();
    photo_editor::desktop::observability::logInfo(
        photoEditorPerfLog(),
        QStringLiteral("benchmark.preview_asset.completed"),
        {
            {QStringLiteral("asset_id"), asset_id},
            {QStringLiteral("file_path"), file_path},
            {QStringLiteral("elapsed_ms"), asset_timer.elapsed()},
            {QStringLiteral("image_width"), image_size.width()},
            {QStringLiteral("image_height"), image_size.height()},
        });
  }

  photo_editor::desktop::observability::logInfo(
      photoEditorPerfLog(),
      QStringLiteral("benchmark.preview_corpus.completed"),
      {
          {QStringLiteral("corpus"), corpus_path},
          {QStringLiteral("asset_count"), rendered_count},
          {QStringLiteral("elapsed_ms"), total_timer.elapsed()},
      });
  if (rapid_supersede && rapid_supersede_cancelled_count <= 0) {
    return fail(QStringLiteral("Rapid supersede workload did not cancel stale render work."));
  }
  std::cout << "rendered_assets=" << rendered_count << '\n';
  return rendered_count > 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
