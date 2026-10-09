#include "canonical_composite_mask_alpha.h"
#include "desktop_shell_smoke_runner.h"
#include "native_window_test_input.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>

#include <QApplication>
#include <QAccessible>
#include <QColor>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QImage>
#include <QInputMethodEvent>
#include <QJSValue>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLineF>
#include <QMouseEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QCursor>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlIncubator>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRectF>
#include <QRegularExpression>
#include <QScopedPointer>
#include <QScopeGuard>
#include <QStyleHints>
#include <QSettings>
#include <QSet>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QTime>
#include <QUrl>
#include <QVariant>
#include <qqml.h>
#include <QQuickStyle>
#include <QTemporaryDir>
#include <QMutex>
#include <QVector>

#include "app_shell_controller.h"
#include "app_bootstrap.h"
#include "brush_live_preview_item.h"
#include "photo_text_document_style.h"
#include "photo_text_style.h"
#include "photo_paint_surface.h"
#include "photo_render_utils.h"
#include "carousel_document_controller.h"
#include "catalog_asset_model.h"
#include "library_workflow_controller.h"
#include "library_original_image_provider.h"
#include "editor_preview_controller.h"
#include "editor_preview_image_provider.h"
#include "observability.h"
#include "editor_preview_texture.h"
#include "editor_preview_texture_item.h"
#include "edit_adjustment_patch.h"
#include "export_job_controller.h"
#include "job_queue_model.h"
#include "job_store.h"
#include "job_runner.h"
#include "macos_updater.h"
#include "preview_backend_controller.h"
#include "tablet_input_area.h"
#include "timeline_audio_output.h"
#include "timeline_document_controller.h"
#include "timeline_playback_controller.h"
#include "recent_user_input_filter.h"
#include "raw_image_decoder.h"
#include "fixtures/synthetic_dng.h"

void initialize_desktop_shell_test_visual_resources() {
  Q_INIT_RESOURCE(looksy_agent_sprite);
  Q_INIT_RESOURCE(looksy_brand_assets);
  Q_INIT_RESOURCE(shell_visual_test_shaders);
}

namespace photo_editor::desktop {

#if defined(PHOTO_EDITOR_TEST_NATIVE_PREVIEW_TEXTURE)
std::unique_ptr<EditorPreviewTexture> create_metal_editor_preview_texture();
#endif

struct TimelinePlaybackFrameTestAccess {
  static QMediaPlayer* player(TimelinePlaybackController& controller) { return &controller.player_; }
  static QVariantMap readiness(const TimelinePlaybackController& controller) {
    return {
        {QStringLiteral("nativeEnabled"), controller.native_media_enabled_},
        {QStringLiteral("waitingForMedia"), controller.awaiting_media_source_},
        {QStringLiteral("waitingForPosition"), controller.awaiting_position_sync_},
        {QStringLiteral("awaitedPositionMs"), controller.awaited_position_ms_},
        {QStringLiteral("positionMs"), controller.player_.position()},
        {QStringLiteral("durationMs"), controller.player_.duration()},
        {QStringLiteral("seekable"), controller.player_.isSeekable()},
        {QStringLiteral("playerState"), static_cast<int>(controller.player_.playbackState())},
        {QStringLiteral("mediaStatus"), static_cast<int>(controller.player_.mediaStatus())},
        {QStringLiteral("outputAttached"), !controller.video_frame_item_.isNull()},
        {QStringLiteral("decodedFrameAvailable"), controller.video_sink_.videoFrame().isValid()},
        {QStringLiteral("decodedFrameStartUs"), controller.video_sink_.videoFrame().startTime()},
        {QStringLiteral("sequenceSeconds"), controller.timeline_document_->sequencePlayheadSeconds()},
        {QStringLiteral("sourceSeconds"), controller.timeline_document_->currentClipPlayheadSeconds()},
    };
  }
};

struct EditorPreviewControllerTestAccess {
  static bool set_active_mask_definition(EditorPreviewController& controller, const QVariantMap& mask) {
    return controller.updateActiveMaskDefinition(mask);
  }
  static bool enable_native_preview_texture(EditorPreviewController& controller) {
#if defined(PHOTO_EDITOR_TEST_NATIVE_PREVIEW_TEXTURE)
    controller.display_preview_texture_ = create_metal_editor_preview_texture();
    return controller.display_preview_texture_ && controller.display_preview_texture_->available();
#else
    (void)controller;
    return false;
#endif
  }
  static QString recipe_json(const EditorPreviewController& controller) {
    return serialize_edit_recipe_json(controller.currentState());
  }
  static bool brush_commit_running(const EditorPreviewController& controller) {
    return controller.mask_worker_coordinator_.brushRasterCommitWatcher().isRunning();
  }
  static bool brush_commit_active(const EditorPreviewController& controller) {
    return controller.brush_raster_commit_active_;
  }
  static bool brush_commit_scheduled(const EditorPreviewController& controller) {
    return controller.pending_brush_commit_scheduled_;
  }
  static qsizetype pending_brush_commit_count(const EditorPreviewController& controller) {
    return controller.pending_brush_commits_.size();
  }
  static bool edit_transaction_active(const EditorPreviewController& controller) {
    return controller.history_service_.transactionActive();
  }
  static int render_generation(const EditorPreviewController& controller) {
    return controller.preview_session_.generation();
  }
  static std::optional<RenderResult> ready_interactive_render_result(
      const EditorPreviewController& controller) {
    const auto future = controller.interactive_render_watcher_.future();
    if (!future.isFinished() || future.resultCount() <= 0) return std::nullopt;
    return future.result();
  }

