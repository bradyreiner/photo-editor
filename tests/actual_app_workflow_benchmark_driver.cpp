#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <numeric>
#include <thread>
#include <vector>

#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDate>
#include <QDateTime>
#include <QEventLoop>
#include <QDataStream>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPointer>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSet>
#include <QSettings>
#include <QSGRendererInterface>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>
#include <QTest>
#include <QThreadPool>
#include <QTimer>
#include <QTime>
#include <QUrl>
#include <QVariant>
#include <QVector>
#include <qqml.h>

#include "app_shell_controller.h"
#include "app_bootstrap.h"
#include "brush_live_preview_item.h"
#include "brush_input_schema.h"
#include "brush_matte_rebuild.h"
#include "local_mask_compiler.h"
#include "photo_text_document_style.h"
#include "carousel_document_controller.h"
#include "catalog_asset_model.h"
#include "recent_user_input_filter.h"
#include "visual_search_controller.h"
#include "catalog_similarity_search.h"
#include "search_runtime_bundle.h"
#include "search_edit_workflow.h"
#include "agent_edit_controller.h"
#include "edit_adjustment_patch.h"
#include "edit_recipe_mask_schema.h"
#include "editor_preview_controller.h"
#include "photo_render_utils.h"
#include "photo_editor/pipeline/metal/metal_pipeline_backend.h"
#include "editor_preview_image_provider.h"
#include "counting_preview_image_provider.h"
#include "native_preview_readback.h"
#include "editor_preview_texture_item.h"
#include "export_job_controller.h"
#include "job_queue_model.h"
#include "job_runner.h"
#include "macos_updater.h"
#include "observability.h"
#include "preview_backend_controller.h"
#include "qml_performance_monitor.h"
#include "tablet_input_area.h"
#include "timeline_audio_output.h"
#include "timeline_document_controller.h"
#include "timeline_playback_controller.h"
#if defined(__APPLE__) && defined(PHOTO_EDITOR_ENABLE_METAL_PREVIEW_BACKEND)
#include "gaussian_scene_controller.h"
#include "gaussian_scene_viewport.h"
#include "workspace_switch_proof.h"
#include "gaussian_viewer_film_proof.h"
#include "gaussian_capture_setup_proof.h"
#endif

namespace photo_editor::desktop {

struct JobQueueModelTestAccess {
  static void publish(JobQueueModel& model, const QVector<PersistentJobRecord>& records) {
    model.publishJobs(records);
  }
};

struct EditorPreviewControllerTestAccess {
  static quint64 history_preview_token(const EditorPreviewController& controller) {
    return controller.history_read_token_->load();
  }
  static bool history_preview_active(const EditorPreviewController& controller) {
    return controller.history_preview_state_.has_value();
  }
  static QVariantMap brush_outline_worker_state(EditorPreviewController& controller) {
    auto& watcher = controller.mask_worker_coordinator_.activeMaskOutlineWatcher();
    QVariantMap state{{"running", watcher.isRunning()}, {"finished", watcher.isFinished()}};
    if (watcher.isFinished()) {
      const auto result = watcher.result();
      state.insert("success", result.success);
      state.insert("source", result.source);
      state.insert("path_count", result.paths.size());
      state.insert("revision", result.request.state_revision);
      state.insert("current_revision", controller.mask_session_.workRevision());
    }
    return state;
  }
  static QByteArray live_brush_alpha(const EditorPreviewController& controller) {
    return controller.active_brush_live_preview_frame_.raster.alpha;
  }
  static QSize live_brush_size(const EditorPreviewController& controller) {
    return controller.active_brush_live_preview_frame_.raster.image.size();
  }
  static auto live_brush_points(const EditorPreviewController& controller) {
    return controller.active_brush_live_points_;
  }
  static QString recipe_json(const EditorPreviewController& controller) {
    return serialize_edit_recipe_json(controller.currentState());
  }
  static QVariantMap layer_stack_timings(const EditorPreviewController& controller) {
    return controller.last_photo_layer_stack_performance_;
  }
  static void install_checkpoint_raster(EditorPreviewController& controller, int edge) {
    auto state = controller.currentState();
    QByteArray alpha(edge * edge, '\0');
    quint32 noise = 0x12345678U;
    for (auto& byte : alpha) {
      noise ^= noise << 13; noise ^= noise >> 17; noise ^= noise << 5;
      byte = static_cast<char>(noise & 255U);
    }
    auto mask = default_mask_definition(QStringLiteral("brush"));
    mask.insert(QStringLiteral("id"), QStringLiteral("checkpoint-tool-mask"));
    mask.insert(QStringLiteral("segmentationWidth"), edge);
    mask.insert(QStringLiteral("segmentationHeight"), edge);
    mask.insert(QStringLiteral("segmentationData"), alpha);
    state.mask_stack = {mask};
    state.active_mask_index = -1;
    controller.applyState(state);
  }
  static qint64 stop_checkpoint_helper_and_size(EditorPreviewController& controller) {
    controller.persist_timer_.stop();
    return QFileInfo(controller.recipe_save_journal_.path()).size();
  }

  static auto inspector_surface(const EditorPreviewController& controller) { return controller.pixel_inspector_native_surface_; }
  static auto* native_texture(EditorPreviewController& controller) { return controller.display_preview_texture_.get(); }
  static bool full_native_coverage(const EditorPreviewController& controller) {
    return controller.display_preview_native_full_resolution_coverage_size_ ==
               controller.displayPreviewNativeSize() &&
        QRegion(QRect(QPoint(), controller.displayPreviewNativeSize())).subtracted(
            controller.display_preview_native_full_resolution_coverage_).isEmpty();
  }
  static void publish_visible_patch_without_base(
      EditorPreviewController& controller, const QImage& reference, const QRect& rect) {
    controller.display_preview_texture_->clear();
    controller.display_preview_native_rendered_cache_key_.clear();
    controller.display_preview_native_full_resolution_coverage_ = {};
    controller.display_preview_native_full_resolution_coverage_size_ = {};
    controller.display_preview_native_full_resolution_coverage_state_signature_.clear();
    RenderResult result;
    result.success = true;
    result.full_resolution = true;
    result.generation = controller.preview_session_.generation();
    result.file_path = controller.file_path_;
    result.render_reason = QStringLiteral("viewport_change");
    result.render_mode = QStringLiteral("max_preview_visible_tile");
    result.tile_visible_region_active = true;
    result.tile_visible_region = controller.preview_viewport_rect_;
    result.output_width = reference.width();
    result.output_height = reference.height();
    result.source_width = controller.sourceImageWidth();
    result.source_height = controller.sourceImageHeight();
    result.texture_upload_rect = rect;
    result.texture_upload_image = reference.copy(rect);
    result.partial_texture_upload_candidate = true;
    controller.handleRenderResult(true, result);
  }
  static QImage inspector_cpu_image(const EditorPreviewController& controller) { return controller.pixel_inspector_preview_image_; }
  static bool inspector_has_cpu_image(const EditorPreviewController& controller) { return !controller.pixel_inspector_preview_image_.isNull(); }
  static bool inspector_idle(const EditorPreviewController& controller) {
    return !controller.native_pixel_sample_active_ && !controller.native_pixel_sample_pending_;
  }
  static auto* inspector_watcher(EditorPreviewController& controller) { return &controller.native_pixel_sample_watcher_; }
  static auto preview_request(const EditorPreviewController& controller) { return controller.currentRenderRequest(false); }
  static auto full_preview_request(const EditorPreviewController& controller) { return controller.currentRenderRequest(true); }
  static bool preview_idle(const EditorPreviewController& controller) {
    return !controller.render_scheduler_.interactiveActive() &&
           !controller.render_scheduler_.pendingDispatchActive() &&
           !controller.edit_transaction_interactive_preview_timer_.isActive() &&
           !controller.interactive_render_watcher_.isRunning() &&
           !controller.interactive_request_pending_ &&
           !controller.photo_layer_adjustment_render_timer_.isActive();
  }

  static bool all_idle(const EditorPreviewController& controller) {
    return preview_idle(controller) && !controller.render_scheduler_.fullActive() &&
           !controller.full_render_watcher_.isRunning() && !controller.full_request_pending_;
  }
  static bool native_preview_reaches_viewport(const EditorPreviewController& controller) {
    const auto source = controller.sourceImageSize();
    const auto native = controller.displayPreviewNativeSize();
    const auto required_edge = std::min(controller.previewTargetEdge(),
                                       std::max(source.width(), source.height()));
    return source.isValid() && native.isValid() &&
           std::max(native.width(), native.height()) >= required_edge;
  }
  static int pending_brush_commit_count(const EditorPreviewController& controller) {
    return static_cast<int>(controller.pending_brush_commits_.size()) +
           (controller.pending_brush_commit_scheduled_ ? 1 : 0) +
           (controller.mask_worker_coordinator_.brushRasterCommitWatcher().isRunning() ? 1 : 0) +
           (controller.brush_raster_commit_active_ ? 1 : 0);
  }

  static QVariantMap last_mask_work_telemetry(const EditorPreviewController& controller) {
    return controller.last_mask_work_telemetry_;
  }
};

}  // namespace photo_editor::desktop

namespace {

struct SampleSummary final {
  qint64 elapsed_ms = 0;
  double avg_ms = 0.0;
  double p50_ms = 0.0;
  double p90_ms = 0.0;
  double p95_ms = 0.0;
  double p99_ms = 0.0;
  double max_ms = 0.0;
};

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

bool write_workflow_fixture(const QString& path) {
  QImage image(1800, 1200, QImage::Format_ARGB32);
  for (int y = 0; y < image.height(); ++y) {
    const auto yn = static_cast<double>(y) / static_cast<double>(std::max(1, image.height() - 1));
    for (int x = 0; x < image.width(); ++x) {
      const auto xn =
          static_cast<double>(x) / static_cast<double>(std::max(1, image.width() - 1));
      const auto wave = std::sin((xn * 7.0 + yn * 4.0) * 3.14159265358979323846);
      image.setPixelColor(
          x,
          y,
          QColor::fromRgb(
              std::clamp(static_cast<int>(42 + xn * 174 + wave * 20), 0, 255),
              std::clamp(static_cast<int>(68 + yn * 142 - wave * 14), 0, 255),
              std::clamp(static_cast<int>(96 + (1.0 - xn) * 84 + yn * 24), 0, 255)));
    }
  }
  return image.save(path);
}

photo_editor::desktop::CatalogAssetItem make_asset(
    const QString& asset_id,
    const QString& file_path,
    const QString& thumbnail_source = QString()) {
  photo_editor::desktop::CatalogAssetItem item;
  item.asset_id = asset_id;
  item.camera_label = QStringLiteral("Benchmark Camera");
  item.file_path = file_path;
  item.thumbnail_source =
      thumbnail_source.isEmpty() ? QUrl::fromLocalFile(file_path).toString() : thumbnail_source;
  item.capture_date = QStringLiteral("Jul 4, 2026");
  item.capture_date_value = QDate(2026, 7, 4);
  item.imported_at = QStringLiteral("Jul 4, 2026  9:00 AM");
  item.imported_at_value = QDateTime(QDate(2026, 7, 4), QTime(9, 0));
  item.media_type = QStringLiteral("photo");
  item.online = true;
  return item;
}

void process_layout_events(int rounds = 3, const char* context = "") {
  for (int index = 0; index < rounds; ++index) {
    QElapsedTimer process_events_timer;
    process_events_timer.start();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    const auto process_events_elapsed_ms = process_events_timer.elapsed();
    if (process_events_elapsed_ms >= 50) {
      std::cerr << "UI_TIMING utc="
                << QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString()
                << " call=process_layout_events context=" << context
                << " elapsed_ms=" << process_events_elapsed_ms
                << std::endl;
    }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }
}

bool wait_for_condition(
    const std::function<bool()>& condition,
    int timeout_ms = 3000,
    const char* context = "") {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    if (condition()) {
      return true;
    }
    QElapsedTimer process_events_timer;
    process_events_timer.start();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    const auto process_events_elapsed_ms = process_events_timer.elapsed();
    if (process_events_elapsed_ms >= 50) {
      std::cerr << "UI_TIMING utc="
                << QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString()
                << " call=wait_for_condition context=" << context
                << " elapsed_ms=" << process_events_elapsed_ms
                << std::endl;
    }
    if (condition()) {
      return true;
    }
    QThread::msleep(25);
  }
  if (condition()) {
    return true;
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  return condition();
}

bool save_timeline_and_wait(
    photo_editor::desktop::TimelineDocumentController* timeline_document,
    const char* context,
    QString* error = nullptr) {
  if (timeline_document == nullptr) {
    if (error != nullptr)
      *error = QStringLiteral("Timeline document controller is unavailable.");
    return false;
  }
  if (!timeline_document->saveTimelineAsync()) {
    if (error != nullptr)
      *error = timeline_document->status();
    return false;
  }
  if (!wait_for_condition(
          [timeline_document]() { return !timeline_document->timelineSaveInProgress(); },
          120000,
          context)) {
    if (error != nullptr)
      *error = QStringLiteral("Timed out while saving the timeline.");
    return false;
  }
  const auto save_status = timeline_document->status();
  if (timeline_document->savedDocumentPath().isEmpty() ||
      !save_status.startsWith(QStringLiteral("Saved timeline "))) {
    if (error != nullptr)
      *error = save_status.isEmpty()
                   ? QStringLiteral("Timeline save did not complete successfully.")
                   : save_status;
    return false;
  }
  return true;
}

class RouteInputDispatchProbe final : public QObject {
 public:
  explicit RouteInputDispatchProbe(QObject* target)
      : QObject(target), target_(target) {
    if (target_ != nullptr) {
      target_->installEventFilter(this);
    }
  }

  ~RouteInputDispatchProbe() override {
    joinPoster();
  }

  void postNoOpKeyAfter(
      const std::chrono::steady_clock::time_point route_started_at,
      const int delay_ms = 15) {
    joinPoster();
    post_time_ns_.store(0, std::memory_order_relaxed);
    dispatch_time_ns_.store(0, std::memory_order_relaxed);
    completion_time_ns_.store(0, std::memory_order_relaxed);
    route_offset_ms_.store(0, std::memory_order_relaxed);
    auto* const target = target_.data();
    const auto completion_event_type = completionEventType();
    poster_ = std::thread([this, target, route_started_at, delay_ms, completion_event_type]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
      const auto posted_at = std::chrono::steady_clock::now();
      route_offset_ms_.store(
          std::chrono::duration_cast<std::chrono::milliseconds>(
              posted_at - route_started_at)
              .count(),
          std::memory_order_release);
      post_time_ns_.store(timestampNs(posted_at), std::memory_order_release);
      if (target != nullptr) {
        QCoreApplication::postEvent(
            target,
            new QKeyEvent(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier));
        QCoreApplication::postEvent(target, new QEvent(completion_event_type));
      }
    });
  }

  void joinPoster() {
    if (poster_.joinable()) {
      poster_.join();
    }
  }

  [[nodiscard]] bool delivered() const {
    return completion_time_ns_.load(std::memory_order_acquire) > 0;
  }

  [[nodiscard]] bool recentInputFilterObserved() const {
    return recent_input_filter_observed_.load(std::memory_order_acquire);
  }

  [[nodiscard]] double dispatchDelayMs() const {
    const auto posted = post_time_ns_.load(std::memory_order_acquire);
    const auto dispatched = dispatch_time_ns_.load(std::memory_order_acquire);
    if (posted <= 0 || dispatched < posted) {
      return -1.0;
    }
    return static_cast<double>(dispatched - posted) / 1000000.0;
  }

  [[nodiscard]] qint64 routeOffsetMs() const {
    return route_offset_ms_.load(std::memory_order_acquire);
  }

  bool eventFilter(QObject* watched, QEvent* event) override {
    if (watched == target_ && event != nullptr &&
        post_time_ns_.load(std::memory_order_acquire) > 0) {
      if (event->type() == QEvent::KeyPress) {
        const auto* key_event = static_cast<QKeyEvent*>(event);
        if (key_event->key() == Qt::Key_unknown) {
          dispatch_time_ns_.store(timestampNs(), std::memory_order_release);
        }
      } else if (event->type() == completionEventType()) {
        recent_input_filter_observed_.store(
            target_->property("recentUserInput").toBool(), std::memory_order_release);
        completion_time_ns_.store(timestampNs(), std::memory_order_release);
      }
    }
    return QObject::eventFilter(watched, event);
  }

 private:
  static QEvent::Type completionEventType() {
    static const auto event_type =
        static_cast<QEvent::Type>(QEvent::registerEventType());
    return event_type;
  }

  static qint64 timestampNs(
      const std::chrono::steady_clock::time_point timestamp =
          std::chrono::steady_clock::now()) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               timestamp.time_since_epoch())
        .count();
  }

  QPointer<QObject> target_;
  std::thread poster_;
  std::atomic<qint64> post_time_ns_{0};
  std::atomic<qint64> dispatch_time_ns_{0};
  std::atomic<qint64> completion_time_ns_{0};
  std::atomic<qint64> route_offset_ms_{0};
  std::atomic<bool> recent_input_filter_observed_{false};
};

QObject* find_named_object_recursive(QObject* root, const QString& object_name, QSet<const QObject*>& visited) {
  if (root == nullptr || visited.contains(root)) {
    return nullptr;
  }
  visited.insert(root);
  if (root->objectName() == object_name) {
    return root;
  }
  if (auto* item = qobject_cast<QQuickItem*>(root); item != nullptr) {
    for (auto* child_item : item->childItems()) {
      if (auto* found = find_named_object_recursive(child_item, object_name, visited);
          found != nullptr) {
        return found;
      }
    }
  }
  if (auto* window = qobject_cast<QQuickWindow*>(root); window != nullptr) {
    if (auto* found = find_named_object_recursive(window->contentItem(), object_name, visited);
        found != nullptr) {
      return found;
    }
  }
  for (auto* child : root->children()) {
    if (auto* found = find_named_object_recursive(child, object_name, visited);
        found != nullptr) {
      return found;
    }
  }
  return nullptr;
}

QObject* find_named_object(QObject* root, const QString& object_name) {
  if (root == nullptr) {
    return nullptr;
  }
  if (root->objectName() == object_name) {
    return root;
  }
  if (auto* found = root->findChild<QObject*>(object_name, Qt::FindChildrenRecursively);
      found != nullptr) {
    return found;
  }
  QSet<const QObject*> visited;
  return find_named_object_recursive(root, object_name, visited);
}

QQuickItem* find_named_item(QObject* root, const QString& object_name) {
  return qobject_cast<QQuickItem*>(find_named_object(root, object_name));
}

bool item_visible(const QQuickItem* item) {
  return item != nullptr && item->isVisible() && item->width() > 0.0 && item->height() > 0.0;
}

QQuickItem* find_visible_named_item(QObject* root, const QString& object_name) {
  if (root == nullptr) {
    return nullptr;
  }
  const auto find_recursive = [&](const auto& self,
                                  QObject* candidate,
                                  QSet<const QObject*>& visited) -> QQuickItem* {
    if (candidate == nullptr || visited.contains(candidate)) {
      return nullptr;
    }
    visited.insert(candidate);
    if (candidate->objectName() == object_name) {
      if (auto* item = qobject_cast<QQuickItem*>(candidate); item_visible(item)) {
        return item;
      }
    }
    if (auto* item = qobject_cast<QQuickItem*>(candidate); item != nullptr) {
      for (auto* child_item : item->childItems()) {
        if (auto* found = self(self, child_item, visited); found != nullptr) {
          return found;
        }
      }
    }
    if (auto* window = qobject_cast<QQuickWindow*>(candidate); window != nullptr) {
      if (auto* found = self(self, window->contentItem(), visited); found != nullptr) {
        return found;
      }
    }
    for (auto* child : candidate->children()) {
      if (auto* found = self(self, child, visited); found != nullptr) {
        return found;
      }
    }
    return nullptr;
  };
  QSet<const QObject*> visited;
  if (auto* visible_item = find_recursive(find_recursive, root, visited); visible_item != nullptr) {
    return visible_item;
  }
  const auto candidates = root->findChildren<QObject*>(object_name, Qt::FindChildrenRecursively);
  for (auto* candidate : candidates) {
    if (auto* item = qobject_cast<QQuickItem*>(candidate); item_visible(item)) {
      return item;
    }
  }
  return find_named_item(root, object_name);
}

bool named_item_visible(QObject* root, const QString& object_name) {
  return item_visible(find_visible_named_item(root, object_name));
}

QObject* active_workspace(QObject* root) {
  QVariant workspace_value;
  if (root == nullptr ||
      !QMetaObject::invokeMethod(
          root,
          "activeWorkspaceItem",
          Q_RETURN_ARG(QVariant, workspace_value))) {
    return nullptr;
  }
  return qvariant_cast<QObject*>(workspace_value);
}

void log_stage(
    const QString& stage,
    qint64 elapsed_ms,
    int iteration,
    const QVariantMap& extra = {}) {
  auto fields = extra;
  fields.insert(QStringLiteral("workflow"), QStringLiteral("actual_app_qml"));
  fields.insert(QStringLiteral("stage"), stage);
  fields.insert(QStringLiteral("iteration"), iteration);
  fields.insert(QStringLiteral("elapsed_ms"), elapsed_ms);
  photo_editor::desktop::observability::logInfo(
      photoEditorPerfLog(),
      QStringLiteral("benchmark.actual_app_workflow.stage_completed"),
      fields);
}

void log_summary(
    const QString& stage,
    const SampleSummary& summary,
    int iterations,
    const QVariantMap& extra = {}) {
  if (iterations <= 0) return;
  auto fields = extra;
  fields.insert(QStringLiteral("workflow"), QStringLiteral("actual_app_qml"));
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
      QStringLiteral("benchmark.actual_app_workflow.stage_summary"),
      fields);
}

void save_capture_if_requested(
    QQuickWindow* window,
    const QString& capture_dir,
    const QString& name,
    QJsonArray* captures) {
  QElapsedTimer capture_timer;
  capture_timer.start();
  const auto report_slow_capture = qScopeGuard([&]() {
    const auto elapsed_ms = capture_timer.elapsed();
    if (elapsed_ms >= 50) {
      std::cerr << "UI_TIMING utc="
                << QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString()
                << " call=save_capture_if_requested name=" << name.toStdString()
                << " elapsed_ms=" << elapsed_ms << std::endl;
    }
  });
  if (window == nullptr || capture_dir.isEmpty() || captures == nullptr) {
    return;
  }
  if (!QDir().mkpath(capture_dir)) {
    return;
  }
  process_layout_events(8);
  QEventLoop frame_loop;
  QTimer frame_timeout;
  frame_timeout.setSingleShot(true);
  bool frame_presented = false;
  const auto frame_connection = QObject::connect(
      window, &QQuickWindow::frameSwapped, &frame_loop, [&]() {
        frame_presented = true;
        frame_loop.quit();
      });
  const auto timeout_connection = QObject::connect(
      &frame_timeout, &QTimer::timeout, &frame_loop, &QEventLoop::quit);
  frame_timeout.start(1000);
  window->update();
  if (!frame_presented)
    frame_loop.exec();
  frame_timeout.stop();
  QObject::disconnect(frame_connection);
  QObject::disconnect(timeout_connection);
  if (!frame_presented)
    return;
  const auto image = window->grabWindow();
  if (image.isNull() || image.width() <= 0 || image.height() <= 0) {
    return;
  }
  const auto path = QDir(capture_dir).filePath(name + QStringLiteral(".png"));
  if (!image.save(path)) {
    return;
  }
  QJsonObject capture;
  capture.insert(QStringLiteral("name"), name);
  capture.insert(QStringLiteral("path"), path);
  capture.insert(QStringLiteral("width"), image.width());
  capture.insert(QStringLiteral("height"), image.height());
  captures->append(capture);
}

QRectF item_scene_rect(const QQuickItem* item) {
  if (item == nullptr) {
    return {};
  }
  return item->mapRectToScene(QRectF(0.0, 0.0, item->width(), item->height()));
}

bool image_has_rendered_content(const QImage& image) {
  if (image.isNull() || image.width() <= 0 || image.height() <= 0) {
    return false;
  }
  auto minimum_luminance = 255;
  auto maximum_luminance = 0;
  qint64 luminance_sum = 0;
  qint64 sample_count = 0;
  for (auto y = 0; y < image.height(); y += 16) {
    for (auto x = 0; x < image.width(); x += 16) {
      const auto luminance = qGray(image.pixel(x, y));
      minimum_luminance = std::min(minimum_luminance, luminance);
      maximum_luminance = std::max(maximum_luminance, luminance);
      luminance_sum += luminance;
      ++sample_count;
    }
  }
  return sample_count > 0 && luminance_sum / sample_count >= 5 &&
         maximum_luminance - minimum_luminance >= 24;
}

bool write_timeline_proof_wave_file(const QString& file_path) {
  QFile file(file_path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  constexpr quint32 sample_rate = 44100;
  constexpr quint16 channel_count = 1;
  constexpr quint16 bits_per_sample = 16;
  constexpr quint32 duration_seconds = 8;
  constexpr quint32 sample_count = sample_rate * duration_seconds;
  constexpr quint32 bytes_per_sample = bits_per_sample / 8;
  constexpr quint32 data_size = sample_count * channel_count * bytes_per_sample;
  constexpr quint32 byte_rate = sample_rate * channel_count * bytes_per_sample;
  constexpr quint16 block_align = channel_count * bytes_per_sample;

  QDataStream stream(&file);
  stream.setByteOrder(QDataStream::LittleEndian);
  stream.writeRawData("RIFF", 4);
  stream << quint32(36 + data_size);
  stream.writeRawData("WAVE", 4);
  stream.writeRawData("fmt ", 4);
  stream << quint32(16) << quint16(1) << channel_count << sample_rate << byte_rate
         << block_align << bits_per_sample;
  stream.writeRawData("data", 4);
  stream << data_size;
  constexpr auto kPi = 3.14159265358979323846;
  for (quint32 sample_index = 0; sample_index < sample_count; ++sample_index) {
    const auto seconds = static_cast<double>(sample_index) / sample_rate;
    const auto phrase = 0.20 + 0.76 * std::abs(std::sin(kPi * seconds / 1.6));
    const auto carrier = std::sin(2.0 * kPi * (180.0 + 45.0 * std::sin(seconds * 0.8)) * seconds);
    const auto transient = std::fmod(seconds, 1.0) < 0.09 ? 1.0 : 0.58;
    const auto sample = static_cast<qint16>(
        std::clamp(phrase * carrier * transient, -1.0, 1.0) * 27000.0);
    stream << sample;
  }
  return stream.status() == QDataStream::Ok;
}

bool write_timeline_proof_video_file(
    const QString& image_path,
    const QString& video_path) {
  const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  if (ffmpeg.isEmpty() || image_path.isEmpty() || video_path.isEmpty()) {
    return false;
  }
  QFile::remove(video_path);
  QProcess process;
  process.setProcessChannelMode(QProcess::MergedChannels);
  process.start(
      ffmpeg,
      {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
       QStringLiteral("-loop"), QStringLiteral("1"), QStringLiteral("-i"), image_path,
       QStringLiteral("-t"), QStringLiteral("10"), QStringLiteral("-r"), QStringLiteral("30"),
       QStringLiteral("-vf"), QStringLiteral("scale=640:-2"), QStringLiteral("-an"),
       QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-preset"),
       QStringLiteral("ultrafast"), QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
       QStringLiteral("-y"), video_path});
  if (!process.waitForStarted(3000) || !process.waitForFinished(30000)) {
    process.kill();
    return false;
  }
  return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0 &&
      QFileInfo(video_path).size() > 1024;
}

bool capture_timeline_ui_state(
    QQuickWindow* window,
    QObject* root,
    const QString& output_dir,
    const QString& state,
    int width,
    int height,
    const QStringList& required_objects,
    QJsonArray* manifest,
    const QJsonObject& metadata = {}) {
  if (window == nullptr || root == nullptr || manifest == nullptr) {
    return false;
  }
  if (std::abs(root->property("width").toDouble() - width) > 0.5) {
    root->setProperty("width", width);
  }
  if (std::abs(root->property("height").toDouble() - height) > 0.5) {
    root->setProperty("height", height);
  }
  process_layout_events(12);
  window->requestUpdate();
  process_layout_events(8);

  QStringList missing_objects;
  for (const auto& object_name : required_objects) {
    if (!named_item_visible(root, object_name)) {
      missing_objects.push_back(object_name);
    }
  }
  if (!missing_objects.isEmpty()) {
    std::cerr << "Timeline proof " << state.toStdString() << " missing/hidden: "
              << missing_objects.join(QStringLiteral(", ")).toStdString() << '\n';
    return false;
  }

  const auto frame = window->grabWindow();
  if (!image_has_rendered_content(frame)) {
    std::cerr << "Timeline proof " << state.toStdString()
              << " did not produce a visibly rendered frame.\n";
    return false;
  }
  const auto file_name = QStringLiteral("%1-%2x%3.png").arg(state).arg(width).arg(height);
  const auto file_path = QDir(output_dir).filePath(file_name);
  if (!frame.save(file_path)) {
    std::cerr << "Timeline proof could not save " << file_path.toStdString() << '\n';
    return false;
  }

  QJsonObject entry{
      {QStringLiteral("state"), state},
      {QStringLiteral("width"), width},
      {QStringLiteral("height"), height},
      {QStringLiteral("path"), file_path},
      {QStringLiteral("requiredObjects"), QJsonArray::fromStringList(required_objects)},
  };
  if (!metadata.isEmpty()) {
    entry.insert(QStringLiteral("metadata"), metadata);
  }
  manifest->append(entry);
  return true;
}

bool timeline_layout_contract_holds(QObject* root, QString* reason) {
  auto* source = find_named_item(root, QStringLiteral("timelineSourceRail"));
  auto* program = find_named_item(root, QStringLiteral("timelineProgramPanel"));
  auto* inspector = find_named_item(root, QStringLiteral("timelineEffectsRail"));
  auto* stage = find_named_item(root, QStringLiteral("timelineProgramMonitorStage"));
  auto* monitor = find_named_item(root, QStringLiteral("timelineProgramMonitor"));
  auto* surface = find_named_item(root, QStringLiteral("timelineSurfacePanel"));
  auto* viewport = find_named_item(root, QStringLiteral("timelineViewport"));
  if (!item_visible(source) || !item_visible(program) || !item_visible(inspector) ||
      !item_visible(stage) || !item_visible(monitor) || !item_visible(surface) ||
      !item_visible(viewport)) {
    if (reason != nullptr) {
      *reason = QStringLiteral("a required primary region is missing or hidden");
    }
    return false;
  }

  const auto source_rect = item_scene_rect(source);
  const auto program_rect = item_scene_rect(program);
  const auto inspector_rect = item_scene_rect(inspector);
  const auto stage_rect = item_scene_rect(stage);
  const auto monitor_rect = item_scene_rect(monitor);
  const auto surface_rect = item_scene_rect(surface);
  const auto viewport_rect = item_scene_rect(viewport);
  const auto monitor_contained = stage_rect.adjusted(-1.0, -1.0, 1.0, 1.0).contains(monitor_rect);
  const auto horizontal_order =
      source_rect.right() <= program_rect.left() + 2.0 &&
      program_rect.right() <= inspector_rect.left() + 2.0;
  const auto timeline_below_program = surface_rect.top() >= program_rect.bottom() - 4.0;
  const auto viewport_inside_timeline =
      surface_rect.adjusted(-1.0, -1.0, 1.0, 1.0).contains(viewport_rect);
  const auto program_is_primary =
      program_rect.width() >= source_rect.width() * 1.25 &&
      program_rect.width() >= inspector_rect.width() * 1.25;
  if (!horizontal_order || !timeline_below_program || !monitor_contained ||
      !viewport_inside_timeline || !program_is_primary) {
    if (reason != nullptr) {
      *reason = QStringLiteral(
          "primary-region geometry failed: source=%1x%2 program=%3x%4 inspector=%5x%6 "
          "surface=%7x%8 monitor=%9x%10")
                    .arg(source_rect.width())
                    .arg(source_rect.height())
                    .arg(program_rect.width())
                    .arg(program_rect.height())
                    .arg(inspector_rect.width())
                    .arg(inspector_rect.height())
                    .arg(surface_rect.width())
                    .arg(surface_rect.height())
                    .arg(monitor_rect.width())
                    .arg(monitor_rect.height());
    }
    return false;
  }
  return true;
}

bool horizontal_control_group_fits(
    QObject* root,
    const QString& container_name,
    const QStringList& ordered_item_names,
    QString* reason) {
  auto* container = find_visible_named_item(root, container_name);
  if (!item_visible(container)) {
    if (reason != nullptr) {
      *reason = QStringLiteral("container %1 is missing").arg(container_name);
    }
    return false;
  }
  const auto container_rect = item_scene_rect(container).adjusted(-1.0, -1.0, 1.0, 1.0);
  auto previous_right = container_rect.left();
  for (const auto& item_name : ordered_item_names) {
    auto* item = find_visible_named_item(root, item_name);
    if (!item_visible(item)) {
      if (reason != nullptr) {
        *reason = QStringLiteral("control %1 is missing").arg(item_name);
      }
      return false;
    }
    const auto item_rect = item_scene_rect(item);
    if (!container_rect.contains(item_rect) || item_rect.left() < previous_right - 2.0) {
      if (reason != nullptr) {
        *reason = QStringLiteral("control %1 escaped or overlapped at x=%2 width=%3")
                      .arg(item_name)
                      .arg(item_rect.left())
                      .arg(item_rect.width());
      }
      return false;
    }
    previous_right = item_rect.right();
  }
  return true;
}

QJsonObject measure_timeline_continuous_scrub_preview(
    QObject* workspace,
    photo_editor::desktop::TimelinePlaybackController* timeline_playback) {
  if (workspace == nullptr || timeline_playback == nullptr) {
    return {};
  }
  timeline_playback->play();
  const auto media_ready_after_warmup = wait_for_condition(
      [timeline_playback]() { return timeline_playback->mediaReady(); },
      8000,
      "timeline-scrub-media-ready");
  const auto first_frame_ready_after_warmup = wait_for_condition(
      [timeline_playback]() {
        return timeline_playback->frameProcessingMetrics()
                   .value(QStringLiteral("sampleCount"))
                   .toLongLong() > 0;
      },
      3000,
      "timeline-scrub-first-frame");
  timeline_playback->pause();
  timeline_playback->seekSequenceSeconds(0.0);
  timeline_playback->resetPlaybackDiagnostics();
  auto scrub_probe_active = false;
  auto last_scrub_frame_sample_count =
      timeline_playback->frameProcessingMetrics()
          .value(QStringLiteral("sampleCount"))
          .toLongLong();
  auto scrub_published_frame_count = qint64{0};
  auto scrub_interaction_active_samples = 0;
  auto scrub_input_index = 0;
  std::vector<double> scrub_request_times_ms;
  std::vector<double> scrub_frame_response_ms;
  QElapsedTimer scrub_elapsed_timer;
  const auto scrub_frame_connection = QObject::connect(
      timeline_playback,
      &photo_editor::desktop::TimelinePlaybackController::processedFrameSourceChanged,
      workspace,
      [&]() {
        if (!scrub_probe_active) {
          return;
        }
        const auto sample_count = timeline_playback->frameProcessingMetrics()
                                     .value(QStringLiteral("sampleCount"))
                                     .toLongLong();
        if (sample_count < last_scrub_frame_sample_count) {
          last_scrub_frame_sample_count = sample_count;
          return;
        }
        if (sample_count == last_scrub_frame_sample_count) {
          return;
        }
        const auto new_samples = sample_count - last_scrub_frame_sample_count;
        for (auto sample = qint64{0}; sample < new_samples; ++sample) {
          ++scrub_published_frame_count;
          if (!scrub_request_times_ms.empty()) {
            scrub_frame_response_ms.push_back(
                scrub_elapsed_timer.elapsed() - scrub_request_times_ms.back());
          }
        }
        last_scrub_frame_sample_count = sample_count;
      });

  const auto begin_gesture_method =
      workspace->metaObject()->indexOfMethod("beginTimelineSeekGesture()") >= 0;
  if (begin_gesture_method) {
    QMetaObject::invokeMethod(workspace, "beginTimelineSeekGesture");
  } else {
    workspace->setProperty("timelineSeeking", true);
  }
  timeline_playback->pause();
  scrub_elapsed_timer.start();
  scrub_probe_active = true;

  constexpr auto scrub_request_count = 48;
  constexpr auto scrub_request_interval_ms = 40;
  QEventLoop scrub_input_loop;
  QTimer scrub_input_timer;
  scrub_input_timer.setTimerType(Qt::PreciseTimer);
  scrub_input_timer.setInterval(scrub_request_interval_ms);
  QObject::connect(&scrub_input_timer, &QTimer::timeout, &scrub_input_loop, [&]() {
    if (timeline_playback->interactionActive()) {
      ++scrub_interaction_active_samples;
    }
    const auto forward_index = scrub_input_index < scrub_request_count / 2
                                  ? scrub_input_index
                                  : scrub_request_count - 1 - scrub_input_index;
    const auto requested_seconds = 0.1 + static_cast<double>(forward_index) * 0.15;
    scrub_request_times_ms.push_back(scrub_elapsed_timer.elapsed());
    QMetaObject::invokeMethod(
        workspace,
        "requestTimelineSeek",
        Q_ARG(QVariant, requested_seconds));
    ++scrub_input_index;
    if (scrub_input_index >= scrub_request_count) {
      scrub_input_timer.stop();
      scrub_input_loop.quit();
    }
  });
  scrub_input_timer.start();
  scrub_input_loop.exec();

  const auto samples_before_final_response = scrub_published_frame_count;
  (void)wait_for_condition(
      [&]() { return scrub_published_frame_count > samples_before_final_response; },
      120,
      "timeline-continuous-scrub-frame");
  const auto scrub_active_duration_ms = scrub_elapsed_timer.elapsed();
  scrub_probe_active = false;
  QObject::disconnect(scrub_frame_connection);
  if (workspace->metaObject()->indexOfMethod("finishTimelineSeekGesture()") >= 0) {
    QMetaObject::invokeMethod(workspace, "finishTimelineSeekGesture");
  } else {
    workspace->setProperty("timelineSeeking", false);
    workspace->setProperty("interactionPlayheadPreviewSeconds", -1.0);
  }

  auto scrub_preview_probe = QJsonObject{};
  if (!scrub_frame_response_ms.empty()) {
    std::sort(scrub_frame_response_ms.begin(), scrub_frame_response_ms.end());
    const auto p95_index = std::min(
        scrub_frame_response_ms.size() - 1,
        static_cast<std::size_t>(std::ceil(
            static_cast<double>(scrub_frame_response_ms.size()) * 0.95) - 1.0));
    scrub_preview_probe.insert(
        QStringLiteral("latestInputToFrameP95Ms"), scrub_frame_response_ms[p95_index]);
    scrub_preview_probe.insert(
        QStringLiteral("latestInputToFrameMaxMs"), scrub_frame_response_ms.back());
  } else {
    scrub_preview_probe.insert(QStringLiteral("latestInputToFrameP95Ms"), -1.0);
    scrub_preview_probe.insert(QStringLiteral("latestInputToFrameMaxMs"), -1.0);
  }
  const auto scrub_frame_rate = scrub_active_duration_ms > 0
                                    ? static_cast<double>(scrub_published_frame_count) * 1000.0 /
                                          static_cast<double>(scrub_active_duration_ms)
                                    : 0.0;
  scrub_preview_probe.insert(QStringLiteral("requestedUpdates"), scrub_input_index);
  scrub_preview_probe.insert(QStringLiteral("inputRateHz"), 25.0);
  scrub_preview_probe.insert(QStringLiteral("mediaReadyAfterWarmup"), media_ready_after_warmup);
  scrub_preview_probe.insert(
      QStringLiteral("firstFrameReadyAfterWarmup"), first_frame_ready_after_warmup);
  scrub_preview_probe.insert(QStringLiteral("activeDurationMs"), scrub_active_duration_ms);
  scrub_preview_probe.insert(QStringLiteral("processedFrames"), scrub_published_frame_count);
  scrub_preview_probe.insert(QStringLiteral("processedFramesPerSecond"), scrub_frame_rate);
  scrub_preview_probe.insert(
      QStringLiteral("interactionActiveSamples"), scrub_interaction_active_samples);
  scrub_preview_probe.insert(
      QStringLiteral("interactionActiveRatio"),
      scrub_input_index > 0
          ? static_cast<double>(scrub_interaction_active_samples) /
                static_cast<double>(scrub_input_index)
          : 0.0);
  scrub_preview_probe.insert(
      QStringLiteral("previewSource"),
      timeline_playback->usingRenderCache()
          ? QStringLiteral("render-cache")
          : timeline_playback->usingProxy() ? QStringLiteral("proxy")
                                            : QStringLiteral("original"));
  scrub_preview_probe.insert(
      QStringLiteral("adaptiveQualityActive"), timeline_playback->adaptiveQualityActive());
  scrub_preview_probe.insert(
      QStringLiteral("hasVideoSource"), timeline_playback->hasVideoSource());
  scrub_preview_probe.insert(
      QStringLiteral("mediaReady"), timeline_playback->mediaReady());
  scrub_preview_probe.insert(
      QStringLiteral("playbackHealth"), timeline_playback->playbackHealth());
  scrub_preview_probe.insert(
      QStringLiteral("activeSourceUrl"), timeline_playback->activeSourceUrl());
  scrub_preview_probe.insert(
      QStringLiteral("processedFrameSource"), timeline_playback->processedFrameSource());
  scrub_preview_probe.insert(
      QStringLiteral("playbackDiagnostics"),
      QJsonObject::fromVariantMap(timeline_playback->playbackDiagnostics()));
  scrub_preview_probe.insert(
      QStringLiteral("frameProcessingMetrics"),
      QJsonObject::fromVariantMap(timeline_playback->frameProcessingMetrics()));
  return scrub_preview_probe;
}

int run_timeline_ui_proof(
    QQuickWindow* root_window,
    QObject* root,
    photo_editor::desktop::AppShellController* app_shell,
    photo_editor::desktop::TimelineDocumentController* timeline_document,
    photo_editor::desktop::TimelinePlaybackController* timeline_playback,
    const QString& source_path,
    const QString& output_dir) {
  if (root_window == nullptr || root == nullptr || app_shell == nullptr ||
      timeline_document == nullptr || timeline_playback == nullptr || output_dir.isEmpty() ||
      !QDir().mkpath(output_dir)) {
    return fail(QStringLiteral("Timeline UI proof could not initialize its runtime or output directory."));
  }

  app_shell->setDarkMode(true);
  QJsonArray manifest;
  timeline_document->createTimeline(QStringLiteral("Timeline empty-state proof"));
  QVariant routed_to_empty_timeline;
  if (!QMetaObject::invokeMethod(
          root,
          "switchToWorkspaceIndex",
          Q_RETURN_ARG(QVariant, routed_to_empty_timeline),
          Q_ARG(QVariant, root->property("timelineWorkspaceIndex"))) ||
      !routed_to_empty_timeline.toBool() ||
      !wait_for_condition(
          [root]() {
            auto* active = active_workspace(root);
            return active != nullptr &&
                   active->objectName() == QStringLiteral("timelineWorkspace") &&
                   !root->property("workspaceTransitionActive").toBool() &&
                   item_visible(find_visible_named_item(
                       active, QStringLiteral("timelineProgramMonitor")));
          },
          30000)) {
    return fail(QStringLiteral("Timeline empty-state proof could not route to Program."));
  }
  auto* empty_workspace = active_workspace(root);
  if (empty_workspace == nullptr) {
    return fail(QStringLiteral("Timeline empty-state proof could not find its workspace."));
  }
  empty_workspace->setProperty("sourcePanelCollapsed", false);
  empty_workspace->setProperty("effectsPanelCollapsed", true);
  process_layout_events(12);
  const QList<QPair<int, int>> empty_sizes = {{1180, 768}, {1440, 900}, {1920, 1080}};
  const QStringList header_control_objects = {
      QStringLiteral("timelineCompactTitleControl"),
      QStringLiteral("timelineProjectSummaryButton"),
      QStringLiteral("timelineHistoryControlGroup"),
      QStringLiteral("timelinePrimaryAddMediaButton"),
      QStringLiteral("timelineViewMenuButton"),
      QStringLiteral("timelineSaveButton"),
      QStringLiteral("timelineSaveStateChip"),
      QStringLiteral("timelineHeaderMoreMenuButton"),
  };
  for (const auto& size : empty_sizes) {
    root->setProperty("width", size.first);
    root->setProperty("height", size.second);
    process_layout_events(10);
    auto* monitor = find_visible_named_item(
        empty_workspace, QStringLiteral("timelineProgramMonitor"));
    auto* empty_card = find_visible_named_item(
        empty_workspace, QStringLiteral("timelineProgramEmptyState"));
    auto* empty_title = find_visible_named_item(
        empty_workspace, QStringLiteral("timelineProgramEmptyTitle"));
    auto* empty_body = find_visible_named_item(
        empty_workspace, QStringLiteral("timelineProgramEmptyBody"));
    const auto card_contained = item_scene_rect(monitor)
                                    .adjusted(-1.0, -1.0, 1.0, 1.0)
                                    .contains(item_scene_rect(empty_card));
    const auto title_fits = empty_title != nullptr &&
                            empty_title->property("contentHeight").toDouble() <=
                                empty_title->height() + 1.0;
    const auto body_fits = empty_body != nullptr &&
                           (!item_visible(empty_body) ||
                            empty_body->property("contentHeight").toDouble() <=
                                empty_body->height() + 1.0);
    if (!item_visible(empty_card) || !card_contained || !title_fits || !body_fits) {
      return fail(QStringLiteral(
                      "Timeline empty guidance clipped or escaped Program at %1x%2 "
                      "(monitor=%3x%4 card=%5x%6 visible=%7 contained=%8 title=%9/%10 "
                      "body=%11/%12).")
                      .arg(size.first)
                      .arg(size.second)
                      .arg(monitor == nullptr ? -1.0 : monitor->width())
                      .arg(monitor == nullptr ? -1.0 : monitor->height())
                      .arg(empty_card == nullptr ? -1.0 : empty_card->width())
                      .arg(empty_card == nullptr ? -1.0 : empty_card->height())
                      .arg(item_visible(empty_card))
                      .arg(card_contained)
                      .arg(empty_title == nullptr ? -1.0 : empty_title->property("contentHeight").toDouble())
                      .arg(empty_title == nullptr ? -1.0 : empty_title->height())
                      .arg(empty_body == nullptr ? -1.0 : empty_body->property("contentHeight").toDouble())
                      .arg(empty_body == nullptr ? -1.0 : empty_body->height()));
    }
    QString header_reason;
    if (!horizontal_control_group_fits(
            empty_workspace,
            QStringLiteral("timelineHeaderBar"),
            header_control_objects,
            &header_reason)) {
      return fail(QStringLiteral("Timeline header failed at %1x%2: %3")
                      .arg(size.first)
                      .arg(size.second)
                      .arg(header_reason));
    }
    if (!capture_timeline_ui_state(
            root_window,
            root,
            output_dir,
            QStringLiteral("timeline-dark-empty"),
            size.first,
            size.second,
            QStringList{
                QStringLiteral("timelineWorkspace"),
                QStringLiteral("timelineProgramPanel"),
                QStringLiteral("timelineProgramMonitor"),
                QStringLiteral("timelineProgramEmptyState"),
                QStringLiteral("timelineProgramEmptyTitle"),
                QStringLiteral("timelineProgramEmptyBody"),
                QStringLiteral("timelineHeaderBar"),
                QStringLiteral("timelineTransportBar"),
                QStringLiteral("timelineSurfacePanel"),
            } + header_control_objects,
            &manifest,
            QJsonObject{{QStringLiteral("state"), QStringLiteral("empty-contained-single-action")}})) {
      return fail(QStringLiteral("Timeline UI proof could not capture the empty state."));
    }
    if (size.first == 1440) {
      auto* source_tab = find_visible_named_item(empty_workspace, QStringLiteral("timelineSourceTab"));
      auto* media_tab = find_visible_named_item(empty_workspace, QStringLiteral("timelineMediaTab"));
      auto* source_monitor = find_visible_named_item(
          empty_workspace, QStringLiteral("timelineSourceMonitor"));
      auto* empty_source_action = find_visible_named_item(
          empty_workspace, QStringLiteral("timelineSourceEmptyBrowseButton"));
      const auto tab_widths_match = source_tab != nullptr && media_tab != nullptr &&
                                    std::abs(source_tab->width() - media_tab->width()) <= 1.0;
      const auto empty_action_centered = source_monitor != nullptr && empty_source_action != nullptr &&
          std::abs(item_scene_rect(source_monitor).center().x() -
                   item_scene_rect(empty_source_action).center().x()) <= 2.0;
      if (!tab_widths_match || !empty_action_centered ||
          named_item_visible(empty_workspace, QStringLiteral("timelineImportMediaButton"))) {
        return fail(QStringLiteral(
            "Source empty state must keep equal tabs, one centered action, and no duplicate import action."));
      }
      if (!capture_timeline_ui_state(
              root_window,
              root,
              output_dir,
              QStringLiteral("timeline-dark-source-empty"),
              1440,
              900,
              {
                  QStringLiteral("timelineSourceRail"),
                  QStringLiteral("timelineSourceTab"),
                  QStringLiteral("timelineMediaTab"),
                  QStringLiteral("timelineSourceMonitor"),
                  QStringLiteral("timelineSourceEmptyBrowseButton"),
              },
              &manifest,
              QJsonObject{{QStringLiteral("state"), QStringLiteral("source-empty-single-action")}})) {
        return fail(QStringLiteral("Timeline UI proof could not capture the calm Source empty state."));
      }
    }
  }

  root->setProperty("width", 1440);
  root->setProperty("height", 900);
  empty_workspace->setProperty("sourceRailTab", QStringLiteral("media"));
  process_layout_events(10);
  auto* media_source_tab = find_visible_named_item(
      empty_workspace, QStringLiteral("timelineSourceTab"));
  auto* media_media_tab = find_visible_named_item(
      empty_workspace, QStringLiteral("timelineMediaTab"));
  if (media_source_tab == nullptr || media_media_tab == nullptr ||
      std::abs(media_source_tab->width() - media_media_tab->width()) > 1.0 ||
      !named_item_visible(empty_workspace, QStringLiteral("timelineImportMediaButton")) ||
      !wait_for_condition(
          [empty_workspace]() {
            return named_item_visible(
                empty_workspace, QStringLiteral("timelineMediaAssetRow"));
          },
          3000)) {
    return fail(QStringLiteral(
        "Media tab did not reveal an equal-width tab pair, Import action, and stable media row."));
  }
  if (!capture_timeline_ui_state(
          root_window,
          root,
          output_dir,
          QStringLiteral("timeline-dark-source-media"),
          1440,
          900,
          {
              QStringLiteral("timelineSourceRail"),
              QStringLiteral("timelineSourceTab"),
              QStringLiteral("timelineMediaTab"),
              QStringLiteral("timelineImportMediaButton"),
              QStringLiteral("timelineMediaAssetRow"),
              QStringLiteral("timelineMediaAssetThumbnail"),
              QStringLiteral("timelineMediaAssetDragAffordance"),
              QStringLiteral("timelineMediaAssetStatus"),
          },
          &manifest,
          QJsonObject{{QStringLiteral("state"), QStringLiteral("media-ready-to-source-or-insert")}})) {
    return fail(QStringLiteral("Timeline UI proof could not capture the functional Media tab."));
  }
  empty_workspace->setProperty("sourceRailTab", QStringLiteral("source"));

  const auto proof_video_path =
      QDir(output_dir).filePath(QStringLiteral("timeline-ui-proof-video.mp4"));
  if (!write_timeline_proof_video_file(source_path, proof_video_path)) {
    return fail(QStringLiteral("Timeline UI proof could not create its real video fixture."));
  }
  timeline_document->createTimeline(QStringLiteral("Timeline UI runtime proof"));
  timeline_document->addClipFromGallery(
      QStringLiteral("timeline-proof-video"),
      proof_video_path,
      QStringLiteral("Opening shot"),
      10.0,
      QUrl::fromLocalFile(source_path).toString());
  timeline_document->loadSourceClipFromGallery(
      QStringLiteral("timeline-proof-source"),
      proof_video_path,
      QStringLiteral("Source insert"),
      10.0,
      QUrl::fromLocalFile(source_path).toString());
  timeline_document->setSourceInSeconds(1.0);
  timeline_document->setSourceOutSeconds(8.0);
  timeline_document->seekSequenceSeconds(5.0);
  timeline_document->insertSourceClipAtPlayhead();
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_TIMELINE_SCRUB_PROBE_ONLY")) {
    auto* const scrub_workspace = active_workspace(root);
    if (scrub_workspace == nullptr || timeline_document->clipCount() == 0 ||
        !wait_for_condition(
            [scrub_workspace]() {
              return named_item_visible(
                  scrub_workspace, QStringLiteral("timelineProgramMonitor"));
            },
            5000,
            "timeline-scrub-program-monitor")) {
      return fail(QStringLiteral("Timeline scrub probe could not initialize the Program preview."));
    }
    timeline_document->selectClip(0);
    timeline_document->seekSequenceSeconds(0.0);
    timeline_playback->seekSequenceSeconds(0.0);
    process_layout_events(4);
    const auto scrub_preview_probe = measure_timeline_continuous_scrub_preview(
        scrub_workspace, timeline_playback);
    QFile scrub_probe_file(
        QDir(output_dir).filePath(QStringLiteral("timeline-scrub-preview.json")));
    if (scrub_preview_probe.isEmpty() ||
        !scrub_probe_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        scrub_probe_file.write(
            QJsonDocument(scrub_preview_probe).toJson(QJsonDocument::Indented)) <= 0) {
      return fail(QStringLiteral("Timeline scrub probe could not write its result."));
    }
    std::cout << QJsonDocument(scrub_preview_probe).toJson().constData();
    return EXIT_SUCCESS;
  }
  timeline_document->addTitleClip();
  timeline_document->generateCaptionsFromTranscript(
      QStringLiteral("Build the story clearly. Keep every moment readable."));
  timeline_document->selectClip(0);
  timeline_document->setSelectedTrimEndSeconds(4.5);
  timeline_document->trimSelectedClipEndToSequenceSeconds(4.0, true);
  if (std::abs(timeline_document->currentClip()
                   .value(QStringLiteral("trimmedDurationSeconds"))
                   .toDouble() - 4.0) > 0.05) {
    return fail(QStringLiteral("Timeline UI proof could not commit its representative ripple trim."));
  }
  timeline_document->setSelectedClipOpacity(0.86);
  timeline_document->setSelectedFrameScale(1.18);
  timeline_document->seekSequenceSeconds(0.0);
  if (!timeline_document->toggleSelectedClipKeyframe(QStringLiteral("frameOffsetX")) ||
      !timeline_document->toggleSelectedClipKeyframe(QStringLiteral("frameOffsetY")) ||
      !timeline_document->setSelectedClipKeyframeValue(
          QStringLiteral("frameOffsetX"), 0.0, -0.16) ||
      !timeline_document->setSelectedClipKeyframeValue(
          QStringLiteral("frameOffsetY"), 0.0, 0.08)) {
    return fail(QStringLiteral("Timeline UI proof could not create the first real motion-path point."));
  }
  timeline_document->seekSequenceSeconds(2.5);
  if (!timeline_document->toggleSelectedClipKeyframe(QStringLiteral("frameOffsetX")) ||
      !timeline_document->toggleSelectedClipKeyframe(QStringLiteral("frameOffsetY")) ||
      !timeline_document->setSelectedClipKeyframeValue(
          QStringLiteral("frameOffsetX"), 2.5, 0.20) ||
      !timeline_document->setSelectedClipKeyframeValue(
          QStringLiteral("frameOffsetY"), 2.5, -0.10)) {
    return fail(QStringLiteral("Timeline UI proof could not create the second real motion-path point."));
  }
  timeline_document->setSelectedClipPlaybackRate(1.25);
  timeline_document->setSelectedClipFadeInSeconds(0.6);
  timeline_document->setSelectedClipFadeOutSeconds(0.8);
  timeline_document->assignSelectedClipTransition(
      QStringLiteral("in"), QStringLiteral("Dip to Color"), 0.35);
  timeline_document->selectClip(1);
  timeline_document->addSelectedClipEffect(QStringLiteral("Exposure"));
  timeline_document->selectClip(0);
  timeline_document->toggleClipSelection(1);
  if (!timeline_document->groupSelectedClips()) {
    return fail(QStringLiteral("Timeline UI proof could not create a persisted clip group."));
  }
  timeline_document->selectClip(0);

  const auto proof_wave_path =
      QDir(output_dir).filePath(
          QStringLiteral("timeline-ui-proof-dialogue-%1.wav")
              .arg(QCoreApplication::applicationPid()));
  if (!write_timeline_proof_wave_file(proof_wave_path)) {
    return fail(QStringLiteral("Timeline UI proof could not create its waveform fixture."));
  }
  const auto proof_tracks_before_audio = timeline_document->sequenceTracks();
  auto proof_audio_lane = std::numeric_limits<int>::max();
  for (const auto& track_value : proof_tracks_before_audio) {
    const auto track = track_value.toMap();
    if (track.value(QStringLiteral("trackKind")).toString() == QStringLiteral("audio")) {
      proof_audio_lane = qMin(
          proof_audio_lane, track.value(QStringLiteral("laneIndex")).toInt());
    }
  }
  if (proof_audio_lane == std::numeric_limits<int>::max()) {
    return fail(QStringLiteral("Timeline UI proof could not find an audio track for its waveform fixture."));
  }
  for (int track_index = 0; track_index < proof_tracks_before_audio.size(); ++track_index) {
    const auto track = proof_tracks_before_audio.at(track_index).toMap();
    if (track.value(QStringLiteral("trackKind")).toString() != QStringLiteral("audio")) {
      continue;
    }
    const auto should_target = track.value(QStringLiteral("laneIndex")).toInt() == proof_audio_lane;
    if (track.value(QStringLiteral("targeted")).toBool() != should_target) {
      timeline_document->toggleTrackTargeted(track_index);
    }
  }
  timeline_document->seekSequenceSeconds(0.0);
  timeline_document->addClipFromGallery(
      QStringLiteral("timeline-proof-audio-%1")
          .arg(QCoreApplication::applicationPid()),
      proof_wave_path,
      QStringLiteral("Dialogue waveform"),
      8.0,
      QString{});
  const auto proof_audio_clip_added = [&timeline_document]() {
    for (const auto& track_value : timeline_document->sequenceTracks()) {
      const auto track = track_value.toMap();
      if (track.value(QStringLiteral("trackKind")).toString() != QStringLiteral("audio")) {
        continue;
      }
      for (const auto& clip_value : track.value(QStringLiteral("clips")).toList()) {
        if (clip_value.toMap().value(QStringLiteral("displayLabel")).toString() ==
            QStringLiteral("Dialogue waveform")) {
          return true;
        }
      }
    }
    return false;
  }();
  if (!proof_audio_clip_added) {
    QStringList audio_track_states;
    for (const auto& track_value : timeline_document->sequenceTracks()) {
      const auto track = track_value.toMap();
      if (track.value(QStringLiteral("trackKind")).toString() == QStringLiteral("audio")) {
        audio_track_states.push_back(
            QStringLiteral("%1 lane=%2 targeted=%3 locked=%4")
                .arg(track.value(QStringLiteral("displayLabel")).toString())
                .arg(track.value(QStringLiteral("laneIndex")).toInt())
                .arg(track.value(QStringLiteral("targeted")).toBool())
                .arg(track.value(QStringLiteral("locked")).toBool()));
      }
    }
    return fail(QStringLiteral("Timeline UI proof could not insert its waveform fixture: %1; audio tracks: %2")
                    .arg(timeline_document->status(), audio_track_states.join(QStringLiteral(", "))));
  }
  // Fixture preparation is not an interactive edit. Ensure a pressure sample or
  // stale latency-sensitive intent cannot leave its required waveform deferred.
  timeline_document->setCachePressureLevelForTesting(QStringLiteral("normal"));
  timeline_document->setBackgroundWorkIntent(false, false, {});
  if (!wait_for_condition(
          [timeline_document]() {
            const auto tracks = timeline_document->sequenceTracks();
            for (const auto& track_value : tracks) {
              const auto track = track_value.toMap();
              if (track.value(QStringLiteral("trackKind")).toString() !=
                  QStringLiteral("audio")) {
                continue;
              }
              for (const auto& clip_value : track.value(QStringLiteral("clips")).toList()) {
                const auto clip = clip_value.toMap();
                if (clip.value(QStringLiteral("displayLabel")).toString() ==
                        QStringLiteral("Dialogue waveform") &&
                    clip.value(QStringLiteral("waveformState")).toString() ==
                        QStringLiteral("ready") &&
                    !clip.value(QStringLiteral("waveformSourceStandard")).toString().isEmpty()) {
                  return true;
                }
              }
            }
            return false;
          },
          20000)) {
    QStringList waveform_states;
    for (const auto& track_value : timeline_document->sequenceTracks()) {
      const auto track = track_value.toMap();
      for (const auto& clip_value : track.value(QStringLiteral("clips")).toList()) {
        const auto clip = clip_value.toMap();
        if (clip.value(QStringLiteral("displayLabel")).toString() !=
            QStringLiteral("Dialogue waveform")) {
          continue;
        }
        waveform_states.push_back(
            QStringLiteral("track=%1 type=%2 analysis=%3 waveform=%4 source=%5")
                .arg(track.value(QStringLiteral("trackId")).toString())
                .arg(clip.value(QStringLiteral("mediaType")).toString())
                .arg(clip.value(QStringLiteral("mediaAnalysisStatus")).toString())
                .arg(clip.value(QStringLiteral("waveformState")).toString())
                .arg(clip.value(QStringLiteral("waveformSourceStandard")).toString()));
      }
    }
    return fail(QStringLiteral("Timeline UI proof did not produce a real audio waveform (%1).")
                    .arg(waveform_states.join(QStringLiteral("; "))));
  }
  timeline_document->setCachePressureLevelForTesting(QString{});
  QString proof_waveform_standard_path;
  for (const auto& track_value : timeline_document->sequenceTracks()) {
    const auto track = track_value.toMap();
    for (const auto& clip_value : track.value(QStringLiteral("clips")).toList()) {
      const auto clip = clip_value.toMap();
      if (clip.value(QStringLiteral("displayLabel")).toString() ==
          QStringLiteral("Dialogue waveform")) {
        proof_waveform_standard_path = QUrl(
            clip.value(QStringLiteral("waveformSourceStandard")).toString()).toLocalFile();
        break;
      }
    }
    if (!proof_waveform_standard_path.isEmpty()) {
      break;
    }
  }
  const auto preserved_waveform_path =
      QDir(output_dir).filePath(QStringLiteral("timeline-ui-proof-waveform-standard.png"));
  QFile::remove(preserved_waveform_path);
  if (proof_waveform_standard_path.isEmpty() ||
      !QFile::copy(proof_waveform_standard_path, preserved_waveform_path)) {
    return fail(QStringLiteral("Timeline UI proof could not preserve its generated waveform image."));
  }

  const auto offline_fixture_path =
      QDir(output_dir).filePath(QStringLiteral("timeline-ui-proof-offline.png"));
  QFile::remove(offline_fixture_path);
  if (!QFile::copy(source_path, offline_fixture_path)) {
    return fail(QStringLiteral("Timeline UI proof could not create its offline-media fixture."));
  }
  timeline_document->seekSequenceSeconds(0.0);
  timeline_document->addClipFromGallery(
      QStringLiteral("timeline-proof-offline"),
      offline_fixture_path,
      QStringLiteral("Missing interview media"),
      4.0,
      QUrl::fromLocalFile(offline_fixture_path).toString());
  timeline_document->addSequenceMarker(QStringLiteral("Review opening"));
  QString initial_save_error;
  if (!save_timeline_and_wait(timeline_document, "timeline-ui-proof-save", &initial_save_error)) {
    return fail(QStringLiteral("Timeline UI proof could not save its populated document: %1")
                    .arg(initial_save_error));
  }
  const auto saved_path = timeline_document->savedDocumentPath();
  const auto expected_clip_count = timeline_document->clipCount();
  timeline_document->closeTimeline();
  QFile::remove(offline_fixture_path);
  if (!timeline_document->loadTimeline(saved_path) ||
      timeline_document->clipCount() != expected_clip_count) {
    return fail(QStringLiteral("Timeline UI proof could not reopen its populated document losslessly."));
  }
  const auto proof_document = QDir(output_dir).filePath(QStringLiteral("timeline-ui-proof-document.json"));
  QFile::remove(proof_document);
  if (!QFile::copy(saved_path, proof_document)) {
    return fail(QStringLiteral("Timeline UI proof could not preserve its reopened document."));
  }

  QVariant routed;
  if (!QMetaObject::invokeMethod(
          root,
          "switchToWorkspaceIndex",
          Q_RETURN_ARG(QVariant, routed),
          Q_ARG(QVariant, root->property("timelineWorkspaceIndex"))) ||
      !routed.toBool() ||
      !wait_for_condition(
          [root]() {
            return root->property("currentWorkspace").toInt() ==
                       root->property("timelineWorkspaceIndex").toInt() &&
                   !root->property("workspaceTransitionActive").toBool() &&
                   find_named_object(root, QStringLiteral("timelineWorkspace")) != nullptr &&
                   find_named_object(root, QStringLiteral("timelineViewport")) != nullptr;
          },
          6000)) {
    return fail(QStringLiteral("Timeline UI proof could not route to the loaded Timeline workspace."));
  }

  auto* workspace = find_named_object(root, QStringLiteral("timelineWorkspace"));
  if (workspace == nullptr ||
      workspace->property("timelineUiConsistencyContract").toString().isEmpty()) {
    return fail(QStringLiteral("Timeline UI proof could not find the workspace consistency contract."));
  }
  if (auto* failure_indicator =
          find_named_object(root, QStringLiteral("failureActivityIndicator"));
      failure_indicator != nullptr) {
    failure_indicator->setProperty("visible", false);
  }
  workspace->setProperty("sourcePanelCollapsed", false);
  workspace->setProperty("effectsPanelCollapsed", false);
  workspace->setProperty("sourcePanelDetached", false);
  workspace->setProperty("effectsPanelDetached", false);
  workspace->setProperty("timelineAdvancedControlsVisible", false);
  timeline_document->applyOutputPreset(QStringLiteral("youtube"));
  timeline_document->selectClip(0);
  process_layout_events(16);
  if (!wait_for_condition(
          [root]() {
            auto* poster = find_named_object(
                root, QStringLiteral("timelineProgramPrimaryPosterImage"));
            return poster != nullptr && poster->property("status").toInt() == 1 &&
                   !poster->property("source").toString().isEmpty();
          },
          3000)) {
    return fail(QStringLiteral("Timeline UI proof could not display the selected clip poster in Program."));
  }

  root->setProperty("width", 1440);
  root->setProperty("height", 900);
  workspace->setProperty("sourceRailTab", QStringLiteral("source"));
  workspace->setProperty("sourcePanelCollapsed", false);
  timeline_document->loadSourceClipFromGallery(
      QStringLiteral("timeline-proof-source-workflow"),
      proof_video_path,
      QStringLiteral("Interview source"),
      10.0,
      QUrl::fromLocalFile(source_path).toString());
  timeline_document->setSourceInSeconds(1.0);
  timeline_document->setSourceOutSeconds(8.0);
  if (!wait_for_condition(
          [timeline_document, workspace]() {
            const auto source = timeline_document->sourceClip();
            return timeline_document->hasSourceClip() &&
                   timeline_document->sourcePatchReady() &&
                   source.value(QStringLiteral("sourceWidth")).toInt() > 0 &&
                   source.value(QStringLiteral("sourceHeight")).toInt() > 0 &&
                   named_item_visible(workspace, QStringLiteral("timelineSourceVideoOutput"));
          },
          8000)) {
    const auto source = timeline_document->sourceClip();
    return fail(QStringLiteral(
                    "Source workflow did not resolve a real aspect-aware video clip "
                    "(has=%1 patch=%2 type=%3 analysis=%4 provisional=%5 size=%6x%7 "
                    "tab=%8 collapsed=%9 outputVisible=%10).")
                    .arg(timeline_document->hasSourceClip())
                    .arg(timeline_document->sourcePatchReady())
                    .arg(source.value(QStringLiteral("mediaType")).toString())
                    .arg(source.value(QStringLiteral("mediaAnalysisStatus")).toString())
                    .arg(source.value(QStringLiteral("durationProvisional")).toBool())
                    .arg(source.value(QStringLiteral("sourceWidth")).toInt())
                    .arg(source.value(QStringLiteral("sourceHeight")).toInt())
                    .arg(workspace->property("sourceRailTab").toString())
                    .arg(workspace->property("sourcePanelCollapsed").toBool())
                    .arg(named_item_visible(
                        workspace, QStringLiteral("timelineSourceVideoOutput"))));
  }
  process_layout_events(10);
  const auto loaded_source = timeline_document->sourceClip();
  auto* source_monitor = find_visible_named_item(
      workspace, QStringLiteral("timelineSourceMonitor"));
  const auto source_width = loaded_source.value(QStringLiteral("sourceWidth")).toDouble();
  const auto source_height = loaded_source.value(QStringLiteral("sourceHeight")).toDouble();
  const auto expected_source_monitor_height = source_monitor == nullptr || source_width <= 0.0
      ? -1.0
      : std::max(140.0, std::min(240.0, source_monitor->width() * source_height / source_width));
  if (!item_visible(source_monitor) ||
      std::abs(source_monitor->height() - expected_source_monitor_height) > 3.0) {
    return fail(QStringLiteral("Source monitor did not size itself to the resolved video aspect ratio."));
  }
  timeline_document->clearSourceInPoint();
  if (std::abs(timeline_document->sourceInSeconds()) > 0.001 ||
      std::abs(timeline_document->sourceOutSeconds() - 8.0) > 0.001) {
    return fail(QStringLiteral("Source Clear In changed the independently marked Out point."));
  }
  timeline_document->setSourceInSeconds(1.0);
  timeline_document->clearSourceOutPoint();
  if (std::abs(timeline_document->sourceInSeconds() - 1.0) > 0.001 ||
      std::abs(timeline_document->sourceOutSeconds() - 10.0) > 0.001) {
    return fail(QStringLiteral("Source Clear Out changed In or failed to restore the source end."));
  }
  timeline_document->setSourceOutSeconds(8.0);
  timeline_document->seekSequenceSeconds(2.0);
  const auto clip_count_before_source_insert = timeline_document->clipCount();
  timeline_document->insertSourceClipAtPlayhead();
  if (timeline_document->clipCount() <= clip_count_before_source_insert ||
      !timeline_document->canUndo() ||
      !timeline_document->status().startsWith(QStringLiteral("Inserted"))) {
    return fail(QStringLiteral("Source Insert did not add the marked range as one undoable operation."));
  }
  timeline_document->undo();
  if (timeline_document->clipCount() != clip_count_before_source_insert) {
    return fail(QStringLiteral("Undo did not restore the sequence after Source Insert."));
  }
  auto* loaded_source_rail = find_visible_named_item(
      workspace, QStringLiteral("timelineSourceRail"));
  QVariant source_scroll_result;
  if (loaded_source_rail == nullptr ||
      !QMetaObject::invokeMethod(
          loaded_source_rail,
          "scrollEditorToTop",
          Q_RETURN_ARG(QVariant, source_scroll_result)) ||
      !source_scroll_result.toBool()) {
    return fail(QStringLiteral("Timeline UI proof could not reset the Source editor scroll position."));
  }
  process_layout_events(8);
  if (!capture_timeline_ui_state(
          root_window,
          root,
          output_dir,
          QStringLiteral("timeline-dark-source-loaded"),
          1440,
          900,
          {
              QStringLiteral("timelineSourceRail"),
              QStringLiteral("timelineSourceTab"),
              QStringLiteral("timelineMediaTab"),
              QStringLiteral("timelineSourceTitleLabel"),
              QStringLiteral("timelineSourceMonitor"),
              QStringLiteral("timelineSourceVideoOutput"),
              QStringLiteral("timelineSourceStepBackButton"),
              QStringLiteral("timelineSourcePlayButton"),
              QStringLiteral("timelineSourceStepForwardButton"),
              QStringLiteral("timelineSourceMarkInButton"),
              QStringLiteral("timelineSourceMarkOutButton"),
              QStringLiteral("timelineSourceScrubber"),
              QStringLiteral("timelineSourceMarkedDuration"),
              QStringLiteral("timelineSourceInStatus"),
              QStringLiteral("timelineClearSourceInButton"),
              QStringLiteral("timelineSourceOutStatus"),
              QStringLiteral("timelineClearSourceOutButton"),
              QStringLiteral("timelineSourceInsertButton"),
              QStringLiteral("timelineSourceEditMenu"),
          },
          &manifest,
          QJsonObject{
              {QStringLiteral("sourceSize"), QStringLiteral("%1x%2").arg(source_width).arg(source_height)},
              {QStringLiteral("markedRange"), QStringLiteral("1.0-8.0")},
              {QStringLiteral("insertUndo"), true},
          })) {
    return fail(QStringLiteral("Timeline UI proof could not capture the loaded Source workflow."));
  }
  source_scroll_result.clear();
  if (!QMetaObject::invokeMethod(
          loaded_source_rail,
          "scrollEditorToBottom",
          Q_RETURN_ARG(QVariant, source_scroll_result)) ||
      !source_scroll_result.toBool()) {
    return fail(QStringLiteral("Timeline UI proof could not reveal the lower Source edit controls."));
  }
  process_layout_events(8);
  if (!capture_timeline_ui_state(
          root_window,
          root,
          output_dir,
          QStringLiteral("timeline-dark-source-marks"),
          1440,
          900,
          {
              QStringLiteral("timelineSourceRail"),
              QStringLiteral("timelineSourceScrubber"),
              QStringLiteral("timelineSourceMarkedDuration"),
              QStringLiteral("timelineSourceInStatus"),
              QStringLiteral("timelineClearSourceInButton"),
              QStringLiteral("timelineSourceOutStatus"),
              QStringLiteral("timelineClearSourceOutButton"),
              QStringLiteral("timelineSourceInsertButton"),
              QStringLiteral("timelineSourceEditMenu"),
          },
          &manifest,
          QJsonObject{
              {QStringLiteral("markedRange"), QStringLiteral("1.0-8.0")},
              {QStringLiteral("actions"), QStringLiteral("insert-append-overwrite")},
          })) {
    return fail(QStringLiteral("Timeline UI proof could not capture marked Source edit controls."));
  }
  source_scroll_result.clear();
  QMetaObject::invokeMethod(
      loaded_source_rail,
      "scrollEditorToTop",
      Q_RETURN_ARG(QVariant, source_scroll_result));
  process_layout_events(4);

  timeline_playback->seekSequenceSeconds(0.5);
  const auto ganged_dirty_before = timeline_document->dirty();
  const auto ganged_clip_count_before = timeline_document->clipCount();
  QVariant gang_enabled_result;
  if (!QMetaObject::invokeMethod(
          workspace,
          "setProgramSourceGangEnabled",
          Q_RETURN_ARG(QVariant, gang_enabled_result),
          Q_ARG(QVariant, QVariant(true))) ||
      !gang_enabled_result.toBool() ||
      !wait_for_condition(
          [workspace, timeline_document]() {
            const auto state = timeline_document->programSourceGangState();
            return workspace->property("programSourceGangEnabled").toBool() &&
                   state.value(QStringLiteral("matched")).toBool() &&
                   state.value(QStringLiteral("sourceSeconds")).toDouble() >= 0.0 &&
                   item_visible(find_visible_named_item(
                       workspace, QStringLiteral("timelineSourceProgramGangOverlay"))) &&
                   item_visible(find_visible_named_item(
                       workspace, QStringLiteral("timelineSourceProgramGangTimeMap")));
          },
          3000)) {
    const auto state = timeline_document->programSourceGangState();
    return fail(QStringLiteral(
                    "Ganged Source and Program did not establish the exact active Program frame "
                    "(enabled=%1 matched=%2 status=%3 clip=%4 source=%5 overlay=%6 map=%7 "
                    "hidden=%8 collapsed=%9).")
                    .arg(workspace->property("programSourceGangEnabled").toBool())
                    .arg(state.value(QStringLiteral("matched")).toBool())
                    .arg(state.value(QStringLiteral("status")).toString())
                    .arg(state.value(QStringLiteral("clipId")).toString())
                    .arg(state.value(QStringLiteral("sourceSeconds")).toDouble())
                    .arg(named_item_visible(
                        workspace, QStringLiteral("timelineSourceProgramGangOverlay")))
                    .arg(named_item_visible(
                        workspace, QStringLiteral("timelineSourceProgramGangTimeMap")))
                    .arg(workspace->property("sourcePanelHidden").toBool())
                    .arg(workspace->property("sourcePanelCollapsed").toBool()));
  }

  const auto initial_gang_state = timeline_document->programSourceGangState();
  const auto initial_source_seconds =
      initial_gang_state.value(QStringLiteral("sourceSeconds")).toDouble();
  const auto source_frame = 1.0 / std::max(1.0, timeline_document->framesPerSecond());
  auto requested_ganged_source = std::min(
      timeline_document->sourceOutSeconds() - source_frame,
      initial_source_seconds + std::max(source_frame, 0.25));
  if (requested_ganged_source <= timeline_document->sourceInSeconds() + source_frame) {
    requested_ganged_source = std::max(
        timeline_document->sourceInSeconds(), initial_source_seconds - 0.25);
  }
  const auto expected_program_mapping =
      timeline_document->gangedProgramPositionForSource(requested_ganged_source);
  if (!expected_program_mapping.value(QStringLiteral("matched")).toBool() ||
      !expected_program_mapping.value(QStringLiteral("frameExact")).toBool() ||
      !QMetaObject::invokeMethod(
          loaded_source_rail,
          "seekSource",
          Q_ARG(QVariant, QVariant(requested_ganged_source)),
          Q_ARG(QVariant, QVariant(true))) ||
      !wait_for_condition(
          [timeline_document, expected_program_mapping, requested_ganged_source]() {
            const auto state = timeline_document->programSourceGangState();
            return std::abs(
                       timeline_document->sequencePlayheadSeconds() -
                       expected_program_mapping.value(QStringLiteral("sequenceSeconds"))
                           .toDouble()) < 0.002 &&
                   std::abs(
                       state.value(QStringLiteral("sourceSeconds")).toDouble() -
                       requested_ganged_source) < 0.5 /
                           std::max(1.0, timeline_document->framesPerSecond());
          },
          3000)) {
    return fail(QStringLiteral(
        "Ganged Source and Program did not maintain exact bidirectional frame mapping when Source scrubbed Program."));
  }

  const auto ganged_sequence_before_playback =
      timeline_document->sequencePlayheadSeconds();
  timeline_playback->play();
  if (!wait_for_condition(
          [timeline_document, ganged_sequence_before_playback]() {
            const auto state = timeline_document->programSourceGangState();
            return timeline_document->sequencePlaying() &&
                   timeline_document->sequencePlayheadSeconds() >
                       ganged_sequence_before_playback + 0.08 &&
                   state.value(QStringLiteral("matched")).toBool() &&
                   std::abs(
                       state.value(QStringLiteral("sequenceSeconds")).toDouble() -
                       timeline_document->sequencePlayheadSeconds()) <
                       1.5 / std::max(1.0, timeline_document->framesPerSecond());
          },
          3000)) {
    const auto state = timeline_document->programSourceGangState();
    return fail(QStringLiteral(
                    "Ganged Source did not follow Program-master playback within one displayed frame "
                    "(playing=%1 before=%2 program=%3 sourceMap=%4 matched=%5 status=%6).")
                    .arg(timeline_document->sequencePlaying())
                    .arg(ganged_sequence_before_playback)
                    .arg(timeline_document->sequencePlayheadSeconds())
                    .arg(state.value(QStringLiteral("sequenceSeconds")).toDouble())
                    .arg(state.value(QStringLiteral("matched")).toBool())
                    .arg(state.value(QStringLiteral("status")).toString()));
  }
  timeline_playback->pause();
  wait_for_condition(
      [timeline_document]() { return !timeline_document->sequencePlaying(); }, 1500);
  process_layout_events(6);

  if (!capture_timeline_ui_state(
          root_window,
          root,
          output_dir,
          QStringLiteral("timeline-ganged-source-program"),
          1440,
          900,
          {
              QStringLiteral("timelineSourceRail"),
              QStringLiteral("timelineSourceProgramGangButton"),
              QStringLiteral("timelineSourceProgramGangOverlay"),
              QStringLiteral("timelineSourceProgramGangStatus"),
              QStringLiteral("timelineSourceProgramGangTimeMap"),
              QStringLiteral("timelineSourceMonitor"),
              QStringLiteral("timelineProgramMonitor"),
              QStringLiteral("timelineTransportBar"),
          },
          &manifest,
          QJsonObject{
              {QStringLiteral("mapping"), QStringLiteral("bidirectional-frame-exact")},
              {QStringLiteral("playbackMaster"), QStringLiteral("program")},
              {QStringLiteral("sourceAudioMutedWhileGanged"), true},
              {QStringLiteral("documentMutation"), false},
          })) {
    return fail(QStringLiteral(
        "Timeline UI proof could not capture the ganged Source and Program monitors."));
  }

  auto* gang_button = find_visible_named_item(
      workspace, QStringLiteral("timelineSourceProgramGangButton"));
  if (!QMetaObject::invokeMethod(gang_button, "clicked") ||
      !wait_for_condition(
          [workspace, timeline_document]() {
            const auto state = timeline_document->programSourceGangState();
            return !workspace->property("programSourceGangEnabled").toBool() &&
                   !state.value(QStringLiteral("active")).toBool() &&
                   !state.value(QStringLiteral("matched")).toBool();
          },
          1500) ||
      timeline_document->dirty() != ganged_dirty_before ||
      timeline_document->clipCount() != ganged_clip_count_before) {
    return fail(QStringLiteral(
        "Unganging Source and Program retained synchronization state or mutated the edit."));
  }

  timeline_document->selectClip(0);
  timeline_playback->seekSequenceSeconds(1.0);
  const auto guide_dirty_before = timeline_document->dirty();
  const auto guide_clip_count_before = timeline_document->clipCount();
  const QStringList composition_guide_presets = {
      QStringLiteral("thirds"),
      QStringLiteral("crosshair"),
      QStringLiteral("custom"),
      QStringLiteral("crop-bounds"),
      QStringLiteral("title-safe"),
  };
  for (const auto& preset : composition_guide_presets) {
    workspace->setProperty("viewerGuidePreset", preset);
    process_layout_events(4);
    if (workspace->property("viewerGuidePreset").toString() != preset ||
        !named_item_visible(workspace, QStringLiteral("timelineViewerGuideOverlay")) ||
        !named_item_visible(workspace, QStringLiteral("timelineViewerGuideCanvas"))) {
      return fail(QStringLiteral("Timeline viewer guide preset '%1' did not render on Program.")
                      .arg(preset));
    }
  }

  workspace->setProperty("viewerGuidePreset", QStringLiteral("custom"));
  workspace->setProperty("customGuideVerticalA", 0.22);
  workspace->setProperty("customGuideVerticalB", 0.78);
  workspace->setProperty("customGuideHorizontalA", 0.28);
  workspace->setProperty("customGuideHorizontalB", 0.72);
  process_layout_events(6);
  if (!QMetaObject::invokeMethod(
          workspace,
          "handleTimelineViewMenuAction",
          Q_ARG(QVariant, QVariant(QStringLiteral("safe-areas")))) ||
      !wait_for_condition(
          [workspace]() {
            return named_item_visible(
                       workspace, QStringLiteral("timelineViewerGuidePresetPicker")) &&
                   named_item_visible(
                       workspace, QStringLiteral("timelineCustomGuideControls")) &&
                   named_item_visible(
                       workspace, QStringLiteral("timelinePlatformOverlayPresetPicker"));
          },
          1500) ||
      !capture_timeline_ui_state(
          root_window,
          root,
          output_dir,
          QStringLiteral("timeline-viewer-guides-panel"),
          1440,
          900,
          {
              QStringLiteral("timelineViewerGuidePresetPicker"),
              QStringLiteral("timelineCustomGuideControls"),
              QStringLiteral("timelineCustomGuideVerticalASlider"),
              QStringLiteral("timelineCustomGuideHorizontalBSlider"),
              QStringLiteral("timelineResetCustomGuidesButton"),
              QStringLiteral("timelinePlatformOverlayPresetPicker"),
          },
          &manifest,
          QJsonObject{
              {QStringLiteral("surface"), QStringLiteral("one-modular-viewer-guide-popup")},
              {QStringLiteral("defaultDensity"), QStringLiteral("closed")},
          })) {
    return fail(QStringLiteral("Timeline UI proof could not open the unified viewer guide controls."));
  }
  auto* viewer_guides_popup =
      find_named_object(workspace, QStringLiteral("timelinePlatformOverlayPopup"));
  if (viewer_guides_popup == nullptr ||
      !QMetaObject::invokeMethod(viewer_guides_popup, "close")) {
    return fail(QStringLiteral("Timeline UI proof could not close the viewer guide controls."));
  }
  process_layout_events(4);
  if (!capture_timeline_ui_state(
          root_window,
          root,
          output_dir,
          QStringLiteral("timeline-viewer-guides-custom"),
          1440,
          900,
          {
              QStringLiteral("timelineProgramMonitor"),
              QStringLiteral("timelineViewerGuideOverlay"),
              QStringLiteral("timelineViewerGuideCanvas"),
              QStringLiteral("timelineViewerGuideLabel"),
              QStringLiteral("timelineViewMenuButton"),
          },
          &manifest,
          QJsonObject{
              {QStringLiteral("preset"), QStringLiteral("custom")},
              {QStringLiteral("verticalGuides"), QStringLiteral("22%-78%")},
              {QStringLiteral("horizontalGuides"), QStringLiteral("28%-72%")},
              {QStringLiteral("documentMutation"), false},
          })) {
    return fail(QStringLiteral("Timeline UI proof could not capture editable custom viewer guides."));
  }

  workspace->setProperty("viewerGuidePreset", QStringLiteral("motion-path"));
  process_layout_events(6);
  if (!named_item_visible(workspace, QStringLiteral("timelineViewerMotionPath")) ||
      !capture_timeline_ui_state(
          root_window,
          root,
          output_dir,
          QStringLiteral("timeline-viewer-guides-motion-path"),
          1440,
          900,
          {
              QStringLiteral("timelineProgramMonitor"),
              QStringLiteral("timelineViewerMotionPath"),
              QStringLiteral("timelineViewMenuButton"),
          },
          &manifest,
          QJsonObject{
              {QStringLiteral("preset"), QStringLiteral("motion-path")},
              {QStringLiteral("source"), QStringLiteral("real-clip-transform-keyframes")},
              {QStringLiteral("keyframeCount"), 2},
          })) {
    return fail(QStringLiteral("Timeline UI proof could not render the real selected-clip motion path."));
  }

  workspace->setProperty("viewerGuidePreset", QStringLiteral("crop-bounds"));
  process_layout_events(6);
  if (!capture_timeline_ui_state(
          root_window,
          root,
          output_dir,
          QStringLiteral("timeline-viewer-guides-crop-bounds"),
          1440,
          900,
          {
              QStringLiteral("timelineProgramMonitor"),
              QStringLiteral("timelineViewerGuideOverlay"),
              QStringLiteral("timelineViewerGuideCanvas"),
              QStringLiteral("timelineViewerGuideLabel"),
          },
          &manifest,
          QJsonObject{
              {QStringLiteral("preset"), QStringLiteral("crop-bounds")},
              {QStringLiteral("geometry"), QStringLiteral("inverse-live-program-transform")},
              {QStringLiteral("frameScale"), 1.18},
          })) {
    return fail(QStringLiteral("Timeline UI proof could not capture transform-aware crop bounds."));
  }

  workspace->setProperty("viewerGuidePreset", QStringLiteral("off"));
  workspace->setProperty("platformOverlayPreset", QStringLiteral("reels"));
  process_layout_events(6);
  if (!named_item_visible(workspace, QStringLiteral("timelineProgramPlatformPreviewOverlay"))) {
    return fail(QStringLiteral("Platform UI did not remain available through the unified viewer guides."));
  }
  workspace->setProperty("platformOverlayPreset", QStringLiteral("off"));
  process_layout_events(4);
  if (timeline_document->dirty() != guide_dirty_before ||
      timeline_document->clipCount() != guide_clip_count_before) {
    return fail(QStringLiteral("Session-only viewer guides mutated or dirtied the timeline document."));
  }

  const auto multicam_angle_image_path =
      QDir(output_dir).filePath(QStringLiteral("timeline-ui-proof-multicam-angle-b.png"));
  const auto multicam_angle_video_path =
      QDir(output_dir).filePath(QStringLiteral("timeline-ui-proof-multicam-angle-b.mp4"));
  QImage multicam_angle_image(source_path);
  if (multicam_angle_image.isNull() ||
      !multicam_angle_image.flipped(Qt::Horizontal).save(multicam_angle_image_path) ||
      !write_timeline_proof_video_file(
          multicam_angle_image_path, multicam_angle_video_path)) {
    return fail(QStringLiteral("Timeline UI proof could not create a distinct second camera angle."));
  }
  timeline_document->addVideoTrack();
  timeline_document->addVideoTrack();
  QVector<QPair<int, int>> media_video_tracks;
  const auto multicam_tracks = timeline_document->sequenceTracks();
  for (int index = 0; index < multicam_tracks.size(); ++index) {
    const auto track = multicam_tracks.at(index).toMap();
    if (track.value(QStringLiteral("trackKind")).toString() ==
            QStringLiteral("video") &&
        track.value(QStringLiteral("trackRole")).toString() !=
            QStringLiteral("caption")) {
      media_video_tracks.push_back(
          {track.value(QStringLiteral("laneIndex")).toInt(), index});
    }
  }
  std::sort(media_video_tracks.begin(), media_video_tracks.end());
  if (media_video_tracks.size() < 2) {
    return fail(QStringLiteral("Timeline UI proof could not create temporary multicam tracks."));
  }
  const auto multicam_track_one_index =
      media_video_tracks.at(media_video_tracks.size() - 2).second;
  const auto multicam_track_two_index =
      media_video_tracks.constLast().second;
  timeline_document->toggleTrackTargeted(multicam_track_one_index);
  timeline_document->seekSequenceSeconds(0.0);
  timeline_document->addClipFromGallery(
      QStringLiteral("timeline-proof-multicam-a"),
      proof_video_path,
      QStringLiteral("Camera A · Wide"),
      10.0,
      QUrl::fromLocalFile(source_path).toString());
  const auto multicam_angle_one_id =
      timeline_document->currentClip().value(QStringLiteral("clipId")).toString();
  timeline_document->toggleTrackTargeted(multicam_track_two_index);
  timeline_document->seekSequenceSeconds(0.0);
  timeline_document->addClipFromGallery(
      QStringLiteral("timeline-proof-multicam-b"),
      multicam_angle_video_path,
      QStringLiteral("Camera B · Close"),
      10.0,
      QUrl::fromLocalFile(multicam_angle_image_path).toString());
  const auto multicam_angle_two_id =
      timeline_document->currentClip().value(QStringLiteral("clipId")).toString();
  const auto clip_index_for_id = [timeline_document](const QString& clip_id) {
    for (const auto& track_value : timeline_document->sequenceTracks()) {
      for (const auto& clip_value :
           track_value.toMap().value(QStringLiteral("clips")).toList()) {
        const auto clip = clip_value.toMap();
        if (clip.value(QStringLiteral("clipId")).toString() == clip_id) {
          return clip.value(QStringLiteral("index"), -1).toInt();
        }
      }
    }
    return -1;
  };
  const auto multicam_angle_one_index = clip_index_for_id(multicam_angle_one_id);
  const auto multicam_angle_two_index = clip_index_for_id(multicam_angle_two_id);
  if (multicam_angle_one_index < 0 || multicam_angle_two_index < 0) {
    return fail(QStringLiteral("Timeline UI proof could not route its temporary camera clips."));
  }
  timeline_document->selectClip(multicam_angle_one_index);
  timeline_document->seekCurrentClipSeconds(1.0);
  if (!timeline_document->setSelectedClipMulticamSyncMarkerAtPlayhead()) {
    return fail(QStringLiteral("Timeline UI proof could not mark the first multicam angle."));
  }
  timeline_document->selectClip(multicam_angle_two_index);
  timeline_document->seekCurrentClipSeconds(1.0);
  if (!timeline_document->setSelectedClipMulticamSyncMarkerAtPlayhead()) {
    return fail(QStringLiteral("Timeline UI proof could not mark the second multicam angle."));
  }
  timeline_document->selectClip(multicam_angle_one_index);
  timeline_document->toggleClipSelection(multicam_angle_two_index);
  const auto multicam_group_id =
      timeline_document->createMulticamGroupFromSelection(
          QStringLiteral("marker"), QStringLiteral("Interview cameras"));
  timeline_document->seekSequenceSeconds(2.0);
  if (multicam_group_id.isEmpty() ||
      !timeline_document->switchMulticamAngle(multicam_group_id, 1, 2.0) ||
      !timeline_document->setMulticamAudioMode(
          multicam_group_id, QStringLiteral("fixed-angle"), 0)) {
    return fail(QStringLiteral("Timeline UI proof could not author independent multicam video and audio choices."));
  }

  // The offscreen Qt platform used by this proof cannot safely construct two
  // simultaneous AVFoundation video outputs. The product default remains live
  // decode; this visual pass uses the same exact controller mapping with each
  // angle's real poster while native tests prove the source-time contract.
  workspace->setProperty("multicamLiveDecodeEnabled", false);
  if (!QMetaObject::invokeMethod(
          workspace,
          "openMulticam",
          Q_ARG(QVariant, QVariant())) ||
      !wait_for_condition(
          [workspace]() {
            auto* popup = find_named_object(
                workspace, QStringLiteral("timelineMulticamPopup"));
            return popup != nullptr && popup->property("visible").toBool() &&
                   named_item_visible(workspace, QStringLiteral("timelineMulticamMonitorGrid")) &&
                   named_item_visible(workspace, QStringLiteral("timelineMulticamAngleMonitor_1")) &&
                   named_item_visible(workspace, QStringLiteral("timelineMulticamAngleMonitor_2")) &&
                   named_item_visible(workspace, QStringLiteral("timelineMulticamAudioModePicker")) &&
                   named_item_visible(workspace, QStringLiteral("timelineMulticamPlayPauseButton"));
          },
          5000)) {
    return fail(QStringLiteral("Timeline UI proof could not open the synchronized multicam monitor."));
  }
  QPointer<QObject> multicam_angle_one =
      find_named_object(workspace, QStringLiteral("timelineMulticamAngleMonitor_1"));
  QPointer<QObject> multicam_angle_two =
      find_named_object(workspace, QStringLiteral("timelineMulticamAngleMonitor_2"));
  auto* multicam_content =
      find_named_object(workspace, QStringLiteral("timelineMulticamContent"));
  if (multicam_angle_one == nullptr || multicam_angle_two == nullptr ||
      multicam_content == nullptr ||
      !multicam_angle_one->property("previewState").toMap()
           .value(QStringLiteral("available")).toBool() ||
      !multicam_angle_two->property("previewState").toMap()
           .value(QStringLiteral("available")).toBool() ||
      multicam_angle_one->property("activeAngle").toBool() ||
      !multicam_angle_two->property("activeAngle").toBool() ||
      !multicam_angle_one->property("audioActive").toBool() ||
      multicam_angle_two->property("audioActive").toBool()) {
    return fail(QStringLiteral("The multicam monitor did not distinguish Program video from the fixed audio angle."));
  }

  QKeyEvent angle_one_press(QEvent::KeyPress, Qt::Key_1, Qt::NoModifier);
  QKeyEvent angle_one_release(QEvent::KeyRelease, Qt::Key_1, Qt::NoModifier);
  QCoreApplication::sendEvent(multicam_content, &angle_one_press);
  QCoreApplication::sendEvent(multicam_content, &angle_one_release);
  if (timeline_document->programClip()
          .value(QStringLiteral("displayLabel")).toString() !=
      QStringLiteral("Camera A · Wide")) {
    return fail(QStringLiteral("Multicam keyboard key 1 did not cut Program to camera one."));
  }
  QKeyEvent angle_two_press(QEvent::KeyPress, Qt::Key_2, Qt::NoModifier);
  QKeyEvent angle_two_release(QEvent::KeyRelease, Qt::Key_2, Qt::NoModifier);
  QCoreApplication::sendEvent(multicam_content, &angle_two_press);
  QCoreApplication::sendEvent(multicam_content, &angle_two_release);
  if (timeline_document->programClip()
          .value(QStringLiteral("displayLabel")).toString() !=
          QStringLiteral("Camera B · Close") ||
      !timeline_document->nudgeMulticamCut(multicam_group_id, 2.0, 1)) {
    return fail(QStringLiteral("Multicam keyboard switching or one-frame decision editing failed."));
  }

  const auto multicam_playhead_before = timeline_document->sequencePlayheadSeconds();
  timeline_playback->play();
  if (!wait_for_condition(
          [timeline_document, multicam_playhead_before]() {
            return timeline_document->sequencePlaying() &&
                   timeline_document->sequencePlayheadSeconds() >
                       multicam_playhead_before + 0.08;
          },
          3000)) {
    return fail(QStringLiteral("The synchronized multicam angles did not follow Program playback."));
  }
  timeline_playback->pause();
  wait_for_condition(
      [timeline_document]() { return !timeline_document->sequencePlaying(); }, 1500);
  process_layout_events(8);
  if (auto* unrelated_failure =
          find_named_object(root, QStringLiteral("failureActivityIndicator"));
      unrelated_failure != nullptr) {
    unrelated_failure->setProperty("visible", false);
  }
  if (!capture_timeline_ui_state(
          root_window,
          root,
          output_dir,
          QStringLiteral("timeline-multicam-monitor"),
          1440,
          900,
          {
              QStringLiteral("timelineMulticamMonitorGrid"),
              QStringLiteral("timelineMulticamAngleMonitor_1"),
              QStringLiteral("timelineMulticamAngleMonitor_2"),
              QStringLiteral("timelineMulticamProgramTimecode"),
              QStringLiteral("timelineMulticamAudioModePicker"),
              QStringLiteral("timelineMulticamPlayPauseButton"),
              QStringLiteral("timelineMulticamCutList"),
          },
          &manifest,
          QJsonObject{
              {QStringLiteral("sync"), QStringLiteral("shared-program-sequence-clock")},
              {QStringLiteral("programAngle"), 2},
              {QStringLiteral("audioPolicy"), QStringLiteral("fixed-angle-1")},
              {QStringLiteral("keyboardSwitching"), QStringLiteral("keys-1-and-2-proven")},
              {QStringLiteral("cutEditing"), QStringLiteral("one-frame-nudge-proven")},
          })) {
    return fail(QStringLiteral("Timeline UI proof could not capture the functional multicam monitor."));
  }

  auto* multicam_popup =
      find_named_object(workspace, QStringLiteral("timelineMulticamPopup"));
  QPointer<QObject> release_angle_one =
      find_named_object(workspace, QStringLiteral("timelineMulticamAngleMonitor_1"));
  QPointer<QObject> release_angle_two =
      find_named_object(workspace, QStringLiteral("timelineMulticamAngleMonitor_2"));
  if (multicam_popup == nullptr ||
      !QMetaObject::invokeMethod(multicam_popup, "close") ||
      !wait_for_condition(
          [multicam_popup, release_angle_one, release_angle_two]() {
            return !multicam_popup->property("visible").toBool() &&
                   (release_angle_one.isNull() ||
                    release_angle_one->property("previewState").toMap().isEmpty()) &&
                   (release_angle_two.isNull() ||
                    release_angle_two->property("previewState").toMap().isEmpty());
          },
          2000)) {
    return fail(QStringLiteral("Closing the multicam monitor did not release its preview sources."));
  }
  if (QCoreApplication::arguments().contains(
          QStringLiteral("--timeline-multicam-proof-only"))) {
    QFile manifest_file(
        QDir(output_dir).filePath(
            QStringLiteral("timeline-multicam-runtime-proof-manifest.json")));
    if (!manifest_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        manifest_file.write(
            QJsonDocument(manifest).toJson(QJsonDocument::Indented)) <= 0) {
      return fail(QStringLiteral(
          "Timeline multicam proof could not write its evidence manifest."));
    }
    std::cout << "Timeline multicam runtime proof passed with " << manifest.size()
              << " captures in " << output_dir.toStdString() << '\n';
    return EXIT_SUCCESS;
  }
  if (!timeline_document->loadTimeline(saved_path) ||
      timeline_document->clipCount() != expected_clip_count) {
    return fail(QStringLiteral("Timeline UI proof could not restore its primary document after multicam proof."));
  }
  timeline_document->applyOutputPreset(QStringLiteral("youtube"));
  workspace->setProperty("multicamLiveDecodeEnabled", true);
  if (!wait_for_condition(
          [timeline_document]() {
            return !timeline_document->incrementalLoadBusy();
          },
          5000)) {
    return fail(QStringLiteral("Primary proof media did not finish hydrating after multicam proof."));
  }
  auto restored_playable_index = -1;
  for (int index = 0; index < timeline_document->clipCount(); ++index) {
    timeline_document->selectClip(index);
    const auto clip = timeline_document->currentClip();
    if (clip.value(QStringLiteral("displayLabel")).toString() ==
            QStringLiteral("Opening shot") &&
        clip.value(QStringLiteral("mediaType")).toString() ==
            QStringLiteral("video")) {
      restored_playable_index = index;
      break;
    }
  }
  if (restored_playable_index < 0) {
    return fail(QStringLiteral("Primary proof could not restore its playable opening clip."));
  }
  timeline_playback->seekCurrentClipSeconds(1.0);
  if (!wait_for_condition(
          [timeline_document, timeline_playback]() {
            return !timeline_document->sequencePlaying() &&
                   !timeline_document->playbackSourceUrl().isEmpty() &&
                   !timeline_playback->activeSourceUrl().isEmpty();
          },
          5000)) {
    const auto restored_clip = timeline_document->currentClip();
    return fail(QStringLiteral(
                    "Timeline playback did not settle after restoring the primary proof document "
                    "(clip=%1 type=%2 file=%3 documentSource=%4 activeSource=%5 hydration=%6).")
                    .arg(restored_clip.value(QStringLiteral("displayLabel")).toString())
                    .arg(restored_clip.value(QStringLiteral("mediaType")).toString())
                    .arg(restored_clip.value(QStringLiteral("filePath")).toString())
                    .arg(timeline_document->playbackSourceUrl())
                    .arg(timeline_playback->activeSourceUrl())
                    .arg(timeline_document->incrementalLoadStatus()));
  }
  process_layout_events(8);

  const QStringList expanded_objects = {
      QStringLiteral("timelineWorkspace"),
      QStringLiteral("timelineHeaderBar"),
      QStringLiteral("timelineSourceRail"),
      QStringLiteral("timelineProgramPanel"),
      QStringLiteral("timelineProgramMonitor"),
      QStringLiteral("timelineTransportBar"),
      QStringLiteral("timelineEffectsRail"),
      QStringLiteral("timelineSurfacePanel"),
      QStringLiteral("timelineToolBar"),
      QStringLiteral("timelineTrackHeaderColumn"),
      QStringLiteral("timelineTrackHeaderWidthHandle"),
      QStringLiteral("timelineViewport"),
      QStringLiteral("timelinePlayheadHitTarget"),
      QStringLiteral("timelineBladeToolButton"),
      QStringLiteral("timelineTrimToolButton"),
      QStringLiteral("timelineVerticalSplitterHandle"),
      QStringLiteral("timelineSourceCollapseButton"),
      QStringLiteral("timelineInspectorCollapseButton"),
      QStringLiteral("timelinePrimaryAddMediaButton"),
      QStringLiteral("timelineJumpStartButton"),
      QStringLiteral("timelineStepBackButton"),
      QStringLiteral("timelineStepForwardButton"),
      QStringLiteral("timelineLinkedSelectionButton"),
      QStringLiteral("timelineSnappingButton"),
      QStringLiteral("timelineZoomSlider"),
      QStringLiteral("timelineZoomMenuButton"),
      QStringLiteral("timelineCreateMenuButton"),
      QStringLiteral("timelineSearchButton"),
      QStringLiteral("timelineRuler"),
  };
  const QStringList core_objects = {
      QStringLiteral("timelineWorkspace"),
      QStringLiteral("timelineProgramPanel"),
      QStringLiteral("timelineProgramMonitor"),
      QStringLiteral("timelineSurfacePanel"),
      QStringLiteral("timelineToolBar"),
      QStringLiteral("timelineViewport"),
  };

  auto* track_headers = find_visible_named_item(
      workspace, QStringLiteral("timelineTrackHeaderColumn"));
  auto* viewport = find_visible_named_item(workspace, QStringLiteral("timelineViewport"));
  auto* ruler = find_visible_named_item(workspace, QStringLiteral("timelineRuler"));
  auto* playhead_line = find_visible_named_item(
      workspace, QStringLiteral("timelinePlayheadLine"));
  auto* playhead_target = find_visible_named_item(
      workspace, QStringLiteral("timelinePlayheadHitTarget"));
  timeline_playback->seekSequenceSeconds(0.0);
  if (viewport != nullptr) {
    viewport->setProperty("contentX", 0.0);
  }
  process_layout_events(3);
  if (!item_visible(track_headers) || !item_visible(viewport) || !item_visible(ruler) ||
      !item_visible(playhead_line) || !item_visible(playhead_target) ||
      std::abs(item_scene_rect(track_headers).right() - item_scene_rect(viewport).left()) > 2.0 ||
      std::abs(item_scene_rect(ruler).left() - item_scene_rect(viewport).left()) > 2.0 ||
      std::abs(item_scene_rect(playhead_line).center().x() - item_scene_rect(ruler).left()) > 2.0 ||
      playhead_target->width() < playhead_line->width() + 12.0) {
    return fail(QStringLiteral(
        "Timeline ruler, time zero, lane boundary, playhead, or hit-target geometry diverged."));
  }

  timeline_document->setActiveTool(QStringLiteral("razor"));
  process_layout_events(2);
  auto* blade_button = find_visible_named_item(
      workspace, QStringLiteral("timelineBladeToolButton"));
  if (blade_button == nullptr || !blade_button->property("active").toBool()) {
    return fail(QStringLiteral("Timeline Blade did not expose its active checked state."));
  }
  timeline_document->setActiveTool(QStringLiteral("roll"));
  process_layout_events(2);
  auto* trim_button = find_visible_named_item(
      workspace, QStringLiteral("timelineTrimToolButton"));
  if (trim_button == nullptr || !trim_button->property("active").toBool()) {
    return fail(QStringLiteral("Timeline Trim did not expose its active checked state."));
  }
  timeline_document->setActiveTool(QStringLiteral("select"));

  workspace->setProperty("snappingEnabled", false);
  workspace->setProperty("rippleModeEnabled", false);
  timeline_document->setLinkedSelectionEnabled(false);
  process_layout_events(2);
  auto* snapping_button = find_visible_named_item(
      workspace, QStringLiteral("timelineSnappingButton"));
  auto* ripple_button = find_visible_named_item(
      workspace, QStringLiteral("timelineRippleButton"));
  auto* linked_button = find_visible_named_item(
      workspace, QStringLiteral("timelineLinkedSelectionButton"));
  if (snapping_button == nullptr || ripple_button == nullptr || linked_button == nullptr ||
      snapping_button->property("active").toBool() ||
      ripple_button->property("active").toBool() ||
      linked_button->property("active").toBool()) {
    return fail(QStringLiteral("Timeline checked edit states did not switch off independently."));
  }
  workspace->setProperty("snappingEnabled", true);
  workspace->setProperty("rippleModeEnabled", true);
  timeline_document->setLinkedSelectionEnabled(true);
  process_layout_events(2);
  if (!snapping_button->property("active").toBool() ||
      !ripple_button->property("active").toBool() ||
      !linked_button->property("active").toBool()) {
    return fail(QStringLiteral("Timeline checked edit states did not switch on independently."));
  }

  workspace->setProperty("timelineZoom", 24.0);
  process_layout_events(2);
  QVariant low_zoom_interval;
  if (!QMetaObject::invokeMethod(
          workspace, "rulerInterval", Q_RETURN_ARG(QVariant, low_zoom_interval))) {
    return fail(QStringLiteral("Timeline ruler interval was not callable at low zoom."));
  }
  workspace->setProperty("timelineZoom", 240.0);
  process_layout_events(2);
  QVariant high_zoom_interval;
  if (!QMetaObject::invokeMethod(
          workspace, "rulerInterval", Q_RETURN_ARG(QVariant, high_zoom_interval)) ||
      high_zoom_interval.toDouble() >= low_zoom_interval.toDouble()) {
    return fail(QStringLiteral("Timeline ruler density did not increase with zoom."));
  }

  workspace->setProperty("timelineZoom", 84.0);
  viewport->setProperty("contentX", 0.0);
  process_layout_events(2);
  const auto anchor_seconds = 5.0;
  const auto old_zoom = workspace->property("timelineZoom").toDouble();
  const auto old_content_x = viewport->property("contentX").toDouble();
  const auto anchor_content_x = anchor_seconds * old_zoom;
  const auto old_anchor_screen_x = anchor_content_x - old_content_x;
  if (!QMetaObject::invokeMethod(
          workspace,
          "setTimelineZoom",
          Q_ARG(QVariant, QVariant(old_zoom * 1.75)),
          Q_ARG(QVariant, QVariant(anchor_content_x)))) {
    return fail(QStringLiteral("Timeline anchored zoom was not callable."));
  }
  process_layout_events(3);
  const auto new_anchor_screen_x =
      anchor_seconds * workspace->property("timelineZoom").toDouble() -
      viewport->property("contentX").toDouble();
  if (std::abs(new_anchor_screen_x - old_anchor_screen_x) > 1.0) {
    return fail(QStringLiteral("Timeline zoom did not preserve its content anchor."));
  }
  workspace->setProperty("timelineZoom", 84.0);
  viewport->setProperty("contentX", 0.0);
  process_layout_events(3);

  timeline_document->selectClip(restored_playable_index);
  timeline_playback->seekCurrentClipSeconds(0.0);
  if (!wait_for_condition(
          [timeline_playback]() {
            return !timeline_playback->activeSourceUrl().isEmpty();
          },
          3000)) {
    return fail(QStringLiteral("Full-screen proof could not restore the playable Program source."));
  }

  auto* normal_program_stage = find_visible_named_item(
      workspace, QStringLiteral("timelineProgramMonitorStage"));
  auto* truthful_program_monitor = find_visible_named_item(
      workspace, QStringLiteral("timelineProgramMonitor"));
  const auto fullscreen_source_before = timeline_playback->activeSourceUrl();
  const auto fullscreen_playhead_before = timeline_document->sequencePlayheadSeconds();
  const auto fullscreen_window_visibility_before = root_window->visibility();
  workspace->setProperty("fullScreenProgramNativeWindowEnabled", false);
  QVariant entered_fullscreen_program;
  if (normal_program_stage == nullptr || truthful_program_monitor == nullptr ||
      truthful_program_monitor->parentItem() != normal_program_stage ||
      !QMetaObject::invokeMethod(
          workspace,
          "enterFullScreenProgram",
          Q_RETURN_ARG(QVariant, entered_fullscreen_program)) ||
      !entered_fullscreen_program.toBool() ||
      !wait_for_condition(
          [root, workspace]() {
            auto* monitor = find_visible_named_item(
                workspace, QStringLiteral("timelineProgramMonitor"));
            auto* stage = find_visible_named_item(
                workspace, QStringLiteral("timelineFullScreenProgramStage"));
            return workspace->property("fullScreenProgramActive").toBool() &&
                   item_visible(find_visible_named_item(
                       workspace, QStringLiteral("timelineFullScreenProgramSurface"))) &&
                   monitor != nullptr && stage != nullptr && monitor->parentItem() == stage &&
                   !item_visible(find_named_item(root, QStringLiteral("appNavigationBar")));
          },
          3000)) {
    return fail(QStringLiteral(
        "Full-screen Program did not reparent the truthful monitor or remove shell chrome."));
  }

  auto* fullscreen_program_stage = find_visible_named_item(
      workspace, QStringLiteral("timelineFullScreenProgramStage"));
  truthful_program_monitor = find_visible_named_item(
      workspace, QStringLiteral("timelineProgramMonitor"));
  auto* fullscreen_exit = find_visible_named_item(
      workspace, QStringLiteral("timelineFullScreenExitButton"));
  auto* fullscreen_timecode = find_visible_named_item(
      workspace, QStringLiteral("timelineFullScreenTimecode"));
  auto* fullscreen_play_pause = find_visible_named_item(
      workspace, QStringLiteral("timelineFullScreenPlayPauseButton"));
  auto* fullscreen_step_forward = find_visible_named_item(
      workspace, QStringLiteral("timelineFullScreenStepForwardButton"));
  const auto fullscreen_stage_rect = item_scene_rect(fullscreen_program_stage);
  const auto fullscreen_monitor_rect = item_scene_rect(truthful_program_monitor);
  if (!item_visible(fullscreen_exit) || !item_visible(fullscreen_timecode) ||
      !item_visible(fullscreen_play_pause) || !item_visible(fullscreen_step_forward) ||
      fullscreen_timecode->property("text").toString().isEmpty() ||
      !fullscreen_stage_rect.adjusted(-1.0, -1.0, 1.0, 1.0)
           .contains(fullscreen_monitor_rect) ||
      fullscreen_monitor_rect.width() < fullscreen_stage_rect.width() * 0.75 ||
      std::abs(timeline_document->sequencePlayheadSeconds() - fullscreen_playhead_before) >
          0.001 ||
      timeline_playback->activeSourceUrl() != fullscreen_source_before) {
    return fail(QStringLiteral(
                    "Full-screen Program changed playback continuity or omitted its controls "
                    "(exit=%1 timecode=%2 play=%3 step=%4 text=%5 contained=%6 width=%7/%8 "
                    "playhead=%9/%10 sourceBefore=%11 sourceAfter=%12).")
                    .arg(item_visible(fullscreen_exit))
                    .arg(item_visible(fullscreen_timecode))
                    .arg(item_visible(fullscreen_play_pause))
                    .arg(item_visible(fullscreen_step_forward))
                    .arg(fullscreen_timecode == nullptr
                             ? QString{} : fullscreen_timecode->property("text").toString())
                    .arg(fullscreen_stage_rect.adjusted(-1.0, -1.0, 1.0, 1.0)
                             .contains(fullscreen_monitor_rect))
                    .arg(fullscreen_monitor_rect.width())
                    .arg(fullscreen_stage_rect.width())
                    .arg(timeline_document->sequencePlayheadSeconds())
                    .arg(fullscreen_playhead_before)
                    .arg(fullscreen_source_before)
                    .arg(timeline_playback->activeSourceUrl()));
  }

  workspace->setProperty("fullScreenProgramSafeAreas", true);
  workspace->setProperty("fullScreenProgramPlatformPreset", QStringLiteral("tiktok"));
  process_layout_events(5);
  if (!workspace->property("fullScreenProgramSafeAreas").toBool() ||
      workspace->property("fullScreenProgramPlatformPreset").toString() !=
          QStringLiteral("tiktok") ||
      !item_visible(find_visible_named_item(
          workspace, QStringLiteral("timelineSafeAreaOverlay"))) ||
      !item_visible(find_visible_named_item(
          workspace, QStringLiteral("timelineProgramPlatformPreviewOverlay")))) {
    return fail(QStringLiteral(
        "Full-screen Program temporary safe-area and platform overlays did not reach the actual monitor."));
  }

  const auto playhead_before_step = timeline_document->sequencePlayheadSeconds();
  if (!QMetaObject::invokeMethod(fullscreen_step_forward, "clicked") ||
      !wait_for_condition(
          [timeline_document, playhead_before_step]() {
            return timeline_document->sequencePlayheadSeconds() > playhead_before_step + 0.001;
          },
          1000)) {
    return fail(QStringLiteral("Full-screen Program frame-step transport did not move the sequence."));
  }
  if (!QMetaObject::invokeMethod(fullscreen_play_pause, "clicked") ||
      !wait_for_condition(
          [timeline_document]() { return timeline_document->sequencePlaying(); }, 1500) ||
      !QMetaObject::invokeMethod(fullscreen_play_pause, "clicked") ||
      !wait_for_condition(
          [timeline_document]() { return !timeline_document->sequencePlaying(); }, 1500)) {
    return fail(QStringLiteral("Full-screen Program play/pause did not control the shared sequence clock."));
  }
  QMetaObject::invokeMethod(workspace, "showFullScreenProgramControls");
  if (!capture_timeline_ui_state(
          root_window,
          root,
          output_dir,
          QStringLiteral("timeline-fullscreen-program"),
          1440,
          900,
          {
              QStringLiteral("timelineFullScreenProgramSurface"),
              QStringLiteral("timelineFullScreenProgramStage"),
              QStringLiteral("timelineProgramMonitor"),
              QStringLiteral("timelineFullScreenTopBar"),
              QStringLiteral("timelineFullScreenExitButton"),
              QStringLiteral("timelineFullScreenTimecode"),
              QStringLiteral("timelineFullScreenTransport"),
              QStringLiteral("timelineFullScreenPlayPauseButton"),
              QStringLiteral("timelineFullScreenSeekSlider"),
              QStringLiteral("timelineFullScreenSafeAreasButton"),
              QStringLiteral("timelineFullScreenPlatformOverlayMenu"),
              QStringLiteral("timelineProgramPlatformPreviewOverlay"),
          },
          &manifest,
          QJsonObject{
              {QStringLiteral("surface"), QStringLiteral("actual-program-reparented")},
              {QStringLiteral("transport"), QStringLiteral("shared-sequence-clock")},
              {QStringLiteral("temporaryOverlays"), QStringLiteral("safe+tiktok")},
              {QStringLiteral("return"), QStringLiteral("button+escape")},
          })) {
    return fail(QStringLiteral("Timeline UI proof could not capture full-screen Program."));
  }

  if (!QMetaObject::invokeMethod(fullscreen_exit, "clicked") ||
      !wait_for_condition(
          [root, root_window, workspace, normal_program_stage,
           fullscreen_window_visibility_before]() {
            auto* monitor = find_visible_named_item(
                workspace, QStringLiteral("timelineProgramMonitor"));
            return !workspace->property("fullScreenProgramActive").toBool() &&
                   monitor != nullptr && monitor->parentItem() == normal_program_stage &&
                   root_window->visibility() == fullscreen_window_visibility_before &&
                   item_visible(find_visible_named_item(
                       root, QStringLiteral("appNavigationBar")));
          },
          3000)) {
    return fail(QStringLiteral(
        "Full-screen Program did not restore the normal Timeline layout or obvious shell route."));
  }
  QString restored_fullscreen_layout_reason;
  if (workspace->property("fullScreenProgramSafeAreas").toBool() ||
      workspace->property("fullScreenProgramPlatformPreset").toString() !=
          QStringLiteral("off") ||
      root_window->visibility() != fullscreen_window_visibility_before ||
      timeline_playback->activeSourceUrl() != fullscreen_source_before ||
      !timeline_layout_contract_holds(root, &restored_fullscreen_layout_reason)) {
    return fail(QStringLiteral(
                    "Full-screen Program did not restore the normal Timeline layout "
                    "(safe=%1 platform=%2 visibility=%3/%4 sourceBefore=%5 sourceAfter=%6 "
                    "layout=%7 playhead=%8 clip=%9 type=%10 clipSource=%11).")
                    .arg(workspace->property("fullScreenProgramSafeAreas").toBool())
                    .arg(workspace->property("fullScreenProgramPlatformPreset").toString())
                    .arg(root_window->visibility())
                    .arg(fullscreen_window_visibility_before)
                    .arg(fullscreen_source_before)
                    .arg(timeline_playback->activeSourceUrl())
                    .arg(restored_fullscreen_layout_reason)
                    .arg(timeline_document->sequencePlayheadSeconds())
                    .arg(timeline_document->currentClip().value(QStringLiteral("label")).toString())
                    .arg(timeline_document->currentClip().value(QStringLiteral("mediaType")).toString())
                    .arg(timeline_document->currentClip().value(QStringLiteral("sourceUrl")).toString()));
  }

  timeline_document->selectClip(0);
  timeline_document->addSelectedClipEffect(QStringLiteral("Exposure"));
  timeline_document->setSelectedClipEffectAmount(QStringLiteral("Exposure"), 0.42);
  if (!timeline_document->createSequenceVersion(QStringLiteral("Comparison proof"))) {
    return fail(QStringLiteral("Program comparison could not create its immutable version reference."));
  }
  const auto comparison_versions = timeline_document->sequenceVersions();
  if (comparison_versions.isEmpty() ||
      !comparison_versions.first().toMap()
           .value(QStringLiteral("comparisonMediaAvailable")).toBool() ||
      !QFileInfo(comparison_versions.first().toMap()
                     .value(QStringLiteral("comparisonMediaPath")).toString()).isReadable()) {
    return fail(QStringLiteral(
        "Program comparison version did not retain an immutable readable reference still."));
  }

  const auto comparison_document_state_before = timeline_document->currentClip();
  const auto comparison_dirty_before = timeline_document->dirty();
  QVariant opened_program_comparison;
  if (!QMetaObject::invokeMethod(
          workspace,
          "openProgramComparison",
          Q_RETURN_ARG(QVariant, opened_program_comparison),
          Q_ARG(QVariant, QVariant())) ||
      !opened_program_comparison.toBool() ||
      !wait_for_condition(
          [workspace]() {
            auto* monitor = find_visible_named_item(
                workspace, QStringLiteral("timelineProgramMonitor"));
            auto* stage = find_visible_named_item(
                workspace, QStringLiteral("timelineProgramComparisonStage"));
            return workspace->property("programComparisonActive").toBool() &&
                   !workspace->property("programComparisonCaptureInProgress").toBool() &&
                   !workspace->property("programComparisonReferenceSource").toUrl().isEmpty() &&
                   !workspace->property("programComparisonCurrentSource").toUrl().isEmpty() &&
                   monitor != nullptr && stage != nullptr && monitor->parentItem() == stage;
          },
          5000)) {
    return fail(QStringLiteral(
        "Program comparison did not reparent the truthful monitor or capture its exact paused frame."));
  }

  auto* comparison_window = qobject_cast<QQuickWindow*>(
      find_named_object(workspace, QStringLiteral("timelineProgramComparisonWindow")));
  auto* comparison_stage = find_visible_named_item(
      workspace, QStringLiteral("timelineProgramComparisonStage"));
  truthful_program_monitor = find_visible_named_item(
      workspace, QStringLiteral("timelineProgramMonitor"));
  auto* comparison_close = find_visible_named_item(
      workspace, QStringLiteral("timelineProgramComparisonCloseButton"));
  auto* comparison_source_menu = find_visible_named_item(
      workspace, QStringLiteral("timelineProgramComparisonSourceMenu"));
  auto* comparison_blend_slider = find_visible_named_item(
      workspace, QStringLiteral("timelineProgramComparisonBlendSlider"));
  if (comparison_window == nullptr || !comparison_window->isVisible() ||
      comparison_stage == nullptr || truthful_program_monitor == nullptr ||
      truthful_program_monitor->parentItem() != comparison_stage ||
      !item_visible(comparison_close) || !item_visible(comparison_source_menu) ||
      !item_visible(comparison_blend_slider) || timeline_document->sequencePlaying() ||
      timeline_document->currentClip() != comparison_document_state_before ||
      timeline_document->dirty() != comparison_dirty_before) {
    return fail(QStringLiteral(
        "Program comparison changed document state, kept playback running, or omitted its modular controls."));
  }

  struct ComparisonModeProof final {
    QString key;
    QString object_name;
  };
  const QList<ComparisonModeProof> comparison_modes = {
      {QStringLiteral("split"), QStringLiteral("timelineProgramComparisonSplitView")},
      {QStringLiteral("wipe"), QStringLiteral("timelineProgramComparisonWipeView")},
      {QStringLiteral("overlay"), QStringLiteral("timelineProgramComparisonOverlayView")},
      {QStringLiteral("difference"), QStringLiteral("timelineProgramComparisonDifferenceView")},
  };
  for (const auto& mode : comparison_modes) {
    workspace->setProperty("programComparisonMode", mode.key);
    process_layout_events(4);
    if (!item_visible(find_visible_named_item(workspace, mode.object_name))) {
      return fail(QStringLiteral("Program comparison mode '%1' did not render its visual surface.")
                      .arg(mode.key));
    }
  }

  QVariant used_version_reference;
  if (!QMetaObject::invokeMethod(
          workspace,
          "useProgramComparisonVersion",
          Q_RETURN_ARG(QVariant, used_version_reference),
          Q_ARG(QVariant, QVariant(0))) ||
      !used_version_reference.toBool() ||
      workspace->property("programComparisonReferenceKind").toString() !=
          QStringLiteral("previous-version")) {
    return fail(QStringLiteral("Program comparison could not load the saved version still."));
  }
  QVariant used_reference_still;
  if (!QMetaObject::invokeMethod(
          workspace,
          "useProgramComparisonReferenceStill",
          Q_RETURN_ARG(QVariant, used_reference_still),
          Q_ARG(QVariant, QVariant(QUrl::fromLocalFile(source_path)))) ||
      !used_reference_still.toBool() ||
      workspace->property("programComparisonReferenceKind").toString() !=
          QStringLiteral("reference-still")) {
    return fail(QStringLiteral("Program comparison could not load a user reference still."));
  }

  workspace->setProperty("programComparisonMode", QStringLiteral("wipe"));
  process_layout_events(6);
  if (!capture_timeline_ui_state(
          comparison_window,
          comparison_window,
          output_dir,
          QStringLiteral("timeline-program-comparison-wipe"),
          1120,
          780,
          {
              QStringLiteral("timelineProgramComparisonWindowSurface"),
              QStringLiteral("timelineProgramComparisonStage"),
              QStringLiteral("timelineProgramMonitor"),
              QStringLiteral("timelineProgramComparisonOverlay"),
              QStringLiteral("timelineProgramComparisonWipeView"),
              QStringLiteral("timelineProgramComparisonReferenceStatus"),
              QStringLiteral("timelineProgramComparisonControls"),
              QStringLiteral("timelineProgramComparisonSourceMenu"),
              QStringLiteral("timelineProgramComparisonBlendSlider"),
              QStringLiteral("timelineProgramComparisonCloseButton"),
          },
          &manifest,
          QJsonObject{
              {QStringLiteral("surface"), QStringLiteral("actual-program-reparented")},
              {QStringLiteral("mode"), QStringLiteral("wipe")},
              {QStringLiteral("reference"), QStringLiteral("user-still")},
              {QStringLiteral("documentMutation"), false},
          })) {
    return fail(QStringLiteral("Timeline UI proof could not capture Program wipe comparison."));
  }

  workspace->setProperty("programComparisonMode", QStringLiteral("difference"));
  if (!wait_for_condition(
          [workspace]() {
            return !workspace->property("programComparisonDifferenceSource").toUrl().isEmpty() &&
                   workspace->property("programComparisonDifferenceData")
                       .toMap()
                       .value(QStringLiteral("success"))
                       .toBool();
          },
          3000)) {
    return fail(QStringLiteral(
        "Program comparison did not produce its bounded linear-light difference frame."));
  }
  process_layout_events(6);
  if (!capture_timeline_ui_state(
          comparison_window,
          comparison_window,
          output_dir,
          QStringLiteral("timeline-program-comparison-difference"),
          1120,
          780,
          {
              QStringLiteral("timelineProgramComparisonWindowSurface"),
              QStringLiteral("timelineProgramComparisonStage"),
              QStringLiteral("timelineProgramMonitor"),
              QStringLiteral("timelineProgramComparisonDifferenceView"),
              QStringLiteral("timelineProgramComparisonReferenceStatus"),
              QStringLiteral("timelineProgramComparisonControls"),
              QStringLiteral("timelineProgramComparisonBlendSlider"),
              QStringLiteral("timelineProgramComparisonCloseButton"),
          },
          &manifest,
          QJsonObject{
              {QStringLiteral("mode"), QStringLiteral("linear-light-difference")},
              {QStringLiteral("reference"), QStringLiteral("user-still")},
              {QStringLiteral("current"), QStringLiteral("current-program")},
              {QStringLiteral("documentMutation"), false},
          })) {
    return fail(QStringLiteral("Timeline UI proof could not capture Program difference comparison."));
  }

  QVariant began_effect_comparison;
  if (!QMetaObject::invokeMethod(
          workspace,
          "beginProgramEffectsComparison",
          Q_RETURN_ARG(QVariant, began_effect_comparison)) ||
      !began_effect_comparison.toBool() ||
      !wait_for_condition(
          [workspace]() {
            return workspace->property("programComparisonBypassEffects").toBool() &&
                   !workspace->property("programComparisonCaptureInProgress").toBool() &&
                   workspace->property("programComparisonReferenceKind").toString() ==
                       QStringLiteral("effects-after") &&
                   !workspace->property("programComparisonCurrentSource").toUrl().isEmpty();
          },
          5000) ||
      timeline_document->currentClip() != comparison_document_state_before ||
      timeline_document->dirty() != comparison_dirty_before) {
    return fail(QStringLiteral(
        "Program before/after effects changed project history or failed to isolate the selected clip preview."));
  }

  if (!QMetaObject::invokeMethod(comparison_close, "clicked") ||
      !wait_for_condition(
          [workspace, normal_program_stage]() {
            auto* monitor = find_visible_named_item(
                workspace, QStringLiteral("timelineProgramMonitor"));
            return !workspace->property("programComparisonActive").toBool() &&
                   !workspace->property("programComparisonBypassEffects").toBool() &&
                   monitor != nullptr && monitor->parentItem() == normal_program_stage;
          },
          3000) ||
      timeline_document->currentClip() != comparison_document_state_before ||
      timeline_document->dirty() != comparison_dirty_before ||
      !workspace->property("programComparisonReferenceSource").toUrl().isEmpty() ||
      !workspace->property("programComparisonCurrentSource").toUrl().isEmpty()) {
    return fail(QStringLiteral(
        "Program comparison did not restore the ordinary Program monitor or clear its session-only bypass state."));
  }

  QVariant opened_program_scopes;
  if (!QMetaObject::invokeMethod(
          workspace,
          "openProgramScopes",
          Q_RETURN_ARG(QVariant, opened_program_scopes),
          Q_ARG(QVariant, QVariant(false))) ||
      !opened_program_scopes.toBool() ||
      !wait_for_condition(
          [workspace, timeline_document]() {
            return workspace->property("programScopesVisible").toBool() &&
                   !workspace->property("programScopesDetached").toBool() &&
                   item_visible(find_visible_named_item(
                       workspace, QStringLiteral("timelineDockedProgramScopes"))) &&
                   timeline_document->programScopeData()
                       .value(QStringLiteral("success"))
                       .toBool() &&
                   timeline_document->programScopePublishedRevision() >= 0;
          },
          5000)) {
    return fail(QStringLiteral(
        "Timeline scopes did not analyze the actual bounded Program capture in their calm dock."));
  }
  const QStringList docked_scope_keys = {
      QStringLiteral("waveform"), QStringLiteral("vectorscope"),
      QStringLiteral("parade"), QStringLiteral("histogram"),
      QStringLiteral("phase")};
  const QStringList docked_scope_objects = {
      QStringLiteral("timelineScopeWaveformTile"),
      QStringLiteral("timelineScopeVectorscopeTile"),
      QStringLiteral("timelineScopeRGBParadeTile"),
      QStringLiteral("timelineScopeHistogramTile"),
      QStringLiteral("timelineScopeAudioPhaseTile")};
  for (auto index = 0; index < docked_scope_keys.size(); ++index) {
    workspace->setProperty("programScopesActiveScope", docked_scope_keys.at(index));
    process_layout_events(3);
    if (!item_visible(find_visible_named_item(workspace, docked_scope_objects.at(index)))) {
      return fail(QStringLiteral("The docked Program scope '%1' was not directly selectable.")
                      .arg(docked_scope_keys.at(index)));
    }
  }

  QVariantList proof_phase_points;
  for (auto index = 0; index < 64; ++index) {
    const auto phase = static_cast<double>(index) / 63.0 * 6.283185307179586;
    proof_phase_points.push_back(QVariantMap{
        {QStringLiteral("left"), std::sin(phase) * 0.78},
        {QStringLiteral("right"), std::sin(phase + 0.32) * 0.72},
    });
  }
  QMetaObject::invokeMethod(
      workspace,
      "publishTrackPhase",
      Q_ARG(QVariant, QVariant(QStringLiteral("runtime-proof-audio"))),
      Q_ARG(QVariant, QVariant(proof_phase_points)),
      Q_ARG(QVariant, QVariant(0.84)),
      Q_ARG(QVariant, QVariant(0.78)));

  const auto scopes_revision_before_playback =
      timeline_document->programScopePublishedRevision();
  timeline_playback->play();
  if (!wait_for_condition(
          [timeline_document]() { return timeline_document->sequencePlaying(); }, 1500) ||
      !wait_for_condition(
          [timeline_document, scopes_revision_before_playback]() {
            return timeline_document->sequencePlaying() &&
                   timeline_document->programScopePublishedRevision() >
                       scopes_revision_before_playback;
          },
          5000)) {
    return fail(QStringLiteral(
        "Live Program scopes paused transport or failed to publish a newer worker result during playback."));
  }
  timeline_playback->pause();
  if (!wait_for_condition(
          [timeline_document]() { return !timeline_document->sequencePlaying(); }, 1500)) {
    return fail(QStringLiteral("Timeline scopes transport proof could not return to a paused frame."));
  }
  const auto scopes_document_state_before_close = timeline_document->currentClip();
  const auto scopes_dirty_before_close = timeline_document->dirty();

  QVariant detached_program_scopes;
  if (!QMetaObject::invokeMethod(
          workspace,
          "toggleProgramScopesDetached",
          Q_RETURN_ARG(QVariant, detached_program_scopes)) ||
      !detached_program_scopes.toBool() ||
      !wait_for_condition(
          [workspace]() {
            auto* window = qobject_cast<QQuickWindow*>(find_named_object(
                workspace, QStringLiteral("timelineProgramScopesWindow")));
            return workspace->property("programScopesDetached").toBool() &&
                   window != nullptr && window->isVisible();
          },
          3000)) {
    return fail(QStringLiteral("Timeline scopes did not move into their focused pop-out window."));
  }
  auto* scopes_window = qobject_cast<QQuickWindow*>(find_named_object(
      workspace, QStringLiteral("timelineProgramScopesWindow")));
  auto* scopes_content = scopes_window == nullptr ? nullptr : scopes_window->contentItem();
  QMetaObject::invokeMethod(workspace, "restoreDetachedScopesGeometry");
  if (scopes_window == nullptr || scopes_content == nullptr ||
      !wait_for_condition(
          [scopes_content]() {
            return item_visible(scopes_content) &&
                   item_visible(find_visible_named_item(
                       scopes_content, QStringLiteral("timelineDetachedProgramScopes")));
          },
          3000)) {
    return fail(QStringLiteral("Timeline scopes pop-out did not expose its shared panel."));
  }
  for (const auto& object_name : docked_scope_objects) {
    if (!item_visible(find_visible_named_item(scopes_content, object_name))) {
      return fail(QStringLiteral(
          "Timeline scopes did not publish all five truthful scope surfaces in the pop-out."));
    }
  }
  if (!capture_timeline_ui_state(
          scopes_window,
          scopes_content,
          output_dir,
          QStringLiteral("timeline-program-scopes"),
          1120,
          720,
          {
              QStringLiteral("timelineDetachedProgramScopes"),
              QStringLiteral("timelineScopesStatusChip"),
              QStringLiteral("timelineScopesAllGrid"),
              QStringLiteral("timelineScopeWaveformTile"),
              QStringLiteral("timelineScopeVectorscopeTile"),
              QStringLiteral("timelineScopeRGBParadeTile"),
              QStringLiteral("timelineScopeHistogramTile"),
              QStringLiteral("timelineScopeAudioPhaseTile"),
              QStringLiteral("timelineScopesFreezeButton"),
              QStringLiteral("timelineScopesRefreshButton"),
              QStringLiteral("timelineScopesDetachButton"),
              QStringLiteral("timelineScopesCloseButton"),
          },
          &manifest,
          QJsonObject{
              {QStringLiteral("videoSource"), QStringLiteral("actual-program-capture")},
              {QStringLiteral("analysis"), QStringLiteral("bounded-worker-latest-wins")},
              {QStringLiteral("audioSource"), QStringLiteral("decoded-post-processing-stereo")},
              {QStringLiteral("transportMutation"), false},
              {QStringLiteral("documentMutation"), false},
          })) {
    return fail(QStringLiteral("Timeline UI proof could not capture the all-scope pop-out."));
  }
  auto* scopes_close = find_visible_named_item(
      scopes_window, QStringLiteral("timelineScopesCloseButton"));
  const auto scopes_close_clicked = QMetaObject::invokeMethod(scopes_close, "clicked");
  const auto scopes_closed_cleanly = wait_for_condition(
          [workspace, timeline_document]() {
            return !workspace->property("programScopesVisible").toBool() &&
                   !workspace->property("programScopesDetached").toBool() &&
                   timeline_document->programScopeData().isEmpty();
          },
          3000);
  const auto scopes_document_unchanged =
      timeline_document->currentClip() == scopes_document_state_before_close;
  const auto scopes_dirty_unchanged =
      timeline_document->dirty() == scopes_dirty_before_close;
  if (!scopes_close_clicked || !scopes_closed_cleanly || !scopes_document_unchanged ||
      !scopes_dirty_unchanged) {
    return fail(QStringLiteral(
        "Timeline scopes did not close cleanly without changing the sequence or retaining analysis state."));
  }

  const QList<QPair<int, int>> sizes = {{1180, 768}, {1440, 900}, {1920, 1080}};
  auto compact_expanded_program_width = 0.0;
  for (const auto& size : sizes) {
    root->setProperty("width", size.first);
    root->setProperty("height", size.second);
    process_layout_events(12);
    QString layout_reason;
    if (!timeline_layout_contract_holds(root, &layout_reason)) {
      return fail(QStringLiteral("Timeline UI proof failed at %1x%2: %3")
                      .arg(size.first)
                      .arg(size.second)
                      .arg(layout_reason));
    }
    const auto expected_size_class = size.first < 1280 ? QStringLiteral("compact")
                                    : size.first < 1680 ? QStringLiteral("standard")
                                                        : QStringLiteral("wide");
    if (workspace->property("workspaceSizeClass").toString() != expected_size_class) {
      return fail(QStringLiteral("Timeline UI proof did not enter the %1 responsive mode at %2 px.")
                      .arg(expected_size_class)
                      .arg(size.first));
    }
    if (size.first == 1180) {
      compact_expanded_program_width =
          item_scene_rect(find_named_item(root, QStringLiteral("timelineProgramPanel"))).width();
    }
    if (!capture_timeline_ui_state(
            root_window,
            root,
            output_dir,
            QStringLiteral("timeline-dark-expanded"),
            size.first,
            size.second,
            expanded_objects,
            &manifest,
            QJsonObject{
                {QStringLiteral("theme"), QStringLiteral("dark")},
                {QStringLiteral("layout"), QStringLiteral("source-program-inspector-multitrack")},
                {QStringLiteral("savedDocument"), proof_document},
            })) {
      return fail(QStringLiteral("Timeline UI proof could not capture an expanded layout."));
    }
  }

  const auto original_header_width = workspace->property("laneHeaderWidth").toDouble();
  if (!QMetaObject::invokeMethod(
          workspace,
          "setLaneHeaderWidth",
          Q_ARG(QVariant, QVariant(200.0))) ||
      std::abs(workspace->property("laneHeaderWidth").toDouble() - 200.0) > 0.1) {
    return fail(QStringLiteral("Timeline track-header width did not resize directly."));
  }
  if (!QMetaObject::invokeMethod(
          workspace,
          "setLaneHeaderWidth",
          Q_ARG(QVariant, QVariant(original_header_width)))) {
    return fail(QStringLiteral("Timeline track-header width did not restore after resize proof."));
  }
  const auto sequence_tracks = timeline_document->sequenceTracks();
  const auto first_track_id = sequence_tracks.isEmpty()
                                  ? QString{}
                                  : sequence_tracks.constFirst().toMap()
                                        .value(QStringLiteral("trackId"))
                                        .toString();
  QVariant resized_track_height;
  if (first_track_id.isEmpty() ||
      !QMetaObject::invokeMethod(
          workspace,
          "setTrackHeight",
          Q_ARG(QVariant, QVariant(first_track_id)),
          Q_ARG(QVariant, QVariant(96.0))) ||
      !QMetaObject::invokeMethod(
          workspace,
          "trackHeight",
          Q_RETURN_ARG(QVariant, resized_track_height),
          Q_ARG(QVariant, QVariant(first_track_id))) ||
      std::abs(resized_track_height.toDouble() - 96.0) > 0.1) {
    return fail(QStringLiteral("Timeline track height preset/direct resize did not apply."));
  }

  root->setProperty("width", 1440);
  root->setProperty("height", 900);
  if (viewport != nullptr) {
    viewport->setProperty("contentX", 0.0);
    viewport->setProperty("contentY", 0.0);
  }
  process_layout_events(10);
  auto second_video_track_row = -1;
  auto video_track_count = 0;
  for (auto row = 0; row < sequence_tracks.size(); ++row) {
    const auto track = sequence_tracks.at(row).toMap();
    if (track.value(QStringLiteral("trackKind")).toString() != QStringLiteral("video") ||
        track.value(QStringLiteral("trackRole")).toString() == QStringLiteral("caption")) {
      continue;
    }
    if (video_track_count++ == 1) {
      second_video_track_row = row;
      break;
    }
  }
  QQuickItem* second_video_drop_lane = nullptr;
  QSet<const QObject*> drop_lane_search_visited;
  const auto find_second_video_drop_lane = [&](const auto& self,
                                               QObject* candidate) -> QQuickItem* {
    if (candidate == nullptr || drop_lane_search_visited.contains(candidate)) return nullptr;
    drop_lane_search_visited.insert(candidate);
    if (candidate->objectName() == QStringLiteral("timelineMediaDropPreview")) {
      auto* preview = qobject_cast<QQuickItem*>(candidate);
      auto* lane = preview == nullptr ? nullptr : preview->parentItem();
      if (lane != nullptr &&
          lane->property("trackRowIndex").toInt() == second_video_track_row) {
        return lane;
      }
    }
    if (auto* item = qobject_cast<QQuickItem*>(candidate); item != nullptr) {
      for (auto* child_item : item->childItems()) {
        if (auto* lane = self(self, child_item); lane != nullptr) return lane;
      }
    }
    if (auto* window = qobject_cast<QQuickWindow*>(candidate); window != nullptr) {
      if (auto* lane = self(self, window->contentItem()); lane != nullptr) return lane;
    }
    for (auto* child : candidate->children()) {
      if (auto* lane = self(self, child); lane != nullptr) return lane;
    }
    return nullptr;
  };
  second_video_drop_lane = find_second_video_drop_lane(find_second_video_drop_lane, workspace);
  if (second_video_track_row < 0 || second_video_drop_lane == nullptr ||
      !item_visible(second_video_drop_lane)) {
    return fail(QStringLiteral("Timeline proof could not locate the visible second video lane (row=%1).")
                    .arg(second_video_track_row));
  }
  const auto drop_lane_x = qBound(20.0, 500.0, second_video_drop_lane->width() - 20.0);
  auto drop_scene_point = second_video_drop_lane->mapToScene(
      QPointF(drop_lane_x, second_video_drop_lane->height() * 0.5));
  auto drop_viewport_point = viewport->mapFromScene(drop_scene_point);
  if (drop_viewport_point.x() < 0.0 || drop_viewport_point.x() > viewport->width() ||
      drop_viewport_point.y() < 0.0 || drop_viewport_point.y() > viewport->height()) {
    const auto max_content_x = qMax(
        0.0, viewport->property("contentWidth").toDouble() - viewport->width());
    const auto max_content_y = qMax(
        0.0, viewport->property("contentHeight").toDouble() - viewport->height());
    viewport->setProperty(
        "contentX",
        qBound(0.0,
               viewport->property("contentX").toDouble() + drop_viewport_point.x() -
                   viewport->width() * 0.5,
               max_content_x));
    viewport->setProperty(
        "contentY",
        qBound(0.0,
               viewport->property("contentY").toDouble() + drop_viewport_point.y() -
                   viewport->height() * 0.5,
               max_content_y));
    process_layout_events(6);
    drop_lane_search_visited.clear();
    second_video_drop_lane = find_second_video_drop_lane(find_second_video_drop_lane, workspace);
    if (second_video_drop_lane == nullptr || !item_visible(second_video_drop_lane)) {
      return fail(QStringLiteral("Timeline proof could not keep its target video lane visible after scrolling."));
    }
    drop_scene_point = second_video_drop_lane->mapToScene(
        QPointF(drop_lane_x, second_video_drop_lane->height() * 0.5));
    drop_viewport_point = viewport->mapFromScene(drop_scene_point);
  }
  if (drop_viewport_point.x() < 0.0 || drop_viewport_point.x() > viewport->width() ||
      drop_viewport_point.y() < 0.0 || drop_viewport_point.y() > viewport->height()) {
    return fail(QStringLiteral("Timeline proof target lane remained outside the visible timeline viewport."));
  }
  if (!QMetaObject::invokeMethod(
          workspace,
          "updateExternalMediaDrop",
          Q_ARG(QVariant, QVariant(QVariantMap{
              {QStringLiteral("mediaType"), QStringLiteral("video")},
              {QStringLiteral("titleText"), QStringLiteral("Drop preview")},
          })),
          Q_ARG(QVariant, QVariant(drop_scene_point.x())),
          Q_ARG(QVariant, QVariant(drop_scene_point.y())))) {
    return fail(QStringLiteral("Timeline proof could not drive its external media drag path."));
  }
  process_layout_events(6);
  auto* drop_preview = find_visible_named_item(
      workspace, QStringLiteral("timelineMediaDropPreview"));
  auto* drop_label = find_visible_named_item(
      workspace, QStringLiteral("timelineMediaDropPreviewLabel"));
  if (!item_visible(drop_preview) || !item_visible(drop_label) ||
      !drop_label->property("text").toString().startsWith(QStringLiteral("Insert"))) {
    return fail(QStringLiteral(
                    "Timeline compatible lane drop did not expose insertion feedback "
                    "(preview=%1 label=%2 text=%3 state=%4).")
                    .arg(item_visible(drop_preview))
                    .arg(item_visible(drop_label))
                    .arg(drop_label == nullptr ? QStringLiteral("<missing>")
                                               : drop_label->property("text").toString())
                    .arg(QString::fromUtf8(
                        QJsonDocument::fromVariant(
                            workspace->property("externalMediaDropState"))
                            .toJson(QJsonDocument::Compact))));
  }
  if (!capture_timeline_ui_state(
          root_window,
          root,
          output_dir,
          QStringLiteral("timeline-dark-media-drop-preview"),
          1440,
          900,
          core_objects + QStringList{
              QStringLiteral("timelineMediaDropPreview"),
              QStringLiteral("timelineMediaDropPreviewLabel"),
          },
          &manifest,
          QJsonObject{
          {QStringLiteral("operation"), QStringLiteral("insert")},
          {QStringLiteral("targetTrackRow"), second_video_track_row},
          {QStringLiteral("previewX"), drop_lane_x},
      })) {
    return fail(QStringLiteral("Timeline UI proof could not capture exact lane drop feedback."));
  }
  workspace->setProperty("externalMediaDropState", QVariantMap{{QStringLiteral("visible"), false}});
  process_layout_events(3);

  const auto original_monitor_panel_height =
      workspace->property("monitorPanelHeight").toDouble();
  workspace->setProperty("sourcePanelCollapsed", true);
  workspace->setProperty("effectsPanelCollapsed", true);
  workspace->setProperty("monitorPanelHeight", 240.0);
  workspace->setProperty("timelineZoom", 48.0);
  viewport->setProperty("contentX", 0.0);
  viewport->setProperty("contentY", 0.0);
  timeline_document->selectClip(0);
  process_layout_events(10);
  if (auto* failure_indicator =
          find_named_object(root, QStringLiteral("failureActivityIndicator"));
      failure_indicator != nullptr) {
    failure_indicator->setProperty("visible", false);
  }
  if (!capture_timeline_ui_state(
          root_window,
          root,
          output_dir,
          QStringLiteral("timeline-dark-clip-feedback-selected"),
          1440,
          900,
          core_objects + QStringList{
              QStringLiteral("timelineClipContent"),
              QStringLiteral("timelineClipTitleLabel"),
              QStringLiteral("timelineClipMetadataLabel"),
              QStringLiteral("timelineClipStatusStrip"),
              QStringLiteral("timelineClipStatusBadge-effects"),
              QStringLiteral("timelineClipStatusBadge-speed"),
              QStringLiteral("timelineClipStatusBadge-group"),
              QStringLiteral("timelineClipStatusBadge-caption"),
              QStringLiteral("timelineClipStatusBadge-offline"),
              QStringLiteral("timelineClipSelectionOverlay"),
              QStringLiteral("timelineClipLeftTrimHandle"),
              QStringLiteral("timelineClipRightTrimHandle"),
              QStringLiteral("timelineClipFadeInHandle"),
              QStringLiteral("timelineClipFadeOutHandle"),
              QStringLiteral("timelineClipTransitionInBadge"),
          },
          &manifest,
          QJsonObject{
              {QStringLiteral("fixture"), QStringLiteral("thumbnails-waveform-statuses-handles")},
              {QStringLiteral("offlineMedia"), true},
              {QStringLiteral("groupedClipCount"), 2},
          })) {
    return fail(QStringLiteral(
        "Timeline UI proof could not capture populated clip readability and status feedback."));
  }

  viewport->setProperty("contentY", 120.0);
  process_layout_events(6);
  if (!wait_for_condition(
          [workspace]() {
            auto* waveform_image = find_visible_named_item(
                workspace, QStringLiteral("timelineWaveformImage-Dialogue waveform"));
            return waveform_image != nullptr &&
                   waveform_image->property("status").toInt() == 1 &&
                   !waveform_image->property("source").toString().isEmpty();
          },
          3000)) {
    return fail(QStringLiteral("Timeline UI proof waveform image did not reach Image.Ready."));
  }
  if (!capture_timeline_ui_state(
          root_window,
          root,
          output_dir,
          QStringLiteral("timeline-dark-clip-waveform"),
          1440,
          900,
          core_objects + QStringList{
              QStringLiteral("timelineClip-Dialogue waveform"),
              QStringLiteral("timelineWaveformImage-Dialogue waveform"),
              QStringLiteral("timelineClipTitleLabel"),
          },
          &manifest,
          QJsonObject{
              {QStringLiteral("fixture"), QStringLiteral("generated-pcm-waveform")},
              {QStringLiteral("waveformState"), QStringLiteral("ready")},
              {QStringLiteral("durationSeconds"), 8.0},
          })) {
    return fail(QStringLiteral("Timeline UI proof could not capture the generated audio waveform."));
  }
  viewport->setProperty("contentY", 0.0);
  process_layout_events(4);

  QVariant drag_target_top;
  if (!QMetaObject::invokeMethod(
          workspace,
          "trackTopY",
          Q_RETURN_ARG(QVariant, drag_target_top),
          Q_ARG(QVariant, QVariant(1)))) {
    return fail(QStringLiteral("Timeline clip-drag proof could not resolve its target lane."));
  }
  const auto clip_drag_state = [&workspace, &drag_target_top](
                                   const QString& operation,
                                   const QString& result) {
    workspace->setProperty(
        "dragPreviewState",
        QVariantMap{
            {QStringLiteral("visible"), true},
            {QStringLiteral("width"), 216.0},
            {QStringLiteral("x"), 360.0},
            {QStringLiteral("y"), drag_target_top.toDouble() + 6.0},
            {QStringLiteral("height"), 58.0},
            {QStringLiteral("seconds"), 7.5},
            {QStringLiteral("durationSeconds"), 4.5},
            {QStringLiteral("trackLabel"), QStringLiteral("V2")},
            {QStringLiteral("operation"), operation},
            {QStringLiteral("result"), result},
            {QStringLiteral("targetRow"), 1},
        });
    workspace->setProperty(
        "snapGuideState",
        QVariantMap{
            {QStringLiteral("visible"), true},
            {QStringLiteral("seconds"), 7.5},
            {QStringLiteral("label"), operation + QStringLiteral(" · Marker Review opening · 00:00:07:15")},
        });
  };

  timeline_document->setEditMode(QStringLiteral("overwrite"));
  workspace->setProperty("rippleModeEnabled", false);
  clip_drag_state(QStringLiteral("Overwrite"), QStringLiteral("replaces 4.50s"));
  process_layout_events(6);
  const QStringList drag_feedback_objects = {
      QStringLiteral("timelineClipDragDestinationLane"),
      QStringLiteral("timelineClipDragAffectedRange"),
      QStringLiteral("timelineClipDragPreview"),
      QStringLiteral("timelineClipDragPreviewLabel"),
      QStringLiteral("timelineClipDragInsertionPoint"),
      QStringLiteral("timelineSnapGuide"),
      QStringLiteral("timelineSnapGuideLabel"),
  };
  if (!capture_timeline_ui_state(
          root_window,
          root,
          output_dir,
          QStringLiteral("timeline-dark-clip-drag-overwrite"),
          1440,
          900,
          core_objects + drag_feedback_objects,
          &manifest,
          QJsonObject{
              {QStringLiteral("operation"), QStringLiteral("overwrite")},
              {QStringLiteral("destination"), QStringLiteral("V2")},
              {QStringLiteral("affectedSeconds"), 4.5},
          })) {
    return fail(QStringLiteral("Timeline UI proof could not capture overwrite move feedback."));
  }

  timeline_document->setEditMode(QStringLiteral("insert"));
  workspace->setProperty("rippleModeEnabled", true);
  clip_drag_state(
      QStringLiteral("Ripple move"),
      QStringLiteral("closes source gap · shifts +4.50s"));
  process_layout_events(6);
  if (!capture_timeline_ui_state(
          root_window,
          root,
          output_dir,
          QStringLiteral("timeline-dark-clip-drag-ripple"),
          1440,
          900,
          core_objects + drag_feedback_objects,
          &manifest,
          QJsonObject{
              {QStringLiteral("operation"), QStringLiteral("ripple-move")},
              {QStringLiteral("sourceGap"), QStringLiteral("closed")},
              {QStringLiteral("shiftSeconds"), 4.5},
          })) {
    return fail(QStringLiteral("Timeline UI proof could not capture ripple move feedback."));
  }
  workspace->setProperty("dragPreviewState", QVariantMap{{QStringLiteral("visible"), false}});
  workspace->setProperty("snapGuideState", QVariantMap{{QStringLiteral("visible"), false}});
  workspace->setProperty("rippleModeEnabled", false);
  workspace->setProperty("sourcePanelCollapsed", false);
  workspace->setProperty("effectsPanelCollapsed", false);
  workspace->setProperty("timelineZoom", 84.0);
  workspace->setProperty("monitorPanelHeight", original_monitor_panel_height);
  process_layout_events(6);

  const auto clip_index_for = [timeline_document](
                                  const QString& media_type,
                                  const QString& display_label = QString{},
                                  bool require_effect = false) {
    for (const auto& track_value : timeline_document->sequenceTracks()) {
      const auto track = track_value.toMap();
      for (const auto& clip_value : track.value(QStringLiteral("clips")).toList()) {
        const auto clip = clip_value.toMap();
        if (!media_type.isEmpty() &&
            clip.value(QStringLiteral("mediaType")).toString() != media_type) {
          continue;
        }
        if (!display_label.isEmpty() &&
            clip.value(QStringLiteral("displayLabel")).toString() != display_label) {
          continue;
        }
        if (require_effect && clip.value(QStringLiteral("effectStack")).toList().isEmpty()) {
          continue;
        }
        return clip.value(QStringLiteral("index")).toInt();
      }
    }
    return -1;
  };
  const auto first_video_index = clip_index_for(QStringLiteral("video"));
  const auto video_with_effect_index =
      clip_index_for(QStringLiteral("video"), QString{}, true);
  const auto effects_video_index =
      video_with_effect_index >= 0 ? video_with_effect_index : first_video_index;
  const auto audio_index =
      clip_index_for(QStringLiteral("audio"), QStringLiteral("Dialogue waveform"));
  const auto title_index = clip_index_for(QStringLiteral("title"));
  const auto caption_index = clip_index_for(QStringLiteral("caption"));
  if (first_video_index < 0 || effects_video_index < 0 || audio_index < 0 ||
      title_index < 0 || caption_index < 0) {
    return fail(QStringLiteral(
                    "Timeline Inspector proof fixture is incomplete: video=%1 effects=%2 audio=%3 title=%4 caption=%5 clips=%6.")
                    .arg(first_video_index)
                    .arg(effects_video_index)
                    .arg(audio_index)
                    .arg(title_index)
                    .arg(caption_index)
                    .arg(timeline_document->clips().size()));
  }

  workspace->setProperty("sourcePanelCollapsed", true);
  workspace->setProperty("effectsPanelCollapsed", false);
  workspace->setProperty("monitorPanelHeight", 620.0);
  root->setProperty("width", 1440);
  root->setProperty("height", 900);
  const QStringList inspector_shell_objects = {
      QStringLiteral("timelineEffectsRail"),
      QStringLiteral("timelineInspectorCollapseButton"),
  };

  timeline_document->deselectAllClips();
  process_layout_events(8);
  if (!capture_timeline_ui_state(
          root_window,
          root,
          output_dir,
          QStringLiteral("timeline-dark-inspector-empty"),
          1440,
          900,
          inspector_shell_objects + QStringList{
              QStringLiteral("timelineInspectorNoSelectionState"),
              QStringLiteral("timelineInspectorFocusTimelineButton"),
              QStringLiteral("timelineInspectorSequenceSettingsButton"),
              QStringLiteral("timelineInspectorProjectSettingsButton"),
          },
          &manifest,
          QJsonObject{{QStringLiteral("selectionType"), QStringLiteral("none")},
                      {QStringLiteral("libraryVisible"), false}})) {
    return fail(QStringLiteral("Timeline UI proof could not capture the concise empty Inspector."));
  }

  timeline_document->selectClip(effects_video_index);
  timeline_document->addSelectedClipEffect(QStringLiteral("Exposure"));
  timeline_document->addSelectedClipEffect(QStringLiteral("Blur"));
  timeline_document->addSelectedClipEffect(QStringLiteral("Punch"));
  timeline_document->setSelectedClipEffectAmount(QStringLiteral("Exposure"), 0.55);
  workspace->setProperty("inspectorTab", QStringLiteral("effects"));
  process_layout_events(8);
  auto* applied_stack = find_visible_named_item(
      workspace, QStringLiteral("timelineAppliedEffectsStack"));
  auto* effect_library = find_visible_named_item(
      workspace, QStringLiteral("timelineEffectLibraryList"));
  if (!item_visible(applied_stack) || !item_visible(effect_library) ||
      item_scene_rect(applied_stack).bottom() > item_scene_rect(effect_library).top() + 2.0) {
    const auto applied_rect = item_scene_rect(applied_stack);
    const auto library_rect = item_scene_rect(effect_library);
    return fail(QStringLiteral(
                    "Timeline applied effects and available library separation failed: visible=%1/%2 applied=%3,%4 %5x%6 library=%7,%8 %9x%10.")
                    .arg(item_visible(applied_stack))
                    .arg(item_visible(effect_library))
                    .arg(applied_rect.x())
                    .arg(applied_rect.y())
                    .arg(applied_rect.width())
                    .arg(applied_rect.height())
                    .arg(library_rect.x())
                    .arg(library_rect.y())
                    .arg(library_rect.width())
                    .arg(library_rect.height()));
  }
  if (!capture_timeline_ui_state(
          root_window,
          root,
          output_dir,
          QStringLiteral("timeline-dark-inspector-video-effects"),
          1440,
          900,
          inspector_shell_objects + QStringList{
              QStringLiteral("timelineInspectorEffectsTab"),
              QStringLiteral("timelineInspectorMotionTab"),
              QStringLiteral("timelineInspectorAudioTab"),
              QStringLiteral("timelineInspectorMoreMenu"),
              QStringLiteral("timelineEffectsInspector"),
              QStringLiteral("timelineAppliedEffectsStack"),
              QStringLiteral("timelineAppliedEffect-Exposure"),
              QStringLiteral("timelineEffectMoveDown-Exposure"),
              QStringLiteral("timelineEffectDisclosure-Exposure"),
              QStringLiteral("timelineEffectBypass-Exposure"),
              QStringLiteral("timelineEffectLibraryList"),
          },
          &manifest,
          QJsonObject{{QStringLiteral("selectionType"), QStringLiteral("video")},
                      {QStringLiteral("appliedEffects"), 3},
                      {QStringLiteral("appliedAndAvailableSeparated"), true}})) {
    return fail(QStringLiteral("Timeline UI proof could not capture the video Effects Inspector."));
  }

  workspace->setProperty("inspectorTab", QStringLiteral("motion"));
  process_layout_events(8);
  if (!capture_timeline_ui_state(
          root_window,
          root,
          output_dir,
          QStringLiteral("timeline-dark-inspector-video-motion"),
          1440,
          900,
          inspector_shell_objects + QStringList{
              QStringLiteral("timelineMotionInspector"),
              QStringLiteral("timelineCropModeControl"),
              QStringLiteral("timelineAutoReframeButton"),
          },
          &manifest,
          QJsonObject{{QStringLiteral("selectionType"), QStringLiteral("video")},
                      {QStringLiteral("tab"), QStringLiteral("motion")}})) {
    return fail(QStringLiteral("Timeline UI proof could not capture video transform and crop controls."));
  }

  QVariant ignored_result;
  QMetaObject::invokeMethod(
      workspace,
      "setInspectorContentScrollPosition",
      Q_RETURN_ARG(QVariant, ignored_result),
      Q_ARG(QVariant, QVariant(72.0)));
  QMetaObject::invokeMethod(
      workspace,
      "setInspectorEffectDisclosure",
      Q_RETURN_ARG(QVariant, ignored_result),
      Q_ARG(QVariant, QVariant(QStringLiteral("Exposure"))),
      Q_ARG(QVariant, QVariant(false)));

  timeline_document->selectClip(audio_index);
  process_layout_events(8);
  if (workspace->property("inspectorTab").toString() != QStringLiteral("audio")) {
    return fail(QStringLiteral("Audio selection did not replace an incompatible visual Inspector tab."));
  }
  if (!capture_timeline_ui_state(
          root_window,
          root,
          output_dir,
          QStringLiteral("timeline-dark-inspector-audio"),
          1440,
          900,
          inspector_shell_objects + QStringList{
              QStringLiteral("timelineAudioInspector"),
              QStringLiteral("timelineAudioPanControl"),
              QStringLiteral("timelineAudioNormalizationControls"),
              QStringLiteral("timelineMeasureLoudnessInspectorButton"),
              QStringLiteral("timelineNormalizeLoudnessInspectorButton"),
          },
          &manifest,
          QJsonObject{{QStringLiteral("selectionType"), QStringLiteral("audio")},
                      {QStringLiteral("controls"), QStringLiteral("gain-pan-fades-normalization")}})) {
    return fail(QStringLiteral("Timeline UI proof could not capture the coherent Audio Inspector."));
  }

  workspace->setProperty("inspectorTab", QStringLiteral("title"));
  timeline_document->selectClip(title_index);
  process_layout_events(8);
  if (!capture_timeline_ui_state(
          root_window,
          root,
          output_dir,
          QStringLiteral("timeline-dark-inspector-title"),
          1440,
          900,
          inspector_shell_objects + QStringList{
              QStringLiteral("timelineTitleInspector"),
              QStringLiteral("timelineTitleTemplateMenuButton"),
          },
          &manifest,
          QJsonObject{{QStringLiteral("selectionType"), QStringLiteral("title")}})) {
    return fail(QStringLiteral("Timeline UI proof could not capture the title Inspector."));
  }

  timeline_document->selectClip(caption_index);
  process_layout_events(8);
  if (workspace->property("inspectorTab").toString() != QStringLiteral("title") ||
      !capture_timeline_ui_state(
          root_window,
          root,
          output_dir,
          QStringLiteral("timeline-dark-inspector-caption"),
          1440,
          900,
          inspector_shell_objects + QStringList{QStringLiteral("timelineCaptionInspector")},
          &manifest,
          QJsonObject{{QStringLiteral("selectionType"), QStringLiteral("caption")}})) {
    return fail(QStringLiteral("Timeline UI proof could not capture the caption Inspector."));
  }

  timeline_document->selectClip(first_video_index);
  workspace->setProperty("inspectorTab", QStringLiteral("transitions"));
  process_layout_events(8);
  if (!capture_timeline_ui_state(
          root_window,
          root,
          output_dir,
          QStringLiteral("timeline-dark-inspector-transition"),
          1440,
          900,
          inspector_shell_objects + QStringList{QStringLiteral("timelineTransitionsInspector")},
          &manifest,
          QJsonObject{{QStringLiteral("selectionType"), QStringLiteral("transition")},
                      {QStringLiteral("transition"), QStringLiteral("Dip to Color")}})) {
    return fail(QStringLiteral("Timeline UI proof could not capture transition context."));
  }

  timeline_document->selectClip(first_video_index);
  timeline_document->toggleClipSelection(caption_index);
  process_layout_events(8);
  if (timeline_document->selectedClipCount() < 2 ||
      !capture_timeline_ui_state(
          root_window,
          root,
          output_dir,
          QStringLiteral("timeline-dark-inspector-multi"),
          1440,
          900,
          inspector_shell_objects + QStringList{QStringLiteral("timelineInspectorMultiSelectionState")},
          &manifest,
          QJsonObject{{QStringLiteral("selectionType"), QStringLiteral("multi")},
                      {QStringLiteral("singleClipControlsVisible"), false}})) {
    return fail(QStringLiteral("Timeline UI proof could not capture safe multi-clip Inspector actions."));
  }

  timeline_document->selectClip(effects_video_index);
  process_layout_events(8);
  workspace->setProperty("inspectorTab", QStringLiteral("motion"));
  QVariant restored_scroll;
  if (!QMetaObject::invokeMethod(
          workspace,
          "inspectorContentScrollPosition",
          Q_RETURN_ARG(QVariant, restored_scroll)) ||
      std::abs(restored_scroll.toDouble() - 72.0) > 0.5) {
    return fail(QStringLiteral("Visual Inspector scroll position did not restore by selection type and tab."));
  }
  const auto disclosure_by_type = workspace->property("inspectorDisclosureByType").toMap();
  if (disclosure_by_type.value(QStringLiteral("video")).toMap()
          .value(QStringLiteral("Exposure"), true).toBool()) {
    return fail(QStringLiteral("Applied-effect disclosure did not persist for video selections."));
  }
  workspace->setProperty("sourcePanelCollapsed", false);
  workspace->setProperty("monitorPanelHeight", original_monitor_panel_height);
  process_layout_events(6);

  auto* source_window = find_named_object(root, QStringLiteral("timelineDetachedSourceWindow"));
  auto* inspector_window = find_named_object(root, QStringLiteral("timelineDetachedInspectorWindow"));
  if (source_window == nullptr || inspector_window == nullptr) {
    return fail(QStringLiteral("Timeline UI proof could not find its detached panel windows."));
  }
  source_window->setProperty("contentRequested", true);
  inspector_window->setProperty("contentRequested", true);
  workspace->setProperty("sourcePanelDetached", true);
  workspace->setProperty("effectsPanelDetached", true);
  process_layout_events(10);
  if (!source_window->property("visible").toBool() ||
      !inspector_window->property("visible").toBool() ||
      named_item_visible(root, QStringLiteral("timelineSourceRail")) ||
      named_item_visible(root, QStringLiteral("timelineEffectsRail"))) {
    return fail(QStringLiteral("Timeline UI proof could not detach Source and Inspector into modular windows."));
  }
  workspace->setProperty("sourcePanelDetached", false);
  workspace->setProperty("effectsPanelDetached", false);
  process_layout_events(10);
  if (!named_item_visible(root, QStringLiteral("timelineSourceRail")) ||
      !named_item_visible(root, QStringLiteral("timelineEffectsRail"))) {
    return fail(QStringLiteral("Timeline UI proof did not restore detached panels to their docks."));
  }

  timeline_document->deselectAllClips();
  workspace->setProperty("effectsPanelCollapsed", true);
  if (!capture_timeline_ui_state(
          root_window,
          root,
          output_dir,
          QStringLiteral("timeline-dark-calm-default"),
          1180,
          768,
          core_objects,
          &manifest,
          QJsonObject{{QStringLiteral("state"), QStringLiteral("progressive-disclosure")}})) {
    return fail(QStringLiteral("Timeline UI proof could not capture the calm default."));
  }

  workspace->setProperty("sourcePanelCollapsed", true);
  if (!capture_timeline_ui_state(
          root_window,
          root,
          output_dir,
          QStringLiteral("timeline-dark-panels-collapsed"),
          1180,
          768,
          core_objects,
          &manifest)) {
    return fail(QStringLiteral("Timeline UI proof could not capture collapsed side panels."));
  }
  const auto collapsed_program_width =
      item_scene_rect(find_named_item(root, QStringLiteral("timelineProgramPanel"))).width();
  if (collapsed_program_width <= compact_expanded_program_width + 200.0) {
    return fail(QStringLiteral("Collapsed Timeline side panels did not return their width to Program."));
  }
  workspace->setProperty("sourcePanelCollapsed", false);
  workspace->setProperty("effectsPanelCollapsed", false);
  workspace->setProperty("timelineSearchExpanded", true);
  workspace->setProperty("timelineSearchQuery", QStringLiteral("opening"));
  workspace->setProperty("markerReviewExpanded", true);
  if (!capture_timeline_ui_state(
          root_window,
          root,
          output_dir,
          QStringLiteral("timeline-dark-search-marker-review"),
          1440,
          900,
          expanded_objects,
          &manifest)) {
    return fail(QStringLiteral("Timeline UI proof could not capture search and marker review."));
  }
  workspace->setProperty("timelineSearchExpanded", false);
  workspace->setProperty("timelineSearchQuery", QString{});
  workspace->setProperty("markerReviewExpanded", false);

  struct AspectProof final {
    QString preset;
    QString label;
    double ratio;
  };
  const QList<AspectProof> aspect_presets = {
      {QStringLiteral("youtube"), QStringLiteral("16x9"), 16.0 / 9.0},
      {QStringLiteral("youtube_shorts"), QStringLiteral("9x16"), 9.0 / 16.0},
      {QStringLiteral("square"), QStringLiteral("1x1"), 1.0},
      {QStringLiteral("portrait"), QStringLiteral("4x5"), 4.0 / 5.0},
  };
  for (const auto& preset : aspect_presets) {
    timeline_document->applyOutputPreset(preset.preset);
    root->setProperty("width", 1438);
    root->setProperty("height", 898);
    process_layout_events(8);
    auto* monitor = find_named_item(root, QStringLiteral("timelineProgramMonitor"));
    const auto actual_aspect = monitor == nullptr ? 0.0 : monitor->width() / monitor->height();
    if (!item_visible(monitor) || std::abs(actual_aspect - preset.ratio) > 0.02) {
      return fail(QStringLiteral("Timeline viewer did not fit the %1 sequence aspect ratio.").arg(preset.label));
    }
    if (!capture_timeline_ui_state(
            root_window,
            root,
            output_dir,
            QStringLiteral("timeline-dark-viewer-%1").arg(preset.label),
            1440,
            900,
            expanded_objects,
            &manifest,
            QJsonObject{
                {QStringLiteral("aspect"), preset.label},
                {QStringLiteral("measuredAspect"), actual_aspect},
            })) {
      return fail(QStringLiteral("Timeline UI proof could not capture the %1 viewer.").arg(preset.label));
    }
  }

  timeline_document->applyOutputPreset(QStringLiteral("youtube"));
  timeline_playback->seekSequenceSeconds(1.0);
  const auto before_step = timeline_document->sequencePlayheadSeconds();
  timeline_playback->stepFrames(1);
  const auto after_step = timeline_document->sequencePlayheadSeconds();
  const auto frame_duration = 1.0 / std::max(1.0, timeline_document->framesPerSecond());
  if (std::abs((after_step - before_step) - frame_duration) > 0.0001) {
    return fail(QStringLiteral("Timeline Program frame-step did not advance exactly one frame."));
  }
  timeline_playback->seekSequenceSeconds(0.0);
  if (std::abs(timeline_document->sequencePlayheadSeconds()) > 0.0001) {
    return fail(QStringLiteral("Timeline Program jump-to-start did not seek in one operation."));
  }

  workspace->setProperty("sourcePanelCollapsed", true);
  workspace->setProperty("effectsPanelCollapsed", true);
  workspace->setProperty("timelineAdvancedControlsVisible", false);
  workspace->setProperty("timelineSearchExpanded", false);
  workspace->setProperty("markerReviewExpanded", false);
  timeline_document->selectClip(0);
  if (viewport != nullptr) {
    viewport->setProperty("contentX", 0.0);
    viewport->setProperty("contentY", 0.0);
  }
  process_layout_events(8);

  const QStringList accessibility_scale_objects = core_objects + QStringList{
      QStringLiteral("timelineHeaderBar"),
      QStringLiteral("timelineCompactTitleControl"),
      QStringLiteral("timelineProjectSummaryButton"),
      QStringLiteral("timelineHistoryControlGroup"),
      QStringLiteral("timelinePrimaryAddMediaButton"),
      QStringLiteral("timelineViewMenuButton"),
      QStringLiteral("timelineSaveButton"),
      QStringLiteral("timelineSaveStateChip"),
      QStringLiteral("timelineHeaderMoreMenuButton"),
      QStringLiteral("timelineClipContent"),
  };
  struct InterfaceScaleProof final {
    double scale;
    QString label;
  };
  const QList<InterfaceScaleProof> scale_proofs = {
      {1.0, QStringLiteral("100")},
      {1.25, QStringLiteral("125")},
      {1.5, QStringLiteral("150")},
      {2.0, QStringLiteral("200")},
  };
  for (const auto& proof : scale_proofs) {
    app_shell->setInterfaceScale(proof.scale);
    root->setProperty("width", 1920);
    root->setProperty("height", 1080);
    process_layout_events(14);
    QString header_reason;
    if (!horizontal_control_group_fits(
            workspace,
            QStringLiteral("timelineHeaderBar"),
            header_control_objects,
            &header_reason)) {
      return fail(QStringLiteral("Timeline header failed at %1% interface scale: %2")
                      .arg(proof.label, header_reason));
    }
    if (!capture_timeline_ui_state(
            root_window,
            root,
            output_dir,
            QStringLiteral("timeline-dark-scale-%1").arg(proof.label),
            1920,
            1080,
            accessibility_scale_objects,
            &manifest,
            QJsonObject{{QStringLiteral("interfaceScale"), proof.scale},
                        {QStringLiteral("panels"), QStringLiteral("collapsed")},
                        {QStringLiteral("clipState"), QStringLiteral("populated-readable")}})) {
      return fail(QStringLiteral("Timeline UI proof could not capture %1% interface scale.")
                      .arg(proof.label));
    }
  }

  app_shell->setInterfaceScale(1.0);
  root->setProperty("width", 1440);
  root->setProperty("height", 900);
  process_layout_events(10);
  auto* keyboard_viewport = find_visible_named_item(
      workspace, QStringLiteral("timelineViewport"));
  if (keyboard_viewport == nullptr) {
    return fail(QStringLiteral("Timeline accessibility proof could not find the keyboard viewport."));
  }
  root_window->requestActivate();
  keyboard_viewport->setFocus(true, Qt::TabFocusReason);
  keyboard_viewport->forceActiveFocus(Qt::TabFocusReason);
  process_layout_events(3);
  if (!keyboard_viewport->hasFocus() && !keyboard_viewport->hasActiveFocus()) {
    return fail(QStringLiteral("Timeline viewport did not expose a visible keyboard focus target."));
  }
  timeline_playback->seekSequenceSeconds(1.0);
  const auto before_keyboard_step = timeline_document->sequencePlayheadSeconds();
  QKeyEvent right_press(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
  QKeyEvent right_release(QEvent::KeyRelease, Qt::Key_Right, Qt::NoModifier);
  QCoreApplication::sendEvent(keyboard_viewport, &right_press);
  QCoreApplication::sendEvent(keyboard_viewport, &right_release);
  process_layout_events(3);
  if (timeline_document->sequencePlayheadSeconds() <= before_keyboard_step) {
    return fail(QStringLiteral("Timeline keyboard focus did not route Right Arrow to frame stepping."));
  }
  if (!capture_timeline_ui_state(
          root_window,
          root,
          output_dir,
          QStringLiteral("timeline-dark-accessibility-focus"),
          1440,
          900,
          core_objects + QStringList{
              QStringLiteral("timelineViewport"),
              QStringLiteral("timelineViewportFocusRing"),
              QStringLiteral("timelineClipContent"),
              QStringLiteral("timelinePlayheadLine"),
          },
          &manifest,
          QJsonObject{{QStringLiteral("focus"), QStringLiteral("timeline-viewport")},
                      {QStringLiteral("keyboardFrameStep"), true},
                      {QStringLiteral("stateEncoding"), QStringLiteral("color-plus-shape-plus-focus")}})) {
    return fail(QStringLiteral("Timeline UI proof could not capture accessible focus and state encoding."));
  }

  const QList<QPair<int, int>> release_fixture_sizes = {
      {1180, 768}, {1440, 900}, {1920, 1080}};
  const auto reset_release_viewport = [viewport]() {
    if (viewport != nullptr) {
      viewport->setFocus(false, Qt::OtherFocusReason);
      viewport->setProperty("contentX", 0.0);
      viewport->setProperty("contentY", 0.0);
    }
  };
  const auto capture_release_fixture = [&, app_shell](
                                            const QString& fixture_name,
                                            const QStringList& required_objects,
                                            const QJsonObject& fixture_metadata = {}) {
    app_shell->setInterfaceScale(1.0);
    reset_release_viewport();
    for (const auto& size : release_fixture_sizes) {
      auto metadata = fixture_metadata;
      metadata.insert(QStringLiteral("fixture"), fixture_name);
      metadata.insert(QStringLiteral("interfaceScale"), 1.0);
      if (!capture_timeline_ui_state(
              root_window,
              root,
              output_dir,
              QStringLiteral("timeline-release-%1").arg(fixture_name),
              size.first,
              size.second,
              required_objects,
              &manifest,
              metadata)) {
        return false;
      }
    }
    app_shell->setInterfaceScale(2.0);
    reset_release_viewport();
    auto scale_metadata = fixture_metadata;
    scale_metadata.insert(QStringLiteral("fixture"), fixture_name);
    scale_metadata.insert(QStringLiteral("interfaceScale"), 2.0);
    const auto captured = capture_timeline_ui_state(
        root_window,
        root,
        output_dir,
        QStringLiteral("timeline-release-%1-scale-200").arg(fixture_name),
        1920,
        1080,
        required_objects,
        &manifest,
        scale_metadata);
    app_shell->setInterfaceScale(1.0);
    process_layout_events(6);
    return captured;
  };

  timeline_document->createTimeline(QStringLiteral("Release fixture empty"));
  workspace->setProperty("sourcePanelCollapsed", true);
  workspace->setProperty("effectsPanelCollapsed", true);
  process_layout_events(8);
  if (!capture_release_fixture(
          QStringLiteral("empty"),
          core_objects + QStringList{QStringLiteral("timelineProgramEmptyState")},
          QJsonObject{{QStringLiteral("document"), QStringLiteral("empty")}})) {
    return fail(QStringLiteral("Timeline release matrix could not capture the empty fixture."));
  }

  timeline_document->createTimeline(QStringLiteral("Release fixture one clip"));
  timeline_document->addClipFromGallery(
      QStringLiteral("release:one-clip"),
      source_path,
      QStringLiteral("One clip"),
      6.0,
      QUrl::fromLocalFile(source_path).toString());
  timeline_document->selectClip(0);
  timeline_document->applyOutputPreset(QStringLiteral("youtube_shorts"));
  process_layout_events(10);
  if (!capture_release_fixture(
          QStringLiteral("one-clip-portrait"),
          core_objects + QStringList{QStringLiteral("timelineClipContent")},
          QJsonObject{{QStringLiteral("aspect"), QStringLiteral("9x16")},
                      {QStringLiteral("clipCount"), 1}})) {
    return fail(QStringLiteral("Timeline release matrix could not capture one-clip portrait."));
  }
  timeline_document->applyOutputPreset(QStringLiteral("youtube"));
  process_layout_events(8);
  if (!capture_release_fixture(
          QStringLiteral("one-clip-landscape"),
          core_objects + QStringList{QStringLiteral("timelineClipContent")},
          QJsonObject{{QStringLiteral("aspect"), QStringLiteral("16x9")},
                      {QStringLiteral("clipCount"), 1}})) {
    return fail(QStringLiteral("Timeline release matrix could not capture one-clip landscape."));
  }

  if (!timeline_document->loadTimeline(saved_path)) {
    return fail(QStringLiteral("Timeline release matrix could not restore the multitrack fixture."));
  }
  timeline_document->applyOutputPreset(QStringLiteral("portrait"));
  timeline_document->selectClip(0);
  workspace->setProperty("effectsPanelCollapsed", true);
  process_layout_events(10);
  if (!capture_release_fixture(
          QStringLiteral("multitrack-social"),
          core_objects + QStringList{QStringLiteral("timelineClipContent")},
          QJsonObject{{QStringLiteral("aspect"), QStringLiteral("4x5")},
                      {QStringLiteral("tracks"), timeline_document->sequenceTracks().size()}})) {
    return fail(QStringLiteral("Timeline release matrix could not capture multitrack social."));
  }

  timeline_document->createTimeline(QStringLiteral("Release fixture long form"));
  timeline_document->addClipFromGallery(
      QStringLiteral("release:long-form"),
      source_path,
      QStringLiteral("Thirty minute program"),
      1800.0,
      QUrl::fromLocalFile(source_path).toString());
  timeline_document->selectClip(0);
  timeline_document->applyOutputPreset(QStringLiteral("youtube"));
  workspace->setProperty("timelineZoom", 12.0);
  process_layout_events(10);
  if (!capture_release_fixture(
          QStringLiteral("long-form"),
          core_objects + QStringList{QStringLiteral("timelineClipContent")},
          QJsonObject{{QStringLiteral("durationSeconds"), 1800.0},
                      {QStringLiteral("virtualized"), true}})) {
    return fail(QStringLiteral("Timeline release matrix could not capture long-form."));
  }

  if (!timeline_document->loadTimeline(saved_path)) {
    return fail(QStringLiteral("Timeline release matrix could not restore missing-media state."));
  }
  workspace->setProperty("timelineZoom", 48.0);
  const auto missing_media_index =
      clip_index_for(QString{}, QStringLiteral("Missing interview media"));
  timeline_document->selectClip(missing_media_index);
  workspace->setProperty("effectsPanelCollapsed", true);
  process_layout_events(10);
  if (missing_media_index < 0 ||
      !capture_release_fixture(
          QStringLiteral("missing-media"),
          core_objects + QStringList{QStringLiteral("timelineClipStatusBadge-offline")},
          QJsonObject{{QStringLiteral("offline"), true}})) {
    return fail(QStringLiteral("Timeline release matrix could not capture missing media."));
  }
  if (!QFile::copy(source_path, offline_fixture_path)) {
    return fail(QStringLiteral("Timeline release matrix could not restore the offline fixture after capture."));
  }

  const auto release_effects_index = clip_index_for(QStringLiteral("video"));
  timeline_document->selectClip(release_effects_index);
  timeline_document->addSelectedClipEffect(QStringLiteral("Exposure"));
  timeline_document->addSelectedClipEffect(QStringLiteral("Blur"));
  timeline_document->addSelectedClipEffect(QStringLiteral("Punch"));
  timeline_document->addSelectedClipEffect(QStringLiteral("Vignette"));
  workspace->setProperty("effectsPanelCollapsed", false);
  workspace->setProperty("inspectorTab", QStringLiteral("effects"));
  process_layout_events(10);
  if (release_effects_index < 0 ||
      !capture_release_fixture(
          QStringLiteral("effects-heavy"),
          core_objects + QStringList{
              QStringLiteral("timelineEffectsRail"),
              QStringLiteral("timelineAppliedEffectsStack")},
          QJsonObject{{QStringLiteral("effectCount"), 4}})) {
    return fail(QStringLiteral("Timeline release matrix could not capture effects-heavy state."));
  }

  if (!timeline_document->loadTimeline(saved_path)) {
    return fail(QStringLiteral("Timeline release matrix could not restore caption state."));
  }
  const auto release_caption_index = clip_index_for(QStringLiteral("caption"));
  timeline_document->selectClip(release_caption_index);
  workspace->setProperty("effectsPanelCollapsed", false);
  workspace->setProperty("inspectorTab", QStringLiteral("title"));
  process_layout_events(10);
  if (release_caption_index < 0 ||
      !capture_release_fixture(
          QStringLiteral("caption"),
          core_objects + QStringList{
              QStringLiteral("timelineEffectsRail"),
              QStringLiteral("timelineCaptionInspector")},
          QJsonObject{{QStringLiteral("selection"), QStringLiteral("caption")}})) {
    return fail(QStringLiteral("Timeline release matrix could not capture caption state."));
  }

  if (!timeline_document->loadTimeline(saved_path)) {
    return fail(QStringLiteral("Timeline release matrix could not restore audio-mix state."));
  }
  const auto release_audio_index =
      clip_index_for(QStringLiteral("audio"), QStringLiteral("Dialogue waveform"));
  timeline_document->selectClip(release_audio_index);
  workspace->setProperty("effectsPanelCollapsed", false);
  workspace->setProperty("inspectorTab", QStringLiteral("audio"));
  process_layout_events(10);
  if (release_audio_index < 0 ||
      !capture_release_fixture(
          QStringLiteral("audio-mix"),
          core_objects + QStringList{
              QStringLiteral("timelineEffectsRail"),
              QStringLiteral("timelineAudioInspector"),
              QStringLiteral("timelineAudioPanControl")},
          QJsonObject{{QStringLiteral("selection"), QStringLiteral("audio")}})) {
    return fail(QStringLiteral("Timeline release matrix could not capture audio mix state."));
  }

  if (!timeline_document->loadTimeline(saved_path)) {
    return fail(QStringLiteral("Timeline acceptance proof could not restore its saved edit."));
  }
  workspace->setProperty("sourcePanelCollapsed", true);
  workspace->setProperty("effectsPanelCollapsed", true);
  workspace->setProperty("timelineZoom", 48.0);
  timeline_document->selectClip(0);
  reset_release_viewport();
  root->setProperty("width", 1440);
  root->setProperty("height", 900);
  process_layout_events(10);

  QJsonObject interaction_latency_ms;
  const auto measure_interaction = [&](const QString& name,
                                       const std::function<void()>& submit,
                                       const std::function<bool()>& visible) {
    QElapsedTimer intent_timer;
    intent_timer.start();
    submit();
    const auto intent_ms = static_cast<double>(intent_timer.nsecsElapsed()) / 1000000.0;
    QElapsedTimer feedback_timer;
    feedback_timer.start();
    const auto became_visible = wait_for_condition(visible, 100);
    const auto feedback_ms = static_cast<double>(feedback_timer.nsecsElapsed()) / 1000000.0;
    interaction_latency_ms.insert(
        name,
        QJsonObject{{QStringLiteral("intentMs"), intent_ms},
                    {QStringLiteral("visibleFeedbackMs"), feedback_ms}});
    if (intent_ms > 16.7 || !became_visible || feedback_ms > 100.0) {
      std::cerr << "Timeline interaction budget failed: " << name.toStdString()
                << " intent=" << intent_ms << "ms feedback=" << feedback_ms
                << "ms visible=" << became_visible << '\n';
    }
    return intent_ms <= 16.7 && became_visible && feedback_ms <= 100.0;
  };

  auto* acceptance_playhead = find_visible_named_item(
      workspace, QStringLiteral("timelinePlayheadLine"));
  const auto scrub_x_before = item_scene_rect(acceptance_playhead).x();
  if (!measure_interaction(
          QStringLiteral("pointer-scrub"),
          [workspace]() {
            QMetaObject::invokeMethod(
                workspace, "requestTimelineSeek", Q_ARG(QVariant, 1.5));
          },
          [acceptance_playhead, scrub_x_before]() {
            return item_visible(acceptance_playhead) &&
                   std::abs(item_scene_rect(acceptance_playhead).x() - scrub_x_before) > 1.0;
          })) {
    return fail(QStringLiteral("Timeline pointer scrub missed its interaction or visible-feedback budget."));
  }
  const auto drag_x_before = item_scene_rect(acceptance_playhead).x();
  if (!measure_interaction(
          QStringLiteral("playhead-drag"),
          [workspace]() {
            QMetaObject::invokeMethod(
                workspace, "requestTimelineSeek", Q_ARG(QVariant, 2.0));
          },
          [acceptance_playhead, drag_x_before]() {
            return item_visible(acceptance_playhead) &&
                   std::abs(item_scene_rect(acceptance_playhead).x() - drag_x_before) > 1.0;
          })) {
    return fail(QStringLiteral("Timeline playhead drag missed its interaction or visible-feedback budget."));
  }

  const auto scrub_preview_probe =
      measure_timeline_continuous_scrub_preview(workspace, timeline_playback);

  auto* acceptance_clip = find_visible_named_item(workspace, QStringLiteral("timelineClipContent"));
  const auto clip_width_before_trim = acceptance_clip == nullptr ? 0.0 : acceptance_clip->width();
  const auto acceptance_clip_state = timeline_document->currentClip();
  const auto trim_target_seconds =
      acceptance_clip_state.value(QStringLiteral("startSeconds")).toDouble() +
      std::max(0.5,
               acceptance_clip_state.value(QStringLiteral("trimmedDurationSeconds")).toDouble() - 0.3);
  if (!measure_interaction(
          QStringLiteral("trim"),
          [workspace, trim_target_seconds]() {
            QMetaObject::invokeMethod(
                workspace,
                "requestSelectedTrimEndToSequenceSeconds",
                Q_ARG(QVariant, trim_target_seconds));
          },
          [acceptance_clip, clip_width_before_trim]() {
            return item_visible(acceptance_clip) &&
                   std::abs(acceptance_clip->width() - clip_width_before_trim) > 1.0;
          })) {
    return fail(QStringLiteral("Timeline trim missed its interaction or visible-feedback budget."));
  }
  if (!wait_for_condition(
          [workspace]() {
            return workspace->property("interactionTrimEndSequenceSeconds").toDouble() < 0.0;
          },
          2000)) {
    return fail(QStringLiteral("Timeline trim preview did not settle into its document commit."));
  }

  auto* acceptance_fade_handle = find_visible_named_item(
      workspace, QStringLiteral("timelineSelectedClipFadeInHandle"));
  const auto fade_width_before =
      acceptance_fade_handle == nullptr ? 0.0 : acceptance_fade_handle->width();
  if (!measure_interaction(
          QStringLiteral("fade"),
          [workspace]() {
            QMetaObject::invokeMethod(
                workspace,
                "requestSelectedFadeInSeconds",
                Q_ARG(QVariant, 0.50));
          },
          [acceptance_fade_handle, fade_width_before]() {
            return item_visible(acceptance_fade_handle) &&
                   std::abs(acceptance_fade_handle->width() - fade_width_before) > 1.0;
          })) {
    return fail(QStringLiteral("Timeline fade missed its interaction or visible-feedback budget."));
  }
  if (!wait_for_condition(
          [workspace]() {
            return workspace->property("interactionFadeInSeconds").toDouble() < 0.0;
          },
          2000)) {
    return fail(QStringLiteral("Timeline fade preview did not settle into its document commit."));
  }

  auto* acceptance_monitor_panel = find_visible_named_item(
      workspace, QStringLiteral("timelineMainSplit"));
  const auto monitor_panel_height_before =
      item_scene_rect(acceptance_monitor_panel).height();
  auto* vertical_splitter_handle = find_visible_named_item(
      workspace, QStringLiteral("timelineVerticalSplitterHandle"));
  const auto splitter_center = vertical_splitter_handle == nullptr
                                   ? QPointF()
                                   : vertical_splitter_handle->mapToScene(QPointF(
                                         vertical_splitter_handle->width() * 0.5,
                                         vertical_splitter_handle->height() * 0.5));
  const auto splitter_drag_end = splitter_center + QPointF(0.0, 24.0);
  if (!measure_interaction(
          QStringLiteral("splitter-resize"),
          [root_window, vertical_splitter_handle, splitter_center, splitter_drag_end]() {
            if (root_window == nullptr || vertical_splitter_handle == nullptr)
              return;
            const auto send_pointer = [root_window](
                                          QEvent::Type type,
                                          const QPointF& position,
                                          Qt::MouseButton button,
                                          Qt::MouseButtons buttons) {
              QMouseEvent event(
                  type,
                  position,
                  root_window->mapToGlobal(position.toPoint()),
                  button,
                  buttons,
                  Qt::NoModifier);
              QCoreApplication::sendEvent(root_window, &event);
            };
            send_pointer(
                QEvent::MouseButtonPress,
                splitter_center,
                Qt::LeftButton,
                Qt::LeftButton);
            send_pointer(
                QEvent::MouseMove,
                splitter_drag_end,
                Qt::NoButton,
                Qt::LeftButton);
            send_pointer(
                QEvent::MouseButtonRelease,
                splitter_drag_end,
                Qt::LeftButton,
                Qt::NoButton);
          },
          [acceptance_monitor_panel,
           vertical_splitter_handle,
           monitor_panel_height_before]() {
            return item_visible(acceptance_monitor_panel) &&
                   item_visible(vertical_splitter_handle) &&
                   std::abs(item_scene_rect(acceptance_monitor_panel).height() -
                            monitor_panel_height_before) > 1.0;
          })) {
    return fail(QStringLiteral("Timeline splitter resize missed its interaction or visible-feedback budget (panel=%1 requested=%2 preference=%3).")
                    .arg(monitor_panel_height_before)
                    .arg(splitter_drag_end.y() - splitter_center.y())
                    .arg(workspace->property("monitorPanelHeight").toDouble()));
  }

  auto* acceptance_clip_for_zoom = find_visible_named_item(
      workspace, QStringLiteral("timelineClipContent"));
  const auto clip_width_before_zoom =
      acceptance_clip_for_zoom == nullptr ? 0.0 : acceptance_clip_for_zoom->width();
  if (!measure_interaction(
          QStringLiteral("zoom"),
          [workspace]() { workspace->setProperty("timelineZoom", 64.0); },
          [acceptance_clip_for_zoom, clip_width_before_zoom]() {
            return item_visible(acceptance_clip_for_zoom) &&
                   std::abs(acceptance_clip_for_zoom->width() - clip_width_before_zoom) > 1.0;
          })) {
    return fail(QStringLiteral("Timeline zoom missed its interaction or visible-feedback budget."));
  }

  const QVariantMap acceptance_drag_preview{
      {QStringLiteral("visible"), true},
      {QStringLiteral("width"), 180.0},
      {QStringLiteral("x"), 220.0},
      {QStringLiteral("y"), 32.0},
      {QStringLiteral("height"), 48.0},
      {QStringLiteral("seconds"), 3.5},
      {QStringLiteral("durationSeconds"), 2.0},
      {QStringLiteral("trackLabel"), QStringLiteral("V1")},
      {QStringLiteral("operation"), QStringLiteral("Ripple move")},
      {QStringLiteral("result"), QStringLiteral("Close source gap" )},
      {QStringLiteral("targetRow"), 0},
  };
  if (!measure_interaction(
          QStringLiteral("clip-drag"),
          [workspace, acceptance_drag_preview]() {
            workspace->setProperty("dragPreviewState", acceptance_drag_preview);
          },
          [workspace]() {
            return named_item_visible(workspace, QStringLiteral("timelineClipDragPreview"));
          })) {
    return fail(QStringLiteral("Timeline clip drag missed its interaction or visible-feedback budget."));
  }

  if (!capture_timeline_ui_state(
          root_window,
          root,
          output_dir,
          QStringLiteral("timeline-release-acceptance-feedback"),
          1440,
          900,
          core_objects + QStringList{
              QStringLiteral("timelineClipDragPreview"),
              QStringLiteral("timelineClipDragInsertionPoint"),
              QStringLiteral("timelinePlayheadLine")},
          &manifest,
          QJsonObject{{QStringLiteral("interactionLatencyMs"), interaction_latency_ms},
                      {QStringLiteral("intentBudgetMs"), 16.7},
                      {QStringLiteral("visibleFeedbackBudgetMs"), 100.0}})) {
    return fail(QStringLiteral("Timeline acceptance proof could not capture immediate interaction feedback."));
  }
  workspace->setProperty("dragPreviewState", QVariantMap{{QStringLiteral("visible"), false}});
  process_layout_events(4);

  QFile::remove(offline_fixture_path);
  if (!QFile::copy(source_path, offline_fixture_path)) {
    return fail(QStringLiteral("Timeline acceptance proof could not relink its missing-media fixture."));
  }
  QString final_save_error;
  if (!save_timeline_and_wait(timeline_document, "timeline-acceptance-save", &final_save_error)) {
    return fail(QStringLiteral("Timeline acceptance proof could not save its final edit: %1")
                    .arg(final_save_error));
  }
  const auto acceptance_export_path =
      QDir(output_dir).filePath(QStringLiteral("timeline-release-acceptance.mp4"));
  QFile::remove(acceptance_export_path);
  QElapsedTimer export_timer;
  export_timer.start();
  if (!timeline_document->renderCurrentTimeline(acceptance_export_path) ||
      QFileInfo(acceptance_export_path).size() <= 1024) {
    return fail(QStringLiteral(
                    "Timeline acceptance proof could not render a real final export: %1 (bytes=%2).")
                    .arg(timeline_document->status())
                    .arg(QFileInfo(acceptance_export_path).size()));
  }

  const QJsonObject acceptance_report{
      {QStringLiteral("contract"), QStringLiteral("timeline-release-acceptance:v1")},
      {QStringLiteral("operations"), QJsonArray{
           QStringLiteral("import"), QStringLiteral("source-mark"), QStringLiteral("insert"),
           QStringLiteral("trim"), QStringLiteral("ripple"), QStringLiteral("layer-audio"),
           QStringLiteral("effect"), QStringLiteral("caption"), QStringLiteral("resize-panels"),
           QStringLiteral("collapse-panels"), QStringLiteral("save"), QStringLiteral("reopen"),
           QStringLiteral("export")}},
      {QStringLiteral("interactionLatencyMs"), interaction_latency_ms},
      {QStringLiteral("continuousScrubPreview"), scrub_preview_probe},
      {QStringLiteral("exportPath"), acceptance_export_path},
      {QStringLiteral("exportBytes"), QFileInfo(acceptance_export_path).size()},
      {QStringLiteral("exportElapsedMs"), export_timer.elapsed()},
      {QStringLiteral("previewExportParityGate"),
       QStringLiteral("timeline_document_controller_test:center-pixel-codec-tolerance<=4")},
      {QStringLiteral("fixtureMatrix"),
       QJsonArray{QStringLiteral("empty"), QStringLiteral("one-clip-portrait"),
                  QStringLiteral("one-clip-landscape"), QStringLiteral("multitrack-social"),
                  QStringLiteral("long-form"), QStringLiteral("missing-media"),
                  QStringLiteral("effects-heavy"), QStringLiteral("caption"),
                  QStringLiteral("audio-mix")}},
  };
  QFile acceptance_report_file(
      QDir(output_dir).filePath(QStringLiteral("timeline-release-acceptance.json")));
  if (!acceptance_report_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
      acceptance_report_file.write(
          QJsonDocument(acceptance_report).toJson(QJsonDocument::Indented)) <= 0) {
    return fail(QStringLiteral("Timeline acceptance proof could not write its release report."));
  }

  QFile manifest_file(QDir(output_dir).filePath(QStringLiteral("timeline-ui-runtime-proof-manifest.json")));
  if (!manifest_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
      manifest_file.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented)) <= 0) {
    return fail(QStringLiteral("Timeline UI proof could not write its evidence manifest."));
  }
  std::cout << "Timeline UI runtime proof passed with " << manifest.size()
            << " captures in " << output_dir.toStdString() << '\n';
  return EXIT_SUCCESS;
}

}  // namespace

#include "photo_recipe_clipboard_proof.h"

int main(int argc, char** argv) {
  Q_INIT_RESOURCE(looksy_agent_sprite);
  Q_INIT_RESOURCE(looksy_brand_assets);
  Q_INIT_RESOURCE(shell_visual_test_shaders);
  if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
    qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
  }

  QApplication app(argc, argv);
  QQuickStyle::setStyle(QStringLiteral("Basic"));
  QApplication::setFont(QFont(QStringLiteral("Helvetica Neue")));
  QCoreApplication::setOrganizationName(QStringLiteral("PhotoEditorBenchmarks"));
  QCoreApplication::setApplicationName(QStringLiteral("ActualAppWorkflowBenchmark"));

  const auto arguments = app.arguments();
  const auto optical_proof_dir = option_value(arguments, QStringLiteral("--optical-ui-proof-dir"));
  const bool optical_real_export = !optical_proof_dir.isEmpty() && arguments.contains("--optical-real-export");
  const bool optical_real_subject_selection =
      !optical_proof_dir.isEmpty() && qEnvironmentVariableIsSet("PHOTO_EDITOR_OPTICAL_REAL_SUBJECT");
  if (!optical_proof_dir.isEmpty()) qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
  const auto recipe_clipboard_proof_dir = option_value(arguments, QStringLiteral("--recipe-clipboard-proof-dir"));
  if (!recipe_clipboard_proof_dir.isEmpty()) qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
  const auto color_interaction_proof = arguments.contains(QStringLiteral("--color-interaction-proof"));
  const auto visual_search_proof_dir = option_value(arguments, QStringLiteral("--visual-search-proof-dir"));
  const auto similarity_proof_dir = option_value(arguments, QStringLiteral("--similarity-search-proof-dir"));
  const auto iterations = std::max(1, option_int(arguments, QStringLiteral("--iterations"), 5));
  const auto timeout_ms =
      std::max(1000, option_int(arguments, QStringLiteral("--timeout-ms"), 15000));
  const bool brush_only = arguments.contains(QStringLiteral("--brush-only"));
  const bool brush_auto_mask = arguments.contains(QStringLiteral("--brush-auto-mask"));
  const bool route_only = arguments.contains(QStringLiteral("--route-only"));
  const bool route_only_no_render =
      arguments.contains(QStringLiteral("--route-only-no-render"));
  const bool route_only_async = arguments.contains(QStringLiteral("--route-only-async"));
  const bool route_only_active_editor =
      arguments.contains(QStringLiteral("--route-only-active-editor"));
  if ((route_only_no_render || route_only_async) && !route_only) {
    return fail(QStringLiteral("Route-only diagnostic options require --route-only."));
  }
  const auto capture_dir = option_value(arguments, QStringLiteral("--capture-dir"));
  const auto viewer_film_proof_dir = option_value(arguments, QStringLiteral("--viewer-film-proof-dir"));
  const auto capture_setup_proof_dir = option_value(arguments, QStringLiteral("--capture-setup-proof-dir"));
  const bool gaussian_proof = !viewer_film_proof_dir.isEmpty() || !capture_setup_proof_dir.isEmpty();
  const auto timeline_ui_proof_dir =
      option_value(arguments, QStringLiteral("--timeline-ui-proof-dir"));
  if (!timeline_ui_proof_dir.isEmpty()) {
    QCoreApplication::setApplicationName(QStringLiteral("TimelineUIRuntimeProof"));
  }
  auto source_path = option_value(arguments, QStringLiteral("--source"));
  const auto search_edit_proof_dir = option_value(arguments, QStringLiteral("--search-edit-proof-dir"));
  const bool live_visual_search = !visual_search_proof_dir.isEmpty() &&
      !option_value(arguments, QStringLiteral("--visual-search-model")).isEmpty();

  QTemporaryDir runtime_root;
  QTemporaryDir cache_root;
  QTemporaryDir fixture_dir;
  if (!runtime_root.isValid() || !cache_root.isValid() || !fixture_dir.isValid()) {
    return fail(QStringLiteral("Unable to create actual-app workflow temporary directories."));
  }
  const bool native_readback_proof = arguments.contains("--native-readback-proof");
  const bool library_proof = arguments.contains("--library-grid-proof") ||
      arguments.contains("--asset-selection-proof") ||
      arguments.contains("--cutout-adjustment-proof") ||
      arguments.contains("--layer-drop-indicator-proof") ||
      arguments.contains("--layer-move-proof") ||
      arguments.contains("--direct-cutout-color-proof") ||
      arguments.contains("--brush-fidelity-proof") ||
      arguments.contains("--library-duration-proof") ||
      arguments.contains("--library-membership-proof") ||
      arguments.contains("--library-startup-proof");
  if (!option_value(arguments, "--workspace-switch-proof-dir").isEmpty() ||
      gaussian_proof || color_interaction_proof || brush_only ||
      !recipe_clipboard_proof_dir.isEmpty() || !optical_proof_dir.isEmpty() ||
      !timeline_ui_proof_dir.isEmpty() || library_proof || native_readback_proof) {
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, runtime_root.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, runtime_root.path());
  }
  // Exercise macOS's production preferences backend without touching the user's
  // preferences. INI remains the default for all existing isolated proofs.
  const bool native_settings_fixture = brush_only &&
      qEnvironmentVariableIsSet("PHOTO_EDITOR_BENCHMARK_NATIVE_SETTINGS");
  if (native_settings_fixture) {
    QCoreApplication::setOrganizationName(QStringLiteral("LooksyPerformanceFixtures"));
    QCoreApplication::setApplicationName(
        QStringLiteral("ToolPreferences-") + QDir(runtime_root.path()).dirName());
    QSettings::setDefaultFormat(QSettings::NativeFormat);
  }
  const auto clear_native_settings_fixture = qScopeGuard([&] {
    if (native_settings_fixture) {
      QSettings settings;
      settings.clear();
      settings.sync();
    }
  });
  if (!search_edit_proof_dir.isEmpty() || live_visual_search) {
    const auto isolated_data = QDir(search_edit_proof_dir.isEmpty() ? visual_search_proof_dir : search_edit_proof_dir)
        .filePath(QStringLiteral("runtime"));
    QDir().mkpath(QDir(isolated_data).filePath(QStringLiteral("agent-models")));
    qputenv("PHOTO_EDITOR_APP_DATA_DIR", isolated_data.toUtf8());
    const auto model = option_value(arguments, QStringLiteral("--visual-search-model"));
    QFile manifest(QDir(isolated_data).filePath(QStringLiteral("agent-models/models.json")));
    if (model.isEmpty() || !manifest.open(QIODevice::WriteOnly)) return fail(QStringLiteral("Live search/edit requires an installed model and isolated manifest."));
    manifest.write(QJsonDocument(QJsonObject{{"models", QJsonArray{QJsonObject{
        {"id", "search-edit-live"}, {"name", model}, {"backend", "ollama"}, {"model", model},
        {"endpoint", "http://127.0.0.1:11434"}, {"vision", true}, {"available", true},
        // Use the engine's normal request deadline. Earlier extended-deadline
        // runs remain separate evidence; an unavailable critic requires review.
        {"toolCallPlanning", false}, {"contextWindow", 16384}}}}}).toJson());
  }
  if (library_proof || native_readback_proof || !qEnvironmentVariableIsSet("PHOTO_EDITOR_APP_DATA_DIR")) {
    qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime_root.path().toUtf8());
  }
  if (library_proof || native_readback_proof || !qEnvironmentVariableIsSet("PHOTO_EDITOR_CACHE_DIR")) {
    qputenv("PHOTO_EDITOR_CACHE_DIR", cache_root.path().toUtf8());
  }
  if (optical_real_export) {
    qputenv("PHOTO_EDITOR_APP_DATA_ROOT", runtime_root.path().toUtf8());
    qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime_root.path().toUtf8());
    qputenv("PHOTO_EDITOR_CACHE_DIR", cache_root.path().toUtf8());
    qunsetenv("PHOTO_EDITOR_DATABASE_URL");
  } else {
    const auto membership_database = arguments.contains("--library-membership-proof")
        ? option_value(arguments, "--membership-database-url") : QString{};
    qputenv("PHOTO_EDITOR_DATABASE_URL", membership_database.isEmpty()
        ? QByteArray("postgresql://127.0.0.1:1/photo_editor_offline_test") : membership_database.toUtf8());
  }
  const auto stop_owned_catalog = qScopeGuard([&] {
    if (!optical_real_export) return;
    QThreadPool::globalInstance()->waitForDone(10000);
    QProcess stop;
    stop.start(photo_editor::desktop::AppBootstrap::helperProgram("catalog_local_db"),
        photo_editor::desktop::AppBootstrap::helperArguments("catalog_local_db", {"stop"}));
    if (!stop.waitForFinished(10000)) { stop.kill(); stop.waitForFinished(1000); }
  });

  if (source_path.isEmpty()) {
    source_path = fixture_dir.filePath(QStringLiteral("actual-app-workflow-source.png"));
    if (!write_workflow_fixture(source_path)) {
      return fail(QStringLiteral("Unable to create actual-app workflow fixture image."));
    }
  } else if (!QFileInfo::exists(source_path)) {
    return fail(QStringLiteral("Actual-app workflow source does not exist: %1").arg(source_path));
  }

  photo_editor::desktop::observability::initialize();
  photo_editor::desktop::observability::logInfo(
      photoEditorPerfLog(),
      QStringLiteral("benchmark.actual_app_workflow.started"),
      {
          {QStringLiteral("workflow"), QStringLiteral("actual_app_qml")},
          {QStringLiteral("scenario"), route_only ? QStringLiteral("route_only")
              : brush_only ? QStringLiteral("brush_only") : QStringLiteral("full_workflow")},
          {QStringLiteral("brush_auto_mask"), brush_auto_mask},
          {QStringLiteral("elapsed_ms"), 0},
          {QStringLiteral("iterations"), iterations},
          {QStringLiteral("source"), source_path},
      });

  if (!option_value(arguments, "--responsiveness-proof-dir").isEmpty()) {
    const QDir helper_root(fixture_dir.filePath("responsiveness"));
    const QDir binaries(helper_root.filePath("rust/target/debug"));
    if (!QDir().mkpath(binaries.absolutePath())) return fail("Cannot create isolated helper directory.");
    QFile manifest(helper_root.filePath("rust/Cargo.toml"));
    if (!manifest.open(QIODevice::WriteOnly)) return fail("Cannot create isolated helper manifest.");
    manifest.write("# isolated responsiveness fixture\n");
    for (const auto* name : {"catalog_state", "catalog_snapshot"}) {
      QFile helper(binaries.filePath(QLatin1String(name)));
      if (!helper.open(QIODevice::WriteOnly)) return fail("Cannot create isolated helper.");
      helper.write("#!/bin/sh\nexit 1\n");
      helper.close();
      helper.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    }
    qputenv("PHOTO_EDITOR_REPOSITORY_ROOT", helper_root.absolutePath().toUtf8());
    if (photo_editor::desktop::AppBootstrap::helperProgram("catalog_state") != binaries.filePath("catalog_state"))
      return fail("Responsiveness fixture helper is not authoritative.");
  }

  if (arguments.contains("--library-membership-proof")) {
    const auto helper = option_value(arguments, "--membership-helper-fixture");
    const QDir helper_root(fixture_dir.filePath("membership"));
    const QDir binaries(helper_root.filePath("rust/target/debug"));
    if (!QFileInfo(helper).isExecutable() || !QDir().mkpath(binaries.absolutePath()))
      return fail("Membership proof needs an executable helper fixture.");
    QFile manifest(helper_root.filePath("rust/Cargo.toml"));
    if (!manifest.open(QIODevice::WriteOnly) || manifest.write("# isolated fixture\n") < 0)
      return fail("Cannot create membership fixture repository.");
    for (const auto& name : {QStringLiteral("catalog_state"), QStringLiteral("catalog_snapshot")})
      if (!QFile::link(helper, binaries.filePath(name))) return fail("Cannot install membership fixture.");
    QFile snapshot(helper_root.filePath("snapshot.json"));
    if (!snapshot.open(QIODevice::WriteOnly) ||
        snapshot.write(R"({"assets":[],"projects":[],"drives":[],"review_states":{},"relinked_paths":{}})") < 0)
      return fail("Cannot create membership fixture snapshot.");
    qputenv("PHOTO_EDITOR_REPOSITORY_ROOT", helper_root.absolutePath().toUtf8());
    qputenv("PHOTO_EDITOR_MEMBERSHIP_FIXTURE_ROOT", helper_root.absolutePath().toUtf8());
    if (photo_editor::desktop::AppBootstrap::helperProgram("catalog_state") != binaries.filePath("catalog_state"))
      return fail("Membership fixture helper is not authoritative.");
  }

  photo_editor::desktop::AppShellController app_shell;
  if (library_proof || brush_only || native_readback_proof)
    app_shell.completeFirstRunExperience();
  photo_editor::desktop::CatalogAssetModel catalog_assets;
  photo_editor::desktop::CatalogSimilaritySearch similarity_search(&catalog_assets, "isolated-actual-ui-proof");
  if (!similarity_proof_dir.isEmpty()) {
    const auto bundle_root = option_value(arguments, "--search-runtime-bundle");
    bool configured = false;
    if (!bundle_root.isEmpty()) {
      const auto checked = photo_editor::desktop::verify_search_runtime_bundle(
          bundle_root, option_value(arguments, "--search-runtime-receipt"));
      if (!checked.ok) return fail(checked.error);
      configured = similarity_search.controller()->configureExecutable(checked.executable,
          option_value(arguments, "--search-model-root"), option_value(arguments, "--search-device"), runtime_root.filePath("similarity.sqlite3"));
    } else {
      configured = similarity_search.controller()->configure(option_value(arguments, "--search-python"),
          QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR)).filePath("scripts/search_model_worker.py"),
          option_value(arguments, "--search-model-root"), option_value(arguments, "--search-device"), runtime_root.filePath("similarity.sqlite3"));
    }
    if (!configured) return fail("Similarity proof needs explicit local runtime paths.");
  }
  photo_editor::desktop::CarouselDocumentController carousel_document;
  photo_editor::desktop::EditorPreviewController editor_preview;
  photo_editor::desktop::JobRunner job_runner;
  photo_editor::desktop::JobQueueModel job_queue;
  photo_editor::desktop::ExportJobController export_jobs;
  photo_editor::desktop::MacosUpdater macos_updater;
  photo_editor::desktop::PreviewBackendController preview_backend;
  photo_editor::desktop::QmlPerformanceMonitor qml_performance;
  photo_editor::desktop::TimelineDocumentController timeline_document;
  photo_editor::desktop::TimelinePlaybackController timeline_playback(&timeline_document);

  const bool workspace_switch_proof = !option_value(arguments, "--workspace-switch-proof-dir").isEmpty();
  if (!timeline_ui_proof_dir.isEmpty() || color_interaction_proof || !visual_search_proof_dir.isEmpty() || !similarity_proof_dir.isEmpty() || workspace_switch_proof || gaussian_proof || !optical_proof_dir.isEmpty()) {
    app_shell.completeFirstRunExperience();
  }

  preview_backend.setPreferredBackend(color_interaction_proof || workspace_switch_proof || gaussian_proof || brush_only ? QStringLiteral("Auto") : QStringLiteral("CPU"));
  editor_preview.setPreviewTargetEdge(960);

  qmlRegisterType<photo_editor::desktop::PhotoTextDocumentStyle>("PhotoEditor", 1, 0, "PhotoTextDocumentStyle");
  qmlRegisterType<photo_editor::desktop::BrushLivePreviewItem>(
      "PhotoEditor",
      1,
      0,
      "BrushLivePreviewItem");
  qmlRegisterType<photo_editor::desktop::EditorPreviewTextureItem>(
      "PhotoEditor",
      1,
      0,
      "EditorPreviewTextureItem");
  qmlRegisterType<photo_editor::desktop::TabletInputArea>(
      "PhotoEditor",
      1,
      0,
      "TabletInputArea");
  qmlRegisterType<photo_editor::desktop::TimelineAudioOutput>(
      "PhotoEditor",
      1,
      0,
      "TimelineAudioOutput");

#if defined(__APPLE__) && defined(PHOTO_EDITOR_ENABLE_METAL_PREVIEW_BACKEND)
  photo_editor::desktop::GaussianSceneController gaussian_scene;
  qmlRegisterType<photo_editor::desktop::GaussianSceneViewport>("PhotoEditor", 1, 0, "GaussianSceneViewport");
#endif
  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty(
      "timelineInspectorDeferredLoadingEnabled",
      !arguments.contains(QStringLiteral("--timeline-route-performance-eager-inspector")));
  engine.rootContext()->setContextProperty(
      "timelineSourceDeferredLoadingEnabled",
      !arguments.contains(QStringLiteral("--timeline-route-performance-eager-source-panel")));
  engine.rootContext()->setContextProperty(
      "timelineRoutePerformanceForceCollapsedInspector",
      arguments.contains(QStringLiteral("--timeline-route-performance-force-collapsed-inspector")));
  engine.rootContext()->setContextProperty(
      "timelineRoutePerformanceForceCollapsedSourcePanel",
      arguments.contains(QStringLiteral("--timeline-route-performance-force-collapsed-source-panel")));
  QStringList library_qml_errors;
  if (library_proof) {
    QObject::connect(&engine, &QQmlEngine::warnings, &app, [&](const QList<QQmlError>& warnings) {
      for (const auto& warning : warnings) {
        if (warning.description().contains("TypeError") ||
            warning.description().contains("ReferenceError"))
          library_qml_errors.append(warning.toString());
      }
    });
  }
#if defined(__APPLE__) && defined(PHOTO_EDITOR_ENABLE_METAL_PREVIEW_BACKEND)
  engine.rootContext()->setContextProperty("gaussianScene", &gaussian_scene);
#endif
  photo_editor::desktop::VisualSearchController visual_search;
  photo_editor::desktop::AgentEditController search_edit_agent;
  photo_editor::desktop::SearchEditWorkflow search_edit_workflow(&catalog_assets);
  if (live_visual_search) {
    search_edit_agent.setCatalogModel(&catalog_assets);
    search_edit_agent.setExportJobController(&export_jobs);
    search_edit_agent.setSelectedModelId(QStringLiteral("search-edit-live"));
    engine.rootContext()->setContextProperty("agentEdit", &search_edit_agent);
  }
  if (!search_edit_proof_dir.isEmpty()) {
    search_edit_agent.setCatalogModel(&catalog_assets);
    search_edit_agent.setExportJobController(&export_jobs);
    search_edit_agent.setSelectedModelId(QStringLiteral("search-edit-live"));
    search_edit_workflow.setDraftBackend([&] {
      return search_edit_agent.running() || search_edit_agent.draftActive() || search_edit_agent.awaitingUserInput();
    }, [&](const QStringList& ids, const QString& instruction) {
      search_edit_agent.setUserInstructions(instruction);
      return search_edit_agent.startDraftForConfirmedAssets(ids, instruction);
    });
    QObject::connect(&search_edit_agent, &photo_editor::desktop::AgentEditController::draftRunFinished,
        &search_edit_workflow, &photo_editor::desktop::SearchEditWorkflow::draftFinished);
    engine.rootContext()->setContextProperty("agentEdit", &search_edit_agent);
    engine.rootContext()->setContextProperty("searchEdit", &search_edit_workflow);
  }
  if (arguments.contains(QStringLiteral("--indexed-search"))) {
    visual_search.setIndexPath(option_value(arguments, QStringLiteral("--visual-index-path")).isEmpty()
        ? runtime_root.filePath(QStringLiteral("visual-search/descriptions.sqlite3"))
        : option_value(arguments, QStringLiteral("--visual-index-path")));
    visual_search.setUseIndex(true);
  }
  auto* editor_preview_image_provider = new CountingPreviewImageProvider;
  editor_preview.setImageProvider(editor_preview_image_provider);
  engine.addImageProvider(QStringLiteral("editor-preview"), editor_preview_image_provider);
  engine.addImageProvider(
      QStringLiteral("timeline-playback"), timeline_playback.createImageProvider());
  engine.rootContext()->setContextProperty("appShell", &app_shell);
  engine.rootContext()->setContextProperty("catalogAssets", &catalog_assets);
  engine.rootContext()->setContextProperty("visualSearch", &visual_search);
  engine.rootContext()->setContextProperty("similaritySearch", &similarity_search);
  engine.rootContext()->setContextProperty("carouselDocument", &carousel_document);
  engine.rootContext()->setContextProperty("editorPreview", &editor_preview);
  engine.rootContext()->setContextProperty("exportJobs", &export_jobs);
  engine.rootContext()->setContextProperty("jobQueue", &job_queue);
  engine.rootContext()->setContextProperty("macosUpdater", &macos_updater);
  engine.rootContext()->setContextProperty("previewBackend", &preview_backend);
  engine.rootContext()->setContextProperty("qmlPerformance", &qml_performance);
  engine.rootContext()->setContextProperty("timelineDocument", &timeline_document);
  engine.rootContext()->setContextProperty("timelinePlayback", &timeline_playback);

  QElapsedTimer shell_timer;
  shell_timer.start();
  const auto main_qml_path =
      qEnvironmentVariableIsSet("PHOTO_EDITOR_BENCHMARK_QML_ROOT")
          ? QDir(qEnvironmentVariable("PHOTO_EDITOR_BENCHMARK_QML_ROOT")).filePath(QStringLiteral("Main.qml"))
          : QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR)).filePath(QStringLiteral("apps/desktop/qml/Main.qml"));
  photo_editor::desktop::observability::logInfo(
      photoEditorPerfLog(),
      QStringLiteral("benchmark.actual_app_workflow.qml_load_started"),
      {{QStringLiteral("workflow"), QStringLiteral("actual_app_qml")},
       {QStringLiteral("elapsed_ms"), 0},
       {QStringLiteral("path"), main_qml_path},
       {QStringLiteral("qt_platform"), QGuiApplication::platformName()},
       {QStringLiteral("requested_rhi_backend"), qEnvironmentVariable("QSG_RHI_BACKEND")},
       {QStringLiteral("settings_backend"), native_settings_fixture ? QStringLiteral("native-isolated") : QStringLiteral("default")}});
  engine.load(QUrl::fromLocalFile(main_qml_path));
  QCoreApplication::processEvents();
  photo_editor::desktop::observability::logInfo(
      photoEditorPerfLog(),
      QStringLiteral("benchmark.actual_app_workflow.qml_load_returned"),
      {{QStringLiteral("workflow"), QStringLiteral("actual_app_qml")},
       {QStringLiteral("elapsed_ms"), shell_timer.elapsed()},
       {QStringLiteral("root_count"), engine.rootObjects().size()}});
  if (engine.rootObjects().isEmpty()) {
    return fail(QStringLiteral("Actual-app workflow should load Main.qml."));
  }
  QObject* root = engine.rootObjects().constFirst();
  auto* root_window = qobject_cast<QQuickWindow*>(root);
  if (root == nullptr || root_window == nullptr) {
    return fail(QStringLiteral("Actual-app workflow requires Main.qml to expose a root window."));
  }
  new photo_editor::desktop::RecentUserInputFilter(root_window);
  root->setProperty("width", optical_proof_dir.isEmpty() ? 1600 : std::max(900, option_int(arguments, "--optical-width", 1600)));
  root->setProperty("height", optical_proof_dir.isEmpty() ? 1000 : std::max(650, option_int(arguments, "--optical-height", 1000)));
  process_layout_events(8);
  if (!wait_for_condition(
          [&]() {
            auto* workspace = active_workspace(root);
            return workspace != nullptr &&
                   workspace->objectName() == QStringLiteral("libraryStudioWorkspace");
          },
          timeout_ms)) {
    return fail(QStringLiteral("Actual-app workflow did not expose the initial QML workspace."));
  }
  const auto shell_elapsed_ms = shell_timer.elapsed();
  log_stage(
      QStringLiteral("qml_shell_load_to_ready"),
      shell_elapsed_ms,
      0,
      {{QStringLiteral("root_width"), root->property("width").toInt()},
       {QStringLiteral("root_height"), root->property("height").toInt()}});

  if (route_only) {
    // Isolate first-use workspace construction from photo decode, preview
    // rendering, masks, thumbnails, and export jobs in the full workflow.
    // Mark synthetic input before the short route loop so Main.qml's idle
    // workspace prewarm cannot overlap the measurement.
    QElapsedTimer route_suite_timer;
    route_suite_timer.start();
    root->setProperty("recentUserInput", true);
    if (route_only_active_editor) {
      timeline_document.createTimelineWithSettings(
          {{QStringLiteral("timelineName"), QStringLiteral("Route Performance Fixture")},
           {QStringLiteral("width"), 1080},
           {QStringLiteral("height"), 1920},
           {QStringLiteral("frameRate"), 30},
           {QStringLiteral("audioSampleRate"), 48000},
           {QStringLiteral("colorSpace"), QStringLiteral("Rec.709")},
           {QStringLiteral("dataLevels"), QStringLiteral("Legal")}});
    }
    if (route_only_async) {
      auto* carousel_loader = find_named_object(
          root,
          QStringLiteral("carouselWorkspaceLoader"));
      auto* timeline_loader = find_named_object(
          root,
          QStringLiteral("timelineWorkspaceLoader"));
      if (carousel_loader == nullptr || timeline_loader == nullptr ||
          !carousel_loader->setProperty("asynchronous", true) ||
          !timeline_loader->setProperty("asynchronous", true)) {
        return fail(QStringLiteral("Route-only asynchronous Loader controls are unavailable."));
      }
    }
    if (route_only_no_render) {
      root_window->hide();
      QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
    }
    std::vector<double> carousel_route_samples;
    std::vector<double> carousel_input_dispatch_samples;
    std::vector<double> timeline_route_samples;
    std::vector<double> timeline_input_dispatch_samples;
    carousel_route_samples.reserve(static_cast<std::size_t>(iterations));
    carousel_input_dispatch_samples.reserve(static_cast<std::size_t>(iterations));
    timeline_route_samples.reserve(static_cast<std::size_t>(iterations));
    timeline_input_dispatch_samples.reserve(static_cast<std::size_t>(iterations));

    const auto measure_route = [&](const int workspace_index,
                                   const QString& workspace_name,
                                   const QString& ready_item,
                                   const QString& route_stage,
                                   const QString& input_stage,
                                   std::vector<double>* route_samples,
                                   std::vector<double>* input_samples,
                                   const int iteration) {
      if (workspace_index < 0 || route_samples == nullptr || input_samples == nullptr)
        return false;

      QMetaObject::invokeMethod(root, "flushQmlFrameSummary");
      root->setProperty("qmlFrameCaptureEnabled", true);
      QMetaObject::invokeMethod(root, "beginQmlFrameCaptureWindow");
      QElapsedTimer route_timer;
      route_timer.start();
      const auto route_started_at = std::chrono::steady_clock::now();
      RouteInputDispatchProbe input_probe(root_window);
      input_probe.postNoOpKeyAfter(route_started_at);
      QElapsedTimer workspace_switch_timer;
      workspace_switch_timer.start();
      root->setProperty("currentWorkspace", workspace_index);
      const auto workspace_switch_setter_ms = workspace_switch_timer.elapsed();
      if (!wait_for_condition(
              [&]() {
                auto* workspace = active_workspace(root);
                return workspace != nullptr && workspace->objectName() == workspace_name;
              },
              timeout_ms)) {
        std::cerr << "Route-only workspace root timed out: " << workspace_name.toStdString()
                  << '\n';
        return false;
      }
      const auto workspace_root_ready_ms = route_timer.elapsed();
      if (!wait_for_condition(
              [&]() {
                auto* workspace = active_workspace(root);
                if (workspace == nullptr || workspace->objectName() != workspace_name)
                  return false;
                if (!route_only_no_render)
                  return named_item_visible(workspace, ready_item);
                const auto ready_object =
                    find_named_object(workspace, ready_item);
                return workspace->property("visible").toBool() && ready_object != nullptr &&
                       ready_object->property("visible").toBool();
              },
              timeout_ms)) {
        std::cerr << "Route-only workspace timed out: " << workspace_name.toStdString()
                  << '\n';
        return false;
      }

      const auto route_elapsed_ms = route_timer.elapsed();
      route_samples->push_back(static_cast<double>(route_elapsed_ms));
      if (!wait_for_condition(
              [&input_probe]() { return input_probe.delivered(); },
              timeout_ms,
              "route-only-input-event")) {
        std::cerr << "Route-only input probe timed out: " << workspace_name.toStdString()
                  << '\n';
        return false;
      }
      input_probe.joinPoster();
      const auto input_dispatch_delay_ms = input_probe.dispatchDelayMs();
      const auto recent_input_detected = input_probe.recentInputFilterObserved();
      if (input_dispatch_delay_ms < 0.0 || !recent_input_detected) {
        std::cerr << "Route-only input probe was not observed: "
                  << workspace_name.toStdString() << '\n';
        return false;
      }
      input_samples->push_back(input_dispatch_delay_ms);
      log_stage(
          input_stage,
          static_cast<qint64>(std::lround(input_dispatch_delay_ms)),
          iteration,
          {{QStringLiteral("dispatch_delay_ms_exact"), input_dispatch_delay_ms},
           {QStringLiteral("route_offset_ms"), input_probe.routeOffsetMs()},
           {QStringLiteral("workspace_switch_setter_ms"), workspace_switch_setter_ms},
           {QStringLiteral("workspace_root_ready_ms"), workspace_root_ready_ms},
           {QStringLiteral("route_visible_ms"), route_elapsed_ms},
           {QStringLiteral("recent_user_input_detected"), recent_input_detected},
           {QStringLiteral("preview_asset_loaded"), false},
           {QStringLiteral("active_timeline"), route_only_active_editor},
           {QStringLiteral("window_visible"), root_window->isVisible()}});
      root_window->removeEventFilter(&input_probe);
      process_layout_events(1);
      QMetaObject::invokeMethod(root, "flushQmlFrameSummary");
      root->setProperty("qmlFrameCaptureEnabled", false);
      log_stage(
          route_stage,
          route_elapsed_ms,
          iteration,
          {{QStringLiteral("preview_asset_loaded"), false},
           {QStringLiteral("active_timeline"), route_only_active_editor},
           {QStringLiteral("workspace_switch_setter_ms"), workspace_switch_setter_ms},
           {QStringLiteral("workspace_root_ready_ms"), workspace_root_ready_ms},
           {QStringLiteral("window_visible"), root_window->isVisible()}});
      return true;
    };

    const auto library_workspace_index =
        root->property("libraryWorkspaceIndex").toInt();
    const auto carousel_workspace_index =
        root->property("carouselWorkspaceIndex").toInt();
    const auto timeline_workspace_index =
        root->property("timelineWorkspaceIndex").toInt();
    for (int iteration = 0; iteration < iterations; ++iteration) {
      if (iteration > 0) {
        root->setProperty("currentWorkspace", library_workspace_index);
        if (!wait_for_condition(
                [&]() {
                  auto* workspace = active_workspace(root);
                  return workspace != nullptr &&
                         workspace->objectName() == QStringLiteral("libraryStudioWorkspace");
                },
                timeout_ms)) {
          return fail(QStringLiteral("Route-only workflow could not return to Library."));
        }
      }
      if (!measure_route(
              carousel_workspace_index,
              QStringLiteral("carouselWorkspace"),
              QStringLiteral("carouselSetupSurface"),
              QStringLiteral("carousel_route_to_visible"),
              QStringLiteral("carousel_input_event_dispatch_delay"),
              &carousel_route_samples,
              &carousel_input_dispatch_samples,
              iteration) ||
          !measure_route(
              timeline_workspace_index,
              QStringLiteral("timelineWorkspace"),
              route_only_active_editor ? QStringLiteral("timelineEditorPageContent")
                                       : QStringLiteral("timelineSetupSurface"),
              QStringLiteral("timeline_route_to_visible"),
              QStringLiteral("timeline_input_event_dispatch_delay"),
              &timeline_route_samples,
              &timeline_input_dispatch_samples,
              iteration)) {
        return fail(QStringLiteral("Route-only workflow did not complete its route and input measurements."));
      }
    }

    QMetaObject::invokeMethod(root, "flushQmlFrameSummary");
    log_summary(
        QStringLiteral("carousel_route_to_visible"),
        summarize_samples(carousel_route_samples),
        iterations,
        {{QStringLiteral("preview_asset_loaded"), false}});
    log_summary(
        QStringLiteral("carousel_input_event_dispatch_delay"),
        summarize_samples(carousel_input_dispatch_samples),
        iterations,
        {{QStringLiteral("preview_asset_loaded"), false}});
    log_summary(
        QStringLiteral("timeline_route_to_visible"),
        summarize_samples(timeline_route_samples),
        iterations,
        {{QStringLiteral("preview_asset_loaded"), false}});
    log_summary(
        QStringLiteral("timeline_input_event_dispatch_delay"),
        summarize_samples(timeline_input_dispatch_samples),
        iterations,
        {{QStringLiteral("preview_asset_loaded"), false}});
    photo_editor::desktop::observability::logInfo(
        photoEditorPerfLog(),
        QStringLiteral("benchmark.actual_app_workflow.completed"),
        {{QStringLiteral("workflow"), QStringLiteral("actual_app_qml")},
         {QStringLiteral("scenario"), QStringLiteral("route_only")},
         {QStringLiteral("iterations"), iterations},
         {QStringLiteral("preview_asset_loaded"), false},
         {QStringLiteral("active_timeline"), route_only_active_editor},
         {QStringLiteral("workspace_loader_async"), route_only_async},
         {QStringLiteral("window_visible"), root_window->isVisible()},
         {QStringLiteral("elapsed_ms"), route_suite_timer.elapsed()}});
    return EXIT_SUCCESS;
  }

  if (!optical_proof_dir.isEmpty()) {
    using Access = photo_editor::desktop::EditorPreviewControllerTestAccess;
    QVector<photo_editor::desktop::CatalogAssetItem> optical_assets{
        make_asset(QStringLiteral("optical-proof-photo"), source_path)};
    if (option_value(arguments, QStringLiteral("--source")).isEmpty()) {
      const QImage generated(source_path);
      for (int index = 0; index < 23; ++index) {
        const auto image_path = fixture_dir.filePath(QStringLiteral("orientation-%1.png").arg(index));
        const auto size = index % 2 == 0 ? QSize(540, 810) : QSize(810, 540);
        if (!generated.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).save(image_path))
          return fail("Could not prepare synthetic filmstrip orientations.");
        optical_assets.append(make_asset(QStringLiteral("optical-orientation-%1").arg(index), image_path));
      }
    }
    catalog_assets.replaceAssetsForTesting(optical_assets);
    catalog_assets.selectIndexWithModifiers(0, 0);
    root->setProperty("currentWorkspace", root->property("editWorkspaceIndex"));
    root->setProperty("editWorkspaceMode", QStringLiteral("adjust"));
    if (!wait_for_condition([&]() {
          return editor_preview.hasAsset() && Access::all_idle(editor_preview) &&
                 named_item_visible(active_workspace(root), QStringLiteral("editImageViewport"));
        }, timeout_ms)) return fail(QStringLiteral("Optical proof did not reach the photo viewport."));
    editor_preview.setExposure(1.53);
    if (!wait_for_condition([&]() { return Access::all_idle(editor_preview); }, timeout_ms))
      return fail(QStringLiteral("Optical proof photo edit did not settle."));
    QJsonArray captures;
    bool compact_inspector_verified = false;
    bool filmstrip_scroll_verified = false;
    const auto settle_preview = [&]() {
      QElapsedTimer stable;
      stable.start();
      QString last_source;
      return wait_for_condition([&]() {
        const auto current_source = editor_preview.displayPreviewSource();
        if (!Access::all_idle(editor_preview) || editor_preview.processing() || current_source != last_source) {
          stable.restart();
          last_source = current_source;
          return false;
        }
        return stable.elapsed() >= 350;
      }, timeout_ms);
    };
    if (!settle_preview()) return fail("Initial optical preview did not settle.");
    save_capture_if_requested(root_window, optical_proof_dir, QStringLiteral("01-edit"), &captures);
    auto* workspace = active_workspace(root);
    auto* pointer = workspace->findChild<QQuickItem*>(QStringLiteral("exposureSliderPrecisionPointer"));
    auto* caustic = workspace->findChild<QObject*>(QStringLiteral("editCompletionCaustic"));
    if (!pointer || !caustic) return fail(QStringLiteral("Optical adjustment interaction targets are missing."));
    const auto old_exposure = editor_preview.exposure();
    const auto press = pointer->mapToScene(QPointF(pointer->width() * 0.65, pointer->height() * 0.5));
    const auto release = press + QPointF(18, 0);
    const auto send_pointer = [&](QEvent::Type type, const QPointF& position, Qt::MouseButton button, Qt::MouseButtons buttons) {
      QMouseEvent event(type, position, root_window->mapToGlobal(position.toPoint()), button, buttons, Qt::NoModifier);
      QCoreApplication::sendEvent(root_window, &event);
      process_layout_events();
    };
    send_pointer(QEvent::MouseButtonPress, press, Qt::LeftButton, Qt::LeftButton);
    send_pointer(QEvent::MouseMove, release, Qt::NoButton, Qt::LeftButton);
    send_pointer(QEvent::MouseButtonRelease, release, Qt::LeftButton, Qt::NoButton);
    if (std::abs(editor_preview.exposure() - old_exposure) < 0.001)
      return fail(QStringLiteral("The visible exposure control did not change the recipe."));
    if (!wait_for_condition([&]() { return caustic->property("completionCount").toInt() > 0; }, timeout_ms))
      return fail(QStringLiteral("Committed adjustment did not receive a completion effect: pending=%1 baseline=%2 source=%3 busy=%4 full=%5")
          .arg(workspace->property("editCompletionPending").toBool()).arg(workspace->property("editCompletionBaseline").toString())
          .arg(editor_preview.displayPreviewSource()).arg(editor_preview.processing()).arg(editor_preview.fullRenderRunning()));
    if (!wait_for_condition([&]() { return caustic->property("phase").toDouble() >= 0.3; }, 2000))
      return fail(QStringLiteral("Completion animation did not advance."));
    save_capture_if_requested(root_window, optical_proof_dir, QStringLiteral("01b-completion"), &captures);
    if (!wait_for_condition([&]() { return !caustic->property("running").toBool(); }, 2000))
      return fail(QStringLiteral("Completion animation did not become idle."));
    if (!wait_for_condition([&]() { return !caustic->property("surfaceAllocated").toBool(); }, 2000))
      return fail("Idle caustic retained its drawing surface.");
    const int idle_paints = caustic->property("paintCount").toInt();
    if (idle_paints == 0) return fail("Completion surface never painted.");
    QElapsedTimer idle_sample;
    idle_sample.start();
    wait_for_condition([&]() { return idle_sample.elapsed() >= 300; }, 1000);
    if (caustic->property("paintCount").toInt() != idle_paints)
      return fail("Completion continued painting while idle.");
    auto* reset_view = workspace->findChild<QQuickItem*>(QStringLiteral("fitToViewButton"));
    if (!reset_view) return fail("Preview Fit button is missing.");
    const auto edited_exposure = editor_preview.exposure();
    const auto reset_position = reset_view->mapToScene(QPointF(reset_view->width() / 2, reset_view->height() / 2));
    send_pointer(QEvent::MouseButtonPress, reset_position, Qt::LeftButton, Qt::LeftButton);
    send_pointer(QEvent::MouseButtonRelease, reset_position, Qt::LeftButton, Qt::NoButton);
    if (std::abs(editor_preview.exposure() - edited_exposure) > 0.001)
      return fail("Resetting the preview changed the image exposure.");
    if (arguments.contains("--optical-record-caustic")) {
      const QString frames_dir = QDir(optical_proof_dir).filePath("caustic-frames");
      QDir().mkpath(frames_dir);
      std::vector<QImage> frames;
      QJsonArray frame_times;
      QElapsedTimer recording;
      recording.start();
      QMetaObject::invokeMethod(caustic, "complete");
      for (int index = 0; index < 25; ++index) {
        wait_for_condition([&]() { return recording.elapsed() >= index * 40; }, 2000);
        frames.push_back(root_window->grabWindow().scaledToWidth(1280, Qt::SmoothTransformation));
        frame_times.append(QJsonObject{{"frame", index}, {"elapsedMs", recording.elapsed()},
            {"phase", caustic->property("phase").toDouble()}});
      }
      for (std::size_t index = 0; index < frames.size(); ++index)
        if (!frames[index].save(QDir(frames_dir).filePath(QString("frame-%1.png").arg(index, 3, 10, QChar('0')))))
          return fail("Could not save caustic recording frame.");
      QFile frame_receipt(QDir(frames_dir).filePath("timing.json"));
      if (!frame_receipt.open(QIODevice::WriteOnly)) return fail("Could not save caustic frame timings.");
      frame_receipt.write(QJsonDocument(frame_times).toJson());
    }
    caustic->setProperty("reducedMotion", true);
    QMetaObject::invokeMethod(caustic, "complete");
    if (caustic->property("running").toBool()) return fail(QStringLiteral("Reduced motion still animated."));
    caustic->setProperty("reducedMotion", false);
    if (!QMetaObject::invokeMethod(workspace, "activateEditTool", Q_ARG(QVariant, QStringLiteral("compare"))))
      return fail(QStringLiteral("Optical proof could not open comparison."));
    process_layout_events(12);
    if (!wait_for_condition([&]() { return Access::all_idle(editor_preview); }, timeout_ms)) return fail("Comparison did not settle.");
    if (!settle_preview()) return fail("Comparison publication did not settle.");
    save_capture_if_requested(root_window, optical_proof_dir, QStringLiteral("02-compare"), &captures);
    const auto prior_compare_view = workspace->property("compareViewMode");
    workspace->setProperty("compareViewMode", "wipe");
    process_layout_events(12);
    auto* wipe = qobject_cast<QQuickItem*>(find_named_object(workspace, "beforeAfterWipeView"));
    if (!wipe || !wipe->isVisible() || wipe->width() <= 0) return fail("Wipe pointer surface is unavailable.");
    const auto wipe_start = wipe->mapToScene(QPointF(wipe->width() * 0.5, wipe->height() * 0.5));
    const auto wipe_end = wipe->mapToScene(QPointF(wipe->width() * 0.72, wipe->height() * 0.5));
    send_pointer(QEvent::MouseButtonPress, wipe_start, Qt::LeftButton, Qt::LeftButton);
    send_pointer(QEvent::MouseMove, wipe_end, Qt::NoButton, Qt::LeftButton);
    send_pointer(QEvent::MouseButtonRelease, wipe_end, Qt::LeftButton, Qt::NoButton);
    if (std::abs(workspace->property("compareSplitPosition").toDouble() - 0.72) > 0.025)
      return fail("Wipe divider did not follow pointer drag.");
    QElapsedTimer wipe_transition;
    wipe_transition.start();
    wait_for_condition([&] { return wipe_transition.elapsed() >= 220; }, 1000);
    save_capture_if_requested(root_window, optical_proof_dir, "02d-wipe-pointer", &captures);
    workspace->setProperty("compareViewMode", prior_compare_view);
    process_layout_events(12);
    const auto source_snapshot = editor_preview.createEditHistorySnapshot(QStringLiteral("Comparison label proof"));
    if (source_snapshot.isEmpty()) return fail("Could not create comparison label fixture.");
    editor_preview.setCompareSnapshotId(source_snapshot);
    editor_preview.setCompareSourceMode(QStringLiteral("auto"));
    process_layout_events(4);
    QVariant source_title;
    if (!QMetaObject::invokeMethod(workspace, "comparePanelTitle", Q_RETURN_ARG(QVariant, source_title),
                                  Q_ARG(QVariant, 0)) ||
        !source_title.toString().contains(QStringLiteral("Snapshot: Comparison label proof")))
      return fail("Automatic snapshot comparison has the wrong title.");
    auto* snapshot_source = workspace->findChild<QObject*>(QStringLiteral("compareSnapshotSourceButton"));
    if (!snapshot_source || !snapshot_source->property("active").toBool())
      return fail("Automatic snapshot comparison has no active source control.");
    editor_preview.setCompareSourceMode(QStringLiteral("original"));
    editor_preview.deleteEditHistorySnapshot(source_snapshot);
    if (!settle_preview()) return fail("Comparison source reset did not settle.");
    auto* bypass_menu = workspace->findChild<QQuickItem*>(QStringLiteral("compareBypassMenu"));
    if (!bypass_menu) return fail("Comparison bypass menu is inaccessible.");
    const auto send_key = [&](int key) {
      QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
      QCoreApplication::sendEvent(root_window, &press);
      QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
      QCoreApplication::sendEvent(root_window, &release);
      process_layout_events();
    };
    bypass_menu->forceActiveFocus(Qt::TabFocusReason);
    send_key(Qt::Key_Space);
    auto* bypass_popup = root->findChild<QObject*>(QStringLiteral("compareBypassMenuPopup"));
    if (!bypass_popup || !wait_for_condition([&]() { return bypass_popup->property("opened").toBool(); }))
      return fail("Space did not open the focused comparison bypass menu.");
    save_capture_if_requested(root_window, optical_proof_dir, QStringLiteral("02c-bypass-menu"), &captures);
    send_key(Qt::Key_Down);
    if (bypass_popup->property("currentFocusIndex").toInt() != 1)
      return fail("Down did not move bypass focus to Color.");
    send_key(Qt::Key_Return);
    QVariant bypass_title;
    QMetaObject::invokeMethod(workspace, "comparePanelTitle", Q_RETURN_ARG(QVariant, bypass_title), Q_ARG(QVariant, 0));
    if (!editor_preview.compareAdjustmentGroups().contains("color") ||
        !bypass_title.toString().contains("Color bypassed"))
      return fail("Keyboard bypass selection did not update its state and Before title.");
    if (!wait_for_condition([&]() { return !bypass_popup->property("visible").toBool(); }))
      return fail("Selecting a bypass group did not dismiss the menu.");
    bypass_menu->forceActiveFocus(Qt::TabFocusReason);
    send_key(Qt::Key_Space);
    if (!wait_for_condition([&]() { return bypass_popup->property("opened").toBool(); }))
      return fail("Keyboard could not reopen the bypass menu.");
    send_key(Qt::Key_Escape);
    if (!wait_for_condition([&]() { return !bypass_popup->property("visible").toBool(); }) ||
        !editor_preview.compareAdjustmentGroups().contains("color"))
      return fail("Escape did not close the menu while preserving the selected bypass group.");
    editor_preview.setCompareAdjustmentGroups({});
    workspace->setProperty("compareViewMode", QStringLiteral("side-by-side"));
    process_layout_events(12);
    if (!named_item_visible(workspace, QStringLiteral("compareSourceControls")) ||
        !named_item_visible(workspace, QStringLiteral("compareImageViewport0")))
      return fail("Linked comparison lost its controls or viewports.");
    save_capture_if_requested(root_window, optical_proof_dir, QStringLiteral("02b-linked-compare"), &captures);
    workspace->setProperty("compareViewMode", QStringLiteral("split"));
    QMetaObject::invokeMethod(workspace, "activateEditTool", Q_ARG(QVariant, QStringLiteral("adjust")));
    QMetaObject::invokeMethod(workspace, "openScopesDock", Q_ARG(QVariant, QStringLiteral("bottom")));
    process_layout_events(12);
    if (!wait_for_condition([&]() { return Access::all_idle(editor_preview); }, timeout_ms)) return fail("Scopes photo did not settle.");
    if (!settle_preview()) return fail("Scopes publication did not settle.");
    save_capture_if_requested(root_window, optical_proof_dir, QStringLiteral("03-scopes"), &captures);
    auto* scope_settings_button = workspace->findChild<QObject*>(QStringLiteral("bottomScopeSettingsButton"));
    if (!scope_settings_button || !QMetaObject::invokeMethod(scope_settings_button, "clicked"))
      return fail("Scope settings are inaccessible.");
    auto* scope_settings = workspace->findChild<QObject*>(QStringLiteral("bottomScopeSettingsPopup"));
    if (!scope_settings || !wait_for_condition([&]() { return scope_settings->property("opened").toBool(); }))
      return fail("Scope settings did not open.");
    save_capture_if_requested(root_window, optical_proof_dir, QStringLiteral("03b-scope-settings"), &captures);
    QMetaObject::invokeMethod(scope_settings, "close");
    QMetaObject::invokeMethod(workspace, "openScopesDock", Q_ARG(QVariant, QStringLiteral("right")));
    process_layout_events(12);
    if (!named_item_visible(workspace, QStringLiteral("inspectorScopeSettingsButton")))
      return fail("Inspector scope settings are inaccessible.");
    save_capture_if_requested(root_window, optical_proof_dir, QStringLiteral("03c-inspector-scopes"), &captures);
    auto* inspector_settings_button = workspace->findChild<QObject*>(QStringLiteral("inspectorScopeSettingsButton"));
    QMetaObject::invokeMethod(inspector_settings_button, "clicked");
    auto* inspector_settings = workspace->findChild<QObject*>(QStringLiteral("inspectorScopeSettingsPopup"));
    if (!inspector_settings || !wait_for_condition([&]() { return inspector_settings->property("opened").toBool(); }))
      return fail("Inspector scope settings did not open.");
    auto* log_toggle = workspace->findChild<QObject*>(QStringLiteral("inspectorScopeLogToggle"));
    const bool prior_log = workspace->property("scopeLogHistogram").toBool();
    if (!log_toggle || !QMetaObject::invokeMethod(log_toggle, "clicked") ||
        workspace->property("scopeLogHistogram").toBool() == prior_log)
      return fail("Inspector histogram scale toggle did not change state.");
    QMetaObject::invokeMethod(log_toggle, "clicked");
    save_capture_if_requested(root_window, optical_proof_dir, QStringLiteral("03d-inspector-scope-settings"), &captures);
    QMetaObject::invokeMethod(inspector_settings, "close");

    root->setProperty("currentWorkspace", root->property("exportWorkspaceIndex"));
    process_layout_events(12);
    save_capture_if_requested(root_window, optical_proof_dir, QStringLiteral("04-export"), &captures);
    auto* preset_control = root->findChild<QQuickItem*>(QStringLiteral("exportPresetCombo"));
    if (!preset_control) return fail("Delivery preset control is missing.");
    const auto preset_index = preset_control->property("currentIndex").toInt();
    preset_control->forceActiveFocus(Qt::TabFocusReason);
    send_key(Qt::Key_Space);
    auto* preset_popup = preset_control->property("popup").value<QObject*>();
    if (!preset_popup || !wait_for_condition([&]() { return preset_popup->property("opened").toBool(); }))
      return fail("Delivery preset menu did not open from the keyboard.");
    save_capture_if_requested(root_window, optical_proof_dir, QStringLiteral("04a-preset-menu"), &captures);
    auto* preset_list = preset_popup->property("contentItem").value<QQuickItem*>();
    auto* preset_content = preset_list ? preset_list->property("contentItem").value<QQuickItem*>() : nullptr;
    int readable_options = 0;
    if (preset_content) {
      for (auto* option : preset_content->childItems()) {
        if (!option->inherits("QQuickItemDelegate")) continue;
        auto* label = option->property("contentItem").value<QQuickItem*>();
        if (!label || label->height() + 0.5 < label->property("contentHeight").toDouble())
          return fail("Preset dropdown text is vertically clipped.");
        ++readable_options;
      }
    }
    if (readable_options == 0) return fail("Preset dropdown has no readable option delegates.");

    send_key(Qt::Key_Escape);
    if (preset_popup->property("visible").toBool() || preset_control->property("currentIndex").toInt() != preset_index)
      return fail("Closing the preset menu changed the preset or left the popup open.");

    auto* output_color = root->findChild<QQuickItem*>(QStringLiteral("exportColorSpaceSection"));
    auto* output_crop = root->findChild<QQuickItem*>(QStringLiteral("exportCropSection"));
    if (!output_color || !output_crop)
      return fail("Delivery color/crop sections are missing.");
    const auto color_origin = output_color->mapToScene(QPointF(0, 0));
    const auto crop_origin = output_crop->mapToScene(QPointF(0, 0));
    if (output_color->width() < 100 || output_crop->width() < 100 ||
        output_color->height() < 60 || output_crop->height() < 60 ||
        std::abs(color_origin.y() - crop_origin.y()) > 1 ||
        crop_origin.x() < color_origin.x() + output_color->width() + 10)
      return fail("Delivery color/crop row overlaps or has collapsed geometry.");

    QQmlComponent delivery_component(&engine, QUrl::fromLocalFile(
        QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR)).filePath("apps/desktop/qml/components/ExportQueueRow.qml")));
    std::unique_ptr<QObject> delivery_object(delivery_component.createWithInitialProperties(
        {{"statusText", "Completed"}, {"title", "Completion feedback fixture"},
         {"subtitle", "Simulated job states · no file exported"}}));
    auto* delivery_row = qobject_cast<QQuickItem*>(delivery_object.get());
    if (!delivery_row) return fail("Could not load export completion fixture.");
    delivery_row->setParentItem(root_window->contentItem());
    delivery_row->setWidth(std::min(600.0, root_window->width() - 32.0));
    delivery_row->setHeight(delivery_row->implicitHeight());
    delivery_row->setX(16);
    delivery_row->setY(root_window->height() - delivery_row->height() - 16);
    delivery_row->setZ(1000);
    process_layout_events(4);
    auto* delivery_effect = delivery_row->findChild<QObject*>("exportRowCompletionCaustic");
    if (!delivery_effect || delivery_effect->property("completionCount").toInt() != 0)
      return fail("Completed job history incorrectly triggered a completion effect.");
    delivery_row->setProperty("statusText", "Running");
    delivery_row->setProperty("statusText", "Failed");
    if (delivery_effect->property("completionCount").toInt() != 0)
      return fail("Failed export incorrectly triggered a completion effect.");
    delivery_row->setProperty("statusText", "Saving");
    delivery_row->setProperty("statusText", "Completed");
    if (!wait_for_condition([&]() { return delivery_effect->property("phase").toDouble() >= 0.3
        && delivery_effect->property("running").toBool(); }, 2000))
      return fail("Export completion did not animate.");
    save_capture_if_requested(root_window, optical_proof_dir, QStringLiteral("04b-export-completion-fixture"), &captures);
    if (!wait_for_condition([&]() { return !delivery_effect->property("running").toBool()
        && !delivery_effect->property("surfaceAllocated").toBool(); }, 2000))
      return fail("Export completion did not release its drawing surface.");
    delivery_object.reset();
    photo_editor::desktop::PersistentJobRecord queue_fixture;
    queue_fixture.job_id = "optical-completion-fixture";
    queue_fixture.job_kind = "asset_export";
    queue_fixture.display_label = "Queue transition fixture · no file exported";
    queue_fixture.status = "Running";
    queue_fixture.progress = 80;
    photo_editor::desktop::JobQueueModelTestAccess::publish(job_queue, {queue_fixture});
    process_layout_events(12);
    QPointer<QObject> queue_row = find_named_object(root, QStringLiteral("exportQueueRow"));
    if (!queue_row) return fail("Export queue model did not create a visible row.");
    auto* queue_effect = queue_row->findChild<QObject*>(QStringLiteral("exportRowCompletionCaustic"));
    queue_fixture.status = "Completed";
    queue_fixture.progress = 100;
    photo_editor::desktop::JobQueueModelTestAccess::publish(job_queue, {queue_fixture});
    process_layout_events(4);
    if (!queue_row || !queue_effect || queue_effect->property("completionCount").toInt() != 1)
      return fail("Queue model refresh lost the row or its completion transition.");
    if (!wait_for_condition([&]() { return queue_effect->property("phase").toDouble() >= 0.3 &&
          queue_effect->property("running").toBool() && queue_effect->property("paintCount").toInt() > 0; }, 2000))
      return fail("Queue completion transition did not paint its animation.");
    save_capture_if_requested(root_window, optical_proof_dir, QStringLiteral("04c-queue-completion-fixture"), &captures);
    if (!wait_for_condition([&]() { return !queue_effect->property("running").toBool() &&
          !queue_effect->property("surfaceAllocated").toBool(); }, 2000))
      return fail("Queue completion animation did not finish.");
    photo_editor::desktop::JobQueueModelTestAccess::publish(job_queue, {});
    if (optical_real_export) {
      bool recovered = false;
      QString recovery_error;
      const auto recovery_connection = QObject::connect(&job_runner,
          &photo_editor::desktop::JobRunner::startupRecoveryFinished, &app,
          [&](const QString& error) { recovered = true; recovery_error = error; });
      job_runner.initializeFromStore();
      const bool recovery_ready = wait_for_condition([&] { return recovered; }, timeout_ms);
      QObject::disconnect(recovery_connection);
      if (!recovery_ready || !recovery_error.isEmpty()) return fail("Isolated export runner recovery failed: " + recovery_error);
      job_queue.initializeFromStore();
      export_jobs.initializeFromStore();
      if (!wait_for_condition([&] { return !job_queue.loading() && !export_jobs.loading(); }, timeout_ms))
        return fail("Isolated export history did not load.");
      const auto export_destination = fixture_dir.filePath("actual-export");
      if (!QDir().mkpath(export_destination)) return fail("Could not create isolated export destination.");
      auto* destination_field = find_named_object(root, "exportDestinationField");
      auto* start_export = qobject_cast<QQuickItem*>(find_named_object(root, "exportCurrentSplitButton"));
      if (!destination_field || !start_export) return fail("Native export controls are missing.");
      destination_field->setProperty("text", export_destination);
      process_layout_events(12);
      if (!start_export->property("primaryEnabled").toBool()) return fail("Native export preflight blocked the generated image.");
      QStringList observed_export_states;
      const auto state_connection = QObject::connect(&job_queue, &photo_editor::desktop::JobQueueModel::jobsChanged,
          &app, [&] {
            for (int row = 0; row < job_queue.rowCount(); ++row) {
              const auto state = job_queue.data(job_queue.index(row), photo_editor::desktop::JobQueueModel::StatusRole).toString();
              if (observed_export_states.isEmpty() || observed_export_states.last() != state)
                observed_export_states.push_back(state);
            }
          });
      const auto disconnect_states = qScopeGuard([&] { QObject::disconnect(state_connection); });
      const auto export_position = start_export->mapToScene(QPointF(start_export->width() * 0.35, start_export->height() / 2));
      send_pointer(QEvent::MouseButtonPress, export_position, Qt::LeftButton, Qt::LeftButton);
      send_pointer(QEvent::MouseButtonRelease, export_position, Qt::LeftButton, Qt::NoButton);
      QPointer<QObject> real_row;
      if (!wait_for_condition([&] {
            real_row = find_named_object(root, "exportQueueRow");
            return real_row && (real_row->property("statusText") == "Completed" || real_row->property("statusText") == "Failed");
          }, timeout_ms)) return fail("Real export did not reach a terminal queue state: " + export_jobs.status());
      if (real_row->property("statusText") != "Completed")
        return fail("Real export failed: " + real_row->property("errorText").toString());
      auto* real_effect = real_row->findChild<QObject*>("exportRowCompletionCaustic");
      if (!real_effect || real_effect->property("completionCount").toInt() != 1)
        return fail("Actual export completion did not trigger exactly one caustic; observed states: " + observed_export_states.join(", "));
      if (!wait_for_condition([&] { return real_effect->property("paintCount").toInt() > 0 &&
            real_effect->property("phase").toDouble() >= 0.3 && real_effect->property("running").toBool(); }, 2000))
        return fail("Actual export caustic did not paint.");
      save_capture_if_requested(root_window, optical_proof_dir, "04d-real-export-completion", &captures);
      const auto output_path = real_row->property("outputPath").toString();
      QImage exported(output_path);
      QImage original(source_path);
      if (exported.isNull() || exported.size() != original.size() ||
          !QFile::copy(output_path, QDir(optical_proof_dir).filePath("exported-fixture.png")))
        return fail("Completed export did not produce the expected full-resolution image artifact.");
      if (!wait_for_condition([&] { return !real_effect->property("running").toBool() &&
            !real_effect->property("surfaceAllocated").toBool(); }, 2000))
        return fail("Actual export completion did not release its effect surface.");
    }
    root->setProperty("currentWorkspace", root->property("libraryWorkspaceIndex"));
    process_layout_events(12);
    save_capture_if_requested(root_window, optical_proof_dir, QStringLiteral("06-library"), &captures);
    for (const auto& workspace_capture : std::vector<std::pair<QString, QString>>{
        {"carouselWorkspaceIndex", "07-carousel"}, {"timelineWorkspaceIndex", "08-timeline"},
        {"sceneWorkspaceIndex", "09-scene"}}) {
      root->setProperty("currentWorkspace", root->property(workspace_capture.first.toUtf8().constData()));
      QElapsedTimer workspace_settle;
      workspace_settle.start();
      wait_for_condition([&]() { return workspace_settle.elapsed() >= 700; }, 2000);
      process_layout_events(12);
      save_capture_if_requested(root_window, optical_proof_dir, workspace_capture.second, &captures);
      if (workspace_capture.first == "carouselWorkspaceIndex") {
        carousel_document.createCarouselWithSettings({{"carouselName", "Optical layout fixture"},
            {"documentFormat", "4x5"}, {"slideCount", 3}, {"colorSpace", "sRGB"},
            {"backgroundColor", "#fbf7f1"}, {"templateKind", "full"},
            {"primaryMediaPath", source_path}, {"primaryMediaLabel", "Generated fixture"}});
        QElapsedTimer carousel_settle;
        carousel_settle.start();
        wait_for_condition([&]() { return carousel_settle.elapsed() >= 1000; }, 2000);
        process_layout_events(12);
        save_capture_if_requested(root_window, optical_proof_dir, QStringLiteral("07b-carousel-composer"), &captures);
        auto* compact_inspector = qobject_cast<QQuickItem*>(find_named_object(root, QStringLiteral("carouselCompactInspectorButton")));
        QObject* compact_drawer = nullptr;
        if (compact_inspector && compact_inspector->isVisible()) {
          const auto position = compact_inspector->mapToScene(QPointF(compact_inspector->width() / 2,
                                                                     compact_inspector->height() / 2));
          send_pointer(QEvent::MouseButtonPress, position, Qt::LeftButton, Qt::LeftButton);
          send_pointer(QEvent::MouseButtonRelease, position, Qt::LeftButton, Qt::NoButton);
          compact_drawer = find_named_object(root, QStringLiteral("carouselCompactInspectorDrawer"));
          if (!compact_drawer || !wait_for_condition([&]() { return compact_drawer->property("opened").toBool(); }))
            return fail("Compact Carousel inspector did not open from its visible button.");
          process_layout_events(12);
          compact_inspector_verified = true;
          save_capture_if_requested(root_window, optical_proof_dir, QStringLiteral("07bb-carousel-inspector"), &captures);
        }
        auto* board_actions = root->findChild<QObject*>("carouselBoardActionsMenu");
        if (!board_actions || !QMetaObject::invokeMethod(board_actions, "openMenu"))
          return fail("Carousel board actions menu is inaccessible.");
        auto* board_popup = root->findChild<QObject*>("carouselBoardActionsMenuPopup");
        if (!board_popup || !wait_for_condition([&]() { return board_popup->property("opened").toBool(); }))
          return fail("Carousel board menu did not finish opening.");
        save_capture_if_requested(root_window, optical_proof_dir, QStringLiteral("07c-carousel-board-actions"), &captures);
        QMetaObject::invokeMethod(board_actions, "closeMenu");
        QMetaObject::invokeMethod(board_actions, "menuItemTriggered", Q_ARG(QString, QStringLiteral("range")));
        auto* board_range = root->findChild<QObject*>("carouselBoardRangeCommandDialog");
        if (!board_range || !wait_for_condition([&]() { return board_range->property("visible").toBool(); }))
          return fail("Carousel board range action did not open its dialog.");
        QMetaObject::invokeMethod(board_range, "close");
        if (compact_drawer) QMetaObject::invokeMethod(compact_drawer, "close");
      } else if (workspace_capture.first == "timelineWorkspaceIndex") {
        timeline_document.createTimelineWithSettings({{"timelineName", "Optical timeline fixture"},
            {"width", 1920}, {"height", 1080}, {"frameRate", 30}, {"audioSampleRate", 48000},
            {"colorSpace", "Rec.709"}, {"dataLevels", "legal"}});
        timeline_document.addClipFromGallery("optical-image-1", source_path, "Generated image", 5.0,
            QUrl::fromLocalFile(source_path).toString());
        timeline_document.addTitleClip();
        timeline_document.selectClip(0);
        QElapsedTimer timeline_settle;
        timeline_settle.start();
        wait_for_condition([&]() { return timeline_settle.elapsed() >= 1200; }, 2500);
        process_layout_events(12);
        save_capture_if_requested(root_window, optical_proof_dir, QStringLiteral("08b-timeline-editor"), &captures);
      }

    }

    root->setProperty("currentWorkspace", root->property("editWorkspaceIndex"));
    process_layout_events(12);
    workspace->setProperty("scopeDockVisible", false);
    if (optical_assets.size() > 1) {
      auto* strip = qobject_cast<QQuickItem*>(find_named_object(workspace, QStringLiteral("editThumbnailFilmstrip")));
      if (!strip || strip->property("contentWidth").toDouble() <= strip->width())
        return fail("Filmstrip fixture did not create horizontal overflow.");
      QMetaObject::invokeMethod(strip, "positionViewAtBeginning");
      process_layout_events(12);
      const double wheel_start = strip->property("contentX").toDouble();
      const auto wheel_point = strip->mapToScene(QPointF(strip->width() / 2, strip->height() / 2));
      QWheelEvent wheel(wheel_point, root_window->mapToGlobal(wheel_point.toPoint()),
                        QPoint(-180, 0), QPoint(-120, 0), Qt::NoButton, Qt::NoModifier,
                        Qt::NoScrollPhase, false);
      QCoreApplication::sendEvent(root_window, &wheel);
      if (!wait_for_condition([&] { return strip->property("contentX").toDouble() > wheel_start + 10; }, 2000))
        return fail("Filmstrip did not scroll in response to horizontal wheel input.");
      QMetaObject::invokeMethod(strip, "positionViewAtEnd");
      QQuickItem* last_card = nullptr;
      if (!wait_for_condition([&]() {
            auto* content = qvariant_cast<QQuickItem*>(strip->property("contentItem"));
            if (!content) return false;
            for (auto* delegate : content->childItems()) {
              auto* card = find_named_object(delegate, QStringLiteral("editFilmstripThumbnailCard"));
              if (card && card->property("assetIndex").toInt() == optical_assets.size() - 1) {
                auto* image = card->findChild<QObject*>(QStringLiteral("filmstripImage"));
                if (image && image->property("status").toInt() == 1) { last_card = qobject_cast<QQuickItem*>(card); return last_card != nullptr; }
              }
            }
            return false;
          }, timeout_ms)) {
        save_capture_if_requested(root_window, optical_proof_dir, QStringLiteral("filmstrip-loading-failure"), &captures);
        return fail(QStringLiteral("Scrolled filmstrip thumbnail did not load: x=%1 width=%2 contentWidth=%3")
                    .arg(strip->property("contentX").toDouble()).arg(strip->width()).arg(strip->property("contentWidth").toDouble()));
      }
      const auto point = last_card->mapToScene(QPointF(last_card->width() / 2, last_card->height() / 2));
      send_pointer(QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton);
      send_pointer(QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
      if (!wait_for_condition([&]() { return catalog_assets.selectedIndex() == optical_assets.size() - 1; }, timeout_ms))
        return fail("Scrolled filmstrip photo could not be selected by pointer.");
      if (!settle_preview()) return fail("Scrolled filmstrip selection did not settle.");
      filmstrip_scroll_verified = true;
      save_capture_if_requested(root_window, optical_proof_dir, QStringLiteral("01c-filmstrip-scrolled"), &captures);
      if (!QMetaObject::invokeMethod(workspace, "selectAsset", Q_ARG(QVariant, QVariant(0)),
                                     Q_ARG(QVariant, QVariant(0))) ||
          !wait_for_condition([&]() { return catalog_assets.selectedIndex() == 0; }, timeout_ms) ||
          !settle_preview()) return fail("Filmstrip could not return to the original photo.");
    }
    QVariantMap real_subject_mask;
    if (optical_real_subject_selection) {
      editor_preview.addAIMask(QStringLiteral("subject"));
      if (!wait_for_condition([&]() {
            return Access::all_idle(editor_preview);
          }, timeout_ms))
        return fail(QStringLiteral("Native Vision subject preview did not settle."));
      if (!editor_preview.hasActiveMask())
        return fail(QStringLiteral("Native Vision did not create a subject mask from the supplied real photo: %1")
                        .arg(editor_preview.previewFailureDetail()));
      real_subject_mask =
          editor_preview.maskStack().at(editor_preview.activeMaskIndex()).toMap();
      const auto mask_bytes = real_subject_mask.value(QStringLiteral("segmentationData")).toByteArray();
      const auto metadata = real_subject_mask.value(QStringLiteral("generatedMaskMetadata")).toMap();
      if (real_subject_mask.value(QStringLiteral("kind")).toString() != QStringLiteral("subject") ||
          mask_bytes.isEmpty() ||
          real_subject_mask.value(QStringLiteral("segmentationWidth")).toInt() <= 0 ||
          real_subject_mask.value(QStringLiteral("segmentationHeight")).toInt() <= 0 ||
          !metadata.value(QStringLiteral("objectAwareRefinement")).toBool() ||
          metadata.value(QStringLiteral("objectAwareRefinementRuntime")).toString().isEmpty())
        return fail(QStringLiteral("Native Vision subject mask is missing its matte or refinement metadata."));
      std::cout << "real-photo-selection-runtime="
                << editor_preview.smartSelectionRuntimeLabel().toStdString()
                << " object-aware-defaults-applied="
                << metadata.value(QStringLiteral("objectAwareDefaultsApplied")).toBool()
                << " fine-edge=" << real_subject_mask.value(QStringLiteral("selectionFineEdge")).toDouble()
                << " selection-feather=" << real_subject_mask.value(QStringLiteral("selectionFeather")).toDouble()
                << " matte=" << real_subject_mask.value(QStringLiteral("segmentationWidth")).toInt()
                << 'x' << real_subject_mask.value(QStringLiteral("segmentationHeight")).toInt()
                << '\n';
    } else {
      QImage matte(512, 512, QImage::Format_RGB32);
      for (int y = 0; y < 512; ++y) {
        for (int x = 0; x < 512; ++x) {
          const double dx = (x - 256.0) / 155.0;
          const double dy = (y - 280.0) / 230.0;
          const int alpha = static_cast<int>(255.0 * std::clamp((1.0 - dx * dx - dy * dy) * 15.0, 0.0, 1.0));
          matte.setPixel(x, y, qRgb(alpha, alpha, alpha));
        }
      }
      const auto matte_path = fixture_dir.filePath(QStringLiteral("selection-fixture.png"));
      if (!matte.save(matte_path) || !editor_preview.addAlphaImageMask(matte_path)) return fail("Selection fixture could not be created.");
      // Layout proof uses the imported matte unchanged. Expensive refinement is
      // a separate rendering/performance gate, not a prerequisite to view tools.
      if (!editor_preview.resetActiveMaskRefinement()) return fail("Imported matte refinement could not reset.");
      editor_preview.setActiveMaskFeather(0.0);
      editor_preview.setActiveMaskSelectionSmooth(0.0);
    }
    QMetaObject::invokeMethod(workspace, "activateEditTool", Q_ARG(QVariant, QStringLiteral("masks")));
    process_layout_events(12);
    send_pointer(QEvent::MouseMove, QPointF(10, 10), Qt::NoButton, Qt::NoButton);
    if (!wait_for_condition([&]() { return Access::all_idle(editor_preview); }, timeout_ms)) return fail("Selection did not settle.");
    if (!settle_preview()) return fail("Selection publication did not settle.");
    const auto selection_failure = editor_preview.previewFailureDetail();
    auto* selection_breadcrumb = workspace->findChild<QObject*>(QStringLiteral("activeEditTargetBreadcrumbLabel"));
    if (!selection_breadcrumb || !selection_breadcrumb->property("text").toString().contains(editor_preview.activeMaskName()) ||
        named_item_visible(workspace, QStringLiteral("selectedMaskHeader")))
      return fail("Selection inspector did not consolidate its active target.");
    save_capture_if_requested(root_window, optical_proof_dir, QStringLiteral("05-selection-fixture"), &captures);
    auto* mask_enabled_control = workspace->findChild<QQuickItem*>(QStringLiteral("maskFirstVisibilityButton"));
    if (!mask_enabled_control || !mask_enabled_control->property("checked").toBool())
      return fail("Mask enabled state is not exposed as a checked control.");
    const auto overlay_mode = editor_preview.maskPreviewDisplayMode();
    const auto mask_enabled_center = mask_enabled_control->mapToScene(
        QPointF(mask_enabled_control->width() / 2, mask_enabled_control->height() / 2));
    send_pointer(QEvent::MouseButtonPress, mask_enabled_center, Qt::LeftButton, Qt::LeftButton);
    send_pointer(QEvent::MouseButtonRelease, mask_enabled_center, Qt::LeftButton, Qt::NoButton);
    process_layout_events(4);
    if (editor_preview.activeMaskEnabled() || mask_enabled_control->property("checked").toBool() ||
        editor_preview.maskPreviewDisplayMode() != overlay_mode)
      return fail("Disabling a mask did not preserve its independent overlay state.");
    mask_enabled_control->forceActiveFocus(Qt::TabFocusReason);
    send_key(Qt::Key_Space);
    if (!editor_preview.activeMaskEnabled() || !mask_enabled_control->property("checked").toBool())
      return fail("Keyboard could not restore the mask enabled state.");
    if (!settle_preview()) return fail("Mask enable-state check did not settle.");
    editor_preview.setActiveMaskLocked(true);
    process_layout_events(12);
    if (mask_enabled_control->property("toggleEnabled").toBool() ||
        !named_item_visible(workspace, QStringLiteral("selectedMaskLockNotice")) ||
        !named_item_visible(workspace, QStringLiteral("selectedMaskUnlockButton")))
      return fail("Consolidated selection inspector hid its lock recovery controls.");
    if (!settle_preview()) return fail("Locked selection publication did not settle.");
    save_capture_if_requested(root_window, optical_proof_dir, QStringLiteral("05c-selection-locked"), &captures);
    auto* selection_unlock = workspace->findChild<QObject*>(QStringLiteral("selectedMaskUnlockButton"));
    if (!selection_unlock || !QMetaObject::invokeMethod(selection_unlock, "clicked") || editor_preview.activeMaskLocked())
      return fail("Selection could not unlock from its inspector notice.");
    auto* selection_options_button = workspace->findChild<QObject*>(QStringLiteral("maskRefineMoreButton"));
    if (!selection_options_button || !QMetaObject::invokeMethod(selection_options_button, "clicked"))
      return fail("Selection refinement options are inaccessible.");
    auto* selection_options = workspace->findChild<QObject*>(QStringLiteral("maskRefineOptionsPopup"));
    if (!selection_options || !wait_for_condition([&]() { return selection_options->property("opened").toBool(); }))
      return fail("Selection refinement options did not open.");
    save_capture_if_requested(root_window, optical_proof_dir, QStringLiteral("05b-selection-options"), &captures);
    QMetaObject::invokeMethod(selection_options, "close");
    const QJsonObject receipt{{"source", source_path}, {"recipePersistenceDisabled", true},
        {"platform", QGuiApplication::platformName()}, {"compactCarouselInspectorVerified", compact_inspector_verified}, {"filmstripScrollVerified", filmstrip_scroll_verified}, {"filmstripWheelVerified", filmstrip_scroll_verified}, {"wipePointerVerified", true}, {"pointerChangedExposure", true}, {"viewResetPreservedExposure", true}, {"bypassKeyboardNavigation", true},
        {"finiteCompletion", true}, {"idlePaintsStable", true}, {"exportCompletionStates", true}, {"reducedMotionSuppressed", true}, {"selectionRenderFailure", selection_failure}, {"selectionFeather", editor_preview.activeMaskFeather()},
        {"presetKeyboardVerified", true}, {"maskEnableStateVerified", true}, {"selectionSmooth", editor_preview.activeMaskSelectionSmooth()},
        {"selectionFixture", optical_real_subject_selection ? "native Apple Vision subject matte from supplied photo; generated-mask refinement defaults recorded" : "synthetic ellipse; refinement reset; not AI segmentation"},
        {"realSubjectSelection", optical_real_subject_selection},
        {"selectionRuntime", optical_real_subject_selection ? editor_preview.smartSelectionRuntimeLabel() : QString()},
        {"selectionRefinementDefaultsApplied", optical_real_subject_selection ? real_subject_mask.value(QStringLiteral("generatedMaskMetadata")).toMap().value(QStringLiteral("objectAwareDefaultsApplied")).toBool() : false},
        {"selectionFineEdge", optical_real_subject_selection ? real_subject_mask.value(QStringLiteral("selectionFineEdge")).toDouble() : 0.0},
        {"selectionFeather", optical_real_subject_selection ? real_subject_mask.value(QStringLiteral("selectionFeather")).toDouble() : 0.0},
        {"captures", captures}};
    QFile receipt_file(QDir(optical_proof_dir).filePath(QStringLiteral("receipt.json")));
    if (captures.size() != (24 + (optical_real_export ? 1 : 0) + (compact_inspector_verified ? 1 : 0) + (filmstrip_scroll_verified ? 1 : 0)) || !receipt_file.open(QIODevice::WriteOnly))
      return fail(QStringLiteral("Optical proof captures or receipt incomplete."));
    receipt_file.write(QJsonDocument(receipt).toJson());
    return EXIT_SUCCESS;
  }

  if (!recipe_clipboard_proof_dir.isEmpty()) {
    return run_photo_recipe_clipboard_proof(root_window, catalog_assets, editor_preview,
        engine, source_path, recipe_clipboard_proof_dir);
  }
  if (!capture_setup_proof_dir.isEmpty()) return run_gaussian_capture_setup_proof(root_window, capture_setup_proof_dir, timeout_ms,
      option_value(arguments, QStringLiteral("--capture-only-state")));
  if (!viewer_film_proof_dir.isEmpty()) {
#if defined(__APPLE__) && defined(PHOTO_EDITOR_ENABLE_METAL_PREVIEW_BACKEND)
    return run_gaussian_viewer_film_proof(root_window,gaussian_scene,option_value(arguments,"--scene-project"),
        viewer_film_proof_dir,option_int(arguments,"--scene-camera-index",0),timeout_ms,
        option_int(arguments,"--viewer-photo-edge",0));
#else
    return fail("The viewer film proof requires native Metal.");
#endif
  }
  if (!option_value(arguments, "--workspace-switch-proof-dir").isEmpty()) {
#if defined(__APPLE__) && defined(PHOTO_EDITOR_ENABLE_METAL_PREVIEW_BACKEND)
    catalog_assets.replaceAssetsForTesting({make_asset("workspace-switch-photo", source_path, QString())});
    catalog_assets.selectIndex(0);
    return run_workspace_switch_proof(root_window, gaussian_scene,
        option_value(arguments, "--scene-project"), option_value(arguments, "--workspace-switch-proof-dir"),
        iterations, timeout_ms, !capture_dir.isEmpty());
#else
    return fail("The workspace/scene proof requires the native Metal desktop build.");
#endif
  }

  if (arguments.contains("--library-membership-proof")) {
#include "catalog_membership_native_proof.inc"
  }
  if (arguments.contains("--library-duration-proof")) {
#include "catalog_duration_native_proof.inc"
  }
  if (arguments.contains("--library-startup-proof")) {
    QJsonArray captures;
    auto* workspace = active_workspace(root);
    auto* popup = find_named_object(root, QStringLiteral("startupLoadingPopup"));
    if (!workspace || !popup) return fail("Library startup surfaces are unavailable.");
    root->setProperty("recentUserInput", true);
    // Exercise the real asynchronous snapshot completion without using the
    // user's catalog or preferences. The helper returns one offline record.
    const auto helper_root = fixture_dir.filePath("snapshot-helper");
    QDir().mkpath(helper_root + "/rust/target/debug");
    QFile manifest(helper_root + "/rust/Cargo.toml");
    if (!manifest.open(QIODevice::WriteOnly)) return fail("Cannot create helper fixture.");
    manifest.close();
    QFile helper(helper_root + "/rust/target/debug/catalog_snapshot");
    if (!helper.open(QIODevice::WriteOnly)) return fail("Cannot create snapshot fixture.");
    helper.write("#!/bin/sh\n/bin/sleep 1\nprintf '%s' '{\"assets\":[{\"asset_id\":\"startup-fixture\",\"location\":{}}],\"drives\":[],\"projects\":[],\"review_states\":{}}'\n");
    helper.close();
    helper.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    const auto previous_root = qgetenv("PHOTO_EDITOR_REPOSITORY_ROOT");
    qputenv("PHOTO_EDITOR_REPOSITORY_ROOT", helper_root.toUtf8());
    const auto restore_root = qScopeGuard([&]() {
      if (previous_root.isEmpty()) qunsetenv("PHOTO_EDITOR_REPOSITORY_ROOT");
      else qputenv("PHOTO_EDITOR_REPOSITORY_ROOT", previous_root);
    });
    bool ready_after_publication = false;
    const auto loading_connection = QObject::connect(&catalog_assets,
        &photo_editor::desktop::CatalogAssetModel::loadingChanged, &app, [&]() {
          if (!catalog_assets.loading()) ready_after_publication = catalog_assets.assetCount() == 1;
        });
    catalog_assets.refresh();
    if (!wait_for_condition([&]() { return popup->property("opened").toBool(); }, timeout_ms))
      return fail("Startup popup did not appear while the catalog was loading.");
    save_capture_if_requested(root_window, capture_dir, "library-startup-loading", &captures);
    if (!wait_for_condition([&]() { return !catalog_assets.loading() &&
        !popup->property("visible").toBool(); }, timeout_ms) || !ready_after_publication)
      return fail("Startup loading closed before the catalog was published.");
    QObject::disconnect(loading_connection);
    catalog_assets.replaceAssetsForTesting({});
    process_layout_events(8);
    auto* empty = find_visible_named_item(workspace, "libraryEmptyState");
    auto* empty_action = find_visible_named_item(workspace, "libraryEmptyStateAction");
    if (!empty || !empty_action || named_item_visible(workspace, "libraryInspectorPanel"))
      return fail("Empty library must show an import action and hide the blank inspector.");
    save_capture_if_requested(root_window, capture_dir, "library-empty", &captures);
    if (!QMetaObject::invokeMethod(empty_action, "clicked") ||
        !workspace->property("importModeOpen").toBool())
      return fail("Empty library import action did not open Import.");
    workspace->setProperty("importModeOpen", false);

    QVector<photo_editor::desktop::CatalogAssetItem> items;
    QVector<QString> sources;
    const QImage source_image(source_path);
    for (int index = 0; index < 40; ++index) {
      auto item = make_asset(QStringLiteral("startup-photo-%1").arg(index), source_path, {});
      item.online = false;
      items.append(item);
      const auto path = fixture_dir.filePath(QStringLiteral("startup-thumb-%1.png").arg(index));
      const auto size = index % 3 == 0 ? QSize(160, 240) : index % 3 == 1 ? QSize(240, 160) : QSize(240, 180);
      if (!source_image.scaled(size, Qt::IgnoreAspectRatio).save(path))
        return fail("Cannot prepare mixed-aspect thumbnail fixture.");
      sources.append(QUrl::fromLocalFile(path).toString());
    }
    catalog_assets.replaceAssetsForTesting(items);
    catalog_assets.selectIndex(0);
    process_layout_events(8);
    for (int index = 0; index < items.size(); ++index) {
      if (!catalog_assets.applyThumbnailResultForTesting(items[index].asset_id, source_path,
          items[index].adjustment_patch_json, sources[index]))
        return fail("Delayed thumbnail result was rejected.");
    }
    auto* grid = workspace->findChild<QQuickItem*>("studioLibraryGalleryGrid");
    auto* inspector = workspace->findChild<QQuickItem*>("libraryInspectorPreview");
    if (!grid || !inspector) return fail("Loaded Library surfaces are unavailable.");
    const auto thumbnails_ready = [&]() {
      int ready = 0;
      int visible = 0;
      const auto bounds = grid->mapRectToScene(QRectF(0, 0, grid->width(), grid->height()));
      std::function<void(QQuickItem*)> visit = [&](QQuickItem* item) {
        if (item->objectName() == "libraryGalleryImage" && item_visible(item) &&
            item->mapRectToScene(QRectF(0, 0, item->width(), item->height())).intersects(bounds)) {
          ++visible;
          if (item->property("status").toInt() == 1) ++ready;
        }
        for (auto* child : item->childItems()) visit(child);
      };
      visit(grid);
      return visible > 1 && ready == visible;
    };
    if (!wait_for_condition([&]() { return thumbnails_ready() &&
        inspector->property("source").toString() == sources[0] &&
        inspector->property("status").toInt() == 1; }, timeout_ms))
      return fail("Delayed thumbnails did not populate the grid and selected inspector.");
    for (int index = 0; index < 3; ++index) {
      const double expected[] = {2.0 / 3.0, 1.5, 4.0 / 3.0};
      if (!wait_for_condition([&]() {
        QVariant ratio;
        return QMetaObject::invokeMethod(workspace, "libraryGalleryAspectRatio",
            Q_RETURN_ARG(QVariant, ratio), Q_ARG(QVariant, QVariant(index)),
            Q_ARG(QVariant, QVariant(sources[index])),
            Q_ARG(QVariant, QVariant(items[index].asset_id))) &&
            std::abs(ratio.toDouble() - expected[index]) <= 0.01;
      }, timeout_ms))
        return fail("Library layout did not discover the source thumbnail aspect ratio.");
    }
    // Intrinsic aspect discovery may rebuild rows once; idle and window resize
    // must then settle, rather than continually feeding painted size into layout.
    for (const auto width : {1600, 1437, 1600}) {
      root_window->setWidth(width);
      QElapsedTimer settle; settle.start();
      while (settle.elapsed() < 700) { QCoreApplication::processEvents(); QThread::msleep(5); }
      const auto revision = grid->property("layoutRevision").toInt();
      settle.restart();
      while (settle.elapsed() < 500) { QCoreApplication::processEvents(); QThread::msleep(5); }
      if (grid->property("layoutRevision").toInt() != revision || !thumbnails_ready())
        return fail("Mixed-aspect Library thumbnails did not settle after resize.");
    }
    save_capture_if_requested(root_window, capture_dir, "library-startup-ready", &captures);
    if (!library_qml_errors.isEmpty())
      return fail("Library startup raised QML errors: " + library_qml_errors.join('\n'));
    const QJsonObject result{{"ok", true}, {"startupReadyAfterPublication", ready_after_publication},
        {"delayedThumbnailsReady", true}, {"inspectorReady", true}, {"resizeSettled", true},
        {"assetCount", items.size()}, {"captures", captures}};
    if (!capture_dir.isEmpty()) {
      QFile receipt(QDir(capture_dir).filePath("receipt.json"));
      if (!receipt.open(QIODevice::WriteOnly)) return fail("Cannot write startup proof receipt.");
      receipt.write(QJsonDocument(result).toJson());
    }
    std::cout << QJsonDocument(result).toJson().constData();
    return EXIT_SUCCESS;
  }

#include "layer_drop_indicator_native_proof.inc"
#include "layer_move_native_proof.inc"
#include "direct_cutout_color_native_proof.inc"
#include "brush_fidelity_native_proof.inc"

  if (arguments.contains("--cutout-adjustment-proof")) {
    using namespace photo_editor::desktop;
    using Access = EditorPreviewControllerTestAccess;
    const QString id = QStringLiteral("cutout-adjustment-native");
    catalog_assets.replaceAssetsForTesting({make_asset(id, source_path)});
    catalog_assets.selectIndexWithModifiers(0, 0);
    if (!QMetaObject::invokeMethod(root, "openSelectedPhotoInEdit") ||
        !wait_for_condition([&] { return editor_preview.assetId() == id &&
            Access::all_idle(editor_preview) && active_workspace(root) &&
            active_workspace(root)->objectName() == "editStudioWorkspace"; }, timeout_ms))
      return fail("Cutout proof could not open the isolated photo in Edit.");
    auto* workspace = active_workspace(root);
    workspace->setProperty("editInspectorTab", "layers");
    editor_preview.setExposure(0.3);
    editor_preview.setTemperature(18);
    const QImage source(source_path);
    const auto render = [&] {
      return render_output_image_with_backend(source, parse_edit_recipe_json(Access::recipe_json(editor_preview)),
          QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
    };
    const auto settle = [&] {
      return wait_for_condition([&] { return Access::all_idle(editor_preview) &&
          !editor_preview.photoPaintBusy(); }, timeout_ms);
    };
    int button_clicks = 0;
    const auto click_add = [&] {
      if (!settle()) return false;
      process_layout_events(4);
      auto* button = find_named_item(workspace, "photoLayerAddSplitButton");
      if (!button || !item_visible(button)) return false;
      const auto before = render();
      const auto count = editor_preview.photoLayerCount();
      const auto point = button->mapToScene(QPointF(button->width() * 0.35, button->height() / 2));
      QTest::mouseClick(root_window, Qt::LeftButton, Qt::NoModifier, point.toPoint());
      ++button_clicks;
      return editor_preview.photoLayerCount() == count + 1 && settle() &&
          !before.isNull() && render() == before;
    };
    if (!click_add()) return fail("Visible Add Layer click must add a neutral first adjustment.");
    if (!editor_preview.setPhotoLayerAdjustmentValue(0, "exposure", 0.4) ||
        !editor_preview.setPhotoLayerAdjustmentValue(0, "temperature", -12))
      return fail("Could not configure the grade below the native cutout.");
    editor_preview.addMask("rectangle");
    editor_preview.setActiveMaskFeather(0);
    editor_preview.setActiveMaskCenterX(0.25);
    editor_preview.setActiveMaskCenterY(0.5);
    editor_preview.setActiveMaskRadiusX(0.25);
    editor_preview.setActiveMaskRadiusY(0.5);
    const auto mask = editor_preview.maskSets().value(0).toMap();
    const auto create_cutout = [&](const QString& key) {
      const auto count = editor_preview.photoLayerCount();
      if (!QMetaObject::invokeMethod(workspace, "handleMaskSetRowMenuAction",
              Q_ARG(QVariant, QVariant(key)), Q_ARG(QVariant, QVariant(mask)),
              Q_ARG(QVariant, QVariant(-1)), Q_ARG(QVariant, QVariant(-1)),
              Q_ARG(QVariant, QVariant(0))) ||
          editor_preview.photoLayerCount() != count + 1 || !settle()) return false;
      QVariant selected;
      return QMetaObject::invokeMethod(workspace, "setActivePhotoLayer", Q_RETURN_ARG(QVariant, selected),
          Q_ARG(QVariant, QVariant(0))) && selected.toBool();
    };
    QJsonArray captures;
    if (!create_cutout("copy-pixels") || !click_add())
      return fail("Linked cutout and second Add Layer click must render safely and preserve colors.");
    save_capture_if_requested(root_window, capture_dir, "linked-cutout-second-adjustment", &captures);
    if (!create_cutout("copy-independent") || !editor_preview.photoPaintError().isEmpty() || !click_add())
      return fail("Independent cutout and another Add Layer click must render safely and preserve colors.");
    save_capture_if_requested(root_window, capture_dir, "independent-cutout-new-adjustment", &captures);
    std::cout << QJsonDocument(QJsonObject{{"schema", "cutout-adjustment-native:v1"}, {"ok", true},
        {"qt_platform", QGuiApplication::platformName()}, {"isolated_data", true},
        {"native_add_layer_clicks", button_clicks}, {"linked_cutout", true}, {"independent_cutout", true},
        {"neutral_add_pixels_identical", true}, {"captures", captures}}).toJson().constData();
    return EXIT_SUCCESS;
  }

  if (arguments.contains("--library-grid-proof") || arguments.contains("--asset-selection-proof")) {
    root->setProperty("currentWorkspace", root->property("libraryWorkspaceIndex"));
    if (!wait_for_condition([&] { return active_workspace(root) != nullptr &&
        active_workspace(root)->objectName() == "libraryStudioWorkspace"; }, timeout_ms,
        "library-route-ready"))
      return fail("Library route not ready.");
    const auto count = std::clamp(option_int(arguments, "--asset-count", 2000), 20, 10000);
    const QImage library_source_image(source_path);
    if (library_source_image.isNull())
      return fail("Could not prepare the representative 240-pixel Library thumbnail fixture.");
    const auto unique_cached_thumbnail_sources =
        arguments.contains("--unique-cached-thumbnail-sources");
    const auto library_thumbnail_image = library_source_image.scaled(
        QSize(240, 160), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QVector<QString> library_thumbnail_sources;
    library_thumbnail_sources.reserve(count);
    if (unique_cached_thumbnail_sources) {
      for (int index = 0; index < count; ++index) {
        const auto library_thumbnail_path = fixture_dir.filePath(
            QStringLiteral("library-grid-thumbnail-%1.png").arg(index));
        if (!library_thumbnail_image.save(library_thumbnail_path))
          return fail("Could not write a unique Library thumbnail source.");
        library_thumbnail_sources.push_back(QUrl::fromLocalFile(library_thumbnail_path).toString());
      }
    } else {
      const auto library_thumbnail_path = fixture_dir.filePath(
          QStringLiteral("library-grid-thumbnail.png"));
      if (!library_thumbnail_image.save(library_thumbnail_path))
        return fail("Could not prepare the representative 240-pixel Library thumbnail fixture.");
      const auto shared_thumbnail_source = QUrl::fromLocalFile(library_thumbnail_path).toString();
      for (int index = 0; index < count; ++index)
        library_thumbnail_sources.push_back(shared_thumbnail_source);
    }
    QVector<photo_editor::desktop::CatalogAssetItem> items;
    QString selection_recipe;
    const auto selection_recipe_mb = std::clamp(option_int(arguments, "--selection-recipe-mb", 0), 0, 128);
    if (arguments.contains("--asset-selection-proof") && selection_recipe_mb > 0) {
      photo_editor::desktop::EditRecipeState recipe;
      QVariantMap tiles;
      for (int tile = 0; tile < selection_recipe_mb * 16; ++tile)
        tiles.insert(QStringLiteral("%1,0").arg(tile), QString(64 * 1024, QChar('A' + tile % 26)));
      recipe.layers.append(QVariantMap{{"id", "selection-paint"}, {"kind", "pixel"}, {"name", "Cutout"},
          {"paintTiles", QVariantMap{{"version", 1}, {"tiles", tiles}}}});
      selection_recipe = photo_editor::desktop::serialize_adjustment_patch_json(recipe,
          photo_editor::desktop::all_adjustment_groups(), photo_editor::desktop::AdjustmentPatchScope::AssetBound);
    }
    for (int i = 0; i < count; ++i) {
      auto item = make_asset(QStringLiteral("library-proof-%1").arg(i), source_path,
                             library_thumbnail_sources.at(i));
      if (i == 1 || i == 2) item.adjustment_patch_json = selection_recipe;
      if (unique_cached_thumbnail_sources)
        item.online = false;
      items.append(item);
    }
    QElapsedTimer timer;
    timer.start();
    catalog_assets.replaceAssetsForTesting(items);
    const auto catalog_replace_ms = timer.elapsed();
    process_layout_events(8, "library-initial-grid-populate");
    const auto initial_grid_settle_ms = timer.elapsed() - catalog_replace_ms;
    auto* workspace = active_workspace(root);
    auto* grid = workspace->findChild<QQuickItem*>("studioLibraryGalleryGrid");
    if (!grid || !grid->isVisible()) return fail("Library grid not visible.");
    QElapsedTimer gallery_assets_probe_timer;
    gallery_assets_probe_timer.start();
    const auto gallery_assets_probe = catalog_assets.galleryAssets();
    const auto gallery_assets_probe_ms = gallery_assets_probe_timer.elapsed();
    QVariant gallery_rows_probe;
    QElapsedTimer gallery_rows_probe_timer;
    gallery_rows_probe_timer.start();
    const auto gallery_rows_probe_invoked = QMetaObject::invokeMethod(
        workspace,
        "libraryGalleryRows",
        Q_RETURN_ARG(QVariant, gallery_rows_probe),
        Q_ARG(QVariant, QVariant(grid->property("galleryViewportWidth"))));
    const auto gallery_rows_probe_ms = gallery_rows_probe_timer.elapsed();
    auto tile_count = [&] {
      std::function<int(QQuickItem*)> count_tiles = [&](QQuickItem* item) {
        int found = item->objectName() == "libraryGalleryTile" ? 1 : 0;
        for (auto* child : item->childItems()) found += count_tiles(child);
        return found;
      };
      return count_tiles(grid);
    };
    auto has_tile = [&](int index) {
      std::function<bool(QQuickItem*)> visit = [&](QQuickItem* item) {
        if (item->objectName() == "libraryGalleryTile" && item->property("assetIndex").toInt() == index)
          return true;
        for (auto* child : item->childItems()) if (visit(child)) return true;
        return false;
      };
      return visit(grid);
    };
    const auto populate_ms = timer.elapsed();
    const auto initial_tiles = tile_count();
    if (!has_tile(0)) return fail("First gallery item was not instantiated.");
    if (arguments.contains("--asset-selection-proof")) {
      int opened = 0;
      const auto asset_connection = QObject::connect(&editor_preview,
          &photo_editor::desktop::EditorPreviewController::assetChanged, [&] { ++opened; });
      const auto cleanup = qScopeGuard([&] { QObject::disconnect(asset_connection); });
      QJsonArray measurements;
      QJsonArray captures;
      const auto click = [&](QQuickItem* item, bool double_click) {
        if (!item || !item->isVisible()) return false;
        const auto point = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
        QElapsedTimer click_timer; click_timer.start();
        if (double_click)
          QTest::mouseDClick(root_window, Qt::LeftButton, Qt::NoModifier, point.toPoint(), 10);
        else
          QTest::mouseClick(root_window, Qt::LeftButton, Qt::NoModifier, point.toPoint());
        const auto dispatch_ms = static_cast<double>(click_timer.nsecsElapsed()) / 1e6;
        double paint_feedback_ms = 0;
        if (arguments.contains("--selection-frame-proof")) {
          QEventLoop paint_loop;
          QTimer paint_timeout;
          paint_timeout.setSingleShot(true);
          int presented_frames = 0;
          const auto paint_connection = QObject::connect(root_window, &QQuickWindow::frameSwapped,
              &paint_loop, [&] {
                if (++presented_frames >= 2) paint_loop.quit();
                else root_window->update();
              });
          QObject::connect(&paint_timeout, &QTimer::timeout, &paint_loop, &QEventLoop::quit);
          paint_timeout.start(1000);
          root_window->update();
          paint_loop.exec();
          QObject::disconnect(paint_connection);
          if (presented_frames < 2) return false;
          paint_feedback_ms = static_cast<double>(click_timer.nsecsElapsed()) / 1e6;
        }
        process_layout_events(3, "asset-selection-click");
        measurements.append(QJsonObject{{"target", item->objectName()},
            {"double_click", double_click}, {"dispatch_ms", dispatch_ms}, {"paint_feedback_ms", paint_feedback_ms}});
        return true;
      };
      std::function<QQuickItem*(QQuickItem*, int)> tile_at = [&](QQuickItem* item, int index) -> QQuickItem* {
        if (item->objectName() == "libraryGalleryTile" && item->property("assetIndex").toInt() == index)
          return item;
        for (auto* child : item->childItems()) if (auto* found = tile_at(child, index)) return found;
        return nullptr;
      };
      const auto original_asset = editor_preview.assetId();
      const auto original_opens = opened;
      if (arguments.contains("--selection-only")) {
        double max_dispatch_ms = 0;
        double max_paint_feedback_ms = 0;
        for (const auto index : {1, 0, 2, 0, 1}) {
          if (!click(tile_at(grid, index), false) || catalog_assets.selectedIndex() != index ||
              editor_preview.assetId() != original_asset || opened != original_opens)
            return fail("Selection latency fixture must select without opening any image.");
          max_dispatch_ms = std::max(max_dispatch_ms, measurements.last().toObject().value("dispatch_ms").toDouble());
          max_paint_feedback_ms = std::max(max_paint_feedback_ms, measurements.last().toObject().value("paint_feedback_ms").toDouble());
        }
        const auto budget_ms = option_int(arguments, "--selection-budget-ms", 0);
        const auto paint_budget_ms = option_int(arguments, "--selection-paint-budget-ms", 0);
        const auto within_budget = (budget_ms <= 0 || max_dispatch_ms < budget_ms) &&
            (paint_budget_ms <= 0 || max_paint_feedback_ms < paint_budget_ms);
        std::cout << QJsonDocument(QJsonObject{{"schema", "selection-latency-proof:v1"}, {"ok", within_budget},
            {"recipe_mb", selection_recipe_mb}, {"max_dispatch_ms", max_dispatch_ms}, {"budget_ms", budget_ms},
            {"max_paint_feedback_ms", max_paint_feedback_ms}, {"paint_budget_ms", paint_budget_ms},
            {"opens", opened - original_opens}, {"measurements", measurements}}).toJson().constData();
        return within_budget ? EXIT_SUCCESS : EXIT_FAILURE;
      }
      if (!click(tile_at(grid, 1), false) || catalog_assets.selectedIndex() != 1 ||
          editor_preview.assetId() != original_asset || opened != original_opens)
        return fail("A Library single click must select without opening or replacing the editor.");
      save_capture_if_requested(root_window, capture_dir, "library-selection", &captures);
      if (!click(tile_at(grid, 1), true) || !wait_for_condition([&] {
            return editor_preview.assetId() == "library-proof-1" &&
                root->property("currentWorkspace") == root->property("editWorkspaceIndex");
          }, timeout_ms, "library-double-click-open"))
        return fail("A Library double click must open the selected photo in Edit.");
      workspace = active_workspace(root);
      if (!wait_for_condition([&] { return !root->property("workspaceTransitionActive").toBool() &&
          workspace->property("pageContentReady").toBool(); }, timeout_ms, "filmstrip-selection-ready"))
        return fail("Edit did not finish opening before the filmstrip selection test.");
      workspace->setProperty("editFilmstripCollapsed", false);
      process_layout_events(8, "filmstrip-visible");
      std::function<QQuickItem*(QQuickItem*)> find_next_card = [&](QQuickItem* item) -> QQuickItem* {
        if (item->objectName() == "editFilmstripThumbnailCard" && item->property("assetIndex").toInt() == 2)
          return item;
        for (auto* child : item->childItems()) if (auto* found = find_next_card(child)) return found;
        return nullptr;
      };
      auto* next_card = find_next_card(qobject_cast<QQuickItem*>(workspace));
      const auto filmstrip_opens = opened;
      if (!click(next_card, false) || catalog_assets.selectedIndex() != 2 ||
          editor_preview.assetId() != "library-proof-1" || opened != filmstrip_opens) {
        std::cerr << "Filmstrip selection: selected=" << catalog_assets.selectedIndex()
                  << " asset=" << editor_preview.assetId().toStdString() << " opens=" << opened - filmstrip_opens
                  << " card=" << (next_card != nullptr) << '\n';
        return fail("A filmstrip single click must leave the active editor document untouched.");
      }
      QVariant export_payload;
      if (!QMetaObject::invokeMethod(root, "currentExportPayload", Q_RETURN_ARG(QVariant, export_payload)) ||
          export_payload.toMap().value("assetId").toString() != "library-proof-1")
        return fail("Export must still target the open image after selecting another thumbnail.");
      save_capture_if_requested(root_window, capture_dir, "filmstrip-selection", &captures);
      if (!click(next_card, true) || !wait_for_condition([&] {
            return editor_preview.assetId() == "library-proof-2";
          }, timeout_ms, "filmstrip-double-click-open"))
        return fail("A filmstrip double click must open the newly selected photo.");
      QVariant boards_added;
      if (!QMetaObject::invokeMethod(root, "addBoardsFromCurrentEdit", Q_RETURN_ARG(QVariant, boards_added)) ||
          !boards_added.toBool()) return fail("Could not prepare the isolated board-selection fixture.");
      QVariant board_added;
      if (!QMetaObject::invokeMethod(root, "addEditBoard", Q_RETURN_ARG(QVariant, board_added),
              Q_ARG(QVariant, false)) || !board_added.toBool()) return fail("Could not add the second board.");
      process_layout_events(8, "board-selection-fixture");
      workspace = active_workspace(root);
      if (!wait_for_condition([&] { return !root->property("workspaceTransitionActive").toBool() &&
          !workspace->property("editBoardSwitchBusy").toBool(); }, timeout_ms, "board-selection-ready"))
        return fail("Board fixture did not finish opening before its selection test.");
      const auto loaded_board = editor_preview.assetId();
      const auto board_opens = opened;
      if (!click(find_named_item(workspace, "editCanvasBoardHeaderSelect0"), false) ||
          workspace->property("selectedEditBoardIndex").toInt() != 0 ||
          editor_preview.assetId() != loaded_board || opened != board_opens || carousel_document.currentIndex() != 1) {
        auto* area = find_named_item(workspace, "editCanvasBoardHeaderSelect0");
        std::cerr << "Board selection: selected=" << workspace->property("selectedEditBoardIndex").toInt()
                  << " loaded=" << editor_preview.assetId().toStdString() << " expected=" << loaded_board.toStdString()
                  << " opens=" << opened - board_opens << " current=" << carousel_document.currentIndex()
                  << " area=" << (area != nullptr) << " visible=" << (area && area->isVisible()) << '\n';
        QJsonArray failure_captures;
        save_capture_if_requested(root_window, capture_dir, "board-selection-failure", &failure_captures);
        return fail("A board single click must highlight without loading or changing the edit target.");
      }
      auto* first_board = find_named_item(workspace, "editCanvasBoard0");
      auto* second_board = find_named_item(workspace, "editCanvasBoard1");
      if (!first_board || !first_board->property("highlightedBoard").toBool() ||
          !second_board || second_board->property("highlightedBoard").toBool())
        return fail("Board highlighting must follow selection while its edit target stays unchanged.");
      if (!capture_dir.isEmpty()) {
        QElapsedTimer highlight_timer; highlight_timer.start();
        wait_for_condition([&] { return highlight_timer.elapsed() >= 250; }, 1000, "board-highlight-frame");
      }
      save_capture_if_requested(root_window, capture_dir, "board-selection", &captures);
      if (!click(find_named_item(workspace, "editCanvasBoardHeaderSelect0"), true) ||
          !wait_for_condition([&] { return carousel_document.currentIndex() == 0 &&
              editor_preview.assetId() != loaded_board; }, timeout_ms, "board-double-click-open"))
        return fail("A board double click must open that board for editing.");
      root->setProperty("editBoardsActive", false);
      root->setProperty("carouselPhotoEditActive", false);
      carousel_document.createCarouselWithSettings({{"carouselName", "Media activation fixture"},
          {"documentFormat", "4x5"}, {"slideCount", 1}, {"templateKind", "full"}});
      QQmlComponent carousel_component(&engine, QUrl::fromLocalFile(
          QDir(QFileInfo(main_qml_path).absolutePath()).filePath("workspaces/CarouselView.qml")));
      std::unique_ptr<QObject> carousel_object(carousel_component.createWithInitialProperties(
          {{"integratedEdit", true}, {"width", root_window->width()}, {"height", root_window->height()}}));
      auto* carousel_view = qobject_cast<QQuickItem*>(carousel_object.get());
      if (!carousel_view) return fail("Could not create the Carousel media activation fixture.");
      carousel_view->setParentItem(root_window->contentItem());
      carousel_view->setZ(1000);
      carousel_view->setProperty("mediaPickerAssets", QVariantList{QVariantMap{
          {"assetId", "activation-photo"}, {"mediaType", "photo"}, {"online", true},
          {"filePath", source_path}, {"thumbnailSource", QUrl::fromLocalFile(source_path).toString()}}});
      process_layout_events(8, "carousel-media-fixture");
      auto* media_thumb = find_named_item(carousel_view, "carouselQuickLibraryAssetactivation-photo");
      if (!media_thumb) return fail("Carousel quick-library thumbnail was not instantiated.");
      // Isolate the production delegate from inspector scrolling; retain its
      // original QML creation context and handlers to test real click routing.
      media_thumb->setParentItem(carousel_view);
      media_thumb->setPosition(QPointF(20, 20));
      media_thumb->setZ(2000);
      media_thumb->setVisible(true);
      if (!click(media_thumb, false) ||
          carousel_view->property("selectedQuickLibraryAssetId").toString() != "activation-photo" ||
          carousel_document.selectionDetails().value("hasMedia").toBool())
        return fail("A Carousel media single click must select without placing or loading the image.");
      if (!click(media_thumb, true) ||
          !carousel_document.selectionDetails().value("hasMedia").toBool())
        return fail("A Carousel media double click must place the selected image.");
      std::cout << QJsonDocument(QJsonObject{{"schema", "asset-selection-proof:v1"}, {"ok", true},
          {"library_select_only", true}, {"filmstrip_select_only", true}, {"board_select_only", true},
          {"carousel_media_select_only", true}, {"export_keeps_open_image", true},
          {"double_click_open", true}, {"isolated_data", true}, {"measurements", measurements},
          {"captures", captures}}).toJson().constData();
      return EXIT_SUCCESS;
    }
    QJsonArray captures;
    auto* app_navigation_bar = root->findChild<QQuickItem*>(QStringLiteral("appNavigationBar"));
    auto* main_workspace_surface = root->findChild<QQuickItem*>(QStringLiteral("mainWorkspaceSurface"));
    if (!app_navigation_bar || !main_workspace_surface)
      return fail("Library scroll proof requires the shell navigation and workspace surface.");
    const auto app_navigation_y_before_scroll = app_navigation_bar->mapRectToScene(
        QRectF(0, 0, app_navigation_bar->width(), app_navigation_bar->height())).y();
    const auto workspace_surface_y_before_scroll = main_workspace_surface->mapRectToScene(
        QRectF(0, 0, main_workspace_surface->width(), main_workspace_surface->height())).y();
    const auto grid_y_before_scroll = grid->mapRectToScene(
        QRectF(0, 0, grid->width(), grid->height())).y();
    save_capture_if_requested(root_window, capture_dir, "library-top-before-scroll", &captures);
    // ListView refines its estimated content height as the final, shorter row
    // is instantiated. Compare against the current scroll limit each frame.
    const auto scroll_end = [&]() {
      return std::max(0.0, grid->property("contentHeight").toDouble() - grid->height());
    };
    const auto layout_revision_before_scroll = grid->property("layoutRevision").toInt();
    std::function<QQuickItem*(QQuickItem*, int)> find_tile_by_index = [&](QQuickItem* item, int index) -> QQuickItem* {
      if (item->objectName() == "libraryGalleryTile" && item->property("assetIndex").toInt() == index)
        return item;
      for (auto* child : item->childItems()) {
        if (auto* found = find_tile_by_index(child, index))
          return found;
      }
      return nullptr;
    };
    const auto visible_thumbnail_readiness = [&]() {
      QPair<int, int> counts{0, 0};
      const auto viewport_bounds = grid->mapRectToScene(QRectF(0, 0, grid->width(), grid->height()));
      std::function<void(QQuickItem*)> visit = [&](QQuickItem* item) {
        if (item->objectName() == "libraryGalleryTile" && item->isVisible()) {
          const auto tile_bounds = item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
          if (tile_bounds.intersects(viewport_bounds)) {
            ++counts.first;
            std::function<QQuickItem*(QQuickItem*)> find_image = [&](QQuickItem* parent) -> QQuickItem* {
              for (auto* child : parent->childItems()) {
                if (child->objectName() == "libraryGalleryImage")
                  return child;
                if (auto* nested = find_image(child))
                  return nested;
              }
              return nullptr;
            };
            if (auto* image = find_image(item); image != nullptr && image->property("status").toInt() == 1)
              ++counts.second;
          }
        }
        for (auto* child : item->childItems())
          visit(child);
      };
      visit(grid);
      return counts;
    };
    QPointer<QQuickItem> first_tile_before_scroll(find_tile_by_index(grid, 0));
    if (first_tile_before_scroll.isNull())
      return fail("First library tile disappeared before wheel input started.");
    const auto wheel_point = grid->mapToScene(QPointF(grid->width() / 2.0, grid->height() / 2.0));
    const auto wheel_global_point = root_window->mapToGlobal(wheel_point.toPoint());
    std::vector<double> wheel_event_to_frame_samples;
    wheel_event_to_frame_samples.reserve(256);
    std::vector<double> wheel_dispatch_samples;
    wheel_dispatch_samples.reserve(256);
    QElapsedTimer wheel_heartbeat_clock;
    wheel_heartbeat_clock.start();
    qint64 wheel_heartbeat_last_ms = wheel_heartbeat_clock.elapsed();
    qint64 wheel_event_heartbeat_max_gap_ms = 0;
    bool wheel_event_heartbeat_active = false;
    QTimer wheel_heartbeat;
    wheel_heartbeat.setInterval(1);
    const auto heartbeat_connection = QObject::connect(&wheel_heartbeat, &QTimer::timeout,
        root_window, [&]() {
          const auto now_ms = wheel_heartbeat_clock.elapsed();
          if (wheel_event_heartbeat_active)
            wheel_event_heartbeat_max_gap_ms = std::max(
                wheel_event_heartbeat_max_gap_ms, now_ms - wheel_heartbeat_last_ms);
          wheel_heartbeat_last_ms = now_ms;
        });
    wheel_heartbeat.start();
    timer.restart();
    int wheel_event_count = 0;
    bool first_tile_retained_after_first_wheel = false;
    int slowest_wheel_event_index = 0;
    double slowest_wheel_event_content_y = 0.0;
    double slowest_wheel_event_ms = -1.0;
    int visible_thumbnail_frame_count = 0;
    int visible_thumbnail_ready_frame_count = 0;
    int visible_thumbnail_asset_samples = 0;
    int visible_thumbnail_ready_asset_samples = 0;
    while (grid->property("contentY").toDouble() < scroll_end() - 1.0 && wheel_event_count < 512) {
      const auto content_y_before = grid->property("contentY").toDouble();
      wheel_event_heartbeat_max_gap_ms = 0;
      wheel_heartbeat_last_ms = wheel_heartbeat_clock.elapsed();
      wheel_event_heartbeat_active = true;
      QElapsedTimer wheel_event_timer;
      wheel_event_timer.start();
      QWheelEvent wheel(wheel_point, wheel_global_point, QPoint(0, -180), QPoint(0, -120),
                        Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
      QEventLoop frame_loop;
      QTimer frame_timeout;
      frame_timeout.setSingleShot(true);
      bool wheel_frame_presented = false;
      bool frame_timed_out = false;
      const auto frame_connection = QObject::connect(
          root_window, &QQuickWindow::frameSwapped, &frame_loop, [&]() {
            wheel_frame_presented = true;
            frame_loop.quit();
          });
      const auto timeout_connection = QObject::connect(
          &frame_timeout, &QTimer::timeout, &frame_loop, [&]() {
            frame_timed_out = true;
            frame_loop.quit();
          });
      frame_timeout.start(1000);
      QElapsedTimer wheel_dispatch_timer;
      wheel_dispatch_timer.start();
      QCoreApplication::sendEvent(root_window, &wheel);
      wheel_dispatch_samples.push_back(static_cast<double>(wheel_dispatch_timer.elapsed()));
      ++wheel_event_count;
      if (!wheel_frame_presented)
        frame_loop.exec();
      frame_timeout.stop();
      QObject::disconnect(frame_connection);
      QObject::disconnect(timeout_connection);
      const auto heartbeat_tail_gap_ms = wheel_heartbeat_clock.elapsed() - wheel_heartbeat_last_ms;
      wheel_event_heartbeat_max_gap_ms = std::max(wheel_event_heartbeat_max_gap_ms,
                                                   heartbeat_tail_gap_ms);
      wheel_event_heartbeat_active = false;
      const auto wheel_event_moved_content = grid->property("contentY").toDouble() > content_y_before + 0.5;
      if (!wheel_event_moved_content || !wheel_frame_presented) {
        save_capture_if_requested(root_window, capture_dir, "library-wheel-failure", nullptr);
        const auto grid_bounds = grid->mapRectToScene(QRectF(0, 0, grid->width(), grid->height()));
        std::cerr << "Library wheel diagnostic: accepted=" << wheel.isAccepted()
                  << " before=" << content_y_before
                  << " after=" << grid->property("contentY").toDouble()
                  << " end=" << scroll_end()
                  << " gridVisible=" << grid->isVisible()
                  << " gridEnabled=" << grid->isEnabled()
                  << " frameTimedOut=" << frame_timed_out
                  << " gridRect=" << grid_bounds.x() << ',' << grid_bounds.y() << ','
                  << grid_bounds.width() << ',' << grid_bounds.height()
                  << " wheelPos=" << wheel_point.x() << ',' << wheel_point.y()
                  << " window=" << root_window->width() << 'x' << root_window->height() << '\n';
        return fail("Library grid wheel input did not move content and present a new frame.");
      }
      const auto event_to_frame_ms = static_cast<double>(wheel_event_timer.elapsed());
      wheel_event_to_frame_samples.push_back(event_to_frame_ms);
      const auto thumbnail_readiness = visible_thumbnail_readiness();
      if (thumbnail_readiness.first > 0) {
        ++visible_thumbnail_frame_count;
        visible_thumbnail_asset_samples += thumbnail_readiness.first;
        visible_thumbnail_ready_asset_samples += thumbnail_readiness.second;
        if (thumbnail_readiness.second > 0)
          ++visible_thumbnail_ready_frame_count;
      }
      if (wheel_event_count == 1)
        first_tile_retained_after_first_wheel = !first_tile_before_scroll.isNull() &&
            first_tile_before_scroll->property("assetIndex").toInt() == 0;
      if (event_to_frame_ms > slowest_wheel_event_ms) {
        slowest_wheel_event_ms = event_to_frame_ms;
        slowest_wheel_event_index = wheel_event_count;
        slowest_wheel_event_content_y = grid->property("contentY").toDouble();
      }
      if (event_to_frame_ms > 50.0) {
        std::cerr << "Library slow wheel detail: event=" << wheel_event_count
                  << " eventToFrame=" << event_to_frame_ms
                  << "ms dispatch=" << wheel_dispatch_samples.back()
                  << "ms guiHeartbeatGap=" << wheel_event_heartbeat_max_gap_ms
                  << "ms contentY=" << grid->property("contentY").toDouble()
                  << "/" << scroll_end()
                  << " listCount=" << grid->property("count").toInt()
                  << " currentIndex=" << grid->property("currentIndex").toInt()
                  << " tiles=" << tile_count()
                  << " layoutRevision=" << grid->property("layoutRevision").toInt()
                  << " contentHeight=" << grid->property("contentHeight").toDouble()
                  << " editLoaded=" << root->property("editWorkspaceLoaded").toBool()
                  << " carouselLoaded=" << root->property("carouselWorkspaceLoaded").toBool()
                  << " timelineLoaded=" << root->property("timelineWorkspaceLoaded").toBool()
                  << " exportLoaded=" << root->property("exportWorkspaceLoaded").toBool()
                  << " qmlFrameMax=" << root->property("qmlFrameMaxIntervalMs").toDouble()
                  << " qmlFrameJank=" << root->property("qmlFrameJankCount").toInt()
                  << '\n';
      }
    }
    QObject::disconnect(heartbeat_connection);
    const auto scroll_ms = timer.elapsed();
    const auto app_navigation_y_after_scroll = app_navigation_bar->mapRectToScene(
        QRectF(0, 0, app_navigation_bar->width(), app_navigation_bar->height())).y();
    const auto workspace_surface_y_after_scroll = main_workspace_surface->mapRectToScene(
        QRectF(0, 0, main_workspace_surface->width(), main_workspace_surface->height())).y();
    const auto grid_y_after_scroll = grid->mapRectToScene(
        QRectF(0, 0, grid->width(), grid->height())).y();
    if (!app_navigation_bar->isVisible() ||
        std::abs(app_navigation_y_after_scroll - app_navigation_y_before_scroll) > 1.0 ||
        std::abs(workspace_surface_y_after_scroll - workspace_surface_y_before_scroll) > 1.0 ||
        std::abs(grid_y_after_scroll - grid_y_before_scroll) > 1.0)
      return fail("Library wheel input moved or hid the shell navigation instead of only scrolling the gallery.");
    const auto wheel_event_summary = summarize_samples(wheel_event_to_frame_samples);
    const auto wheel_dispatch_summary = summarize_samples(wheel_dispatch_samples);
    QElapsedTimer bottom_thumbnail_readiness_timer;
    bottom_thumbnail_readiness_timer.start();
    QPair<int, int> bottom_thumbnail_readiness;
    const auto bottom_visible_thumbnails_ready = wait_for_condition([&]() {
      bottom_thumbnail_readiness = visible_thumbnail_readiness();
      return bottom_thumbnail_readiness.first > 0 &&
             bottom_thumbnail_readiness.second == bottom_thumbnail_readiness.first;
    }, timeout_ms, "library-bottom-visible-thumbnails");
    const auto bottom_thumbnail_readiness_ms = bottom_thumbnail_readiness_timer.elapsed();
    const auto scroll_layout_rebuilds = grid->property("layoutRevision").toInt() - layout_revision_before_scroll;
    if (grid->property("contentY").toDouble() < scroll_end() - 1.0)
      return fail("Library grid wheel input did not reach the final virtualized row.");
    if (scroll_layout_rebuilds != 0)
      return fail("Default 3:2 thumbnail loads should not rebuild every gallery row while scrolling.");
    if (visible_thumbnail_frame_count == 0 || visible_thumbnail_ready_frame_count == 0 ||
        !bottom_visible_thumbnails_ready) {
      std::cerr << "Library thumbnail readiness: frames=" << visible_thumbnail_frame_count
                << " framesWithReady=" << visible_thumbnail_ready_frame_count
                << " readySamples=" << visible_thumbnail_ready_asset_samples << "/"
                << visible_thumbnail_asset_samples
                << " bottomReady=" << bottom_thumbnail_readiness.second << "/"
                << bottom_thumbnail_readiness.first
                << " bottomWaitMs=" << bottom_thumbnail_readiness_ms
                << " activeWorkers=" << catalog_assets.activeThumbnailWorkerCount()
                << " pendingWorkers=" << catalog_assets.pendingThumbnailWorkerCount() << '\n';
      return fail("The Library should present ready thumbnails during wheel scrolling and finish the visible row after motion stops.");
    }
    if (wheel_event_summary.p95_ms > 50.0 || wheel_event_summary.max_ms > 100.0) {
      std::cerr << "Library wheel latency: p95=" << wheel_event_summary.p95_ms
                << "ms max=" << wheel_event_summary.max_ms
                << "ms dispatchP95=" << wheel_dispatch_summary.p95_ms
                << "ms dispatchMax=" << wheel_dispatch_summary.max_ms
                << "ms slowestEvent=" << slowest_wheel_event_index
                << " contentY=" << slowest_wheel_event_content_y
                << " rowRebuilds=" << scroll_layout_rebuilds
                << " firstTileRetained=" << first_tile_retained_after_first_wheel << '\n';
      return fail("Library grid wheel-to-present latency exceeded the 50/100 ms p95/max budget.");
    }
    const auto end_tiles = tile_count();
    if (!has_tile(count - 1)) return fail("Scrolling failed to instantiate the last gallery item.");
    auto publish_images = [&](int first, int last) {
      for (int index = first; index <= last; ++index) {
        const auto& item = items[index];
        const auto patch = catalog_assets.assetById(item.asset_id).value("adjustmentPatchJson").toString();
        if (!catalog_assets.applyThumbnailResultForTesting(item.asset_id, item.file_path, patch,
            item.thumbnail_source)) return false;
      }
      return wait_for_condition([&] {
        std::function<int(QQuickItem*)> ready_images = [&](QQuickItem* item) {
          int found = item->objectName() == "libraryGalleryImage" && item->property("status").toInt() == 1 ? 1 : 0;
          for (auto* child : item->childItems()) found += ready_images(child);
          return found;
        };
        return ready_images(grid) > 0;
      }, timeout_ms, first == 0 ? "library-top-thumb-publication"
                                : "library-bottom-thumb-publication");
    };
    if (!publish_images(std::max(0, count - 40), count - 1)) return fail("End thumbnails did not become ready.");
    save_capture_if_requested(root_window, capture_dir, "library-end", &captures);
    grid->setProperty("contentY", 0.0);
    process_layout_events(4, "library-return-to-top");
    if (!publish_images(0, std::min(39, count - 1))) return fail("Start thumbnails did not become ready.");
    process_layout_events(8, "library-top-thumb-settle");
    std::function<QQuickItem*(QQuickItem*)> first_tile = [&](QQuickItem* item) -> QQuickItem* {
      if (item->objectName() == "libraryGalleryTile" && item->property("assetIndex").toInt() == 0) return item;
      for (auto* child : item->childItems()) if (auto* found = first_tile(child)) return found;
      return nullptr;
    };
    QPointer<QQuickItem> retained_tile(first_tile(grid));
    const auto layout_revision = grid->property("layoutRevision").toInt();
    timer.restart();
    for (int update = 0; update < 30; ++update) {
      catalog_assets.dataChanged(catalog_assets.index(0, 0), catalog_assets.index(0, 0),
          {photo_editor::desktop::CatalogAssetModel::RatingRole});
      process_layout_events(2, "library-metadata-update");
    }
    const auto update_ms = timer.elapsed();
    const auto delegate_retained = !retained_tile.isNull();
    const auto layout_rebuilds = grid->property("layoutRevision").toInt() - layout_revision;
    if (arguments.contains("--require-incremental") && (!delegate_retained || layout_rebuilds != 0))
      return fail("A metadata-only update recreated gallery delegates.");
    if (!has_tile(0)) return fail("Scrolling back lost the first gallery item.");
    if (!QMetaObject::invokeMethod(workspace, "selectAsset", Q_ARG(QVariant, QVariant(0)),
        Q_ARG(QVariant, QVariant(0))) || catalog_assets.selectedIndex() != 0)
      return fail("Virtualized gallery selection did not preserve the asset index.");
    catalog_assets.setSelectedRating(4);
    process_layout_events(4, "library-rating-update");
    if (!first_tile(grid) || first_tile(grid)->property("rating").toInt() != 4)
      return fail("An incremental rating update did not reach the visible tile.");
    catalog_assets.toggleSelectedPick();
    process_layout_events(4, "library-pick-update");
    if (!first_tile(grid)->property("picked").toBool())
      return fail("An incremental pick update did not reach the visible tile.");
    QElapsedTimer filter_apply_timer;
    filter_apply_timer.start();
    catalog_assets.setMinimumRatingFilter(4);
    const auto filter_model_update_ms = filter_apply_timer.elapsed();
    process_layout_events(6, "library-filter-apply");
    const auto filter_apply_ms = filter_apply_timer.elapsed();
    if (catalog_assets.assetCount() != 1 || !first_tile(grid) ||
        first_tile(grid)->property("assetId").toString() != items[0].asset_id)
      return fail("Rating filter did not rebuild the gallery with the correct identity.");
    QElapsedTimer filter_clear_timer;
    filter_clear_timer.start();
    catalog_assets.setMinimumRatingFilter(0);
    process_layout_events(6, "library-filter-clear");
    const auto filter_clear_ms = filter_clear_timer.elapsed();
    if (catalog_assets.assetCount() != count || !first_tile(grid) ||
        first_tile(grid)->property("rating").toInt() != 4)
      return fail("Clearing the filter lost the incrementally updated rating.");
    if (!QMetaObject::invokeMethod(workspace, "selectAsset", Q_ARG(QVariant, QVariant(1)),
        Q_ARG(QVariant, QVariant(0)))) return fail("Gallery selection could not move.");
    process_layout_events(4);
    if (first_tile(grid)->property("batchSelected").toBool())
      return fail("Gallery selection highlight was stale after selection moved.");

    if (arguments.contains("--require-virtualized") &&
        (initial_tiles <= 0 || initial_tiles > 100 || end_tiles <= 0 || end_tiles > 100))
      return fail("Library created an unbounded number of delegates.");
    const QJsonObject result{{"assets", count}, {"populate_ms", populate_ms},
        {"catalog_replace_ms", catalog_replace_ms},
        {"initial_grid_settle_ms", initial_grid_settle_ms},
        {"gallery_assets_probe_count", gallery_assets_probe.size()},
        {"gallery_assets_probe_ms", gallery_assets_probe_ms},
        {"gallery_rows_probe_invoked", gallery_rows_probe_invoked},
        {"gallery_rows_probe_ms", gallery_rows_probe_ms},
        {"scroll_ms", scroll_ms}, {"wheel_event_count", wheel_event_count},
        {"wheel_event_to_frame_p95_ms", wheel_event_summary.p95_ms},
        {"wheel_event_to_frame_max_ms", wheel_event_summary.max_ms},
        {"wheel_dispatch_p95_ms", wheel_dispatch_summary.p95_ms},
        {"wheel_dispatch_max_ms", wheel_dispatch_summary.max_ms},
        {"slowest_wheel_event_index", slowest_wheel_event_index},
        {"slowest_wheel_event_content_y", slowest_wheel_event_content_y},
        {"visible_thumbnail_frame_count", visible_thumbnail_frame_count},
        {"visible_thumbnail_ready_frame_count", visible_thumbnail_ready_frame_count},
        {"visible_thumbnail_asset_samples", visible_thumbnail_asset_samples},
        {"visible_thumbnail_ready_asset_samples", visible_thumbnail_ready_asset_samples},
        {"visible_thumbnail_readiness_ratio", visible_thumbnail_asset_samples > 0
            ? static_cast<double>(visible_thumbnail_ready_asset_samples) / visible_thumbnail_asset_samples
            : 0.0},
        {"bottom_visible_thumbnail_count", bottom_thumbnail_readiness.first},
        {"bottom_visible_thumbnail_ready_count", bottom_thumbnail_readiness.second},
        {"bottom_thumbnail_readiness_ms", bottom_thumbnail_readiness_ms},
        {"unique_cached_thumbnail_sources", unique_cached_thumbnail_sources},
        {"first_tile_retained_after_first_wheel", first_tile_retained_after_first_wheel},
        {"scroll_layout_rebuilds", scroll_layout_rebuilds},
        {"initial_tiles", initial_tiles}, {"end_tiles", end_tiles},
        {"metadata_update_ms", update_ms}, {"delegate_retained", delegate_retained},
        {"filter_model_update_ms", filter_model_update_ms},
        {"filter_apply_ms", filter_apply_ms}, {"filter_clear_ms", filter_clear_ms},
        {"layout_rebuilds", layout_rebuilds}, {"captures", captures}};
    if (!capture_dir.isEmpty()) {
      if (captures.size() != 2)
        return fail("Large-catalog visual proof did not capture both the top and bottom gallery states.");
      QFile receipt_file(QDir(capture_dir).filePath(QStringLiteral("receipt.json")));
      if (!receipt_file.open(QIODevice::WriteOnly))
        return fail("Large-catalog visual receipt could not be written.");
      receipt_file.write(QJsonDocument(result).toJson());
    }
    std::cout << QJsonDocument(result).toJson().constData();
    return EXIT_SUCCESS;
  }

  if (!similarity_proof_dir.isEmpty()) {
    if (!QDir().mkpath(similarity_proof_dir)) return fail("Similarity proof output unavailable.");
    QFile manifest_file(option_value(arguments, "--search-fixtures"));
    if (!manifest_file.open(QIODevice::ReadOnly) || manifest_file.size() > 1024 * 1024)
      return fail("Similarity proof requires the explicit photo fixture manifest.");
    const auto images = QJsonDocument::fromJson(manifest_file.readAll()).object().value("images").toArray();
    if (images.size() < 2 || images.size() > 64) return fail("Invalid similarity proof scope.");
    QVector<photo_editor::desktop::CatalogAssetItem> items;
    for (const auto& value : images) {
      const auto image = value.toObject();
      items.append(make_asset(image.value("id").toString(), image.value("path").toString(), QString()));
    }
    catalog_assets.setInteractiveRenderingActive(true);
    catalog_assets.replaceAssetsForTesting(items);
    process_layout_events(8);
    for (const auto& item : items) {
      const auto current = catalog_assets.assetById(item.asset_id);
      if (!catalog_assets.applyThumbnailResultForTesting(item.asset_id, item.file_path,
          current.value("adjustmentPatchJson").toString(), QUrl::fromLocalFile(item.file_path).toString()))
        return fail("Current fixture preview could not be published.");
    }
    process_layout_events(8);
    auto* workspace = active_workspace(root);
    auto* entry = workspace->findChild<QObject*>("libraryVisualSearchButton");
    if (!entry || !QMetaObject::invokeMethod(entry, "clicked")) return fail("AI Search entry point unavailable.");
    process_layout_events(4);
    auto* panel = workspace->findChild<QQuickItem*>("libraryVisualSearchPanel");
    auto* mode = workspace->findChild<QObject*>("visualSearchSimilarityToggle");
    auto* input = workspace->findChild<QObject*>("visualSearchQueryInput");
    auto* start = workspace->findChild<QObject*>("visualSearchStartButton");
    if (!panel || !panel->isVisible() || !mode || !mode->property("visible").toBool() || !input || !start)
      return fail("Similarity controls are not visible in the actual app.");
    mode->setProperty("checked", true);
    if (!QMetaObject::invokeMethod(mode, "toggled")) return fail("Similarity mode control not invokable.");
    process_layout_events(4);
    if (!workspace->property("useSimilaritySearch").toBool()) return fail("Similarity mode was not selected.");
    auto* search = similarity_search.controller();
    QJsonArray captures, timings;
    auto run_query = [&] {
      input->setProperty("text", "a white dog outdoors");
      process_layout_events(2);
      QElapsedTimer timer; timer.start();
      if (!start->property("enabled").toBool() || !QMetaObject::invokeMethod(start, "clicked") ||
          search->query() != "a white dog outdoors") return false;
      if (!wait_for_condition([&] { return !search->running(); }, 180000) ||
          search->resultIds().value(0) != "dog" || !catalog_assets.visualSearchActive() ||
          catalog_assets.assetCount() != search->resultIds().size()) {
        std::cerr << search->status().toStdString() << '\n';
        return false;
      }
      timings.append(QJsonObject{{"milliseconds", timer.elapsed()}, {"encoded", search->encoded()}, {"reused", search->reused()}});
      return true;
    };
    save_capture_if_requested(root_window, similarity_proof_dir, "similarity-ready", &captures);
    if (!run_query()) return fail("Actual Find photos did not retrieve/project the expected dog.");
    save_capture_if_requested(root_window, similarity_proof_dir, "similarity-results", &captures);
    auto* clear = workspace->findChild<QObject*>("visualSearchClearButton");
    if (!clear || !QMetaObject::invokeMethod(clear, "clicked") || catalog_assets.visualSearchActive())
      return fail("Similarity Show all in view did not clear projection.");
    if (!run_query() || search->encoded() != 0 || search->reused() != items.size())
      return fail("Actual repeated search did not reuse current image vectors.");
    // A changed ordinary filter synchronously invalidates candidates and clears
    // only the result projection after the model's change notification drains.
    catalog_assets.setSearchFilter("no-such-photo-for-similarity-proof");
    if (!search->results().isEmpty() ||
        !wait_for_condition([&] { return !catalog_assets.visualSearchActive(); }, 5000) || catalog_assets.assetCount() != 0)
      return fail("Filter change retained stale candidates or broadened the current view.");
    catalog_assets.setSearchFilter(QString());
    if (!run_query()) return fail("Search did not recover after changing filters.");
    root_window->setWidth(1100);
    process_layout_events(8);
    save_capture_if_requested(root_window, similarity_proof_dir, "similarity-compact", &captures);
    similarity_search.cancel();
    search->stop();
    if (!wait_for_condition([&] { return !search->running(); }, 5000) || catalog_assets.visualSearchActive())
      return fail("Similarity proof left an owned worker or stale projection.");
    if (captures.size() != 3) return fail("Similarity visual captures incomplete.");
    const QJsonObject receipt{{"actualFindPhotos", true}, {"actualLocalModel", true}, {"expectedTop", "dog"},
        {"selfContainedWorker", !option_value(arguments, "--search-runtime-bundle").isEmpty()},
        {"changedFilterInvalidated", true}, {"ownedWorkerStopped", true}, {"timings", timings}, {"captures", captures}};
    QFile receipt_file(QDir(similarity_proof_dir).filePath("receipt.json"));
    if (!receipt_file.open(QIODevice::WriteOnly)) return fail("Similarity receipt could not be written.");
    receipt_file.write(QJsonDocument(receipt).toJson());
    std::cout << QJsonDocument(receipt).toJson().constData();
    return EXIT_SUCCESS;
  }

  if (!search_edit_proof_dir.isEmpty()) {
    catalog_assets.setInteractiveRenderingActive(true);
    const auto id = QStringLiteral("search-edit-fixture");
    const auto preview = option_value(arguments, QStringLiteral("--visual-search-preview"));
    auto fixture_asset = make_asset(id, source_path, QString());
    const auto renderer = option_value(arguments, QStringLiteral("--search-edit-renderer"));
    if (!renderer.isEmpty() && renderer != "aces2" && renderer != "legacy")
      return fail(QStringLiteral("Search/edit renderer must be aces2 or legacy."));
    fixture_asset.adjustment_patch_json = renderer == "legacy"
        ? photo_editor::desktop::canonical_asset_bound_adjustment_patch_json(QStringLiteral("{}"))
        : photo_editor::desktop::serialize_adjustment_patch_json(photo_editor::desktop::EditRecipeState{},
            photo_editor::desktop::all_adjustment_groups(), photo_editor::desktop::AdjustmentPatchScope::AssetBound);
    catalog_assets.replaceAssetsForTesting({fixture_asset});
    if (!catalog_assets.applyThumbnailResultForTesting(id, source_path, fixture_asset.adjustment_patch_json,
        QUrl::fromLocalFile(preview.isEmpty() ? source_path : preview).toString()))
      return fail(QStringLiteral("Fixture thumbnail must match its canonical recipe generation."));
    auto before = catalog_assets.assetById(id).value(QStringLiteral("adjustmentPatchJson")).toString();
    auto* workspace = active_workspace(root);
    auto* dialog = workspace->findChild<QObject*>(QStringLiteral("searchEditDialog"));
    if (dialog == nullptr) return fail(QStringLiteral("Search/edit popup missing."));
    QMetaObject::invokeMethod(dialog, "open");
    if (!wait_for_condition([&] { return dialog->property("opened").toBool(); }, 5000)) return fail(QStringLiteral("Search/edit popup did not finish opening."));
    auto* input = dialog->findChild<QObject*>(QStringLiteral("searchEditInstruction"));
    auto* find = dialog->findChild<QObject*>(QStringLiteral("searchEditFind"));
    if (!input || !find) return fail(QStringLiteral("Search/edit controls missing."));
    input->setProperty("text", option_value(arguments, QStringLiteral("--visual-search-query")));
    QJsonArray captures;
    save_capture_if_requested(root_window, search_edit_proof_dir, QStringLiteral("instruction"), &captures);
    // Let the real workspace finish initial asset normalization before taking
    // the baseline whose exact restoration this workflow is responsible for.
    before = catalog_assets.assetById(id).value(QStringLiteral("adjustmentPatchJson")).toString();
    QMetaObject::invokeMethod(find, "clicked");
    if (!wait_for_condition([&] { return !search_edit_workflow.search()->running(); }, 300000) || search_edit_workflow.phase() != "confirm")
      return fail(QStringLiteral("Live search/edit search failed: ") + search_edit_workflow.status());
    if (!wait_for_condition([&] {
          QVariant ready;
          return QMetaObject::invokeMethod(dialog, "firstPreviewReady", Q_RETURN_ARG(QVariant, ready)) && ready.toBool();
        }, 5000)) {
      save_capture_if_requested(root_window, search_edit_proof_dir, QStringLiteral("missing-match-preview"), &captures);
      return fail(QStringLiteral("The confirmed match must have a visible loaded thumbnail."));
    }
    save_capture_if_requested(root_window, search_edit_proof_dir, QStringLiteral("confirm-targets"), &captures);
    if (search_edit_workflow.search()->resultIds() != QStringList{id} || catalog_assets.assetById(id).value("adjustmentPatchJson").toString() != before)
      return fail(QStringLiteral("Expected one matching fixture and no pre-confirmation edits."));
    auto* confirm = dialog->findChild<QObject*>(QStringLiteral("searchEditConfirm"));
    if (!confirm || !confirm->property("enabled").toBool()) return fail(QStringLiteral("Target confirmation is not enabled."));
    QMetaObject::invokeMethod(confirm, "clicked");
    const auto completed = wait_for_condition([&] { return !search_edit_agent.running(); }, 600000);
    const auto report = search_edit_agent.lastRunReport();
    QFile report_file(QDir(search_edit_proof_dir).filePath(QStringLiteral("draft-report.json")));
    if (report_file.open(QIODevice::WriteOnly)) report_file.write(QJsonDocument::fromVariant(report).toJson());
    save_capture_if_requested(root_window, search_edit_proof_dir, QStringLiteral("draft-review"), &captures);
    if (!completed || !report.value("ok").toBool() || !search_edit_agent.draftActive())
      return fail(QStringLiteral("Live masked draft incomplete; inspect draft-report.json: ") + search_edit_agent.errorMessage());
    const auto parsed = photo_editor::desktop::parse_adjustment_patch_json(catalog_assets.assetById(id).value("adjustmentPatchJson").toString());
    bool generated_background = false;
    if (parsed.state) {
      for (const auto& value : parsed.state->mask_stack) {
        const auto mask = value.toMap();
        generated_background = generated_background || (!mask.value("segmentationData").toByteArray().isEmpty() &&
            mask.value("kind").toString() == "background" && mask.value("temperature").toDouble() > 0);
      }
    }
    const auto rollback = search_edit_agent.rollbackDraft();
    const bool restored = rollback.value("ok").toBool() && catalog_assets.assetById(id).value("adjustmentPatchJson").toString() == before;
    QFile receipt(QDir(search_edit_proof_dir).filePath(QStringLiteral("receipt.json")));
    if (receipt.open(QIODevice::WriteOnly)) receipt.write(QJsonDocument::fromVariant(QVariantMap{
        {"generatedBackgroundMask", generated_background}, {"rollbackRestored", restored}, {"rollback", rollback},
        {"beforePatch", before}, {"restoredPatch", catalog_assets.assetById(id).value("adjustmentPatchJson")},
        {"search", search_edit_workflow.snapshot()},
        {"captures", captures.toVariantList()}}).toJson());
    return generated_background && restored ? EXIT_SUCCESS : fail(QStringLiteral("Missing real warm background segmentation or exact rollback."));
  }

  if (!visual_search_proof_dir.isEmpty()) {
    // Actual Main.qml / StudioWorkspace, isolated catalog; optionally exercise the
    // installed vision model with --visual-search-model and an explicit --source.
    const auto supplied_preview = option_value(arguments, QStringLiteral("--visual-search-preview"));
    if (!supplied_preview.isEmpty()) catalog_assets.setInteractiveRenderingActive(true);
    catalog_assets.replaceAssetsForTesting({make_asset(QStringLiteral("visual-search-fixture"), source_path,
        supplied_preview.isEmpty() ? QString() : QUrl::fromLocalFile(supplied_preview).toString())});
    // updateAssets deliberately invalidates previews for online originals.
    // Inject the supplied decoded fixture through the normal completion path,
    // with automatic regeneration paused so it cannot race this scoped proof.
    if (!supplied_preview.isEmpty() && !catalog_assets.applyThumbnailResultForTesting(
            QStringLiteral("visual-search-fixture"), source_path, QString(),
            QUrl::fromLocalFile(supplied_preview).toString())) {
      return fail(QStringLiteral("Unable to publish the supplied RAW preview fixture."));
    }
    auto* workspace = active_workspace(root);
    auto* toggle = workspace->findChild<QObject*>(QStringLiteral("libraryVisualSearchButton"));
    if (toggle == nullptr || !QMetaObject::invokeMethod(toggle, "clicked")) {
      return fail(QStringLiteral("AI Search entry point is not invokable."));
    }
    process_layout_events(8);
    auto* panel = workspace->findChild<QQuickItem*>(QStringLiteral("libraryVisualSearchPanel"));
    if (panel == nullptr || !panel->isVisible() || panel->height() < 100) {
      return fail(QStringLiteral("AI Search panel is not visibly laid out."));
    }
    QJsonArray captures;
    save_capture_if_requested(root_window, visual_search_proof_dir, QStringLiteral("visual-search-open"), &captures);
    const auto live_model = option_value(arguments, QStringLiteral("--visual-search-model"));
    if (!live_model.isEmpty()) {
      const auto query = option_value(arguments, QStringLiteral("--visual-search-query"));
      auto* input = panel->findChild<QObject*>(QStringLiteral("visualSearchQueryInput"));
      auto* start = panel->findChild<QObject*>(QStringLiteral("visualSearchStartButton"));
      if (!input || !start || !input->setProperty("text", query))
        return fail(QStringLiteral("Visual search query input is unavailable."));
      process_layout_events(2);
      if (!start->property("enabled").toBool() || !QMetaObject::invokeMethod(start, "clicked") || visual_search.query() != query)
        return fail(QStringLiteral("The actual Find photos action did not submit the requested query."));
      if (!wait_for_condition([&] { return !visual_search.running(); }, 240000) || visual_search.inspected() != 1) {
        return fail(QStringLiteral("Live visual search failed: %1").arg(visual_search.status()));
      }
      if (!catalog_assets.visualSearchActive() || catalog_assets.assetCount() != visual_search.results().size()) {
        return fail(QStringLiteral("QML did not project visual search results into the catalog."));
      }
    } else {
      catalog_assets.setVisualSearchResults({QStringLiteral("visual-search-fixture")});
    }
    save_capture_if_requested(root_window, visual_search_proof_dir, QStringLiteral("visual-search-results"), &captures);
    if (catalog_assets.assetCount() > 0) {
      const auto original_patch = catalog_assets.assetAt(0).value(QStringLiteral("adjustmentPatchJson"));
      // Exercise the actual QML handoff signal. This verifies dialog routing and
      // scope only; opening the dialog does not claim to generate an agent draft.
      if (!QMetaObject::invokeMethod(panel, "editRequested") ||
          workspace->property("agentScopeMode").toString() != QStringLiteral("current-photo")) {
        return fail(QStringLiteral("Visual search broadened the focused-photo handoff scope."));
      }
      QObject* dialog = nullptr;
      if (!wait_for_condition([&] {
            dialog = workspace->findChild<QObject*>(QStringLiteral("agentRunDialog"));
            return dialog != nullptr && dialog->property("visible").toBool();
          }, timeout_ms) || dialog->property("scopeMode").toString() != QStringLiteral("current-photo")) {
        return fail(QStringLiteral("AI Edit dialog did not preserve current-photo scope."));
      }
      QMetaObject::invokeMethod(dialog, "close");
      if (!wait_for_condition([&] { return !dialog->property("visible").toBool(); }, timeout_ms)) {
        return fail(QStringLiteral("AI Edit dialog did not finish closing before the next scope check."));
      }
      catalog_assets.selectAll();
      QMetaObject::invokeMethod(panel, "editRequested");
      process_layout_events(8);
      if (workspace->property("agentScopeMode").toString() != QStringLiteral("selected-assets") ||
          dialog->property("scopeMode").toString() != QStringLiteral("selected-assets") ||
          catalog_assets.assetAt(0).value(QStringLiteral("adjustmentPatchJson")) != original_patch) {
        return fail(QStringLiteral("Explicit selection handoff changed scope or edited the recipe."));
      }
      QMetaObject::invokeMethod(dialog, "close");
      if (!wait_for_condition([&] { return !dialog->property("visible").toBool(); }, timeout_ms)) {
        return fail(QStringLiteral("AI Edit dialog did not finish closing before compact capture."));
      }
    }
    auto* clear = workspace->findChild<QObject*>(QStringLiteral("visualSearchClearButton"));
    if (clear == nullptr || !QMetaObject::invokeMethod(clear, "clicked") || catalog_assets.visualSearchActive()) {
      return fail(QStringLiteral("Show all in view did not clear the visual filter."));
    }
    root_window->setWidth(1100);
    process_layout_events(8);
    save_capture_if_requested(root_window, visual_search_proof_dir, QStringLiteral("visual-search-compact"), &captures);
    if (captures.size() != 3) return fail(QStringLiteral("Visual search window captures failed."));
    std::cout << QJsonDocument(QJsonObject{{"captures", captures}, {"liveModel", live_model},
        {"primarySearchActionExercised", live_visual_search}, {"detailVerifiedCount", visual_search.detailVerifiedCount()},
        {"status", visual_search.status()}, {"results", QJsonArray::fromVariantList(visual_search.results())}}).toJson().constData();
    return EXIT_SUCCESS;
  }

  // Native interaction measurements use the complete shell and isolated storage.
#include "editor_responsiveness_proof.inc"

  if (!timeline_ui_proof_dir.isEmpty()) {
    auto timeline_media_asset = make_asset(
        QStringLiteral("timeline-proof-library-video"),
        source_path);
    timeline_media_asset.camera_label = QStringLiteral("Library interview clip");
    timeline_media_asset.media_type = QStringLiteral("video");
    timeline_media_asset.duration_text = QStringLiteral("0:10");
    timeline_media_asset.duration_seconds = 10.0;
    catalog_assets.replaceAssetsForTesting({timeline_media_asset});
    return run_timeline_ui_proof(
        root_window,
        root,
        &app_shell,
        &timeline_document,
        &timeline_playback,
        source_path,
        timeline_ui_proof_dir);
  }

  int frame_swap_count = 0;
  QObject::connect(
      root_window,
      &QQuickWindow::frameSwapped,
      root_window,
      [&frame_swap_count]() { ++frame_swap_count; });
  QJsonArray captures;

  std::vector<double> edit_prewarm_samples;
  std::vector<double> library_samples;
  std::vector<double> import_samples;
  std::vector<double> open_samples;
  std::vector<double> masks_samples;
  std::vector<double> brush_page_samples;
  std::vector<double> brush_feedback_first_use_samples;
  std::vector<double> brush_stroke_first_use_samples;
  std::vector<double> brush_feedback_samples;
  std::vector<double> brush_raster_completion_samples;
  std::vector<double> brush_commit_visible_samples;
  std::vector<double> tool_draft_samples;
  std::vector<double> carousel_route_samples;
  std::vector<double> carousel_input_dispatch_samples;
  std::vector<double> timeline_route_samples;
  std::vector<double> timeline_input_dispatch_samples;
  std::vector<double> export_route_samples;
  std::vector<double> frame_tick_samples;

  edit_prewarm_samples.reserve(1);
  library_samples.reserve(static_cast<std::size_t>(iterations));
  import_samples.reserve(static_cast<std::size_t>(iterations));
  open_samples.reserve(static_cast<std::size_t>(iterations));
  masks_samples.reserve(static_cast<std::size_t>(iterations));
  brush_page_samples.reserve(static_cast<std::size_t>(iterations));
  brush_feedback_first_use_samples.reserve(1);
  brush_feedback_samples.reserve(static_cast<std::size_t>(iterations));
  brush_raster_completion_samples.reserve(static_cast<std::size_t>(iterations));
  brush_commit_visible_samples.reserve(static_cast<std::size_t>(iterations));
  tool_draft_samples.reserve(static_cast<std::size_t>(iterations));
  carousel_route_samples.reserve(static_cast<std::size_t>(iterations));
  carousel_input_dispatch_samples.reserve(static_cast<std::size_t>(iterations));
  timeline_route_samples.reserve(static_cast<std::size_t>(iterations));
  timeline_input_dispatch_samples.reserve(static_cast<std::size_t>(iterations));
  export_route_samples.reserve(static_cast<std::size_t>(iterations));
  frame_tick_samples.reserve(static_cast<std::size_t>(iterations));

  QVector<photo_editor::desktop::CatalogAssetItem> workflow_assets;
  workflow_assets.reserve(iterations);
  for (int iteration = 0; iteration < iterations; ++iteration) {
    workflow_assets.append(make_asset(
        QStringLiteral("actual-app-workflow-%1").arg(iteration + 1),
        source_path));
  }
  const auto recipe_path = option_value(arguments, QStringLiteral("--recipe-file"));
  if (color_interaction_proof && !recipe_path.isEmpty()) {
    QFile recipe_file(recipe_path);
    if (!recipe_file.open(QIODevice::ReadOnly)) return fail("Cannot read interaction proof recipe.");
    const auto recipe_state = photo_editor::desktop::parse_edit_recipe_json(QString::fromUtf8(recipe_file.readAll()));
    const auto patch = photo_editor::desktop::serialize_adjustment_patch_json(recipe_state,
        photo_editor::desktop::all_adjustment_groups(), photo_editor::desktop::AdjustmentPatchScope::AssetBound);
    for (auto& asset : workflow_assets) asset.adjustment_patch_json = patch;
  }
  catalog_assets.replaceAssetsForTesting(workflow_assets);

  QElapsedTimer edit_prewarm_timer;
  edit_prewarm_timer.start();
  if (!wait_for_condition(
          [&]() {
            QVariant ready_value;
            return QMetaObject::invokeMethod(
                       root,
                       "editWorkspacePrewarmReady",
                       Q_RETURN_ARG(QVariant, ready_value)) &&
                   ready_value.toBool();
          },
          timeout_ms)) {
    return fail(QStringLiteral("Timed out prewarming the Edit workspace after QML shell readiness."));
  }
  edit_prewarm_samples.push_back(static_cast<double>(edit_prewarm_timer.elapsed()));
  log_stage(
      QStringLiteral("edit_workspace_prewarm_ready"),
      edit_prewarm_timer.elapsed(),
      0);

  if (native_readback_proof) {
#include "native_inspector_readback_proof.inc"
  }

  if (color_interaction_proof) {
    using Access = photo_editor::desktop::EditorPreviewControllerTestAccess;
    catalog_assets.selectIndex(0);
    root->setProperty("currentWorkspace", root->property("editWorkspaceIndex"));
    root->setProperty("editWorkspaceMode", QStringLiteral("adjust"));
    editor_preview.setPreviewTargetEdge(2560);
    editor_preview.setScopesVisible(option_int(arguments, QStringLiteral("--scopes-visible"), 1) != 0);
    if (!wait_for_condition([&]() {
          return editor_preview.hasAsset() && Access::all_idle(editor_preview) &&
                 editor_preview.displayPreviewNativeAvailable() &&
                 named_item_visible(active_workspace(root), QStringLiteral("editImageViewport"));
        }, timeout_ms)) {
      return fail(QStringLiteral("Color interaction proof requires a visible native preview."));
    }
    QElapsedTimer gui_clock;
    gui_clock.start();
    qint64 last_gui_tick = 0;
    qint64 max_gui_gap_ms = 0;
    QTimer gui_heartbeat;
    QObject::connect(&gui_heartbeat, &QTimer::timeout, root_window, [&]() {
      const auto now = gui_clock.elapsed();
      max_gui_gap_ms = std::max(max_gui_gap_ms, now - last_gui_tick);
      last_gui_tick = now;
    });
    gui_heartbeat.start(8);
    const auto color_control = option_value(arguments, QStringLiteral("--color-control"));
    const auto apply_color_edit = [&](double value) {
      if (color_control == QStringLiteral("curve")) {
        editor_preview.setActiveCurvePoints({QVariantMap{{"x", 0.0}, {"y", 0.0}},
            QVariantMap{{"x", 0.5}, {"y", 0.5 + value * 0.25}},
            QVariantMap{{"x", 1.0}, {"y", 1.0}}});
      } else if (color_control == QStringLiteral("saturation")) {
        editor_preview.setSaturation(value * 100.0);
      } else if (color_control == QStringLiteral("temperature")) {
        editor_preview.setTemperature(value * 100.0);
      } else {
        editor_preview.setExposure(value);
      }
    };
    apply_color_edit(0.01);
    if (!wait_for_condition([&]() { return Access::all_idle(editor_preview); }, timeout_ms))
      return fail(QStringLiteral("Color interaction warmup timed out."));
    save_capture_if_requested(root_window, capture_dir, QStringLiteral("color-before"), &captures);

    QElapsedTimer interaction_timer;
    int revision_floor = 0;
    qint64 first_publication_ms = -1;
    qint64 first_frame_ms = -1;
    bool measuring = false;
    const auto publication_connection = QObject::connect(
        &editor_preview, &photo_editor::desktop::EditorPreviewController::previewChanged,
        root_window, [&]() {
          if (measuring && first_publication_ms < 0 &&
              editor_preview.displayPreviewNativeRevision() > revision_floor) {
            first_publication_ms = interaction_timer.elapsed();
            root_window->update();
          }
        });
    const auto frame_connection = QObject::connect(
        root_window, &QQuickWindow::frameSwapped, root_window, [&]() {
          if (measuring && first_publication_ms >= 0 && first_frame_ms < 0)
            first_frame_ms = interaction_timer.elapsed();
        }, Qt::QueuedConnection);
    const auto disconnect_measurements = qScopeGuard([&]() {
      QObject::disconnect(publication_connection);
      QObject::disconnect(frame_connection);
    });
    const auto wait_precisely = [&](const std::function<bool()>& ready) {
      if (ready()) return true;
      QEventLoop loop;
      QTimer poll;
      QTimer deadline;
      deadline.setSingleShot(true);
      QObject::connect(&poll, &QTimer::timeout, &loop, [&]() { if (ready()) loop.quit(); });
      QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
      poll.start(1);
      deadline.start(timeout_ms);
      loop.exec();
      return ready();
    };
    for (int iteration = 0; iteration < iterations; ++iteration) {
      revision_floor = editor_preview.displayPreviewNativeRevision();
      first_publication_ms = first_frame_ms = -1;
      measuring = true;
      interaction_timer.start();
      apply_color_edit(0.1 + 0.03 * static_cast<double>(iteration));
      const auto setter_ms = interaction_timer.elapsed();
      if (!wait_precisely([&]() {
            return first_frame_ms >= 0 && Access::all_idle(editor_preview) &&
                   Access::native_preview_reaches_viewport(editor_preview);
          }))
        return fail(QStringLiteral("Color interaction did not publish and complete a QML frame."));
      const auto settled_ms = interaction_timer.elapsed();
      const auto settled_frame_floor = frame_swap_count;
      root_window->update();
      if (!wait_precisely([&]() { return frame_swap_count > settled_frame_floor; }))
        return fail(QStringLiteral("Settled preview did not complete a QML frame."));
      measuring = false;
      log_stage(QStringLiteral("color_edit_frame_completion"), interaction_timer.elapsed(), iteration,
          {{QStringLiteral("control"), color_control.isEmpty() ? QStringLiteral("exposure") : color_control},
           {QStringLiteral("first_publication_ms"), first_publication_ms},
           {QStringLiteral("setter_ms"), setter_ms},
           {QStringLiteral("first_frame_after_publication_ms"), first_frame_ms},
           {QStringLiteral("all_render_work_idle_ms"), settled_ms},
           {QStringLiteral("scopes_visible"), editor_preview.scopesVisible()},
           {QStringLiteral("preview_target_edge"), editor_preview.previewTargetEdge()},
           {QStringLiteral("native_width"), editor_preview.displayPreviewNativeSize().width()},
           {QStringLiteral("native_height"), editor_preview.displayPreviewNativeSize().height()},
           {QStringLiteral("native_revision"), editor_preview.displayPreviewNativeRevision()},
           {QStringLiteral("frame_swaps"), frame_swap_count}});
    }
    // Supersede edits at a nominal 60 Hz while the actual editor remains visible.
    QElapsedTimer burst_timer;
    burst_timer.start();
    const auto burst_frame_floor = frame_swap_count;
    const auto burst_revision_floor = editor_preview.displayPreviewNativeRevision();
    int burst_overlay_publications = 0;
    int burst_overlay_frames = 0;
    int burst_native_frames = 0;
    auto last_presented_native_revision = burst_revision_floor;
    QString last_presented_overlay;
    auto last_burst_overlay = editor_preview.displayPreviewOverlaySource();
    const auto burst_overlay_connection = QObject::connect(&editor_preview,
        &photo_editor::desktop::EditorPreviewController::previewChanged, root_window, [&]() {
          const auto source = editor_preview.displayPreviewOverlaySource();
          if (!source.isEmpty() && source != last_burst_overlay) ++burst_overlay_publications;
          last_burst_overlay = source;
        });
    const auto burst_frame_connection = QObject::connect(root_window, &QQuickWindow::frameSwapped,
        root_window, [&]() {
          const auto revision = editor_preview.displayPreviewNativeRevision();
          if (revision > last_presented_native_revision) ++burst_native_frames;
          last_presented_native_revision = revision;
          const auto source = editor_preview.displayPreviewOverlaySource();
          if (!source.isEmpty() && source != last_presented_overlay) ++burst_overlay_frames;
          last_presented_overlay = source;
        });
    qint64 maximum_setter_ms = 0;
    editor_preview.beginCoalescedEdit();
    for (int sample = 0; sample < 30; ++sample) {
      const auto sample_started_ms = burst_timer.elapsed();
      apply_color_edit(-0.4 + static_cast<double>(sample) * 0.04);
      maximum_setter_ms = std::max(maximum_setter_ms, burst_timer.elapsed() - sample_started_ms);
      root_window->update();
      // Real pointer events yield to the GUI loop between inputs even when a
      // setter exceeds 16 ms. A synchronous loop would starve timers/render
      // completion artificially on larger RAW recipes.
      QEventLoop input_tick;
      QTimer::singleShot(static_cast<int>(std::max<qint64>(1,
          sample_started_ms + 16 - burst_timer.elapsed())), &input_tick, &QEventLoop::quit);
      input_tick.exec();
    }
    const auto burst_input_ms = burst_timer.elapsed();
    const auto burst_input_frames = frame_swap_count - burst_frame_floor;
    const auto burst_input_revisions = editor_preview.displayPreviewNativeRevision() - burst_revision_floor;
    QObject::disconnect(burst_overlay_connection);
    QObject::disconnect(burst_frame_connection);
    editor_preview.endCoalescedEdit();
    if (!wait_precisely([&]() {
          return Access::all_idle(editor_preview) && Access::native_preview_reaches_viewport(editor_preview);
        }))
      return fail(QStringLiteral("Color interaction burst failed to settle."));
    const auto final_frame_floor = frame_swap_count;
    root_window->update();
    if (!wait_precisely([&]() { return frame_swap_count > final_frame_floor; }))
      return fail(QStringLiteral("Color interaction burst final frame failed."));
    log_stage(QStringLiteral("color_edit_burst_settled"), burst_timer.elapsed(), 0,
        {{QStringLiteral("control"), color_control.isEmpty() ? QStringLiteral("exposure") : color_control},
         {QStringLiteral("updates"), 30}, {QStringLiteral("frame_swaps"), frame_swap_count - burst_frame_floor},
         {QStringLiteral("input_duration_ms"), burst_input_ms},
         {QStringLiteral("input_frame_swaps"), burst_input_frames},
         {QStringLiteral("input_native_publications"), burst_input_revisions},
         {QStringLiteral("maximum_setter_ms"), maximum_setter_ms},
         {QStringLiteral("input_overlay_publications"), burst_overlay_publications},
         {QStringLiteral("input_overlay_frames"), burst_overlay_frames},
         {QStringLiteral("input_native_frames"), burst_native_frames},
         {QStringLiteral("native_width"), editor_preview.displayPreviewNativeSize().width()},
         {QStringLiteral("native_height"), editor_preview.displayPreviewNativeSize().height()},
         {QStringLiteral("final_exposure"), editor_preview.exposure()}});
    QObject::disconnect(publication_connection);
    QObject::disconnect(frame_connection);
    save_capture_if_requested(root_window, capture_dir, QStringLiteral("color-after"), &captures);
    QMetaObject::invokeMethod(root, "flushQmlFrameSummary");
    gui_heartbeat.stop();
    std::cout << "gui_heartbeat_max_gap_ms=" << max_gui_gap_ms << '\n';
    std::cout << "color_interaction_iterations=" << iterations << '\n';
    if (arguments.contains(QStringLiteral("--require-live-feedback")) &&
        burst_native_frames + burst_overlay_frames <= 0)
      return fail(QStringLiteral("Continuous edits produced no visible preview publications during input."));
    if (arguments.contains(QStringLiteral("--capture-scopes"))) {
      auto* workspace = root->findChild<QObject*>(QStringLiteral("editStudioWorkspace"));
      if (!workspace || !QMetaObject::invokeMethod(workspace, "openScopesDock",
              Q_ARG(QVariant, QStringLiteral("right"))))
        return fail(QStringLiteral("Cannot reveal scopes for visual proof."));
      QElapsedTimer scope_settle;
      scope_settle.start();
      if (!wait_precisely([&]() { return scope_settle.elapsed() >= 350; }))
        return fail(QStringLiteral("Scope visual proof did not settle."));
      save_capture_if_requested(root_window, capture_dir, QStringLiteral("scopes-after"), &captures);
    }
    return EXIT_SUCCESS;
  }

  QElapsedTimer suite_timer;
  suite_timer.start();
  for (int iteration = 0; iteration < iterations; ++iteration) {
    const auto asset_id = QStringLiteral("actual-app-workflow-%1").arg(iteration + 1);

    QElapsedTimer library_timer;
    library_timer.start();
    root->setProperty("currentWorkspace", root->property("libraryWorkspaceIndex"));
    if (!wait_for_condition(
            [&]() {
              auto* workspace = active_workspace(root);
              return workspace != nullptr &&
                     workspace->objectName() == QStringLiteral("libraryStudioWorkspace");
            },
            timeout_ms)) {
      return fail(QStringLiteral("Timed out routing to the Library workspace."));
    }
    library_samples.push_back(static_cast<double>(library_timer.elapsed()));
    log_stage(QStringLiteral("library_route_to_visible"), library_timer.elapsed(), iteration);
    if (iteration == 0) {
      save_capture_if_requested(
          root_window,
          capture_dir,
          QStringLiteral("01-library"),
          &captures);
    }

    QElapsedTimer import_timer;
    import_timer.start();
    if (!QMetaObject::invokeMethod(root, "beginImportWorkspace")) {
      return fail(QStringLiteral("Actual-app workflow could not invoke beginImportWorkspace."));
    }
    if (!wait_for_condition(
            [&]() {
              auto* workspace = active_workspace(root);
              return workspace != nullptr &&
                     workspace->objectName() == QStringLiteral("libraryStudioWorkspace") &&
                     find_named_object(workspace, QStringLiteral("importWorkspace")) != nullptr &&
                     named_item_visible(workspace, QStringLiteral("importMainPanel"));
            },
            timeout_ms)) {
      return fail(QStringLiteral("Timed out routing to the Import workspace."));
    }
    import_samples.push_back(static_cast<double>(import_timer.elapsed()));
    log_stage(QStringLiteral("import_route_to_visible"), import_timer.elapsed(), iteration);
    if (iteration == 0) {
      save_capture_if_requested(
          root_window,
          capture_dir,
          QStringLiteral("02-import"),
          &captures);
    }

    QElapsedTimer open_timer;
    open_timer.start();
    catalog_assets.selectIndex(iteration);
    root->setProperty("currentWorkspace", root->property("editWorkspaceIndex"));
    root->setProperty("editWorkspaceMode", QStringLiteral("adjust"));
    if (!wait_for_condition(
            [&]() {
              auto* workspace = active_workspace(root);
              return editor_preview.hasAsset() && editor_preview.assetId() == asset_id &&
                     workspace != nullptr &&
                     workspace->objectName() == QStringLiteral("editStudioWorkspace") &&
                     named_item_visible(workspace, QStringLiteral("editImageViewport")) &&
                     (!editor_preview.displayPreviewSource().isEmpty() ||
                      !editor_preview.previewSource().isEmpty() ||
                      editor_preview.displayPreviewNativeAvailable());
            },
            timeout_ms)) {
      return fail(QStringLiteral("Timed out opening the benchmark photo in the Edit workspace."));
    }
    open_samples.push_back(static_cast<double>(open_timer.elapsed()));
    log_stage(
        QStringLiteral("open_photo_to_edit_preview_ready"),
        open_timer.elapsed(),
        iteration,
        {{QStringLiteral("asset_id"), asset_id},
         {QStringLiteral("qt_platform"), QGuiApplication::platformName()},
         {QStringLiteral("qt_graphics_api"), static_cast<int>(root_window->rendererInterface()->graphicsApi())},
         {QStringLiteral("qt_metal_renderer"), root_window->rendererInterface()->graphicsApi() == QSGRendererInterface::Metal},
         {QStringLiteral("source_image_width"), editor_preview.sourceImageWidth()},
         {QStringLiteral("source_image_height"), editor_preview.sourceImageHeight()}});
    if (iteration == 0) {
      save_capture_if_requested(
          root_window,
          capture_dir,
          QStringLiteral("03-edit-open-photo"),
          &captures);
    }

#include "checkpoint_tool_native_proof.inc"
#include "effect_tool_native_proof.inc"

    QElapsedTimer masks_timer;
    masks_timer.start();
    QVariant masks_route_result;
    if (!QMetaObject::invokeMethod(
            root,
            "activatePrimaryEditTool",
            Q_RETURN_ARG(QVariant, masks_route_result),
            Q_ARG(QVariant, QStringLiteral("masks"))) ||
        !masks_route_result.toBool()) {
      return fail(QStringLiteral("Actual-app workflow could not route to Masks."));
    }
    if (!wait_for_condition(
            [&]() {
              auto* workspace = active_workspace(root);
              return workspace != nullptr &&
                     workspace->objectName() == QStringLiteral("editStudioWorkspace") &&
                     named_item_visible(workspace, QStringLiteral("maskToolRail")) &&
                     named_item_visible(workspace, QStringLiteral("maskBrushShapeMenuButton"));
            },
            timeout_ms)) {
      return fail(QStringLiteral("Timed out opening the Masks page tool rail."));
    }
    masks_samples.push_back(static_cast<double>(masks_timer.elapsed()));
    log_stage(QStringLiteral("masks_page_to_tool_rail_ready"), masks_timer.elapsed(), iteration);

#include "lasso_tool_native_proof.inc"

    QElapsedTimer brush_page_timer;
    brush_page_timer.start();
    auto* edit_workspace = active_workspace(root);
    qint64 brush_click_invoke_ms = -1;
    qint64 brush_click_layout_ms = -1;
    auto* brush_button =
        find_named_object(edit_workspace, QStringLiteral("maskBrushShapeMenuButton"));
    auto brush_button_clicked = false;
    if (brush_button != nullptr && QMetaObject::invokeMethod(brush_button, "click")) {
      brush_button_clicked = true;
      brush_click_invoke_ms = brush_page_timer.elapsed();
    } else if (brush_button != nullptr && QMetaObject::invokeMethod(brush_button, "clicked")) {
      brush_button_clicked = true;
      brush_click_invoke_ms = brush_page_timer.elapsed();
    }
    if (!brush_button_clicked) {
      if (edit_workspace == nullptr ||
          !QMetaObject::invokeMethod(
              edit_workspace,
              "startMaskBrushSource",
              Q_ARG(QVariant, QVariant(QStringLiteral("add"))))) {
        return fail(QStringLiteral("Actual-app workflow could not start brush mode."));
      }
      brush_click_invoke_ms = brush_page_timer.elapsed();
    }
    qint64 brush_active_mask_kind_ms = -1;
    qint64 brush_workspace_ms = -1;
    qint64 brush_hud_ms = -1;
    qint64 brush_context_ms = -1;
    qint64 brush_overlay_ms = -1;
    if (!wait_for_condition(
            [&]() {
              auto* workspace = active_workspace(root);
              const auto elapsed_ms = brush_page_timer.elapsed();
              const auto active_mask_kind_ready =
                  editor_preview.activeMaskKind() == QStringLiteral("brush");
              const auto workspace_ready =
                  workspace != nullptr &&
                  workspace->objectName() == QStringLiteral("editStudioWorkspace");
              const auto hud_ready =
                  workspace_ready &&
                  named_item_visible(workspace, QStringLiteral("brushToolHud"));
              const auto context_ready =
                  workspace_ready &&
                  named_item_visible(workspace, QStringLiteral("brushContextPanel"));
              const auto overlay_ready =
                  workspace_ready &&
                  named_item_visible(
                      workspace,
                      QStringLiteral("editImageViewportMaskCanvasOverlay"));
              if (active_mask_kind_ready && brush_active_mask_kind_ms < 0) {
                brush_active_mask_kind_ms = elapsed_ms;
              }
              if (workspace_ready && brush_workspace_ms < 0) {
                brush_workspace_ms = elapsed_ms;
              }
              if (hud_ready && brush_hud_ms < 0) {
                brush_hud_ms = elapsed_ms;
              }
              if (context_ready && brush_context_ms < 0) {
                brush_context_ms = elapsed_ms;
              }
              if (overlay_ready && brush_overlay_ms < 0) {
                brush_overlay_ms = elapsed_ms;
              }
              return active_mask_kind_ready &&
                     workspace_ready &&
                     hud_ready &&
                     overlay_ready;
            },
            timeout_ms)) {
      return fail(QStringLiteral("Timed out opening brush mode in the actual QML shell."));
    }
    brush_page_samples.push_back(static_cast<double>(brush_page_timer.elapsed()));
    log_stage(
        QStringLiteral("brush_page_to_hud_ready"),
        brush_page_timer.elapsed(),
        iteration,
        {
            {QStringLiteral("active_mask_kind"), editor_preview.activeMaskKind()},
            {QStringLiteral("brush_click_invoke_ms"), brush_click_invoke_ms},
            {QStringLiteral("brush_click_layout_ms"), brush_click_layout_ms},
            {QStringLiteral("active_mask_kind_ready_ms"), brush_active_mask_kind_ms},
            {QStringLiteral("workspace_ready_ms"), brush_workspace_ms},
            {QStringLiteral("brush_hud_ready_ms"), brush_hud_ms},
            {QStringLiteral("brush_context_ready_ms"), brush_context_ms},
            {QStringLiteral("mask_overlay_ready_ms"), brush_overlay_ms},
        });
    if (iteration == 0) {
      save_capture_if_requested(
          root_window,
          capture_dir,
          QStringLiteral("04-brush-ready"),
          &captures);
    }

    editor_preview.setActiveMaskBrushAutoMask(brush_auto_mask);
    auto* vector_canvas = find_named_object(active_workspace(root), QStringLiteral("editImageViewportLiveBrushVectorCanvas"));
    if (vector_canvas == nullptr) return fail(QStringLiteral("Live brush Canvas receipt unavailable."));
    vector_canvas->setProperty("frameReceiptEnabled", true);
    const bool brush_phase_profile = qEnvironmentVariableIsSet("PHOTO_EDITOR_BRUSH_PHASE_PROFILE");
    vector_canvas->setProperty("framePhaseTracingEnabled", brush_phase_profile);
    vector_canvas->setProperty("framePhaseReferenceEpochMs", 0.0);
    vector_canvas->setProperty("framePhaseStrokeSignalOffsetMs", -1.0);
    vector_canvas->setProperty("framePhaseTimerOffsetMs", -1.0);
    vector_canvas->setProperty("framePhasePaintStartOffsetMs", -1.0);
    vector_canvas->setProperty("framePhasePaintEndOffsetMs", -1.0);
    vector_canvas->setProperty("framePhasePaintedOffsetMs", -1.0);
    auto* immediate_coverage = find_named_object(active_workspace(root), QStringLiteral("editImageViewportImmediateBrushCoverage"));
    auto* raster_image = find_named_item(active_workspace(root), QStringLiteral("editImageViewportLiveBrushRasterImage"));
    const auto immediate_revision_before = immediate_coverage ? immediate_coverage->property("completedPaintRevision").toInt() : 0;
    const auto canvas_revision_before = vector_canvas->property("completedPaintRevision").toInt();
    QElapsedTimer stroke_timer;
    stroke_timer.start();
    editor_preview.beginActiveBrushStroke();
    const auto stroke_setup_ms = stroke_timer.elapsed();
    const auto brush_points_before = editor_preview.activeMaskLiveBrushPoints().size();
    const auto brush_preview_revision_before = editor_preview.activeMaskLiveBrushPreviewRevision();
    QElapsedTimer feedback_timer;
    feedback_timer.start();
    struct BrushFrameReceipt {
      std::atomic<qint64> canvas_synced_ms{-1};
      std::atomic<qint64> presented_ms{-1};
      std::atomic<qint64> stroke_presented_ms{-1};
    };
    const auto receipt = std::make_shared<BrushFrameReceipt>();
    const auto phase_reference_epoch_ms = brush_phase_profile
        ? QDateTime::currentMSecsSinceEpoch() : qint64{0};
    vector_canvas->setProperty("framePhaseReferenceEpochMs", phase_reference_epoch_ms);
    qint64 raster_observed_ms = -1;
    const auto raster_connection = QObject::connect(&editor_preview,
        &photo_editor::desktop::EditorPreviewController::activeBrushStrokeChanged,
        root_window, [&] {
          if (raster_observed_ms < 0 && editor_preview.activeMaskLiveBrushPreviewRevision() > brush_preview_revision_before)
            raster_observed_ms = feedback_timer.elapsed();
        });
    // The GUI thread is blocked during scene-graph synchronization, so reading
    // Canvas properties here is safe. Require a completed paint with live points
    // to have synchronized before accepting the following swap.
    const auto sync_connection = QObject::connect(root_window, &QQuickWindow::afterSynchronizing,
        root_window, [receipt, vector_canvas, immediate_coverage, raster_image, immediate_revision_before,
                      canvas_revision_before, brush_points_before, feedback_timer, &editor_preview] {
          const bool immediate_painted = immediate_coverage &&
              immediate_coverage->property("completedPaintRevision").toInt() > immediate_revision_before &&
              immediate_coverage->property("paintedPointCount").toInt() > brush_points_before;
          const bool raster_painted = raster_image && raster_image->isVisible() &&
              raster_image->property("status").toInt() == 1 &&
              editor_preview.activeMaskLiveBrushPreviewPointCount() > brush_points_before;
          const bool trail_painted = vector_canvas->property("completedPaintRevision").toInt() > canvas_revision_before &&
              vector_canvas->property("paintedPointCount").toInt() > brush_points_before;
          if (receipt->canvas_synced_ms.load() < 0 && (immediate_painted || raster_painted || trail_painted))
            receipt->canvas_synced_ms.store(feedback_timer.elapsed());
        }, Qt::DirectConnection);
    const auto swap_connection = QObject::connect(root_window, &QQuickWindow::frameSwapped,
        root_window, [receipt, feedback_timer, stroke_timer] {
          if (receipt->canvas_synced_ms.load() >= 0 && receipt->presented_ms.load() < 0) {
            receipt->stroke_presented_ms.store(stroke_timer.elapsed());
            receipt->presented_ms.store(feedback_timer.elapsed());
          }
        }, Qt::DirectConnection);
    const auto disconnect_feedback = qScopeGuard([&] {
      QObject::disconnect(raster_connection);
      QObject::disconnect(sync_connection);
      QObject::disconnect(swap_connection);
    });
    editor_preview.addBrushPoint(0.31, 0.36);
    const auto point_dispatch_ms = feedback_timer.elapsed();
    const auto feedback_is_visible = [&]() { return receipt->presented_ms.load() >= 0; };
    if (!feedback_is_visible()) {
      QEventLoop feedback_loop;
      QTimer feedback_timeout;
      feedback_timeout.setSingleShot(true);
      const auto frame_connection = QObject::connect(
          root_window, &QQuickWindow::frameSwapped, &feedback_loop, [&]() {
            if (feedback_is_visible()) feedback_loop.quit();
          });
      const auto timeout_connection = QObject::connect(
          &feedback_timeout, &QTimer::timeout, &feedback_loop, &QEventLoop::quit);
      feedback_timeout.start(timeout_ms);
      feedback_loop.exec();
      QObject::disconnect(frame_connection);
      QObject::disconnect(timeout_connection);
    }
    if (!feedback_is_visible()) {
      return fail(QStringLiteral("Timed out waiting for the first rendered frame containing live brush feedback."));
    }
    const auto brush_feedback_ms = static_cast<double>(receipt->presented_ms.load());
    QVariantMap brush_timing_fields{
        {QStringLiteral("feedback_contract"), QStringLiteral("controller_point_to_painted_canvas_sync_and_swap:v2")},
        {QStringLiteral("stroke_setup_ms"), stroke_setup_ms},
        {QStringLiteral("point_dispatch_ms"), point_dispatch_ms},
        {QStringLiteral("canvas_paint_and_sync_ms"), receipt->canvas_synced_ms.load()},
        {QStringLiteral("frame_swap_ms"), receipt->presented_ms.load()},
        {QStringLiteral("auto_mask"), brush_auto_mask},
        {QStringLiteral("live_preview_backend"), editor_preview.activeMaskLiveBrushPreviewBackend()}};
    if (brush_phase_profile) {
      brush_timing_fields.insert(QStringLiteral("phase_start_epoch_ms"), phase_reference_epoch_ms);
      brush_timing_fields.insert(QStringLiteral("phase_stroke_signal_offset_ms"),
          vector_canvas->property("framePhaseStrokeSignalOffsetMs"));
      brush_timing_fields.insert(QStringLiteral("phase_timer_offset_ms"),
          vector_canvas->property("framePhaseTimerOffsetMs"));
      brush_timing_fields.insert(QStringLiteral("phase_paint_start_offset_ms"),
          vector_canvas->property("framePhasePaintStartOffsetMs"));
      brush_timing_fields.insert(QStringLiteral("phase_paint_end_offset_ms"),
          vector_canvas->property("framePhasePaintEndOffsetMs"));
      brush_timing_fields.insert(QStringLiteral("phase_painted_offset_ms"),
          vector_canvas->property("framePhasePaintedOffsetMs"));
    }
    if (iteration == 0) {
      brush_stroke_first_use_samples.push_back(static_cast<double>(receipt->stroke_presented_ms.load()));
      log_stage(QStringLiteral("brush_stroke_begin_to_qml_frame_first_use"),
          receipt->stroke_presented_ms.load(), iteration, brush_timing_fields);
    }
    if (iteration == 0) {
      brush_feedback_first_use_samples.push_back(brush_feedback_ms);
      log_stage(
          QStringLiteral("brush_controller_point_to_qml_frame_first_use"),
          receipt->presented_ms.load(), iteration, brush_timing_fields);
    } else {
      brush_feedback_samples.push_back(brush_feedback_ms);
    }
    log_stage(
        QStringLiteral("brush_controller_point_to_qml_frame"),
        receipt->presented_ms.load(), iteration, brush_timing_fields);
    if (iteration == 0) save_capture_if_requested(root_window, capture_dir,
        QStringLiteral("05-brush-feedback"), &captures);
    if (!wait_for_condition(
            [&]() {
              return editor_preview.activeMaskLiveBrushPreviewRevision() > brush_preview_revision_before;
            },
            timeout_ms)) {
      return fail(QStringLiteral("Timed out waiting for the high-fidelity brush bitmap worker."));
    }
    if (raster_observed_ms < 0) raster_observed_ms = feedback_timer.elapsed();
    brush_raster_completion_samples.push_back(static_cast<double>(raster_observed_ms));
    log_stage(
        QStringLiteral("brush_high_fidelity_raster_complete"),
        raster_observed_ms,
        iteration,
        {{QStringLiteral("worker_elapsed_ms"), editor_preview.activeMaskLiveBrushPreviewElapsedMs()},
         {QStringLiteral("feedback_already_presented"), raster_observed_ms >= receipt->presented_ms.load()}});
    // Stop reading this workspace before later workflow routes can unload it.
    QObject::disconnect(raster_connection);
    QObject::disconnect(sync_connection);
    QObject::disconnect(swap_connection);
    editor_preview.addBrushPoint(0.40, 0.44);
    editor_preview.addBrushPoint(0.49, 0.52);
    editor_preview.addBrushPoint(0.58, 0.60);

    QElapsedTimer commit_timer;
    commit_timer.start();
    editor_preview.endActiveBrushStroke();
    brush_commit_visible_samples.push_back(static_cast<double>(commit_timer.elapsed()));
    const auto mask_telemetry =
        photo_editor::desktop::EditorPreviewControllerTestAccess::last_mask_work_telemetry(
            editor_preview);
    log_stage(
        QStringLiteral("brush_release_to_qml_commit_visible"),
        commit_timer.elapsed(),
        iteration,
        {{QStringLiteral("active_mask_count"), editor_preview.maskStack().size()},
         {QStringLiteral("pending_brush_commit_count"),
          photo_editor::desktop::EditorPreviewControllerTestAccess::pending_brush_commit_count(
              editor_preview)},
         {QStringLiteral("mask_raster_ms"),
          mask_telemetry.value(QStringLiteral("mask_raster_ms")).toLongLong()}});

    QElapsedTimer draft_timer;
    draft_timer.start();
    editor_preview.setExposure(iteration % 2 == 0 ? 0.18 : 0.08);
    tool_draft_samples.push_back(static_cast<double>(draft_timer.elapsed()));
    log_stage(
        QStringLiteral("tool_adjust_to_qml_draft_scheduled"),
        draft_timer.elapsed(),
        iteration,
        {{QStringLiteral("tool"), QStringLiteral("exposure")}});

    if (brush_only) continue;

    QMetaObject::invokeMethod(root, "flushQmlFrameSummary");
    root->setProperty("qmlFrameCaptureEnabled", true);
    QMetaObject::invokeMethod(root, "beginQmlFrameCaptureWindow");
    QElapsedTimer carousel_route_timer;
    carousel_route_timer.start();
    const auto carousel_route_started_at = std::chrono::steady_clock::now();
    RouteInputDispatchProbe carousel_input_probe(root_window);
    carousel_input_probe.postNoOpKeyAfter(carousel_route_started_at);
    root->setProperty("currentWorkspace", root->property("carouselWorkspaceIndex"));
    if (!wait_for_condition(
            [&]() {
              auto* workspace = active_workspace(root);
              return workspace != nullptr &&
                     workspace->objectName() == QStringLiteral("carouselWorkspace") &&
                     named_item_visible(workspace, QStringLiteral("carouselSetupSurface"));
            },
            timeout_ms)) {
      return fail(QStringLiteral("Timed out routing to the Carousel workspace."));
    }
    const auto carousel_route_elapsed_ms = carousel_route_timer.elapsed();
    carousel_route_samples.push_back(static_cast<double>(carousel_route_elapsed_ms));
    if (!wait_for_condition(
            [&carousel_input_probe]() { return carousel_input_probe.delivered(); },
            timeout_ms,
            "carousel-route-input-event")) {
      return fail(QStringLiteral("Timed out dispatching the no-op key event during Carousel routing."));
    }
    carousel_input_probe.joinPoster();
    const auto carousel_input_dispatch_delay_ms =
        carousel_input_probe.dispatchDelayMs();
    const auto carousel_recent_input_detected =
        carousel_input_probe.recentInputFilterObserved();
    if (carousel_input_dispatch_delay_ms < 0.0 || !carousel_recent_input_detected) {
      return fail(QStringLiteral("Carousel route input probe was not observed by the UI input filter."));
    }
    carousel_input_dispatch_samples.push_back(carousel_input_dispatch_delay_ms);
    log_stage(
        QStringLiteral("carousel_input_event_dispatch_delay"),
        static_cast<qint64>(std::lround(carousel_input_dispatch_delay_ms)),
        iteration,
        {{QStringLiteral("dispatch_delay_ms_exact"), carousel_input_dispatch_delay_ms},
         {QStringLiteral("route_offset_ms"), carousel_input_probe.routeOffsetMs()},
         {QStringLiteral("route_visible_ms"), carousel_route_elapsed_ms},
         {QStringLiteral("recent_user_input_detected"), carousel_recent_input_detected}});
    root_window->removeEventFilter(&carousel_input_probe);
    process_layout_events(1);
    QMetaObject::invokeMethod(root, "flushQmlFrameSummary");
    root->setProperty("qmlFrameCaptureEnabled", false);
    log_stage(
        QStringLiteral("carousel_route_to_visible"),
        carousel_route_elapsed_ms,
        iteration);

    QMetaObject::invokeMethod(root, "flushQmlFrameSummary");
    root->setProperty("qmlFrameCaptureEnabled", true);
    QMetaObject::invokeMethod(root, "beginQmlFrameCaptureWindow");
    QElapsedTimer timeline_route_timer;
    timeline_route_timer.start();
    const auto timeline_route_started_at = std::chrono::steady_clock::now();
    RouteInputDispatchProbe timeline_input_probe(root_window);
    timeline_input_probe.postNoOpKeyAfter(timeline_route_started_at);
    root->setProperty("currentWorkspace", root->property("timelineWorkspaceIndex"));
    if (!wait_for_condition(
            [&]() {
              auto* workspace = active_workspace(root);
              return workspace != nullptr &&
                     workspace->objectName() == QStringLiteral("timelineWorkspace") &&
                     (named_item_visible(workspace, QStringLiteral("timelineHeaderBar")) ||
                      named_item_visible(workspace, QStringLiteral("timelineSetupSurface")));
            },
            timeout_ms)) {
      return fail(QStringLiteral("Timed out routing to the Timeline workspace."));
    }
    const auto timeline_route_elapsed_ms = timeline_route_timer.elapsed();
    timeline_route_samples.push_back(static_cast<double>(timeline_route_elapsed_ms));
    if (!wait_for_condition(
            [&timeline_input_probe]() { return timeline_input_probe.delivered(); },
            timeout_ms,
            "timeline-route-input-event")) {
      return fail(QStringLiteral("Timed out dispatching the no-op key event during Timeline routing."));
    }
    timeline_input_probe.joinPoster();
    const auto timeline_input_dispatch_delay_ms =
        timeline_input_probe.dispatchDelayMs();
    const auto timeline_recent_input_detected =
        timeline_input_probe.recentInputFilterObserved();
    if (timeline_input_dispatch_delay_ms < 0.0 || !timeline_recent_input_detected) {
      return fail(QStringLiteral("Timeline route input probe was not observed by the UI input filter."));
    }
    timeline_input_dispatch_samples.push_back(timeline_input_dispatch_delay_ms);
    log_stage(
        QStringLiteral("timeline_input_event_dispatch_delay"),
        static_cast<qint64>(std::lround(timeline_input_dispatch_delay_ms)),
        iteration,
        {{QStringLiteral("dispatch_delay_ms_exact"), timeline_input_dispatch_delay_ms},
         {QStringLiteral("route_offset_ms"), timeline_input_probe.routeOffsetMs()},
         {QStringLiteral("route_visible_ms"), timeline_route_elapsed_ms},
         {QStringLiteral("recent_user_input_detected"), timeline_recent_input_detected}});
    root_window->removeEventFilter(&timeline_input_probe);
    process_layout_events(1);
    QMetaObject::invokeMethod(root, "flushQmlFrameSummary");
    root->setProperty("qmlFrameCaptureEnabled", false);
    log_stage(
        QStringLiteral("timeline_route_to_visible"),
        timeline_route_elapsed_ms,
        iteration);

    QElapsedTimer export_route_timer;
    export_route_timer.start();
    root->setProperty("currentWorkspace", root->property("exportWorkspaceIndex"));
    if (!wait_for_condition(
            [&]() {
              auto* workspace = active_workspace(root);
              return workspace != nullptr &&
                     workspace->objectName() == QStringLiteral("exportStudioWorkspace") &&
                     named_item_visible(workspace, QStringLiteral("exportPresetsPanel")) &&
                     named_item_visible(workspace, QStringLiteral("exportSettingsPanel"));
            },
            timeout_ms)) {
      return fail(QStringLiteral("Timed out routing to the Export workspace."));
    }
    export_route_samples.push_back(static_cast<double>(export_route_timer.elapsed()));
    log_stage(
        QStringLiteral("export_route_to_visible"),
        export_route_timer.elapsed(),
        iteration);

    const auto frames_before = frame_swap_count;
    QElapsedTimer frame_timer;
    frame_timer.start();
    root_window->requestUpdate();
    frame_tick_samples.push_back(static_cast<double>(frame_timer.elapsed()));
    log_stage(
        QStringLiteral("qml_frame_tick"),
        frame_timer.elapsed(),
        iteration,
        {{QStringLiteral("frame_swaps_before"), frames_before},
         {QStringLiteral("frame_swaps_after"), frame_swap_count},
         {QStringLiteral("frame_swaps_observed"), frame_swap_count - frames_before}});
  }

  QMetaObject::invokeMethod(root, "flushQmlFrameSummary");

  log_summary(
      QStringLiteral("qml_shell_load_to_ready"),
      summarize_samples({static_cast<double>(shell_elapsed_ms)}),
      1);
  log_summary(
      QStringLiteral("edit_workspace_prewarm_ready"),
      summarize_samples(edit_prewarm_samples),
      static_cast<int>(edit_prewarm_samples.size()));
  log_summary(
      QStringLiteral("library_route_to_visible"),
      summarize_samples(library_samples),
      iterations);
  log_summary(
      QStringLiteral("import_route_to_visible"),
      summarize_samples(import_samples),
      iterations);
  log_summary(
      QStringLiteral("open_photo_to_edit_preview_ready"),
      summarize_samples(open_samples),
      iterations);
  log_summary(
      QStringLiteral("masks_page_to_tool_rail_ready"),
      summarize_samples(masks_samples),
      iterations);
  log_summary(
      QStringLiteral("brush_page_to_hud_ready"),
      summarize_samples(brush_page_samples),
      iterations);
  log_summary(
      QStringLiteral("brush_controller_point_to_qml_frame"),
      summarize_samples(brush_feedback_samples),
      static_cast<int>(brush_feedback_samples.size()));
  log_summary(
      QStringLiteral("brush_controller_point_to_qml_frame_first_use"),
      summarize_samples(brush_feedback_first_use_samples),
      static_cast<int>(brush_feedback_first_use_samples.size()));
  log_summary(
      QStringLiteral("brush_stroke_begin_to_qml_frame_first_use"),
      summarize_samples(brush_stroke_first_use_samples),
      static_cast<int>(brush_stroke_first_use_samples.size()));
  log_summary(
      QStringLiteral("brush_high_fidelity_raster_complete"),
      summarize_samples(brush_raster_completion_samples),
      iterations);
  log_summary(
      QStringLiteral("brush_release_to_qml_commit_visible"),
      summarize_samples(brush_commit_visible_samples),
      iterations);
  log_summary(
      QStringLiteral("tool_adjust_to_qml_draft_scheduled"),
      summarize_samples(tool_draft_samples),
      iterations);
  log_summary(
      QStringLiteral("carousel_route_to_visible"),
      summarize_samples(carousel_route_samples),
      static_cast<int>(carousel_route_samples.size()));
  log_summary(
      QStringLiteral("carousel_input_event_dispatch_delay"),
      summarize_samples(carousel_input_dispatch_samples),
      static_cast<int>(carousel_input_dispatch_samples.size()));
  log_summary(
      QStringLiteral("timeline_route_to_visible"),
      summarize_samples(timeline_route_samples),
      static_cast<int>(timeline_route_samples.size()));
  log_summary(
      QStringLiteral("timeline_input_event_dispatch_delay"),
      summarize_samples(timeline_input_dispatch_samples),
      static_cast<int>(timeline_input_dispatch_samples.size()));
  log_summary(
      QStringLiteral("export_route_to_visible"),
      summarize_samples(export_route_samples),
      static_cast<int>(export_route_samples.size()));
  log_summary(
      QStringLiteral("qml_frame_tick"),
      summarize_samples(frame_tick_samples),
      static_cast<int>(frame_tick_samples.size()),
      {{QStringLiteral("frame_swaps_total"), frame_swap_count}});

  if (!capture_dir.isEmpty()) {
    const auto manifest_path = QDir(capture_dir).filePath(QStringLiteral("manifest.json"));
    QFile manifest_file(manifest_path);
    if (manifest_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      QJsonObject manifest;
      manifest.insert(QStringLiteral("captures"), captures);
      manifest_file.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented));
    }
  }

  photo_editor::desktop::observability::logInfo(
      photoEditorPerfLog(),
      QStringLiteral("benchmark.actual_app_workflow.completed"),
      {
          {QStringLiteral("workflow"), QStringLiteral("actual_app_qml")},
          {QStringLiteral("iterations"), iterations},
          {QStringLiteral("elapsed_ms"), suite_timer.elapsed()},
          {QStringLiteral("qml_shell_load_ms"), shell_elapsed_ms},
          {QStringLiteral("edit_workspace_prewarm_p95_ms"),
           summarize_samples(edit_prewarm_samples).p95_ms},
          {QStringLiteral("library_route_p95_ms"), summarize_samples(library_samples).p95_ms},
          {QStringLiteral("import_route_p95_ms"), summarize_samples(import_samples).p95_ms},
          {QStringLiteral("open_photo_p95_ms"), summarize_samples(open_samples).p95_ms},
          {QStringLiteral("masks_page_p95_ms"), summarize_samples(masks_samples).p95_ms},
          {QStringLiteral("brush_page_p95_ms"), summarize_samples(brush_page_samples).p95_ms},
          {QStringLiteral("brush_feedback_p95_ms"),
           summarize_samples(brush_feedback_samples).p95_ms},
          {QStringLiteral("brush_commit_visible_p95_ms"),
           summarize_samples(brush_commit_visible_samples).p95_ms},
          {QStringLiteral("tool_draft_p95_ms"), summarize_samples(tool_draft_samples).p95_ms},
          {QStringLiteral("carousel_route_p95_ms"),
           summarize_samples(carousel_route_samples).p95_ms},
          {QStringLiteral("carousel_input_dispatch_p95_ms"),
           summarize_samples(carousel_input_dispatch_samples).p95_ms},
          {QStringLiteral("timeline_route_p95_ms"),
           summarize_samples(timeline_route_samples).p95_ms},
          {QStringLiteral("timeline_input_dispatch_p95_ms"),
           summarize_samples(timeline_input_dispatch_samples).p95_ms},
          {QStringLiteral("export_route_p95_ms"),
           summarize_samples(export_route_samples).p95_ms},
          {QStringLiteral("frame_swaps_total"), frame_swap_count},
      });

  std::cout << "actual_app_workflow_iterations=" << iterations << '\n';
  if (!capture_dir.isEmpty()) {
    std::cout << "actual_app_workflow_capture_dir=" << capture_dir.toStdString() << '\n';
  }
  return EXIT_SUCCESS;
}