  static QString file_path(const EditorPreviewController& controller) {
    return controller.file_path_;
  }

  static void handle_render_result(
      EditorPreviewController& controller,
      const bool full_resolution,
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

photo_editor::desktop::CatalogAssetItem make_asset(
    const QString& asset_id,
    const QString& file_name,
    const QString& thumbnail_source = QString()) {
  photo_editor::desktop::CatalogAssetItem item;
  item.asset_id = asset_id;
  item.camera_label = QStringLiteral("Test Camera");
  item.file_path = QDir::isAbsolutePath(file_name) ? file_name : QStringLiteral("/tmp/%1").arg(file_name);
  item.thumbnail_source = thumbnail_source;
  item.capture_date = QStringLiteral("Apr 8, 2026");
  item.capture_date_value = QDate(2026, 4, 8);
  item.imported_at = QStringLiteral("Apr 8, 2026  9:00 AM");
  item.imported_at_value = QDateTime(QDate(2026, 4, 8), QTime(9, 0));
  item.media_type = QStringLiteral("photo");
  item.online = true;
  return item;
}

bool write_overlay_visual_fixture(const QString& file_path, const QString& fixture_kind) {
  const auto kind = fixture_kind.trimmed().toLower();
  const int width = kind == QStringLiteral("saturated-small") ? 600 : 1200;
  const int height = kind == QStringLiteral("saturated-small") ? 400 : 800;
  QImage image(
      width,
      height,
      kind == QStringLiteral("transparent") ? QImage::Format_ARGB32
                                            : QImage::Format_RGB32);

  for (int y = 0; y < height; ++y) {
    const auto fy = static_cast<double>(y) / static_cast<double>(height - 1);
    for (int x = 0; x < width; ++x) {
      const auto fx = static_cast<double>(x) / static_cast<double>(width - 1);
      QColor color;
      if (kind == QStringLiteral("agent-scene")) {
        const auto subject_head = std::pow((fx - 0.49) / 0.075, 2.0) +
                                  std::pow((fy - 0.34) / 0.105, 2.0) <= 1.0;
        const auto subject_body = std::pow((fx - 0.49) / 0.19, 2.0) +
                                  std::pow((fy - 0.69) / 0.37, 2.0) <= 1.0;
        if ((fx > 0.14 && fx < 0.38 && fy > 0.08 && fy < 0.34) ||
            (fx > 0.72 && fx < 0.96 && fy > 0.06 && fy < 0.38)) {
          const auto glow = static_cast<int>(18.0 * (1.0 - fy));
          color = QColor(244 + glow / 3, 231 + glow / 4, 186 + glow / 2);
        } else if (subject_head) {
          color = QColor(154, 101, 72);
        } else if (subject_body) {
          color = QColor(29, 48, 51);
        } else if (fy > 0.72) {
          color = QColor(
              static_cast<int>(73.0 + 30.0 * fx),
              static_cast<int>(59.0 + 18.0 * fx),
              static_cast<int>(49.0 + 12.0 * fx));
        } else {
          color = QColor(
              static_cast<int>(55.0 + 34.0 * fx + 22.0 * fy),
              static_cast<int>(69.0 + 38.0 * fx + 24.0 * fy),
              static_cast<int>(67.0 + 22.0 * fx + 18.0 * fy));
        }
      } else if (kind == QStringLiteral("bright")) {
        const auto value = static_cast<int>(230.0 + 24.0 * fx + 10.0 * fy);
        color = QColor(qBound(0, value, 255), qBound(0, value + 2, 255), qBound(0, value + 6, 255));
      } else if (kind == QStringLiteral("dark")) {
        const auto value = static_cast<int>(8.0 + 24.0 * fx + 16.0 * fy);
        color = QColor(qBound(0, value, 255), qBound(0, value + 2, 255), qBound(0, value + 5, 255));
      } else if (kind == QStringLiteral("saturated") ||
                 kind == QStringLiteral("saturated-small")) {
        if (x < width / 3) {
          color = QColor(238, static_cast<int>(32 + 180 * fy), 44);
        } else if (x < (width * 2) / 3) {
          color = QColor(32, 214, static_cast<int>(54 + 160 * fx));
        } else {
          color = QColor(static_cast<int>(78 + 130 * fy), 70, 245);
        }
      } else if (kind == QStringLiteral("transparent")) {
        const bool checker = ((x / 72) + (y / 72)) % 2 == 0;
        const auto alpha = checker ? 42 : 172;
        color = checker ? QColor(255, 255, 255, alpha) : QColor(38, 44, 52, alpha);
        if (fx > 0.28 && fx < 0.72 && fy > 0.24 && fy < 0.76) {
          color = QColor(
              static_cast<int>(40 + 160 * fx),
              static_cast<int>(210 - 80 * fy),
              224,
              218);
        }
      } else {
        color = QColor(36, 82, 116);
      }
      image.setPixelColor(x, y, color);
    }
  }

  return image.save(file_path);
}

photo_editor::desktop::PersistentJobRecord make_persistent_job(
    const QString& job_id,
    const QString& job_kind,
    const QString& status,
    const QString& message = QString()) {
  photo_editor::desktop::PersistentJobRecord record;
  record.job_id = job_id;
  record.job_kind = job_kind;
  record.display_label = job_id;
  record.source_path = QStringLiteral("/tmp/%1").arg(job_id);
  record.status = status;
  record.error_message = message;
  record.created_at = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
  record.updated_at = record.created_at;
  return record;
}

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
      if (auto* found = find_named_object_recursive(child_item, object_name, visited); found != nullptr) {
        return found;
      }
    }
  }
  for (auto* child : root->children()) {
    if (auto* found = find_named_object_recursive(child, object_name, visited); found != nullptr) {
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

void collect_named_items_recursive(
    QObject* root,
    const QString& object_name,
    QSet<const QObject*>& visited,
    QVector<QQuickItem*>* items) {
  if (root == nullptr || items == nullptr || visited.contains(root)) {
    return;
  }
  visited.insert(root);
  if (root->objectName() == object_name) {
    if (auto* item = qobject_cast<QQuickItem*>(root); item != nullptr) {
      items->push_back(item);
    }
  }
  if (auto* item = qobject_cast<QQuickItem*>(root); item != nullptr) {
    for (auto* child_item : item->childItems()) {
      collect_named_items_recursive(child_item, object_name, visited, items);
    }
  }
  for (auto* child : root->children()) {
    collect_named_items_recursive(child, object_name, visited, items);
  }
}

QVector<QQuickItem*> find_named_items(QObject* root, const QString& object_name) {
  QVector<QQuickItem*> items;
  QSet<const QObject*> visited;
  collect_named_items_recursive(root, object_name, visited, &items);
  return items;
}

void process_layout_events(int rounds = 3) {
  for (int index = 0; index < rounds; ++index) {
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }
}

bool wait_for_condition(const std::function<bool()>& condition, int timeout_ms = 3000) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    if (condition()) {
      return true;
    }
    QThread::msleep(25);
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  return condition();
}

void trace_smoke_progress(const char* label) {
  if (!qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_TRACE_PROGRESS")) {
    return;
  }
  static QElapsedTimer timer;
  if (!timer.isValid()) {
    timer.start();
  }
  std::cerr << "[smoke] +" << timer.elapsed() << "ms " << label << '\n';
}

bool item_visible(const QQuickItem* item) {
  return item != nullptr && item->isVisible() && item->width() > 0.0 && item->height() > 0.0;
}

QRectF scene_rect(const QQuickItem* item) {
  if (item == nullptr) {
    return {};
  }
  return item->mapRectToScene(QRectF(0.0, 0.0, item->width(), item->height()));
}

bool contains_with_tolerance(const QRectF& outer, const QRectF& inner, double tolerance = 1.0) {
  return inner.left() + tolerance >= outer.left() && inner.top() + tolerance >= outer.top() &&
         inner.right() <= outer.right() + tolerance && inner.bottom() <= outer.bottom() + tolerance;
}

bool overlaps_with_tolerance(const QRectF& left, const QRectF& right, double tolerance = 1.0) {
  return left.right() > right.left() + tolerance && right.right() > left.left() + tolerance &&
         left.bottom() > right.top() + tolerance && right.bottom() > left.top() + tolerance;
}

bool visible_named_items_fit_parent(
    QObject* root,
    const QString& parent_name,
    const QStringList& item_names) {
  auto* parent = find_named_item(root, parent_name);
  if (!item_visible(parent)) {
    return false;
  }
  const auto parent_rect = scene_rect(parent);
  for (const auto& item_name : item_names) {
    auto* item = find_named_item(root, item_name);
    if (item_visible(item) && !contains_with_tolerance(parent_rect, scene_rect(item))) {
      return false;
    }
  }
  return true;
}

bool visible_named_items_do_not_overlap(QObject* root, const QStringList& item_names) {
  QVector<QRectF> rects;
  for (const auto& item_name : item_names) {
    auto* item = find_named_item(root, item_name);
    if (item_visible(item)) {
      rects.push_back(scene_rect(item));
    }
  }
  for (int left = 0; left < rects.size(); ++left) {
    for (int right = left + 1; right < rects.size(); ++right) {
      if (overlaps_with_tolerance(rects.at(left), rects.at(right))) {
        return false;
      }
    }
  }
  return true;
}

bool visible_named_item_instances_do_not_overlap(
    QObject* root,
    const QString& object_name,
    int minimum_visible_count,
    QString* failure_detail = nullptr) {
  QVector<QRectF> rects;
  const auto items = find_named_items(root, object_name);
  for (auto* item : items) {
    if (item_visible(item)) {
      rects.push_back(scene_rect(item));
    }
  }
  if (rects.size() < minimum_visible_count) {
    if (failure_detail != nullptr) {
      *failure_detail = QStringLiteral("%1 visible count %2 < %3")
                            .arg(object_name)
                            .arg(rects.size())
                            .arg(minimum_visible_count);
    }
    return false;
  }
  for (int left = 0; left < rects.size(); ++left) {
    for (int right = left + 1; right < rects.size(); ++right) {
      if (overlaps_with_tolerance(rects.at(left), rects.at(right))) {
        if (failure_detail != nullptr) {
          *failure_detail = QStringLiteral("%1 row %2 overlaps row %3: %4,%5 %6x%7 vs %8,%9 %10x%11")
                                .arg(object_name)
                                .arg(left)
                                .arg(right)
                                .arg(rects.at(left).x())
                                .arg(rects.at(left).y())
                                .arg(rects.at(left).width())
                                .arg(rects.at(left).height())
                                .arg(rects.at(right).x())
                                .arg(rects.at(right).y())
                                .arg(rects.at(right).width())
                                .arg(rects.at(right).height());
        }
        return false;
      }
    }
  }
  return true;
}

void log_visible_named_item_geometry(QObject* root, const QStringList& item_names) {
  for (const auto& item_name : item_names) {
    auto* item = find_named_item(root, item_name);
    if (!item_visible(item)) {
      continue;
    }
    const auto rect = scene_rect(item);
    std::cerr << item_name.toStdString() << " rect=" << rect.x() << "," << rect.y()
              << " " << rect.width() << "x" << rect.height() << '\n';
  }
}

bool named_object_visible_for_proof(QObject* root, const QString& object_name) {
  auto* object = find_named_object(root, object_name);
  if (object == nullptr) {
    return false;
  }
  const auto is_visible = [](QObject* candidate) {
    if (auto* item = qobject_cast<QQuickItem*>(candidate); item != nullptr) {
      return item_visible(item) && item->window() != nullptr;
    }
    const auto visible = candidate->property("visible");
    return !visible.isValid() || visible.toBool();
  };
  if (is_visible(object)) {
    return true;
  }
  const auto items = find_named_items(root, object_name);
  return std::any_of(items.cbegin(), items.cend(), [&](QQuickItem* candidate) {
    return is_visible(candidate);
  });
}

QString visual_proof_file_stem(const QString& state, int width, int height) {
  auto clean_state = state.toLower();
  clean_state.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
  clean_state = clean_state.trimmed();
  while (clean_state.startsWith(QLatin1Char('-'))) {
    clean_state.remove(0, 1);
  }
  while (clean_state.endsWith(QLatin1Char('-'))) {
    clean_state.chop(1);
  }
  if (clean_state.isEmpty()) {
    clean_state = QStringLiteral("state");
  }
  return QStringLiteral("%1-%2x%3").arg(clean_state).arg(width).arg(height);
}

bool capture_visual_proof(
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
  root->setProperty("width", width);
  root->setProperty("height", height);
  process_layout_events(10);
  // Resizing the offscreen root can briefly leave nested Qt Quick Layouts at
  // zero height even after their page transition has been settled. A visual
  // proof must capture the stable rendered state, so allow the same bounded
  // layout convergence window used by the interaction assertions.
  const auto required_objects_visible = [&]() {
    return std::all_of(
        required_objects.cbegin(),
        required_objects.cend(),
        [&](const QString& object_name) {
          return named_object_visible_for_proof(root, object_name);
        });
  };
  if (!required_objects_visible()) {
    wait_for_condition(required_objects_visible, 750);
  }
  QStringList missing_or_hidden;
  for (const auto& object_name : required_objects) {
    if (!named_object_visible_for_proof(root, object_name)) {
      missing_or_hidden.push_back(object_name);
    }
  }
  if (!missing_or_hidden.isEmpty()) {
    std::cerr << "Visual proof state " << state.toStdString()
              << " missing/hidden: " << missing_or_hidden.join(QStringLiteral(", ")).toStdString()
              << '\n';
    return false;
  }
  QImage image;
  const auto capture_matches_size = [&] {
    window->requestUpdate();
    image = window->grabWindow();
    const auto ratio = window->devicePixelRatio();
    return window->width() == width && window->height() == height &&
           image.size() == QSize(qRound(width * ratio), qRound(height * ratio));
  };
  if (!wait_for_condition(capture_matches_size, 2000)) {
    std::cerr << "Visual proof state " << state.toStdString()
              << " capture dimensions did not settle: requested=" << width << 'x' << height
              << " window=" << window->width() << 'x' << window->height()
              << " pixels=" << image.width() << 'x' << image.height()
              << " ratio=" << window->devicePixelRatio() << '\n';
    return false;
  }
  const auto file_name = visual_proof_file_stem(state, width, height) + QStringLiteral(".png");
  const auto file_path = QDir(output_dir).filePath(file_name);
  if (!image.save(file_path)) {
    std::cerr << "Unable to save visual proof capture: " << file_path.toStdString() << '\n';
    return false;
  }
  QJsonObject entry;
  entry.insert(QStringLiteral("state"), state);
  entry.insert(QStringLiteral("width"), width);
  entry.insert(QStringLiteral("height"), height);
  entry.insert(QStringLiteral("pixel_width"), image.width());
  entry.insert(QStringLiteral("pixel_height"), image.height());
  entry.insert(QStringLiteral("device_pixel_ratio"), window->devicePixelRatio());
  entry.insert(QStringLiteral("path"), file_path);
  entry.insert(QStringLiteral("required_objects"), QJsonArray::fromStringList(required_objects));
  if (!metadata.isEmpty()) {
    entry.insert(QStringLiteral("metadata"), metadata);
  }
  manifest->append(entry);
  return true;
}

bool capture_visual_proof_pair(
    QQuickWindow* window,
    QObject* root,
    const QString& output_dir,
    const QString& state,
    const QStringList& required_objects,
    QJsonArray* manifest,
    const QJsonObject& metadata = {}) {
  return capture_visual_proof(
              window,
              root,
              output_dir,
             state,
             1600,
              1000,
              required_objects,
              manifest,
              metadata) &&
         capture_visual_proof(
              window,
              root,
             output_dir,
             state,
             1180,
              820,
              required_objects,
              manifest,
              metadata);
}

bool visible_named_buttons_fit_content(QObject* root, const QStringList& button_names) {
  for (const auto& button_name : button_names) {
    auto* button = find_named_object(root, button_name);
    auto* item = qobject_cast<QQuickItem*>(button);
    if (button == nullptr || item == nullptr) {
      return false;
    }
    if (!item_visible(item)) {
      continue;
    }
    const auto content_fits = button->property("contentFitsControl");
    const auto label_fits = button->property("labelFitsControl");
    if (!content_fits.isValid() || !label_fits.isValid() || !content_fits.toBool() ||
        !label_fits.toBool()) {
      return false;
    }
  }
  return true;
}

bool visible_mini_buttons_fit_content_recursive(
    QObject* root,
    QSet<const QObject*>& visited,
    QString* failing_object_name) {
  if (root == nullptr || visited.contains(root)) {
    return true;
  }
  visited.insert(root);

  if (auto* item = qobject_cast<QQuickItem*>(root); item != nullptr && item_visible(item)) {
    const auto content_fits = root->property("contentFitsControl");
    const auto label_fits = root->property("labelFitsControl");
    if ((content_fits.isValid() || label_fits.isValid()) &&
        (!content_fits.toBool() || !label_fits.toBool())) {
      if (failing_object_name != nullptr) {
        *failing_object_name = root->objectName();
      }
      return false;
    }
    for (auto* child_item : item->childItems()) {
      if (!visible_mini_buttons_fit_content_recursive(child_item, visited, failing_object_name)) {
        return false;
      }
    }
  }

  for (auto* child : root->children()) {
    if (!visible_mini_buttons_fit_content_recursive(child, visited, failing_object_name)) {
      return false;
    }
  }
  return true;
}

bool visible_mini_buttons_fit_content(QObject* root, QString* failing_object_name = nullptr) {
  QSet<const QObject*> visited;
  return visible_mini_buttons_fit_content_recursive(root, visited, failing_object_name);
}

bool visible_named_labels_fit_content(QObject* root, const QStringList& label_names) {
  for (const auto& label_name : label_names) {
    auto* label = find_named_object(root, label_name);
    auto* item = qobject_cast<QQuickItem*>(label);
    if (label == nullptr || item == nullptr) {
      return false;
    }
    if (!item_visible(item)) {
      continue;
    }
    const auto implicit_width = label->property("implicitWidth").toDouble();
    const auto width = item->width();
    const auto elide = label->property("elide");
    const auto truncated = label->property("truncated");
    const auto clip = label->property("clip");
    if (implicit_width > width + 1.0 && (!elide.isValid() || elide.toInt() == 0) &&
        (!truncated.isValid() || !truncated.toBool()) && (!clip.isValid() || !clip.toBool())) {
      return false;
    }
  }
  return true;
}

QString accessible_name_for_object(QObject* object) {
  if (object == nullptr) {
    return {};
  }
  auto* interface = QAccessible::queryAccessibleInterface(object);
  return interface == nullptr ? QString() : interface->text(QAccessible::Name).trimmed();
}

bool send_activation_key(QObject* object, int key = Qt::Key_Space) {
  auto* item = qobject_cast<QQuickItem*>(object);
  if (item == nullptr || !item_visible(item)) {
    return false;
  }
  item->forceActiveFocus(Qt::TabFocusReason);
  QCoreApplication::processEvents();
  if (!item->hasActiveFocus()) {
    return false;
  }
  QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
  QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
  QCoreApplication::sendEvent(item, &press);
  QCoreApplication::sendEvent(item, &release);
  QCoreApplication::processEvents();
  return true;
}

bool send_window_key(
    QQuickWindow* window,
    int key,
    Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  if (window == nullptr) {
    return false;
  }
  QKeyEvent press(QEvent::KeyPress, key, modifiers);
  QKeyEvent release(QEvent::KeyRelease, key, modifiers);
  const auto press_sent = QCoreApplication::sendEvent(window, &press);
  const auto release_sent = QCoreApplication::sendEvent(window, &release);
  QCoreApplication::processEvents();
  return press_sent && release_sent;
}

QSet<QString> literal_shortcut_sequences(const QString& source) {
  QSet<QString> sequences;
  const QRegularExpression sequence_pattern(QStringLiteral("sequence:\\s*\\\"([^\\\"]+)\\\""));
  auto matches = sequence_pattern.globalMatch(source);
  while (matches.hasNext()) {
    const auto match = matches.next();
    const auto sequence = match.captured(1).trimmed();
    if (!sequence.isEmpty()) {
      sequences.insert(sequence);
    }
  }
  return sequences;
}

}  // namespace


int run_desktop_shell_smoke(int argc, char** argv) {
  ::initialize_desktop_shell_test_visual_resources();
  if (!qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_NATIVE_POINTER")) {
    qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
  }
  QQuickStyle::setStyle(QStringLiteral("Basic"));
  QApplication app(argc, argv);
  QApplication::setFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont));
  QCoreApplication::setOrganizationName(QStringLiteral("PhotoEditorTests"));
  QCoreApplication::setApplicationName(QStringLiteral("DesktopShellSmokeTest"));

struct AccessibilityCatalogCleanup {
  QTemporaryDir* runtime_root = nullptr;
  bool enabled = false;

  ~AccessibilityCatalogCleanup() {
    if (!enabled || runtime_root == nullptr) {
      return;
    }
    const auto helper_path = QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
                                 .filePath(QStringLiteral("rust/target/debug/catalog_local_db"));
    if (!QFileInfo(helper_path).isExecutable()) {
      return;
    }

    QProcess process;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.remove(QStringLiteral("PHOTO_EDITOR_DATABASE_URL"));
    environment.insert(QStringLiteral("PHOTO_EDITOR_APP_DATA_DIR"), runtime_root->path());
    process.setProcessEnvironment(environment);
    process.start(helper_path, {QStringLiteral("stop")});
    if (!process.waitForStarted(3000) || !process.waitForFinished(10000) ||
        process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
      if (process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished(1000);
      }
      runtime_root->setAutoRemove(false);
      std::cerr << "Accessibility shard could not stop its isolated catalog; preserving "
                << runtime_root->path().toStdString() << ".\n";
    }
  }
};

// Keep focused ownership-source checks in the sharded source contract.
  const auto run_proof = [&]() -> int {
#include "desktop_shell_smoke/source_contract.inc"
#include "desktop_shell_smoke/shell_and_editing.inc"
#include "desktop_shell_smoke/adjustments_crop_selection.inc"
#include "desktop_shell_smoke/mask_brush_retouch_layers.inc"
#include "desktop_shell_smoke/library_and_application.inc"
  };

  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_NATIVE_POINTER")) {
    QTimer::singleShot(0, &app, [&app, &run_proof]() { app.exit(run_proof()); });
    return app.exec();
  }
  return run_proof();
}
